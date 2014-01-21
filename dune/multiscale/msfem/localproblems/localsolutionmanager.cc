#include <config.h>
#include <boost/format.hpp>
#include <dune/stuff/common/memory.hh>
#include <dune/multiscale/tools/discretefunctionwriter.hh>

#include "localsolutionmanager.hh"

namespace Dune {
namespace Multiscale {
namespace MsFEM {
LocalSolutionManager::LocalSolutionManager(const CoarseEntityType& coarseEntity, LocalGridListType& subgridList,
                                           const MacroMicroGridSpecifierType& gridSpecifier)
  : subgridList_(subgridList)
  , gridSpecifier_(gridSpecifier)
  , subGridPart_(subgridList_.getSubGrid(coarseEntity))
  , localDiscreteFunctionSpace_(subGridPart_)
  , coarseId_(gridSpecifier_.coarseSpace().gridPart().grid().globalIdSet().id(coarseEntity))
  , numBoundaryCorrectors_((gridSpecifier_.simplexCoarseGrid()) ? 1 : 2)
  , numLocalProblems_((gridSpecifier_.simplexCoarseGrid()) ? GridSelector::dimgrid + 1
                                                           : gridSpecifier_.coarseSpace().mapper().maxNumDofs() + 2)
  , localSolutions_(numLocalProblems_)
  , localSolutionLocation_((boost::format("local_problems/_localProblemSolutions_%d") % coarseId_).str()) {
  for (auto& it : localSolutions_)
    it = DSC::make_unique<LocalGridDiscreteFunctionType>("Local problem Solution", localDiscreteFunctionSpace_);
}

LocalSolutionManager::LocalSolutionVectorType& LocalSolutionManager::getLocalSolutions() { return localSolutions_; }

const LocalSolutionManager::LocalGridDiscreteFunctionSpaceType& LocalSolutionManager::space() const {
  return localDiscreteFunctionSpace_;
}

const LocalSolutionManager::LocalGridPartType& LocalSolutionManager::grid_part() const { return subGridPart_; }

void LocalSolutionManager::loadLocalSolutions() {
  // reader for the cell problem data file:
  DiscreteFunctionReader reader(localSolutionLocation_);

  for (unsigned int i = 0; i < numLocalProblems_; ++i) {
    localSolutions_[i]->clear();
    reader.read(i, *(localSolutions_[i]));
  }
  return;
} // loadLocalSolutions

void LocalSolutionManager::saveLocalSolutions() const {
  // reader for the cell problem data file:
  DiscreteFunctionWriter writer(localSolutionLocation_);

  for (auto& it : localSolutions_)
    writer.append(*it);
} // saveLocalSolutions

std::size_t LocalSolutionManager::numBoundaryCorrectors() const { return numBoundaryCorrectors_; }

} // namespace MsFEM {
} // namespace Multiscale {
} // namespace Dune {
