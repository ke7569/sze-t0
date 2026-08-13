#include "../strategy/sse_config_guard.h"
#include <iostream>
int main() {
    nlohmann::json config;
    config["market"] = "SH";
    config["model_path"] = "/opt/sse-t0/models/v04-legacy-midmix-sse/SSEMODL1.bin";
    config["sse_factor_contract"] = "v0.4-sse-cob-batch-end-100us";
    config["sse_inference_backend"] = "native-cpp";
    std::string error = "stale";
    if (!sse_strategy_library::validate_config(config, &error) || !error.empty()) {
        std::cerr << "valid SSE config rejected: " << error << std::endl;
        return 1;
    }
    config["market"] = "SZ";
    if (sse_strategy_library::validate_config(config, &error) ||
        error.find("non-SH") == std::string::npos) {
        std::cerr << "non-SH config accepted: " << error << std::endl;
        return 2;
    }
    config["market"] = "SH";
    config.erase("model_path");
    if (sse_strategy_library::validate_config(config, &error) ||
        error.find("model_path") == std::string::npos) {
        std::cerr << "missing model config accepted: " << error << std::endl;
        return 3;
    }
    config["model_path"] = "/opt/sse-t0/models/v04-legacy-midmix-sse/SSEMODL1.bin";
    config["sse_factor_contract"] = "mix153060";
    if (sse_strategy_library::validate_config(config, &error) ||
        error.find("factor contract") == std::string::npos) {
        std::cerr << "SZE factor contract accepted by SSE config" << std::endl;
        return 4;
    }
    config["sse_factor_contract"] = "v0.4-sse-cob-batch-end-100us";
    config["sse_inference_backend"] = "torch";
    if (sse_strategy_library::validate_config(config, &error) ||
        error.find("native-cpp") == std::string::npos) {
        std::cerr << "Torch backend accepted by SSE config" << std::endl;
        return 5;
    }
    std::cout << "sse_config_guard_test: PASS" << std::endl;
    return 0;
}
