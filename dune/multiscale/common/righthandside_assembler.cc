#include <config.h>
#include <assert.h>
#include <dune/common/exceptions.hh>
#include <dune/multiscale/problems/selector.hh>
#include <dune/stuff/common/ranges.hh>
#include <dune/stuff/functions/femadapter.hh>
#include <dune/stuff/common/profiler.hh>
#include <dune/fem/misc/threads/domainthreaditerator.hh>
#include <memory>

#include <dune/stuff/fem/functions/integrals.hh>
#include <dune/stuff/functions/femadapter.hh>
#include <dune/multiscale/common/traits.hh>
#include <dune/multiscale/tools/misc.hh>
#include <dune/multiscale/common/dirichletconstraints.hh>
#include <dune/multiscale/msfem/localproblems/subgrid-list.hh>
#include <dune/multiscale/msfem/localproblems/localsolutionmanager.hh>

#include "righthandside_assembler.hh"

void Dune::Multiscale::RightHandSideAssembler::assemble_msfem(
    const CommonTraits::DiscreteFunctionSpaceType& coarse_space,
    const Dune::Multiscale::CommonTraits::FirstSourceType& f, DMM::LocalGridList& subgrid_list,
    Dune::Multiscale::CommonTraits::DiscreteFunctionType& rhsVector) {

  // cache grid variable
  bool isSimplexGrid = DSG::is_simplex_grid(coarse_space);
  
  static constexpr int dimension = CommonTraits::GridType::dimension;
  DSC_PROFILER.startTiming("msfem.assembleRHS");
  const auto& diffusion = *Problem::getDiffusion();
  const auto& neumannData = *Problem::getNeumannData();

  rhsVector.clear();
  RangeType f_x;
  Fem::DomainDecomposedIteratorStorage<CommonTraits::GridPartType> threadIterators(rhsVector.space().gridPart());

#ifdef _OPENMP
#pragma omp parallel
#endif
  {
    for (const auto& coarse_grid_entity : threadIterators) {
      const auto& coarseGeometry = coarse_grid_entity.geometry();
      auto rhsLocalFunction = rhsVector.localFunction(coarse_grid_entity);
      const auto numLocalBaseFunctions = rhsLocalFunction.numDofs();
      const auto& coarseBaseSet = coarse_space.basisFunctionSet(coarse_grid_entity);

      // --------- add corrector contribution of right hand side --------------------------
      // Load local solutions
      MsFEM::LocalSolutionManager localSolutionManager(coarse_space, coarse_grid_entity, subgrid_list);
      localSolutionManager.load();
      auto& localSolutions = localSolutionManager.getLocalSolutions();
      assert(localSolutions.size() > 0);
      MsFEM::MsFEMTraits::LocalGridDiscreteFunctionType dirichletExtension("Dirichlet Extension",
                                                                           localSolutionManager.space());
      dirichletExtension.clear();
      Dune::Multiscale::copyDirichletValues(rhsVector.space(), dirichletExtension);

      // iterator for the micro grid ( grid for the reference element T_0 )
      const auto& subGrid = subgrid_list.getSubGrid(coarse_grid_entity);
      auto view = subGrid.leafGridView();
      for (const auto& localEntity : DSC::viewRange(view)) {
        if (subgrid_list.covers(coarse_grid_entity, localEntity)) {
          // higher order quadrature, since A^{\epsilon} is highly variable
          const auto localQuadrature = DSFe::make_quadrature(localEntity, localSolutionManager.space());
          auto dirichletExtensionLF = dirichletExtension.localFunction(localEntity);

          // evaluate all local solutions and their jacobians in all quadrature points
          std::vector<std::vector<RangeType>> allLocalSolutionEvaluations(
              localSolutions.size(), std::vector<RangeType>(localQuadrature.nop(), 0.0));
          std::vector<std::vector<JacobianRangeType>> allLocalSolutionJacobians(
              localSolutions.size(), std::vector<JacobianRangeType>(localQuadrature.nop(), JacobianRangeType(0.0)));
          for (auto lsNum : DSC::valueRange(localSolutions.size())) {
            auto localFunction = localSolutions[lsNum]->localFunction(localEntity);
            // this evaluates the local solutions in all quadrature points...
            localFunction.evaluateQuadrature(localQuadrature, allLocalSolutionEvaluations[lsNum]);
            // while this automatically evaluates their jacobians.
            localFunction.evaluateQuadrature(localQuadrature, allLocalSolutionJacobians[lsNum]);

            // assemble intersection-part
            const auto& subGridPart = localSolutionManager.grid_part();
            for (const auto& intersection : DSC::intersectionRange(subGridPart.grid().leafGridView(), localEntity)) {
              if (DMP::is_neumann(intersection)) {
                const int orderOfIntegrand =
                    (CommonTraits::polynomial_order - 1) + 2 * (CommonTraits::polynomial_order + 1);
                const int quadOrder = std::ceil((orderOfIntegrand + 1) / 2);
                const auto faceQuad = DSFe::make_quadrature(intersection, localSolutions[lsNum]->space(), quadOrder);
                RangeType neumannValue(0.0);
                const auto numQuadPoints = faceQuad.nop();
                // loop over all quadrature points

                std::vector<RangeType> phi_x_vec(numLocalBaseFunctions);
                std::vector<RangeType> localSolutionOnFace(numLocalBaseFunctions);
                localFunction.evaluateQuadrature(faceQuad, localSolutionOnFace);

                for (unsigned int iqP = 0; iqP < numQuadPoints; ++iqP) {
                  // get local coordinate of quadrature point
                  const auto& xLocal = faceQuad.localPoint(iqP);
                  const auto& faceGeometry = intersection.geometry();
                  const auto& xGlobal = faceGeometry.global(xLocal);
                  const auto& xInCoarseLocal = coarse_grid_entity.geometry().local(xGlobal);
                  const double factor = faceGeometry.integrationElement(xLocal) * faceQuad.weight(iqP);

                  neumannData.evaluate(xGlobal, neumannValue);
                  coarseBaseSet.evaluateAll(xInCoarseLocal, phi_x_vec);
                  for (auto i : DSC::valueRange(numLocalBaseFunctions)) {
                    assert((long long)i < (long long)phi_x_vec.size());
                    assert(iqP < localSolutionOnFace.size());
                    rhsLocalFunction[i] += factor * (neumannValue * (phi_x_vec[i] + localSolutionOnFace[iqP]));
                  }
                }
              }
            }
          }

          // assemble element-part
          const auto& localGeometry = localEntity.geometry();
          for (size_t qP = 0; qP < localQuadrature.nop(); ++qP) {
            // local (barycentric) coordinates (with respect to entity)
            const auto& quadPoint = localQuadrature.point(qP);
            const auto quadPointGlobal = localGeometry.global(quadPoint);

            const double quadWeight = localQuadrature.weight(qP) * localGeometry.integrationElement(quadPoint);

            // evaluate gradient of basis function
            const auto quadPointLocalInCoarse = coarseGeometry.local(quadPointGlobal);
            std::vector<RangeType> coarseBaseEvals(numLocalBaseFunctions);
            std::vector<JacobianRangeType> coarseBaseJacs(numLocalBaseFunctions);
            coarseBaseSet.evaluateAll(quadPointLocalInCoarse, coarseBaseEvals);
            coarseBaseSet.jacobianAll(quadPointLocalInCoarse, coarseBaseJacs);

            for (int coarseBF = 0; coarseBF < numLocalBaseFunctions; ++coarseBF) {
              JacobianRangeType diffusive_flux(0.0);

              JacobianRangeType reconstructionGradPhi(coarseBaseJacs[coarseBF]);
              RangeType reconstructionPhi(coarseBaseEvals[coarseBF]);

              if (isSimplexGrid) {
                assert(localSolutions.size() == dimension + localSolutionManager.numBoundaryCorrectors());
                for (const auto& i : DSC::valueRange(dimension))
                  reconstructionPhi += coarseBaseJacs[coarseBF][0][i] * allLocalSolutionEvaluations[i][qP];
                //! @todo add the dirichlet and neumann-correctors!
              } else {
                assert(localSolutions.size() == numLocalBaseFunctions + localSolutionManager.numBoundaryCorrectors());
                // local corrector for coarse base func
                reconstructionPhi += allLocalSolutionEvaluations[coarseBF][qP];
                // element part of boundary conditions
                JacobianRangeType directionOfFlux(0.0);
                //! @attention At this point we assume, that the quadrature points on the subgrid and hostgrid
                //! are the same (dirichletExtensionLF is a localfunction on the hostgrid, quadPoint stems from
                //! a quadrature on the subgrid)!!
                dirichletExtensionLF.jacobian(quadPoint, directionOfFlux);
                // add dirichlet-corrector
                directionOfFlux += allLocalSolutionJacobians[numLocalBaseFunctions + 1][qP];
                // subtract neumann-corrector
                directionOfFlux -= allLocalSolutionJacobians[numLocalBaseFunctions][qP];

                diffusion.diffusiveFlux(quadPointGlobal, directionOfFlux, diffusive_flux);
                reconstructionGradPhi += allLocalSolutionJacobians[coarseBF][qP];
              }
              f.evaluate(quadPointGlobal, f_x);

              rhsLocalFunction[coarseBF] += quadWeight * (f_x * reconstructionPhi);
              rhsLocalFunction[coarseBF] -= quadWeight * (diffusive_flux[0] * reconstructionGradPhi[0]);
            }
          }
        }
      }
    } // for
  }   // omp region

  // set dirichlet dofs to zero
  Dune::Multiscale::getConstraintsCoarse(rhsVector.space()).setValue(0.0, rhsVector);
  rhsVector.communicate();
  DSC_LOG_INFO << "Time to assemble and communicate MsFEM rhs: " << DSC_PROFILER.stopTiming("msfem.assembleRHS") << "ms"
               << std::endl;
}
