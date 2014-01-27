// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

#ifndef DUNE_MSFEM_ERRORESTIMATOR_HH
#define DUNE_MSFEM_ERRORESTIMATOR_HH

#include <dune/common/unused.hh>
#include <memory>
#include <array>
#include <boost/range/adaptor/map.hpp>
#include <dune/stuff/common/fixed_map.hh>
#include <dune/stuff/common/memory.hh>
#include <dune/stuff/common/ranges.hh>
#include <dune/stuff/aliases.hh>
#include <dune/stuff/grid/entity.hh>
#include <dune/stuff/grid/intersection.hh>
#include <dune/multiscale/tools/misc.hh>
#include <dune/multiscale/hmm/cell_problem_numbering.hh>

// where the quadratures are defined
#include <dune/fem/quadrature/cachingquadrature.hh>

#include <dune/multiscale/msfem/conservative_flux_solver.hh>
#include "estimator_utils.hh"

namespace Dune {
namespace Multiscale {
namespace MsFEM {

//! \TODO docme
template <class DiscreteFunctionPointerPair, class EntityPointer, class GridPartType, int N = 3>
struct FluxContainer {
  FluxContainer(const EntityPointer _entity, const GridPartType& _grid_part)
    : entity(_entity)
    , grid_part(_grid_part) {}

  std::array<DiscreteFunctionPointerPair, N> fluxes;
  const EntityPointer entity;
  const GridPartType& grid_part;

  template <class SmallerIntersectionType>
  int intersection_compatible(const SmallerIntersectionType& smaller) const {
    int j = 0;
    for (auto& intersection : DSC::intersectionRange(grid_part, *entity)) {
      assert(intersection.dimension == SmallerIntersectionType::dimension);
      bool compatible = true;
      if (intersection.geometry().corners() != smaller.geometry().corners())
        return false;
      for (auto i : DSC::valueRange(intersection.geometry().corners())) {
        const bool contains = DSG::intersectionContains(intersection, smaller.geometry().corner(i));
        compatible = compatible && contains;
      }
      if (compatible)
        return j;
      j++;
    }
    return -1;
  }
};

//! \TODO docme
template <class DiscreteFunctionImp, class DiffusionImp, class SourceImp, class MacroMicroGridSpecifierImp,
          class LocalGridListImp>
class MsFEMErrorEstimator {
private:
  typedef MsFEMErrorEstimator<DiscreteFunctionImp, DiffusionImp, SourceImp, MacroMicroGridSpecifierImp, LocalGridListImp>
  ThisType;
  typedef DiffusionImp DiffusionOperatorType;
  typedef SourceImp SourceType;
  typedef MacroMicroGridSpecifierImp MacroMicroGridSpecifierType;
  typedef LocalGridListImp LocalGridListType;

  //! Necessary typedefs for the DiscreteFunctionImp:

  typedef DiscreteFunctionImp DiscreteFunctionType;
  typedef typename DiscreteFunctionType::FunctionSpaceType FunctionSpaceType;

  typedef typename DiscreteFunctionType::DiscreteFunctionSpaceType DiscreteFunctionSpaceType;

  typedef typename DiscreteFunctionSpaceType::RangeFieldType RangeFieldType;
  typedef typename DiscreteFunctionSpaceType::DomainType DomainType;
  typedef typename DiscreteFunctionSpaceType::RangeType RangeType;
  typedef typename DiscreteFunctionSpaceType::JacobianRangeType JacobianRangeType;

  typedef typename DiscreteFunctionType::DofIteratorType DofIteratorType;
  typedef typename DiscreteFunctionSpaceType::GridPartType GridPartType;
  typedef typename DiscreteFunctionSpaceType::GridType GridType;
  typedef typename DiscreteFunctionSpaceType::IteratorType IteratorType;
  typedef typename DiscreteFunctionSpaceType::LagrangePointSetType LagrangePointSetType;

  typedef typename GridType::Traits::LeafIndexSet LeafIndexSetType;

  typedef typename GridPartType::IntersectionIteratorType IntersectionIteratorType;
  typedef typename IntersectionIteratorType::Intersection Intersection;
  typedef typename GridType::template Codim<0>::Entity EntityType;
  typedef typename GridType::template Codim<0>::EntityPointer EntityPointerType;
  typedef typename GridType::template Codim<1>::Entity FaceType;
  typedef typename GridType::template Codim<1>::EntityPointer FacePointerType;
  typedef typename GridType::template Codim<0>::Geometry EntityGeometryType;
  typedef typename GridType::template Codim<1>::Geometry FaceGeometryType;
  typedef typename DiscreteFunctionSpaceType::BasisFunctionSetType BasisFunctionSetType;

  // --------------------------- subgrid typedefs ------------------------------------

  typedef typename MsFEMTraits::LocalGridType LocalGridType;
  typedef typename MsFEMTraits::LocalGridPartType LocalGridPartType;
  typedef typename MsFEMTraits::LocalGridDiscreteFunctionSpaceType LocalGridDiscreteFunctionSpaceType;
  typedef typename MsFEMTraits::LocalGridDiscreteFunctionType LocalGridDiscreteFunctionType;

  typedef typename LocalGridDiscreteFunctionSpaceType::IteratorType SubGridIteratorType;
  typedef typename SubGridIteratorType::Entity SubGridEntityType;
  typedef typename LocalGridDiscreteFunctionType::LocalFunctionType SubGridLocalFunctionType;

  typedef typename LocalGridDiscreteFunctionSpaceType::LagrangePointSetType SubGridLagrangePointSetType;

  static const int faceCodim = 1;
  typedef typename SubGridLagrangePointSetType::template Codim<faceCodim>::SubEntityIteratorType
  SubGridFaceDofIteratorType;

  typedef typename LagrangePointSetType::template Codim<faceCodim>::SubEntityIteratorType FaceDofIteratorType;

  typedef typename LocalGridType::template Codim<0>::Geometry SubGridEntityGeometryType;

  typedef std::array<RangeType, 3> JumpArray;
  typedef std::array<const Intersection*, 3> IntersectionArray;
  typedef std::unique_ptr<DiscreteFunctionType> DiscreteFunctionPointer;
  typedef std::array<DiscreteFunctionPointer, 2> DiscreteFunctionPointerPair;
  typedef FluxContainer<DiscreteFunctionPointerPair, EntityPointerType, GridPartType, 3> FluxContainerType;
  typedef Stuff::Common::FixedMap<std::string, RangeType, 6> ErrormapType;

  typedef EstimatorUtils<ThisType> EstimatorUtilsType;
  template <class T>
  friend struct EstimatorUtils;

  //!-----------------------------------------------------------------------------------------

  static const int dimension = GridType::dimension;
  static const int spacePolOrd = DiscreteFunctionSpaceType::polynomialOrder;
  static const int maxnumOfBaseFct = 100;

  const DiscreteFunctionSpaceType& fineDiscreteFunctionSpace_;
  MacroMicroGridSpecifierType& specifier_;
  LocalGridListType& subgrid_list_;
  const DiffusionOperatorType& diffusion_;
  const SourceType& f_;

  // ----- local error indicators (for each coarse grid element T) -------------

  // local coarse residual, i.e. H ||f||_{L^2(T)}
  typedef std::vector<RangeType> RangeTypeVector;
  RangeTypeVector loc_coarse_residual_;

  // local coarse grid jumps (contribute to the total coarse residual)
  RangeTypeVector loc_coarse_grid_jumps_;

  // local projection error (we project to get a globaly continous approximation)
  RangeTypeVector loc_projection_error_;

  // local jump in the conservative flux
  RangeTypeVector loc_conservative_flux_jumps_;

  // local approximation error
  RangeTypeVector loc_approximation_error_;

  // local sum over the fine grid jumps (for a fixed subgrid that cooresponds with a coarse entity T)
  RangeTypeVector loc_fine_grid_jumps_;

  void initialize_local_error_manager();
  void set_loc_coarse_residual(std::size_t index, const RangeType& loc_coarse_residual);
  void set_loc_coarse_grid_jumps(std::size_t index, const RangeType& loc_coarse_grid_jumps);
  void set_loc_projection_error(std::size_t index, const RangeType& loc_projection_error);
  void set_loc_conservative_flux_jumps(std::size_t index, const RangeType& loc_conservative_flux_jumps);
  void set_loc_approximation_error(std::size_t index, const RangeType& loc_approximation_error);
  void set_loc_fine_grid_jumps(std::size_t index, const RangeType& loc_fine_grid_jumps);
  RangeType get_loc_coarse_residual(std::size_t index) const;
  RangeType get_loc_coarse_grid_jumps(std::size_t index) const;
  RangeType get_loc_projection_error(std::size_t index) const;
  RangeType get_loc_conservative_flux_jumps(std::size_t index) const;
  RangeType get_loc_approximation_error(std::size_t index) const;
  RangeType get_loc_fine_grid_jumps(std::size_t index) const;

public:
  MsFEMErrorEstimator(const DiscreteFunctionSpaceType& fineDiscreteFunctionSpace,
                      MacroMicroGridSpecifierType& specifier, LocalGridListType& subgrid_list,
                      const DiffusionOperatorType& diffusion, const SourceType& f)
    : fineDiscreteFunctionSpace_(fineDiscreteFunctionSpace)
    , specifier_(specifier)
    , subgrid_list_(subgrid_list)
    , diffusion_(diffusion)
    , f_(f) {}

private:

  //! method to get the local mesh size H of a coarse grid entity 'T'
  // works only for our 2D examples!!!!
  RangeType get_coarse_grid_H(const EntityType& entity) const {
    return DSG::entityDiameter(entity);
  } // get_coarse_grid_H

  // for a coarse grid entity T:
  // return:  H_T ||f||_{L^2(T)}
  RangeType indicator_f(const EntityType& entity) const {
    // create quadrature for given geometry type
    const auto entityQuadrature = DSFe::make_quadrature(entity, fineDiscreteFunctionSpace_);

    // get geoemetry of entity
    const auto& geometry = entity.geometry();

    const auto H_T = get_coarse_grid_H(entity);

    RangeType y(0);
    RangeType local_indicator(0);

    for (auto quadraturePoint : DSC::valueRange(entityQuadrature.nop())) {
      const double weight = entityQuadrature.weight(quadraturePoint) *
                            geometry.integrationElement(entityQuadrature.point(quadraturePoint));

      f_.evaluate(geometry.global(entityQuadrature.point(quadraturePoint)), y);
      y *= y;

      local_indicator += weight * y;
    }

    local_indicator *= std::pow(H_T, 2.0);

    return sqrt(local_indicator);
  } // indicator_f

  // jump in conservative flux
  void getFluxes(const EntityType& coarse_entity, const DiscreteFunctionType& msfem_coarse_part,
                 RangeType& jump_conservative_flux, RangeType& jump_coarse_flux) const {
    const auto& coarseDiscreteFunctionSpace = specifier_.coarseSpace();
    const auto& coarseGridLeafIndexSet = coarseDiscreteFunctionSpace.gridPart().grid().leafIndexSet();
    const auto index_coarse_entity = coarseGridLeafIndexSet.index(coarse_entity);

    //! ---- get sub grid that for coarse entity
    // the sub grid U(T) that belongs to the coarse_grid_entity T
    auto subGridPart = subgrid_list_.gridPart(index_coarse_entity);

    LocalGridDiscreteFunctionSpaceType localDiscreteFunctionSpace(subGridPart);
    std::array<LocalGridDiscreteFunctionType, 2> conservative_flux_coarse_ent = {
        {LocalGridDiscreteFunctionType("Conservative Flux on coarse entity for e_0", localDiscreteFunctionSpace),
         LocalGridDiscreteFunctionType("Conservative Flux on coarse entity for e_1", localDiscreteFunctionSpace)}};
    // --------- load local solutions -------
    boost::format flux_location("cf_problems/_conservativeFlux_e_%d_sg_%d");
    for (int i : {0, 1}) {
      conservative_flux_coarse_ent[i].clear();
      const std::string cf_solution_location = (flux_location % i % index_coarse_entity).str();
      DiscreteFunctionReader(cf_solution_location).read(0, conservative_flux_coarse_ent[i]);
    }

    DiscreteFunctionPointerPair cflux_coarse_ent_host = {
        {DSC::make_unique<DiscreteFunctionType>("Conservative Flux on coarse entity for e_0",
                                                fineDiscreteFunctionSpace_),
         DSC::make_unique<DiscreteFunctionType>("Conservative Flux on coarse entity for e_1",
                                                fineDiscreteFunctionSpace_)}};

    DSG::subgrid_to_hostrid_function(conservative_flux_coarse_ent, cflux_coarse_ent_host);

    std::array<RangeType, 3> coarse_face_volume;

    const GridPartType& coarseGridPart = specifier_.coarseSpace().gridPart();
    // flux for each neighbor entity
    FluxContainerType cflux_neighbor_ent_host(coarse_entity, coarseGridPart);

    int local_face_index = 0;
    IntersectionIteratorType endnit = coarseGridPart.iend(coarse_entity);
    for (IntersectionIteratorType face_it = coarseGridPart.ibegin(coarse_entity); face_it != endnit; ++face_it) {
      coarse_face_volume[local_face_index] = face_it->geometry().volume();

      if (face_it->neighbor()) {
        const EntityPointerType outside_it = face_it->outside();

        const int index_coarse_neighbor_entity = coarseGridLeafIndexSet.index(*outside_it);

        // --- get subgrids and load fluxes ---
        auto subGridPart_neighbor = subgrid_list_.gridPart(index_coarse_neighbor_entity);
        LocalGridDiscreteFunctionSpaceType localDiscreteFunctionSpace_neighbor(subGridPart_neighbor);

        std::array<LocalGridDiscreteFunctionType, 2> conservative_flux_coarse_ent_neighbor = {
            {LocalGridDiscreteFunctionType("Conservative Flux on neighbor coarse entity for e_0",
                                         localDiscreteFunctionSpace_neighbor),
             LocalGridDiscreteFunctionType("Conservative Flux on neighbor coarse entity for e_1",
                                         localDiscreteFunctionSpace_neighbor)}};

        // --------- load local solutions -------
        // the file/place, where we saved the solutions conservative flux problems problems
        for (int i : {0, 1}) {
          conservative_flux_coarse_ent_neighbor[i].clear();
          const std::string cf_solution_location_neighbor = (flux_location % i % index_coarse_neighbor_entity).str();
          // reader for data file:
          DiscreteFunctionReader(cf_solution_location_neighbor).read(0, conservative_flux_coarse_ent_neighbor[i]);
          cflux_neighbor_ent_host.fluxes[local_face_index][i] = DSC::make_unique<DiscreteFunctionType>(
              "Conservative Flux on neighbor coarse entity for e_" + Stuff::Common::toString(i),
              fineDiscreteFunctionSpace_);
        }
        DSG::subgrid_to_hostrid_function(conservative_flux_coarse_ent_neighbor,
                                                        cflux_neighbor_ent_host.fluxes[local_face_index]);
      }

      local_face_index += 1;
    }

    if (local_face_index != 3) {
      DUNE_THROW(Dune::InvalidStateException, "Error! Implementation only for triangular mesh in 2d!");
    }

    const auto contributions = EstimatorUtilsType::flux_contributions(
        localDiscreteFunctionSpace, subGridPart, coarseGridLeafIndexSet, cflux_coarse_ent_host, msfem_coarse_part,
        cflux_neighbor_ent_host, index_coarse_entity, coarse_face_volume, specifier_.getLevelDifference(),
        fineDiscreteFunctionSpace_);
    const auto jump = contributions.first;
    const auto coarse_jump = contributions.second;

    jump_conservative_flux = (sqrt(jump[0]) + sqrt(jump[1]) + sqrt(jump[2]));
    jump_coarse_flux = sqrt(coarse_jump[0]) + sqrt(coarse_jump[1]) + sqrt(coarse_jump[2]);
  } // getFluxes

  void fine_contribution(const LeafIndexSetType& coarseGridLeafIndexSet, const DiscreteFunctionType& msfem_solution,
                         std::vector<RangeType>& loc_approximation_error, std::vector<RangeType>& loc_fine_grid_jumps,
                         ErrormapType& errors) const {
    for (const auto& entity : fineDiscreteFunctionSpace_) {

      // identify coarse grid father entity
      auto coarse_father =
          DSG::make_father(coarseGridLeafIndexSet, EntityPointerType(entity), specifier_.getLevelDifference());
      const int coarse_father_index = coarseGridLeafIndexSet.index(*coarse_father);

      const auto& entityGeometry = entity.geometry();

      const auto& x = entityGeometry.center();
      const auto& x_local = entityGeometry.local(x);

      const auto local_msfem_sol = msfem_solution.localFunction(entity);
      JacobianRangeType gradient_msfem_sol(0.);
      local_msfem_sol.jacobian(x_local, gradient_msfem_sol);

      JacobianRangeType diffusive_flux_x;
      diffusion_.diffusiveFlux(x, gradient_msfem_sol, diffusive_flux_x);

      const auto highOrder_entityQuadrature = DSFe::make_quadrature(entity, fineDiscreteFunctionSpace_);

      for (auto quadraturePoint : DSC::valueRange(highOrder_entityQuadrature.nop())) {
        const double weight = highOrder_entityQuadrature.weight(quadraturePoint) *
                              entityGeometry.integrationElement(highOrder_entityQuadrature.point(quadraturePoint));

        const auto point = entityGeometry.global(highOrder_entityQuadrature.point(quadraturePoint));

        JacobianRangeType diffusive_flux_high_order;
        diffusion_.diffusiveFlux(point, gradient_msfem_sol, diffusive_flux_high_order);

        RangeType value = 0.0;
        for (int i = 0; i < dimension; ++i)
          value += pow(diffusive_flux_x[0][i] - diffusive_flux_high_order[0][i], 2.0);

        loc_approximation_error[coarse_father_index] += weight * value;
        errors["total_approximation_error"] += weight * value;
      }

      const auto& fineGridPart = fineDiscreteFunctionSpace_.gridPart();

      IntersectionIteratorType endnit = fineGridPart.iend(entity);
      for (IntersectionIteratorType nit = fineGridPart.ibegin(entity); nit != endnit; ++nit) {
        const FaceGeometryType& faceGeometry = nit->geometry();

        if (!nit->neighbor()) {
          continue;
        }

        auto outer_fine_grid_it = nit->outside();
        auto& outer_entity = *outer_fine_grid_it;
        const auto& outer_x = outer_entity.geometry().center();

        auto outer_local_msfem_sol = msfem_solution.localFunction(outer_entity);
        JacobianRangeType outer_gradient_msfem_sol(0.);
        outer_local_msfem_sol.jacobian(outer_entity.geometry().local(outer_x), outer_gradient_msfem_sol);

        JacobianRangeType diffusive_flux_outside;
        diffusion_.diffusiveFlux(outer_x, outer_gradient_msfem_sol, diffusive_flux_outside);

        auto unitOuterNormal = nit->centerUnitOuterNormal();

        const auto edge_length = faceGeometry.volume();

        RangeType int_value = 0.0;
        for (int i = 0; i < dimension; ++i)
          int_value += (diffusive_flux_x[0][i] - diffusive_flux_outside[0][i]) * unitOuterNormal[i];
        int_value = pow(int_value, 2.0);

        loc_fine_grid_jumps[coarse_father_index] += edge_length * edge_length * int_value;
        errors["total_fine_grid_jumps"] += edge_length * edge_length * int_value;
      }
    }
  }

  void coarse_contribution(const DiscreteFunctionSpaceType& coarseDiscreteFunctionSpace,
                           const DiscreteFunctionType& msfem_coarse_part, const DiscreteFunctionType& msfem_fine_part,
                           std::vector<RangeType>& loc_coarse_grid_jumps,
                           std::vector<RangeType>& loc_conservative_flux_jumps,
                           std::vector<RangeType>& loc_coarse_residual, std::vector<RangeType>& loc_projection_error,
                           ErrormapType& errors) const {
    const auto& coarseGridLeafIndexSet = coarseDiscreteFunctionSpace.gridPart().grid().leafIndexSet();

    for (const auto& coarse_entity : coarseDiscreteFunctionSpace) {
      const int global_index_entity = coarseGridLeafIndexSet.index(coarse_entity);

      loc_coarse_residual[global_index_entity] = indicator_f(coarse_entity);
      specifier_.set_loc_coarse_residual(global_index_entity, loc_coarse_residual[global_index_entity]);
      errors["total_coarse_residual"] += std::pow(loc_coarse_residual[global_index_entity], 2.0);

      getFluxes(coarse_entity, msfem_coarse_part, loc_conservative_flux_jumps[global_index_entity],
                loc_coarse_grid_jumps[global_index_entity]);

      specifier_.set_loc_coarse_grid_jumps(global_index_entity, loc_coarse_grid_jumps[global_index_entity]);

      specifier_.set_loc_conservative_flux_jumps(global_index_entity, loc_conservative_flux_jumps[global_index_entity]);

      errors["total_conservative_flux_jumps"] += pow(loc_conservative_flux_jumps[global_index_entity], 2.0);
      errors["total_coarse_grid_jumps"] += pow(loc_coarse_grid_jumps[global_index_entity], 2.0);

      // -------------------------- subgrids and local function ----------------------

      // the sub grid U(T) that belongs to the coarse_grid_entity T
      auto subGridPart = subgrid_list_.gridPart(global_index_entity);

      LocalGridDiscreteFunctionSpaceType localDiscreteFunctionSpace(subGridPart);

      LocalGridDiscreteFunctionType local_problem_solution_e0("Local problem Solution e_0", localDiscreteFunctionSpace);
      local_problem_solution_e0.clear();

      LocalGridDiscreteFunctionType local_problem_solution_e1("Local problem Solution e_1", localDiscreteFunctionSpace);
      local_problem_solution_e1.clear();

      // --------- load local solutions -------
      // the file/place, where we saved the solutions of the cell problems
      const std::string local_solution_location =
          (boost::format("local_problems/_localProblemSolutions_%d") %
           coarseDiscreteFunctionSpace.gridPart().grid().globalIdSet().id(coarse_entity)).str();

      // reader for the cell problem data file:
      DiscreteFunctionReader discrete_function_reader(local_solution_location);
      discrete_function_reader.read(0, local_problem_solution_e0);
      discrete_function_reader.read(1, local_problem_solution_e1);

      // iterator for the local micro grid ('the subgrid corresponding with U(T)')
      for (const auto& local_grid_entity : localDiscreteFunctionSpace) {

        // check if "local_grid_entity" (which is an entity of U(T)) is in T:
        // -------------------------------------------------------------------
        const auto host_local_grid_it = localDiscreteFunctionSpace.grid().template getLocalEntity<0>(local_grid_entity);

        auto father_of_loc_grid_it =
            DSG::make_father(coarseGridLeafIndexSet, host_local_grid_it, specifier_.getLevelDifference());
        if (!DSG::entities_identical(coarse_entity, *father_of_loc_grid_it))
          continue;

        const auto local_center = host_local_grid_it->geometry().local(host_local_grid_it->geometry().center());
        auto localized_msfem_coarse_part = msfem_coarse_part.localFunction(*host_local_grid_it);
        auto localized_msfem_fine_part = msfem_fine_part.localFunction(*host_local_grid_it);

        JacobianRangeType grad_msfem_coarse_part;
        localized_msfem_coarse_part.jacobian(local_center, grad_msfem_coarse_part);

        JacobianRangeType grad_msfem_fine_part;
        localized_msfem_fine_part.jacobian(local_center, grad_msfem_fine_part);

        const SubGridEntityGeometryType& local_grid_geometry = local_grid_entity.geometry();
        assert(local_grid_entity.partitionType() == InteriorEntity);

        // quadrature formula on the sub grid entity

        // higher order quadrature, since A^{\epsilon} is highly variable
        const auto local_grid_quadrature = DSFe::make_quadrature(local_grid_entity, localDiscreteFunctionSpace);
        const auto numQuadraturePoints = local_grid_quadrature.nop();

        for (size_t localQuadraturePoint = 0; localQuadraturePoint < numQuadraturePoints; ++localQuadraturePoint) {
          // local (barycentric) coordinates (with respect to entity)
          const auto& local_subgrid_point = local_grid_quadrature.point(localQuadraturePoint);

          auto global_point_in_U_T = local_grid_geometry.global(local_subgrid_point);

          const double weight_local_quadrature = local_grid_quadrature.weight(localQuadraturePoint) *
                                                 local_grid_geometry.integrationElement(local_subgrid_point);

          auto localized_local_problem_solution_e0 = local_problem_solution_e0.localFunction(local_grid_entity);
          auto localized_local_problem_solution_e1 = local_problem_solution_e1.localFunction(local_grid_entity);

          // grad corrector for e_0 and e_1
          JacobianRangeType grad_loc_sol_e0, grad_loc_sol_e1;
          localized_local_problem_solution_e0.jacobian(local_grid_quadrature[localQuadraturePoint], grad_loc_sol_e0);
          localized_local_problem_solution_e1.jacobian(local_grid_quadrature[localQuadraturePoint], grad_loc_sol_e1);

          JacobianRangeType projection_error_gradient;
          for (int k = 0; k < dimension; ++k)
            projection_error_gradient[0][k] =
                grad_msfem_fine_part[0][k] - (grad_loc_sol_e0[0][k] * grad_msfem_coarse_part[0][0] +
                                              grad_loc_sol_e1[0][k] * grad_msfem_coarse_part[0][1]);

          JacobianRangeType diffusive_flux_projection;
          diffusion_.diffusiveFlux(global_point_in_U_T, projection_error_gradient, diffusive_flux_projection);

          RangeType value(0.0);
          for (int k = 0; k < dimension; ++k) {
            value += pow(diffusive_flux_projection[0][k], 2.0);
          }

          loc_projection_error[global_index_entity] += value * weight_local_quadrature;
          errors["total_projection_error"] += value * weight_local_quadrature;
        }
      }
    }
  }

public:
  // adaptive_refinement
  RangeType adaptive_refinement(const DiscreteFunctionType& msfem_solution,
                                const DiscreteFunctionType& msfem_coarse_part,
                                const DiscreteFunctionType& msfem_fine_part) const {
    DSC_LOG_INFO << "Start computing conservative fluxes..." << std::endl;
    typedef ConservativeFluxProblemSolver<LocalGridDiscreteFunctionType, DiscreteFunctionType, DiffusionOperatorType,
                                          MacroMicroGridSpecifierImp> ConservativeFluxProblemSolverType;
    ConservativeFluxProblemSolverType flux_problem_solver(fineDiscreteFunctionSpace_, diffusion_, specifier_);
    flux_problem_solver.solve_all(subgrid_list_);

    DSC_LOG_INFO << "Conservative fluxes computed successfully." << std::endl;
    DSC_LOG_INFO << "Starting error estimation..." << std::endl;

    const auto number_of_coarse_grid_entities = specifier_.coarseSpace().grid().size(0);
    specifier_.initialize_local_error_manager();

    ErrormapType errors(
        {{"total_coarse_residual", RangeType(0.0)},     {"total_projection_error", RangeType(0.0)},
         {"total_coarse_grid_jumps", RangeType(0.0)},   {"total_conservative_flux_jumps", RangeType(0.0)},
         {"total_approximation_error", RangeType(0.0)}, {"total_fine_grid_jumps", RangeType(0.0)}});

    RangeType total_estimated_error(0.0);

    const DiscreteFunctionSpaceType& coarseDiscreteFunctionSpace = specifier_.coarseSpace();
    const LeafIndexSetType& coarseGridLeafIndexSet = coarseDiscreteFunctionSpace.gridPart().grid().leafIndexSet();

    // error contribution of H^2 ||f||_L2(T):
    std::vector<RangeType> loc_coarse_residual(number_of_coarse_grid_entities, RangeType(0.0));
    std::vector<RangeType> loc_projection_error(number_of_coarse_grid_entities, RangeType(0.0));
    std::vector<RangeType> loc_coarse_grid_jumps(number_of_coarse_grid_entities, RangeType(0.0));
    std::vector<RangeType> loc_conservative_flux_jumps(number_of_coarse_grid_entities, RangeType(0.0));
    coarse_contribution(coarseDiscreteFunctionSpace, msfem_coarse_part, msfem_fine_part, loc_coarse_grid_jumps,
                        loc_conservative_flux_jumps, loc_coarse_residual, loc_projection_error, errors);
    std::vector<RangeType> loc_approximation_error(number_of_coarse_grid_entities, RangeType(0.0));
    std::vector<RangeType> loc_fine_grid_jumps(number_of_coarse_grid_entities, RangeType(0.0));
    fine_contribution(coarseGridLeafIndexSet, msfem_solution, loc_approximation_error, loc_fine_grid_jumps, errors);

    for (auto m : DSC::valueRange(number_of_coarse_grid_entities)) {
      loc_approximation_error[m] = sqrt(loc_approximation_error[m]);
      loc_fine_grid_jumps[m] = sqrt(loc_fine_grid_jumps[m]);
      loc_projection_error[m] = sqrt(loc_projection_error[m]);

      specifier_.set_loc_approximation_error(m, loc_approximation_error[m]);
      specifier_.set_loc_fine_grid_jumps(m, loc_fine_grid_jumps[m]);
      specifier_.set_loc_projection_error(m, loc_projection_error[m]);
    }

    for (auto& error : errors | boost::adaptors::map_values) {
      error = std::sqrt(error);
      total_estimated_error += error;
    }

    DSC_LOG_INFO << std::endl << "Estimated Errors:" << std::endl << std::endl
                 << "Total estimated error = " << total_estimated_error << "." << std::endl << "where: " << std::endl;
    for (auto it : errors) {
      DSC_LOG_INFO << it.first << " = " << it.second << "." << std::endl;
    }

    return total_estimated_error;
  } // adaptive_refinement
};  // end of class MsFEMErrorEstimator

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::initialize_local_error_manager() {
  //!TODO previous imp would append number_of_level_host_entities_ many zeroes every time it was called
  loc_coarse_residual_ = RangeTypeVector(number_of_level_host_entities_, 0.0);
  loc_projection_error_ = RangeTypeVector(number_of_level_host_entities_, 0.0);
  loc_coarse_grid_jumps_ = RangeTypeVector(number_of_level_host_entities_, 0.0);
  loc_conservative_flux_jumps_ = RangeTypeVector(number_of_level_host_entities_, 0.0);
  loc_approximation_error_ = RangeTypeVector(number_of_level_host_entities_, 0.0);
  loc_fine_grid_jumps_ = RangeTypeVector(number_of_level_host_entities_, 0.0);
} // initialize_local_error_manager

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_coarse_residual(std::size_t index, const RangeType& loc_coarse_residual) {
  loc_coarse_residual_[index] = loc_coarse_residual;
}

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_coarse_grid_jumps(std::size_t index, const RangeType& loc_coarse_grid_jumps) {
  loc_coarse_grid_jumps_[index] = loc_coarse_grid_jumps;
}

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_projection_error(std::size_t index, const RangeType& loc_projection_error) {
  loc_projection_error_[index] = loc_projection_error;
}

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_conservative_flux_jumps(std::size_t index,
                                                              const RangeType& loc_conservative_flux_jumps) {
  loc_conservative_flux_jumps_[index] = loc_conservative_flux_jumps;
}

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_approximation_error(std::size_t index, const RangeType& loc_approximation_error) {
  loc_approximation_error_[index] = loc_approximation_error;
}

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_fine_grid_jumps(std::size_t index,
                                                      const MacroMicroGridSpecifier::RangeType& loc_fine_grid_jumps) {
  loc_fine_grid_jumps_[index] = loc_fine_grid_jumps;
}

template <class A, class B, class C, class D, class E>
MsFEMErrorEstimator<A,B,C,D,E>::RangeType MsFEMErrorEstimator<A,B,C,D,E>::get_loc_coarse_residual(std::size_t index) const {
  if (loc_coarse_residual_.empty()) {
    DUNE_THROW(Dune::InvalidStateException,
               "Error! Use: initialize_local_error_manager()-method for the grid specifier first!");
  }
  return loc_coarse_residual_[index];
} // get_loc_coarse_residual

template <class A, class B, class C, class D, class E>
MsFEMErrorEstimator<A,B,C,D,E>::RangeType MsFEMErrorEstimator<A,B,C,D,E>::get_loc_coarse_grid_jumps(std::size_t index) const {
  if (loc_coarse_grid_jumps_.empty()) {
    DUNE_THROW(Dune::InvalidStateException,
               "Error! Use: initialize_local_error_manager()-method for the grid specifier first!");
  }
  return loc_coarse_grid_jumps_[index];
} // get_loc_coarse_grid_jumps

template <class A, class B, class C, class D, class E>
MsFEMErrorEstimator<A,B,C,D,E>::RangeType MsFEMErrorEstimator<A,B,C,D,E>::get_loc_projection_error(std::size_t index) const {
  if (loc_projection_error_.empty()) {
    DUNE_THROW(Dune::InvalidStateException,
               "Error! Use: initialize_local_error_manager()-method for the grid specifier first!");
  }
  return loc_projection_error_[index];
} // get_loc_projection_error

template <class A, class B, class C, class D, class E>
MsFEMErrorEstimator<A,B,C,D,E>::RangeType MsFEMErrorEstimator<A,B,C,D,E>::get_loc_conservative_flux_jumps(std::size_t index) const {
  if (loc_conservative_flux_jumps_.empty()) {
    DUNE_THROW(Dune::InvalidStateException,
               "Error! Use: initialize_local_error_manager()-method for the grid specifier first!");
  }
  return loc_conservative_flux_jumps_[index];
} // get_loc_conservative_flux_jumps

template <class A, class B, class C, class D, class E>
MsFEMErrorEstimator<A,B,C,D,E>::RangeType MsFEMErrorEstimator<A,B,C,D,E>::get_loc_approximation_error(std::size_t index) const {
  if (loc_approximation_error_.empty()) {
    DUNE_THROW(Dune::InvalidStateException,
               "Error! Use: initialize_local_error_manager()-method for the grid specifier first!");
  }
  return loc_approximation_error_[index];
} // get_loc_approximation_error

template <class A, class B, class C, class D, class E>
MsFEMErrorEstimator<A,B,C,D,E>::RangeType MsFEMErrorEstimator<A,B,C,D,E>::get_loc_fine_grid_jumps(std::size_t index) const {
  if (loc_fine_grid_jumps_.empty()) {
    DUNE_THROW(Dune::InvalidStateException,
               "Error! Use: initialize_local_error_manager()-method for the grid specifier first!");
  }
  return loc_fine_grid_jumps_[index];
}

template <class A, class B, class C, class D, class E>
void MsFEMErrorEstimator<A,B,C,D,E>::set_loc_coarse_residual(std::size_t index, const MsFEMErrorEstimator::RangeType &loc_coarse_residual)
{

}

// get_loc_fine_grid_jumps

} // namespace MsFEM {
} // namespace Multiscale {
} // namespace Dune {

#endif // ifndef DUNE_MSFEM_ERRORESTIMATOR_HH
