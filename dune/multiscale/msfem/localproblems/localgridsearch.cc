#include <config.h>

#include "localgridsearch.hh"

#include <dune/multiscale/msfem/localproblems/localgridlist.hh>
#include <dune/xt/common/memory.hh>
#include <dune/xt/common/algorithm.hh>
#include <dune/grid/common/gridenums.hh>

Dune::Multiscale::LocalGridSearch::EntityVectorType Dune::Multiscale::LocalGridSearch::
operator()(const PointContainerType& points)
{
  typedef typename EntityVectorType::value_type EPV;
  const auto is_null = [&](const EPV& ptr) { return ptr == nullptr; };
  const auto not_null = [&](const EPV& ptr) { return ptr != nullptr; };

  // only iterate over inner (non-overlap), coarse entities
  const auto& view = static_view_;

  if (!static_iterator_)
    static_iterator_ =
        Dune::XT::Common::make_unique<InteriorIteratorType>(view.begin<0, CommonTraits::InteriorBorderPartition>());
  auto& it = *static_iterator_;
  const auto end = view.end<0, CommonTraits::InteriorBorderPartition>();

  EntityVectorType ret_entities(points.size());
  int steps = 0;
  bool did_cover = false;
  auto null_count = points.size();
  while (null_count) {
    assert(it != end);
    const auto& coarse_entity = *it;
    const auto& localgrid = gridlist_.getSubGrid(coarse_entity);
    const auto& index_set = view.grid().leafIndexSet();
    const auto index = index_set.index(coarse_entity);
    current_coarse_entity_ = Dune::XT::Common::make_unique<MsFEMTraits::CoarseEntityType>(coarse_entity);
    auto& current_search_ptr = coarse_searches_[index];
    if (current_search_ptr == nullptr)
      current_search_ptr = Dune::XT::Common::make_unique<PerGridSearchType>(localgrid.leafGridView());
    did_cover = covers_strict(coarse_entity, points.begin(), points.end());
    if (did_cover) {
      auto first_null = std::find(ret_entities.begin(), ret_entities.end(), nullptr);
      auto entity_ptrs = current_search_ptr->operator()(points);
      Dune::XT::Common::move_if(entity_ptrs.begin(), entity_ptrs.end(), first_null, not_null);
      null_count = std::count_if(ret_entities.begin(), ret_entities.end(), is_null);
    }
    if (++it == end) {
      if (++steps < view.size(0))
        it = view.begin<0, CommonTraits::InteriorBorderPartition>();
      else {
        DUNE_THROW(InvalidStateException, "local grid search failed ");
      }
    }
  }
  return ret_entities;
}

bool Dune::Multiscale::LocalGridSearch::covers_strict(const CommonTraits::SpaceType::EntityType& coarse_entity,
                                                      const Dune::Multiscale::LocalGridSearch::PointIterator first,
                                                      const Dune::Multiscale::LocalGridSearch::PointIterator last)
{
  const auto& reference_element = XT::Grid::reference_element(coarse_entity);
  const auto& coarse_geometry = coarse_entity.geometry();
  for (auto it = first; it != last; ++it) {
    if (!reference_element.checkInside(coarse_geometry.local(*it)))
      return false;
  }
  return true;
}

Dune::Multiscale::LocalGridSearch::LocalGridSearch(const CommonTraits::SpaceType& coarse_space,
                                                   const Dune::Multiscale::LocalGridList& gridlist)
  : coarse_space_(coarse_space)
  , gridlist_(gridlist)
  , static_view_(coarse_space_.grid_layer().grid().leafGridView())
  , static_iterator_(nullptr)
{
}

Dune::Multiscale::LocalGridSearch::LocalGridSearch(const Dune::Multiscale::LocalGridSearch& other)
  : coarse_space_(other.coarse_space_)
  , gridlist_(other.gridlist_)
  , static_view_(coarse_space_.grid_layer().grid().leafGridView())
  , static_iterator_(nullptr)
{
}

const Dune::Multiscale::MsFEMTraits::CoarseEntityType& Dune::Multiscale::LocalGridSearch::current_coarse_pointer() const
{
  assert(current_coarse_entity_);
  return *current_coarse_entity_;
}
