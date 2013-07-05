// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

#ifndef DHUUNE_MSFEM_TRAITS_HH
#define DHUUNE_MSFEM_TRAITS_HH

#include <dune/multiscale/common/traits.hh>
#include <dune/subgrid/subgrid.hh>

namespace Dune {
template< int D, class R >
class Subgrid;

namespace Multiscale {
namespace MsFEM {
class MacroMicroGridSpecifier;
class SubGridList;
template< class T, class R, class S, class G, class H >
class MsFEMErrorEstimator;

// ! type construction for the MSFEM code
struct MsFEMTraits
{
  typedef MacroMicroGridSpecifier                                                             MacroMicroGridSpecifierType;
  typedef Dune::SubGrid< CommonTraits::GridType::dimension, typename CommonTraits::GridType > SubGridType;
  typedef SubGridList                                                                         SubGridListType;

  // ! -------------------------- MsFEM error estimator ----------------------------
  typedef MsFEMErrorEstimator< typename CommonTraits::DiscreteFunctionType,
                               typename CommonTraits::DiffusionType,
                               typename CommonTraits::FirstSourceType,
                               MacroMicroGridSpecifierType,
                               SubGridListType >
    MsFEMErrorEstimatorType;
  // ! -----------------------------------------------------------------------------

  // the following two may change if we intend to use different meshes on coarse and fine level
  typedef typename CommonTraits::GridType::Codim< 0 >::Entity                   CoarseEntityType;
  typedef typename CommonTraits::DiscreteFunctionSpaceType::BaseFunctionSetType CoarseBaseFunctionSetType;
};
} // namespace MsFEM {
} // namespace Multiscale {
} // namespace Dune {}

#endif // DUNE_MSFEM_TRAITS_HH