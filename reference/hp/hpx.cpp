#include "hpx.h"
#include "Channel.pb.h"
#include "IVODataCommDef.h"
#include "Numeric.h"
#include "SpdlogWrapper.h"
#include "hp_common.h"
#include "hp_instrument.h"
#include "hpx.pb.h"
#include "internal/strategy_options.h"
#include "ivo/md_common.h"
#include "nlohmann/json_fwd.hpp"
#include "strategy_config.h"
#include "string_utils.h"
#include "trading_utils.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <sys/cdefs.h>

void Hpx::Init(HpInstrument *ins) {
  ins_ = ins;
  hp_params_ = ins->GetParams();
  global_params_ = hp_params_->global_params;
  pending_buy_.reserve(1024);
  pending_sell_.reserve(1024);
  window_.reserve(1024);

  auto fInitOrder = [&](ivolib::ReqOrderCmdSt &order, ivolib::Direction dic,
                        const std::string &st_name) {
    ivolib::strcpyN(order.instrumentId, ins->GetInstrumentID());
    order.direction = dic;
    order.orderType = ivolib::OrderType::LMP_ORDER;
    order.ordAction = ivolib::OrderAction::ORDER_INSERT,
    strcpy(order.stName, st_name.data());
    strcpy(order.appName,
           ivo::mercury::StrategyConfig::instance()->app_name.data());
  };

  auto global_info = GlobalInfo::instance();
  auto vec = ivolib::split(global_info->fast_name, '_');
  auto quote_name = fmt::format("{}_q_{}", vec[0], vec[1]);
  fInitOrder(hit_sell_order_, ivolib::Direction::SELL, global_info->fast_name);
  fInitOrder(hit_buy_order_, ivolib::Direction::BUY, global_info->fast_name);
  fInitOrder(quote_sell_order_, ivolib::Direction::SELL, quote_name);
  fInitOrder(quote_buy_order_, ivolib::Direction::BUY, quote_name);
}

void Hpx::AfterPrediction(HpOrderContext &context) {
  cur_ob_ = &(context.last_ob);
  theo_price_ = context.theo.theo0;

  // UpdateLiquidity
  auto sec = ivolib::SecondsFromString(cur_ob_->UpdateTime);
  window_.emplace_back(std::make_pair(sec, cur_ob_->Volume));
  while (window_.size() > 1) {
    if (sec - window_.front().first > global_params_->liquidity_window) {
      window_.pop_front();
    } else {
      break;
    }
  }

  window_vol_ = window_.back().second - window_.front().second;

  if ((hp_params_->total_execution_volume == 0 && hp_params_->pi_fast != 0) ||
      (hp_params_->total_execution_volume ^ hp_params_->pi_fast) <= 0 ||
      (((hp_params_->total_execution_volume - hp_params_->pi_fast) ^
        (hp_params_->total_execution_volume)) < 0)) {
    real_progress_ = 0;
  } else {
    real_progress_ = std::abs(static_cast<double>(hp_params_->pi_fast) /
                              hp_params_->total_execution_volume);
  }

  auto sync_factor = (real_progress_ - theo_progress_) * 1e2 *
                     global_params_->progress_sync_rate * 1e-3;

  nlohmann::ordered_json js;
  auto weighted_pending_quote_amount = GetWeightedPendingQuoteCount(js);
  auto pending_quote_penalty = weighted_pending_quote_amount /
                               hp_params_->GetFakeExecVolume() *
                               global_params_->progress_sync_rate * 1e-3;

  // CalcParams
  auto et = global_params_->execution_tolerance_x;
  auto price_adj_factor = std::max(5 / context.mid_p - 1, 0.0);
  et += price_adj_factor;
  auto ot = -et / 2 * 1e-3;

  auto quote_offset = global_params_->quote_offset * 1e-3;
  buy_offset_ = 1 - ot - sync_factor;
  sell_offset_ = 1 + ot + sync_factor;
  quote_buy_offset_ = 1 - quote_offset - sync_factor - pending_quote_penalty;
  quote_sell_offset_ = 1 + quote_offset + sync_factor + pending_quote_penalty;
  cancel_offset_ = global_params_->cancel_offset * 1e-3;

  if (context.max_sell_fast_vol > 0 || context.max_buy_fast_vol > 0) {
    context.fast_quote_offset = quote_offset;
    context.fast_buy_offset = buy_offset_;
    context.fast_sell_offset = sell_offset_;
    context.fast_quote_buy_offset = quote_buy_offset_;
    context.fast_quote_sell_offset = quote_sell_offset_;
  }

  if (not ins_->CanInsertOrder()) {
    return;
  }

  // CancelPendingQuoteOrders
  auto buy_hit_price =
      theo_price_ * (1 - context.theo.offset) * (1 + sync_factor);
  auto sell_hit_price =
      theo_price_ * (1 + context.theo.offset) * (1 - sync_factor);

  for (auto iter = pending_buy_.begin(); iter != pending_buy_.end(); ++iter) {
    if (iter->qty <= 0) {
      continue;
    }

    if (iter->price > sell_hit_price) {
      iter->qty = 0;
      ins_->CancelOrder(iter->id);
    }
  }

  for (auto iter = pending_sell_.begin(); iter != pending_sell_.end(); ++iter) {
    if (iter->qty <= 0) {
      continue;
    }

    if (iter->price < buy_hit_price) {
      iter->qty = 0;
      ins_->CancelOrder(iter->id);
    }
  }

  // Cancel over qty
  HandleAllPendingQuote(force_withdraw_buy_price_, force_withdraw_sell_price_);
  force_withdraw_buy_price_ = 0;
  force_withdraw_sell_price_ = std::numeric_limits<double>::max();

  // Cancel other
  auto buy_cancel_price =
      theo_price_ *
      (1 - cancel_offset_ * global_params_->cancel_offset_multiplier);
  auto sell_cancel_price =
      theo_price_ *
      (1 + cancel_offset_ * global_params_->cancel_offset_multiplier);
  HandleAllPendingQuote(buy_cancel_price, sell_cancel_price);

  // Execute
  if (HitBuy(context)) {
  } else if (HitSell(context)) {
  } else if (QuoteBuy(context)) {
  } else {
    QuoteSell(context);
  }
}

void Hpx::HandleOrderRtn(const ivolib::OrderInfoSt &rtn,
                         int32_t delta_virtual_long,
                         int32_t delta_virtual_short) {
  if (rtn.orderType != ivolib::OrderType::LMP_ORDER) {
    return;
  }

  if (rtn.direction == ivolib::Direction::BUY) {
    auto iter = std::find_if(
        pending_buy_.begin(), pending_buy_.end(),
        [&](const PendingQuoteX &item) { return item.id == rtn.orderRefId; });
    if (iter != pending_buy_.end()) {
      iter->qty += delta_virtual_long;
      if (iter->qty <= 0) {
        pending_buy_.erase(iter);
      }
    }
    return;
  }

  auto iter = std::find_if(
      pending_sell_.begin(), pending_sell_.end(),
      [&](const PendingQuoteX &item) { return item.id == rtn.orderRefId; });
  if (iter != pending_sell_.end()) {
    iter->qty += delta_virtual_short;
    if (iter->qty <= 0) {
      pending_sell_.erase(iter);
    }
  }
}

void Hpx::OnPriceInfo(const HpXVer::PriceInfo &pb) {
  if (__glibc_unlikely(pb.instrument() != ins_->GetInstrumentID())) {
    IVOLOG_WARN("[ErrorPriceInfo] ins={},pb={}", ins_->GetInstrumentID(), pb.ShortDebugString());
    return;
  }

  if (pb.has_force_withdraw_buy_price()) {
    if (__glibc_unlikely(_almostEqual(pb.force_withdraw_buy_price(), 0))) {
      IVOLOG_WARN("[InvalidPrice] ins={},price={}", pb.instrument(),
                  pb.force_withdraw_buy_price());
    } else {
      force_withdraw_buy_price_ = pb.force_withdraw_buy_price();
    }
  }

  if (pb.has_force_withdraw_sell_price()) {
    if (__glibc_unlikely(_almostEqual(pb.force_withdraw_sell_price(), 0))) {
      IVOLOG_WARN("[InvalidPrice] ins={},price={}", pb.instrument(),
                  pb.force_withdraw_sell_price());
    } else {
      force_withdraw_sell_price_ = pb.force_withdraw_sell_price();
    }
  }

  if (pb.pending_buy_volume_dict_size() > 0) {
    pending_buy_volume_.clear();
    for (const auto &item : pb.pending_buy_volume_dict()) {
      pending_buy_volume_.emplace(item.price() * 1000UL, item.volume());
    }
  }

  if (pb.pending_sell_volume_dict_size() > 0) {
    pending_sell_volume_.clear();
    for (const auto &item : pb.pending_sell_volume_dict()) {
      pending_sell_volume_.emplace(item.price() * 1000UL, item.volume());
    }
  }
}

void Hpx::OnTheoProgress(const HpXVer::TheoProgress &pb) {
  if (__glibc_unlikely(
          pb.instrument() != ins_->GetInstrumentID() ||
          GlobalInfo::instance()->batch_code != pb.managed_suffix() ||
          GlobalInfo::instance()->seq_char != pb.key_suffix()[0])) {
    IVOLOG_WARN("[ErrorTheoProgress] ins={},pb={}", ins_->GetInstrumentID(),
                pb.ShortDebugString());
    return;
  }

  theo_progress_ = pb.value();
}

bool Hpx::HitBuy(HpOrderContext &context) {
  auto margin = theo_price_ * buy_offset_ / cur_ob_->AskPrice1 - 1;
  if (margin <= 0) {
    return false;
  }

  auto bv = CalcHitQty(true, margin, context);
  if (bv <= 0) {
    return false;
  }

  auto &order = hit_buy_order_;
  order.insertTime = cur_ob_->TimeStamp;
  order.origQty = bv;
  order.price = MD_AP1(cur_ob_);
  auto send_qty = ins_->SendOrder(order, &context.fast_hit_buy_id);
  if (__glibc_likely(send_qty > 0)) {
    hp_params_->WantBuyFast(send_qty);
    context.hit_buy_qty += send_qty;
    PendingQuoteX dummy;
    dummy.price = MD_AP1(cur_ob_);
    dummy.id = context.fast_hit_buy_id;
    dummy.qty = send_qty;
    dummy.index = ++index_;
    pending_buy_.emplace_back(std::move(dummy));
    return true;
  }
  return false;
}

bool Hpx::HitSell(HpOrderContext &context) {
  auto margin = cur_ob_->BidPrice1 / (theo_price_ * sell_offset_) - 1;
  if (margin <= 0) {
    return false;
  }

  auto sv = CalcHitQty(false, margin, context);
  if (sv <= 0) {
    return false;
  }
  auto &order = hit_sell_order_;
  order.insertTime = cur_ob_->TimeStamp;
  order.origQty = sv;
  order.price = MD_BP1(cur_ob_);
  auto send_qty = ins_->SendOrder(order, &context.fast_hit_sell_id);
  if (__glibc_likely(send_qty > 0)) {
    hp_params_->WantSellFast(send_qty);
    context.hit_sell_qty += send_qty;
      PendingQuoteX dummy;
      dummy.price = MD_BP1(cur_ob_);
      dummy.id = context.fast_hit_sell_id;
      dummy.qty = send_qty;
      dummy.index = ++index_;
      pending_sell_.emplace_back(std::move(dummy));
    return true;
  }
  return false;
}

bool Hpx::QuoteBuy(HpOrderContext &context) {
  auto quote_buy_price =
      std::min(std::floor((2 * theo_price_ - cur_ob_->AskPrice1) * 100) / 100.0,
               cur_ob_->AskPrice1 - hp_params_->price_tick);
  context.fast_quote_buy_price = quote_buy_price;

  auto quote_margin = theo_price_ * quote_buy_offset_ / quote_buy_price - 1;
  if (quote_margin > 0 && global_params_->can_quote) {
    if (not CanQuoteOnTop(true, quote_margin, quote_buy_price)) {
      return false;
    }

    auto bv = CalcQuoteQty(true, quote_margin, quote_buy_price, context);
    if (bv <= 0) {
      return false;
    }

    auto &order = quote_buy_order_;
    order.insertTime = cur_ob_->TimeStamp;
    order.origQty = bv;
    order.price = quote_buy_price;
    auto send_qty = ins_->SendOrder(order, &context.fast_hit_buy_id);
    if (__glibc_likely(send_qty > 0)) {
      hp_params_->WantBuyFast(send_qty);
      PendingQuoteX dummy;
      dummy.price = quote_buy_price;
      dummy.id = context.fast_hit_buy_id;
      dummy.qty = send_qty;
      dummy.index = ++index_;
      pending_buy_.emplace_back(std::move(dummy));
      return true;
    }
  }
  return false;
}

bool Hpx::QuoteSell(HpOrderContext& context) {
  auto quote_sell_price =
      std::max(std::ceil((2 * theo_price_ - cur_ob_->BidPrice1) * 100) / 100.0,
               cur_ob_->BidPrice1 + hp_params_->price_tick);
  context.fast_quote_sell_price = quote_sell_price;

  auto quote_margin = quote_sell_price / (theo_price_ * quote_sell_offset_) - 1;
  if (quote_margin > 0 && global_params_->can_quote) {
    if (not CanQuoteOnTop(false, quote_margin, quote_sell_price)) {
      return false;
    }

    auto bv = CalcQuoteQty(false, quote_margin, quote_sell_price, context);
    if (bv <= 0) {
      return false;
    }

    auto &order = quote_sell_order_;
    order.insertTime = cur_ob_->TimeStamp;
    order.origQty = bv;
    order.price = quote_sell_price;
    auto send_qty = ins_->SendOrder(order, &context.fast_hit_sell_id);
    if (__glibc_likely(send_qty > 0)) {
      hp_params_->WantSellFast(send_qty);
      PendingQuoteX dummy;
      dummy.price = quote_sell_price;
      dummy.id = context.fast_hit_sell_id;
      dummy.qty = send_qty;
      dummy.index = ++index_;
      pending_sell_.emplace_back(std::move(dummy));
      return true;
    }
  }
  return false;
}

std::int32_t Hpx::AdjustBuyQty(std::int32_t qty, double price,
                               const HpOrderContext &context) {
  std::int32_t min_qty =
      std::ceil(hp_params_->min_order_size / price / hp_params_->vol_tick) *
      hp_params_->vol_tick;
  // 不报小单
  if (qty < min_qty && global_params_->order_size_limit) {
    return 0;
  }

  qty =
      std::min(qty, std::min<std::int32_t>(context.max_buy_fast_vol,
                                           hp_params_->max_order_size / price));
  return qty / hp_params_->vol_tick * hp_params_->vol_tick;
}

std::int32_t Hpx::AdjustSellQty(std::int32_t qty, double price,
                                const HpOrderContext &context) {
  std::int32_t min_qty =
      std::ceil(hp_params_->min_order_size / price / hp_params_->vol_tick) *
      hp_params_->vol_tick;

  // 不报小单
  if (qty < min_qty && global_params_->order_size_limit) {
    return 0;
  }

  // 处理碎股
  if (qty >= context.max_sell_fast_vol) {
    return context.max_sell_fast_vol;
  }

  qty = std::min<std::int32_t>(qty, hp_params_->max_order_size / price);
  return qty / hp_params_->vol_tick * hp_params_->vol_tick;
}

std::int32_t Hpx::CalcHitQty(bool buy, double margin,
                             const HpOrderContext &context) {
  if (hp_params_->auto_fast == 0 || margin <= 0) {
    return 0;
  }

  std::int32_t qty = (margin / 1e-3) *
                     (hp_params_->GetFakeExecVolume() * 1e-2) /
                     global_params_->progress_sync_rate;
  if (buy) {
    qty = std::min(qty, static_cast<std::int32_t>(cur_ob_->AskVolume1 -
                                                  context.hit_buy_qty));

    return AdjustBuyQty(qty, cur_ob_->AskPrice1, context);
  } else {
    qty = std::min(qty, static_cast<std::int32_t>(cur_ob_->BidVolume1 -
                                                  context.hit_sell_qty));
    return AdjustSellQty(qty, cur_ob_->BidPrice1, context);
  }
}

std::int32_t Hpx::CalcQuoteQty(bool buy, double margin, double price,
                               const HpOrderContext &context) {
  if (hp_params_->auto_fast == 0 || margin <= 0) {
    return 0;
  }

  auto qty =
      static_cast<std::int32_t>(margin * hp_params_->GetFakeExecVolume() /
                                global_params_->progress_sync_rate);
  qty = std::min(
      static_cast<std::int32_t>(cur_ob_->AskVolume1 + cur_ob_->BidVolume1),
      qty);
  return buy ? AdjustBuyQty(qty, price, context)
             : AdjustSellQty(qty, price, context);
}

bool Hpx::CanQuoteOnTop(bool buy, double margin, double price) {
  static constexpr auto kTolerance = 1e-6;

  if (buy) {
    if (price < cur_ob_->BidPrice1 - kTolerance) {
      return false;
    }

    if (price > cur_ob_->BidPrice1 + kTolerance) {
      return true;
    }

    if (PriceCount(true, price) / cur_ob_->BidVolume1 >
        global_params_->max_proportion) {
      return false;
    }

  } else {
    if (price > cur_ob_->AskPrice1 + kTolerance) {
      return false;
    }

    if (price < cur_ob_->AskPrice1 - kTolerance) {
      return true;
    }

    if (PriceCount(false, price) / cur_ob_->AskVolume1 >
        global_params_->max_proportion) {
      return false;
    }

  }
  return true;
}

double Hpx::PriceCount(bool buy, double price) {
  std::uint64_t key = price * 1000UL;
  if (buy) {
    auto iter = pending_buy_volume_.find(key);
    if (iter == pending_buy_volume_.end()) {
      return 0;
    }
    return iter->second;
  }

  auto iter = pending_sell_volume_.find(key);
  if (iter == pending_sell_volume_.end()) {
    return 0;
  }
  return iter->second;
}

void Hpx::HandleAllPendingQuote(double cancel_buy, double cancel_sell) {
  if (hp_params_->auto_fast == 0) {
    for (auto item : pending_buy_) {
      ins_->CancelOrder(item.id);
    }
    pending_buy_.resize(0);

    for (auto item : pending_sell_) {
      ins_->CancelOrder(item.id);
    }
    pending_sell_.resize(0);
    return;
  }

  auto iter1 = pending_buy_.begin();
  auto iter2 = pending_sell_.begin();

  while (iter1 != pending_buy_.end() && iter2 != pending_sell_.end()) {
    if (iter1->index < iter2->index) {
      if (iter1->price < cancel_buy) {
        ins_->CancelOrder(iter1->id);
        iter1 = pending_buy_.erase(iter1);
      } else {
        ++iter1;
      }
    } else {
      if (iter2->price > cancel_sell) {
        ins_->CancelOrder(iter2->id);
        iter2 = pending_sell_.erase(iter2);
      } else {
        ++iter2;
      }
    }
  }

  while (iter1 != pending_buy_.end()) {
    if (iter1->price < cancel_buy) {
      ins_->CancelOrder(iter1->id);
      iter1 = pending_buy_.erase(iter1);
    } else {
      return;
    }
  }

  while (iter2 != pending_sell_.end()) {
    if (iter2->price > cancel_sell) {
      ins_->CancelOrder(iter2->id);
      iter2 = pending_sell_.erase(iter2);
    } else {
      return;
    }
  }
}

double Hpx::GetWeightedPendingQuoteCount(nlohmann::ordered_json &js) {
  double buy_res = 0;
  for (const auto &item : pending_buy_) {
    buy_res += item.price * item.qty *
               (1 - (cur_ob_->BidPrice1 - item.price) / cur_ob_->BidPrice1 /
                        cancel_offset_);
    nlohmann::ordered_json dummy;
    dummy["price"] = item.price;
    dummy["qty"] = item.qty;
    js["buy_orders"].emplace_back(std::move(dummy));
  }

  js["buy_weighted"] = buy_res;

  double sell_res = 0;

  for (const auto &item : pending_sell_) {
    sell_res += item.price * item.qty *
                (1 - (item.price - cur_ob_->AskPrice1) / cur_ob_->AskPrice1 /
                         cancel_offset_);
    nlohmann::ordered_json dummy;
    dummy["price"] = item.price;
    dummy["qty"] = item.qty;
    js["sell_orders"].emplace_back(std::move(dummy));
  }
  js["sell_weighted"] = sell_res;

  return buy_res + sell_res;
}

HpxEtf::HpxEtf(HpInstrument *ins) {
  ins_ = ins;
  hp_params_ = ins->GetParams();
  global_params_ = hp_params_->global_params;

  auto fInitOrder = [&](ivolib::ReqOrderCmdSt &order, ivolib::Direction dic) {
    ivolib::strcpyN(order.instrumentId, ins->GetInstrumentID());
    order.direction = dic;
    order.orderType = ivolib::OrderType::FAK_ORDER;
    order.ordAction = ivolib::OrderAction::ORDER_INSERT,
    ivolib::strcpyN(order.appName,
                    ivo::mercury::StrategyConfig::instance()->app_name);
    auto vec = ivolib::split<std::string>(order.appName, '_');
    auto st_name = fmt::format("{}e_{}", vec[0], vec[1]);
    ivolib::strcpyN(order.stName, st_name);
  };

  fInitOrder(hit_buy_order_, ivolib::Direction::BUY);
  fInitOrder(hit_sell_order_, ivolib::Direction::SELL);
  context_.name = &(ins->GetInstrumentID());
  context_.model = NoModel;
}

void HpxEtf::OnMarketDataUpdate(const MD &md) {
  static ivo::mercury::OnlyNeedSnapshot only_snapshot;
  if (not only_snapshot(md)) {
    return;
  }

  if (not ins_->CanInsertOrder()) {
    return;
  }

  auto cur_ob = reinterpret_cast<const OB *>(&md);
  auto sec = ivolib::SecondsFromString(cur_ob->UpdateTime);

  if (__glibc_unlikely(last_sec_ == 0)) {
    last_sec_ = sec;
    return;
  }

  if (sec - last_sec_ <= global_params_->downsample_interval) {
    return;
  }

  last_sec_ = sec;

  if (cur_ob->AskPrice1 - cur_ob->BidPrice1 - 0.001 >=
      1e-6 + global_params_->spread_tolerance) {
    return;
  }

  auto mid = MD_FAST_MID_PRICE(cur_ob);
  static auto vol_unit = 100;

  if (hp_params_->MaxBuyQtyForFast() > 0) {
    if (hp_params_->MaxBuyQtyForFast() * mid > hp_params_->min_order_size ||
        global_params_->order_size_limit == 0) {
      context_.last_ob = *cur_ob;
      context_.shortable = hp_params_->shortable;
      context_.cur_shortable = hp_params_->GetRemainingShortableForT0();
      context_.static_pos = hp_params_->static_position;
      context_.position_limit = hp_params_->GetPositionLimit();
      context_.pos_in_fast = hp_params_->pi_fast;
      context_.pos_ex_fast = hp_params_->pe_fast;
      context_.max_buy_fast_vol = hp_params_->MaxBuyQtyForFast();

      auto bv = std::min(hp_params_->MaxBuyQtyForFast(),
                         static_cast<std::int32_t>(cur_ob->AskVolume1));
      bv = bv / vol_unit * vol_unit;
      if (bv > 0) {
        auto &order = hit_buy_order_;
        order.insertTime = cur_ob->TimeStamp;
        order.origQty = bv;
        order.price = MD_AP1(cur_ob);
        auto send_qty = ins_->SendOrder(order, &context_.fast_hit_buy_id);
        if (__glibc_likely(send_qty > 0)) {
          hp_params_->WantBuyFast(send_qty);
          context_.hit_buy_qty += send_qty;
        }
      }
    }
  } else if (hp_params_->MaxSellQtyForFast() > 0) {
    if (hp_params_->MaxSellQtyForFast() * mid > hp_params_->min_order_size ||
        global_params_->order_size_limit == 0) {
      context_.last_ob = *cur_ob;
      context_.shortable = hp_params_->shortable;
      context_.cur_shortable = hp_params_->GetRemainingShortableForT0();
      context_.static_pos = hp_params_->static_position;
      context_.position_limit = hp_params_->GetPositionLimit();
      context_.pos_in_fast = hp_params_->pi_fast;
      context_.pos_ex_fast = hp_params_->pe_fast;
      context_.max_sell_fast_vol = hp_params_->MaxSellQtyForFast();

      auto sv = std::min(hp_params_->MaxSellQtyForFast(),
                         static_cast<std::int32_t>(cur_ob->BidVolume1));
      sv = sv / vol_unit * vol_unit;
      if (sv > 0) {
        auto &order = hit_sell_order_;
        order.insertTime = cur_ob->TimeStamp;
        order.origQty = sv;
        order.price = MD_BP1(cur_ob);
        auto send_qty = ins_->SendOrder(order, &context_.fast_hit_sell_id);
        if (__glibc_likely(send_qty > 0)) {
          hp_params_->WantSellFast(send_qty);
          context_.hit_sell_qty += send_qty;
        }
      }
    }
  }

  if (context_.hit_sell_qty + context_.hit_buy_qty > 0) {
    ins_->PushAndClearContext(context_);
  }
}
