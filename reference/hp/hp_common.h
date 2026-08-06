#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <algorithm>
#include "ManagedPara.pb.h"
#include "MdClient/TickMarketData.h"
#include "SpdlogWrapper.h"
#include "ivo/md_common.h"
#include "Utils/string_utils.h"
#include "Utils/json_utils.h"
#include "nlohmann/json_fwd.hpp"
#include "order_context.h"
#include "singletonT.h"
#include "spdlog/fmt/bundled/format.h"
#include "strategy_config.h"
#include "strategy_utils.h"
#include "spdlog/spdlog.h"
#include "trading_utils.h"
#include "magic_enum.hpp"
#include "Utils/Utils.h"

template <typename Req, typename T>
bool GetPBKey(const Req& req, const std::string& key, T& value) {
  auto it = req.pvalues().find(key);
  if (it == req.pvalues().end()) {
    return false;
  }
  value = it->second;
  return true;
}

static const std::string kExtraManagedSuffix = "EX";
static constexpr std::uint32_t kMDCacheSize = 1024;
static const std::string kGunCloseDes = "GunOrInsClose";
static const std::string kInvalidVolume = "InvalidVolume";
static const std::string kInvalidPred = "InvalidPredction";
static const std::string kDelayTooMuch = "DelayTooMuch";
static const std::string kPredictionInvalid = "PredictionInValid";
static const std::string kMaxQtyLessThenZero = "max_qty<=0";
static const std::string kSendQtyLessThenZero = "send_qty<=0";
static const std::string kBanByVPos = "ban_by_vpos";
static const std::string kBanByPrice = "ban_by_price";
static const std::uint32_t kDefaultDownSample = 2000U;
static const std::uint32_t kInfinityDownSample = 8000U;
static const std::string kHpTime0935 = "09:35:00";
static const std::string kHpTime1301 = "13:01:00";
static const auto kHpTime0930MS = ivolib::MillSecondsFromString(ivolib::kTime0930.data(), 0);
static const auto kHpTime0935MS = ivolib::MillSecondsFromString(kHpTime0935.data(), 0);
static const auto kHpTime1300MS = ivolib::MillSecondsFromString(ivolib::kTime1300.data(), 0);
static const auto kHpTime1301MS = ivolib::MillSecondsFromString(kHpTime1301.data(), 0);

enum class State : int {
  BeforeCallOrder = 0,
  AfterCallOrder,
  Continous,
};

struct BarFilter {
  bool operator()(const MD &md) const {
    if (__glibc_unlikely(before_start)) {
      if (md >= kStartTime) {
        before_start = false;
      } else {
        return false;
      }
    }

    return md < kTime145630;
  }

  static inline std::string kStartTime = "09:30:00";
  static inline std::string kTime145630 = "14:56:30";
  mutable bool before_start = true;
};

class BarQueue {
public:
  struct StrokesTheo {
    char code[9];
    std::uint64_t timestamp;
    double price;
  } __attribute__((packed));

  static constexpr auto kBarQueueCapacity = 1 << 18;
  static constexpr auto kBarQueueMask = kBarQueueCapacity - 1;

  static BarQueue *Init(const std::string &name) {
    static constexpr auto kBarQueueMemSize =
        sizeof(BarQueue) - sizeof(std::uint64_t);

    int fd;
    int flags = MAP_SHARED;
    if (getuid() == 0) // only root can lock the memory
    {
      flags |= MAP_LOCKED;
    }

    fd = shm_open(name.c_str(), O_RDWR, 00777);
    IVO_ASSERT(fd >= 0, "InvalidShm {}", name);

    struct stat st;
    int errorId = fstat(fd, &st);

    IVO_ASSERT(errorId >= 0 && st.st_size == kBarQueueMemSize,
               "InvalidShm {} {} {} {}", name, errorId, st.st_size,
               kBarQueueMemSize);

    auto queue = (BarQueue *)mmap(NULL, kBarQueueMemSize,
                                  PROT_READ | PROT_WRITE, flags, fd, 0);
    IVO_ASSERT(queue != MAP_FAILED, "InvalidShm {}", name);
    close(fd);
    queue->cur_idx_ = queue->next_idx_;
    IVOLOG_INFO("shm={},idx={}", name, queue->cur_idx_);
    return queue;
  }

  StrokesTheo *Read() {
    if (cur_idx_ == next_idx_.load(std::memory_order_relaxed)) {
      return nullptr;
    }

    return &(data_[cur_idx_++ & kBarQueueMask]);
  }

  std::uint64_t PrevReadIdx() { return cur_idx_ - 1; }

private:
  std::atomic<std::uint64_t> next_idx_{0};
  [[maybe_unused]] char padding1_[64 - sizeof(std::uint64_t)];
  StrokesTheo data_[kBarQueueCapacity];
  [[maybe_unused]] char padding2_[256];
  std::uint64_t cur_idx_{0};
};

enum ModelType {
  Model21F = 0,
  Infinity,
  NoModel,
  Leading21F,
  DSChange,
  DSChange21,
  DSChangeV4,
};

struct OrderFactor {
  double order_pf = 0;
  double order_nf = 0;
  double order_mf = 0;
  double cxl_buy_flow = 0;
  double cxl_sell_flow = 0;
  double buy_order_volume = 0;
  double sell_order_volume = 0;
};

struct TradeFactor {
  double trade_pcf = 0;
  double trade_ncf = 0;
  double trade_pt = 0;
  double trade_nt = 0;
  double order_pf = 0;
  double order_nf = 0;
  double order_cnt = 0;
  double buy_order_volume = 0;
  double sell_order_volume = 0;
};

enum FactorType {
  kFactorType21 = 0,
  kFactorType28,
  kFactorType40,
  KFactorTypeFullob,
  kFactorTypeNone,
};

struct HpFactor {
  float order_pf = 0;
  float order_nf = 0;
  float order_mf = 0;
  float trade_pcf = 0;
  float trade_ncf = 0;
  float trade_pt = 0;
  float trade_nt = 0;

  float hermes;
  float tr_sqrt_positive;
  float percent_spread;
  float percent_mid_rtn;
  float fee_on_tick;
  float bid_vol_chg_ratio;
  float ask_vol_chg_ratio;
  float pct_weighted_rtn1;
  float pct_weighted_rtn2;
  float pct_weighted_rtn3;
  float pct_weighted_rtn4;
  float pct_weighted_rtn5;
  float pct_weighted_ask;
  float pct_weighted_bid;
  float pct_weighted_ask_rtn;
  float pct_weighted_bid_rtn;
  float weighted_vol_diff_ratio;
  float vol_diff_ratio;
  float pct_turnover;
  float pct_liquidity_ask;
  float pct_liquidity_bid;

  // full ob factors todo修改命名风格
  float PositiveFillRate = 0;
  float NegativeFillRate = 0;
  float OrderFlowImbalance = 0;
  float CFRImbalance = 0;
  float FixDisImbalancePct1 = 0;
  float FixDisImbalancePct2 = 0;
  float WeightedFixDisImbalancePct1 = 0;
  float WeightedFixDisImbalancePct2 = 0;
  float AvgSizeImbalance = 0;
  float AvgSizeImbalanceLevel1 = 0;
  float AvgSizeImbalanceLevel5 = 0;
  float OrderCountImbalance = 0;
  float OrderCountImbalanceLevel1 = 0;
  float OrderCountImbalanceLevel5 = 0;
  float OrderLifeImbalance = 0;
  float OrderLifeImbalanceLevel1 = 0;
  float OrderLifeImbalanceLevel5 = 0;
  float MaxBidDistance = 0;
  float MaxAskDistance = 0;
  float MaxVolDistanceImbalance = 0;
  float YoungOrderbookImbalance = 0;
  float FixDistHermes = 0;

  float last_buy_trade_price = 0;
  float last_sell_trade_price = 0;
  float buy_trade_l1 = 0;
  float buy_trade_l3 = 0;
  float sell_trade_l1 = 0;
  float sell_trade_l3 = 0;
  float leading_buy_rtn = 0;
  float leading_sell_rtn = 0;
  float leading_buy_amount = 0;
  float leading_sell_amount = 0;
  float bias_from_avg = 0;
  float bias_from_open = 0;

  HpFactor() = default;
  HpFactor(FactorType type) : factor_type(type) {}
  FactorType factor_type = kFactorTypeNone;
  static size_t factor_size;
};

struct GlobalParams {
  double offset_multiplier;          // p601
  double skew_base_line_sz;          // p6n0
  double skew_base_line_sh;          // p6n1
  double static_position_ratio;      // p6n2
  double offset_multiplier_sh = 1;   // p6n3
  double bf_multiplier_sz = 0;       // p6n4
  double offset_multiplier_sz = 1;   // p6n5
  double bf_multiplier_sh = 0;       // p6n6
  double quote_offset_multiplier_sz; // p6n7
  double quote_offset_multiplier_sh; // p6n8

  double can_quote_ratio = 0.3;
  double quote_bias_factor = 1;
  double cancel_offset = 10;
  double cancel_offset_multiplier = 1;
  double quote_offset = 1;
  int liquidity_window = 60;
  double max_proportion = 0.1;
  int max_quote_num_per_account = 30;
  int max_quote_num = 1;
  int quote_base_line = 1;
  int can_quote = 1;
  int quote_count = 10;
  double progress_sync_rate = 1.0;

  int planned_execution_time = 240;
  int downsample_interval = 3;
  double spread_tolerance = 0;

  double hp_offset_multiplier = 1.0;
  double hpb_offset_multiplier = 1.0;
  double cross_offset_multiplier = 0.7;

  double execution_tolerance = 0;
  double execution_tolerance_x = 0;
  std::int32_t order_size_limit = 0;

  void Update(const proto::ManagedParaDetailProto &detail);
  TO_JSON_IN(GlobalParams, offset_multiplier, execution_tolerance,
             skew_base_line_sz, skew_base_line_sh, static_position_ratio,
             offset_multiplier_sh, bf_multiplier_sz, offset_multiplier_sz,
             bf_multiplier_sh, quote_offset_multiplier_sz,
             quote_offset_multiplier_sh, can_quote_ratio, quote_bias_factor,
             cancel_offset, quote_offset, liquidity_window, max_proportion);
};

struct HpParams {
  double offset_base_line = 0;      // p601
  std::int32_t max_hit_vol = 100;   // p603
  double fee_share = 0;             // p604
  double history_amount_short = 0;  // p606
  double history_amount = 0;        // p607
  std::int32_t pi_sub = 0;          // p6n0
  std::int32_t auto_flag = 0;       // p6n1
  std::int32_t auto_fast = 1;
  double pos_limit_factor = 0;      // p6n2
  std::int32_t pe_sub = 0;          // p6n3
  std::int32_t pi = 0;              // p6n4
  std::int32_t pe = 0;              // p6n5
  std::int32_t shortable = 0;       // p6n6, p6n1 from PosGroup
  std::int32_t static_position = 0; // p6n7, p6n2 from PosGroup
  std::int32_t pi_fast = 0;         // p6n8
  std::int32_t pe_fast = 0;         // p6n9
  std::int32_t static_short_position = 0;    // p6n3 PosGroup
  std::int32_t short_sell_position = 0;  // p6n5 from PosGroup
  std::int32_t short_total_position = 0; // p6n6 from PosGroup

  double max_order_size = 100000;
  double min_order_size = 10000;

  std::int32_t total_execution_volume = 10000000;

  double price_tick;
  double u_price;
  double l_price;
  std::int32_t vol_tick = 100;

  std::int32_t vl_pos = 0;
  std::int32_t vs_pos = 0;
  std::int32_t fast_vl_pos = 0;
  std::int32_t fast_vs_pos = 0;
  std::int32_t add_vl_pos = 0;
  std::int32_t add_vs_pos = 0;

  GlobalParams *global_params = nullptr;

  TO_JSON_IN(HpParams, offset_base_line, max_hit_vol, fee_share,
             history_amount_short, history_amount, pi_sub, auto_flag,
             pos_limit_factor, pe_sub, pi, pe, shortable, static_position,
             pi_fast, pe_fast, static_short_position, price_tick, u_price,
             l_price, vol_tick, short_sell_position, short_total_position);

  bool CanBuy() {
    bool buy_flag = (auto_flag == 1 || auto_flag == 2);
    if (not buy_flag) {
      return false;
    }
    return vs_pos == 0 && fast_vs_pos == 0 && add_vs_pos == 0;
  }

  bool CanBuyFast() { return CanBuy() && auto_fast == 1; }

  bool CanSell() {
    bool sell_flag = (auto_flag == 1 || auto_flag == 3);
    if (not sell_flag) {
      return false;
    }
    return vl_pos == 0 && fast_vl_pos == 0 && add_vl_pos == 0;
  }

  bool CanSellFast() { return CanSell() && auto_fast == 1; }

  void WantSell(std::int32_t qty) { vs_pos += qty; }
  void WantBuy(std::int32_t qty) { vl_pos += qty; }
  void WantSellFast(std::int32_t qty) { fast_vs_pos += qty; }
  void WantBuyFast(std::int32_t qty) { fast_vl_pos += qty; }
  void WantSellAdd(std::int32_t qty) { add_vs_pos += qty; }
  void WantBuyAdd(std::int32_t qty) { add_vl_pos += qty; }

  std::int32_t GetFakeExecVolume() {
    return total_execution_volume == 0 ? 1000000
                                       : std::abs(total_execution_volume);
  }

  std::int32_t GetPositionLimit() {
    return std::clamp(pos_limit_factor * global_params->static_position_ratio, 0.0, 1.0) * static_position;
  }

  std::int32_t GetVirtualLong() { return vl_pos + fast_vl_pos + add_vl_pos; }
  std::int32_t GetVirtualShort() { return vs_pos + fast_vs_pos + add_vs_pos; }
  std::int32_t GetRemainingShortableForT0() {
    // T0当日剩余可卖： shortable - 剩余未执行的卖出量
    // 如果是调入，则剩余未执行的卖出量为0
    return shortable - (static_short_position + std::min(pi_fast + pi_sub, 0));
  }

  bool HKShort() {
#ifdef HK
    if (shortable > 0) {
      return false;
    } else if (shortable == 0 && short_total_position > short_sell_position) {
      return true;
    }
#endif
    return false;
  }

  // 最后一笔才能卖出碎股
  std::int32_t AdjustSell(std::int32_t qty) {
    if (qty >= shortable) {
      return std::min(shortable, max_hit_vol);
    }
    return std::min(qty / vol_tick * vol_tick, max_hit_vol);
  }

  std::int32_t MaxSellQtyForT0() {
    auto qty = std::min(GetRemainingShortableForT0(), GetPositionLimit() + pi) - vs_pos;
    return AdjustSell(qty);
  }

  std::int32_t MaxBuyQtyForT0() {
    // T0可买为当日T0剩余可卖量+已卖量
    auto qty = std::min(GetRemainingShortableForT0(), GetPositionLimit()) - pi - vl_pos;
    return std::min(qty, max_hit_vol);
  }

  std::int32_t MaxSellQtyForFast() {
    auto qty = std::min(pe_fast + pi_fast - fast_vs_pos, shortable - (vs_pos + fast_vs_pos + add_vs_pos));
    return AdjustSell(qty);
  }

  std::int32_t MaxBuyQtyForFast() {
    return std::min(-(pe_fast + pi_fast) - fast_vl_pos, max_hit_vol);
  }

  std::int32_t MaxSellQtyForSub() {
    auto qty = std::min(pe_sub + pi_sub - add_vs_pos, shortable - (vs_pos + fast_vs_pos + add_vs_pos));
    return AdjustSell(qty);
  }

  std::int32_t MaxBuyQtyForSub() { return std::min(-(pe_sub + pi_sub) - add_vl_pos, max_hit_vol); }
};

struct HpTheo {
  double theo0 = 0;
  double bias_factor = 0;
  double bias = 0;
  double skew = 0;
  double unitbias = 0;
  double hit_theo0_bp = 0;
  double hit_theo0_sp = 0;
  double quote_theo0_bp = 0;
  double quote_theo0_sp = 0;
  double offset = 0;
  double q_offset = 0;
  double b_offset = 0;
  double s_offset = 0;
  double b_q_offset = 0;
  double s_q_offset = 0;
  TO_JSON_IN(HpTheo, theo0, bias_factor, bias, skew, unitbias, hit_theo0_bp,
             hit_theo0_sp, quote_theo0_bp, quote_theo0_sp, offset, q_offset,
             b_offset, s_offset, b_q_offset, s_q_offset);
};

struct HpOrderContext {
  const std::string *name = nullptr;
  ModelType model;
  const std::string* description = nullptr;
  const std::string* no_hit_buy_des = nullptr;
  const std::string* no_hit_sell_des = nullptr;
  const std::string* no_fast_hit_buy_des = nullptr;
  const std::string* no_fast_hit_sell_des = nullptr;
  const std::string* no_sub_hit_buy_des = nullptr;
  const std::string* no_sub_hit_sell_des = nullptr;
  OB last_ob;
  std::uint64_t app_seq;
  HpTheo theo;
  HpFactor factor;
  float prediction;
  float prediction_raw;

  int32_t static_pos;
  int32_t static_short_pos;
  int32_t position_limit;
  int32_t shortable;
  int32_t cur_shortable;
  int32_t pi;
  int32_t pe;
  int32_t pos_in_fast;
  int32_t pos_ex_fast;
  int32_t pos_in_addsub;
  int32_t pos_ex_addsub;

  uint64_t hit_buy_id;
  uint64_t hit_sell_id;
  uint64_t quote_buy_id;
  uint64_t quote_sell_id;
  uint64_t fast_hit_buy_id;
  uint64_t fast_hit_sell_id;
  uint64_t addsub_hit_buy_id;
  uint64_t addsub_hit_sell_id;
  uint64_t cancel_buy_id;
  uint64_t cancel_sell_id;

  int32_t hit_buy_qty;
  int32_t b_margin_div_ub;
  int32_t max_buy_vol;
  int32_t max_buy_fast_vol;
  int32_t max_buy_sub_vol;

  int32_t hit_sell_qty;
  int32_t s_margin_div_ub;
  int32_t max_sell_vol;
  int32_t max_sell_fast_vol;
  int32_t max_sell_sub_vol;

  double mid_p;
  double execution_tolerance;
  double ot;

  double fast_quote_offset;
  double fast_buy_offset;
  double fast_sell_offset;
  double fast_quote_buy_offset;
  double fast_quote_sell_offset;
  double fast_quote_buy_price;
  double fast_quote_sell_price;

  HpOrderContext() {}
  HpOrderContext(const std::string* name_ptr, ModelType model_type) {
    memset(this, 0, sizeof(HpOrderContext));
    name = name_ptr;
    model = model_type;
  }
};

void to_json(nlohmann::ordered_json& j, const HpOrderContext& context);
void to_json(nlohmann::ordered_json& j, const HpFactor& factor);

struct PendingQuote {
  std::uint64_t id = 0;
  double price = 0;
  void Clear() { id = 0; }
};

class GlobalInfo : public ivolib::SingletonT<GlobalInfo> {
 public:
  int seq_code = 0;
  char seq_char = 'A';
  int batch_code = 0;
  bool exchange_sh = false;

  std::string pos_group;
  std::string t0_name;
  std::string fast_name;
  std::string sub_name;

  std::string pi;
  std::string pi_fast;
  std::string pi_sub;
  std::string shortable;
  std::string static_position;
  std::string static_short_position;
  std::string short_sell_position;
  std::string short_total_position;
  std::string pos_limit_factor;

  std::string total_execution_volume;

  std::unordered_map<std::string, std::int64_t> fast_position_map_;
  std::unordered_map<std::string, std::int64_t> sub_position_map_;
  std::unordered_map<std::string, std::int64_t> st_position_map_;

  nlohmann::json bar_js;

  static inline std::int32_t kCallOrderMillSeconds = 0;

  ~GlobalInfo() {
  }

  void FillKey() {
#define FMT_KEY(Key) Key = fmt::format("{}_{}", #Key, seq_char)

    FMT_KEY(pi);
    FMT_KEY(pi_fast);
    FMT_KEY(pi_sub);
    FMT_KEY(shortable);
    FMT_KEY(static_position);
    FMT_KEY(static_short_position);
    FMT_KEY(short_sell_position);
    FMT_KEY(short_total_position);
    FMT_KEY(total_execution_volume);
    FMT_KEY(pos_limit_factor);
  }

  std::string GetKeyBySeq(const std::string &key) {
    return fmt::format("{}_{}", key, seq_char);
  }

  template <typename Req, typename T>
  bool GetValueBySeqSlow(const Req& req, const std::string& key, T& t) {
    return GetPBKey(req, GetKeyBySeq(key), t);
  }

  TO_JSON_IN(GlobalInfo, seq_char, batch_code, exchange_sh, pos_group,
             fast_name, sub_name, pi, pi_fast, pi_sub, shortable,
             static_position, static_short_position, short_sell_position,
             short_total_position);

private:
  std::string hpx_channel_id_;
};

class HpManager : public ivolib::SingletonT<HpManager> {
public:
  void UpdateMillSeconds() {
    auto ms = ivolib::GetTimeStamp() / 1000;
    time_t seconds = static_cast<time_t>(ms / 1000);
    int mill = static_cast<int>(ms % 1000);

    struct tm *timeinfo = localtime(&seconds);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);

    local_time = ivolib::MillSecondsFromString(buffer, mill);
    // 10s打印一次日志
    IVOLOG_INFO_EVERY_N(10000, "[UpdateSeconds] {},{}", buffer, local_time);
  }
  std::int32_t local_time = 0;
};
