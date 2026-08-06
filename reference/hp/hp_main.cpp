#include "LogWrapper/LogWrapper.h"
#include "SpdlogWrapper.h"
#include "Tsc.h"
#include "algorithm.h"
#include "hp_common.h"
#include "hp_impl.h"
#include "hp_prediction.h"
#include "magic_enum.hpp"
#include "mercury.h"

using namespace ivo::mercury;

int main(int argc, char** argv) {
  StrategyConfig config;
  config.Init(argv[1]);

  const auto& app_name = config.app_name;
  IVO_ASSERT(ivolib::contains(app_name, "Hp"));
  bool sh = ivolib::contains(app_name, "HpH");

  auto model_string = config.GetRequiredConfig<std::string>("model");
  auto model = magic_enum::enum_cast<ModelType>(model_string);
  IVO_ASSERT(model.has_value(), "Invalid Model {}", model_string);

  switch (model.value()) {
  case Model21F:
  case DSChange21: {
    HpOptions<Just21F> opts;
    return StartStrategy(argc, argv, &opts);
  }

  case Leading21F: {
    IVO_ASSERT(sh);
    HpOptions<LeadingModel<PredictionLeading21F>> opts;
    return StartStrategy(argc, argv, &opts);
  }

  case DSChange: {
    if (sh) {
      HpOptions<LeadingModel<PredictionLeadingDSChange>> opts;
      return StartStrategy(argc, argv, &opts);
    } else {
      HpOptions<NotLeadingTrade<PredictionDSChange>> opts;
      return StartStrategy(argc, argv, &opts);
    }
  }

  case Infinity: {
    IVO_ASSERT(!sh);
    HpOptions<NotLeadingTrade<PredictionInfinity>> opts;
    return StartStrategy(argc, argv, &opts);
  }

  default:
    IVO_ASSERT(false, "Unreachable path");
  }

  return 0;
}
