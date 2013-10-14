#ifndef DUNE_MULTISCALE_LA_BACKEND_HH
#define DUNE_MULTISCALE_LA_BACKEND_HH

#include <dune/fem/function/adaptivefunction.hh>
#include <dune/fem/operator/matrix/spmatrix.hh>
#include <dune/fem/operator/linear/spoperator.hh>
#ifdef USE_PETSC_BACKEND
#include <dune/fem/function/petscdiscretefunction/petscdiscretefunction.hh>
#include <dune/fem/operator/linear/petscoperator.hh>
#include <dune/fem/solver/petscsolver.hh>
#endif
#include <dune/fem/solver/oemsolver.hh>

namespace Dune {

template <bool T>
class LagrangeMatrixSetup;

namespace Fem {
template <class T>
class LagrangeParallelMatrixAdapter;
template <class T>
class ParallelScalarProduct;
template <class T, class R, class S>
class SparseRowMatrixOperator;
} // namespace Fem


namespace Multiscale {

template <class DiscreteFunctionType, class LinearOperatorType>
class FemSolverWrapper
{
public:
  FemSolverWrapper( LinearOperatorType& op,
           double redEps,
           double absLimit,
           int maxIter,
                    bool verbose, std::string type = "bcgs", std::string precond = "none", int precond_iterations = 1 )
    : op_(op)
    , redEps_(redEps)
    , absLimit_(absLimit)
    , maxIter_(maxIter)
    , verbose_(verbose)
    , type_(type)
    , precond_(precond)
    , precond_iterations_(precond_iterations)
  {}

  void apply(const DiscreteFunctionType& arg, DiscreteFunctionType& dest) const
  {
    Dune::Fem::OEMBICGSTABOp<DiscreteFunctionType,LinearOperatorType>(op_, redEps_, absLimit_, maxIter_, verbose_).apply(arg, dest);
  }

  void operator() (const DiscreteFunctionType& arg, DiscreteFunctionType& dest) const
  {
    apply(arg, dest);
  }

private:
  LinearOperatorType& op_;
  const double redEps_;
  const double absLimit_;
  const int maxIter_;
  const bool verbose_;
  const std::string type_;
  const std::string precond_;
  const int precond_iterations_;

};

template <class DiscreteFunctionSpaceType>
struct BackendChooser {

#ifdef USE_PETSC_BACKEND
  typedef Dune::Fem::PetscDiscreteFunction<DiscreteFunctionSpaceType> DiscreteFunctionType;
  typedef Dune::Fem::PetscLinearOperator<DiscreteFunctionType, DiscreteFunctionType> LinearOperatorType;
  typedef Dune::Fem::PetscInverseOperator<DiscreteFunctionType, LinearOperatorType> InverseOperatorType;
#else
  typedef Dune::Fem::AdaptiveDiscreteFunction<DiscreteFunctionSpaceType> DiscreteFunctionType;
  typedef Dune::Fem::SparseRowLinearOperator<DiscreteFunctionType, DiscreteFunctionType> LinearOperatorType;
  typedef FemSolverWrapper<DiscreteFunctionType, LinearOperatorType> InverseOperatorType;
#endif


};

} // namespace Multiscale
} // namespace Dune

#endif // DUNE_MULTISCALE_LA_BACKEND_HH
