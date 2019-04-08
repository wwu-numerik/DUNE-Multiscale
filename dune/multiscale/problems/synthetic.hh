// dune-multiscale
// Copyright Holders: Patrick Henning, Rene Milk
// License: BSD 2-Clause License (http://opensource.org/licenses/BSD-2-Clause)

#ifndef DUNE_ELLIPTIC_MODEL_PROBLEM_SPECIFICATION_HH_SYNTHETIC
#define DUNE_ELLIPTIC_MODEL_PROBLEM_SPECIFICATION_HH_SYNTHETIC

#include <dune/multiscale/problems/base.hh>
#include <dune/xt/grid/boundaryinfo.hh>
#include <memory>
#include <string>

#include "dune/multiscale/common/traits.hh"

#ifdef __GNUC__

#define PURE __attribute__((const))
#define HOT __attribute__((hot))
#define ALWAYS_INLINE __attribute__((always_inline)) inline

#else

#define PURE
#define HOT
#define ALWAYS_INLINE inline

#endif

namespace Dune {
namespace Multiscale {
namespace Problem {
/** \addtogroup problem_9 Problem::Nine
 * @{ **/
//! ------------ Synthetic Elliptic Problem -------------------

namespace Synthetic {

struct ModelProblemData : public IModelProblemData
{
  virtual bool hasExactSolution() const final override
  {
    return true;
  }

  ModelProblemData(MPIHelper::MPICommunicator /*global*/,
                   MPIHelper::MPICommunicator /*local*/,
                   Dune::XT::Common::Configuration /*config_in*/);

  const BoundaryInfoType& boundaryInfo() const final override;
  const SubBoundaryInfoType& subBoundaryInfo() const final override;
  std::pair<CommonTraits::DomainType, CommonTraits::DomainType> gridCorners() const final override;

private:
  XT::Common::Configuration boundary_settings() const;
  std::unique_ptr<Dune::XT::Grid::NormalBasedBoundaryInfo<typename View::Intersection>> boundaryInfo_;
  Dune::XT::Grid::AllDirichletBoundaryInfo<typename SubView::Intersection> subBoundaryInfo_;
};

class Source : public Dune::Multiscale::CommonTraits::FunctionBaseType
{
public:
  Source(MPIHelper::MPICommunicator /*global*/,
         MPIHelper::MPICommunicator /*local*/,
         Dune::XT::Common::Configuration config_in);

  PURE HOT void
  evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/ = {}) const final override;
  virtual size_t order(const XT::Common::Parameter& /*mu*/ = {}) const final override;

private:
  const double epsilon_;
};

class Diffusion : public DiffusionBase
{
public:
  Diffusion(MPIHelper::MPICommunicator /*global*/,
            MPIHelper::MPICommunicator /*local*/,
            Dune::XT::Common::Configuration config_in);

  //! currently used in gdt assembler
  virtual void evaluate(const DomainType& x,
                        DiffusionBase::RangeType& y,
                        const XT::Common::Parameter& /*mu*/ = {}) const final override;
  PURE HOT void diffusiveFlux(const DomainType& x,
                              const Problem::JacobianRangeType& direction,
                              Problem::JacobianRangeType& flux) const final override;

  virtual size_t order(const XT::Common::Parameter& /*mu*/ = {}) const final override;

private:
  const double epsilon_;
};

class ExactSolution : public Dune::Multiscale::CommonTraits::FunctionBaseType
{
public:
  ExactSolution(MPIHelper::MPICommunicator global,
                MPIHelper::MPICommunicator /*local*/,
                Dune::XT::Common::Configuration config_in);

  PURE HOT void
  evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/ = {}) const final override;
  PURE HOT void jacobian(const DomainType& x,
                         JacobianRangeType& grad_u,
                         const XT::Common::Parameter& /*mu*/ = {}) const final override;
  virtual size_t order(const XT::Common::Parameter& /*mu*/ = {}) const final override;
  virtual std::string name() const final override;

private:
  const double epsilon_;
};

class DirichletData : public DirichletDataBase
{
public:
  DirichletData(MPIHelper::MPICommunicator global,
                MPIHelper::MPICommunicator local,
                Dune::XT::Common::Configuration config_in)
    : solution_(global, local, config_in)
  {
  }

  PURE void evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/ = {}) const final override;
  PURE void
  jacobian(const DomainType& x, JacobianRangeType& y, const XT::Common::Parameter& /*mu*/ = {}) const final override;

private:
  ExactSolution solution_;
};

class NeumannData : public NeumannDataBase
{
public:
  NeumannData(MPIHelper::MPICommunicator /*global*/,
              MPIHelper::MPICommunicator /*local*/,
              Dune::XT::Common::Configuration config_in)
  {
  }

  PURE void evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/ = {}) const final override;
};

} //! @} namespace Synthetic {
}
} // namespace Multiscale {
} // namespace Dune {

#endif // ifndef DUNE_ELLIPTIC_MODEL_PROBLEM_SPECIFICATION_HH_SYNTHETIC
