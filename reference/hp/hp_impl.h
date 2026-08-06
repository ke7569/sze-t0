#pragma once
#include "MdClient/TickMarketData.h"
#include "SpdlogWrapper.h"
#include "Utils/Tsc.h"
#include "base/ivo_base.h"
#include "hpx.h"
#include "internal/strategy_options.h"
#include "ivo/md_common.h"
#include "magic_enum.hpp"
#include "mercury.h"
#include "hp_common.h"
#include "hp_prediction.h"
#include "hp_instrument.h"
#include "strategy_config.h"
#include "string_utils.h"
#include "trading_utils.h"
#include "hpx.pb.h"
#include <string>
#include <string_view>
#include <limits>
#include <memory>
#include <sys/cdefs.h>

template <typename Ins>
class HpOptions;
class Just21F : public HpInstrument {
public:
  static constexpr bool kNeedLoop = false;
  static constexpr bool kNeedBar = false;

  Just21F(const std::string &ins, ManagedParaDetailProtoPtr mins,
          InstrumentParaDetailProtoPtr cins,
          ManagedParaDetailProtoPtr group_mins, GlobalParams *global_params)
      : HpInstrument(ins, mins, cins, group_mins, global_params) {
    auto config = ivo::mercury::StrategyConfig::instance();
    auto downsample = config->GetConfigWithDefault<std::uint32_t>(
        "downsample", kDefaultDownSample);
    auto model = Model21F;
    auto use_short_history =
        config->GetConfigWithDefault<bool>("use_short_history", false);
#ifdef DS21
    model = DSChange21;
    downsample = kInfinityDownSample;
    use_short_history = true;
#endif
    pred_ = std::make_unique<Prediction21F>(
        GetInstrumentID(), params_, model, downsample, use_short_history);
  }

  static bool FilterMarketData(const MD &md) {
#ifdef DS21
    return AnyMDType(md, ivolib::TICK_TYPE::SNAPSHOT_MATCH,
                     ivolib::TICK_TYPE::SNAPSHOT_MATCH_FAIL,
                     ivolib::TICK_TYPE::SZ_OPEN_AUCTION_SNAPSHOT_MATCH);
#else
    static ivo::mercury::OnlyNeedSnapshot filter;
    return filter(md);
#endif
  }

  void HandleMD(const MD* md) {
    static ivo::mercury::OnlyNeedContinousAuction only_continous;
    if (__glibc_unlikely(not only_continous(*md))) {
      return;
    }

    if (not CanPrediction() || not pred_->HandleMD(md)) {
      return;
    }
    AfterPrediction(pred_->context_);
    PushAndClearContext(pred_->context_);
  }

 private:
  std::unique_ptr<Prediction21F> pred_;
};

template <typename Pred>
class LeadingModel : public HpInstrument {
public:
  static constexpr bool kNeedLoop = true;
  static constexpr bool kNeedBar = false;

  LeadingModel(const std::string &ins, ManagedParaDetailProtoPtr mins,
               InstrumentParaDetailProtoPtr cins,
               ManagedParaDetailProtoPtr group_mins,
               GlobalParams *global_params)
      : HpInstrument(ins, mins, cins, group_mins, global_params),
        pred_(GetInstrumentID(), params_),
        pred_21f_(GetInstrumentID(), params_, Model21F, kDefaultDownSample,
                  false) {}

  static bool FilterMarketData(const MD &md) {
    return md < ivolib::kTime1457 and Pred::FilterMarketData(md);
  }

  void HandleMD(const MD *md) {
    if (not CanPrediction()) {
      return;
    }

#ifdef TEST_LOCAL
    static uint64_t cur_tsc = 0;
    if (md->TickType == ivolib::TICKTYPE_ORDER or
        md->TickType == ivolib::ORDER_MATCH) {
      cur_tsc = md->TickOrder.TimeStamp;
    }
    if (md->TickType == ivolib::TICKTYPE_TRADE) {
      cur_tsc = md->TickTrade.TimeStamp;
    }

    if (md->TickType == ivolib::TICKTYPE_TRADE or
        md->TickType == ivolib::TICKTYPE_ORDER or
        md->TickType == ivolib::ORDER_MATCH) {
      RunLoop(cur_tsc);
    }
#endif

    if (!use_21f_ && (md->TickType == ivolib::SNAPSHOT_MATCH_FAIL)) {
      use_21f_ = true;
      OnMDMatchError();
    }

    if (use_21f_) {
      if (pred_21f_.HandleMD(md)) {
        AfterPrediction(pred_21f_.context_);
        PushAndClearContext(pred_21f_.context_);
      }
      return;
    }

    pred_.HandleMD(md);
  }

  void RunLoop(std::uint64_t cur_tsc) {
    if (use_21f_ || not CanPrediction() || not pred_.MayPrediction(cur_tsc)) {
      return;
    }

    auto &context = pred_.context_;
    AfterPrediction(context);
    PushAndClearContext(context);
  }

private:
  bool use_21f_ = false;
  Pred pred_;
  Prediction21F pred_21f_ ;
};

template <typename Pred>
class NotLeadingTrade : public HpInstrument {
 public:
  static constexpr bool kNeedLoop = false;
  static constexpr bool kNeedBar = false;

  NotLeadingTrade(const std::string& ins, ManagedParaDetailProtoPtr mins,
                  InstrumentParaDetailProtoPtr cins,
                  ManagedParaDetailProtoPtr group_mins,
                  GlobalParams* global_params)
      : HpInstrument(ins, mins, cins, group_mins, global_params) {
    pred_21f_ =
        std::make_unique<Prediction21F>(GetInstrumentID(), params_);
    pred_ = std::make_unique<Pred>(GetInstrumentID(), params_);
  }

  static bool FilterMarketData(const MD& md) {
    return md < ivolib::kTime1457 &&
           md.TickType != ivolib::SH_UNORDERED_ORDER &&
           md.TickType != ivolib::SH_UNORDERED_TRADE;
  }

  void HandleMD(const MD* md) {
    if (not CanPrediction()) {
      return;
    }

    if (!use_21f_ && (md->TickType == ivolib::SNAPSHOT_MATCH_FAIL)) {
      use_21f_ = true;
      OnMDMatchError();
    }

    if (use_21f_) {
      if (pred_21f_->HandleMD(md)) {
        AfterPrediction(pred_21f_->context_);
        PushAndClearContext(pred_21f_->context_);
      }
      return;
    }

    if (not pred_->HandleMD(md)) {
      return;
    }
    AfterPrediction(pred_->context_);
    PushAndClearContext(pred_->context_);
  }

 private:
  bool use_21f_ = false;
  std::unique_ptr<Prediction21F> pred_21f_;
  std::unique_ptr<Pred> pred_;
};

template <typename Ins>
class HpWorker : public ivo::mercury::KeyWorkerInstance<Ins> {
 public:
  HpWorker(HpOptions<Ins>* opts) : opts_(opts) {}
  int Init(const std::unordered_map<std::string, ivo::mercury::InstrumentInfo>&
               instrument_map,
           const ivo::mercury::GlobalInstrumentInfo& info) override;

  void OnMarketDataUpdate(const MD &md, Ins *ins) {
    // 集合竞价处理
    if (__glibc_unlikely(state_ != State::Continous)) {
      auto ms = ivolib::MillSecondsFromString(md.UpdateTime, md.MillSec);
      if (call_order_) {
        if (state_ == State::BeforeCallOrder) {
          if (ms < GlobalInfo::kCallOrderMillSeconds) {
            ins->HandleCallMD(md);
          } else {
            for (auto &item : this->GetInstrumentMap()) {
              item.second->HandleCallOrder();
            }
          }
        } else {
          if (md >= ivolib::kTime0930) {
            for (auto &item : this->GetInstrumentMap()) {
              item.second->CancelAll();
            }
          }
        }
      }
      if (ms >= GlobalInfo::kCallOrderMillSeconds && md < ivolib::kTime0930) {
        state_ = State::AfterCallOrder;
      } else if (md >= ivolib::kTime0930) {
        state_ = State::Continous;
      }
    }

    auto etf = ins->GetEtf();
    if (etf != nullptr) {
      etf->OnMarketDataUpdate(md);
    } else {
      // 预测逻辑
      ins->HandleMD(&md);
    }
  }

  void OnGlobalManagedParamUpdate(const proto::ManagedParaDetailProto& detail,
                                  bool delta);

  void RunAfterHandleAllPendingMarketData() {
    if constexpr (Ins::kNeedBar) {
      if (bar_queue_ != nullptr) {
        while (true) {
          auto ref = bar_queue_->Read();
          if (ref == nullptr) {
            break;
          }
          auto ins = this->FindInstrument(std::string(ref->code, 9));
          if (ins == nullptr) {
            continue;
          }
          ins->HandleBar(ref, bar_queue_->PrevReadIdx());
        }
      }
    }

#ifndef TEST_LOCAL
    if constexpr (Ins::kNeedLoop) {
      if (state_ == State::Continous) {
        auto tsc = ivolib::getTscTick();
        for (auto &item : all_ins_) {
          item->RunLoop(tsc);
        }
      }
    }
#endif
  }

  void OutputTheo() {
    for (auto &item : all_ins_) {
      item->OutputTheo();
    }
  }

  void OnChannelMsgUpdate(const proto::ChannelMsgProto &msg) {
    if (not msg.has_content()) {
      return;
    }
    const auto &key = msg.key();

    if (key == HpXVer::HpxPriceProto::descriptor()->full_name()) {
      HpXVer::HpxPriceProto pb;
      pb.ParseFromArray(msg.content().data(), msg.content().size());
      for (const auto &item : pb.items()) {
        auto ins = this->FindInstrument(item.instrument());
        if (ins == nullptr) {
          continue;
        }
        ins->GetHpx().OnPriceInfo(item);
      }
    } else if (key == HpXVer::HpxTheoProgressProto::descriptor()->full_name()) {
      HpXVer::HpxTheoProgressProto pb;
      pb.ParseFromArray(msg.content().data(), msg.content().size());
      for (const auto &item : pb.items()) {
        auto ins = this->FindInstrument(item.instrument());
        if (ins == nullptr) {
          continue;
        }
        ins->GetHpx().OnTheoProgress(item);
      }
    }
  }

  void OpenHpxChannelUntilSucceed();
  void OnChannelDestroy(const proto::ChannelDestroyNotifyProto &msg);

private:
  std::vector<Ins*> all_ins_;
  bool call_order_ = false;
  GlobalParams global_params_;
  BarQueue* bar_queue_ = nullptr;
  State state_ = State::BeforeCallOrder;
  HpOptions<Ins>* opts_;
};

template <typename Ins>
class HpOptions : public ivo::mercury::StrategyOptions<HpWorker<Ins>> {
 public:
  void RunBeforeConnectTank() override;
  void RunBeforeMakeKeyWorker() override;
  void RunBeforeStartKeyWorker() override;
  std::unique_ptr<HpWorker<Ins>> MakeOneKeyWorker() override {
    return std::make_unique<HpWorker<Ins>>(this);
  }
  int OpenHpxChannel();
  void CloseHpxChannel();

private:
  std::string hpx_channel_id_;
};

#include "hp_impl_inl.h"
