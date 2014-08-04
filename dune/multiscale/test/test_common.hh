#ifndef DUNE_MULTISCALE_TEST_COMMON_HH
#define DUNE_MULTISCALE_TEST_COMMON_HH

#include <config.h>

#include <dune/stuff/test/test_common.hh>
#include <gtest.h>

#include <string>
#include <array>
#include <initializer_list>
#include <vector>

#include <dune/stuff/common/ranges.hh>
#include <dune/stuff/grid/information.hh>
#include <dune/stuff/common/parameter/configcontainer.hh>

#include <dune/multiscale/common/grid_creation.hh>

#include <boost/filesystem.hpp>


using namespace Dune::Stuff::Common;
using namespace Dune::Multiscale;
using namespace std;


string prepend_test_dir(string fn)
{
  boost::filesystem::path path(st_testdata_directory);
  path /= fn;
  return path.string();
}

void set_param(map<string, string> params) {
  for( auto vp : params) {
    DSC_CONFIG.set(vp.first, vp.second);
  }
}

class GridTestBase : public ::testing::TestWithParam<map<string,string>> {

public:

 virtual void SetUp() {
    set_param(GetParam());
    grids_ = make_grids();
  }
 virtual void TearDown() {
   grids_ = decltype(grids_)();
 }

protected:
 decltype(make_grids()) grids_;
};

static const map<string, string> p_small = {{"grids.macro_cells_per_dim", "[4;4;4]"}
                                           ,{"micro_cells_per_macrocell_dim", "[8;8;8]"}
                                           ,{"msfem.oversampling_layers", "0"}
                                           };
static const map<string, string> p_large = {{"grids.macro_cells_per_dim", "[20;20;20]"}
                                           ,{"micro_cells_per_macrocell_dim", "[40;40;40]"}
                                           ,{"msfem.oversampling_layers", "0"}
                                           };
static const map<string, string> p_aniso = {{"grids.macro_cells_per_dim", "[14;4;6]"}
                                           ,{"micro_cells_per_macrocell_dim", "[3;32;8]"}
                                           ,{"msfem.oversampling_layers", "0"}
                                           };
static const map<string, string> p_wover = {{"grids.macro_cells_per_dim", "[14;14;14]"}
                                           ,{"micro_cells_per_macrocell_dim", "[18;18;18]"}
                                           ,{"msfem.oversampling_layers", "6"}
                                           };
static const map<string, string> p_huge  = {{"grids.macro_cells_per_dim", "[40;40;40]"}
                                           ,{"micro_cells_per_macrocell_dim", "[120;120;120]"}
                                           ,{"msfem.oversampling_layers", "0"}
                                           };
static const map<string, string> p_fail  = {{"grids.macro_cells_per_dim", "[12;15;10]"}
                                           ,{"micro_cells_per_macrocell_dim", "[6;7;10]"}
                                           ,{"msfem.oversampling_layers", "0"}
                                           };
#endif // DUNE_MULTISCALE_TEST_COMMON_HH
