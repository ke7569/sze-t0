#include "sze_config_guard.h"

namespace sze_strategy_library {

namespace {

bool reject(const char* message, std::string* error) {
    if (error != 0) {
        *error = message;
    }
    return false;
}

bool is_hp_mode(const nlohmann::json& config) {
    nlohmann::json::const_iterator mode = config.find("sz_orderbook_mode");
    if (mode == config.end() || !mode->is_string()) {
        mode = config.find("orderbook_mode");
    }
    if (mode == config.end() || !mode->is_string()) {
        return false;
    }
    const std::string value = mode->get<std::string>();
    return value == "hp-shadow" || value == "hp_shadow" ||
           value == "hp-realtime" || value == "hp_realtime";
}

bool is_hp_shadow_mode(const nlohmann::json& config) {
    nlohmann::json::const_iterator mode = config.find("sz_orderbook_mode");
    if (mode == config.end() || !mode->is_string()) {
        mode = config.find("orderbook_mode");
    }
    if (mode == config.end() || !mode->is_string()) {
        return false;
    }
    const std::string value = mode->get<std::string>();
    return value == "hp-shadow" || value == "hp_shadow";
}

bool is_hp_realtime_mode(const nlohmann::json& config) {
    nlohmann::json::const_iterator mode = config.find("sz_orderbook_mode");
    if (mode == config.end() || !mode->is_string()) {
        mode = config.find("orderbook_mode");
    }
    if (mode == config.end() || !mode->is_string()) {
        return false;
    }
    const std::string value = mode->get<std::string>();
    return value == "hp-realtime" || value == "hp_realtime";
}

bool validate_live_routing(const nlohmann::json& config, std::string* error) {
    nlohmann::json::const_iterator routing = config.find("sze_order_routing");
    if (routing == config.end() || !routing->is_object()) {
        return reject("hp-realtime requires explicit sze_order_routing object", error);
    }
    nlohmann::json::const_iterator enabled = routing->find("enabled");
    nlohmann::json::const_iterator mode = routing->find("mode");
    if (enabled == routing->end() || !enabled->is_boolean() || !enabled->get<bool>() ||
        mode == routing->end() || !mode->is_string() ||
        (mode->get<std::string>() != "live" && mode->get<std::string>() != "virtual")) {
        return reject("hp-realtime requires enabled sze_order_routing mode live or virtual", error);
    }
    nlohmann::json::const_iterator capture = config.find("mix153060_capture");
    if (capture != config.end() && capture->is_object()) {
        nlohmann::json::const_iterator capture_only = capture->find("capture_only");
        if (capture_only != capture->end() && capture_only->is_boolean() &&
            capture_only->get<bool>()) {
            return reject("sze_order_routing rejects capture_only", error);
        }
    }
    nlohmann::json::const_iterator recovery = config.find("sze_recovery_consumer");
    if (recovery != config.end() && recovery->is_object()) {
        nlohmann::json::const_iterator recovery_enabled = recovery->find("enabled");
        if (recovery_enabled != recovery->end() && recovery_enabled->is_boolean() &&
            recovery_enabled->get<bool>()) {
            return reject("sze_order_routing rejects recovery consumer", error);
        }
    }
    nlohmann::json::const_iterator md_sources = config.find("md_source_index");
    nlohmann::json::const_iterator td_sources = config.find("td_source_index");
    if (md_sources == config.end() || !md_sources->is_array() || md_sources->empty() ||
        td_sources == config.end() || !td_sources->is_array() || td_sources->size() != 1 ||
        !td_sources->at(0).is_number_integer()) {
        return reject("sze_order_routing requires non-empty md_source_index and one td_source_index", error);
    }
    return true;
}

}  // namespace

bool validate_config(const nlohmann::json& config, std::string* error) {
    if (error != 0) {
        error->clear();
    }
    nlohmann::json::const_iterator market = config.find("market");
    if (market == config.end()) {
        return reject("SZE strategy library requires config field market=SZ", error);
    }
    if (!market->is_string()) {
        return reject("SZE strategy library requires string config field market=SZ", error);
    }
    const std::string value = market->get<std::string>();
    if (value == "SH") {
        return reject(
            "SZE strategy library rejects market=SH; Shanghai requires a separate strategy library",
            error);
    }
    if (value != "SZ") {
        return reject(
            "SZE strategy library rejects non-SZ market; a separate strategy library is required",
            error);
    }
    if (!is_hp_mode(config)) {
        return true;
    }
    if (config.find("mix153060_model_artifact") != config.end() ||
        config.find("hp_model_artifact") != config.end()) {
        return reject(
            "HP-mode Shenzhen config accepts only model_path; remove model artifact aliases",
            error);
    }
    nlohmann::json::const_iterator model_path = config.find("model_path");
    if (model_path == config.end() || !model_path->is_string() ||
        model_path->get<std::string>().empty()) {
        return reject("HP-mode Shenzhen config requires non-empty string model_path", error);
    }
    nlohmann::json::const_iterator routing = config.find("sze_order_routing");
    if (routing != config.end() && routing->is_object()) {
        nlohmann::json::const_iterator enabled = routing->find("enabled");
        if (enabled != routing->end() && enabled->is_boolean() && enabled->get<bool>() &&
            !is_hp_realtime_mode(config)) {
            return reject("sze_order_routing requires hp-realtime mode", error);
        }
    }
    if (is_hp_realtime_mode(config) && !validate_live_routing(config, error)) {
        return false;
    }
    nlohmann::json::const_iterator recovery =
        config.find("sze_recovery_consumer");
    if (recovery == config.end()) {
        return true;
    }
    if (!recovery->is_object()) {
        return reject("sze_recovery_consumer must be an object", error);
    }
    nlohmann::json::const_iterator enabled = recovery->find("enabled");
    if (enabled == recovery->end() || !enabled->is_boolean() ||
        !enabled->get<bool>()) {
        if (recovery->find("allow_invalid_replay_for_analysis") != recovery->end() &&
            recovery->at("allow_invalid_replay_for_analysis").is_boolean() &&
            recovery->at("allow_invalid_replay_for_analysis").get<bool>()) {
            return reject(
                "allow_invalid_replay_for_analysis requires enabled recovery consumer",
                error);
        }
        return true;
    }
    nlohmann::json::const_iterator analysis =
        recovery->find("allow_invalid_replay_for_analysis");
    if (analysis != recovery->end() && !analysis->is_boolean()) {
        return reject(
            "allow_invalid_replay_for_analysis must be boolean", error);
    }
    if (analysis != recovery->end() && analysis->is_boolean() &&
        analysis->get<bool>()) {
        if (!is_hp_shadow_mode(config)) {
            return reject(
                "allow_invalid_replay_for_analysis requires hp-shadow mode", error);
        }
        nlohmann::json::const_iterator capture = config.find("mix153060_capture");
        if (capture == config.end() || !capture->is_object() ||
            capture->find("enabled") == capture->end() ||
            !capture->at("enabled").is_boolean() ||
            !capture->at("enabled").get<bool>() ||
            capture->find("capture_only") == capture->end() ||
            !capture->at("capture_only").is_boolean() ||
            !capture->at("capture_only").get<bool>() ||
            capture->find("samples") == capture->end() ||
            !capture->at("samples").is_boolean() ||
            !capture->at("samples").get<bool>()) {
            return reject(
                "allow_invalid_replay_for_analysis requires enabled capture_only samples",
                error);
        }
        nlohmann::json::const_iterator td_sources = config.find("td_source_index");
        if (td_sources == config.end() || !td_sources->is_array() ||
            !td_sources->empty()) {
            return reject(
                "allow_invalid_replay_for_analysis requires empty td_source_index",
                error);
        }
        nlohmann::json::const_iterator vtd_sources = config.find("vtd");
        if (vtd_sources == config.end() || !vtd_sources->is_array() ||
            !vtd_sources->empty()) {
            return reject(
                "allow_invalid_replay_for_analysis requires explicit empty vtd",
                error);
        }
    }
    nlohmann::json::const_iterator md_sources = config.find("md_source_index");
    if (md_sources != config.end() &&
        (!md_sources->is_array() || !md_sources->empty())) {
        return reject(
            "sze_recovery_consumer requires empty md_source_index to prevent duplicate delivery",
            error);
    }
    const bool analysis_mode = analysis != recovery->end() &&
        analysis->is_boolean() && analysis->get<bool>();
    const char* required_strings[] = {
        "journal_directory", "journal_prefix"
    };
    for (std::size_t index = 0;
         index < sizeof(required_strings) / sizeof(required_strings[0]); ++index) {
        nlohmann::json::const_iterator item =
            recovery->find(required_strings[index]);
        if (item == recovery->end() || !item->is_string() ||
            item->get<std::string>().empty()) {
            return reject(
                "sze_recovery_consumer requires journal_directory and journal_prefix",
                error);
        }
    }
    if (!analysis_mode) {
        nlohmann::json::const_iterator shm_path = recovery->find("shm_path");
        if (shm_path == recovery->end() || !shm_path->is_string() ||
            shm_path->get<std::string>().empty()) {
            return reject(
                "sze_recovery_consumer requires shm_path for online handoff", error);
        }
    }
    nlohmann::json::const_iterator trading_day = recovery->find("trading_day");
    nlohmann::json::const_iterator state_cpu = recovery->find("state_cpu");
    nlohmann::json::const_iterator strategy_cpu = recovery->find("strategy_cpu");
    if (trading_day == recovery->end() || !trading_day->is_number() ||
        state_cpu == recovery->end() || !state_cpu->is_number() ||
        strategy_cpu == recovery->end() || !strategy_cpu->is_number()) {
        return reject(
            "sze_recovery_consumer requires numeric trading_day, state_cpu, and strategy_cpu",
            error);
    }
    if (state_cpu->get<int>() == strategy_cpu->get<int>()) {
        return reject(
            "sze_recovery_consumer state_cpu and strategy_cpu must be distinct",
            error);
    }
    return true;
}

}  // namespace sze_strategy_library
