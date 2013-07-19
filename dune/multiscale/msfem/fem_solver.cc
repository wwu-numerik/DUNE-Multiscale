#include "fem_solver.hh"

#include <dune/common/fmatrix.hh>

#include <dune/fem/solver/oemsolver/oemsolver.hh>
#include <dune/fem/operator/discreteoperatorimp.hh>

#include <dune/fem/operator/2order/lagrangematrixsetup.hh>
#include <dune/fem/operator/matrix/spmatrix.hh>

#include <dune/multiscale/common/righthandside_assembler.hh>
#include <dune/multiscale/fem/elliptic_fem_matrix_assembler.hh>
#include <dune/multiscale/fem/fem_traits.hh>
#include <dune/multiscale/common/traits.hh>

namespace Dune {
namespace Multiscale {

//! define a dummy mass term:
template< class FunctionSpaceImp >
class DummyMass
  : public Dune::Fem::Function< FunctionSpaceImp, DummyMass< FunctionSpaceImp > >
{
private:
  typedef FunctionSpaceImp FunctionSpaceType;

  typedef DummyMass< FunctionSpaceType >                     ThisType;
  typedef Dune::Fem::Function< FunctionSpaceType, ThisType > BaseType;

  typedef typename FunctionSpaceType::DomainType DomainType;
  typedef typename FunctionSpaceType::RangeType  RangeType;

  typedef typename FunctionSpaceType::DomainFieldType DomainFieldType;
  typedef typename FunctionSpaceType::RangeFieldType  RangeFieldType;

  typedef DomainFieldType TimeType;

public:
  inline void evaluate(const DomainType& /*x*/,
                       RangeType& y) const {
    DUNE_THROW(Dune::InvalidStateException, "Do not use the Dummy-class!!!");
    y = 0;
  }

  // dummy implementation
  inline void evaluate(const DomainType& x,
                       const TimeType /*time*/,
                       RangeType& y) const {
    DSC_LOG_ERROR << "WARNING! Wrong call for 'evaluate' method of the MassTerm class (evaluate(x,t,y)). Return 0.0."
              << std::endl;
    return evaluate(x, y);
  }
};


Elliptic_FEM_Solver::Elliptic_FEM_Solver(const Elliptic_FEM_Solver::DiscreteFunctionSpace& discreteFunctionSpace)
    : discreteFunctionSpace_(discreteFunctionSpace)
{}


//! - ∇ (A(x,∇u)) + F(x,u,∇u) = f - divG
//! then:
//! A --> diffusion operator ('DiffusionOperatorType')
//! F --> lower order term ('LowerOrderTerm')
//  e.g. F(x,u,∇u) = ∇u b + c u, with
//  b --> advective part (former 'AdvectionTermType')
//  c --> reaction part (former 'ReactionTermType')
//! f --> 'first' source term, scalar ('SourceTermType')
//! G --> 'second' source term, vector valued ('SecondSourceTermType')
void Elliptic_FEM_Solver::solve_dirichlet_zero(const CommonTraits::DiffusionType& diffusion_op,
                                               const std::unique_ptr<const CommonTraits::LowerOrderTermType> &lower_order_term, // lower order term F(x, u(x), grad u(x) )
                                               const CommonTraits::FirstSourceType& f,
                                                     Elliptic_FEM_Solver::DiscreteFunction& solution) const
{
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();

    //! define the right hand side assembler tool
    // (for linear and non-linear elliptic and parabolic problems, for sources f and/or G )
    RightHandSideAssembler< DiscreteFunctionType > rhsassembler;

    //! define the discrete (elliptic) operator that describes our problem
    // ( effect of the discretized differential operator on a certain discrete function )
    Multiscale::FEM::DiscreteEllipticOperator< DiscreteFunction, CommonTraits::DiffusionType> discrete_elliptic_op(
                discreteFunctionSpace_,
                diffusion_op, lower_order_term );
    // discrete elliptic operator (corresponds with FEM Matrix)


  if (DSC_CONFIG_GET("problem.linear", true))
  {
    DSC_LOG_INFO << "Solving linear problem." << std::endl;
    DSC_LOG_INFO << "Solving linear problem with standard FEM and resolution level "
                  << DSC_CONFIG_GET("fem.grid_level", 4) << "." << std::endl;
    DSC_LOG_INFO << "------------------------------------------------------------------------------" << std::endl;

    //! (stiffness) matrix
    FEMMatrix fem_matrix("FEM stiffness matrix", discreteFunctionSpace_, discreteFunctionSpace_);

    //! right hand side vector
    // right hand side for the finite element method:
    DiscreteFunction fem_rhs("fem rhs", discreteFunctionSpace_);
    fem_rhs.clear();

    // to assemble the computational time
    Dune::Timer assembleTimer;

    // assemble the stiffness matrix
    discrete_elliptic_op.assemble_matrix( fem_matrix );

    DSC_LOG_INFO << "Time to assemble standard FEM stiffness matrix: " << assembleTimer.elapsed() << "s" << std::endl;
    
    // assemble right hand side
    rhsassembler.assemble< 2* DiscreteFunctionSpace::polynomialOrder + 2 >(f, fem_rhs);

    // oneLinePrint( DSC_LOG_DEBUG , fem_rhs );

    // --- boundary treatment ---
    // set the dirichlet points to zero (in righ hand side of the fem problem)
    typedef typename DiscreteFunctionSpace::IteratorType EntityIterator;
    EntityIterator endit = discreteFunctionSpace_.end();
    for (EntityIterator it = discreteFunctionSpace_.begin(); it != endit; ++it)
    {
      IntersectionIterator iit = gridPart.ibegin(*it);
      const IntersectionIterator endiit = gridPart.iend(*it);
      for ( ; iit != endiit; ++iit)
      {

        if ( iit->boundary() && ( iit->boundaryId() != 1) )
          continue;

        if ( iit->boundary() ) {
          LocalFunction rhsLocal = fem_rhs.localFunction(*it);
          const LagrangePointSet& lagrangePointSet
                  = discreteFunctionSpace_.lagrangePointSet(*it);

          const int face = iit->indexInInside();

          auto faceIterator
                  = lagrangePointSet.beginSubEntity< faceCodim >(face);
          const auto faceEndIterator
                  = lagrangePointSet.endSubEntity< faceCodim >(face);
          for ( ; faceIterator != faceEndIterator; ++faceIterator)
            rhsLocal[*faceIterator] = 0;
        }
      }
    }
    // --- end boundary treatment ---

    if (DSC_CONFIG_GET("fem.algebraic_solver", "bi_cg_stab" ) == "cg" )
    {
      const InverseFEMMatrix_CG fem_cg(fem_matrix, 1e-8, 1e-8, 20000, DSC_CONFIG_GET("global.cgsolver_verbose", false));
      fem_rhs.communicate();
      fem_cg(fem_rhs, solution);
    }
    else
    {
      const InverseFEMMatrix fem_biCGStab(fem_matrix, 1e-8, 1e-8, 20000, DSC_CONFIG_GET("global.cgsolver_verbose", false));
      fem_rhs.communicate();
      fem_biCGStab(fem_rhs, solution);
    }

    DSC_LOG_INFO << "---------------------------------------------------------------------------------" << std::endl;
    DSC_LOG_INFO << "Standard FEM problem solved in " << assembleTimer.elapsed() << "s." << std::endl << std::endl
              << std::endl;

  } else {
    DSC_LOG_INFO << "Solving non-linear problem." << std::endl;
    DSC_LOG_INFO << "Solving nonlinear problem with FEM + Newton-Method. Resolution level of grid = "
                  << DSC_CONFIG_GET("fem.grid_level", 4) << "." << std::endl;
    DSC_LOG_INFO << "---------------------------------------------------------------------------------" << std::endl;
    
    Dune::Timer assembleTimer;
    //! residual vector
    // current residual
    DiscreteFunction residual("FEM Newton Residual", discreteFunctionSpace_ );
    residual.clear();
    
    DiscreteFunction system_rhs("fem newton rhs", discreteFunctionSpace_ );
    system_rhs.clear();
    
    const int fem_polorder = 2* CommonTraits::DiscreteFunctionSpaceType::polynomialOrder + 2;

    typedef typename DiscreteFunction::DiscreteFunctionSpaceType DiscreteFunctionSpace;
    typedef typename DiscreteFunctionSpace::RangeType  RangeType;

    RangeType relative_newton_error_finescale = 10000.0;
    RangeType rhs_L2_norm = 10000.0;

    //! (stiffness) matrix
    FEMMatrix fem_matrix("FEM stiffness matrix", discreteFunctionSpace_, discreteFunctionSpace_);
    
    int iteration_step = 1;
    // the Newton step for the FEM reference problem (solved with Newton Method):
    // L2-Norm of residual < tolerance ?
    double tolerance = 1e-06;
    while (relative_newton_error_finescale > tolerance)
    {

      // (here: solution = solution from the last iteration step)
      DSC_LOG_INFO << "Newton iteration " << iteration_step << ":" << std::endl;
      Dune::Timer stepAssembleTimer;
      // assemble the stiffness matrix
      discrete_elliptic_op.assemble_jacobian_matrix(solution, fem_matrix);

      DSC_LOG_INFO << "Time to assemble FEM Newton stiffness matrix for current iteration: "
                   << stepAssembleTimer.elapsed() << "s" << std::endl;

      // assemble right hand side
      system_rhs.clear();
      rhsassembler.assemble_for_Newton_method< fem_polorder >( f, diffusion_op, *lower_order_term, solution,
                                                               system_rhs );

      const Dune::Fem::L2Norm< typename CommonTraits::DiscreteFunctionType::GridPartType > l2norm(system_rhs.gridPart());
      rhs_L2_norm = l2norm.norm(system_rhs);
      if (rhs_L2_norm < 1e-10)
      {
        // residual solution almost identical to zero: break
        DSC_LOG_INFO << "Residual solution almost identical to zero. Therefore: break loop." << std::endl;
        DSC_LOG_INFO << "(L^2-Norm of current right hand side = " << rhs_L2_norm << " < 1e-10)" << std::endl;
        break;
      }
      // set Dirichlet Boundary to zero
      // --- boundary treatment ---
      // set the dirichlet points to zero (in righ hand side of the fem problem)
      typedef typename DiscreteFunctionSpace::IteratorType EntityIterator;
      EntityIterator endit = discreteFunctionSpace_.end();
      for (EntityIterator it = discreteFunctionSpace_.begin(); it != endit; ++it)
      {
        IntersectionIterator iit = gridPart.ibegin(*it);
        const IntersectionIterator endiit = gridPart.iend(*it);
        for ( ; iit != endiit; ++iit)
        {

          if ( iit->boundary() && ( iit->boundaryId() != 1) )
            continue;

          if ( iit->boundary() ) {
            LocalFunction rhsLocal = system_rhs.localFunction(*it);
            const LagrangePointSet& lagrangePointSet
                    = discreteFunctionSpace_.lagrangePointSet(*it);

            const int face = iit->indexInInside();

            auto faceIterator
                    = lagrangePointSet.beginSubEntity< faceCodim >(face);
            const auto faceEndIterator
                    = lagrangePointSet.endSubEntity< faceCodim >(face);
            for ( ; faceIterator != faceEndIterator; ++faceIterator)
              rhsLocal[*faceIterator] = 0;
          }
        }
      }
      // --- end boundary treatment ---

      const Fem::OEMBICGSTABOp< DiscreteFunction, FEMMatrix > fem_newton_biCGStab(fem_matrix, 1e-8, 1e-8, 20000, true);
      fem_newton_biCGStab(system_rhs, residual);

      if ( residual.dofsValid() )
      {
        solution += residual;
        relative_newton_error_finescale = l2norm.norm(residual);
        relative_newton_error_finescale /= l2norm.norm(solution);

        DSC_LOG_INFO << "Relative L2-Newton Error = " << relative_newton_error_finescale << std::endl;
        // residual solution almost identical to zero: break
        DSC_LOG_INFO << "Relative L2-Newton Error = " << relative_newton_error_finescale << std::endl;
        if (relative_newton_error_finescale <= tolerance)
        {
          DSC_LOG_INFO << "Since tolerance = " << tolerance << ": break loop." << std::endl;
        }
        residual.clear();
      } else {
        DSC_LOG_INFO << "WARNING! Invalid dofs in 'residual'." << std::endl;
        break;
      }

      iteration_step += 1;
    }

    DSC_LOG_INFO << "Nonlinear Finite Element Problem solved with Newton Method in " << assembleTimer.elapsed() << "s." << std::endl
                 << std::endl << std::endl;

  }// end 'problem.linear <-> else'

    // oneLinePrint( DSC_LOG_DEBUG , solution );
} // solve_dirichlet_zero


//! - ∇ (A(x,∇u)) + F(x,u,∇u) = f - divG
//! then:
//! A --> diffusion operator ('DiffusionOperatorType')
//! F --> lower order term ('LowerOrderTerm')
//  e.g. F(x,u,∇u) = ∇u b + c u, with
//  b --> advective part (former 'AdvectionTermType')
//  c --> reaction part (former 'ReactionTermType')
//! f --> 'first' source term, scalar ('SourceTermType')
//! G --> 'second' source term, vector valued ('SecondSourceTermType')
void Elliptic_FEM_Solver::solve(const CommonTraits::DiffusionType& diffusion_op,
                                const std::unique_ptr<const CommonTraits::LowerOrderTermType> &lower_order_term, // lower order term F(x, u(x), grad u(x) )
                                const CommonTraits::FirstSourceType& f,
                                const CommonTraits::DiscreteFunctionType& dirichlet_extension,
                                const CommonTraits::NeumannBCType& neumann_bc,
                                      Elliptic_FEM_Solver::DiscreteFunction& solution) const
{
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();

    //! define the right hand side assembler tool
    // (for linear and non-linear elliptic and parabolic problems, for sources f and/or G )
    RightHandSideAssembler< DiscreteFunctionType > rhsassembler;

    //! define the discrete (elliptic) operator that describes our problem
    // ( effect of the discretized differential operator on a certain discrete function )
    Multiscale::FEM::DiscreteEllipticOperator< DiscreteFunction, CommonTraits::DiffusionType> discrete_elliptic_op(
                discreteFunctionSpace_,
                diffusion_op, lower_order_term );
    // discrete elliptic operator (corresponds with FEM Matrix)


  if (DSC_CONFIG_GET("problem.linear", true))
  {
    DSC_LOG_INFO << "Solving linear problem." << std::endl;
    DSC_LOG_INFO << "Solving linear problem with standard FEM and resolution level "
                  << DSC_CONFIG_GET("fem.grid_level", 4) << "." << std::endl;
    DSC_LOG_INFO << "------------------------------------------------------------------------------" << std::endl;

    //! (stiffness) matrix
    FEMMatrix fem_matrix("FEM stiffness matrix", discreteFunctionSpace_, discreteFunctionSpace_);

    //! right hand side vector
    // right hand side for the finite element method:
    DiscreteFunction fem_rhs("fem rhs", discreteFunctionSpace_);
    fem_rhs.clear();

    // to assemble the computational time
    Dune::Timer assembleTimer;

    // assemble the stiffness matrix
    discrete_elliptic_op.assemble_matrix( fem_matrix );

    DSC_LOG_INFO << "Time to assemble standard FEM stiffness matrix: " << assembleTimer.elapsed() << "s" << std::endl;
    
    // assemble right hand side
    rhsassembler.assemble< 2* DiscreteFunctionSpace::polynomialOrder + 2 >(f, diffusion_op, dirichlet_extension, neumann_bc, fem_rhs);

    // oneLinePrint( DSC_LOG_DEBUG , fem_rhs );

    // --- boundary treatment ---
    // set the dirichlet points to zero (in righ hand side of the fem problem)
    typedef typename DiscreteFunctionSpace::IteratorType EntityIterator;
    EntityIterator endit = discreteFunctionSpace_.end();
    for (EntityIterator it = discreteFunctionSpace_.begin(); it != endit; ++it)
    {
      IntersectionIterator iit = gridPart.ibegin(*it);
      const IntersectionIterator endiit = gridPart.iend(*it);
      for ( ; iit != endiit; ++iit)
      {

        if ( iit->boundary() && ( iit->boundaryId() != 1) )
          continue;

        if ( iit->boundary() ) {
          LocalFunction rhsLocal = fem_rhs.localFunction(*it);
          const LagrangePointSet& lagrangePointSet
                  = discreteFunctionSpace_.lagrangePointSet(*it);

          const int face = iit->indexInInside();

          auto faceIterator
                  = lagrangePointSet.beginSubEntity< faceCodim >(face);
          const auto faceEndIterator
                  = lagrangePointSet.endSubEntity< faceCodim >(face);
          for ( ; faceIterator != faceEndIterator; ++faceIterator)
            rhsLocal[*faceIterator] = 0;
        }
      }
    }
    // --- end boundary treatment ---

    if (DSC_CONFIG_GET("fem.algebraic_solver", "bi_cg_stab" ) == "cg" )
    {
      const InverseFEMMatrix_CG fem_cg(fem_matrix, 1e-8, 1e-8, 20000, DSC_CONFIG_GET("global.cgsolver_verbose", false));
      fem_rhs.communicate();
      fem_cg(fem_rhs, solution);
    }
    else
    {
      const InverseFEMMatrix fem_biCGStab(fem_matrix, 1e-8, 1e-8, 20000, DSC_CONFIG_GET("global.cgsolver_verbose", false));
      fem_rhs.communicate();
      fem_biCGStab(fem_rhs, solution);
    }

    DSC_LOG_INFO << "---------------------------------------------------------------------------------" << std::endl;
    DSC_LOG_INFO << "Standard FEM problem solved in " << assembleTimer.elapsed() << "s." << std::endl << std::endl
              << std::endl;

  } else {
    DSC_LOG_INFO << "Solving non-linear problem." << std::endl;
    DSC_LOG_INFO << "Solving nonlinear problem with FEM + Newton-Method. Resolution level of grid = "
                  << DSC_CONFIG_GET("fem.grid_level", 4) << "." << std::endl;
    DSC_LOG_INFO << "---------------------------------------------------------------------------------" << std::endl;
    
    Dune::Timer assembleTimer;
    //! residual vector
    // current residual
    DiscreteFunction residual("FEM Newton Residual", discreteFunctionSpace_ );
    residual.clear();
    
    DiscreteFunction system_rhs("fem newton rhs", discreteFunctionSpace_ );
    system_rhs.clear();
    
    const int fem_polorder = 2* CommonTraits::DiscreteFunctionSpaceType::polynomialOrder + 2;

    typedef typename DiscreteFunction::DiscreteFunctionSpaceType DiscreteFunctionSpace;
    typedef typename DiscreteFunctionSpace::RangeType  RangeType;

    RangeType relative_newton_error_finescale = 10000.0;
    RangeType rhs_L2_norm = 10000.0;

    //! (stiffness) matrix
    FEMMatrix fem_matrix("FEM stiffness matrix", discreteFunctionSpace_, discreteFunctionSpace_);
    
    int iteration_step = 1;
    // the Newton step for the FEM reference problem (solved with Newton Method):
    // L2-Norm of residual < tolerance ?
    double tolerance = 1e-06;
    while (relative_newton_error_finescale > tolerance)
    {

      // (here: solution = solution from the last iteration step)
      DSC_LOG_INFO << "Newton iteration " << iteration_step << ":" << std::endl;
      Dune::Timer stepAssembleTimer;
      // assemble the stiffness matrix
      discrete_elliptic_op.assemble_jacobian_matrix(solution, dirichlet_extension, fem_matrix);

      DSC_LOG_INFO << "Time to assemble FEM Newton stiffness matrix for current iteration: "
                   << stepAssembleTimer.elapsed() << "s" << std::endl;

      // assemble right hand side
      system_rhs.clear();
      rhsassembler.assemble_for_Newton_method< fem_polorder >( f, diffusion_op, *lower_order_term, solution,
                                                               dirichlet_extension, neumann_bc, system_rhs );

      const Dune::Fem::L2Norm< typename CommonTraits::DiscreteFunctionType::GridPartType > l2norm(system_rhs.gridPart());
      rhs_L2_norm = l2norm.norm(system_rhs);
      if (rhs_L2_norm < 1e-10)
      {
        // residual solution almost identical to zero: break
        DSC_LOG_INFO << "Residual solution almost identical to zero. Therefore: break loop." << std::endl;
        DSC_LOG_INFO << "(L^2-Norm of current right hand side = " << rhs_L2_norm << " < 1e-10)" << std::endl;
        break;
      }
      // set Dirichlet Boundary to zero
      // --- boundary treatment ---
      // set the dirichlet points to zero (in righ hand side of the fem problem)
      typedef typename DiscreteFunctionSpace::IteratorType EntityIterator;
      EntityIterator endit = discreteFunctionSpace_.end();
      for (EntityIterator it = discreteFunctionSpace_.begin(); it != endit; ++it)
      {
        IntersectionIterator iit = gridPart.ibegin(*it);
        const IntersectionIterator endiit = gridPart.iend(*it);
        for ( ; iit != endiit; ++iit)
        {

          if ( iit->boundary() && ( iit->boundaryId() != 1) )
            continue;

          if ( iit->boundary() ) {
            LocalFunction rhsLocal = system_rhs.localFunction(*it);
            const LagrangePointSet& lagrangePointSet
                    = discreteFunctionSpace_.lagrangePointSet(*it);

            const int face = iit->indexInInside();

            auto faceIterator
                    = lagrangePointSet.beginSubEntity< faceCodim >(face);
            const auto faceEndIterator
                    = lagrangePointSet.endSubEntity< faceCodim >(face);
            for ( ; faceIterator != faceEndIterator; ++faceIterator)
              rhsLocal[*faceIterator] = 0;
          }
        }
      }
      // --- end boundary treatment ---

      const Fem::OEMBICGSTABOp< DiscreteFunction, FEMMatrix > fem_newton_biCGStab(fem_matrix, 1e-8, 1e-8, 20000, true);
      fem_newton_biCGStab(system_rhs, residual);

      if ( residual.dofsValid() )
      {
        solution += residual;
        relative_newton_error_finescale = l2norm.norm(residual);
        relative_newton_error_finescale /= l2norm.norm(solution);

        DSC_LOG_INFO << "Relative L2-Newton Error = " << relative_newton_error_finescale << std::endl;
        // residual solution almost identical to zero: break
        DSC_LOG_INFO << "Relative L2-Newton Error = " << relative_newton_error_finescale << std::endl;
        if (relative_newton_error_finescale <= tolerance)
        {
          DSC_LOG_INFO << "Since tolerance = " << tolerance << ": break loop." << std::endl;
        }
        residual.clear();
      } else {
        DSC_LOG_INFO << "WARNING! Invalid dofs in 'residual'." << std::endl;
        break;
      }

      iteration_step += 1;
    }

    DSC_LOG_INFO << "Nonlinear Finite Element Problem solved with Newton Method in " << assembleTimer.elapsed() << "s." << std::endl
                 << std::endl << std::endl;

  }// end 'problem.linear <-> else'

    // oneLinePrint( DSC_LOG_DEBUG , solution );
} // end solve()


} // namespace Multiscale
}


