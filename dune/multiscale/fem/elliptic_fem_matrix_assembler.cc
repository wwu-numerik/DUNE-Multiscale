#include <dune/fem/solver/oemsolver/oemsolver.hh>
#include <dune/fem/solver/inverseoperators.hh>
#include <dune/fem/operator/common/operator.hh>
#include <dune/fem/operator/discreteoperatorimp.hh>
#include <dune/fem/operator/2order/lagrangematrixsetup.hh>
#include <dune/fem/operator/matrix/spmatrix.hh>

#include <dune/stuff/common/ranges.hh>
#include <dune/multiscale/common/righthandside_assembler.hh>

namespace Dune {
namespace Multiscale {
namespace FEM {

template< class DiscreteFunctionImp, class DiffusionImp, class ReactionImp >
void DiscreteEllipticOperator< DiscreteFunctionImp, DiffusionImp, ReactionImp >::operator()(const DiscreteFunction& /*u*/,
                                                                                            DiscreteFunction& /*w*/)
const {
  DUNE_THROW(Dune::NotImplemented,"the ()-operator of the DiscreteEllipticOperator class is not yet implemented and still a dummy.");
} // ()

template< class DiscreteFunctionImp, class DiffusionImp, class ReactionImp >
template< class MatrixType >
void DiscreteEllipticOperator< DiscreteFunctionImp, DiffusionImp, ReactionImp >::assemble_matrix(
  MatrixType& global_matrix,
  bool boundary_treatment ) const {
  typedef typename MatrixType::LocalMatrixType LocalMatrix;

  global_matrix.reserve();
  global_matrix.clear();

  std::vector< typename BaseFunctionSet::JacobianRangeType > gradient_phi( discreteFunctionSpace_.mapper().maxNumDofs() );

  // micro scale base function:
  std::vector< RangeType > phi( discreteFunctionSpace_.mapper().maxNumDofs() );
  for (const auto& entity : discreteFunctionSpace_)
  {
    const Geometry& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    LocalMatrix local_matrix = global_matrix.localMatrix(entity, entity);

    const BaseFunctionSet& baseSet = local_matrix.domainBaseFunctionSet();
    const auto numBaseFunctions = baseSet.size();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const Quadrature quadrature(entity, 2 * discreteFunctionSpace_.order() + 2);
    const size_t numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint)
    {
      // local (barycentric) coordinates (with respect to entity)
      const typename Quadrature::CoordinateType& local_point = quadrature.point(quadraturePoint);
      const DomainType global_point = geometry.global(local_point);
      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      // transposed of the the inverse jacobian
      const auto& inverse_jac = geometry.jacobianInverseTransposed(local_point);
      baseSet.jacobianAll(quadrature[quadraturePoint], inverse_jac, gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);

      for (unsigned int i = 0; i < numBaseFunctions; ++i)
      {
        // A( \nabla \phi ) // diffusion operator evaluated in (x,\nabla \phi)
        typename LocalFunction::JacobianRangeType diffusion_in_gradient_phi;
        diffusion_operator_.diffusiveFlux(global_point, gradient_phi[i], diffusion_in_gradient_phi);
        for (unsigned int j = 0; j < numBaseFunctions; ++j)
        {
          local_matrix.add( j, i, weight * (diffusion_in_gradient_phi[0] * gradient_phi[j][0]) );

          if (reaction_coefficient_)
          {
            RangeType c;
            reaction_coefficient_->evaluate(global_point, c);
            local_matrix.add( j, i, weight * c * (phi[i][0] * phi[j][0]) );
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment)
  {
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();
    for (const auto& entity : discreteFunctionSpace_)
    {
      if ( !entity.hasBoundaryIntersections() )
        continue;

      LocalMatrix local_matrix = global_matrix.localMatrix(entity, entity);
      const LagrangePointSet& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);
      const IntersectionIterator iend = gridPart.iend(entity);
      for (IntersectionIterator iit = gridPart.ibegin(entity); iit != iend; ++iit)
      {
        const Intersection& intersection = *iit;
        if ( !intersection.boundary() )
          continue;

        const int face = intersection.indexInInside();
        const FaceDofIterator fdend = lagrangePointSet.template endSubEntity< 1 >(face);
        for (FaceDofIterator fdit = lagrangePointSet.template beginSubEntity< 1 >(face); fdit != fdend; ++fdit)
          local_matrix.unitRow(*fdit);
      }
    }
  }
} // assemble_matrix


template< class DiscreteFunctionImp, class DiffusionImp, class ReactionImp >
template< class MatrixType, class HostDiscreteFunctionSpaceType >
void DiscreteEllipticOperator< DiscreteFunctionImp,
                               DiffusionImp,
                               ReactionImp >::assemble_matrix
  (MatrixType& global_matrix, HostDiscreteFunctionSpaceType& hostSpace, bool boundary_treatment ) const {
  typedef typename MatrixType::LocalMatrixType LocalMatrix;

  global_matrix.reserve();
  global_matrix.clear();

  std::vector< typename BaseFunctionSet::JacobianRangeType > gradient_phi( discreteFunctionSpace_.mapper().maxNumDofs() );

  // micro scale base function:
  std::vector< RangeType > phi( discreteFunctionSpace_.mapper().maxNumDofs() );

  const Iterator end = discreteFunctionSpace_.end();
  for (Iterator it = discreteFunctionSpace_.begin(); it != end; ++it)
  {
    const Entity& entity = *it;
    const Geometry& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    LocalMatrix local_matrix = global_matrix.localMatrix(entity, entity);

    const BaseFunctionSet& baseSet = local_matrix.domainBaseFunctionSet();
    const auto numBaseFunctions = baseSet.numBaseFunctions();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const Quadrature quadrature(entity, 2 * discreteFunctionSpace_.order() + 2);
    const size_t numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint)
    {
      // local (barycentric) coordinates (with respect to entity)
      const typename Quadrature::CoordinateType& local_point = quadrature.point(quadraturePoint);

      const DomainType global_point = geometry.global(local_point);

      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      // transposed of the the inverse jacobian
      const auto& inverse_jac = geometry.jacobianInverseTransposed(local_point);
      baseSet.jacobianAll(quadrature[quadraturePoint], inverse_jac, gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);

      for (unsigned int i = 0; i < numBaseFunctions; ++i)
      {
        // A( \nabla \phi ) // diffusion operator evaluated in (x,\nabla \phi)
        typename LocalFunction::JacobianRangeType diffusion_in_gradient_phi;
        diffusion_operator_.diffusiveFlux(global_point, gradient_phi[i], diffusion_in_gradient_phi);
        for (unsigned int j = 0; j < numBaseFunctions; ++j)
        {
          local_matrix.add( j, i, weight * (diffusion_in_gradient_phi[0] * gradient_phi[j][0]) );

          if (reaction_coefficient_)
          {
            RangeType c;
            reaction_coefficient_->evaluate(global_point, c);
            local_matrix.add( j, i, weight * c * (phi[i][0] * phi[j][0]) );
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment)
  {
    typedef typename HostDiscreteFunctionSpaceType::GridPartType HostGridPartType;
    typedef typename HostDiscreteFunctionSpaceType::GridType     HostGridType;

    typedef typename HostDiscreteFunctionSpaceType::IteratorType::Entity HostEntityType;
    typedef typename HostEntityType::EntityPointer                       HostEntityPointerType;

    typedef typename HostGridPartType::IntersectionIteratorType HostIntersectionIterator;

    const HostGridPartType& hostGridPart = hostSpace.gridPart();

    const GridPart& gridPart = discreteFunctionSpace_.gridPart();
    const Grid& subGrid = discreteFunctionSpace_.grid();

    for (Iterator it = discreteFunctionSpace_.begin(); it != end; ++it)
    {
      const Entity& entity = *it;

      const HostEntityPointerType host_entity_pointer = subGrid.template getHostEntity< 0 >(entity);
      const HostEntityType& host_entity = *host_entity_pointer;

      LocalMatrix local_matrix = global_matrix.localMatrix(entity, entity);

      const LagrangePointSet& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);

      const HostIntersectionIterator iend = hostGridPart.iend(host_entity);
      for (HostIntersectionIterator iit = hostGridPart.ibegin(host_entity); iit != iend; ++iit)
      {
        if ( iit->neighbor() ) // if there is a neighbor entity
        {
          // check if the neighbor entity is in the subgrid
          const HostEntityPointerType neighborHostEntityPointer = iit->outside();
          const HostEntityType& neighborHostEntity = *neighborHostEntityPointer;
          if ( subGrid.template contains< 0 >(neighborHostEntity) )
          {
            continue;
          }
        }

        const int face = (*iit).indexInInside();
        const FaceDofIterator fdend = lagrangePointSet.template endSubEntity< 1 >(face);
        for (FaceDofIterator fdit = lagrangePointSet.template beginSubEntity< 1 >(face); fdit != fdend; ++fdit)
          local_matrix.unitRow(*fdit);
      }
    }
  }
} // assemble_matrix


template< class DiscreteFunctionImp, class DiffusionImp, class ReactionImp >
template< class MatrixType >
void DiscreteEllipticOperator< DiscreteFunctionImp, DiffusionImp, ReactionImp >::assemble_jacobian_matrix(
  DiscreteFunction& disc_func,
  MatrixType& global_matrix,
  bool boundary_treatment ) const {
  typedef typename MatrixType::LocalMatrixType LocalMatrix;

  typedef typename DiscreteFunction::LocalFunctionType
  LocalFunction;

  global_matrix.reserve();
  global_matrix.clear();

  std::vector< typename BaseFunctionSet::JacobianRangeType > gradient_phi( discreteFunctionSpace_.mapper().maxNumDofs() );

  // micro scale base function:
  std::vector< RangeType > phi( discreteFunctionSpace_.mapper().maxNumDofs() );

  const Iterator end = discreteFunctionSpace_.end();
  for (Iterator it = discreteFunctionSpace_.begin(); it != end; ++it)
  {
    const Entity& entity = *it;
    const Geometry& geometry = entity.geometry();
    assert(entity.partitionType() == InteriorEntity);

    LocalMatrix local_matrix = global_matrix.localMatrix(entity, entity);
    LocalFunction local_disc_function = disc_func.localFunction(entity);

    const BaseFunctionSet& baseSet = local_matrix.domainBaseFunctionSet();
    const auto numBaseFunctions = baseSet.size();

    // for constant diffusion "2*discreteFunctionSpace_.order()" is sufficient, for the general case, it is better to
    // use a higher order quadrature:
    const Quadrature quadrature(entity, 2 * discreteFunctionSpace_.order() + 2);
    const size_t numQuadraturePoints = quadrature.nop();
    for (size_t quadraturePoint = 0; quadraturePoint < numQuadraturePoints; ++quadraturePoint)
    {
      // local (barycentric) coordinates (with respect to entity)
      const typename Quadrature::CoordinateType& local_point = quadrature.point(quadraturePoint);

      const DomainType global_point = geometry.global(local_point);

      const double weight = quadrature.weight(quadraturePoint) * geometry.integrationElement(local_point);

      // transposed of the the inverse jacobian
      const auto& inverse_jac = geometry.jacobianInverseTransposed(local_point);
      baseSet.jacobianAll(quadrature[quadraturePoint], inverse_jac, gradient_phi);
      baseSet.evaluateAll(quadrature[quadraturePoint], phi);

      for (unsigned int i = 0; i < numBaseFunctions; ++i)
      {
        typename BaseFunctionSet::JacobianRangeType grad_local_disc_function;
        local_disc_function.jacobian(quadrature[quadraturePoint], grad_local_disc_function);
        // here: no multiplication with jacobian inverse transposed required!

        // JA( \nabla u_H ) \nabla phi_i // jacobian of diffusion operator evaluated in (x,grad_local_disc_function) in
        // direction of the gradient of the current base function
        typename LocalFunction::JacobianRangeType jac_diffusion_flux;
        diffusion_operator_.jacobianDiffusiveFlux(global_point,
                                                  grad_local_disc_function,
                                                  gradient_phi[i],
                                                  jac_diffusion_flux);

        for (unsigned int j = 0; j < numBaseFunctions; ++j)
        {
          local_matrix.add( j, i, weight * (jac_diffusion_flux[0] * gradient_phi[j][0]) );

          if (reaction_coefficient_)
          {
            RangeType c;
            reaction_coefficient_->evaluate(global_point, c);
            local_matrix.add( j, i, weight * c * (phi[i][0] * phi[j][0]) );
          }
        }
      }
    }
  }

  // boundary treatment
  if (boundary_treatment)
  {
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();
    for (Iterator it = discreteFunctionSpace_.begin(); it != end; ++it)
    {
      const Entity& entity = *it;
      if ( !entity.hasBoundaryIntersections() )
        continue;

      LocalMatrix local_matrix = global_matrix.localMatrix(entity, entity);

      const LagrangePointSet& lagrangePointSet = discreteFunctionSpace_.lagrangePointSet(entity);

      const IntersectionIterator iend = gridPart.iend(entity);
      for (IntersectionIterator iit = gridPart.ibegin(entity); iit != iend; ++iit)
      {
        const Intersection& intersection = *iit;
        if ( !intersection.boundary() )
          continue;

        const int face = intersection.indexInInside();
        const FaceDofIterator fdend = lagrangePointSet.template endSubEntity< 1 >(face);
        for (FaceDofIterator fdit = lagrangePointSet.template beginSubEntity< 1 >(face); fdit != fdend; ++fdit)
          local_matrix.unitRow(*fdit);
      }
    }
  }
} // assemble_jacobian_matrix

} //namespace FEM {
} //namespace Multiscale {
} //namespace Dune {
