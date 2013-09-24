#include "elliptic_msfem_matrix_assembler.hh"

namespace Dune {
namespace Multiscale {
namespace MsFEM {

DiscreteEllipticMsFEMOperator::DiscreteEllipticMsFEMOperator(
    MacroMicroGridSpecifierType& specifier, const CoarseDiscreteFunctionSpace& coarseDiscreteFunctionSpace,
    // number of layers per coarse grid entity T:  U(T) is created by enrichting T with
    // n(T)-layers:
    MsFEMTraits::SubGridListType& subgrid_list, const DiffusionModel& diffusion_op)
  : specifier_(specifier)
  , coarseDiscreteFunctionSpace_(coarseDiscreteFunctionSpace)
  , subgrid_list_(subgrid_list)
  , diffusion_operator_(diffusion_op)
  , petrovGalerkin_(DSC_CONFIG_GET("msfem.petrov_galerkin", true)) {

  bool silence = false;
  // coarseDiscreteFunctionSpace_ = specifier_.coarseSpace();
  // fineDiscreteFunctionSpace_ = specifier_.fineSpace();
  MsFEMLocalProblemSolverType loc_prob_solver(specifier_.fineSpace(), specifier_, subgrid_list_, diffusion_operator_);

  loc_prob_solver.assemble_all(silence);
}

} // namespace MsFEM {
} // namespace Multiscale {
} // namespace Dune {
