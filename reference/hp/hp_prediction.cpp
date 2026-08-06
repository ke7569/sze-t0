#include "hp_prediction.h"
#include "MdClient/TickMarketData.h"
#include "Tsc.h"
#include "Utils/trading_utils.h"
#include "aot2c_auto_hp.h"
#include "base/ivo_base.h"
#include "factor/ivo_math.h"
#include "hp_common.h"
#include "internal/strategy_options.h"
#include "ivo/md_common.h"
#include <array>
#include <cstring>
#include <sys/cdefs.h>
#include <algorithm>
#include <utility>
#include "hp_impl.h"
#include "magic_enum.hpp"
#include "hp_fullob_factor.h"
#include "Utils/Utils.h"

std::array<float, PredictionBase::FACTOR_SIZE> PredictionBase::mean_ = {0.0};
std::array<float, PredictionBase::FACTOR_SIZE> PredictionBase::std_ = {0.0};

void PredictionBase::LoadScaler(const std::string& scaler_file, ModelType model) {
  if (model == ModelType::DSChange) {
    std::ifstream ifs(scaler_file);
    IVOLOG_FATAL_IF(not ifs.good(), "Invalid scaler file {}", scaler_file);

    nlohmann::json scaler;
    ifs >> scaler;
    IVOLOG_INFO("LoadScaler {}", scaler.dump());
    ifs.close();

    int i = 0;
    for (auto& item : scaler["mean"].get<std::vector<float>>()) {
      PredictionBase::mean_[i++] = item;
    }
    i = 0;
    for (auto& item : scaler["std"].get<std::vector<float>>()) {
      PredictionBase::std_[i++] = item;
    }
  }
}

void PredictionBase::Get21FFactor(const OB *cur_ob, HpFactor &factor) {
  factor.hermes = hp_percent_hermes(cur_ob, params_->u_price, params_->l_price);
  factor.tr_sqrt_positive =
      hp_tr_sqrt_positive(prev_ob_ref_, cur_ob, params_->fee_share);
  factor.percent_spread = hp_percent_spread(cur_ob);
  factor.percent_mid_rtn = hp_percent_mid_rtn(prev_ob_ref_, cur_ob);
  factor.fee_on_tick = hp_fee_on_tick(cur_ob);
  factor.bid_vol_chg_ratio = hp_bid_vol_chg_ratio(prev_ob_ref_, cur_ob);
  factor.ask_vol_chg_ratio = hp_ask_vol_chg_ratio(prev_ob_ref_, cur_ob);
  factor.pct_weighted_rtn1 = hp_pct_weighted_rtn1(prev_ob_ref_, cur_ob);
  factor.pct_weighted_rtn2 = hp_pct_weighted_rtn2(prev_ob_ref_, cur_ob);
  factor.pct_weighted_rtn3 = hp_pct_weighted_rtn3(prev_ob_ref_, cur_ob);
  factor.pct_weighted_rtn4 = hp_pct_weighted_rtn4(prev_ob_ref_, cur_ob);
  factor.pct_weighted_rtn5 = hp_pct_weighted_rtn5(prev_ob_ref_, cur_ob);
  factor.pct_weighted_ask = hp_pct_weighted_ask(cur_ob);
  factor.pct_weighted_bid = hp_pct_weighted_bid(cur_ob);
  factor.pct_weighted_ask_rtn = hp_pct_weighted_ask_rtn(prev_ob_ref_, cur_ob);
  factor.pct_weighted_bid_rtn = hp_pct_weighted_bid_rtn(prev_ob_ref_, cur_ob);
  factor.weighted_vol_diff_ratio = hp_weighted_vol_diff_ratio(cur_ob);
  factor.vol_diff_ratio = hp_vol_diff_ratio(cur_ob);
  factor.pct_turnover = hp_pct_turnover(prev_ob_ref_, cur_ob);
  factor.pct_liquidity_ask = hp_pct_liquidity_ask(cur_ob);
  factor.pct_liquidity_bid = hp_pct_liquidity_bid(cur_ob);
}

bool Prediction21F::HandleMD(const MD *md) {
  if (not g_only_snapshot((*md))) {
    return false;
  }

  auto ob = reinterpret_cast<const OB *>(md);
  if (not HandleOB(ob)) {
    return false;
  }

  auto &factor = context_.factor;
  Get21FFactor(ob, factor);
  factor.factor_type = kFactorType21;
#ifdef DS21
  hp_model_DSChangeN8000F21Ver1_3SZ_run(
      &(factor.hermes), &(output_[0]), &context_.prediction_raw, &(output_[0]));
#else
  hp_model_EvaluationVer2_0_1SH_run(&(factor.hermes), &(output_[0]),
                                    &context_.prediction_raw, &(output_[0]));
#endif
  context_.prediction = context_.prediction_raw;
  context_.prediction = std::clamp(context_.prediction, -15.0f, 15.0f);
  context_.last_ob = *(ob);
  prev_ob_ = *(ob);

  if (sample_count_ < kSampleCountThreshold) {
    ++sample_count_;
    context_.prediction = context_.prediction_raw = 0;
  }

  return true;
}

bool Prediction21F::HandleOB(const OB *ob) {
  if (MD_IDX_INVALID(ob, 1)) {
    prev_ob_ref_ = nullptr;
    output_ = {0.0};
    return false;
  }

  if (__glibc_unlikely(prev_ob_ref_ == nullptr)) {
    prev_ob_ = *ob;
    prev_ob_ref_ = &(prev_ob_);
    return false;
  }

  static auto k1300InMs = ivolib::MillSecondsFromString("13:00:00", 0);

  auto ms = ivolib::MillSecondsFromString(ob->UpdateTime, ob->MillSec);
  auto prev_ob_ms = ivolib::MillSecondsFromString(prev_ob_ref_->UpdateTime,
                                                  prev_ob_ref_->MillSec);

  if (ms - prev_ob_ms > 100000UL && ms >= k1300InMs) { // 100s
    return true;
  }

  if (ms == prev_ob_ms || MD_TURNOVER(ob) <= MD_TURNOVER(prev_ob_ref_)) {
    return false;
  }

#ifdef DS21
  if (std::abs(MD_FAST_MID_PRICE(ob) - MD_FAST_MID_PRICE(prev_ob_ref_)) >
      1e-6) {
    return true;
  }
#endif



  return (MD_TURNOVER(ob) - MD_TURNOVER(prev_ob_ref_)) * downsample_ >
         threshold_;
}

bool PredictionLeading21F::HandleMD(const MD *md) {
  if (not g_only_snapshot(*md)) {
    return false;
  }

  cur_tsc_ = ivolib::getTscTick();
  auto ob = reinterpret_cast<const OB *>(md);

  if (MD_IDX_INVALID(ob, 1)) {
    prev_ob_ref_ = nullptr;
    output_ = {0.0};
    return false;
  }

  if (__glibc_unlikely(prev_ob_ref_ == nullptr)) {
    prev_ob_ = *ob;
    prev_ob_ref_ = &(prev_ob_);
    return false;
  }

  cur_ob_ = *ob;
  return false;
}

bool PredictionLeadingDSChange::HandleMD(const MD *md) {
  switch (md->TickType) {
  case ivolib::TICKTYPE_ORDER: {
    HandleOD(md);
    break;
  }
  case ivolib::TICKTYPE_TRADE: {
    HandleTD(md);
    break;
  }
  case ivolib::SNAPSHOT_MATCH: {
    HandleOB(reinterpret_cast<const OB *>(md));
    break;
  }
  default:
    break;
  }
  return false;
}

void PredictionLeadingDSChange::Get28FFactor(const OB *cur_ob, HpFactor &factor) {
  OrderFactor order_factor;
  TradeFactor trade_factor;
  for (const auto &item : od_que_) {
    HandleCachedOD(&item, order_factor);
  }
  od_que_.resize(0);

  for (const auto &item : td_que_) {
    HandleCachedTD(&item, trade_factor);
  }
  td_que_.resize(0);

  factor.order_pf = order_factor.order_pf;
  factor.order_nf = order_factor.order_nf;
  factor.order_mf = order_factor.order_mf / params_->fee_share;
  factor.trade_pcf = trade_factor.trade_pcf / params_->fee_share;
  factor.trade_ncf = trade_factor.trade_ncf / params_->fee_share;
  factor.trade_pt = trade_factor.trade_pt / params_->fee_share;
  factor.trade_nt = trade_factor.trade_nt / params_->fee_share;

  // SH
  {
    factor.order_pf += trade_factor.order_pf;
    factor.order_nf += trade_factor.order_nf;
    factor.trade_pcf = order_factor.cxl_buy_flow / params_->fee_share;
    factor.trade_ncf = order_factor.cxl_sell_flow / params_->fee_share;

    order_factor.buy_order_volume += trade_factor.buy_order_volume;
    order_factor.sell_order_volume += trade_factor.sell_order_volume;
  }

  auto cur_tsc = ivolib::MillSecondsFromString(cur_ob_.UpdateTime, cur_ob_.MillSec);
  full_book_->GetFullOBFactor(order_factor, trade_factor, factor, cur_tsc, params_->fee_share);
}

void PredictionLeadingDSChange::HandleCachedOD(const OD* od, OrderFactor& factor) {
  double* pf = &(factor.order_pf);
  double* nf = &(factor.order_nf);
  double* mf = &(factor.order_mf);
  double order_pf_vol = 0;
  double order_nf_vol = 0;

  {
    double banch_price = MD_BP1(prev_ob_ref_);
    double limit_price = MD_AV1(prev_ob_ref_) == 0
                             ? (banch_price + params_->price_tick)
                             : (MD_AP1(prev_ob_ref_));
    if (OD_BUY(od) && SH_OD_INSERT(od) && od->Price < limit_price - 1e-6) {
      double weight = 1 - std::tanh((banch_price / od->Price - 1) * 1000 * 0.1);
      *pf += (od->Qty / params_->fee_share * weight);
    }
  }

  {
    double banch_price = MD_AP1(prev_ob_ref_);
    double limit_price = MD_BV1(prev_ob_ref_) == 0
                             ? (banch_price - params_->price_tick)
                             : (MD_BP1(prev_ob_ref_));
    if (OD_SELL(od) && SH_OD_INSERT(od) && od->Price > limit_price + 1e-6) {
      double weight = 1 - std::tanh((od->Price / banch_price - 1) * 1000 * 0.1);
      *nf += (od->Qty / params_->fee_share * weight);
    }
  }

  if (OD_BUY(od)) {
    if (od->Price > MD_AP1(prev_ob_ref_) + 1e-6) {
      order_pf_vol = od->Qty;
    }
    if (SH_OD_CXL(od)) {
      factor.cxl_buy_flow += od->Qty;
    } else {
      factor.buy_order_volume += od->Qty;
    }
  } else {
    if (od->Price < MD_BP1(prev_ob_ref_) - 1e-6) {
      order_nf_vol = od->Qty;
    }
    if (SH_OD_CXL(od)) {
      factor.cxl_sell_flow += od->Qty;
    } else {
      factor.sell_order_volume += od->Qty;
    }
  }
  *mf += (order_pf_vol - order_nf_vol);
}

void PredictionLeadingDSChange::HandleCachedTD(const TD* td, TradeFactor& factor) {
  if (td->TradeFlag != '4') {
    if (td->BidNo > td->AskNo) {
      factor.trade_pt += td->Qty;
    } else if (td->BidNo < td->AskNo) {
      factor.trade_nt += td->Qty;
    }
  }

  double* pf = &(factor.order_pf);
  double* nf = &(factor.order_nf);

  if (td->BidNo > td->AskNo) {
    double bench_price = MD_BP1(prev_ob_ref_);
    double weight = 1 - std::tanh((bench_price / td->Price - 1) * 1000 * 0.1);
    *pf += (td->Qty / params_->fee_share * weight);
    // SH TD
    if (td->TradeFlag != '4' and td->TradeFlag != 'F') {
      factor.buy_order_volume += td->Qty;
    }
  } else {
    double bench_price = MD_AP1(prev_ob_ref_);
    double weight = 1 - std::tanh((td->Price / bench_price - 1) * 1000 * 0.1);
    *nf += (td->Qty / params_->fee_share * weight);
    if (td->TradeFlag != '4' and td->TradeFlag != 'F') {
      factor.sell_order_volume += td->Qty;
    }
  }
}

void PredictionLeadingDSChange::DoPrediction() {
  auto &factor = context_.factor;
  factor.factor_type = KFactorTypeFullob;
  Get21FFactor(&(cur_ob_), factor);
  Get28FFactor(&(cur_ob_), factor);

  full_book_->Normalized(&factor, normed_, mean_, std_);
  hp_model_DSChangeN8000CobVer4_1SH_run(normed_, &(output_[0]),
                                     &context_.prediction_raw, &(output_[0]));

  context_.prediction = std::clamp(context_.prediction_raw, -15.0f, 15.0f);
  context_.last_ob = cur_ob_;
  context_.app_seq = last_app_seq_;
  prev_ob_ = cur_ob_;
  cum_amount_ = 0;
  may_prediction_ = false;
}

void PredictionDSChange::HandleCachedOD(const OD* od, OrderFactor& factor) {
  double* pf = &(factor.order_pf);
  double* nf = &(factor.order_nf);
  double* mf = &(factor.order_mf);
  double order_pf_vol = 0;
  double order_nf_vol = 0;

  {
    double banch_price = MD_BP1(prev_ob_ref_);
    double limit_price = MD_AV1(prev_ob_ref_) == 0
                             ? (banch_price + params_->price_tick)
                             : (MD_AP1(prev_ob_ref_));
    if (OD_BUY(od) && od->Price < limit_price - 1e-6) {
      double weight = 1 - std::tanh((banch_price / od->Price - 1) * 1000 * 0.1);
      *pf += (od->Qty / params_->fee_share * weight);
    }
  }

  {
    double banch_price = MD_AP1(prev_ob_ref_);
    double limit_price = MD_BV1(prev_ob_ref_) == 0
                             ? (banch_price - params_->price_tick)
                             : (MD_BP1(prev_ob_ref_));
    if (OD_SELL(od) && od->Price > limit_price + 1e-6) {
      double weight = 1 - std::tanh((od->Price / banch_price - 1) * 1000 * 0.1);
      *nf += (od->Qty / params_->fee_share * weight);
    }
  }

  if (OD_BUY(od)) {
    if (od->OrdType != TMD_O_LMP || od->Price > MD_AP1(prev_ob_ref_) + 1e-6) {
      order_pf_vol = od->Qty;
    }
    if (not SH_OD_CXL(od)) {
      factor.buy_order_volume += od->Qty;
    }
  } else {
    if (od->OrdType != TMD_O_LMP || od->Price < MD_BP1(prev_ob_ref_) - 1e-6) {
      order_nf_vol = od->Qty;
    }
    if (not SH_OD_CXL(od)) {
      factor.sell_order_volume += od->Qty;
    }
  }
  *mf += (order_pf_vol - order_nf_vol);
}

void PredictionDSChange::HandleCachedTD(const TD* td, OrderFactor& orderfactor, TradeFactor& factor) {
  if (td->TradeFlag == '4') { // cxl
    if (td->AskNo == 0) {
      factor.trade_pcf += td->Qty;
      orderfactor.cxl_buy_flow += td->Qty;
    }
    if (td->BidNo == 0) {
      factor.trade_ncf += td->Qty;
      orderfactor.cxl_sell_flow += td->Qty;
    }
  // // todo 要考虑SH？
  // } else if (td->TradeFlag == 'F') {
  } else {
    if (td->BidNo > td->AskNo) {
      factor.trade_pt += td->Qty;
      // todo 要考虑SH TD？
      if (td->TradeFlag != 'F') {
        factor.buy_order_volume += td->Qty;
      }
    } else if (td->BidNo < td->AskNo) {
      factor.trade_nt += td->Qty;
      // todo 要考虑SH TD？
      if (td->TradeFlag != 'F') {
        factor.sell_order_volume += td->Qty;
      }
    }
  }
}

void PredictionDSChange::Get28FFactor(const OB *cur_ob, HpFactor &factor) {
  OrderFactor order_factor;
  TradeFactor trade_factor;

  for (const auto &item : od_que_) {
    HandleCachedOD(&item, order_factor);
  }
  od_que_.resize(0);

  for (const auto &item : td_que_) {
    HandleCachedTD(&item, order_factor, trade_factor);
  }
  td_que_.resize(0);

  factor.order_pf = order_factor.order_pf;
  factor.order_nf = order_factor.order_nf;
  factor.order_mf = order_factor.order_mf / params_->fee_share;
  factor.trade_pcf = trade_factor.trade_pcf / params_->fee_share;
  factor.trade_ncf = trade_factor.trade_ncf / params_->fee_share;
  factor.trade_pt = trade_factor.trade_pt / params_->fee_share;
  factor.trade_nt = trade_factor.trade_nt / params_->fee_share;

  auto cur_tsc = ivolib::MillSecondsFromString(cur_ob_.UpdateTime, cur_ob_.MillSec);
  full_book_->GetFullOBFactor(order_factor, trade_factor, factor, cur_tsc, params_->fee_share);
}

bool PredictionDSChange::HandleMD(const MD *md) {
  bool prediction = false;
  bool should_prediction = false;
  switch (md->TickType) {
  case ivolib::TICKTYPE_ORDER: {
    // IVOLOG_INFO("order:{}", md->TickOrder.Seq);
    HandleOD(md);
    break;
  }
  case ivolib::TICKTYPE_TRADE: {
    // IVOLOG_INFO("order:{}", md->TickTrade.Seq);
    should_prediction = HandleTD(md);
    break;
  }
  case ivolib::SNAPSHOT_MATCH: {
    // IVOLOG_INFO("order:{}.{}", md->UpdateTime, md->MillSec);
    should_prediction = HandleOB(reinterpret_cast<const OB *>(md));
    break;
  }
  default:
    break;
  }
  auto sec = ivolib::SecondsFromString(md->UpdateTime);
  if (sec >= kSec93000 and should_prediction) {
    prediction = MayPrediction();
  }
  return prediction;
}

void PredictionDSChange::DoPrediction() {
  auto &factor = context_.factor;
  factor.factor_type = KFactorTypeFullob;
  Get21FFactor(&cur_ob_, factor);
  Get28FFactor(&cur_ob_, factor);

  full_book_->Normalized(&factor, normed_, mean_, std_);
  hp_model_DSChangeN8000CobVer4_1SZ_run(normed_, &(output_[0]),
                                     &context_.prediction_raw, &(output_[0]));
  prev_ob_ = cur_ob_;

  context_.prediction = std::clamp(context_.prediction_raw, -15.0f, 15.0f);
  context_.last_ob = cur_ob_;
  context_.app_seq = last_app_seq_;
  cum_amount_ = 0;
}

bool PredictionInfinity::HandleOB(const OB *ob) {
  if (MD_IDX_INVALID(ob, 1)) {
    prev_ob_ref_ = nullptr;
    memset(&prev_ob_, 0, sizeof(prev_ob_));
    output_ = {0.0};
    return false;
  }

  if (__glibc_unlikely(prev_ob_ref_ == nullptr)) {
    prev_ob_ = *ob;
    prev_ob_ref_ = &(prev_ob_);
    return false;
  }

  auto ms1 =
      ivolib::MillSecondsFromString(prev_ob_.UpdateTime, prev_ob_.MillSec);
  auto ms2 = ivolib::MillSecondsFromString(ob->UpdateTime, ob->MillSec);
  if (ms1 == ms2) {
    return false;
  }

  if ((MD_TURNOVER(ob) - MD_TURNOVER(prev_ob_ref_)) * downsample_ <
      threshold_) {
    return false;
  }

  auto &factor = context_.factor;
  factor.factor_type = kFactorType28;
  Get21FFactor(ob, factor);
  Get28FFactor(ob, factor);

  hp_model_InfinityN8000W100Ver1_0SZ_run(&(factor.order_pf), &(output_[0]),
                                         &context_.prediction_raw,
                                         &(output_[0]));
  prev_ob_ = *ob;

  context_.prediction = std::clamp(context_.prediction_raw, -15.0f, 15.0f);
  context_.last_ob = *ob;
  context_.app_seq = last_app_seq_;
  context_.model = Infinity;
  cum_amount_ = 0;
  return true;
}

void FullBook::GetFullOBFactor(const OrderFactor& order_factor, const TradeFactor& trade_factor,
      HpFactor& factor, int32_t cur_tsc, double fee_share) {
    auto& askdepth = GetAskDepth();
    auto& biddepth = GetBidDepth();
    auto fullob = hp_foreach_depth(askdepth, biddepth);

    // IVOLOG_INFO("fullob ask_level1:{},{},{},{},{},{}",
    //     fullob.ask_level1.price,
    //     fullob.ask_level1.volume_sum,
    //     fullob.ask_01.count_sum,
    //     fullob.ask_01.tsc_sum,
    //     fullob.ask_level1.amt_sum,test_last_appseq_
    //     );
    // IVOLOG_INFO("fullob bid_level1:{},{},{},{},{}",
    //     fullob.bid_level1.price,
    //     fullob.bid_level1.volume_sum,
    //     fullob.bid_level1.count_sum,
    //     fullob.bid_level1.tsc_sum,
    //     fullob.bid_level1.amt_sum
    //     );

    // IVOLOG_INFO("fullob order_factor:{},{},{},{},{},{},{}",
    //   last_app_seq_,
    //   order_factor.order_pf,
    //   order_factor.order_nf,
    //   order_factor.cxl_buy_flow,
    //   order_factor.cxl_sell_flow,
    //   order_factor.buy_order_volume,
    //   order_factor.sell_order_volume);

    // IVOLOG_INFO("fullob trade_factor:{},{},{},{},{},{},{}",
    //   last_app_seq_,
    //   trade_factor.trade_pcf,
    //   trade_factor.trade_ncf,
    //   trade_factor.trade_pt,
    //   trade_factor.trade_nt,
    //   trade_factor.buy_order_volume,
    //   trade_factor.sell_order_volume);


    factor.PositiveFillRate = hp_positive_fillrate(order_factor, trade_factor);
    factor.NegativeFillRate = hp_negative_fillrate(order_factor, trade_factor);
    factor.OrderFlowImbalance = hp_orderflow_imbalance(order_factor, trade_factor);
#ifdef TEST_LOCAL
  if (instrument_id_ == "000001.SZ") {
    fee_share = 816048.1215;
  } else if (instrument_id_ == "600036.SH") {
    fee_share = 1171836.1832;
  }
#endif
    factor.CFRImbalance = hp_cfr_imbalance(order_factor, trade_factor, fee_share);

    if (fullob.ask_total_count == 0 || fullob.bid_total_count == 0) {
      return;
    }

    factor.FixDisImbalancePct1 = hp_fix_dis_imbalance(fullob.ask_01, fullob.bid_01);
    factor.FixDisImbalancePct2 = hp_fix_dis_imbalance(fullob.ask_05, fullob.bid_05);
    factor.WeightedFixDisImbalancePct1 = hp_weighted_fix_dis_imbalance(fullob.ask_01, fullob.bid_01,
        fullob.mp, fullob.mp * 0.01);
    factor.WeightedFixDisImbalancePct2 = hp_weighted_fix_dis_imbalance(fullob.ask_05, fullob.bid_05,
        fullob.mp, fullob.mp * 0.05);
    factor.AvgSizeImbalance = hp_avg_size_imbalance(fullob.ask_10, fullob.bid_10);
    factor.AvgSizeImbalanceLevel1 = hp_avg_size_imbalance(fullob.ask_level1, fullob.bid_level1);
    factor.AvgSizeImbalanceLevel5 = hp_avg_size_imbalance(fullob.ask_level5, fullob.bid_level5);
    factor.OrderCountImbalance = hp_order_count_imbalance(fullob.ask_10, fullob.bid_10);
    factor.OrderCountImbalanceLevel1 = hp_order_count_imbalance(fullob.ask_level1, fullob.bid_level1);
    factor.OrderCountImbalanceLevel5 = hp_order_count_imbalance(fullob.ask_level5, fullob.bid_level5);
    factor.OrderLifeImbalance = hp_order_life_imbalance(fullob.ask_10, fullob.bid_10, cur_tsc);
    factor.OrderLifeImbalanceLevel1 = hp_order_life_imbalance(fullob.ask_level1, fullob.bid_level1, cur_tsc);
    factor.OrderLifeImbalanceLevel5 = hp_order_life_imbalance(fullob.ask_level5, fullob.bid_level5, cur_tsc);
    factor.MaxBidDistance = fullob.bid_max_level_price / fullob.mp - 1;
    factor.MaxAskDistance = fullob.ask_max_level_price / fullob.mp - 1;
    factor.MaxVolDistanceImbalance = hp_max_vol_distance_imbalance(fullob.ask_max_level_price, fullob.bid_max_level_price, fullob.mp);
    factor.YoungOrderbookImbalance = hp_young_orderbook_imbalance(askdepth, biddepth, fullob.mp, cur_tsc);
    factor.FixDistHermes = hp_fix_dist_hermes(askdepth, biddepth, fullob.mp, 0.05);
  }

template<typename MEAN, typename STD>
void FullBook::Normalized(HpFactor* factor, float* normed, const MEAN& mean, const STD& std) {
  static size_t f7_len = offsetof(HpFactor, hermes) / sizeof(float);
  static size_t f28_index = offsetof(HpFactor, PositiveFillRate) / sizeof(float);
  static size_t f21_len = f28_index - f7_len;
  for (auto i = 0; i < f21_len; ++i) {
    *(normed+i) = (*((float*)factor+i+f7_len) - mean[i]) / std[i];
  }

  for (auto i = f21_len; i < f28_index; ++i) {
    *(normed+i) = (*((float*)factor+i-f21_len) - mean[i]) / std[i];
  }

  for (auto i = f28_index; i < HpFactor::factor_size; ++i) {
    *(normed+i) = (*((float*)factor+i) - mean[i]) / std[i];
  }
}