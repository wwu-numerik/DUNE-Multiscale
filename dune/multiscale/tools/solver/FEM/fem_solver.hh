#ifndef Elliptic_FEM_Solver_HH
#define Elliptic_FEM_Solver_HH

#include <dune/common/fmatrix.hh>

#include <dune/fem/solver/oemsolver/oemsolver.hh>
#include <dune/fem/solver/inverseoperators.hh>
#include <dune/fem/operator/discreteoperatorimp.hh>

#include <dune/fem/operator/2order/lagrangematrixsetup.hh>
#include <dune/fem/operator/matrix/spmatrix.hh>

#include <dune/multiscale/tools/assembler/righthandside_assembler.hh>
#include <dune/multiscale/tools/assembler/matrix_assembler/elliptic_fem_matrix_assembler.hh>

namespace Dune {
// define a dummy mass term:
template< class FunctionSpaceImp >
class DummyMass
  : public Dune::Fem::Function< FunctionSpaceImp, DummyMass< FunctionSpaceImp > >
{
public:
  typedef FunctionSpaceImp FunctionSpaceType;

private:
  typedef DummyMass< FunctionSpaceType >                     ThisType;
  typedef Dune::Fem::Function< FunctionSpaceType, ThisType > BaseType;

public:
  typedef typename FunctionSpaceType::DomainType DomainType;
  typedef typename FunctionSpaceType::RangeType  RangeType;

  typedef typename FunctionSpaceType::DomainFieldType DomainFieldType;
  typedef typename FunctionSpaceType::RangeFieldType  RangeFieldType;

  typedef DomainFieldType TimeType;

public:
  inline void evaluate(const DomainType& /*x*/,
                       RangeType& y) const {
    // std :: cout << "Do not use the Dummy-class!!!" << std :: endl;
    // abort();
    y[0] = 0.0;
  }

  // dummy implementation
  inline void evaluate(const DomainType& x,
                       const TimeType time,
                       RangeType& y) const {
    std::cout << "WARNING! Wrong call for 'evaluate' method of the MassTerm class (evaluate(x,t,y)). Return 0.0."
              << std::endl;
    return evaluate(x, y);
  }
};

template< class DiscreteFunctionType >
class Elliptic_FEM_Solver
{
public:
  typedef DiscreteFunctionType DiscreteFunction;

  typedef typename DiscreteFunction::DiscreteFunctionSpaceType DiscreteFunctionSpace;

  typedef typename DiscreteFunction::LocalFunctionType LocalFunction;

  typedef typename DiscreteFunctionSpace::LagrangePointSetType LagrangePointSet;

  typedef typename DiscreteFunctionSpace::GridPartType GridPart;

  enum { faceCodim = 1 };

  typedef typename GridPart::IntersectionIteratorType IntersectionIterator;

  typedef typename LagrangePointSet::template Codim< faceCodim >
    ::SubEntityIteratorType
  FaceDofIterator;

  typedef DummyMass< DiscreteFunctionSpace > DummyMassType;

  // ! --------------------- the standard matrix traits -------------------------------------

  struct MatrixTraits
  {
    typedef DiscreteFunctionSpace                          RowSpaceType;
    typedef DiscreteFunctionSpace                          ColumnSpaceType;
    typedef LagrangeMatrixSetup< false >                   StencilType;
    typedef ParallelScalarProduct< DiscreteFunctionSpace > ParallelScalarProductType;

    template< class M >
    struct Adapter
    {
      typedef LagrangeParallelMatrixAdapter< M > MatrixAdapterType;
    };
  };

  // ! --------------------------------------------------------------------------------------

  // ! --------------------- type of fem stiffness matrix -----------------------------------

  typedef SparseRowMatrixOperator< DiscreteFunction, DiscreteFunction, MatrixTraits > FEMMatrix;

  // ! --------------------------------------------------------------------------------------

  // ! --------------- solver for the linear system of equations ----------------------------

  // use Bi CG Stab [OEMBICGSTABOp] or GMRES [OEMGMRESOp] for non-symmetric matrices and CG [CGInverseOp] for symmetric
  // ones.
  // GMRES seems to be more stable, but is extremely slow!
  // typedef OEMBICGSQOp/*OEMBICGSTABOp*/< DiscreteFunction, FEMMatrix > InverseFEMMatrix;
  typedef CGInverseOperator< DiscreteFunction, FEMMatrix > InverseFEMMatrix;

  // ! --------------------------------------------------------------------------------------

private:
  const DiscreteFunctionSpace& discreteFunctionSpace_;

  std::ofstream* data_file_;

public:
  Elliptic_FEM_Solver(const DiscreteFunctionSpace& discreteFunctionSpace)
    : discreteFunctionSpace_(discreteFunctionSpace)
      , data_file_(NULL)
  {}

  Elliptic_FEM_Solver(const DiscreteFunctionSpace& discreteFunctionSpace, std::ofstream& data_file)
    : discreteFunctionSpace_(discreteFunctionSpace)
      , data_file_(&data_file)
  {}

  template< class Stream >
  void oneLinePrint(Stream& stream, const DiscreteFunction& func) {
    typedef typename DiscreteFunction::ConstDofIteratorType
    DofIteratorType;
    DofIteratorType it = func.dbegin();
    stream << "\n" << func.name() << ": [ ";
    for ( ; it != func.dend(); ++it)
      stream << std::setw(5) << *it << "  ";

    stream << " ] " << std::endl;
  } // oneLinePrint

  // - ∇ (A(x,∇u)) + b ∇u + c u = f - divG
  // then:
  // A --> diffusion operator ('DiffusionOperatorType')
  // b --> advective part ('AdvectionTermType')
  // c --> reaction part ('ReactionTermType')
  // f --> 'first' source term, scalar ('SourceTermType')
  // G --> 'second' source term, vector valued ('SecondSourceTermType')

  // homogenous Dirchilet boundary condition!:
  template< class DiffusionOperator, class SourceTerm >
  void solve_dirichlet_zero(const DiffusionOperator& diffusion_op,
                            const SourceTerm& f,
                            DiscreteFunction& solution) {
    const GridPart& gridPart = discreteFunctionSpace_.gridPart();

    // ! define the right hand side assembler tool
    // (for linear and non-linear elliptic and parabolic problems, for sources f and/or G )
    RightHandSideAssembler< DiscreteFunctionType > rhsassembler;

    // ! define the discrete (elliptic) operator that describes our problem
    // ( effect of the discretized differential operator on a certain discrete function )
    DiscreteEllipticOperator< DiscreteFunction, DiffusionOperator, DummyMassType > discrete_elliptic_op(
      discreteFunctionSpace_,
      diffusion_op);
    // discrete elliptic operator (corresponds with FEM Matrix)

    // ! (stiffness) matrix
    FEMMatrix fem_matrix("FEM stiffness matrix", discreteFunctionSpace_, discreteFunctionSpace_);

    // ! right hand side vector
    // right hand side for the finite element method:
    DiscreteFunction fem_rhs("fem newton rhs", discreteFunctionSpace_);
    fem_rhs.clear();

    std::cout << "Solving linear problem." << std::endl;

    if (data_file_)
    {
      if ( data_file_->is_open() )
      {
        *data_file_ << "Solving linear problem with standard FEM and resolution level "
                    << discreteFunctionSpace_.grid().maxLevel() << "." << std::endl;
        *data_file_ << "------------------------------------------------------------------------------" << std::endl;
      }
    }

    // to assemble the computational time
    Dune::Timer assembleTimer;

    // assemble the stiffness matrix
    discrete_elliptic_op.assemble_matrix(fem_matrix);

    std::cout << "Time to assemble standard FEM stiffness matrix: " << assembleTimer.elapsed() << "s" << std::endl;

    if (data_file_)
    {
      if ( data_file_->is_open() )
      {
        *data_file_ << "Time to assemble standard FEM stiffness matrix: " << assembleTimer.elapsed() << "s"
                    << std::endl;
      }
    }

    // assemble right hand side
    rhsassembler.template assemble< 2* DiscreteFunctionSpace::polynomialOrder + 2 >(f, fem_rhs);

    // oneLinePrint( std::cout , fem_rhs );

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
        if ( !(*iit).boundary() )
          continue;

        LocalFunction rhsLocal = fem_rhs.localFunction(*it);
        const LagrangePointSet& lagrangePointSet
          = discreteFunctionSpace_.lagrangePointSet(*it);

        const int face = (*iit).indexInInside();

        FaceDofIterator faceIterator
          = lagrangePointSet.template beginSubEntity< faceCodim >(face);
        const FaceDofIterator faceEndIterator
          = lagrangePointSet.template endSubEntity< faceCodim >(face);
        for ( ; faceIterator != faceEndIterator; ++faceIterator)
          rhsLocal[*faceIterator] = 0;
      }
    }
    // --- end boundary treatment ---

    InverseFEMMatrix fem_biCGStab(fem_matrix, 1e-8, 1e-8, 20000, true /*VERBOSE*/);
    fem_biCGStab(fem_rhs, solution);

    if (data_file_)
    {
      if ( data_file_->is_open() )
      {
        *data_file_ << "---------------------------------------------------------------------------------" << std::endl;
        *data_file_ << "Standard FEM problem solved in " << assembleTimer.elapsed() << "s." << std::endl
                    << std::endl << std::endl;
      }
    }

    // oneLinePrint( std::cout , solution );
  } // solve_dirichlet_zero

  // ! the following methods are not yet implemented, however note that the required tools are
  // ! already available via 'righthandside_assembler.hh' and 'elliptic_fem_matrix_assembler.hh'!

  template< class DiffusionOperatorType, class ReactionTermType, class SourceTermType >
  void solve() {
    std::cout << "No implemented!" << std::endl;
  }

  template< class DiffusionOperatorType, class ReactionTermType, class SourceTermType, class SecondSourceTermType >
  void solve() {
    std::cout << "No implemented!" << std::endl;
  }
};
}

#endif // #ifndef Elliptic_FEM_Solver_HH
