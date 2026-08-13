#ifndef T0_MAIN_DEEPWIN_STRATEGY_SSE_CONFIG_GUARD_H
#define T0_MAIN_DEEPWIN_STRATEGY_SSE_CONFIG_GUARD_H
#include "../../src/t0-main/json.hpp"
#include <string>
namespace sse_strategy_library {
bool validate_config(const nlohmann::json& config, std::string* error);
}
#endif
