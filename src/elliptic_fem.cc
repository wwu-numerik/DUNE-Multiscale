#include <config.h>
#include <config.h>
// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

// The following FEM code requires an access to the 'ModelProblemData' class,
// which provides us with information about f, A, \Omega, etc.

#include <dune/multiscale/common/main_init.hh>
#include <dune/multiscale/fem/algorithm.hh>
#include <dune/multiscale/problems/selector.hh>
#include <dune/multiscale/fem/fem_traits.hh>

int main(int argc, char** argv) {
  try {
    using namespace Dune::Multiscale;
    using namespace Dune::Multiscale::FEM;
    init(argc, argv);

    DSC_PROFILER.startTiming("total_cpu");

    const std::string path = DSC_CONFIG_GET("global.datadir", "data/");

    // generate directories for data output
    DSC::testCreateDirectory(path);

    // name of the error file in which the data will be saved
    std::string filename_;
    const auto info = Problem::getModelData();

    const std::string save_filename = std::string(path + "/logdata/ms.log.log");
    DSC_LOG_INFO << "LOG FILE " << std::endl << std::endl;
    DSC_LOG_INFO << "Data will be saved under: " << save_filename << std::endl;

    // refinement_level denotes the grid refinement level for the global problem, i.e. it describes 'H'
    const int refinement_level = DSC_CONFIG_GET("fem.grid_level", 4);

    // name of the grid file that describes the macro-grid:
    const std::string gridName = info->getMacroGridFile();
    DSC_LOG_INFO << "loading dgf: " << gridName << std::endl;

    // create a grid pointer for the DGF file belongig to the macro grid:
    CommonTraits::GridPointerType grid_pointer(gridName);

    // refine the grid 'starting_refinement_level' times:
    Dune::Fem::GlobalRefine::apply(*grid_pointer, refinement_level);

    algorithm(grid_pointer, filename_);

    const auto cpu_time = DSC_PROFILER.stopTiming("total_cpu") / 1000.f;
    DSC_LOG_INFO << "Total runtime of the program: " << cpu_time << "ms" << std::endl;
    return 0;
  }
  catch (Dune::Exception& e) {
    std::cerr << e.what() << std::endl;
  }
  return 1;
} // main
