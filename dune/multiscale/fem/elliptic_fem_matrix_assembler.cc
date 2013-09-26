#include <dune/fem/solver/oemsolver/oemsolver.hh>
#include <dune/fem/operator/common/operator.hh>
#include <dune/fem/operator/discreteoperatorimp.hh>
#include <dune/fem/operator/2order/lagrangematrixsetup.hh>
#include <dune/fem/operator/matrix/spmatrix.hh>

#include <dune/stuff/common/ranges.hh>
#include <dune/stuff/fem/matrix_object.hh>
#include <dune/stuff/fem/localmatrix_proxy.hh>
#include <dune/multiscale/common/righthandside_assembler.hh>
#include <dune/multiscale/problems/base.hh>

namespace Dune {
namespace Multiscale {
namespace FEM {

template <class DiscreteFunctionImp, class DiffusionImp>
void DiscreteEllipticOperator<DiscreteFunctionImp, DiffusionImp>::operator()(const DiscreteFunctionImp& /*u*/,
                                                                             DiscreteFunctionImp& /*w*/) const {
  DUNE_THROW(Dune::NotImplemented,
             "the ()-operator of the DiscreteEllipticOperator class is not yet implemented and still a dummy.");
} // ()

template <class DiscreteFunctionImp, class DiffusionImp>
template <class MatrixType>
void DiscreteEllipticOperator<DiscreteFunctionImp, DiffusionImp>::assemble_matrix(MatrixType& global_matrix,
                                                                                  bool boundary_treatment) const {
  //!TODO diagonal stencil would be enough
  DSFe::reserve_matrix(global_matrix);
  global_matrix.clear();

  std::vector<typename BaseFunctionSet::JacobianRangeType> gradient_phi(discreteFunctionSpace_.mapper().maxNumDofs());

  // micro scale base function:
  std::vector<RangeType> phi(discreteFunctionSpace_.mapper().maxNumDofs());
  for (const auto& entity : discreteFunctionSpace_) {
    const auto& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    DSFe::LocalMatrixProxy<MatrixType> local_matrix(global_matrix, entity, entity);

    const auto& baseSet = local_matrix.domainBasisFunctionSet();
    const auto numBaseFunctions = baseSet.size();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const auto quadrature = make_quadrature(entity, discreteFunctionSpace_);
    const auto numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint) {
      // local (barycentric) coordinates (with respect to entity)
      const auto& local_point = quadrature.point(quadraturePoint);
      const auto global_point = geometry.global(local_point);
      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      baseSet.jacobianAll(quadrature[quadraturePoint], gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);
      typename BaseFunctionSet::JacobianRangeType diffusion_in_gradient_phi;
      for (unsigned int i = 0; i < numBaseFunctions; ++i) {
        // A( \nabla \phi ) // diffusion operator evaluated in (x,\nabla \phi)
        diffusion_operator_.diffusiveFlux(global_point, gradient_phi[i], diffusion_in_gradient_phi);
        for (unsigned int j = 0; j < numBaseFunctions; ++j) {
          local_matrix.add(j, i, weight * (diffusion_in_gradient_phi[0] * gradient_phi[j][0]));

          if (lower_order_term_) {
            RangeType F_i;
            lower_order_term_->evaluate(global_point, phi[i], gradient_phi[i], F_i);
            local_matrix.add(j, i, weight * F_i * phi[j][0]);
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment) {
    // set unit rows for dirichlet dofs
    const auto boundary = Problem::getModelData()->boundaryInfo();
    DirichletConstraints<DiscreteFunctionSpace> constraints(*boundary, discreteFunctionSpace_);
    constraints.applyToOperator(global_matrix);
  }
  global_matrix.communicate();
} // assemble_matrix

template <class DiscreteFunctionImp, class DiffusionImp>
template <class MatrixType, class HostDiscreteFunctionSpaceType>
void DiscreteEllipticOperator<DiscreteFunctionImp, DiffusionImp>::assemble_matrix(
    MatrixType& global_matrix, HostDiscreteFunctionSpaceType& hostSpace, bool boundary_treatment) const {
  global_matrix.reserve();
  global_matrix.clear();

  std::vector<typename BaseFunctionSet::JacobianRangeType> gradient_phi(discreteFunctionSpace_.mapper().maxNumDofs());

  // micro scale base function:
  std::vector<RangeType> phi(discreteFunctionSpace_.mapper().maxNumDofs());

  for (const Entity& entity : discreteFunctionSpace_) {
    const auto& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    auto local_matrix = global_matrix.localMatrix(entity, entity);

    const auto& baseSet = local_matrix.domainBaseFunctionSet();
    const auto numBaseFunctions = baseSet.numBaseFunctions();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const auto quadrature = make_quadrature(entity, discreteFunctionSpace_);
    const size_t numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint) {
      // local (barycentric) coordinates (with respect to entity)
      const auto& local_point = quadrature.point(quadraturePoint);
      const auto global_point = geometry.global(local_point);
      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      // transposed of the the inverse jacobian
      const auto& inverse_jac = geometry.jacobianInverseTransposed(local_point);
      baseSet.jacobianAll(quadrature[quadraturePoint], inverse_jac, gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);

      for (unsigned int i = 0; i < numBaseFunctions; ++i) {
        // A( \nabla \phi ) // diffusion operator evaluated in (x,\nabla \phi)
        typename BaseFunctionSet::JacobianRangeType diffusion_in_gradient_phi;
        diffusion_operator_.diffusiveFlux(global_point, gradient_phi[i], diffusion_in_gradient_phi);
        for (unsigned int j = 0; j < numBaseFunctions; ++j) {
          local_matrix.add(j, i, weight * (diffusion_in_gradient_phi[0] * gradient_phi[j][0]));

          if (lower_order_term_) {
            RangeType F_i;
            lower_order_term_->evaluate(global_point, phi[i], gradient_phi[i], F_i);
            local_matrix.add(j, i, weight * F_i * phi[j][0]);
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment) {
    const auto& hostGridPart = hostSpace.gridPart();
    const auto& subGrid = discreteFunctionSpace_.grid();
    for (const auto& entity : discreteFunctionSpace_) {
      const auto host_entity_pointer = subGrid.template getHostEntity<0>(entity);
      const auto& host_entity = *host_entity_pointer;

      auto local_matrix = global_matrix.localMatrix(entity, entity);
      const auto& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);
      const auto iend = hostGridPart.iend(host_entity);
      for (auto iit = hostGridPart.ibegin(host_entity); iit != iend; ++iit) {
        if (iit->neighbor()) // if there is a neighbor entity
        {
          // check if the neighbor entity is in the subgrid
          const auto neighborHostEntityPointer = iit->outside();
          const auto& neighborHostEntity = *neighborHostEntityPointer;
          if (subGrid.template contains<0>(neighborHostEntity)) {
            continue;
          }
        }

        const int face = (*iit).indexInInside();
        const FaceDofIterator fdend = lagrangePointSet.template endSubEntity<1>(face);
        for (FaceDofIterator fdit = lagrangePointSet.template beginSubEntity<1>(face); fdit != fdend; ++fdit)
          local_matrix.unitRow(*fdit);
      }
    }
  }
  global_matrix.communicate();
} // assemble_matrix

template <class DiscreteFunctionImp, class DiffusionImp>
template <class MatrixType>
void DiscreteEllipticOperator<DiscreteFunctionImp, DiffusionImp>::assemble_jacobian_matrix(
    DiscreteFunction& disc_func, MatrixType& global_matrix, bool boundary_treatment) const {
  global_matrix.reserve(DSFe::diagonalAndNeighborStencil(global_matrix));
  global_matrix.clear();

  std::vector<typename BaseFunctionSet::JacobianRangeType> gradient_phi(discreteFunctionSpace_.mapper().maxNumDofs());

  // micro scale base function:
  std::vector<RangeType> phi(discreteFunctionSpace_.mapper().maxNumDofs());

  for (const Entity& entity : discreteFunctionSpace_) {
    const auto& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    auto local_matrix = global_matrix.localMatrix(entity, entity);
    auto local_disc_function = disc_func.localFunction(entity);

    const BaseFunctionSet& baseSet = local_matrix.domainBasisFunctionSet();
    const auto numBaseFunctions = baseSet.size();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const auto quadrature = make_quadrature(entity, discreteFunctionSpace_);
    const auto numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint) {
      // local (barycentric) coordinates (with respect to entity)
      const auto& local_point = quadrature.point(quadraturePoint);

      const auto global_point = geometry.global(local_point);

      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      baseSet.jacobianAll(quadrature[quadraturePoint], gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);

      typename BaseFunctionSet::JacobianRangeType grad_local_disc_function;
      for (unsigned int i = 0; i < numBaseFunctions; ++i) {
        RangeType value_local_disc_function;
        local_disc_function.evaluate(quadrature[quadraturePoint], value_local_disc_function);

        local_disc_function.jacobian(quadrature[quadraturePoint], grad_local_disc_function);
        // here: no multiplication with jacobian inverse transposed required!

        // JA( \nabla u_H ) \nabla phi_i // jacobian of diffusion operator evaluated in (x,grad_local_disc_function) in
        // direction of the gradient of the current base function
        typename BaseFunctionSet::JacobianRangeType jac_diffusion_flux;
        diffusion_operator_.jacobianDiffusiveFlux(global_point, grad_local_disc_function, gradient_phi[i],
                                                  jac_diffusion_flux);

        for (unsigned int j = 0; j < numBaseFunctions; ++j) {
          local_matrix.add(j, i, weight * (jac_diffusion_flux[0] * gradient_phi[j][0]));

          if (lower_order_term_) {
            RangeType F_position_derivative;
            typename BaseFunctionSet::JacobianRangeType F_direction_derivative;
            // lower_order_term_->evaluate( global_point, phi[i], gradient_phi[i], F_i );
            lower_order_term_->position_derivative(global_point, value_local_disc_function, grad_local_disc_function,
                                                   F_position_derivative);
            lower_order_term_->direction_derivative(global_point, value_local_disc_function, grad_local_disc_function,
                                                    F_direction_derivative);
            local_matrix.add(j, i, weight * F_position_derivative * phi[i][0] * phi[j][0]);
            local_matrix.add(j, i, weight * (F_direction_derivative[0] * gradient_phi[i][0]) * phi[j][0]);
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment) {
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();
    for (const Entity& entity : discreteFunctionSpace_) {
      if (!entity.hasBoundaryIntersections())
        continue;

      auto local_matrix = global_matrix.localMatrix(entity, entity);
      const auto& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);

      const IntersectionIterator iend = gridPart.iend(entity);
      for (IntersectionIterator iit = gridPart.ibegin(entity); iit != iend; ++iit) {
        const Intersection& intersection = *iit;
        if (!intersection.boundary())
          continue;

        // boundaryId 1 = Dirichlet face; boundaryId 2 = Neumann face;
        if (intersection.boundary() && (intersection.boundaryId() == 2))
          continue;

        const int face = intersection.indexInInside();
        const FaceDofIterator fdend = lagrangePointSet.template endSubEntity<1>(face);
        for (FaceDofIterator fdit = lagrangePointSet.template beginSubEntity<1>(face); fdit != fdend; ++fdit)
          local_matrix.unitRow(*fdit);
      }
    }
  }
  global_matrix.communicate();
} // assemble_jacobian_matrix

template <class DiscreteFunctionImp, class DiffusionImp>
template <class MatrixType>
void DiscreteEllipticOperator<DiscreteFunctionImp, DiffusionImp>::assemble_jacobian_matrix(
    DiscreteFunction& disc_func, const DiscreteFunction& dirichlet_extension, MatrixType& global_matrix,
    bool boundary_treatment) const {
  global_matrix.reserve(DSFe::diagonalAndNeighborStencil(global_matrix));
  global_matrix.clear();

  std::vector<typename BaseFunctionSet::JacobianRangeType> gradient_phi(discreteFunctionSpace_.mapper().maxNumDofs());

  // micro scale base function:
  std::vector<RangeType> phi(discreteFunctionSpace_.mapper().maxNumDofs());

  for (const Entity& entity : discreteFunctionSpace_) {
    const auto& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    auto local_matrix = global_matrix.localMatrix(entity, entity);
    auto local_disc_function = disc_func.localFunction(entity);
    auto local_dirichlet_extension = dirichlet_extension.localFunction(entity);

    const BaseFunctionSet& baseSet = local_matrix.domainBasisFunctionSet();
    const auto numBaseFunctions = baseSet.size();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const auto quadrature = make_quadrature(entity, discreteFunctionSpace_);
    const auto numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint) {
      // local (barycentric) coordinates (with respect to entity)
      const auto& local_point = quadrature.point(quadraturePoint);

      const auto global_point = geometry.global(local_point);

      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      baseSet.jacobianAll(quadrature[quadraturePoint], gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);

      for (unsigned int i = 0; i < numBaseFunctions; ++i) {
        RangeType value_local_disc_function;
        local_disc_function.evaluate(quadrature[quadraturePoint], value_local_disc_function);

        RangeType value_local_dirichlet_extension;
        local_dirichlet_extension.evaluate(quadrature[quadraturePoint], value_local_dirichlet_extension);

        typename BaseFunctionSet::JacobianRangeType grad_local_disc_function;
        local_disc_function.jacobian(quadrature[quadraturePoint], grad_local_disc_function);

        typename BaseFunctionSet::JacobianRangeType grad_local_dirichlet_extension;
        local_dirichlet_extension.jacobian(quadrature[quadraturePoint], grad_local_dirichlet_extension);

        RangeType total_value = value_local_dirichlet_extension + value_local_disc_function;
        typename BaseFunctionSet::JacobianRangeType total_direction;
        total_direction[0] = grad_local_dirichlet_extension[0] + grad_local_disc_function[0];

        // JA( \nabla u_H ) \nabla phi_i // jacobian of diffusion operator evaluated in (x,grad_local_disc_function) in
        // direction of the gradient of the current base function
        typename BaseFunctionSet::JacobianRangeType jac_diffusion_flux;
        diffusion_operator_.jacobianDiffusiveFlux(global_point, total_direction, gradient_phi[i], jac_diffusion_flux);

        for (unsigned int j = 0; j < numBaseFunctions; ++j) {
          local_matrix.add(j, i, weight * (jac_diffusion_flux[0] * gradient_phi[j][0]));

          if (lower_order_term_) {
            RangeType F_position_derivative;
            typename BaseFunctionSet::JacobianRangeType F_direction_derivative;
            // lower_order_term_->evaluate( global_point, phi[i], gradient_phi[i], F_i );
            lower_order_term_->position_derivative(global_point, total_value, total_direction, F_position_derivative);
            lower_order_term_->direction_derivative(global_point, total_value, total_direction, F_direction_derivative);
            local_matrix.add(j, i, weight * F_position_derivative * phi[i][0] * phi[j][0]);
            local_matrix.add(j, i, weight * (F_direction_derivative[0] * gradient_phi[i][0]) * phi[j][0]);
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment) {
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();
    for (const Entity& entity : discreteFunctionSpace_) {
      if (!entity.hasBoundaryIntersections())
        continue;

      auto local_matrix = global_matrix.localMatrix(entity, entity);

      const LagrangePointSet& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);

      const IntersectionIterator iend = gridPart.iend(entity);
      for (IntersectionIterator iit = gridPart.ibegin(entity); iit != iend; ++iit) {
        const Intersection& intersection = *iit;
        if (!intersection.boundary())
          continue;

        // boundaryId 1 = Dirichlet face; boundaryId 2 = Neumann face;
        if (intersection.boundary() && (intersection.boundaryId() == 2))
          continue;

        const int face = intersection.indexInInside();
        const FaceDofIterator fdend = lagrangePointSet.template endSubEntity<1>(face);
        for (FaceDofIterator fdit = lagrangePointSet.template beginSubEntity<1>(face); fdit != fdend; ++fdit)
          local_matrix.unitRow(*fdit);
      }
    }
  }
  global_matrix.communicate();
} // assemble_jacobian_matrix

} // namespace FEM {
} // namespace Multiscale {
} // namespace Dune {
