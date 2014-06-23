#include <config.h>
#include <dune/stuff/common/parameter/configcontainer.hh>
#include <dune/stuff/fem/functions/integrals.hh>
#include <dune/stuff/common/profiler.hh>
#include <dune/gdt/operators/projections.hh>
#include <dune/gdt/operators/prolongations.hh>
#include <dune/gdt/spaces/constraints.hh>
#include <dune/gdt/functionals/l2.hh>
#include <sstream>

#include "dune/multiscale/msfem/msfem_traits.hh"
#include "coarse_scale_operator.hh"

namespace Dune {
namespace Multiscale {
namespace MsFEM {

template <class MatrixObject, template <class,class> class StencilType = Dune::Fem::DiagonalAndNeighborStencil>
StencilType<typename MatrixObject::DomainSpaceType,
                           typename MatrixObject::DomainSpaceType>
  diagonalAndNeighborStencil(const MatrixObject& object)
{
  return StencilType<typename MatrixObject::DomainSpaceType,
                      typename MatrixObject::RangeSpaceType>(object.domainSpace(), object.rangeSpace());
}


class EllipticCGMsFEM;

class EllipticCGMsFEMTraits
{
public:
  typedef EllipticCGMsFEM derived_type;
  typedef CommonTraits::LinearOperatorType MatrixType;
  typedef CommonTraits::GdtSpaceType SourceSpaceType;
  typedef CommonTraits::GdtSpaceType RangeSpaceType;
  typedef CommonTraits::GridViewType GridViewType;
}; // class EllipticCGTraits

class EllipticCGMsFEM
  : public GDT::Operators::MatrixBased< EllipticCGMsFEMTraits >
  , public GDT::SystemAssembler< EllipticCGMsFEMTraits::RangeSpaceType, EllipticCGMsFEMTraits::GridViewType,
                                 EllipticCGMsFEMTraits::SourceSpaceType>
{
  typedef GDT::SystemAssembler< EllipticCGMsFEMTraits::RangeSpaceType, EllipticCGMsFEMTraits::GridViewType,
  EllipticCGMsFEMTraits::SourceSpaceType > AssemblerBaseType;
  typedef GDT::Operators::MatrixBased< EllipticCGMsFEMTraits > OperatorBaseType;
  typedef GDT::LocalOperator::Codim0Integral<
            GDT::LocalEvaluation::Elliptic< CommonTraits::DiffusionType > > LocalOperatorType;
  typedef GDT::LocalAssembler::Codim0Matrix< LocalOperatorType > LocalAssemblerType;
public:
  typedef EllipticCGMsFEMTraits Traits;

  typedef typename Traits::MatrixType       MatrixType;
  typedef typename Traits::SourceSpaceType  SourceSpaceType;
  typedef typename Traits::RangeSpaceType   RangeSpaceType;
  typedef typename Traits::GridViewType     GridViewType;

  using OperatorBaseType::pattern;

  static Stuff::LA::SparsityPatternDefault pattern(const RangeSpaceType& range_space,
                                                   const SourceSpaceType& source_space,
                                                   const GridViewType& grid_view)
  {
    return range_space.compute_volume_pattern(grid_view, source_space);
  }


  EllipticCGMsFEM(const CommonTraits::DiffusionType& diffusion,
             MatrixType& mtrx,
             const SourceSpaceType& src_spc)
    : OperatorBaseType(mtrx, src_spc)
    , AssemblerBaseType(src_spc)
    , diffusion_(diffusion)
    , local_operator_(diffusion_)
    , local_assembler_(local_operator_)
  {
    this->add(local_assembler_, this->matrix());
  }

  virtual ~EllipticCGMsFEM() {}

  virtual void assemble() DS_OVERRIDE DS_FINAL
  {
    AssemblerBaseType::assemble();
  }

private:
  const CommonTraits::DiffusionType& diffusion_;
  const LocalOperatorType local_operator_;
  const LocalAssemblerType local_assembler_;
}; // class EllipticCG

CoarseScaleOperator::CoarseScaleOperator(const CoarseDiscreteFunctionSpace& coarseDiscreteFunctionSpace,
                                         LocalGridList& subgrid_list, const CommonTraits::DiffusionType& diffusion_op)
  : coarseDiscreteFunctionSpace_(coarseDiscreteFunctionSpace)
  , subgrid_list_(subgrid_list)
  , diffusion_operator_(diffusion_op)
  , petrovGalerkin_(false)
  , global_matrix_(coarseDiscreteFunctionSpace.mapper().size(), coarseDiscreteFunctionSpace.mapper().size(),
                   CommonTraits::EllipticOperatorType::pattern(coarseDiscreteFunctionSpace)) 
{
    DSC_PROFILER.startTiming("msfem.assembleMatrix");

  const bool is_simplex_grid = DSG::is_simplex_grid(coarseDiscreteFunctionSpace_);

  GDT::SystemAssembler<CommonTraits::DiscreteFunctionSpaceType> global_system_assembler_(coarseDiscreteFunctionSpace_);
  EllipticCGMsFEM elliptic_operator(diffusion_operator_, global_matrix_, coarseDiscreteFunctionSpace_);
  global_system_assembler_.add(elliptic_operator);
  global_system_assembler_.assemble();

#if 0 //alter referenzcode
  for (const auto& coarse_grid_entity : DSC::viewRange(*coarseDiscreteFunctionSpace_.grid_view())) {
    int cacheCounter = 0;
    const auto& coarse_grid_geometry = coarse_grid_entity.geometry();
    assert(coarse_grid_entity.partitionType() == InteriorEntity);

    //      DSFe::LocalMatrixProxy<MatrixType> local_matrix(global_matrix_, coarse_grid_entity, coarse_grid_entity);
    auto local_matrix = nullptr;

    const auto& coarse_grid_baseSet = local_matrix.domainBasisFunctionSet();
    const auto numMacroBaseFunctions = coarse_grid_baseSet.size();

    Multiscale::MsFEM::LocalSolutionManager localSolutionManager(coarseDiscreteFunctionSpace_,
                                                                 coarse_grid_entity,
                                                                 subgrid_list_);
    localSolutionManager.load();
    const auto& localSolutions = localSolutionManager.getLocalSolutions();
    assert(localSolutions.size() > 0);

    for (const auto& localGridEntity : DSC::viewRange(*localSolutionManager.space().grid_view())) {
      // ignore overlay elements
      if (!subgrid_list_.covers(coarse_grid_entity, localGridEntity))
        continue;
      const auto& local_grid_geometry = localGridEntity.geometry();

      // higher order quadrature, since A^{\epsilon} is highly variable
      const auto localQuadrature = DSFe::make_quadrature(localGridEntity, localSolutionManager.space());
      const auto numQuadraturePoints = localQuadrature.nop();

      // number of local solutions without the boundary correctors. Those are only needed for the right hand side
      const auto numLocalSolutions = localSolutions.size() - localSolutionManager.numBoundaryCorrectors();
      // evaluate the jacobians of all local solutions in all quadrature points
      std::vector<std::vector<JacobianRangeType>> allLocalSolutionEvaluations(
            numLocalSolutions, std::vector<JacobianRangeType>(localQuadrature.nop(), JacobianRangeType(0.0)));
      for (auto lsNum : DSC::valueRange(numLocalSolutions)) {
        auto& sll = localSolutions[lsNum];
        assert(sll.get());
        //! \todo re-enable
        //            assert(localSolutionManager.space().indexSet().contains(localGridEntity));
        auto localFunction = sll->local_function(localGridEntity);
        localFunction.evaluateQuadrature(localQuadrature, allLocalSolutionEvaluations[lsNum]);
      }

      for (size_t localQuadraturePoint = 0; localQuadraturePoint < numQuadraturePoints; ++localQuadraturePoint) {
        // local (barycentric) coordinates (with respect to entity)
        const auto& local_subgrid_point = localQuadrature.point(localQuadraturePoint);

        auto global_point_in_U_T = local_grid_geometry.global(local_subgrid_point);
        const double weight_local_quadrature = localQuadrature.weight(localQuadraturePoint) *
                                               local_grid_geometry.integrationElement(local_subgrid_point);

        // evaluate the jacobian of the coarse grid base set
        const auto& local_coarse_point = coarse_grid_geometry.local(global_point_in_U_T);
        coarse_grid_baseSet.jacobianAll(local_coarse_point, gradientPhi);

        for (unsigned int i = 0; i < numMacroBaseFunctions; ++i) {
          for (unsigned int j = 0; j < numMacroBaseFunctions; ++j) {
            CoarseDiscreteFunctionSpace::RangeType local_integral(0.0);

            // Compute the gradients of the i'th and j'th local problem solutions
            JacobianRangeType gradLocProbSoli(0.0), gradLocProbSolj(0.0);
            if (is_simplex_grid) {
              assert(allLocalSolutionEvaluations.size() == CommonTraits::GridType::dimension);
              // ∇ Phi_H + ∇ Q( Phi_H ) = ∇ Phi_H + ∂_x1 Phi_H ∇Q( e_1 ) + ∂_x2 Phi_H ∇Q( e_2 )
              for (int k = 0; k < CommonTraits::GridType::dimension; ++k) {
                gradLocProbSoli.axpy(gradientPhi[i][0][k], allLocalSolutionEvaluations[k][localQuadraturePoint]);
                gradLocProbSolj.axpy(gradientPhi[j][0][k], allLocalSolutionEvaluations[k][localQuadraturePoint]);
              }
            } else {
              assert(allLocalSolutionEvaluations.size() == numMacroBaseFunctions);
              gradLocProbSoli = allLocalSolutionEvaluations[i][localQuadraturePoint];
              gradLocProbSolj = allLocalSolutionEvaluations[j][localQuadraturePoint];
            }

            JacobianRangeType reconstructionGradPhii(coarseBaseJacs_[cacheCounter][i]);
            reconstructionGradPhii += gradLocProbSoli;
            JacobianRangeType reconstructionGradPhij(coarseBaseJacs_[cacheCounter][j]);
            reconstructionGradPhij += gradLocProbSolj;
            JacobianRangeType diffusive_flux(0.0);
            diffusion_operator_.diffusiveFlux(global_point_in_U_T, reconstructionGradPhii, diffusive_flux);
            if (petrovGalerkin_)
              local_integral += weight_local_quadrature * (diffusive_flux[0] * coarseBaseJacs_[cacheCounter][j][0]);
            else
              local_integral += weight_local_quadrature * (diffusive_flux[0] * reconstructionGradPhij[0]);

            // add entries
            local_matrix.add(j, i, local_integral);
          }
        }
      }
    }
  } // for


  // set unit rows for dirichlet dofs
  Dune::Multiscale::getConstraintsCoarse(coarseDiscreteFunctionSpace_).applyToOperator(global_matrix_);
#endif
  DSC_PROFILER.stopTiming("msfem.assembleMatrix");
  DSC_LOG_DEBUG << "Time to assemble and communicate MsFEM matrix: " << DSC_PROFILER.getTiming("msfem.assembleMatrix") << "ms\n";
}

void CoarseScaleOperator::apply_inverse(const CoarseScaleOperator::CoarseDiscreteFunction& rhs,
                                        CoarseScaleOperator::CoarseDiscreteFunction& solution) {
  BOOST_ASSERT_MSG(rhs.dofs_valid(), "Coarse scale RHS DOFs need to be valid!");
  DSC_PROFILER.startTiming("msfem.solveCoarse");
  const typename BackendChooser<CoarseDiscreteFunctionSpace>::InverseOperatorType inverse(
      global_matrix_, rhs.space().communicator());/*, 1e-8, 1e-8, DSC_CONFIG_GET("msfem.solver.iterations", rhs.size()),
      DSC_CONFIG_GET("msfem.solver.verbose", false), "bcgs",
      DSC_CONFIG_GET("msfem.solver.preconditioner_type", std::string("sor")));*/
  inverse.apply(rhs.vector(), solution.vector());
  if (!solution.dofs_valid())
    DUNE_THROW(InvalidStateException, "Degrees of freedom of coarse solution are not valid!");
  DSC_PROFILER.stopTiming("msfem.solveCoarse");
  DSC_LOG_DEBUG << "Time to solve coarse MsFEM problem: " << DSC_PROFILER.getTiming("msfem.solveCoarse") << "ms."
               << std::endl;
} // constructor

} // namespace MsFEM {
} // namespace Multiscale {
} // namespace Dune {
