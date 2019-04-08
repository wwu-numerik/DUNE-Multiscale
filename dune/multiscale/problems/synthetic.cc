#include <config.h>
#include <dune/common/exceptions.hh>
#include <dune/xt/common/validation.hh>
#include <dune/xt/grid/boundaryinfo.hh>
#include <dune/xt/common/configuration.hh>
#include <math.h>
#include <sstream>

#include "dune/multiscale/problems/base.hh"
#include "synthetic.hh"

namespace Dune {
namespace Multiscale {
namespace Problem {
namespace Synthetic {

static constexpr double M_TWOPI = M_PI * 2.0;

ModelProblemData::ModelProblemData(MPIHelper::MPICommunicator global,
                                   MPIHelper::MPICommunicator local,
                                   Dune::XT::Common::Configuration config_in)
  : IModelProblemData(global, local, config_in)
  , boundaryInfo_(Dune::XT::Grid::NormalBasedBoundaryInfo<typename View::Intersection>::create(boundary_settings()))
  , subBoundaryInfo_()
{
}

std::pair<CommonTraits::DomainType, CommonTraits::DomainType> ModelProblemData::gridCorners() const
{
  CommonTraits::DomainType lowerLeft(0.0);
  CommonTraits::DomainType upperRight(1.0);
  return {lowerLeft, upperRight};
}

const ModelProblemData::BoundaryInfoType& ModelProblemData::boundaryInfo() const
{
  return *boundaryInfo_;
}

const ModelProblemData::SubBoundaryInfoType& ModelProblemData::subBoundaryInfo() const
{
  return subBoundaryInfo_;
}

XT::Common::Configuration ModelProblemData::boundary_settings() const
{
  XT::Common::Configuration boundarySettings;
  if (DXTC_CONFIG.has_sub("problem.boundaryInfo")) {
    boundarySettings = DXTC_CONFIG.sub("problem.boundaryInfo");
  } else {
    boundarySettings["default"] = "dirichlet";
    boundarySettings["compare_tolerance"] = "1e-10";
    switch (CommonTraits::world_dim) {
      case 1:
        break;
      case 2:
        break;
      case 3:
        boundarySettings["neumann.0"] = "[0.0 0.0 1.0]";
        boundarySettings["neumann.1"] = "[0.0 0.0 -1.0]";
    }
  }
  return boundarySettings;
}

static double get_eps(const Dune::XT::Common::Configuration& config_in)
{
  return config_in.get<double>("problem.epsilon", 0.05);
}

Source::Source(MPIHelper::MPICommunicator /*global*/,
               MPIHelper::MPICommunicator /*local*/,
               Dune::XT::Common::Configuration config_in)
  : epsilon_(get_eps(config_in))
{
}

Diffusion::Diffusion(MPIHelper::MPICommunicator /*global*/,
                     MPIHelper::MPICommunicator /*local*/,
                     Dune::XT::Common::Configuration config_in)
  : epsilon_(get_eps(config_in))
{
  MS_LOG_INFO_0 << "Using synthetic diffusion with epsilon = " << epsilon_ << std::endl;
}

ExactSolution::ExactSolution(MPIHelper::MPICommunicator /*global*/,
                             MPIHelper::MPICommunicator /*local*/,
                             Dune::XT::Common::Configuration config_in)
  : epsilon_(get_eps(config_in))
{
}

PURE HOT void Source::evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/) const
{
  constexpr double pi_square = M_PI * M_PI;
  const double x0_eps = (x[0] / epsilon_);
  const double cos_2_pi_x0_eps = cos(M_TWOPI * x0_eps);
  const double sin_2_pi_x0_eps = sin(M_TWOPI * x0_eps);
  const double coefficient_0 = 2.0 * (1.0 / (8.0 * M_PI * M_PI)) * (1.0 / (2.0 + cos_2_pi_x0_eps));
  const double coefficient_1 = (1.0 / (8.0 * M_PI * M_PI)) * (1.0 + (0.5 * cos_2_pi_x0_eps));
  const double sin_2_pi_x0 = sin(M_TWOPI * x[0]);
  const double cos_2_pi_x0 = cos(M_TWOPI * x[0]);
  const double sin_2_pi_x1 = sin(M_TWOPI * x[1]);

  const double d_x0_coefficient_0 =
      pow(2.0 + cos_2_pi_x0_eps, -2.0) * (1.0 / (M_TWOPI)) * (1.0 / epsilon_) * sin_2_pi_x0_eps;

  const auto grad_u = (M_TWOPI * cos_2_pi_x0 * sin_2_pi_x1)
                      + ((-1.0) * epsilon_ * M_PI * (sin_2_pi_x0 * sin_2_pi_x1 * sin_2_pi_x0_eps))
                      + (M_PI * (cos_2_pi_x0 * sin_2_pi_x1 * cos_2_pi_x0_eps));

  const auto d_x0_x0_u =
      -(4.0 * pi_square * sin_2_pi_x0 * sin_2_pi_x1)
      - (2.0 * pi_square * (epsilon_ + (1.0 / epsilon_)) * cos_2_pi_x0 * sin_2_pi_x1 * sin_2_pi_x0_eps)
      - (4.0 * pi_square * sin_2_pi_x0 * sin_2_pi_x1 * cos_2_pi_x0_eps);

  const auto d_x1_x1_u = -(4.0 * pi_square * sin_2_pi_x0 * sin_2_pi_x1)
                         - (2.0 * pi_square * epsilon_ * cos_2_pi_x0 * sin_2_pi_x1 * sin_2_pi_x0_eps);

  y = -(d_x0_coefficient_0 * grad_u) - (coefficient_0 * d_x0_x0_u) - (coefficient_1 * d_x1_x1_u);
}

void Diffusion::evaluate(const DomainType& x, Diffusion::RangeType& ret, const XT::Common::Parameter& /*mu*/) const
{
  const double x0_eps = (x[0] / epsilon_);
  constexpr double inv_pi8pi = 1. / (8.0 * M_PI * M_PI);
  const double cos_eval = cos(M_TWOPI * x0_eps);
  ret[0][0] = 2.0 * inv_pi8pi * (1.0 / (2.0 + cos_eval));
  ret[1][1] = inv_pi8pi * (1.0 + (0.5 * cos_eval));
}

PURE HOT void Diffusion::diffusiveFlux(const DomainType& x,
                                       const Problem::JacobianRangeType& direction,
                                       Problem::JacobianRangeType& flux) const
{
  Diffusion::RangeType eval;
  evaluate(x, eval);
  flux[0][0] = eval[0][0] * direction[0][0];
  flux[0][1] = eval[1][1] * direction[0][1];
} // diffusiveFlux

size_t Diffusion::order(const XT::Common::Parameter& /*mu*/) const
{
  return 2;
}
size_t Source::order(const XT::Common::Parameter& /*mu*/) const
{
  return 1;
} // evaluate
size_t ExactSolution::order(const XT::Common::Parameter& /*mu*/) const
{
  return 1;
}

std::string ExactSolution::name() const
{
  return "synthetic.exact";
}

PURE HOT void ExactSolution::evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/) const
{
  // approximation obtained by homogenized solution + first corrector

  const double x0_eps = (x[0] / epsilon_);
  const double sin_2_pi_x0_eps = sin(M_TWOPI * x0_eps);
  const double x0_2_pi = M_TWOPI * x[0];
  const double x1_2_pi = M_TWOPI * x[1];
  const double sin_2_pi_x0 = sin(x0_2_pi);
  const double cos_2_pi_x0 = cos(x0_2_pi);
  const double sin_2_pi_x1 = sin(x1_2_pi);

  y = sin_2_pi_x0 * sin_2_pi_x1 + (0.5 * epsilon_ * cos_2_pi_x0 * sin_2_pi_x1 * sin_2_pi_x0_eps);
} // evaluate

PURE HOT void
ExactSolution::jacobian(const DomainType& x, JacobianRangeType& grad_u, const XT::Common::Parameter& /*mu*/) const
{
  const double x0_eps = (x[0] / epsilon_);
  const double cos_2_pi_x0_eps = cos(M_TWOPI * x0_eps);
  const double sin_2_pi_x0_eps = sin(M_TWOPI * x0_eps);
  const double x0_2_pi = M_TWOPI * x[0];
  const double x1_2_pi = M_TWOPI * x[1];
  const double sin_2_pi_x0 = sin(x0_2_pi);
  const double cos_2_pi_x0 = cos(x0_2_pi);
  const double sin_2_pi_x1 = sin(x1_2_pi);
  const double cos_2_pi_x1 = cos(x1_2_pi);
  const double eps_pi_sin_2_pi_x0_eps = epsilon_ * M_PI * sin_2_pi_x0_eps;

  grad_u[0][1] = (M_TWOPI * sin_2_pi_x0 * cos_2_pi_x1) + (eps_pi_sin_2_pi_x0_eps * cos_2_pi_x0 * cos_2_pi_x1);

  grad_u[0][0] = (M_TWOPI * cos_2_pi_x0 * sin_2_pi_x1) - (eps_pi_sin_2_pi_x0_eps * sin_2_pi_x0 * sin_2_pi_x1)
                 + (M_PI * cos_2_pi_x0 * sin_2_pi_x1 * cos_2_pi_x0_eps);
}

PURE void DirichletData::evaluate(const DomainType& x, RangeType& y, const XT::Common::Parameter& /*mu*/) const
{

  const bool x0_bound = FloatCmp::eq(x[0], 0.0) || FloatCmp::eq(x[0], 1.0);
  const bool x1_bound = FloatCmp::eq(x[1], 0.0) || FloatCmp::eq(x[1], 1.0);
  if (x0_bound || x1_bound)
    solution_.evaluate(x, y);
} // evaluate

PURE void DirichletData::jacobian(const DomainType& x, JacobianRangeType& y, const XT::Common::Parameter& /*mu*/) const
{
  solution_.jacobian(x, y);
} // jacobian

PURE void NeumannData::evaluate(const DomainType& /*x*/, RangeType& y, const XT::Common::Parameter& /*mu*/) const
{
  y = 0.0;
} // evaluate

} // namespace Nine
} // namespace Problem
} // namespace Multiscale {
} // namespace Dune {
