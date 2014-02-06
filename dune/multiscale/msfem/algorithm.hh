// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

#ifndef DUNE_MULTISCALE_MSFEM_ALGORITHM_HH
#define DUNE_MULTISCALE_MSFEM_ALGORITHM_HH

#include <dune/multiscale/common/traits.hh>
#include <dune/multiscale/msfem/msfem_traits.hh>
#include <string>
#include <vector>

namespace Dune {
namespace Multiscale {

struct OutputParameters;

namespace MsFEM {

class LocalGridList;

//! \TODO docme
void solution_output(const CommonTraits::DiscreteFunction_ptr& msfem_solution,
                     const CommonTraits::DiscreteFunction_ptr& coarse_part_msfem_solution,
                     const CommonTraits::DiscreteFunction_ptr& fine_part_msfem_solution);

//! \TODO docme
void data_output(const CommonTraits::GridPartType& gridPart,
                 const CommonTraits::DiscreteFunctionSpaceType& discreteFunctionSpace_coarse);

//! \TODO docme
void algorithm();

} // namespace MsFEM {
} // namespace Multiscale {
} // namespace Dune {

#endif // DUNE_MULTISCALE_MSFEM_ALGORITHM_HH
