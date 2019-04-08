// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

#ifndef DUNE_FEM_ALGORITHM_HH
#define DUNE_FEM_ALGORITHM_HH

#include <dune/xt/grid/layers.hh>
#include <dune/xt/la/container.hh>

#include <dune/gdt/spaces/interface.hh>

#include <dune/multiscale/common/traits.hh>

namespace Dune {
namespace Multiscale {

//! the main FEM computation
std::map<std::string, double> cgfem_algorithm();

} // namespace Multiscale {
} // namespace Dune {

#endif // DUNE_FEM_ALGORITHM_HH
