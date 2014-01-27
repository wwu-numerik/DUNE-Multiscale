#include <config.h>
#include <assert.h>
#include <dune/common/timer.hh>
#include <dune/fem/io/parameter.hh>
#include <dune/stuff/fem/localmatrix_proxy.hh>
#include <dune/stuff/common/ranges.hh>
#include <dune/stuff/aliases.hh>
#include <dune/multiscale/tools/misc/linear-lagrange-interpolation.hh>
#include <dune/stuff/fem/functions/integrals.hh>
#include "weighted-clement-operator.hh"

namespace Dune {
namespace Multiscale {
namespace MsFEM {
class MacroMicroGridSpecifier;
}  // namespace MsFEM
}  // namespace Multiscale
}  // namespace Dune

DMM::WeightedClementOperator::WeightedClementOperator(
    const DMM::WeightedClementOperator::DiscreteFunctionSpaceType& space,
    const DMM::WeightedClementOperator::CoarseDiscreteFunctionSpaceType& coarse_space,
    const DMM::WeightedClementOperator::CoarseNodeVectorType& coarse_nodes,
    const DMM::WeightedClementOperator::CoarseBasisFunctionList& coarse_basis,
    const std::map<std::size_t, std::size_t>& global_id_to_internal_id,
    const DMM::MacroMicroGridSpecifier& specifier)
  : discreteFunctionSpace_(space)
  , coarse_space_(coarse_space)
  , dofManager_(DofManagerType::instance(space.grid()))
  , specifier_(specifier)
  , sparsity_pattern_(discreteFunctionSpace_, coarse_space_, specifier_)
  , linearOperator_(discreteFunctionSpace_, coarse_space_)
  , coarse_nodes_(coarse_nodes)
  , coarse_basis_(coarse_basis)
  , global_id_to_internal_id_(global_id_to_internal_id)
  , sequence_(-1)
  , gradCache_(discreteFunctionSpace_.mapper().maxNumDofs())
  , values_(discreteFunctionSpace_.mapper().maxNumDofs()) {}

void DMM::WeightedClementOperator::
operator()(const DMM::WeightedClementOperator::DiscreteFunctionType& u,
           DMM::WeightedClementOperator::CoarseDiscreteFunctionType& w) const {
  systemMatrix().apply(u, w); /*@\label{sto:matrixEval}@*/
}

const DMM::WeightedClementOperator::PreconditionMatrixType&
DMM::WeightedClementOperator::preconditionMatrix() const {
  return systemMatrix().preconditionMatrix();
}

void DMM::WeightedClementOperator::applyTransposed(
    const DMM::WeightedClementOperator::CoarseDiscreteFunctionType& u,
    DMM::WeightedClementOperator::DiscreteFunctionType& w) const {
  systemMatrix().apply_t(u, w); /*@\label{sto:applytransposed}@*/
}

bool DMM::WeightedClementOperator::hasPreconditionMatrix() const {
  return linearOperator_.hasPreconditionMatrix();
}

void DMM::WeightedClementOperator::print(std::ostream& out) const {
  systemMatrix().matrix().print(out);
}

const DMM::WeightedClementOperator::DiscreteFunctionSpaceType&
DMM::WeightedClementOperator::discreteFunctionSpace() const {
  return discreteFunctionSpace_;
}

const DMM::WeightedClementOperator::LinearOperatorType&
DMM::WeightedClementOperator::systemMatrix() const {
  // if stored sequence number it not equal to the one of the
  // dofManager (or space) then the grid has been changed
  // and matrix has to be assembled new
  if (sequence_ != dofManager_.sequence()) /*@\label{sto:sequence}@*/
    assemble();

  return linearOperator_;
}

void DMM::WeightedClementOperator::assemble() const {
  DUNE_THROW(InvalidStateException, "Broken LOD-only code");
#ifdef ENABLE_LOD_ONLY_CODE
  const auto& space = discreteFunctionSpace();

  // reserve memory for matrix
  linearOperator_.reserve(sparsity_pattern_);

  // create timer (also stops time)
  Timer timer;

  // clear matrix
  linearOperator_.clear();

  // coefficients in the matrix that describes the weighted Clement interpolation, i.e. coff[c] = (\int_{\Omega}
  // \Phi_j)^{-1}
  std::vector<double> coff(coarse_space_.size(), 0.0);

  for (const auto& entity : coarse_space_) {
    std::vector<std::size_t> indices;
    coarse_space_.mapper().map(entity, indices);

    // cache geometry of entity
    const auto coarse_geometry = entity.geometry();

    assert(entity.partitionType() == InteriorEntity);

    std::vector<RangeType> phi(coarse_space_.mapper().maxNumDofs());

    // get base function set
    const auto& coarse_baseSet = coarse_space_.basisFunctionSet(entity);
    const auto numBaseFunctions = coarse_baseSet.size();

    // create quadrature of appropriate order
    const auto quadrature = DSFe::make_quadrature(entity, coarse_space_);

    // loop over all quadrature points
    const auto numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint) {
      const auto& local_point = quadrature.point(quadraturePoint);

      const double weight = quadrature.weight(quadraturePoint) * coarse_geometry.integrationElement(local_point);

      coarse_baseSet.evaluateAll(quadrature[quadraturePoint], phi);
      for (unsigned int i = 0; i < numBaseFunctions; ++i) {
        coff[indices[i]] += weight * phi[i];
      }
    }
  }

  for (size_t c = 0; c < coff.size(); ++c) {
    if (coff[c] != 0.0)
      coff[c] = 1.0 / coff[c];
  }

  for (const auto& sp_it : sparsity_pattern_.support()) {
    const auto& entity = *sp_it.first;

    for (const auto& coarse_entity_ptr : sp_it.second) {
      const auto& coarse_entity = *coarse_entity_ptr;
      DSFe::LocalMatrixProxy<LinearOperatorType> localMatrix(linearOperator_, entity, coarse_entity, 1e-12);

      const auto coarse_geometry = coarse_entity.geometry();

      // get base function set
      const auto& coarse_baseSet = coarse_space_.basisFunctionSet(coarse_entity);
      const auto coarse_numBaseFunctions = coarse_baseSet.size();

      const auto& coarse_lagrangepoint_set = specifier_.coarseSpace().lagrangePointSet(coarse_entity);

      // only implemented for 3 Lagrange Points, i.e. piecewise linear functions
      //! @todo Attention: 2D simplex only
      assert(coarse_numBaseFunctions == 3);
      std::vector<RangeType> coarse_phi_corner_0(coarse_numBaseFunctions);
      std::vector<RangeType> coarse_phi_corner_1(coarse_numBaseFunctions);
      std::vector<RangeType> coarse_phi_corner_2(coarse_numBaseFunctions);

      std::vector<DomainType> coarse_corners(coarse_numBaseFunctions);
      std::vector<std::size_t> coarse_global_dof_number;
      coarse_space_.mapper().map(coarse_entity, coarse_global_dof_number);

      // coarse_corner_phi_j[i] = coarse basis function i evaluated in corner j
      coarse_baseSet.evaluateAll(coarse_lagrangepoint_set.point(0), coarse_phi_corner_0);
      coarse_baseSet.evaluateAll(coarse_lagrangepoint_set.point(1), coarse_phi_corner_1);
      coarse_baseSet.evaluateAll(coarse_lagrangepoint_set.point(2), coarse_phi_corner_2);

      for (size_t loc_point = 0; loc_point < coarse_numBaseFunctions; ++loc_point) {
        coarse_corners[loc_point] = coarse_geometry.global(coarse_lagrangepoint_set.point(loc_point));
      }

      // LinearLagrangeInterpolation2D should be eventually replaced by LinearLagrangeFunction2D
      LinearLagrangeInterpolation2D<DiscreteFunctionSpaceType> coarse_basis_interpolation_0(
          coarse_corners[0], coarse_phi_corner_0[0], coarse_corners[1], coarse_phi_corner_1[0], coarse_corners[2],
          coarse_phi_corner_2[0]);

      LinearLagrangeInterpolation2D<DiscreteFunctionSpaceType> coarse_basis_interpolation_1(
          coarse_corners[0], coarse_phi_corner_0[1], coarse_corners[1], coarse_phi_corner_1[1], coarse_corners[2],
          coarse_phi_corner_2[1]);

      LinearLagrangeInterpolation2D<DiscreteFunctionSpaceType> coarse_basis_interpolation_2(
          coarse_corners[0], coarse_phi_corner_0[2], coarse_corners[1], coarse_phi_corner_1[2], coarse_corners[2],
          coarse_phi_corner_2[2]);

      // cache geometry of entity
      const auto geometry = entity.geometry();

      std::vector<RangeType> fine_phi(space.mapper().maxNumDofs());

      // get base function set
      const auto& baseSet = space.basisFunctionSet(entity);
      const auto numBaseFunctions = baseSet.size();
      const auto quadrature = DSFe::make_quadrature(entity, space);

      // loop over all quadrature points
      const auto numQuadraturePoints = quadrature.nop();
      for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint) {
        const auto& local_point = quadrature.point(quadraturePoint);
        const auto global_point = geometry.global(quadrature.point(quadraturePoint));

        const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

        baseSet.evaluateAll(quadrature[quadraturePoint], fine_phi);

        for (unsigned int j = 0; j < numBaseFunctions; ++j) {
          for (unsigned int i = 0; i < coarse_numBaseFunctions; ++i) {

            if (specifier_.is_coarse_boundary_node(coarse_global_dof_number[i])) {
              continue;
            }

            RangeType coarse_phi_i(0.0);

            if (i == 0)
              coarse_basis_interpolation_0.evaluate(global_point, coarse_phi_i);
            if (i == 1)
              coarse_basis_interpolation_1.evaluate(global_point, coarse_phi_i);
            if (i == 2)
              coarse_basis_interpolation_2.evaluate(global_point, coarse_phi_i);

            localMatrix.add(i, j, weight * coff[coarse_global_dof_number[i]] * coarse_phi_i * fine_phi[j]);

          }
        }
      }
    }
  }

  // get elapsed time
  const double assemblyTime = timer.elapsed();
  // in verbose mode print times
  if (Fem::Parameter::verbose())
    std::cout << "Time to assemble weighted clement operator: " << assemblyTime << "s" << std::endl;

  // get grid sequence number from space (for adaptive runs)    /*@LST0S@*/
  sequence_ = dofManager_.sequence();
#endif // ENABLE_LOD_ONLY_CODE
}

void DMM::WeightedClementOperator::boundaryTreatment() const {
  for (const auto& entity : discreteFunctionSpace_) {
    for (const auto& coarse_entity : coarse_space_) {
      if (!DSG::entities_identical(entity, coarse_entity))
        continue;

      // if entity has boundary intersections
      if (entity.hasBoundaryIntersections()) {
        // get local matrix from matrix object
        DSFe::LocalMatrixProxy<LinearOperatorType> localMatrix(linearOperator_, entity, coarse_entity);

        const auto& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);

        const auto endiit = discreteFunctionSpace_.gridPart().iend(entity);
        for (auto iit = discreteFunctionSpace_.gridPart().ibegin(entity); iit != endiit; ++iit) {
          if (iit->neighbor()) // if there is a neighbor entity
            continue;

          const int face = (*iit).indexInInside();
          for (const auto& lp : DSC::lagrangePointSetRange(lagrangePointSet, face))
            localMatrix.unitRow(lp);
        }
      }
    }
  }
}
