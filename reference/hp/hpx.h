#pragma once
#include "Channel.pb.h"
#include "IVOOrderInfo.h"
#include "containers.h"
#include "hp_common.h"
#include "hpx.pb.h"
#include "nlohmann/json_fwd.hpp"
#include <limits>
#include <unordered_map>

class HpInstrument;

struct PendingQuoteX : public PendingQuote {
  std::int32_t qty = 0;
  std::int32_t index = 0;
};

using LmpQueue = ivolib::deque<PendingQuoteX>;
using LmpQuoteIter = LmpQueue::iterator;

class Hpx {
public:
  void Init(HpInstrument *ins);
  void AfterPrediction(HpOrderContext &context);
  void HandleOrderRtn(const ivolib::OrderInfoSt &rtn,
                      int32_t delta_virtual_long, int32_t delta_virtual_short);
  void OnPriceInfo(const HpXVer::PriceInfo &pb);
  void OnTheoProgress(const HpXVer::TheoProgress& pb);
  double GetTheoProgress() const {return theo_progress_;}

private:
  bool HitBuy(HpOrderContext &context);
  bool HitSell(HpOrderContext &context);
  bool QuoteBuy(HpOrderContext &context);
  bool QuoteSell(HpOrderContext &context);
  // 对齐报单量，避免报小单
  std::int32_t AdjustBuyQty(std::int32_t qty, double price, const HpOrderContext& context);
  std::int32_t AdjustSellQty(std::int32_t qty, double price, const HpOrderContext& context);

  std::int32_t CalcHitQty(bool buy, double margin,
                          const HpOrderContext &context);
  std::int32_t CalcQuoteQty(bool buy, double margin, double price,
                            const HpOrderContext &context);
  bool CanQuoteOnTop(bool buy, double margin, double price);
  double PriceCount(bool buy, double price);
  void HandleAllPendingQuote(double cancel_buy, double cancel_sell);
  double GetWeightedPendingQuoteCount(nlohmann::ordered_json& js);

private:
  HpInstrument *ins_;
  HpParams *hp_params_;
  GlobalParams *global_params_;

  double force_withdraw_buy_price_ = 0.0;
  double force_withdraw_sell_price_ = std::numeric_limits<double>::max();

  std::unordered_map<std::uint64_t, std::int32_t> pending_buy_volume_;
  std::unordered_map<std::uint64_t, std::int32_t> pending_sell_volume_;

  ivolib::ReqOrderCmdSt hit_sell_order_;
  ivolib::ReqOrderCmdSt hit_buy_order_;
  ivolib::ReqOrderCmdSt quote_sell_order_;
  ivolib::ReqOrderCmdSt quote_buy_order_;

  std::int32_t index_ = 0;
  LmpQueue pending_buy_;
  LmpQueue pending_sell_;

  ivolib::deque<std::pair<std::uint64_t, std::uint64_t>> window_;
  std::uint64_t window_vol_ = 0;

  OB * cur_ob_ = nullptr;
  double theo_price_ = 0;
  double cancel_offset_ = 0;
  double buy_offset_ = 0;
  double sell_offset_ = 0;
  double quote_buy_offset_ = 0;
  double quote_sell_offset_ = 0;

  double real_progress_ = 0;
  double theo_progress_ = 0;
};

class HpxEtf {
public:
  HpxEtf(HpInstrument *ins);
  void OnMarketDataUpdate(const MD &md);

private:
  std::uint64_t last_sec_ = 0;
  HpInstrument *ins_;
  HpParams *hp_params_;
  GlobalParams *global_params_;
  HpOrderContext context_;

  ivolib::ReqOrderCmdSt hit_sell_order_;
  ivolib::ReqOrderCmdSt hit_buy_order_;
};