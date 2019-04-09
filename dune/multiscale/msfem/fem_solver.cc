#include <config.h>

#include <dune/common/exceptions.hh>
#include <dune/common/timer.hh>
#include <dune/multiscale/msfem/coarse_rhs_functional.hh>
#include <dune/multiscale/common/traits.hh>
#include <dune/multiscale/common/grid_creation.hh>
#include <dune/multiscale/problems/selector.hh>
#include <dune/xt/common/logging.hh>
#include <dune/xt/common/timings.hh>
#include <dune/xt/common/configuration.hh>

#include <dune/gdt/spaces/cg.hh>
#include <dune/gdt/discretefunction/default.hh>
#include <dune/gdt/operators/elliptic.hh>
#include <dune/gdt/functionals/l2.hh>
#include <dune/gdt/spaces/constraints.hh>
#include <dune/gdt/assembler/system.hh>
#include <dune/gdt/projections/l2.hh>
#include <dune/gdt/projections/dirichlet.hh>

#include <limits>
#include <sstream>
#include <string>

#include "fem_solver.hh"

namespace Dune {
namespace Multiscale {

Elliptic_FEM_Solver::Elliptic_FEM_Solver(const Problem::ProblemContainer& problem, GridPtrType grid)
  : grid_(grid)
  , space_(grid_->leafGridView())
  , solution_(space_, "fem_solution")
  , problem_(problem)
{
}

Elliptic_FEM_Solver::Elliptic_FEM_Solver(const DMP::ProblemContainer& problem)
  : Elliptic_FEM_Solver(problem, make_fine_grid(problem, nullptr, false))
{
}

CommonTraits::ConstDiscreteFunctionType& Elliptic_FEM_Solver::solve()
{
  apply(solution_);
  return solution_;
}

void Elliptic_FEM_Solver::apply(CommonTraits::DiscreteFunctionType& solution) const
{
  MS_LOG_DEBUG_0 << "Solving linear problem with standard FEM" << std::endl;
  DXTC_TIMINGS.start("fem.apply");
  typedef GDT::Operators::EllipticCG<Problem::DiffusionBase, CommonTraits::LinearOperatorType, CommonTraits::SpaceType>
      EllipticOperatorType;
  CommonTraits::DiscreteFunctionType projected_dirichlet_data = solution;
  typedef CommonTraits::GridViewType GridViewType;

  const auto& boundary_info = problem_.getModelData().boundaryInfo();
  const auto& neumann = problem_.getNeumannData();
  const auto& dirichlet_data = problem_.getDirichletData();

  CommonTraits::LinearOperatorType system_matrix(
      space_.mapper().size(), space_.mapper().size(), EllipticOperatorType::pattern(space_));
  CommonTraits::GdtVectorType rhs_vector(space_.mapper().size());
  auto& solution_vector = solution.vector();
  // left hand side (elliptic operator)
  EllipticOperatorType elliptic_operator(problem_.getDiffusion(), system_matrix, space_);
  // right hand side
  GDT::Functionals::L2Volume<Problem::SourceType, CommonTraits::GdtVectorType, CommonTraits::SpaceType>
      force_functional(problem_.getSource(), rhs_vector, space_);
  GDT::Functionals::L2Face<Problem::NeumannDataBase, CommonTraits::GdtVectorType, CommonTraits::SpaceType>
      neumann_functional(neumann, rhs_vector, space_);
  // dirichlet boundary values
  auto dirichlet_projection_operator = make_localizable_dirichlet_projection_operator(
      space_.grid_layer(), boundary_info, dirichlet_data, projected_dirichlet_data);
  DXTC_TIMINGS.start("fem.assemble");
  // now assemble everything in one grid walk
  GDT::SystemAssembler<CommonTraits::SpaceType> system_assembler(space_);
  system_assembler.append(elliptic_operator);
  system_assembler.append(force_functional);
  system_assembler.append(neumann_functional,
                          new Dune::XT::Grid::ApplyOn::NeumannIntersections<GridViewType>(boundary_info));
  system_assembler.append(*dirichlet_projection_operator, new XT::Grid::ApplyOn::BoundaryEntities<GridViewType>());
  system_assembler.assemble(true);
  DXTC_TIMINGS.stop("fem.assemble");

  DXTC_TIMINGS.start("fem.constraints");
  // substract the operators action on the dirichlet values, since we assemble in H^1 but solve in H^1_0
  CommonTraits::GdtVectorType tmp(space_.mapper().size());
  system_matrix.mv(projected_dirichlet_data.vector(), tmp);
  rhs_vector -= tmp;
  // apply the dirichlet zero constraints to restrict the system to H^1_0
  GDT::DirichletConstraints<typename GridViewType::Intersection> dirichlet_constraints(
      boundary_info, space_.mapper().size(), true);
  system_assembler.append(dirichlet_constraints, new XT::Grid::ApplyOn::BoundaryEntities<GridViewType>());
  system_assembler.assemble(problem_.config().get("threading.smp_constraints", false));
  dirichlet_constraints.apply(system_matrix, rhs_vector);
  DXTC_TIMINGS.stop("fem.constraints");

  // solve the system
  DXTC_TIMINGS.start("fem.solve");
  const XT::LA::Solver<CommonTraits::LinearOperatorType, typename CommonTraits::SpaceType::DofCommunicatorType>
      linear_solver(system_matrix, space_.dof_communicator());
  const std::string type = problem_.config().get("msfem.fine_solver", "bicgstab.ilut");
  auto linear_solver_options = linear_solver.options(type);
  auto verbose = problem_.config().get("msfem.fine_solver.verbose", "2");
  linear_solver_options.set("max_iter", problem_.config().get("msfem.fine_solver.max_iter", "300"), true);
  linear_solver_options.set("precision", problem_.config().get("msfem.fine_solver.precision", "1e-8"), true);
  linear_solver_options.set("verbose", verbose, true);
  linear_solver_options.set("post_check_solves_system", "0", true);
  linear_solver_options.set("preconditioner.anisotropy_dim", CommonTraits::world_dim, true);
  linear_solver_options.set("preconditioner.isotropy_dim", CommonTraits::world_dim, true);
  linear_solver_options.set("smoother.iterations", "1", true);
  linear_solver_options.set("smoother.relaxation_factor", "0.5", true);
  linear_solver_options.set("criterion.max_level", "100", true);
  linear_solver_options.set("criterion.coarse_target", "1000", true);
  linear_solver_options.set("criterion.min_coarse_rate", "1.2", true);
  linear_solver_options.set("criterion.prolong_damp", "1.6", true);
  linear_solver_options.set("criterion.anisotropy_dim", CommonTraits::world_dim, true);
  linear_solver_options.set("criterion.verbose", verbose, true);
  linear_solver_options.set("smoother.verbose", verbose, true);

  linear_solver.apply(rhs_vector, solution_vector, linear_solver_options);
  // add the dirichlet shift to obtain the solution in H^1
  DXTC_TIMINGS.stop("fem.solve");

  solution_vector += projected_dirichlet_data.vector();

  DXTC_TIMINGS.stop("fem.apply");
}

} // namespace Multiscale
}
