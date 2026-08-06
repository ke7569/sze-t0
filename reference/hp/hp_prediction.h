#pragma once

#include <any>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <limits>
#include <mutex>
#include <sys/cdefs.h>
#include "TickMarketData.h"
#include "Utils/Tsc.h"
#include "Utils/trading_utils.h"
#include "base/containers/circular_deque.h"
#include "internal/strategy_options.h"
#include "ivo/md_common.h"
#include "strategy_config.h"
#include "aot2c_auto_hp.h"
#include "hp_common.h"
#include "Utils/containers.h"
#include "hp_factor.h"
#include "Utils/Numeric.h"
#include "hp_depth_book.h"

static constexpr std::uint32_t kTickCacheSize = 512;
static constexpr std::uint32_t kOBCacheSize = 256;
using PredFunction = std::function<void(float*, float*, float*, float*)>;
static const auto kSec93000 = ivolib::SecondsFromString("09:30:00");
static constexpr double TURNOVER_DIFF_THRESHOLD = 0.1;
class FullBook {
public:
  FullBook(const std::string &instrument_id) {
    if (instrument_id.find(".SZ") != std::string::npos) {
      sz_match_book_ = std::make_unique<hp::MatchBook<hp::Exchange::SZ>>(instrument_id);
    } else if (instrument_id.find(".SH") != std::string::npos) {
      sh_match_book_ = std::make_unique<hp::MatchBook<hp::Exchange::SH>>(instrument_id);
    } else {
      IVOLOG_ERROR("Instrument not supported: {}", instrument_id);
    }
#ifdef TEST_LOCAL
    instrument_id_ = instrument_id;
#endif
  }

  inline void UpdateMD(const ivolib::TickMarketDataByType &md) {
    if (sz_match_book_) {
      sz_match_book_->UpdateMD(md);
    } else {
      sh_match_book_->UpdateMD(md);
    }
  }

  auto& GetAskDepth() const {
    if (sz_match_book_) {
      return sz_match_book_->ask();
    } else {
      return sh_match_book_->ask();
    }
  }

  auto& GetBidDepth() const {
    if (sz_match_book_) {
      return sz_match_book_->bid();
    } else {
      return sh_match_book_->bid();
    }
  }

  void GetFullOBFactor(const OrderFactor& order_factor, const TradeFactor& trade_factor,
      HpFactor& factor, int32_t cur_tsc, double fee_share);

  template<typename MEAN, typename STD>
  static void Normalized(HpFactor* factor, float* normed, const MEAN& mean, const STD& std);

private:
  std::unique_ptr<hp::MatchBook<hp::Exchange::SZ>> sz_match_book_;
  std::unique_ptr<hp::MatchBook<hp::Exchange::SH>> sh_match_book_;
#ifdef TEST_LOCAL
  std::string instrument_id_;
#endif
};

class PredictionBase {
public:
  PredictionBase() = default;
  PredictionBase(const std::string &code, HpParams &params,
                 ModelType real_model = Model21F,
                 std::uint32_t downsample = kDefaultDownSample,
                 bool use_short_history = false)
      : params_(&params), context_(&code, real_model), downsample_(downsample) {
    threshold_ = use_short_history ? params_->history_amount_short
                                   : params_->history_amount;
    // todo test only
#ifdef TEST_LOCAL
    if (code == "000001.SZ") {
      threshold_ = 1084540333.6;
    } else if (code == "600036.SH") {
      threshold_ = 3674142752.0000005;
    }
#endif
    if (_almostEqual(threshold_, 0)) {
      IVOLOG_INFO("[InvalidPredictionSample] code={}", code);
      threshold_ = std::numeric_limits<double>::max();
      downsample_ = 0;
    }
    IVOLOG_INFO("[ModelInit] "
                "code={},real_model={},threshold={},downssample={}",
                code, magic_enum::enum_name(real_model), threshold_,
                downsample_);
  }

protected:
  void Get21FFactor(const OB *cur_ob, HpFactor &factor);

  template <typename MD>
  inline bool FirstSamplePM(const MD *md) {
    if (not prev_ob_ref_) {
      return false;
    }

    static auto k1300InMs = ivolib::MillSecondsFromString("13:00:00", 0);
    auto ms = ivolib::MillSecondsFromString(md->UpdateTime, md->MillSec);
    auto prev_ob_ms = ivolib::MillSecondsFromString(prev_ob_ref_->UpdateTime,
                                                  prev_ob_ref_->MillSec);

    return ms - prev_ob_ms > 100000UL and ms >= k1300InMs;
  }

public:
  HpParams *params_ = nullptr;
  HpOrderContext context_;
  std::uint32_t downsample_;
  double threshold_ = 0;
  OB prev_ob_;
  OB *prev_ob_ref_ = nullptr;
   // 第一个采样点的prev_ob的volume, amt是0，不能用prev_ob_ref_
   // 里的volume和amt
  int64_t last_volume_ = 0;
  double last_amt_ = 0.0;
  std::array<float, 64> output_{0.0};

  static void LoadScaler(const std::string& scaler_file, ModelType model);
  static constexpr std::uint32_t FACTOR_SIZE = 50;
  static std::array<float, FACTOR_SIZE> mean_;
  static std::array<float, FACTOR_SIZE> std_;
};

class Prediction21F : public PredictionBase {
public:
  using PredictionBase::PredictionBase;
  bool HandleMD(const MD *md);
  static bool FilterMarketData(const MD &md) {
    return AnyMDType(md, ivolib::SNAPSHOT_MATCH, ivolib::SNAPSHOT_EXCHANGE,
                     ivolib::SZ_OPEN_AUCTION_SNAPSHOT_MATCH,
                     ivolib::SNAPSHOT_MATCH_FAIL);
  }

protected:
  bool HandleOB(const OB *ob);

protected:
  static inline constexpr std::uint32_t kSampleCountThreshold = 30;
  static inline ivo::mercury::OnlyNeedSnapshot g_only_snapshot;
  std::uint32_t sample_count_ = 0;
};

class PredictionLeading21F : public Prediction21F {
public:
  PredictionLeading21F(const std::string &code, HpParams &params)
      : Prediction21F(
            code, params, Leading21F,
            ivo::mercury::StrategyConfig::instance()
                ->GetConfigWithDefault<std::uint32_t>("downsample",
                                                      kDefaultDownSample),
            ivo::mercury::StrategyConfig::instance()
                ->GetConfigWithDefault<bool>("use_short_history", false)) {}

  PredictionLeading21F(const std::string &code, HpParams &params,
                       ModelType real_model, std::uint32_t downsample,
                       bool use_short_history)
      : Prediction21F(code, params, real_model, downsample, use_short_history) {
  }

  static bool FilterMarketData(const MD &md) {
    static ivo::mercury::OnlyNeedSnapshot only_snapshot;
    return only_snapshot(md);
  }

  bool HandleMD(const MD* md);

  bool MayPrediction(std::uint64_t cur_tsc) {
    if (__glibc_unlikely(prev_ob_ref_ == nullptr)) {
      return false;
    }

    if ((cur_ob_.Turnover - prev_ob_.Turnover) * downsample_ < threshold_ ||
        ivolib::USDurationFromTSC(cur_tsc_, cur_tsc) < 300) { // 0.3ms
      return false;
    }

    auto &factor = context_.factor;
    factor.factor_type = kFactorType21;
    Get21FFactor(&cur_ob_, factor);
    hp_model_F21Ver1_2_1SH_run(&(factor.hermes), &(output_[0]),
                               &context_.prediction_raw, &(output_[0]));
    context_.prediction = context_.prediction_raw;
    context_.prediction = std::clamp(context_.prediction, -15.0f, 15.0f);
    context_.last_ob = cur_ob_;
    prev_ob_ = cur_ob_;
    return true;
  }

protected:
  OB cur_ob_;
  std::uint64_t cur_tsc_;
};

class PredictionLeadingDSChange : public PredictionLeading21F {
public:
  PredictionLeadingDSChange(const std::string &code, HpParams &params,
                       ModelType real_model, std::uint32_t downsample,
                       bool use_short_history)
      : PredictionLeading21F(code, params, DSChangeV4, downsample,
                             use_short_history) {
    td_que_.reserve(kTickCacheSize);
    od_que_.reserve(kTickCacheSize);
    full_book_ = std::make_unique<FullBook>(code);
  }

  PredictionLeadingDSChange(const std::string &code, HpParams &params)
      : PredictionLeadingDSChange(code, params, DSChangeV4, kInfinityDownSample,
                             true) {}

  static bool FilterMarketData(const MD &md) {
    return md.TickType != ivolib::SH_UNORDERED_ORDER &&
           md.TickType != ivolib::SH_UNORDERED_TRADE;
  }

  bool HandleMD(const MD *md);
  bool MayPrediction(std::uint64_t cur_tsc) {
    if (not may_prediction_) {
      return false;
    }

    auto ms1 =
        ivolib::MillSecondsFromString(prev_ob_.UpdateTime, prev_ob_.MillSec);
    auto ms2 =
        ivolib::MillSecondsFromString(cur_ob_.UpdateTime, cur_ob_.MillSec);
    if (ms1 == ms2 or (MD_VOL(&cur_ob_) - last_volume_) < 100) {
      may_prediction_ = false;
      return false;
    }

    // SH延迟采样不能在OB和TD中间操作，否则TD的统计量会出错
    auto turnover_diff = MD_TURNOVER(&cur_ob_) - last_amt_;
    if (cum_amount_ < turnover_diff - TURNOVER_DIFF_THRESHOLD) {
      return false;
    }

#ifndef TEST_LOCAL
    // SH核对数据时临时去掉
    if (ivolib::USDurationFromTSC(cur_tsc_, cur_tsc) < 100) { // 0.1ms
      return false;
    }
#endif

    DoPrediction();
    return true;
  }

protected:
  void Get28FFactor(const OB *cur_ob, HpFactor &factor);
  void HandleCachedOD(const OD *od, OrderFactor &factor);
  void HandleCachedTD(const TD *td, TradeFactor &factor);
  void HandleOD(const MD *md) {
    od_que_.emplace_back(md->TickOrder);
#ifdef TEST_LOCAL
    cur_tsc_ = md->TickOrder.TimeStamp;
#else
    cur_tsc_ = ivolib::getTscTick();
#endif
    last_app_seq_ = md->TickOrder.BizIndex;
    full_book_->UpdateMD(*md);
  }

  void HandleTD(const MD *md) {
    if (__glibc_unlikely(not md_valid_)) {
      cum_amount_ = 0;
      full_book_->UpdateMD(*md);
      return;
    }
    cum_amount_ += (md->TickTrade.Price * md->TickTrade.Qty);
    td_que_.emplace_back(md->TickTrade);
#ifdef TEST_LOCAL
    cur_tsc_ = md->TickTrade.TimeStamp;
#else
    cur_tsc_ = ivolib::getTscTick();
#endif
    last_app_seq_ = md->TickTrade.Seq;

    bool do_sample = false;
    if (prev_ob_ref_) {
      auto turnover_diff = MD_TURNOVER(&cur_ob_) - last_amt_;
      // 无论是中间价变化还是成交金额*downsample>threshold_，都要求收完TD
      if ((cum_amount_ >= turnover_diff - TURNOVER_DIFF_THRESHOLD) and
          ((cum_amount_ * downsample_ > threshold_) or
          (std::abs(MD_FAST_MID_PRICE(&cur_ob_) -
                MD_FAST_MID_PRICE(prev_ob_ref_)) > 1e-6) or
          (FirstSamplePM(md)))) {
        do_sample = true;
      }
    }
    may_prediction_ = do_sample;
    full_book_->UpdateMD(*md);
  }

  void HandleOB(const OB *ob) {
    if (MD_IDX_INVALID(ob, 1)) {
      td_que_.resize(0);
      od_que_.resize(0);
      prev_ob_ref_ = nullptr;
      output_ = {0.0};
      md_valid_ = false;
    }

    cur_ob_ = *ob;
    if (__glibc_unlikely(prev_ob_ref_ == nullptr)) {
      prev_ob_ = *ob;
      prev_ob_ref_ = &(prev_ob_);
      return;
    }

    md_valid_ = true;
    if (ivolib::SecondsFromString(prev_ob_.UpdateTime) < kSec93000) {
      last_volume_ = 0;
      last_amt_ = 0.0;
    } else {
      last_amt_ = MD_TURNOVER(prev_ob_ref_);
      last_volume_ = MD_VOL(prev_ob_ref_);
    }

    auto turnover_diff = MD_TURNOVER(&cur_ob_) - last_amt_;
    if (FirstSamplePM(ob)) {
      may_prediction_ = abs(turnover_diff-cum_amount_) < TURNOVER_DIFF_THRESHOLD;
      IVOLOG_INFO("{} sample at afternoon: appseq={} dosample={}",
        ob->InstrumentID, last_app_seq_, may_prediction_);
      return;
    }
    // 中间价没变化
    if (std::abs(MD_FAST_MID_PRICE(&cur_ob_) -
                 MD_FAST_MID_PRICE(prev_ob_ref_)) < 1e-6) {
      // 毫秒没变化；如果cum_amount_*downsample>threshold_，则采样
      may_prediction_ = (abs(turnover_diff-cum_amount_) < TURNOVER_DIFF_THRESHOLD and
          (cum_amount_ * downsample_ > threshold_));
      // IVOLOG_INFO("case 0 {} {} {} {} {} {}", turnover_diff, cum_amount_, MD_FAST_MID_PRICE(&cur_ob_),MD_FAST_MID_PRICE(prev_ob_ref_), last_app_seq_, may_prediction_);
      return;
    }
    // 中间价有变化
    // case 1. turnover_diff > cum_amount_; 仍有成交没收齐。
    if (turnover_diff > cum_amount_ + TURNOVER_DIFF_THRESHOLD) {
      // IVOLOG_INFO("case 1 {} {} {} {}", turnover_diff, cum_amount_, last_app_seq_, may_prediction_);
      return;
    }

    // todo 开盘集合竞价检查是否有问题
    // case 2. turnover_diff < cum_amount_; 异常case
    if (__glibc_unlikely(turnover_diff + TURNOVER_DIFF_THRESHOLD < cum_amount_)) {
      IVOLOG_ERROR("{} unexpected case: appseq={} turnover_diff={} cum_amount_={}",
        ob->InstrumentID,
        last_app_seq_, turnover_diff, cum_amount_);
    }

    // case 3. turnover_diff == cum_amount_; 没有TD的OB，仅OD/Cancel引起中间价变化。
    may_prediction_ = true;
    // IVOLOG_INFO("case 3 {} {} {} {}", last_app_seq_, turnover_diff, cum_amount_, may_prediction_);
    return;
  }

private:
  void DoPrediction();

protected:
  std::uint64_t last_app_seq_ = 0;
  double cum_amount_ = 0;
  ivolib::deque<TD> td_que_;
  ivolib::deque<OD> od_que_;
  bool may_prediction_ = false;
  bool md_valid_ = true;

private:
  alignas(16) float normed_[PredictionBase::FACTOR_SIZE] = {0.0};
  std::unique_ptr<FullBook> full_book_;
};

class PredictionDSChange : public Prediction21F {
public:
  PredictionDSChange(const std::string &code, HpParams &params)
      : Prediction21F(
            code, params, DSChangeV4, kInfinityDownSample,
            ivo::mercury::StrategyConfig::instance()
                ->GetConfigWithDefault<bool>("use_short_history", false)) {
    td_que_.reserve(kTickCacheSize);
    od_que_.reserve(kTickCacheSize);
    full_book_ = std::make_unique<FullBook>(code);
  }

  bool HandleMD(const MD *md);
  void HandleOD(const MD *od) {
    
    od_que_.emplace_back(od->TickOrder);
    last_app_seq_ = od->TickOrder.Seq;
    full_book_->UpdateMD(*od);
  }

  bool HandleTD(const MD *td) {
    if (__glibc_unlikely(not md_valid_)) {
      cum_amount_ = 0;
      full_book_->UpdateMD(*td);
      return false;
    }
    bool do_sample = false;
    td_que_.emplace_back(td->TickTrade);
    if (td->TickTrade.TradeFlag != '4') {
      cum_amount_ += (td->TickTrade.Price * td->TickTrade.Qty);
      if (prev_ob_ref_) {
        auto turnover_diff = MD_TURNOVER(&cur_ob_) - last_amt_;
        // 无论是中间价变化还是成交金额*downsample>threshold_，都要求收完TD
        if ((cum_amount_ >= turnover_diff - TURNOVER_DIFF_THRESHOLD) and
            ((cum_amount_ * downsample_ > threshold_) or
            (std::abs(MD_FAST_MID_PRICE(&cur_ob_) -
                  MD_FAST_MID_PRICE(prev_ob_ref_)) > 1e-6) or
            (FirstSamplePM(td)))) {
          do_sample = true;
        }
      }
    } else {
      last_app_seq_ = td->TickTrade.Seq;
    }
    full_book_->UpdateMD(*td);
    return do_sample;
  }

  bool HandleOB(const OB *ob) {
    if (MD_IDX_INVALID(ob, 1)) {
      prev_ob_ref_ = nullptr;
      memset(&cur_ob_, 0, sizeof(cur_ob_));
      output_ = {0.0};
      last_volume_ = 0;
      md_valid_ = false;
      return false;
    }

    if (__glibc_unlikely(prev_ob_ref_ == nullptr)) {
      prev_ob_ = *ob;
      prev_ob_ref_ = &(prev_ob_);
      return false;
    }
    md_valid_ = true;
    if (ivolib::SecondsFromString(prev_ob_.UpdateTime) < kSec93000) {
      last_volume_ = 0;
      last_amt_ = 0.0;
    } else {
      last_amt_ = MD_TURNOVER(prev_ob_ref_);
      last_volume_ = MD_VOL(prev_ob_ref_);
    }

    cur_ob_ = *ob;
    auto turnover_diff = MD_TURNOVER(&cur_ob_) - last_amt_;
    if (FirstSamplePM(ob)) {
      IVOLOG_DEBUG("{} sample at afternoon: appseq={} dosample={}",
        ob->InstrumentID, last_app_seq_, abs(turnover_diff-cum_amount_) < TURNOVER_DIFF_THRESHOLD);
      return abs(turnover_diff-cum_amount_) < TURNOVER_DIFF_THRESHOLD;
    }
    // 中间价没变化
    if (std::abs(MD_FAST_MID_PRICE(&cur_ob_) -
                 MD_FAST_MID_PRICE(prev_ob_ref_)) < 1e-6) {
      // IVOLOG_INFO("case 0 {} {}", MD_FAST_MID_PRICE(&cur_ob_),MD_FAST_MID_PRICE(prev_ob_ref_));
      // 毫秒没变化；如果cum_amount_*downsample>threshold_，则采样
      return (abs(turnover_diff-cum_amount_) < TURNOVER_DIFF_THRESHOLD and
          (cum_amount_ * downsample_ > threshold_));
    }
    // 中间价有变化
    // case 1. turnover_diff > cum_amount_; 仍有成交没收齐。
    if (turnover_diff > cum_amount_ + TURNOVER_DIFF_THRESHOLD) {
      // IVOLOG_INFO("case 1 {} {} {}", turnover_diff, cum_amount_, last_app_seq_);
      return false;
    }

    // case 2. turnover_diff < cum_amount_; 异常case
    if (__glibc_unlikely(turnover_diff + TURNOVER_DIFF_THRESHOLD < cum_amount_)) {
      IVOLOG_ERROR("{} unexpected case: appseq={} turnover_diff={} cum_amount_={}",
        ob->InstrumentID,
        last_app_seq_, turnover_diff, cum_amount_);
    }

    // case 3. turnover_diff == cum_amount_; 没有TD的OB，仅OD/Cancel引起中间价变化。
    // IVOLOG_INFO("case 3 {} {}", turnover_diff, cum_amount_);
    return true;
  }

  bool MayPrediction() {
    if ((prev_ob_ref_ == nullptr || MD_AV1(&cur_ob_) == 0 ||
         (MD_VOL(&cur_ob_) - last_volume_) < 100)) {
      return false;
    }

    auto ms1 =
        ivolib::MillSecondsFromString(prev_ob_.UpdateTime, prev_ob_.MillSec);
    auto ms2 =
        ivolib::MillSecondsFromString(cur_ob_.UpdateTime, cur_ob_.MillSec);
    if (ms1 == ms2) {
      return false;
    }

    DoPrediction();
    return true;
  }

  // sample on ob
  bool HandleOBAndPrediction(const OB *ob) {
    if (not HandleOB(ob)) {
      return false;
    }

    cum_amount_ = MD_TURNOVER(ob) - last_amt_;
    return MayPrediction();
  }

  void DoPrediction();

protected:
  void HandleCachedOD(const OD *od, OrderFactor &factor);
  void HandleCachedTD(const TD *td, OrderFactor&, TradeFactor &factor);
  void Get28FFactor(const OB *cur_ob, HpFactor &factor);

protected:
  std::uint64_t last_app_seq_ = 0;
  OB cur_ob_;
  ivolib::deque<TD> td_que_;
  ivolib::deque<OD> od_que_;
  double cum_amount_ = 0;
  alignas(16) float normed_[PredictionBase::FACTOR_SIZE] = {0.0};
  std::unique_ptr<FullBook> full_book_;
  bool md_valid_ = true;
};

class PredictionInfinity : public PredictionDSChange {
public:
  PredictionInfinity(const std::string &code, HpParams &params)
      : PredictionDSChange(code, params) {
    context_.model = Infinity;
  }

  bool HandleOB(const OB *ob);
};
