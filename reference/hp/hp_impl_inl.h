#pragma once
#include <cstddef>
#include <cstring>
#include <iterator>
#include <openssl/ossl_typ.h>
#include <string>
#include <sys/cdefs.h>

#include "Enum.pb.h"
#include "Utils/algorithm.h"
#include "base/ivo_base.h"
#include "ivo/md_common.h"
#include "boost/algorithm/string.hpp"
#include "hp_common.h"
#include "nlohmann/detail/meta/cpp_future.hpp"
#include "nlohmann/json_fwd.hpp"
#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"
#include "strategy.h"
#include "strategy_config.h"
#include "strategy_utils.h"
#include "trading_utils.h"

template <typename Ins>
int HpWorker<Ins>::Init(
    const std::unordered_map<std::string, ivo::mercury::InstrumentInfo>&
        instrument_map,
    const ivo::mercury::GlobalInstrumentInfo& info) {

  IVO_ASSERT(info.global_managed_instrument_ptr != nullptr, "MissedGlobal");
  OnGlobalManagedParamUpdate(*(info.global_managed_instrument_ptr.get()),
                             false);
  nlohmann::ordered_json js = global_params_;
  auto global_info = GlobalInfo::instance();
  IVOLOG_INFO("[Global] {}", js.dump());

  auto config = ivo::mercury::StrategyConfig::instance();
  auto bar_shm = config->GetConfigWithDefault("bar_shm", std::string(""));
  if (not bar_shm.empty()) {
    bar_queue_ = BarQueue::Init(bar_shm);
    IVO_ASSERT(bar_queue_ != nullptr, "InvalidBarShm {}", bar_shm);
  }

  for (const auto& item : instrument_map) {
    const auto& item_info = item.second;

    if (item_info.managed_instrument_ptr == nullptr || item_info.common_instrument_ptr == nullptr) {
      IVOLOG_DEBUG("InvalidContract ins={},mins={},cins={}", item.first, item_info.managed_instrument_ptr == nullptr,
                   item_info.common_instrument_ptr == nullptr);
      continue;
    }

    if (not ivolib::AnyType(item_info.common_instrument_ptr->contractkind(),
                        proto::ContractKindProto::STOCK,
                        proto::ContractKindProto::ETF)) {
      IVOLOG_DEBUG("InvalidContract cins={}", item_info.common_instrument_ptr->ShortDebugString());
      continue;
    }

    if (item_info.managed_instrument_ptr->pvalues_size() == 0) {
      IVOLOG_WARN("OldContract mins={}",
                  item_info.managed_instrument_ptr->ShortDebugString());
      continue;
    }

    ManagedParaDetailProtoPtr pos_group_param;
    for (const auto& item : item_info.extra_managed_instruments) {
      auto prefix = ivo::mercury::ParsePrefix(item);
      if (prefix == global_info->pos_group) {
        pos_group_param = item;
      }
    }

    auto ins = std::make_unique<Ins>(
        item.first, item_info.managed_instrument_ptr,
        item_info.common_instrument_ptr, pos_group_param, &global_params_);
    IVOLOG_INFO("[AddInstrument] {}", ins->ToString());
    all_ins_.emplace_back(ins.get());
    this->AddInstrument(std::move(ins));
  }

  auto output_theo = config->GetConfigWithDefault<int>("output_theo", 0);
  if (output_theo) {
    this->RunEvery(std::bind(&HpWorker<Ins>::OutputTheo, this), output_theo);
  }

  this->RunEvery(std::bind(&HpManager::UpdateMillSeconds, HpManager::instance()), 0.001);

  call_order_ =
      ivo::mercury::StrategyConfig::instance()->GetConfigWithDefault<bool>(
          "call_order", false);

  auto model_string = config->GetRequiredConfig<std::string>("model");
  auto model = magic_enum::enum_cast<ModelType>(model_string);
  PredictionBase::LoadScaler(config->GetConfigWithDefault<std::string>("scaler", "./input/scaler.json"), model.value());
  return 0;
}

template <typename Ins>
void HpWorker<Ins>::OnGlobalManagedParamUpdate(
    const proto::ManagedParaDetailProto& detail, bool delta) {
  global_params_.Update(detail);
}

template <typename Ins>
void HpOptions<Ins>::RunBeforeConnectTank() {
  auto cfg = this->config;
  const auto& app_name = cfg->app_name;
  auto global_info = GlobalInfo::instance();
  global_info->seq_code = app_name[0] - 'A';
  global_info->seq_char = app_name[0];
  global_info->batch_code = app_name[1] - '0';

  cfg->managed_instrument_prefix = fmt::format("HrmsPr{}", global_info->batch_code);

  if (ivolib::contains(app_name, "HpH")) {
    cfg->exchange = "SH";
  } else if (ivolib::contains(app_name, "HpS")) {
    cfg->exchange = "SZ";
  } else if (ivolib::contains(app_name, "HpK")) {
    cfg->exchange = "HK";
  } else {
    IVOLOG_FATAL("Invalid AppName {}", app_name);
  }

  global_info->exchange_sh = (cfg->exchange == "SH");
  if (global_info->exchange_sh) {
    GlobalInfo::kCallOrderMillSeconds =
        ivolib::MillSecondsFromString("09:24:59", 0);
  } else {
    GlobalInfo::kCallOrderMillSeconds =
        ivolib::MillSecondsFromString("09:24:59", 900);
  }

  const auto& st_name = cfg->st_name;
  std::vector<std::string> view = ivolib::split(st_name, '_');
  IVO_ASSERT(view.size() == 2, "Invalid StName {}", st_name);
  global_info->t0_name = st_name;
  global_info->fast_name = fmt::format("{}f_{}", view[0], view[1]);
  global_info->sub_name = fmt::format("{}s_{}", view[0], view[1]);

  this->md_filter = std::bind(&Ins::FilterMarketData, std::placeholders::_1);
}

template <typename Ins>
void HpOptions<Ins>::RunBeforeMakeKeyWorker() {
  if (this->config->template GetConfigWithDefault<bool>(
      "openhpxchannel", true)) {
    int open_hpx = OpenHpxChannel();
    while (open_hpx != 0) {
      IVOLOG_ERROR("OpenHpxChannel failed:{}, retry...", open_hpx);
      std::this_thread::sleep_for(std::chrono::seconds(10));
      open_hpx = OpenHpxChannel();
    }
    IVOLOG_INFO("OpenHpxChannel succeed");
  }

  auto meta_url =
      this->config->template GetConfigWithDefault<std::string>("metaurl", "");
  auto global_info = GlobalInfo::instance();
  if (not meta_url.empty()) {
    auto meta = this->GetConfigFromTankSlow(meta_url);
    IVO_ASSERT(meta.has_value(), "metaurl:{}", meta_url);

    auto meta_json = nlohmann::json::parse(*meta, nullptr, true, true);
    global_info->pos_group =
        meta_json["ex_pos_stat"]["posgrp_pre"].template get<std::string>();
    this->extra_managed_prefix.push_back(global_info->pos_group);

    auto fast_pf = ivo::mercury::GetJsonKeyWithDefault<nlohmann::json>(
        meta_json, "ex_f_pf", nlohmann::json());
    for (auto i = 0; i < fast_pf.size(); ++i) {
      auto &dummy = fast_pf[i];
      IVOLOG_INFO("[FastPosGroup] pos={}", dummy);
      auto pos = this->GetPositionFromTankSlow(dummy, "*");
      IVO_ASSERT(pos.has_value());
      auto &pos_map = global_info->fast_position_map_;
      for (const auto &item : *pos) {
        auto vol = item.longposition() - item.shortposition();
        auto iter = pos_map.find(item.instrumentid());
        if (iter == pos_map.end()) {
          iter = pos_map.emplace(item.instrumentid(), 0).first;
        }
        iter->second += vol;
      }
    }

    auto sub_pf = ivo::mercury::GetJsonKeyWithDefault<nlohmann::json>(
        meta_json, "ex_s_pf", nlohmann::json());
    for (auto i = 0; i < sub_pf.size(); ++i) {
      auto &dummy = sub_pf[i];
      IVOLOG_INFO("[SubPosGroup] pos={}", dummy);
      auto pos = this->GetPositionFromTankSlow(dummy, "*");
      IVO_ASSERT(pos.has_value());
      auto &pos_map = global_info->sub_position_map_;
      for (const auto &item : *pos) {
        auto vol = item.longposition() - item.shortposition();
        auto iter = pos_map.find(item.instrumentid());
        if (iter == pos_map.end()) {
          iter = pos_map.emplace(item.instrumentid(), 0).first;
        }
        iter->second += vol;
      }
    }

    auto st_pf = ivo::mercury::GetJsonKeyWithDefault<nlohmann::json>(
        meta_json, "ex_st_pf", nlohmann::json());
    for (auto i = 0; i < st_pf.size(); ++i) {
      auto &dummy = st_pf[i];
      IVOLOG_INFO("[StPosGroup] pos={}", dummy);
      auto pos = this->GetPositionFromTankSlow(dummy, "*");
      IVO_ASSERT(pos.has_value());
      auto &pos_map = global_info->st_position_map_;
      for (const auto &item : *pos) {
        auto vol = item.longposition() - item.shortposition();
        auto iter = pos_map.find(item.instrumentid());
        if (iter == pos_map.end()) {
          iter = pos_map.emplace(item.instrumentid(), 0).first;
        }
        iter->second += vol;
      }
    }

  } else {
    global_info->pos_group = fmt::format("PosGrp{}", global_info->batch_code);
    this->extra_managed_prefix.push_back(global_info->pos_group);
  }

  global_info->FillKey();
  nlohmann::ordered_json js = *global_info;
  IVOLOG_INFO("[GlobalInfo] {}", js.dump());

}

template <typename Ins>
void HpOptions<Ins>::RunBeforeStartKeyWorker() {
  GlobalInfo::instance()->fast_position_map_.clear();
  GlobalInfo::instance()->sub_position_map_.clear();
}

template <typename Ins>
void HpWorker<Ins>::OnChannelDestroy(const proto::ChannelDestroyNotifyProto &msg) {
  IVOLOG_INFO("OnChannelDestroy: {}", msg.ShortDebugString());
  OpenHpxChannelUntilSucceed();
}

template <typename Ins>
void HpWorker<Ins>::OpenHpxChannelUntilSucceed() {
  auto ret = opts_->OpenHpxChannel();
  if (ret == 0) {
    IVOLOG_INFO("OpenHpxChannel succeed");
    return;
  }

  IVOLOG_ERROR("OpenHpxChannel failed: {}", ret);
  this->RunOnceAfter(std::bind(&HpWorker<Ins>::OpenHpxChannelUntilSucceed, this),
                10);
}

template <typename Ins>
int HpOptions<Ins>::OpenHpxChannel() {
  auto ret = this->OpenChannel("HpXVerPara_prod_v2", hpx_channel_id_);
  if(ret != proto::CreateChannelStatus::CreateChannel_OK) {
    IVOLOG_ERROR("[OpenChannelFailed]");
    return -1;
  }

  HpXVer::HpxSubProto msg;
  msg.set_managed_suffix(GlobalInfo::instance()->batch_code);
  std::string key;
  key.push_back(GlobalInfo::instance()->seq_char);
  msg.set_key_suffix(key);
  auto ret1 = this->SendChannelMsg(hpx_channel_id_, msg, false);
  if (ret1 != proto::ChannelStatus::Channel_OK) {
    IVOLOG_ERROR("[SendChannelFailed]");
    CloseHpxChannel();
    return -2;
  }

  return 0;
}

template <typename Ins>
void HpOptions<Ins>::CloseHpxChannel() {
  if (not hpx_channel_id_.empty()) {
    this->CloseChannel(hpx_channel_id_);
  }
}