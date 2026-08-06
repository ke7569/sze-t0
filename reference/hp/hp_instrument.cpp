#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <sys/cdefs.h>
#include <cmath>

#include "Enum.pb.h"
#include "IVODataCommDef.h"
#include "IVOOrderInfo.h"
#include "LogWrapper/SpdlogWrapper.h"
#include "Tsc.h"
#include "Utils/algorithm.h"
#include "boost/algorithm/string/predicate.hpp"
#include "hp_impl.h"
#include "hp_instrument.h"
#include "hpx.h"
#include "ivo/md_common.h"
#include "boost/date_time/period_formatter.hpp"
#include "boost/algorithm/string.hpp"
#include "hp_common.h"
#include "hp_prediction.h"
#include "Utils/json_utils.h"
#include "spdlog/fmt/bundled/core.h"
#include "spdlog/fmt/bundled/format.h"
#include "strategy.h"
#include "strategy_config.h"
#include "Utils/string_utils.h"
#include "Utils/trading_utils.h"
#include "strategy_utils.h"

HpInstrument::HpInstrument(const std::string& ins,
                           ManagedParaDetailProtoPtr mins,
                           InstrumentParaDetailProtoPtr cins,
                           ManagedParaDetailProtoPtr group_mins,
                           GlobalParams* global_params)
    : ivo::mercury::InstrumentBase<HpOrderContext>(ins) {
  IVOLOG_INFO("[HpInstrumentInfo] ins={},mins={},cins={},group_mins={}", ins,
              mins->ShortDebugString(), cins->ShortDebugString(),
              group_mins == nullptr ? "null" : group_mins->ShortDebugString());
  OnManagedUpdate(*(mins.get()));
  if (group_mins != nullptr) {
    OnPosGroupUpdate(*(group_mins.get()));
  }

  params_.global_params = global_params;

  IVOLOG_FATAL_IF((params_.fee_share <= 0 && not ivolib::contains(ins, "HK") &&
                          cins->contractkind() == proto::STOCK) ||
                      std::isnan(params_.fee_share),
                  "ins={},fee={}", ins, params_.fee_share);
  params_.u_price = cins->upperlimitprice();
  params_.l_price = cins->lowerlimitprice();
  params_.price_tick = cins->pricetick();

  if (cins->has_p503() && cins->p503() != 0) {
    params_.vol_tick = cins->p503();
  } else if (boost::starts_with(ins, "688")) {
    params_.vol_tick = 200;
  }

  // other order
  auto fInitOrder = [&](ivolib::ReqOrderCmdSt& order, ivolib::Direction dic, const std::string& st_name,
                        ivolib::OrderType order_type = ivolib::OrderType::FAK_ORDER) {
    strcpy(order.instrumentId, ins.data());
    order.direction = dic;
    order.orderType = order_type;
    order.ordAction = ivolib::OrderAction::ORDER_INSERT,
    strcpy(order.stName, st_name.data());
    strcpy(order.appName,
           ivo::mercury::StrategyConfig::instance()->app_name.data());
  };

  auto global_info = GlobalInfo::instance();
  fInitOrder(fast_hit_sell_order_, ivolib::Direction::SELL,
             global_info->fast_name);
  fInitOrder(fast_hit_buy_order_, ivolib::Direction::BUY,
             global_info->fast_name);
  fInitOrder(add_sub_hit_sell_order_, ivolib::Direction::SELL,
             global_info->sub_name);
  fInitOrder(add_sub_hit_buy_order_, ivolib::Direction::BUY,
             global_info->sub_name);
  fInitOrder(fast_quote_sell_order_, ivolib::Direction::SELL,
             global_info->fast_name, ivolib::OrderType::LMP_ORDER);
  fInitOrder(fast_quote_buy_order_, ivolib::Direction::BUY,
             global_info->fast_name, ivolib::OrderType::LMP_ORDER);
  fInitOrder(add_sub_quote_sell_order_, ivolib::Direction::SELL,
             global_info->fast_name, ivolib::OrderType::LMP_ORDER);
  liq_pos_key_ = fmt::format("LiqPos@{}", ins);

  params_.pi = 0;
  if (ivolib::contains(global_info->st_position_map_, ins)) {
    params_.pi = global_info->st_position_map_[ins];
    IVOLOG_INFO("[PI] ins={},pi={}", ins, params_.pi);
  }

  params_.pi_fast = 0;
  if (ivolib::contains(global_info->fast_position_map_, ins)) {
    params_.pi_fast = global_info->fast_position_map_[ins];
    IVOLOG_INFO("[FPI] ins={},fpi={}", ins, params_.pi_fast);
  }

  params_.pi_sub = 0;
  if (ivolib::contains(global_info->sub_position_map_, ins)) {
    params_.pi_sub = global_info->sub_position_map_[ins];
    IVOLOG_INFO("[SPI] ins={},spi={}", ins, params_.pi_sub);
  }

  UpdateManagedMapValueToTank({{global_info->pi, params_.pi},
                               {global_info->pi_fast, params_.pi_fast},
                               {global_info->pi_sub, params_.pi_sub}});

  hpx_.Init(this);

  if (cins->contractkind() == proto::ETF) {
    etf_ = std::make_unique<HpxEtf>(this);
  }

  init_ = true;
}

void HpInstrument::OnManagedParamUpdate(
    const proto::ManagedParaDetailProto& detail, bool delta) {
  auto prefix = ivo::mercury::ParsePrefix(detail.paraid());

  if (boost::starts_with(prefix, GlobalInfo::instance()->pos_group)) {
    OnPosGroupUpdate(detail);
  } else if (boost::starts_with(prefix, ivo::mercury::StrategyConfig::instance()
                                            ->managed_instrument_prefix)) {
    OnManagedUpdate(detail);
  } else {
    IVOLOG_ERROR("InvalidInstrument {} {}", detail.paraid(),
                 detail.ShortDebugString());
  }
}

void HpInstrument::OnManagedUpdate(
    const proto::ManagedParaDetailProto &detail) {
#define CHECK_PARA(Var)                                                        \
  if (GetPBKey(detail, #Var, params_.Var)) {                                   \
    IVOLOG_INFO("[UpdateMins] ins={},{}={}", GetInstrumentID(), #Var,       \
                params_.Var);                                                  \
  }

  CHECK_PARA(offset_base_line);
  CHECK_PARA(max_hit_vol);
  CHECK_PARA(fee_share);
  CHECK_PARA(history_amount_short);
  CHECK_PARA(history_amount);

  CHECK_PARA(max_order_size);
  CHECK_PARA(min_order_size);

  auto global_info = GlobalInfo::instance();

#define CHECK_SEQ_PARA(Var)                                                    \
  if (global_info->GetValueBySeqSlow(detail, #Var, params_.Var)) {             \
    IVOLOG_INFO("[UpdateMins] ins={},{}={}", GetInstrumentID(), #Var,       \
                params_.Var);                                                  \
  }

  CHECK_SEQ_PARA(auto_flag);
  CHECK_SEQ_PARA(auto_fast);
  CHECK_SEQ_PARA(pos_limit_factor);
  CHECK_SEQ_PARA(pe_sub);
  CHECK_SEQ_PARA(pe);
  CHECK_SEQ_PARA(pe_fast);
  CHECK_SEQ_PARA(total_execution_volume);
}

void HpInstrument::OnPosGroupUpdate(const proto::ManagedParaDetailProto& detail) {
  auto global_info = GlobalInfo::instance();

  if (GetPBKey(detail, global_info->shortable, params_.shortable)) {
    IVOLOG_INFO("[UpdatePos] ins={},shortable={}", GetInstrumentID(),
                params_.shortable);
    UpdateShortable();
  }

  if (GetPBKey(detail, global_info->static_position, params_.static_position)) {
    IVOLOG_INFO("[UpdatePos] ins={},static={}", GetInstrumentID(),
                params_.static_position);
    UpdateStaticPosition();
  }

  if (GetPBKey(detail, global_info->static_short_position,
               params_.static_short_position)) {
    IVOLOG_INFO("[UpdatePos] ins={},static_short_position={}",
                GetInstrumentID(), params_.static_short_position);
  }

  if (GetPBKey(detail, global_info->short_sell_position,
               params_.short_sell_position)) {
    IVOLOG_INFO("[UpdatePos] ins={},short_sell_pos={}", GetInstrumentID(),
                params_.short_sell_position);
  }

  if (GetPBKey(detail, global_info->short_total_position,
               params_.short_total_position)) {
    IVOLOG_INFO("[UpdatePos] ins={},hk_short_pos={}", GetInstrumentID(),
                params_.short_total_position);
  }
}

bool HpInstrument::HpHitBuy(HpOrderContext& context) {
  std::int32_t hit_buy_qty = 0;
  auto vol_unit = params_.vol_tick;
  auto cur_ob = &(context.last_ob);

  auto theo_price = theo_.hit_theo0_bp;
  auto infer_price = MD_AP1(cur_ob);
  double buy_margin = theo_price / infer_price - 1;

  if (buy_margin > 0) {
    if (theo_.bias_factor >= 1e-6) {
      hit_buy_qty =
          std::min(static_cast<std::int32_t>(buy_margin / theo_.unitbias), static_cast<std::int32_t>(MD_AV1(cur_ob)));
    } else {
      hit_buy_qty = MD_AV1(cur_ob);
    }
  }

  hit_buy_qty = std::min(hit_buy_qty, context.max_buy_vol);
  hit_buy_qty = hit_buy_qty / vol_unit * vol_unit;
  context.b_margin_div_ub = buy_margin / theo_.unitbias;

  if (hit_buy_qty <= 0) {
    context.no_hit_buy_des = &(kSendQtyLessThenZero);
    return false;
  }

  context.hit_buy_qty += HitBuy(hit_buy_qty, infer_price, cur_ob->TimeStamp, &(context.hit_buy_id));
  params_.WantBuy(context.hit_buy_qty);
  if (__glibc_likely(context.hit_buy_qty > 0)) {
    return true;
  }
  context.no_hit_buy_des = &kGunCloseDes;
  return false;
}

bool HpInstrument::HpHitSell(HpOrderContext& context) {
  std::int32_t hit_sell_qty = 0;
  std::int32_t vol_unit = params_.vol_tick;

  auto cur_ob = &(context.last_ob);
  auto infer_price = MD_BP1(cur_ob);
  auto theo_price = theo_.hit_theo0_sp;

  double sell_margin = infer_price / theo_price - 1;
  if (sell_margin > 0) {
    if (theo_.bias_factor >= 1e-6) {
      hit_sell_qty = std::min(static_cast<std::int32_t>(sell_margin / theo_.unitbias),
                              static_cast<std::int32_t>(MD_BV1(cur_ob)));
    } else {
      hit_sell_qty = MD_BV1(cur_ob);
    }
  }

  if (hit_sell_qty >= context.max_sell_vol) {
    hit_sell_qty = context.max_sell_vol;
  } else {
    hit_sell_qty = hit_sell_qty / vol_unit * vol_unit;
  }

  context.s_margin_div_ub = sell_margin / theo_.unitbias;

  if (hit_sell_qty <= 0) {
    context.no_hit_sell_des = &kSendQtyLessThenZero;
    return false;
  }

  context.hit_sell_qty += HitSell(hit_sell_qty, infer_price, cur_ob->TimeStamp,
                                 &(context.hit_sell_id));

  params_.WantSell(context.hit_sell_qty);
  if (__glibc_likely(context.hit_sell_qty > 0)) {
    return true;
  }
  context.no_hit_sell_des = &kGunCloseDes;
  return false;
}

bool HpInstrument::HpQuoteBuy(HpOrderContext& context) {
  std::int32_t quote_v = 0;
  auto vol_unit = params_.vol_tick;
  auto cur_ob = &(context.last_ob);
  auto quote_buy_price = MD_BP1(cur_ob) + params_.price_tick;

  if (quote_buy_price <= theo_.quote_theo0_bp) {
    quote_v = MD_AV1(cur_ob);
    quote_v = std::min(quote_v, params_.MaxBuyQtyForT0());
    quote_v = quote_v / vol_unit * vol_unit;
  }

  if (quote_v <= 0) {
    return false;
  }

  auto send_qty = QuoteBuy(quote_v, quote_buy_price, cur_ob->TimeStamp,
                           &quote_buy_.id);
  if (__glibc_likely(send_qty > 0)) {
    context.quote_buy_id = quote_buy_.id;
    quote_buy_.price = quote_buy_price;
    params_.WantBuy(send_qty);
  }

  return send_qty > 0;
}

bool HpInstrument::HpQuoteSell(HpOrderContext& context) {
  double quote_sell_price = MD_AP1(&(context.last_ob)) - params_.price_tick;
  std::int32_t quote_v = 0;
  auto vol_unit = params_.vol_tick;
  auto cur_ob = &(context.last_ob);

  if (quote_sell_price >= theo_.quote_theo0_sp) {
    quote_v = MD_BV1(cur_ob);
    quote_v = std::min(quote_v, params_.MaxSellQtyForT0());
    std::int32_t old_v = quote_v;
    quote_v = quote_v / vol_unit * vol_unit;
    if (quote_v == 0 && old_v > 0) {
      quote_v = old_v;
    }
  }

  if (quote_v <= 0) {
    return false;
  }

  auto send_qty =
      QuoteSell(quote_v, quote_sell_price, cur_ob->TimeStamp, &quote_sell_.id);
  if (__glibc_likely(send_qty > 0)) {
    context.quote_sell_id = quote_sell_.id;
    quote_sell_.price = quote_sell_price;
    params_.WantSell(send_qty);
  }

  return send_qty > 0;
}

std::string HpInstrument::GetShowTheoMessage() {
  auto config = ivo::mercury::StrategyConfig::instance();
  return fmt::format(
      "app={};theo_progress={};hb_theo={};hs_theo={};"
      "pos={};pi={};bias={};b_offset={};s_offset={}"
      ";prediction={};shortable={};"
      "fast_in_pos={};fast_ex_pos={};"
      "static_pos={};to_execute={};to_scale={}"
      "vl_pos={};vs_pos={};static_sell_pos={};ts={}",
      config->app_name, hpx_.GetTheoProgress(), theo_.hit_theo0_bp, theo_.hit_theo0_sp,
      params_.pi + params_.pe, params_.pi, theo_.bias, theo_.b_offset,
      theo_.s_offset, prediction_, params_.shortable, params_.pi_fast,
      params_.pe_fast, params_.static_position,
      -(params_.pi_fast + params_.pe_fast), -(params_.pi_sub + params_.pe_sub),
      params_.vl_pos, params_.vs_pos, params_.static_short_position,
      update_time_);
}

std::string HpInstrument::ToString() {
  nlohmann::ordered_json js = params_;
  js["ins"] = GetInstrumentID();
  js["liq"] = liq_pos_key_;
  return js.dump();
}

void HpInstrument::HandleOrderRtn(const ivolib::OrderInfoSt& rtn,
                                  int32_t delta_pos, int32_t delta_virtual_long,
                                  int32_t delta_virtual_short, bool order_end) {
  auto global_info = GlobalInfo::instance();

  if (strncmp(rtn.stName, global_info->t0_name.data(),
              global_info->t0_name.size()) == 0) {
    params_.pi += delta_pos;
    params_.vl_pos += delta_virtual_long;
    params_.vs_pos += delta_virtual_short;
    if (delta_pos != 0) {
      UpdatePI();
    }
  } else if (strncmp(rtn.stName, global_info->sub_name.data(),
                     global_info->sub_name.size()) == 0) {
    params_.pi_sub += delta_pos;
    params_.add_vl_pos += delta_virtual_long;
    params_.add_vs_pos += delta_virtual_short;
    if (delta_pos != 0) {
      UpdateSPI();
    }
  } else {
    params_.pi_fast += delta_pos;
    params_.fast_vl_pos += delta_virtual_long;
    params_.fast_vs_pos += delta_virtual_short;
    if (delta_pos != 0) {
      UpdateFPI();
    }
    hpx_.HandleOrderRtn(rtn, delta_virtual_long, delta_virtual_short);
  }

  if (order_end && rtn.orderType == ivolib::LMP_ORDER) {
    if (quote_buy_.id == rtn.orderRefId) {
      quote_buy_.Clear();
    } else if (quote_sell_.id == rtn.orderRefId) {
      quote_sell_.Clear();
    }
  }
}

void HpInstrument::AfterPrediction(HpOrderContext &context) {
  auto t0 = DoTheo(context);
  auto cur_ob = &(context.last_ob);
  if (__glibc_unlikely(MD_BV5(cur_ob) == 0 || MD_AV5(cur_ob) == 0)) {
    context.description = &(kInvalidVolume);
    return;
  }

  if (not __glibc_unlikely(CanTradeSelf())) {
    context.description = &(kGunCloseDes);
    return;
  }

  context.shortable = params_.shortable;
  context.cur_shortable = params_.GetRemainingShortableForT0();
  context.static_pos = params_.static_position;
  context.position_limit = params_.GetPositionLimit();
  context.pi = params_.pi;
  context.pe = params_.pe;
  context.pos_in_fast = params_.pi_fast;
  context.pos_ex_fast = params_.pe_fast;
  context.pos_in_addsub = params_.pi_sub;
  context.pos_ex_addsub = params_.pe_sub;
  context.static_short_pos = params_.static_short_position;
  context.max_buy_vol = params_.MaxBuyQtyForT0();
  context.max_sell_vol = params_.MaxSellQtyForT0();
  context.max_buy_fast_vol = params_.MaxBuyQtyForFast();
  context.max_sell_fast_vol = params_.MaxSellQtyForFast();
  context.max_buy_sub_vol = params_.MaxBuyQtyForSub();
  context.max_sell_sub_vol = params_.MaxSellQtyForSub();
  context.hit_buy_qty = context.hit_sell_qty = 0;

#ifndef SHORT_SELL
#ifndef HK
  if (t0) {
    if (GlobalInfo::instance()->exchange_sh) {
      auto ex_time =
          ivolib::MillSecondsFromString(cur_ob->UpdateTime, cur_ob->MillSec);
      if (HpManager::instance()->local_time - ex_time <=
          250) { //交易所时间delay不超过250ms才报单
        HandleT0(context);
      }
    } else {
      HandleT0(context);
    }
  }
#endif
#endif

  HandleFastAndSub(context);
}

bool HpInstrument::DoTheo(HpOrderContext& context) {
  auto g_params = params_.global_params;
  auto global_info = GlobalInfo::instance();

  auto bf_multiplier = global_info->exchange_sh ? g_params->bf_multiplier_sh
                                                : g_params->bf_multiplier_sz;
  auto position_base_line =
      std::clamp(params_.history_amount_short / 1e3, 2e5, 1e6);
  auto bias_factor = bf_multiplier * context.last_ob.LastPrice / position_base_line;
  auto t0_pos = params_.pi + params_.pe;
  double bias = 0.0;
  if (params_.static_position == 0) {
    bias = t0_pos > 0 ? 2 : -2;
  } else {
    bias = std::clamp(t0_pos * bias_factor, -2.0, 2.0);
  }
  auto skew = (global_info->exchange_sh ? g_params->skew_base_line_sh
                                        : g_params->skew_base_line_sz) *
              g_params->offset_multiplier;
            
  theo_.bias_factor = bias_factor;
  theo_.bias = bias;
  theo_.skew = skew;
  auto x = global_info->exchange_sh ? g_params->offset_multiplier_sh
                                    : g_params->offset_multiplier_sz;
  auto offset = params_.offset_base_line * g_params->offset_multiplier * x;
  auto extern_multiplier = 1.0;
  bool t0 = true;

  prediction_ = context.prediction;
  extern_multiplier = g_params->hp_offset_multiplier;

  context.prediction = prediction_;

  theo_.offset = offset * extern_multiplier;
  theo_.theo0 = (1 + prediction_ / 1000) *
                ((MD_AP1(&(context.last_ob)) + MD_BP1(&(context.last_ob))) / 2);
  theo_.unitbias = theo_.offset * bias_factor;
  theo_.q_offset = theo_.offset * (global_info->exchange_sh
                                       ? g_params->quote_offset_multiplier_sh
                                       : g_params->quote_offset_multiplier_sz);
  theo_.b_offset = (1 - bias * theo_.offset - theo_.offset - skew);
  theo_.s_offset = (1 - bias * theo_.offset + theo_.offset - skew);
  theo_.b_q_offset = (1 - bias * theo_.offset - theo_.q_offset - skew);
  theo_.s_q_offset = (1 - bias * theo_.offset + theo_.q_offset - skew);
  theo_.hit_theo0_bp = theo_.b_offset * theo_.theo0;
  theo_.hit_theo0_sp = theo_.s_offset * theo_.theo0;
  theo_.quote_theo0_bp = theo_.b_q_offset * theo_.theo0;
  theo_.quote_theo0_sp = theo_.s_q_offset * theo_.theo0;
  ivolib::strcpyN(update_time_, context.last_ob.UpdateTime);

  context.theo = theo_;
  return t0;
}

void HpInstrument::CancelBuy(HpOrderContext& context) {
  if (quote_buy_.id == 0) {
    return;
  }

  if (quote_buy_.price > theo_.quote_theo0_bp ||
      quote_buy_.price < MD_BP1(&(context.last_ob)) - 1e-6) {

    CancelOrder(quote_buy_.id);
    context.cancel_buy_id = quote_buy_.id;
    quote_buy_.Clear();
  }
}

void HpInstrument::CancelSell(HpOrderContext& context) {
  if (quote_sell_.id == 0) {
    return;
  }
  if (quote_sell_.price < theo_.quote_theo0_sp ||
      quote_sell_.price > MD_AP1(&(context.last_ob)) + 1e-6) {
    CancelOrder(quote_sell_.id);
    context.cancel_sell_id = quote_sell_.id;
    quote_sell_.Clear();
  }
}

void HpInstrument::CancelLMPSell(HpOrderContext& context) {
  if (quote_sell_.id == 0) {
    return;
  }
  if (quote_sell_.price > MD_AP1(&(context.last_ob)) + 1e-6) {
    CancelOrder(quote_sell_.id);
    context.cancel_sell_id = quote_sell_.id;
    quote_sell_.Clear();
  }
}

void HpInstrument::HandleT0(HpOrderContext& context) {
  CancelBuy(context);
  CancelSell(context);

  if (params_.CanBuy()) {
    HpHitBuy(context);
  } else {
    context.no_hit_buy_des = &kBanByVPos;
  }

  if (params_.CanSell()) {
    HpHitSell(context);
  } else {
    context.no_hit_sell_des = &kBanByVPos;
  }

  // quote
  if (context.hit_buy_qty == 0 && params_.CanBuy() && params_.vl_pos == 0) {
    HpQuoteBuy(context);
  }

  if (context.hit_sell_qty == 0 && params_.CanSell() && params_.vs_pos == 0) {
    HpQuoteSell(context);
  }
}

void HpInstrument::HandleFastAndSub(HpOrderContext& context) {
  auto vol_unit = params_.vol_tick;
  auto cur_ob = &(context.last_ob);
  double mid_price = (MD_AP1(cur_ob) + MD_BP1(cur_ob)) / 2;
  auto theo_price = theo_.theo0;

  context.mid_p = mid_price;
  context.execution_tolerance = params_.global_params->execution_tolerance;
  double et = std::max(params_.global_params->execution_tolerance,
                       params_.price_tick / mid_price - 0.002);
  context.ot = -et / 2;

  hpx_.AfterPrediction(context);

  // sub
  {
    if (params_.CanBuy()) {
      if (theo_price * (1 - context.ot) >= MD_AP1(cur_ob)) {
        auto bv = context.max_buy_sub_vol;
        if (bv >= vol_unit) {
          bv = std::min(static_cast<std::int32_t>(MD_AV1(cur_ob) - context.hit_buy_qty), bv);
          bv = bv / vol_unit * vol_unit;

          if (bv > 0) {
            auto& order = add_sub_hit_buy_order_;
            order.insertTime = cur_ob->TimeStamp;
            order.origQty = bv;
            order.price = MD_AP1(cur_ob);
            auto send_qty = SendOrder(order, &context.addsub_hit_buy_id);

            if (__glibc_likely(send_qty > 0)) {
              params_.WantBuyAdd(send_qty);
              context.hit_buy_qty += send_qty;
            }
          }
        } else {
          context.no_sub_hit_buy_des = &kMaxQtyLessThenZero;
        }
      } else {
        context.no_sub_hit_buy_des = &kBanByPrice;
      }
    } else {
      context.no_sub_hit_buy_des = &kBanByVPos;
    }

    if (params_.CanSell()) {
      if (theo_price * (1 + context.ot) <= MD_BP1(cur_ob)) {
        auto sv = context.max_sell_sub_vol;
        if (sv > 0) {
          sv = std::min(static_cast<std::int32_t>(MD_BV1(cur_ob) - context.hit_sell_qty), sv);
          if (sv >= context.max_sell_sub_vol) {
            sv = context.max_sell_sub_vol;
          } else {
            sv = sv / vol_unit * vol_unit;
          }

          auto hk_short = params_.HKShort();
          if (sv > 0) {
            ivolib::ReqOrderCmdSt* order = nullptr;
            if (not hk_short) {
              order = &add_sub_hit_sell_order_;
              order->price = MD_BP1(cur_ob);
            } else {
              order = &add_sub_quote_sell_order_;
              order->price = MD_AP1(cur_ob);
              order->riskLevel = ivolib::SHORT_SELL_FLAG;
              CancelLMPSell(context);
            }
            order->insertTime = cur_ob->TimeStamp;
            order->origQty = sv;
            auto send_qty = SendOrder(*order, &context.addsub_hit_sell_id);
            order->riskLevel = 0;
            if (__glibc_likely(send_qty > 0)) {
              params_.WantSellAdd(send_qty);
              context.hit_sell_qty += send_qty;
              if (hk_short) {
                quote_sell_.id = context.addsub_hit_sell_id;
                quote_sell_.price = order->price;
              }
            }
          }
        } else {
          context.no_sub_hit_sell_des = &kMaxQtyLessThenZero;
        }
      } else {
        context.no_sub_hit_sell_des = &kBanByPrice;
      }
    } else {
      context.no_sub_hit_sell_des = &kBanByVPos;
    }
  }
}

void HpInstrument::HandleCallOrder() {
  auto ob = &(call_auction_ob_);
  auto to_execute = -(params_.pi_fast + params_.pe_fast);

  if (MD_IDX_INVALID(ob, 1) || to_execute == 0) {
    return;
  }

  static auto kLiqRes = 0.3;
  auto vol = (MD_AV1(ob) + MD_BV1(ob)) / 2;
  auto order_qty = std::min(static_cast<int>(vol * kLiqRes), std::abs(to_execute));
  order_qty = order_qty / params_.vol_tick * params_.vol_tick;

  if (order_qty < 0) {
    return;
  }

  auto price = MD_MID_PRICE1(ob);
  auto execution_tolerance =
      GlobalInfo::instance()->exchange_sh
          ? params_.global_params->execution_tolerance * 2
          : params_.global_params->execution_tolerance;

  HpOrderContext context(&(GetInstrumentID()), NoModel);
  context.last_ob = *(ob);
  context.execution_tolerance = params_.global_params->execution_tolerance;
  context.hit_buy_qty = context.hit_sell_qty = 0;

  static auto fRoundPrice = [](double p) -> double {
    return std::round(p * 100.0) / 100.0;
  };

  // buy
  if (to_execute > 0) {
    auto &order = fast_quote_buy_order_;
    order.price = fRoundPrice(price * (1 + execution_tolerance * 1.25));
    order.insertTime = ivolib::getTscTick();
    order.origQty = order_qty;

    context.max_buy_fast_vol = to_execute;
    auto send_qty = SendOrder(order, &quote_buy_.id);

    if (__glibc_likely(send_qty > 0)) {
      context.fast_hit_buy_id = quote_buy_.id;
      context.hit_buy_qty += send_qty;
      params_.WantBuyFast(send_qty);
    }
  } else {
    auto &order = fast_quote_sell_order_;
    order.price = fRoundPrice(price * (1 - execution_tolerance));
    order.insertTime = ivolib::getTscTick();
    order.origQty = order_qty;

    context.max_sell_fast_vol = -to_execute;
    auto send_qty = SendOrder(order, &quote_sell_.id);

    if (__glibc_likely(send_qty > 0)) {
      context.fast_hit_sell_id = quote_sell_.id;
      context.hit_sell_qty += send_qty;
      params_.WantSellFast(send_qty);
    }
  }

  PushContext(context);
}

void HpInstrument::CancelAll() {
  auto& order = fast_quote_buy_order_;
  order.insertTime = ivolib::getTscTick();
  order.ordAction = ivolib::OrderAction::ORDER_CXL_BY_PREID;

  if (quote_buy_.id != 0) {
    order.orderRefId = quote_buy_.id;
    quote_buy_.Clear();
    SendOrder(order);
  }

  if (quote_sell_.id != 0) {
    order.orderRefId = quote_sell_.id;
    quote_sell_.Clear();
    SendOrder(order);
  }

  order.ordAction = ivolib::OrderAction::ORDER_INSERT;
}
