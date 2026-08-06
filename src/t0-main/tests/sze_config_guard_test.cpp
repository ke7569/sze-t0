#include "../deepwin_strategy/sze_config_guard.h"

#include <iostream>
#include <string>

namespace {

bool expect_rejected(const nlohmann::json& config, const std::string& expected_text) {
    std::string error;
    if (sze_strategy_library::validate_config(config, &error)) {
        std::cerr << "expected rejected config" << std::endl;
        return false;
    }
    if (error.find(expected_text) == std::string::npos) {
        std::cerr << "unexpected rejection: " << error << std::endl;
        return false;
    }
    return true;
}

}  // namespace

int main() {
    nlohmann::json sze;
    sze["market"] = "SZ";
    std::string error = "stale";
    if (!sze_strategy_library::validate_config(sze, &error) || !error.empty()) {
        std::cerr << "SZ config was rejected: " << error << std::endl;
        return 1;
    }

    nlohmann::json sse;
    sse["market"] = "SH";
    if (!expect_rejected(sse, "Shanghai requires a separate strategy library")) {
        return 2;
    }

    nlohmann::json missing;
    if (!expect_rejected(missing, "requires config field market=SZ")) {
        return 3;
    }

    nlohmann::json malformed;
    malformed["market"] = 89;
    if (!expect_rejected(malformed, "requires string config field market=SZ")) {
        return 4;
    }

    nlohmann::json other;
    other["market"] = "BJ";
    if (!expect_rejected(other, "rejects non-SZ market")) {
        return 5;
    }

    nlohmann::json hp;
    hp["market"] = "SZ";
    hp["sz_orderbook_mode"] = "hp-shadow";
    hp["model_path"] = "/tmp/mix153060.bin";
    error = "stale";
    if (!sze_strategy_library::validate_config(hp, &error) || !error.empty()) {
        std::cerr << "HP config was rejected: " << error << std::endl;
        return 6;
    }

    nlohmann::json hp_missing = hp;
    hp_missing.erase("model_path");
    if (!expect_rejected(hp_missing, "requires non-empty string model_path")) {
        return 7;
    }

    nlohmann::json hp_alias = hp;
    hp_alias["mix153060_model_artifact"] = "/tmp/other.bin";
    if (!expect_rejected(hp_alias, "accepts only model_path")) {
        return 8;
    }

    nlohmann::json realtime = hp;
    realtime["sz_orderbook_mode"] = "hp-realtime";
    if (!expect_rejected(realtime, "requires explicit sze_order_routing object")) {
        return 9;
    }
    realtime["md_source_index"] = {88};
    realtime["td_source_index"] = {180};
    realtime["sze_order_routing"] = {{"enabled", true}, {"mode", "virtual"}};
    error = "stale";
    if (!sze_strategy_library::validate_config(realtime, &error) || !error.empty()) {
        std::cerr << "virtual live config was rejected: " << error << std::endl;
        return 10;
    }
    nlohmann::json realtime_capture = realtime;
    realtime_capture["mix153060_capture"] = {{"capture_only", true}};
    if (!expect_rejected(realtime_capture, "rejects capture_only")) {
        return 11;
    }
    nlohmann::json realtime_multi_td = realtime;
    realtime_multi_td["td_source_index"] = {180, 181};
    if (!expect_rejected(realtime_multi_td, "one td_source_index")) {
        return 12;
    }
    nlohmann::json realtime_recovery = realtime;
    realtime_recovery["sze_recovery_consumer"] = {{"enabled", true}};
    if (!expect_rejected(realtime_recovery, "rejects recovery consumer")) {
        return 13;
    }
    nlohmann::json shadow_routing = hp;
    shadow_routing["sze_order_routing"] = {{"enabled", true}, {"mode", "virtual"}};
    if (!expect_rejected(shadow_routing, "requires hp-realtime mode")) {
        return 14;
    }

    nlohmann::json recovery = hp;
    recovery["md_source_index"] = nlohmann::json::array();
    recovery["sze_recovery_consumer"] = {
        {"enabled", true},
        {"trading_day", 20260722},
        {"journal_directory", "/home/zane/data/sze_journal"},
        {"journal_prefix", "000001"},
        {"shm_path", "/dev/shm/sze_000001_20260722.events"},
        {"state_cpu", 7},
        {"strategy_cpu", 8}
    };
    error = "stale";
    if (!sze_strategy_library::validate_config(recovery, &error) || !error.empty()) {
        std::cerr << "recovery config was rejected: " << error << std::endl;
        return 15;
    }

    nlohmann::json duplicate_delivery = recovery;
    duplicate_delivery["md_source_index"] = {88};
    if (!expect_rejected(duplicate_delivery, "empty md_source_index")) {
        return 16;
    }

    nlohmann::json missing_shm = recovery;
    missing_shm["sze_recovery_consumer"].erase("shm_path");
    if (!expect_rejected(missing_shm, "requires shm_path for online handoff")) {
        return 17;
    }

    nlohmann::json same_cpu = recovery;
    same_cpu["sze_recovery_consumer"]["strategy_cpu"] = 7;
    if (!expect_rejected(same_cpu, "must be distinct")) {
        return 18;
    }

    nlohmann::json analysis = recovery;
    analysis["td_source_index"] = nlohmann::json::array();
    analysis["vtd"] = nlohmann::json::array();
    analysis["mix153060_capture"] = {
        {"enabled", true},
        {"capture_only", true},
        {"samples", true}
    };
    analysis["sze_recovery_consumer"]["allow_invalid_replay_for_analysis"] = true;
    error = "stale";
    if (!sze_strategy_library::validate_config(analysis, &error) || !error.empty()) {
        std::cerr << "analysis config was rejected: " << error << std::endl;
        return 19;
    }
    analysis["sze_recovery_consumer"].erase("shm_path");
    error = "stale";
    if (!sze_strategy_library::validate_config(analysis, &error) || !error.empty()) {
        std::cerr << "journal-only analysis config was rejected: " << error << std::endl;
        return 20;
    }

    nlohmann::json analysis_live = analysis;
    analysis_live["mix153060_capture"]["capture_only"] = false;
    if (!expect_rejected(analysis_live, "requires enabled capture_only")) {
        return 21;
    }

    nlohmann::json analysis_td = analysis;
    analysis_td["td_source_index"] = {180};
    if (!expect_rejected(analysis_td, "requires empty td_source_index")) {
        return 22;
    }

    nlohmann::json analysis_vtd = analysis;
    analysis_vtd["vtd"] = {{"source", 180}};
    if (!expect_rejected(analysis_vtd, "requires explicit empty vtd")) {
        return 23;
    }

    std::cout << "sze_config_guard_test: PASS" << std::endl;
    return 0;
}
