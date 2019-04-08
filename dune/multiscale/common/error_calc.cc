#include <config.h>

#include "error_calc.hh"

#include <assert.h>
#include <boost/filesystem/fstream.hpp>
#include <iostream>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <utility>

#include <dune/gdt/assembler/system.hh>
#include <dune/gdt/discretefunction/default.hh>
#include <dune/gdt/products/l2.hh>
#include <dune/gdt/products/h1.hh>
#include <dune/gdt/operators/prolongations.hh>

#include <dune/multiscale/common/traits.hh>
#include <dune/multiscale/common/grid_creation.hh>
#include <dune/multiscale/problems/base.hh>
#include <dune/multiscale/msfem/localsolution_proxy.hh>
#include <dune/multiscale/msfem/proxygridview.hh>
#include <dune/multiscale/msfem/fem_solver.hh>
#include <dune/multiscale/problems/selector.hh>
#include <dune/multiscale/tools/misc/outputparameter.hh>
#include <dune/xt/common/parallel/partitioner.hh>

#include <dune/xt/grid/information.hh>
#include <dune/xt/common/filesystem.hh>
#include <dune/xt/common/configuration.hh>
#include <dune/multiscale/common/heterogenous.hh>
#include <dune/multiscale/msfem/fem_solver.hh>

using namespace Dune::Multiscale;
namespace DGP = Dune::GDT::Products;
typedef DSFu::Difference<Problem::ExactSolutionType, CommonTraits::ConstDiscreteFunctionType> DifferenceType;
typedef DSFu::Difference<CommonTraits::ConstDiscreteFunctionType, CommonTraits::ConstDiscreteFunctionType>
    DiscreteDifferenceType;
typedef DGP::L2Localizable<CommonTraits::InteriorGridViewType, DifferenceType> L2ErrorAnalytical;
typedef DGP::L2Localizable<CommonTraits::InteriorGridViewType, DiscreteDifferenceType> L2ErrorDiscrete;
typedef DGP::H1SemiLocalizable<CommonTraits::InteriorGridViewType, DifferenceType> H1sErrorAnalytical;
typedef DGP::H1SemiLocalizable<CommonTraits::InteriorGridViewType, DiscreteDifferenceType> H1sErrorDiscrete;
typedef DGP::L2Localizable<CommonTraits::InteriorGridViewType, CommonTraits::ConstDiscreteFunctionType> DiscreteL2;


double surface_flow_gdt(const CommonTraits::GridType& grid,
                        const CommonTraits::ConstDiscreteFunctionType& solution,
                        const DMP::ProblemContainer& problem);

void solution_output(const DMP::ProblemContainer& problem,
                     const CommonTraits::ConstDiscreteFunctionType& solution,
                     std::string name = "msfem_solution_")
{
  using namespace Dune;

  Dune::Multiscale::OutputParameters outputparam(problem.config());
  outputparam.set_prefix(name);
  solution.visualize(outputparam.fullpath(solution.name()));
}
template <typename L, typename R>
void solution_output(const DMP::ProblemContainer& problem,
                     const DSFu::Difference<L, R>& solution,
                     const CommonTraits::GridViewType& view,
                     std::string name)
{
  using namespace Dune;

  Dune::Multiscale::OutputParameters outputparam(problem.config());
  outputparam.set_prefix(name);
  solution.visualize(view, outputparam.fullpath(solution.name()));
}
void data_output(const DMP::ProblemContainer& problem, const CommonTraits::GridViewType& gridPart)
{
  using namespace Dune;
  Dune::Multiscale::OutputParameters outputparam(problem.config());

  if (problem.getModelData().hasExactSolution()) {
    const auto& u = problem.getExactSolution();
    outputparam.set_prefix("exact_solution");
    u.visualize(gridPart, outputparam.fullpath(u.name()));
  }
}

Dune::Multiscale::ErrorCalculator::ErrorCalculator(const DMP::ProblemContainer& problem,
                                                   const std::unique_ptr<LocalsolutionProxy>& msfem_solution,
                                                   CommonTraits::ConstDiscreteFunctionType* fem_solution)
  : problem_(problem)
  , msfem_solution_(msfem_solution)
  , fem_solution_(fem_solution)
  , timing_("error.fem+msfem")
{
  assert(fem_solution_);
}

ErrorCalculator::ErrorCalculator(const DMP::ProblemContainer& problem,
                                 const std::unique_ptr<LocalsolutionProxy>& msfem_solution)
  : problem_(problem)
  , msfem_solution_(msfem_solution)
  , fem_solution_(nullptr)
  , timing_("error.msfem")
{
  assert(msfem_solution_);
  if (problem.config().get("msfem.fem_comparison", false)) {
    fem_solver_ = Dune::XT::Common::make_unique<Elliptic_FEM_Solver>(problem);
    // try {
      fem_solution_ = &fem_solver_->solve();
    // } catch (Dune::Exception& e) {
    //   MS_LOG_ERROR << "fine CGFEM solution failed: " << e.what();
    //   fem_solution_ = nullptr;
    // }
  }
}

void match_check(const CommonTraits::GridType& coarse_grid, const CommonTraits::GridType& fine_grid)
{
  const auto fine_view = fine_grid.leafGridView();
  const auto coarse_view = coarse_grid.leafGridView();
  const auto coarse_dimensions = Dune::XT::Grid::dimensions(coarse_view);
  const auto fine_dimensions = Dune::XT::Grid::dimensions(fine_view);
  for (const auto i : Dune::XT::Common::value_range(CommonTraits::world_dim)) {
    const bool match =
        Dune::XT::Common::FloatCmp::eq(coarse_dimensions.coord_limits[i].min(), fine_dimensions.coord_limits[i].min())
        && Dune::XT::Common::FloatCmp::eq(coarse_dimensions.coord_limits[i].max(),
                                          fine_dimensions.coord_limits[i].max());
    if (!match) {
      DUNE_THROW(Dune::InvalidStateException,
                 "Coarse and fine mesh do not match after load balancing, do \
                 you use different refinements in different spatial dimensions?\n"
                     << coarse_dimensions.coord_limits[i]
                     << " | "
                     << fine_dimensions.coord_limits[i]);
    }
  }
}

std::map<std::string, double> Dune::Multiscale::ErrorCalculator::print(std::ostream& out)
{
  using namespace std;
  using namespace DSC;
  out << std::endl << "The L2 errors:" << std::endl << std::endl;

  const size_t over_integrate = problem_.config().get("global.error.over_integrate", 0u);

  auto grids = make_grids(problem_);
  const auto coarse_grid = grids.first;
  const auto fine_grid = grids.second;
  if (!fem_solution_)
    assert(fine_grid);
  const auto fine_space =
      fem_solution_ ? fem_solution_->space() : CommonTraits::SpaceChooserType::make_space(*fine_grid);
  const auto fine_interior_view =
      fine_space.grid_view().grid().template leafGridView<CommonTraits::InteriorBorderPartition>();
  Dune::XT::Common::IndexSetPartitioner<CommonTraits::InteriorGridViewType> ip(fine_interior_view.indexSet());
  SeedListPartitioning<typename CommonTraits::InteriorGridViewType::Grid, 0> partitioning(fine_interior_view, ip);
  GDT::SystemAssembler<CommonTraits::SpaceType, CommonTraits::InteriorGridViewType> system_assembler(
      fine_space, fine_interior_view);
  const auto& grid_view = fine_space.grid_view();

  CommonTraits::DiscreteFunctionType projected_coarse_fem_solution(fine_space);
  Elliptic_FEM_Solver coarse_fem_solver(problem_, coarse_grid);
  try {
    auto& coarse_fem_solution = coarse_fem_solver.solve();
    const Dune::GDT::Operators::LagrangeProlongation<CommonTraits::GridViewType> prolongation_operator(
        fine_space.grid_view());
    prolongation_operator.apply(coarse_fem_solution, projected_coarse_fem_solution);
    if (problem_.config().get("global.vtk_output", false))
      solution_output(problem_, coarse_fem_solution, "coarse-cg-fem_solution_");
  } catch (Dune::Exception& e) {
    MS_LOG_ERROR << "coarse CGFEM solution failed: " << e.what();
    fem_solution_ = nullptr;
  }

  CommonTraits::DiscreteFunctionType fine_msfem_solution(fine_space, "MsFEM_Solution");
  if (msfem_solution_) {
    match_check(*coarse_grid, *fine_grid);
    MsFEMProjection::project(*msfem_solution_, fine_msfem_solution);
    if (problem_.config().get("global.vtk_output", false)) {
      MS_LOG_INFO_0 << "Solution output for MsFEM Solution." << std::endl;
      data_output(problem_, fine_space.grid_view());
      solution_output(problem_, fine_msfem_solution);
    }
    const auto space = CommonTraits::SpaceChooserType::make_space(*coarse_grid);
    CommonTraits::DiscreteFunctionType coarse_fun(space, "MsFEM_Solution_coarse");
    const auto flow = surface_flow_gdt(*coarse_grid, coarse_fun, problem_);
    MS_LOG_ERROR_0 << "FLOW " << flow << std::endl;
  }

  const string msfem_exact = "msfem_exact", fem_exact = "fem_exact", coarse_fem_exact = "coarse_fem_exact",
               msfem_fem = "msfem_fem", msfem_coarse_fem = "msfem_coarse_fem";
  unordered_map<string, DifferenceType> differences;
  unordered_map<string, DiscreteDifferenceType> discrete_differences;
  unordered_map<string, L2ErrorAnalytical> l2_analytical_errors;
  unordered_map<string, H1sErrorAnalytical> h1s_analytical_errors;
  unordered_map<string, L2ErrorDiscrete> l2_discrete_errors;
  unordered_map<string, H1sErrorDiscrete> h1s_discrete_errors;
  std::unique_ptr<DiscreteL2> l2_msfem;
  constexpr auto pcw = std::piecewise_construct_t();

  //! ----------------- compute L2- and H1- errors -------------------
  if (problem_.getModelData().hasExactSolution()) {
    const auto& u = problem_.getExactSolution();

    if (msfem_solution_) {
      const auto name = forward_as_tuple(msfem_exact);
      const auto& difference =
          map_emplace(differences, pcw, name, forward_as_tuple(u, fine_msfem_solution)).first->second;
      const auto product_args = forward_as_tuple(fine_interior_view, difference, over_integrate);
      system_assembler.add(map_emplace(l2_analytical_errors, pcw, name, product_args).first->second);
      system_assembler.add(map_emplace(h1s_analytical_errors, pcw, name, product_args).first->second);
    }

    if (fem_solution_) {
      const auto name = forward_as_tuple(fem_exact);
      const auto& difference = map_emplace(differences, pcw, name, forward_as_tuple(u, *fem_solution_)).first->second;
      const auto product_args = forward_as_tuple(fine_interior_view, difference, over_integrate);
      system_assembler.add(map_emplace(l2_analytical_errors, pcw, name, product_args).first->second);
      system_assembler.add(map_emplace(h1s_analytical_errors, pcw, name, product_args).first->second);
    }
    const auto name = forward_as_tuple(coarse_fem_exact);
    const auto& difference =
        map_emplace(differences, pcw, name, forward_as_tuple(u, projected_coarse_fem_solution)).first->second;
    const auto product_args = forward_as_tuple(fine_interior_view, difference, over_integrate);
    system_assembler.add(map_emplace(l2_analytical_errors, pcw, name, product_args).first->second);
    system_assembler.add(map_emplace(h1s_analytical_errors, pcw, name, product_args).first->second);
  }

  if (msfem_solution_) {
    l2_msfem = Dune::XT::Common::make_unique<DiscreteL2>(fine_interior_view, fine_msfem_solution, over_integrate);
    system_assembler.add(*l2_msfem);
    {
      const auto name = forward_as_tuple(msfem_coarse_fem);
      const auto& difference =
          map_emplace(
              discrete_differences, pcw, name, forward_as_tuple(fine_msfem_solution, projected_coarse_fem_solution))
              .first->second;
      const auto product_args = forward_as_tuple(fine_interior_view, difference, over_integrate);
      system_assembler.add(map_emplace(l2_discrete_errors, pcw, name, product_args).first->second);
      system_assembler.add(map_emplace(h1s_discrete_errors, pcw, name, product_args).first->second);
    }
    if (fem_solution_) {
      const auto name = forward_as_tuple(msfem_fem);
      const auto& difference =
          map_emplace(discrete_differences, pcw, name, forward_as_tuple(fine_msfem_solution, *fem_solution_))
              .first->second;
      const auto product_args = forward_as_tuple(fine_interior_view, difference, over_integrate);
      system_assembler.add(map_emplace(l2_discrete_errors, pcw, name, product_args).first->second);
      system_assembler.add(map_emplace(h1s_discrete_errors, pcw, name, product_args).first->second);
    }
  }

  system_assembler.assemble(partitioning);

  std::map<std::string, double> csv;
  if (problem_.getModelData().hasExactSolution()) {
    if (msfem_solution_) {
      const auto msfem_error = std::sqrt(l2_analytical_errors.at(msfem_exact).apply2());
      out << "|| u_msfem - u_exact ||_L2 =  " << msfem_error << std::endl;
      const auto h1_msfem_error = std::sqrt(h1s_analytical_errors.at(msfem_exact).apply2());
      out << "|| u_msfem - u_exact ||_H1s =  " << h1_msfem_error << std::endl << std::endl;

      csv[msfem_exact + "_L2"] = msfem_error;
      csv[msfem_exact + "_H1s"] = h1_msfem_error;
    }

    if (fem_solution_) {
      const auto fem_error = std::sqrt(l2_analytical_errors.at(fem_exact).apply2());
      out << "|| u_fem_h - u_exact ||_L2 =  " << fem_error << std::endl;
      const auto h1_fem_error = std::sqrt(h1s_analytical_errors.at(fem_exact).apply2());
      out << "|| u_fem_h - u_exact ||_H1s =  " << h1_fem_error << std::endl << std::endl;

      csv[fem_exact + "_L2"] = fem_error;
      csv[fem_exact + "_H1s"] = h1_fem_error;
    }
    const auto fem_error = std::sqrt(l2_analytical_errors.at(coarse_fem_exact).apply2());
    out << "|| u_fem_H - u_exact ||_L2 =  " << fem_error << std::endl;
    const auto h1_fem_error = std::sqrt(h1s_analytical_errors.at(coarse_fem_exact).apply2());
    out << "|| u_fem_H - u_exact ||_H1s =  " << h1_fem_error << std::endl << std::endl;

    csv[coarse_fem_exact + "_L2"] = fem_error;
    csv[coarse_fem_exact + "_H1s"] = h1_fem_error;
  }

  if (msfem_solution_) {
    const auto norm = std::sqrt(l2_msfem->apply2());
    out << "|| u_msfem ||_L2 =  " << norm << std::endl;
    csv["msfem_L2"] = norm;
    const auto fem_error = std::sqrt(l2_discrete_errors.at(msfem_coarse_fem).apply2());
    out << "|| u_fem_H - u_msfem ||_L2 =  " << fem_error << std::endl;
    out << "|| u_fem_H - u_msfem ||_L2 / || u_msfem ||_L2 =  " << fem_error / norm << std::endl;
    const auto h1_fem_error = std::sqrt(h1s_discrete_errors.at(msfem_coarse_fem).apply2());
    out << "|| u_fem_H - u_msfem ||_H1s =  " << h1_fem_error << std::endl << std::endl;

    csv[msfem_coarse_fem + "_L2"] = fem_error;
    csv[msfem_coarse_fem + "_H1s"] = h1_fem_error;

    if (fem_solution_) {
      const auto approx_msfem_error = std::sqrt(l2_discrete_errors.at(msfem_fem).apply2());
      if (std::abs(norm) > 1e-12)
        out << "|| u_msfem - u_fem ||_L2 / || u_msfem ||_L2 =  " << approx_msfem_error / norm << std::endl;
      else
        out << "|| u_msfem - u_fem ||_L2 =  " << approx_msfem_error << std::endl;

      const auto h1_approx_msfem_error = std::sqrt(h1s_discrete_errors.at(msfem_fem).apply2());
      out << "|| u_msfem - u_fem ||_H1s =  " << h1_approx_msfem_error << std::endl << std::endl;

      csv[msfem_fem + "_L2"] = approx_msfem_error;
      csv[msfem_fem + "_H1s"] = h1_approx_msfem_error;
    }
  }

  if (problem_.config().get("global.vtk_output", false)) {
    MS_LOG_INFO_0 << "Differences output for MsFEM Solution." << std::endl;
    for (const auto& mpair : differences)
      solution_output(problem_, mpair.second, grid_view, mpair.first);
    for (const auto& mpair : discrete_differences)
      solution_output(problem_, mpair.second, grid_view, mpair.first);
    if (fem_solution_)
      solution_output(problem_, *fem_solution_, "fine-cg-fem_solution_");
  }

  std::unique_ptr<boost::filesystem::ofstream> csvfile(Dune::XT::Common::make_ofstream(
      std::string(problem_.config().get("global.datadir", "data/")) + std::string("/errors.csv")));
  const std::string sep(",");
  for (const auto& key_val : csv) {
    *csvfile << key_val.first << sep;
  }
  *csvfile << std::endl;
  for (const auto& key_val : csv) {
    *csvfile << key_val.second << sep;
  }
  *csvfile << std::endl;
  return csv;
}

double surface_flow_gdt(const Dune::Multiscale::CommonTraits::GridType& grid,
                        const Dune::Multiscale::CommonTraits::ConstDiscreteFunctionType& solution,
                        const DMP::ProblemContainer& problem)
{
  using namespace Dune::Multiscale;
  const auto gv = grid.leafGridView();

  // Constants and types
  constexpr auto dim = CommonTraits::world_dim;
  typedef double REAL; // TODO read from input
  typedef typename Dune::FieldVector<REAL, dim> FV; // point on cell
  typedef typename Dune::FieldMatrix<REAL, dim, dim> FM; // point on cell
  typedef typename Dune::FieldMatrix<REAL, 1, dim> Grad; // point on cell
  typedef typename Dune::QuadratureRule<REAL, dim - 1> QR;
  typedef typename Dune::QuadratureRules<REAL, dim - 1> QRS;

  const auto& diffusion = problem.getDiffusion();

  // Quadrature rule
  auto iCell = gv.template begin<0, Dune::Interior_Partition>();
  auto iFace = gv.ibegin(*iCell);
  const QR& rule = QRS::rule(iFace->geometry().type(), 2); // TODO order as para

  // Loop over cells
  REAL localFlux(0);
  for (iCell = gv.template begin<0, Dune::Interior_Partition>();
       iCell != gv.template end<0, Dune::Interior_Partition>();
       ++iCell) {
    // Loop over interfaces
    const auto local_solution = solution.local_function(*iCell);
    for (iFace = gv.ibegin(*iCell); iFace != gv.iend(*iCell); ++iFace) {
      if (iFace->boundary() && iFace->geometry().center()[0] == 0) {
        double area = iFace->geometry().volume();
        // Loop over gauss points
        for (auto iGauss = rule.begin(); iGauss != rule.end(); ++iGauss) {
          FV pos = iFace->geometry().global(iGauss->position());
          Grad grad;
          FM diff;
          diffusion.evaluate(pos, diff);
          local_solution->jacobian(pos, grad);
          localFlux -= iGauss->weight() * area * diff[0][0] * grad[0][0];
        }
      }
    }
  }
  localFlux = grid.comm().sum(localFlux);
  return localFlux;
}
