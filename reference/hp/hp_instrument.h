#pragma once

#include "TickMarketData.h"
#include "mercury.h"
#include "hp_common.h"
#include "trading_utils.h"
#include "hpx.h"
#include <sys/cdefs.h>

class HpInstrument : public ivo::mercury::InstrumentBase<HpOrderContext> {
 public:
  HpInstrument(const std::string& ins, ManagedParaDetailProtoPtr mins,
               InstrumentParaDetailProtoPtr cins,
               ManagedParaDetailProtoPtr group_mins,
               GlobalParams* global_params);
  virtual ~HpInstrument() {}

  HpxEtf *GetEtf() { return etf_.get(); }
  HpParams *GetParams() { return &params_; }
  bool CanTradeSelf() { return params_.auto_flag != 0 && CanInsertOrder(); }
  bool CanPrediction() { return params_.history_amount != 0; }
  void OnManagedParamUpdate(const proto::ManagedParaDetailProto& detail,
                            bool delta);
  void HandleOrderRtn(const ivolib::OrderInfoSt& rtn, int32_t delta_pos,
                      int32_t delta_virtual_long, int32_t delta_virtual_short,
                      bool order_end);
  std::string GetShowTheoMessage();
  std::string ToString();
  void PushAndClearContext(HpOrderContext &context) {
    PushContext(context);
    memset(&context.description, 0, kResetSize);
  }

  void OnMDMatchError() {
    IVOLOG_INFO("[OnMatchError] ins={}", GetInstrumentID());
    UpdateManagedMapValueToTank(GlobalInfo::instance()->pos_limit_factor, 0);
    params_.pos_limit_factor = 0;
  }

  void OutputTheo() {
    if (__glibc_unlikely(theo_.theo0 == 0)) {
      return;
    }
    UpdateManagedMapValueToTank("theo", theo_.theo0, liq_pos_key_);
  }

  void HandleCallMD(const MD &md) {
    if (AnyMDType(md, ivolib::SNAPSHOT_EXCHANGE,
                  ivolib::SZ_OPEN_AUCTION_SNAPSHOT_MATCH)) {
      call_auction_ob_ = *(reinterpret_cast<const OB *>(&md));
    }
  }

  void CancelAll();
  void HandleCallOrder();
  Hpx &GetHpx() { return hpx_; }

protected:
  void AfterPrediction(HpOrderContext& context);
  void HandleFastAndSub(HpOrderContext& context);

private:
  bool DoTheo(HpOrderContext& context);
  void HandleT0(HpOrderContext& context);

 private:
  void OnManagedUpdate(const proto::ManagedParaDetailProto& detail);
  void OnPosGroupUpdate(const proto::ManagedParaDetailProto& detail);
  bool HpHitBuy(HpOrderContext& context);
  bool HpHitSell(HpOrderContext& context);
  bool HpQuoteBuy(HpOrderContext& context);
  bool HpQuoteSell(HpOrderContext& context);
  void CancelBuy(HpOrderContext& context);
  void CancelSell(HpOrderContext& context);
  void CancelLMPSell(HpOrderContext& context);

#define UPDATE_PARAM(Key)                                                      \
  UpdateManagedMapValueToTank(GlobalInfo::instance()->Key, params_.Key);

  void UpdatePI() { UPDATE_PARAM(pi); }

  void UpdateFPI() { UPDATE_PARAM(pi_fast); }

  void UpdateSPI() { UPDATE_PARAM(pi_sub); }

  void UpdateShortable() { UPDATE_PARAM(shortable); }

  void UpdateStaticPosition() { UPDATE_PARAM(static_position); }

protected:
  HpParams params_;
  HpTheo theo_;

 private:
  bool init_ = false;
  std::unique_ptr<HpxEtf> etf_{nullptr};
  std::string liq_pos_key_;
  float prediction_;
  ivolib::ReqOrderCmdSt fast_hit_sell_order_;
  ivolib::ReqOrderCmdSt fast_hit_buy_order_;
  ivolib::ReqOrderCmdSt add_sub_hit_sell_order_;
  ivolib::ReqOrderCmdSt add_sub_hit_buy_order_;
  ivolib::ReqOrderCmdSt fast_quote_sell_order_;
  ivolib::ReqOrderCmdSt fast_quote_buy_order_;
  ivolib::ReqOrderCmdSt add_sub_quote_sell_order_;
  PendingQuote quote_buy_;
  PendingQuote quote_sell_;
  OB call_auction_ob_;
  ivolib::UpDateTimeType update_time_;
  static inline const auto kDescriptionOffset =
      reinterpret_cast<std::uintptr_t>(
          &(reinterpret_cast<HpOrderContext *>(0))->description);
  static inline const auto kResetSize =
      sizeof(HpOrderContext) - kDescriptionOffset;
  Hpx hpx_;
};
