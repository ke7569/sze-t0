#pragma once

#include "ivo/md_common.h"
#include "factor/ivo_math.h"

#include "hp_depth_book.h"

#define HP_EPSILON (1e-6)
#define HP_PRICE_SCALE 1000.0

using HpBidDepth = hp::BookDepth<ivolib::md::DirectionType::kBuy>;
using HpAskDepth = hp::BookDepth<ivolib::md::DirectionType::kSell>;

struct HpOrderSum {
  double price = 0;
  double volume_sum = 0;
  int64_t count_sum = 0;
  int64_t tsc_sum = 0;
  double amt_sum = 0;
};

struct HpFullOb {
  HpFullOb() {
    memset(this, 0, sizeof(*this));
  }

  // 1 percent or dist == 0.01 away from mp
  HpOrderSum ask_01;
  HpOrderSum bid_01;

  // 5 percent or dist == 0.05 away from mp
  HpOrderSum ask_05;
  HpOrderSum bid_05;

  // 10 percent or dist == 0.20 away from mp
  HpOrderSum ask_10;
  HpOrderSum bid_10;

  // first level
  HpOrderSum ask_level1;
  HpOrderSum bid_level1;

  // top 5 levels(1-5)
  HpOrderSum ask_level5;
  HpOrderSum bid_level5;

  int64_t ask_max_volume = 0;
  int64_t bid_max_volume = 0;
  double ask_max_level_price = 0;
  double bid_max_level_price = 0;
  int64_t ask_total_count = 0;
  int64_t bid_total_count = 0;
  double mp = 0;
};

template <typename HpDepth>
static inline void hp_depth_max_volume(HpDepth& depth, int64_t& max_volume, double& max_level_price) {
  if (depth.count() == 0) {
    return;
  }

  auto max_volume_fn = [&max_volume, &max_level_price](const hp::Level& level) -> bool {
    if (max_volume < level.volume()) {
      max_volume = level.volume();
      max_level_price = level.price() / HP_PRICE_SCALE;
    }
    return true;
  };

  depth.ForEachLevel(max_volume_fn);
}

static inline HpFullOb hp_foreach_depth(const HpAskDepth& ask_depth, const HpBidDepth& bid_depth) {
  HpFullOb fullob;

  hp_depth_max_volume(ask_depth, fullob.ask_max_volume, fullob.ask_max_level_price);
  hp_depth_max_volume(bid_depth, fullob.bid_max_volume, fullob.bid_max_level_price);
  fullob.ask_total_count = ask_depth.count();
  fullob.bid_total_count = bid_depth.count();

  if (ask_depth.count() > 0 and bid_depth.count() > 0) {
    fullob.mp = (ask_depth.first_level()->price() +
      bid_depth.first_level()->price()) / 2.0 / HP_PRICE_SCALE;
    int cur_level = 1;
    ask_depth.ForEachLevel([&fullob, &cur_level](const hp::Level& level) -> bool {
      bool is_finished = true;
      if (level.price() / HP_PRICE_SCALE < fullob.mp * (1 + 0.01)) {
        auto& ask_01 = fullob.ask_01;
        ask_01.volume_sum += level.volume();
        ask_01.amt_sum += level.price() * level.volume();
        ask_01.count_sum += level.orders().size();
        ask_01.tsc_sum += level.total_extime();
      }

      if (level.price() / HP_PRICE_SCALE  < fullob.mp * (1 + 0.05)) {
        auto& ask_05 = fullob.ask_05;
        ask_05.volume_sum += level.volume();
        ask_05.amt_sum += level.price() * level.volume();
        ask_05.count_sum += level.orders().size();
        ask_05.tsc_sum += level.total_extime();
      }

      if (level.price()/ HP_PRICE_SCALE  < fullob.mp * (1 + 0.10)) {
        auto& ask_10 = fullob.ask_10;
        ask_10.volume_sum += level.volume();
        ask_10.amt_sum += level.price() * level.volume();
        ask_10.count_sum += level.orders().size();
        ask_10.tsc_sum += level.total_extime();
        is_finished = false;
      }

      if (cur_level == 1) {
        auto& ask_level1 = fullob.ask_level1;
        ask_level1.price = level.price();
        ask_level1.volume_sum += level.volume();
        ask_level1.amt_sum += level.price() * level.volume();
        ask_level1.count_sum += level.orders().size();
        ask_level1.tsc_sum += level.total_extime();
      }

      if (cur_level <= 5) {
        auto& ask_level5 = fullob.ask_level5;
        ask_level5.volume_sum += level.volume();
        ask_level5.amt_sum += level.price() * level.volume();
        ask_level5.count_sum += level.orders().size();
        ask_level5.tsc_sum += level.total_extime();
      }

      is_finished &= (cur_level>5);
      ++cur_level;
      return not is_finished;
    });

    cur_level = 1;
    bid_depth.ForEachLevel([&fullob, &cur_level](const hp::Level& level) -> bool {
      bool is_finished = true;
      if (level.price()/ HP_PRICE_SCALE > fullob.mp * (1 - 0.01)) {
        auto& bid_01 = fullob.bid_01;
        bid_01.volume_sum += level.volume();
        bid_01.amt_sum += level.price() * level.volume();
        bid_01.count_sum += level.orders().size();
        bid_01.tsc_sum += level.total_extime();
      }

      if (level.price()/ HP_PRICE_SCALE  > fullob.mp * (1 - 0.05)) {
        auto& bid_05 = fullob.bid_05;
        bid_05.volume_sum += level.volume();
        bid_05.count_sum += level.orders().size();
        bid_05.amt_sum += level.price() * level.volume();
        bid_05.tsc_sum += level.total_extime();
      }

      if (level.price()/ HP_PRICE_SCALE > fullob.mp * (1 - 0.10)) {
        auto& bid_10 = fullob.bid_10;
        bid_10.volume_sum += level.volume();
        bid_10.amt_sum += level.price() * level.volume();
        bid_10.count_sum += level.orders().size();
        bid_10.tsc_sum += level.total_extime();
        is_finished = false;
      }

      if (cur_level == 1) {
        auto& bid_level1 = fullob.bid_level1;
        bid_level1.price = level.price();
        bid_level1.volume_sum += level.volume();
        bid_level1.count_sum += level.orders().size();
        bid_level1.amt_sum += level.price() * level.volume();
        bid_level1.tsc_sum += level.total_extime();
      }

      if (cur_level <= 5) {
        auto& bid_level5 = fullob.bid_level5;
        bid_level5.volume_sum += level.volume();
        bid_level5.count_sum += level.orders().size();
        bid_level5.amt_sum += level.price() * level.volume();
        bid_level5.tsc_sum += level.total_extime();
      }

      is_finished &= (cur_level>5);
      ++cur_level;
      return not is_finished;
    });
  }

  fullob.ask_01.amt_sum /= HP_PRICE_SCALE;
  fullob.bid_01.amt_sum /= HP_PRICE_SCALE;
  fullob.ask_10.amt_sum /= HP_PRICE_SCALE;
  fullob.bid_10.amt_sum /= HP_PRICE_SCALE;
  fullob.ask_05.amt_sum /= HP_PRICE_SCALE;
  fullob.bid_05.amt_sum /= HP_PRICE_SCALE;
  fullob.ask_level1.amt_sum /= HP_PRICE_SCALE;
  fullob.bid_level1.amt_sum /= HP_PRICE_SCALE;
  fullob.ask_level5.amt_sum /= HP_PRICE_SCALE;
  fullob.bid_level5.amt_sum /= HP_PRICE_SCALE;
  return fullob;
}

static inline double hp_positive_fillrate(const OrderFactor& orderfactor, const TradeFactor& tradefactor) {
  double buyOrderVolume = orderfactor.buy_order_volume;
  return buyOrderVolume > HP_EPSILON ? std::clamp(tradefactor.trade_pt / buyOrderVolume, 0.0, 5.0) : 0;
}

static inline double hp_negative_fillrate(const OrderFactor& orderfactor, const TradeFactor& tradefactor) {
  double sellOrderVolume = orderfactor.sell_order_volume;
  return sellOrderVolume > HP_EPSILON ? std::clamp(tradefactor.trade_nt / sellOrderVolume, 0.0, 5.0) : 0;
}

static inline double hp_orderflow_imbalance(const OrderFactor& orderfactor, const TradeFactor& tradefactor) {
  double buyOrderVolume = orderfactor.buy_order_volume;
  double sellOrderVolume = orderfactor.sell_order_volume;
  return (buyOrderVolume - sellOrderVolume) / (buyOrderVolume + sellOrderVolume + 1);
}

static inline double hp_cfr_imbalance(const OrderFactor& orderfactor, const TradeFactor& tradefactor, double bench_volume) {
  // bench_volume = fee_share
  double buyCFR = tradefactor.trade_pt / (tradefactor.trade_pt + orderfactor.cxl_buy_flow + 1);
  double sellCFR = tradefactor.trade_nt / (tradefactor.trade_nt + orderfactor.cxl_sell_flow + 1);

  return (buyCFR - sellCFR) / (buyCFR + sellCFR + 1);
}

static inline double hp_fix_dis_imbalance(HpOrderSum& askordersum, HpOrderSum& bidordersum) {
  double askSum = askordersum.volume_sum;
  double bidSum = bidordersum.volume_sum;
  return (askSum - bidSum) / (askSum + bidSum + 1);
}

static inline double hp_weighted_fix_dis_imbalance(HpOrderSum& askordersum, HpOrderSum& bidordersum, double mp, double max_distance) {
  double askSum = askordersum.volume_sum * (1 + mp / max_distance) - askordersum.amt_sum / max_distance;
  double bidSum = bidordersum.volume_sum * (1 - mp / max_distance) + bidordersum.amt_sum / max_distance;
  if(askSum + bidSum == 0) {
    return 0;
  }
  return (askSum - bidSum) / (askSum + bidSum);
}

static inline double hp_avg_size_imbalance(HpOrderSum& askordersum, HpOrderSum& bidordersum) {
  double bidAvgSize = static_cast<double>(bidordersum.volume_sum) / bidordersum.count_sum;
  double askAvgSize = static_cast<double>(askordersum.volume_sum) / askordersum.count_sum;
  return (askAvgSize - bidAvgSize) / (askAvgSize + bidAvgSize);
}

static inline double hp_order_count_imbalance(HpOrderSum& askordersum, HpOrderSum& bidordersum) {
  return static_cast<double>(askordersum.count_sum - bidordersum.count_sum) / (askordersum.count_sum + bidordersum.count_sum);
}

static inline double hp_order_life_imbalance(HpOrderSum& askordersum, HpOrderSum& bidordersum, int64_t cur_tsc) {
  if (askordersum.count_sum == 0 || bidordersum.count_sum == 0) {
    return 0;
  }

  double askLife = cur_tsc - static_cast<double>(askordersum.tsc_sum) / askordersum.count_sum;
  double bidLife = cur_tsc - static_cast<double>(bidordersum.tsc_sum) / bidordersum.count_sum;

  if (askLife + bidLife == 0) {
    return 0;
  }

  return (askLife - bidLife) / (askLife + bidLife);
}

static inline double hp_max_distance(double p, double mp) {
  double bidDistance = p / mp - 1;
  return bidDistance;
}

static inline double hp_max_ask_distance(double maxAsk, double mp) {
  double askDistance = maxAsk / mp - 1;
  return askDistance;
}

static inline double hp_max_vol_distance_imbalance(double maxAsk, double maxBid, double mp) {
  auto askDistance = maxAsk / mp - 1;
  auto bidDistance = -(maxBid / mp - 1);

  return (askDistance - bidDistance) / (askDistance + bidDistance);
}

static inline double hp_young_orderbook_imbalance(const HpAskDepth& askdepth, const HpBidDepth& biddepth, double p, int64_t cur_tsc, double dist = 0.01) {
  auto mp = hp::to_price(p);
  double maxDistance = mp * dist;
  double askSum = 0;
  double bidSum = 0;

  biddepth.ForEachLevel([&bidSum, mp, maxDistance, cur_tsc](const hp::Level& level) -> bool {
    // 跳过
    if (level.price() >= mp) {
      return true;
    }

    double distance = mp - level.price();
    if (distance > maxDistance) {
      return false;
    }

    double priceWeight = 1.0 - distance / maxDistance;
    bidSum += level.window_volume_sum(cur_tsc) * priceWeight;
    return true;
  });

  askdepth.ForEachLevel([&askSum, mp, maxDistance, cur_tsc](const hp::Level& level) -> bool {
    if (level.price() <= mp) {
      return true;
    }

    double distance = level.price() - mp;
    if (distance > maxDistance) {
      return false;
    }

    auto priceWeight = 1.0 - distance / maxDistance;
    askSum += level.window_volume_sum(cur_tsc) * priceWeight;
    return true;
  });

  if (askSum <= HP_EPSILON && bidSum <= HP_EPSILON) {
    return 0;
  }

  return (askSum - bidSum) / (askSum + bidSum);
}

static inline double hp_fix_dist_hermes(const HpAskDepth& askdepth, const HpBidDepth& biddepth, double p, double dist = 0.01) {
  auto mp = hp::to_price(p);
  double maxDistance = mp * dist;
  double weightedAskSum = 0;
  double askWeight = 0;
  double weightedBidSum = 0;
  double bidWeight = 0;
  double bestAsk = askdepth.first_level()->price();
  double bestBid = biddepth.last_level()->price();

  biddepth.ForEachLevel([&bidWeight, &weightedBidSum, mp, maxDistance](const hp::Level& level) -> bool {
    if (level.price() > mp) {
      return true;
    }

    double distance = mp - level.price();
    if (distance > maxDistance) {
      return false;
    }

    auto priceWeight = 1.0 - distance / maxDistance;
    if (priceWeight <= 0) {
      return false;
    }

    auto weight = priceWeight * level.volume();
    weightedBidSum += weight * level.price();
    bidWeight += weight;

    return true;
  });

  askdepth.ForEachLevel([&askWeight, &weightedAskSum, mp, maxDistance](const hp::Level& level) -> bool {
    if (level.price() < mp) {
      return true;
    }

    double distance = level.price() - mp;
    if (distance > maxDistance) {
      return false;
    }

    auto priceWeight = 1.0 - distance / maxDistance;
    if (priceWeight <= 0) {
      return false;
    }

    auto weight = priceWeight * level.volume();
    weightedAskSum += weight * level.price();
    askWeight += weight;

    return true;
  });

  auto effectiveAsk = askWeight <= HP_EPSILON ? bestAsk : weightedAskSum / askWeight;
  auto effectiveBid = bidWeight <= HP_EPSILON ? bestBid : weightedBidSum / bidWeight;
  auto hermesPrice = (effectiveAsk + effectiveBid) / 2.0;
  if (hermesPrice <= 0) {
    return 0;
  }

  auto rtn = (hermesPrice / mp - 1.0) * 1e3;
  return std::clamp(rtn, -5.0, 5.0);
}
