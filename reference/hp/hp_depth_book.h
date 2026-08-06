#pragma once
#include <SpdlogWrapper.h>

#include "MdClient/TickMarketData.h"
#include "MdClient/TickMarketDataType.h"
#include "folly/sorted_vector_types.h"
#include "phmap.h"

namespace hp {

constexpr int kPriceFactor = 1000;
constexpr int kWindowSec = 30;
using Price = uint32_t;

enum class Exchange { SH, SZ };

static constexpr Price to_price(double price) noexcept {
  return static_cast<Price>((price + 0.0000005) * kPriceFactor);
}

class SlidingWindow {
 public:
  using Vol = uint64_t;

  static constexpr uint32_t kRing = kWindowSec + 1;  // 30s => 31 个桶（闭区间）

  bool add(uint32_t ts_sec, Vol v) noexcept {
    if (v == 0) return true;

    advance_to(ts_sec);
    if (!in_window(ts_sec)) return true;

    const uint32_t tick = ts_sec;
    auto& b = bucket(tick);

    if (b.tick != tick) {
      total_ -= b.sum;
      b.sum = 0;
      b.tick = tick;
    }

    b.sum += v;
    total_ += v;
    return true;
  }

  bool erase(uint32_t ts_sec, Vol v) noexcept {
    if (v == 0) return true;

    if (ts_sec > now_sec_) advance_to(ts_sec);
    if (!in_window(ts_sec)) return true;

    const uint32_t tick = ts_sec;
    auto& b = bucket(tick);

    if (b.tick != tick) return true;

    if (__glibc_unlikely(b.sum < v)) {
      IVOLOG_ERROR("ts {} erase error sum:{} < v:{}", ts_sec, b.sum, v);
      return false;
    }
    b.sum -= v;
    total_ -= v;
    return true;
  }

  Vol total(uint32_t now_sec) noexcept {
    advance_to(now_sec);
    return total_;
  }

  uint32_t now_sec() const noexcept { return now_sec_; }

 private:
  struct Bucket {
    uint32_t tick = 0;
    Vol sum = 0;
  };

  std::array<Bucket, kRing> ring_{};
  Vol total_ = 0;
  uint32_t now_sec_ = 0;

  inline Bucket& bucket(uint32_t tick) noexcept { return ring_[tick % kRing]; }

  // 闭区间：<= 30s 都算窗口内
  inline bool in_window(uint32_t ts_sec) const noexcept {
    return ts_sec <= now_sec_ && (now_sec_ - ts_sec) <= kWindowSec;
  }

  void advance_to(uint32_t now_sec) noexcept {
    if (now_sec <= now_sec_) return;

    const uint32_t old_tick = now_sec_;
    const uint32_t new_tick = now_sec;
    now_sec_ = now_sec;

    const uint32_t gap = new_tick - old_tick;
    if (gap >= kRing) {
      memset(ring_.data(), 0, sizeof(Bucket) * kRing);
      total_ = 0;
      return;
    }

    for (uint32_t t = old_tick + 1; t <= new_tick; ++t) {
      const uint32_t expired = t - kRing;
      auto& b = bucket(expired);
      if (b.tick == expired) {
        total_ -= b.sum;
        memset(&b, 0, sizeof(Bucket));
      }
    }
  }
};

template <Exchange E>
class MatchBook;
class Level;

class QuoteOrder {
  template <Exchange E>
  friend class MatchBook;
  friend class Level;

 public:
  explicit QuoteOrder(const ivolib::MdTickOrder& od, uint64_t extime) noexcept {
    volume_ = od.Qty;
    trade_qty_ = 0;
    tsc_ = extime;
    side_ = static_cast<ivolib::md::DirectionType>(od.Direction);
  }

  auto Left() const noexcept { return volume_ - trade_qty_; }

  auto tsc() const noexcept { return tsc_; }

  bool IsBuy() const noexcept {
    return side_ == ivolib::md::DirectionType::kBuy;
  }

 private:
  bool OnTrade(std::uint32_t v) noexcept {
    trade_qty_ += v;
    return trade_qty_ >= volume_;
  }

  int64_t volume_ = 0;
  int64_t trade_qty_ = 0;
  uint64_t tsc_ = 0;
  ivolib::md::DirectionType side_ = ivolib::md::DirectionType::kBuy;
};

class Level {
  template <Exchange E>
  friend class MatchBook;

 public:
  Level() : price_(0) { orders_.reserve(512); }
  Level(Price p) : price_(p) { orders_.reserve(512); }

  [[nodiscard]] auto volume() const noexcept { return volume_; }
  [[nodiscard]] auto price() const noexcept { return price_; }
  [[nodiscard]] auto total_extime() const noexcept { return total_extime_; }
  [[nodiscard]] auto window_volume_sum(uint32_t extime) const noexcept {
    return sliding_window_.total(extime / 1000);
  }
  [[nodiscard]] auto& orders() const noexcept { return orders_; }

 private:
  bool AddOrder(const ivolib::MdTickOrder& od, uint32_t extime) noexcept {
    auto od_it = orders_.emplace(od.Seq, QuoteOrder(od, extime));
    if (od_it.second) {
      volume_ += od.Qty;
      total_extime_ += extime;
      auto success = sliding_window_.add(extime / 1000, od.Qty);
      IVOLOG_ERROR_IF(not success, "ts {} add order fail ", extime);
      return success;
    }
    return od_it.second;
  }

  bool OnTrade(uint64_t order_id, int64_t qty, uint64_t extime) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
      return false;
    }
    volume_ -= qty;
    auto time = it->second.tsc();

    if (it->second.OnTrade(qty)) {
      total_extime_ -= it->second.tsc();
      orders_.erase(it);
    }
    auto success = sliding_window_.erase(time / 1000, qty);
    IVOLOG_ERROR_IF(not success, "{} erase order_id:{} qty:{} fail", price_,
                    order_id, qty);
    return success;
  }

  bool CancelOrder(uint64_t order_id, uint64_t extime) {
    auto it = orders_.find(order_id);
    if (it != orders_.end()) {
      volume_ -= it->second.Left();
      total_extime_ -= it->second.tsc();
      auto success =
          sliding_window_.erase(it->second.tsc() / 1000, it->second.Left());
      IVOLOG_ERROR_IF(not success, "{} erase order_id:{} qty:{} fail", price_,
                      order_id, it->second.Left());
      orders_.erase(it);
      return success;
    }
    return false;
  }

  Price price_;
  int64_t volume_ = 0;
  phmap::flat_hash_map<uint64_t, QuoteOrder> orders_;
  uint64_t total_extime_ = 0;
  mutable SlidingWindow sliding_window_{};
};

template <ivolib::md::DirectionType Dir>
class BookDepth {
  template <Exchange E>
  friend class MatchBook;

  static_assert(Dir == ivolib::md::DirectionType::kBuy ||
                    Dir == ivolib::md::DirectionType::kSell,
                "BookDepth<Dir>: Dir must be Buy or Sell");
  using PriceCompare =
      std::conditional_t<Dir == ivolib::md::DirectionType::kBuy,
                         std::less<Price>,    // 买盘：价格低的排前面（升序）
                         std::greater<Price>  // 卖盘：价格高的排前面（降序）
                         >;

 public:
  BookDepth() { depths_.reserve(512); }

  /**
   * 返回一档
   * @return
   */
  const Level* first_level() const {
    return depths_.empty() ? nullptr : &depths_.rbegin()->second;
  }

  /**
   * 返回最后一档
   * @return
   */
  const Level* last_level() const {
    return depths_.empty() ? nullptr : &depths_.begin()->second;
  }

  auto count() const noexcept { return depths_.size(); }

  /**
   * 从一档遍历到p 档位，默认不填p会全部遍历
   * @param fn 接收bool func(Level&) 的回调函数，返回false就停止迭代
   * @param p 价格，从一档价格遍历到p价格的档位
   * @return 返回迭代了多少档位
   */
  template <typename Func, typename = std::enable_if_t<
                               std::is_invocable_r_v<bool, Func, const Level&>>>
  auto ForEachPrice(Func&& fn,
                    Price p = std::numeric_limits<Price>::max()) const {
    size_t i = 1;
    for (auto it = depths_.rbegin(); it != depths_.rend() and it->first <= p;
         ++it, ++i) {
      if (not std::forward<Func>(fn)(it->second)) {
        return i;
      }
    }
    return i;
  }

  /**
   * 从最后档遍历到一档
   * @param fn 接收bool func(Level&) 的回调函数，返回false就停止迭代
   * @param p 价格，从最后一档价格遍历到p价格的档位
   * @return 返回迭代了多少档位
   */
  template <typename Func, typename = std::enable_if_t<
                               std::is_invocable_r_v<bool, Func, const Level&>>>
  auto ForEachPriceReverse(Func&& fn, Price p = 0) const {
    size_t i = 1;
    for (auto it = depths_.begin(); it != depths_.end() and it.first >= p;
         ++it, ++i) {
      if (not std::forward<Func>(fn)(it->second)) {
        return i;
      }
    }
    return i;
  }

  /**
   * 从一档遍历到最后一档
   * @param fn 接收bool func(Level&) 的回调函数，返回false就停止迭代
   * @param n 迭代n档位，默认全部迭代
   * @return 返回迭代了多少档位
   */
  template <typename Func, typename = std::enable_if_t<
                               std::is_invocable_r_v<bool, Func, const Level&>>>
  auto ForEachLevel(Func&& fn,
                    size_t n = std::numeric_limits<size_t>::max()) const {
    size_t i = 1;
    for (auto it = depths_.rbegin(); it != depths_.rend() and i <= n;
         ++it, ++i) {
      if (not std::forward<Func>(fn)(it->second)) {
        return i;
      }
    }
    return i;
  }

  /**
   * 从最后档遍历到一档
   * @param fn 接收bool func(Level&) 的回调函数，返回false就停止迭代
   * @param n 迭代n档位，默认全部迭代
   * @return 返回迭代了多少档位
   */
  template <typename Func, typename = std::enable_if_t<
                               std::is_invocable_r_v<bool, Func, const Level&>>>
  auto ForEachLevelReverse(
      Func&& fn, size_t n = std::numeric_limits<size_t>::max()) const {
    size_t i = 1;
    for (auto it = depths_.begin(); it != depths_.end() and i <= n; ++it, ++i) {
      if (not std::forward<Func>(fn)(it->second)) {
        return i;
      }
    }
    return i;
  }

 private:
  folly::sorted_vector_map<Price, Level, PriceCompare> depths_;
};

template <Exchange E>
class MatchBook {
 public:
  explicit MatchBook(const std::string& instrument) : instrument_(instrument) {}
  auto& ask() const noexcept { return asks_; }

  auto& bid() const noexcept { return bids_; }

  auto& instrument() const noexcept { return instrument_; }

  bool UpdateMD(const ivolib::TickMarketDataByType& md) {
    if (not available_) {
      return true;
    }
    uint64_t extime = ivolib::MillSecondsFromString(md.UpdateTime, md.MillSec);
    auto success = doUpdateMd(md, extime);
    if (not success) {
      available_ = false;
      IVOLOG_ERROR("{} {} {}.{} update md fail {}", instrument_, md.TickType,
                   md.UpdateTime, md.MillSec, success);
    }
    return success;
  }

 private:
  bool doUpdateMd(const ivolib::TickMarketDataByType& md, uint64_t extime) {
    switch (md.TickType) {
      case ivolib::TICKTYPE_ORDER: {
        if constexpr (E == Exchange::SZ) {
          return SZAddOrder(md.TickOrder, extime);
        } else {
          switch (static_cast<ivolib::md::OrderType>(md.TickOrder.OrdType)) {
            case ivolib::md::OrderType::kAdd: {
              return AddLimitOrder(md.TickOrder, extime);
            }
            case ivolib::md::OrderType::kDelete: {
              auto& od = md.TickOrder;
              return CancelOrder(od.Seq, to_price(od.Price), od.BizIndex,
                                 IsBuy(od), extime);
            }
            default: {
              return true;
            }
          }
        }
      }
      case ivolib::TICKTYPE_TRADE: {
        if constexpr (E == Exchange::SZ) {
          switch (
              static_cast<ivolib::md::TradeFlagType>(md.TickTrade.TradeFlag)) {
            case ivolib::md::TradeFlagType::kFill: {
              return OnTrade(md.TickTrade, extime);
            }
            case ivolib::md::TradeFlagType::kCancel: {
              auto& td = md.TickTrade;
              return CancelOrder(std::max(td.BidNo, td.AskNo),
                                 to_price(td.Price), td.Seq,
                                 td.BidNo > td.AskNo, extime);
            }
            default: {
              return true;
            }
          }
        } else {
          return OnTrade(md.TickTrade, extime);
        }
      }
      default: {
        return true;
      }
    }
  }

  bool OnTrade(const ivolib::MdTickTrade& td, uint64_t extime) {
    auto bid_no = td.BidNo;
    auto ask_no = td.AskNo;
    auto bid_level = bids_.depths_.rbegin();
    auto ask_level = asks_.depths_.rbegin();
    if constexpr (E == Exchange::SZ) {
      if (bid_level == bids_.depths_.rend() or
          ask_level == asks_.depths_.rend()) {
        IVOLOG_ERROR("{} on trade fail level1 is null bid:{}, ask:{}, seq:{}",
                     instrument_, td.Seq, bids_.depths_.size(),
                     asks_.depths_.size());
        return false;
      }
      if (not bid_level->second.OnTrade(bid_no, td.Qty, extime)) {
        return false;
      }
      if (bid_level->second.orders_.empty()) {
        bids_.depths_.erase(std::next(bid_level).base());
      }
      if (not ask_level->second.OnTrade(ask_no, td.Qty, extime)) {
        return false;
      }
      if (ask_level->second.orders_.empty()) {
        asks_.depths_.erase(std::next(ask_level).base());
      }
    } else {
      if (bid_level != bids_.depths_.rend()) {
        bid_level->second.OnTrade(bid_no, td.Qty, extime);
        if (bid_level->second.orders_.empty()) {
          bids_.depths_.erase(std::next(bid_level).base());
        }
      }
      if (ask_level != asks_.depths_.rend()) {
        ask_level->second.OnTrade(ask_no, td.Qty, extime);
        if (ask_level->second.orders_.empty()) {
          asks_.depths_.erase(std::next(ask_level).base());
        }
      }
    }
    return true;
  }

  static bool IsBuy(const ivolib::MdTickOrder& od) {
    return od.Direction == static_cast<char>(ivolib::md::DirectionType::kBuy);
  }

  template <typename Depth>
  Level& DepthGetLevel(Price p, Depth& depth) {
    auto it = depth.find(p);
    if (it == depth.end()) {
      it = depth.emplace(p, p).first;
    }
    return it->second;
  }

  Level* GetLevelOne(const ivolib::MdTickOrder& od) {
    if (IsBuy(od)) {
      auto o =
          bids_.depths_.empty() ? nullptr : &bids_.depths_.rbegin()->second;
      if (__glibc_unlikely(o == nullptr)) {
        return nullptr;
      }
      return o;
    }
    auto o = asks_.depths_.empty() ? nullptr : &asks_.depths_.rbegin()->second;
    if (__glibc_unlikely(o == nullptr)) {
      return nullptr;
    }
    return o;
  }

  bool SZAddOrder(const ivolib::MdTickOrder& od, uint64_t extime) {
    switch (static_cast<ivolib::md::OrderType>(od.OrdType)) {
      case ivolib::md::OrderType::kSelfBest: {
        auto self = GetLevelOne(od);
        if (__glibc_unlikely(self == nullptr)) {
          if (IsBuy(od)) {
            self = &bids_.depths_[to_price(od.Price)];
          } else {
            self = &asks_.depths_[to_price(od.Price)];
          }
        }
        return self->AddOrder(od, extime);
      }
      case ivolib::md::OrderType::kMarketPrice:
      case ivolib::md::OrderType::kLimitPrice: {
        return AddLimitOrder(od, extime);
      }
      default:
        break;
    }
    return true;
  }

  bool AddLimitOrder(const ivolib::MdTickOrder& od, uint64_t extime) {
    Level* level = nullptr;
    if (IsBuy(od))
      level = &DepthGetLevel(to_price(od.Price), bids_.depths_);
    else
      level = &DepthGetLevel(to_price(od.Price), asks_.depths_);
    return level->AddOrder(od, extime);
  }

  bool CancelOrder(uint64_t order_id, Price p, uint64_t seq, bool is_buy,
                   uint64_t extime) {
    auto func = [order_id, extime, seq, this](Level* l) {
      auto success = l->CancelOrder(order_id, extime);
      IVOLOG_ERROR_IF(success and l->volume_ <= 0 and not l->orders_.empty(),
                      "{} level:{} match error seq:{} volume:{}, order size:{}",
                      instrument_, l->price(), seq, l->volume(),
                      l->orders_.size());
      return success;
    };
    if (is_buy) {
      auto target = bids_.depths_.find(p);
      if (target != bids_.depths_.end()) {
        if (func(&target->second)) {
          if (target->second.orders_.empty()) {
            bids_.depths_.erase(target);
          }
          return true;
        }
      }
      IVOLOG_INFO(
          "{} td seq:{} cancel warning, buy order_id:{}, price:{} level not "
          "found",
          instrument_, seq, order_id, p);
      for (auto it = bids_.depths_.rbegin(); it != bids_.depths_.rend(); ++it) {
        if (it->second.volume() == 0 or it->second.orders().empty()) {
          continue;
        }
        if (func(&it->second)) {
          if (it->second.orders().empty()) {
            bids_.depths_.erase(std::next(it).base());
          }
          return true;
        }
      }
    } else {
      auto target = asks_.depths_.find(p);
      if (target != asks_.depths_.end()) {
        if (func(&target->second)) {
          if (target->second.orders_.empty()) {
            asks_.depths_.erase(target);
          }
          return true;
        }
      }

      IVOLOG_INFO(
          "{} td seq:{} cancel warning, sell order_id:{}, price:{} level not "
          "found",
          instrument_, seq, order_id, p);
      for (auto it = asks_.depths_.rbegin(); it != asks_.depths_.rend(); ++it) {
        if (it->second.volume() == 0 or it->second.orders().empty()) {
          continue;
        }
        if (func(&it->second)) {
          if (it->second.orders().empty()) {
            asks_.depths_.erase(std::next(it).base());
          }
          return true;
        }
      }
    }
    return false;
  }

  std::string instrument_;
  BookDepth<ivolib::md::DirectionType::kBuy> bids_;
  BookDepth<ivolib::md::DirectionType::kSell> asks_;
  bool available_ = true;
};

}  // namespace hp