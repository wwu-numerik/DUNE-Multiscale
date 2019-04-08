// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

/**
   *  \file discretefunctionwriter.hh
   *  \brief  write a bunch of discrete functions to one file and retrieve 'em
   **/

#ifndef DISCRETEFUNCTIONWRITER_HEADERGUARD
#define DISCRETEFUNCTIONWRITER_HEADERGUARD

#include <fstream>
#include <vector>
#include <cassert>
#include <memory>
#include <unordered_map>

#include <dune/multiscale/common/traits.hh>
#include <dune/multiscale/msfem/msfem_traits.hh>
#include <dune/common/deprecated.hh>
#include <dune/common/exceptions.hh>
#include <dune/xt/common/configuration.hh>
#include <dune/xt/common/filesystem.hh>
#include <dune/xt/common/ranges.hh>

#include <dune/xt/common/memory.hh>
#include <dune/xt/common/type_traits.hh>

#include <boost/filesystem/path.hpp>

namespace Dune {
namespace Multiscale {

struct IOTraits
{
  typedef MsFEMTraits::LocalGridDiscreteFunctionType DiscreteFunctionType;
  typedef std::shared_ptr<DiscreteFunctionType> DiscreteFunction_ptr;
  typedef typename DiscreteFunctionType::SpaceType DiscreteFunctionSpaceType;
  typedef std::vector<DiscreteFunction_ptr> Vector;
  typedef typename DiscreteFunctionSpaceType::GridLayerType GridViewType;
};

class DiskBackend : public boost::noncopyable
{

  void load_disk_functions()
  {
    Dune::XT::Common::test_create_directory(dir_.string());
    // if functions present, load em
  }

public:
  /**
   * \brief DiscreteFunctionWriter
   * \param filename will open fstream at config["global.datadir"]/filename
   *  filename may include additional path components
   * \throws Dune::IOError if config["global.datadir"]/filename cannot be opened
   */
  DiskBackend(const Dune::XT::Common::Configuration& config, const std::string filename = "nonsense_default_for_map")
    : dir_(boost::filesystem::path(config.get("global.datadir", "data")) / filename)
    , index_(0)
  {
  }

  void append(const IOTraits::DiscreteFunction_ptr& /*df*/)
  {
    const std::string fn = (dir_ / Dune::XT::Common::to_string(index_++)).string();
    Dune::XT::Common::test_create_directory(fn);
    DUNE_THROW(NotImplemented, "");
  }

  void read(const unsigned long index, IOTraits::DiscreteFunction_ptr& /*df*/)
  {
    const std::string fn = (dir_ / Dune::XT::Common::to_string(index)).string();
    DUNE_THROW(NotImplemented, "");
  }

private:
  const boost::filesystem::path dir_;
  unsigned int index_;
};

/**
 * \brief simple discrete function to disk writer
 * this class isn't type safe in the sense that different appends may append
 * non-convertible discrete function implementations
 */
class MemoryBackend : public boost::noncopyable
{
public:
  /**
   * \brief DiscreteFunctionWriter
   * \param filename will open fstream at config["global.datadir"]/filename
   *  filename may include additional path components
   * \throws Dune::IOError if config["global.datadir"]/filename cannot be opened
   */
  MemoryBackend(IOTraits::GridViewType& grid_view, const std::string /*filename*/ = "nonsense_default_for_map")
    : space_(MsFEMTraits::SpaceChooserType::make_space(grid_view))
  {
  }

  void append(const IOTraits::DiscreteFunction_ptr& df)
  {
    functions_.push_back(df);
  }

  void read(const unsigned long index, IOTraits::DiscreteFunction_ptr& df)
  {
    if (index < functions_.size()) {
      df = functions_.at(index);
    } else
      DUNE_THROW(InvalidStateException, "requesting function at oob index " << index);
    assert(df != nullptr);
  }

  IOTraits::DiscreteFunctionSpaceType& space()
  {
    return space_;
  }

private:
  IOTraits::DiscreteFunctionSpaceType space_;
  IOTraits::Vector functions_;
};

class DiscreteFunctionIO : public boost::noncopyable
{

  typedef DiscreteFunctionIO ThisType;

  DiscreteFunctionIO() = default;

private:
  static ThisType& instance()
  {
    static ThisType s_this;
    return s_this;
  }

  struct ClearGuard
  {
    ~ClearGuard()
    {
      DiscreteFunctionIO::clear();
    }
  };

  template <class IOMapType, class... Args>
  typename IOMapType::mapped_type& get(IOMapType& map, typename IOMapType::key_type key, Args&&... ctor_args)
  {
    auto it = map.find(key);
    if (it != map.end())
      return it->second;
    std::lock_guard<std::mutex> lock(mutex_);
    auto ptr = std::make_shared<typename IOMapType::mapped_type::element_type>(ctor_args...);
    auto ret = Dune::XT::Common::map_emplace(map, key, std::move(ptr));
    assert(ret.second);
    return ret.first->second;
  }

  DiskBackend& get_disk(const Dune::XT::Common::Configuration& config, std::string filename);
  MemoryBackend& get_memory(std::string filename, IOTraits::GridViewType& grid_view);

  //! this needs to be called before global de-init or else dune fem fails
  static void clear();

public:
  static MemoryBackend& memory(std::string filename, IOTraits::GridViewType& grid_view);
  static DiskBackend& disk(const XT::Common::Configuration& config, std::string filename);

  static ClearGuard clear_guard()
  {
    return ClearGuard();
  }

private:
  std::unordered_map<size_t, std::shared_ptr<MemoryBackend>> memory_;
  std::unordered_map<std::string, std::shared_ptr<DiskBackend>> disk_;
  std::mutex mutex_;

}; // class DiscreteFunctionIO

} // namespace Multiscale {
} // namespace Dune {

#endif // ifndef DISCRETEFUNCTIONWRITER_HEADERGUARD
