#include "hp_common.h"
#include "Tsc.h"
#include "magic_enum.hpp"
#include "nlohmann/json_fwd.hpp"
#include <sys/cdefs.h>

size_t HpFactor::factor_size = offsetof(HpFactor, last_buy_trade_price) / sizeof(float);

void to_json(nlohmann::ordered_json& j, const HpOrderContext& context) {
  j["instrument_id"] = *(context.name);
  j["model"] = magic_enum::enum_name(context.model);

#define ADD_STR_KEY(Key)                                                       \
  if (context.Key != nullptr) {                                                \
    j[#Key] = *(context.Key);                                                  \
  }

  ADD_STR_KEY(description);
  ADD_STR_KEY(no_hit_buy_des);
  ADD_STR_KEY(no_hit_sell_des);
  ADD_STR_KEY(no_fast_hit_buy_des);
  ADD_STR_KEY(no_fast_hit_sell_des);
  ADD_STR_KEY(no_sub_hit_buy_des);
  ADD_STR_KEY(no_sub_hit_sell_des);
#undef ADD_STR_KEY
#define ADD_HP_KEY(Key) j[#Key] = context.Key;

  if (context.model != NoModel && context.factor.factor_type != kFactorTypeNone) {
    ADD_HP_KEY(factor);
  }

  ADD_HP_KEY(last_ob);
  ADD_HP_KEY(app_seq);
  ADD_HP_KEY(prediction_raw);
  ADD_HP_KEY(prediction);
  ADD_HP_KEY(theo);
  ADD_HP_KEY(shortable);
  ADD_HP_KEY(cur_shortable);
  ADD_HP_KEY(pi);
  ADD_HP_KEY(pe);
  ADD_HP_KEY(pos_in_fast);
  ADD_HP_KEY(pos_ex_fast);
  ADD_HP_KEY(pos_in_addsub);
  ADD_HP_KEY(pos_ex_addsub);
  ADD_HP_KEY(position_limit);
  ADD_HP_KEY(static_pos);
  ADD_HP_KEY(static_short_pos);
  ADD_HP_KEY(hit_buy_id);
  ADD_HP_KEY(hit_sell_id);
  ADD_HP_KEY(quote_buy_id);
  ADD_HP_KEY(quote_sell_id);
  ADD_HP_KEY(fast_hit_buy_id);
  ADD_HP_KEY(fast_hit_sell_id);
  ADD_HP_KEY(addsub_hit_buy_id);
  ADD_HP_KEY(addsub_hit_sell_id);
  ADD_HP_KEY(cancel_buy_id);
  ADD_HP_KEY(cancel_sell_id);
  ADD_HP_KEY(hit_buy_qty);
  ADD_HP_KEY(b_margin_div_ub);
  ADD_HP_KEY(max_buy_vol);
  ADD_HP_KEY(max_buy_fast_vol);
  ADD_HP_KEY(max_buy_sub_vol);
  ADD_HP_KEY(hit_sell_qty);
  ADD_HP_KEY(s_margin_div_ub);
  ADD_HP_KEY(max_sell_vol);
  ADD_HP_KEY(max_sell_fast_vol);
  ADD_HP_KEY(max_sell_sub_vol);
  ADD_HP_KEY(mid_p);
  ADD_HP_KEY(execution_tolerance);
  ADD_HP_KEY(ot);
  ADD_HP_KEY(fast_quote_offset);
  ADD_HP_KEY(fast_buy_offset);
  ADD_HP_KEY(fast_sell_offset);
  ADD_HP_KEY(fast_quote_buy_offset);
  ADD_HP_KEY(fast_quote_sell_offset);
  ADD_HP_KEY(fast_quote_buy_price);
  ADD_HP_KEY(fast_quote_sell_price);


#undef ADD_HP_KEY
}

void to_json(nlohmann::ordered_json& j, const HpFactor& factor) {
#define ADD_FACTOR_KEY(Key) j[#Key] = factor.Key

  static auto fAnyOne = [](auto&& factor_type, auto&&... type) -> bool {
    return ((factor_type == type) || ...);
  };

  if (fAnyOne(factor.factor_type, FactorType::kFactorType28,
              FactorType::kFactorType40,
              FactorType::KFactorTypeFullob)) {
    ADD_FACTOR_KEY(order_pf);
    ADD_FACTOR_KEY(order_nf);
    ADD_FACTOR_KEY(order_mf);
    ADD_FACTOR_KEY(trade_pcf);
    ADD_FACTOR_KEY(trade_ncf);
    ADD_FACTOR_KEY(trade_pt);
    ADD_FACTOR_KEY(trade_nt);
  }

  ADD_FACTOR_KEY(hermes);
  ADD_FACTOR_KEY(tr_sqrt_positive);
  ADD_FACTOR_KEY(percent_spread);
  ADD_FACTOR_KEY(percent_mid_rtn);
  ADD_FACTOR_KEY(fee_on_tick);
  ADD_FACTOR_KEY(bid_vol_chg_ratio);
  ADD_FACTOR_KEY(ask_vol_chg_ratio);
  ADD_FACTOR_KEY(pct_weighted_rtn1);
  ADD_FACTOR_KEY(pct_weighted_rtn2);
  ADD_FACTOR_KEY(pct_weighted_rtn3);
  ADD_FACTOR_KEY(pct_weighted_rtn4);
  ADD_FACTOR_KEY(pct_weighted_rtn5);
  ADD_FACTOR_KEY(pct_weighted_ask);
  ADD_FACTOR_KEY(pct_weighted_bid);
  ADD_FACTOR_KEY(pct_weighted_ask_rtn);
  ADD_FACTOR_KEY(pct_weighted_bid_rtn);
  ADD_FACTOR_KEY(weighted_vol_diff_ratio);
  ADD_FACTOR_KEY(vol_diff_ratio);
  ADD_FACTOR_KEY(pct_turnover);
  ADD_FACTOR_KEY(pct_liquidity_ask);
  ADD_FACTOR_KEY(pct_liquidity_bid);

  if (fAnyOne(factor.factor_type, FactorType::kFactorType40)) {
    ADD_FACTOR_KEY(last_buy_trade_price);
    ADD_FACTOR_KEY(last_sell_trade_price);
    ADD_FACTOR_KEY(buy_trade_l1);
    ADD_FACTOR_KEY(buy_trade_l3);
    ADD_FACTOR_KEY(sell_trade_l1);
    ADD_FACTOR_KEY(sell_trade_l3);
    ADD_FACTOR_KEY(leading_buy_rtn);
    ADD_FACTOR_KEY(leading_sell_rtn);
    ADD_FACTOR_KEY(leading_buy_amount);
    ADD_FACTOR_KEY(leading_sell_amount);
    ADD_FACTOR_KEY(bias_from_avg);
    ADD_FACTOR_KEY(bias_from_open);
  }

  if (fAnyOne(factor.factor_type, FactorType::KFactorTypeFullob)) {
    ADD_FACTOR_KEY(PositiveFillRate);
    ADD_FACTOR_KEY(NegativeFillRate);
    ADD_FACTOR_KEY(OrderFlowImbalance);
    ADD_FACTOR_KEY(CFRImbalance);
    ADD_FACTOR_KEY(FixDisImbalancePct1);
    ADD_FACTOR_KEY(FixDisImbalancePct2);
    ADD_FACTOR_KEY(WeightedFixDisImbalancePct1);
    ADD_FACTOR_KEY(WeightedFixDisImbalancePct2);
    ADD_FACTOR_KEY(AvgSizeImbalance);
    ADD_FACTOR_KEY(AvgSizeImbalanceLevel1);
    ADD_FACTOR_KEY(AvgSizeImbalanceLevel5);
    ADD_FACTOR_KEY(OrderCountImbalance);
    ADD_FACTOR_KEY(OrderCountImbalanceLevel1);
    ADD_FACTOR_KEY(OrderCountImbalanceLevel5);
    ADD_FACTOR_KEY(OrderLifeImbalance);
    ADD_FACTOR_KEY(OrderLifeImbalanceLevel1);
    ADD_FACTOR_KEY(OrderLifeImbalanceLevel5);
    ADD_FACTOR_KEY(MaxBidDistance);
    ADD_FACTOR_KEY(MaxAskDistance);
    ADD_FACTOR_KEY(MaxVolDistanceImbalance);
    ADD_FACTOR_KEY(YoungOrderbookImbalance);
    ADD_FACTOR_KEY(FixDistHermes);
  }
}

void GlobalParams::Update(const proto::ManagedParaDetailProto& detail) {
  auto global_info = GlobalInfo::instance();

#define CHECK_GLOBAL_PARA(Var)                                                                                         \
  if (GetPBKey(detail, #Var, Var)) {                                                                                   \
    IVOLOG_INFO("[UpdateGlobal] {}={}", #Var, Var);                                                                 \
  }

  CHECK_GLOBAL_PARA(offset_multiplier);

  CHECK_GLOBAL_PARA(can_quote_ratio);

  CHECK_GLOBAL_PARA(liquidity_window);
  CHECK_GLOBAL_PARA(max_proportion);

  CHECK_GLOBAL_PARA(quote_base_line);
  CHECK_GLOBAL_PARA(hp_offset_multiplier);
  CHECK_GLOBAL_PARA(hpb_offset_multiplier);
  CHECK_GLOBAL_PARA(cross_offset_multiplier);
  CHECK_GLOBAL_PARA(execution_tolerance);

#define CHECK_GLOBAL_SEQ_PARA(Var)                                                                                     \
  if (global_info->GetValueBySeqSlow(detail, #Var, Var)) {                                                             \
    IVOLOG_INFO("[UpdateGlobal] {}={}", #Var, Var);                                                                 \
  }

  CHECK_GLOBAL_SEQ_PARA(skew_base_line_sz);
  CHECK_GLOBAL_SEQ_PARA(skew_base_line_sh);
  CHECK_GLOBAL_SEQ_PARA(static_position_ratio);
  CHECK_GLOBAL_SEQ_PARA(offset_multiplier_sh);
  CHECK_GLOBAL_SEQ_PARA(bf_multiplier_sz);
  CHECK_GLOBAL_SEQ_PARA(offset_multiplier_sz);
  CHECK_GLOBAL_SEQ_PARA(bf_multiplier_sh);
  CHECK_GLOBAL_SEQ_PARA(quote_offset_multiplier_sz);
  CHECK_GLOBAL_SEQ_PARA(quote_offset_multiplier_sh);

  CHECK_GLOBAL_SEQ_PARA(can_quote);
  CHECK_GLOBAL_SEQ_PARA(quote_count);
  CHECK_GLOBAL_SEQ_PARA(max_quote_num);
  CHECK_GLOBAL_SEQ_PARA(downsample_interval);
  CHECK_GLOBAL_SEQ_PARA(planned_execution_time);
  CHECK_GLOBAL_SEQ_PARA(spread_tolerance);
  CHECK_GLOBAL_SEQ_PARA(cancel_offset);
  CHECK_GLOBAL_SEQ_PARA(cancel_offset_multiplier);
  CHECK_GLOBAL_SEQ_PARA(quote_offset);
  CHECK_GLOBAL_SEQ_PARA(max_quote_num_per_account);
  CHECK_GLOBAL_SEQ_PARA(progress_sync_rate);
  CHECK_GLOBAL_SEQ_PARA(quote_bias_factor);

  CHECK_GLOBAL_SEQ_PARA(order_size_limit);

  if (global_info->GetValueBySeqSlow(detail, "execution_tolerance",
                                     execution_tolerance_x)) {
    IVOLOG_INFO("[UpdateGlobal] execution_tolerance_x={}",
                execution_tolerance_x);
  }
}
