#include "../../src/t0-main/strategy/StrategyBase.h"
#include "../../src/t0-main/json.hpp"
#include "sse_config_guard.h"
#include "IControlCenter.h"
#include <fstream>
#include <iostream>
using nlohmann::json;
namespace { bool file2json(const std::string& path, json& out) { std::ifstream file(path.c_str()); if (!file.is_open()) return false; try { file >> out; } catch (...) { return false; } return true; } }
#define EXPORT_FLAG __attribute__((__visibility__("default")))
extern "C" {
EXPORT_FLAG IWCStrategy* get_obj(kungfu::yijinjing::IControlCenter*, const std::string&);
EXPORT_FLAG const char* sse_strategy_build_id();
}
const char* sse_strategy_build_id() { return "sse-strategy-v04-legacy-midmix-sse-20260812"; }
IWCStrategy* get_obj(kungfu::yijinjing::IControlCenter* cc, const std::string& cfg_name) {
    json config; const std::string path = cfg_name.empty() ? "config_sse.json" : cfg_name;
    if (!file2json(path, config)) return nullptr;
    std::string error; if (!sse_strategy_library::validate_config(config, &error)) { std::cerr << "[sse_get_obj] " << error << '\n'; return nullptr; }
    // The old StrategyBase->SsePredictor path consumes a different factor ABI
    // and a different checkpoint format. Keep the exported ABI available, but
    // refuse to start it until the native SSE 50-factor pipeline is wired.
    (void)cc;
    std::cerr << "[sse_get_obj] SSE strategy scaffold is offline-only; native SSE factor pipeline is not wired" << '\n';
    return nullptr;
}
