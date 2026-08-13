#include "sse_config_guard.h"
namespace sse_strategy_library {
namespace { bool reject(const char* text, std::string* error) { if (error) *error = text; return false; } }
bool validate_config(const nlohmann::json& config, std::string* error) {
    if (error) error->clear();
    nlohmann::json::const_iterator market = config.find("market");
    if (market == config.end()) return reject("SSE strategy library requires config field market=SH", error);
    if (!market->is_string()) return reject("SSE strategy library requires string config field market=SH", error);
    if (market->get<std::string>() != "SH") return reject("SSE strategy library rejects non-SH market", error);
    nlohmann::json::const_iterator model = config.find("model_path");
    if (model == config.end() || !model->is_string() || model->get<std::string>().empty())
        return reject("SSE strategy library requires non-empty string model_path", error);
    nlohmann::json::const_iterator factors = config.find("sse_factor_contract");
    if (factors == config.end() || !factors->is_string() ||
        factors->get<std::string>() != "v0.4-sse-cob-batch-end-100us")
        return reject("SSE strategy library requires SSE factor contract v0.4-sse-cob-batch-end-100us", error);
    nlohmann::json::const_iterator backend = config.find("sse_inference_backend");
    if (backend == config.end() || !backend->is_string() ||
        backend->get<std::string>() != "native-cpp")
        return reject("SSE strategy library requires sse_inference_backend=native-cpp", error);
    return true;
}
}
