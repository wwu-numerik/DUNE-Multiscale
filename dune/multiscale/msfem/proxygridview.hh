#ifndef DUNE_MULTISCALE_PROXYGRIDVIEW_HH
#define DUNE_MULTISCALE_PROXYGRIDVIEW_HH

#include <dune/multiscale/msfem/msfem_traits.hh>
#include <dune/grid/common/gridview.hh>

namespace Dune {
namespace Multiscale {

class LocalGridList;

struct ProxyGridviewTraits : public DefaultLeafGridViewTraits<MsFEMTraits::LocalGridType>
{
};

class ProxyGridview : public GridView<ProxyGridviewTraits>
{
  typedef GridView<ProxyGridviewTraits> BaseType;

public:
  typedef MsFEMTraits::LocalGridType::ctype ctype;
  static auto constexpr dimension = CommonTraits::world_dim;
  typedef MsFEMTraits::LocalEntityType EntityType;

  template <int cd>
  using Codim = MsFEMTraits::LocalGridType::Codim<cd>;

  ProxyGridview(const LocalGridList& localGrids);

private:
  const LocalGridList& localGrids_;
};

} // namespace Dune
} // namespace Multiscale

#endif // DUNE_MULTISCALE_PROXYGRIDVIEW_HH
