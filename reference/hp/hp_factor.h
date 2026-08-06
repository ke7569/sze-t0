#pragma once

#include "ivo/md_common.h"
#include "factor/ivo_math.h"

#define APBV_BPAV_P(md, idx, p)                                                \
  (((MD_AP(md, idx) * MD_BV(md, idx) + MD_BP(md, idx) * MD_AV(md, idx)) /      \
    (MD_BV(md, idx) + MD_AV(md, idx))) *                                       \
   p)

static inline double hp_mid_price(const OB* cur_md) {
  return (MD_AV1(cur_md) * MD_BV1(cur_md)) > 0 ? (MD_MID_PRICE1(cur_md))
                                               : (MD_LP(cur_md));
}

static inline double hp_mid_rtn(const OB* last_md, const OB* cur_md) {
  return hp_mid_price(cur_md) - hp_mid_price(last_md);
}

static inline double hp_spread(const OB* cur_md) {
  return (MD_AV1(cur_md) * MD_BV1(cur_md)) > 0
             ? (MD_AP1(cur_md) - MD_BP1(cur_md))
             : 0;
}

static inline double hp_calc_hermes_lv2_classic(const OB* md_pro_ref, double up,
                                                double lp) {
  const OB* md = md_pro_ref;
  double sum = 0;
  int div = 0;

  do {
    if (__glibc_unlikely(MD_IDX_INVALID(md, 1))) {
      if (MD_LP(md) <= up && MD_LP(md) >= lp) {
        sum = MD_LP(md);
      }
      div = 1;
      break;
    }
    sum += APBV_BPAV_P(md, 1, 5);
    div += 5;

    if (__glibc_unlikely(MD_IDX_INVALID(md, 2)))
      break;
    sum += APBV_BPAV_P(md, 2, 4);
    div += 4;

    if (__glibc_unlikely(MD_IDX_INVALID(md, 3)))
      break;
    sum += APBV_BPAV_P(md, 3, 3);
    div += 3;

    if (__glibc_unlikely(MD_IDX_INVALID(md, 4)))
      break;
    sum += APBV_BPAV_P(md, 4, 2);
    div += 2;

    if (__glibc_unlikely(MD_IDX_INVALID(md, 5)))
      break;
    sum += APBV_BPAV_P(md, 5, 1);
    div += 1;
  } while (0);

  return sum / div;
}

static inline double hp_percent_hermes(const OB* cur_md, double up, double lp) {
  double mid = hp_mid_price(cur_md);
  if (mid < 0.01 || mid > 1e6) {
    return 0;
  }

  double hermes = hp_calc_hermes_lv2_classic(cur_md, up, lp);
  double rt = (hermes / mid - 1) * 1e3;

  if (rt < -5) {
    rt = -5;
  } else if (rt > 5) {
    rt = 5;
  }
  return rt;
}

static inline double hp_tr_sqrt_positive(const OB* last_ob, const OB* cur_ob,
                                         double fee_share) {
  int64_t v = MD_VOL(cur_ob) - MD_VOL(last_ob);
  double fsv = v / fee_share;
  if (v == 0) {
    return 0;
  }
  if (__glibc_unlikely(!MD_IDX_VALID(cur_ob, 1))) {
    return 0;
  }
  if (__glibc_unlikely(!MD_IDX_VALID(last_ob, 1))) {
    return 0;
  }

  double atp = (MD_TURNOVER(cur_ob) - MD_TURNOVER(last_ob)) / v;
  double spread = MD_AP1(last_ob) - MD_BP1(last_ob);
  double atp_r = atp - hp_mid_price(last_ob);
  double r = atp_r / spread;

  if (r < -0.5) {
    r = -0.5;
  } else if (r > 0.5) {
    r = 0.5;
  }

  return sqrt(fsv * (0.5 + r)) - sqrt(fsv * (0.5 - r));
}

static inline double hp_fee_on_tick(const OB* cur_ob) {
  return sqrt(0.15 * MD_LP(cur_ob));
}

static inline double hp_percent_mid_rtn(const OB* last_md, const OB* cur_md) {
  if (__glibc_unlikely(last_md == nullptr || cur_md == nullptr)) {
    return 0;
  }
  return hp_mid_rtn(last_md, cur_md) / hp_mid_price(cur_md) * 1e3;
}

static inline double hp_percent_spread(const OB* cur_md) {
  return hp_spread(cur_md) / hp_mid_price(cur_md) * 1e3;
}

static inline double hp_liquidity(const OB* cur_md) {
  return (MD_BV1(cur_md) + MD_AV1(cur_md)) / 1e4;
}

static inline double hp_hit_buy_change(const OB* cur_md) {
  return hp_spread(cur_md) / 2.0 / MD_AV1(cur_md);
}

static inline double hp_hit_sell_change(const OB* cur_md) {
  return hp_spread(cur_md) / 2.0 / MD_BV1(cur_md);
}

static inline double hp_weighted_ask(const OB* cur_md) {

  int64_t quoteSum = MD_AV1_5(cur_md);

  if (quoteSum == 0)
    return 0;

  double mid = MD_MID_PRICE1(cur_md);

  double weighted = 0;
  do {
    if (0 == MD_AV1(cur_md)) {
      break;
    }
    weighted += (MD_AV1(cur_md) * MD_AP1(cur_md));

    if (0 == MD_AV2(cur_md)) {
      break;
    }
    weighted += (MD_AV2(cur_md) * MD_AP2(cur_md));

    if (0 == MD_AV3(cur_md)) {
      break;
    }
    weighted += (MD_AV3(cur_md) * MD_AP3(cur_md));

    if (0 == MD_AV4(cur_md)) {
      break;
    }
    weighted += (MD_AV4(cur_md) * MD_AP4(cur_md));

    if (0 == MD_AV5(cur_md)) {
      break;
    }
    weighted += (MD_AV5(cur_md) * MD_AP5(cur_md));

  } while (0);
  return weighted / quoteSum - mid;
}

static inline double hp_weighted_bid(const OB* cur_md) {

  int64_t quoteSum = MD_BV1_5(cur_md);

  if (quoteSum == 0)
    return 0;

  double mid = MD_MID_PRICE1(cur_md);

  double weighted = 0;
  do {
    if (0 == MD_BV1(cur_md)) {
      break;
    }
    weighted += (MD_BV1(cur_md) * MD_BP1(cur_md));

    if (0 == MD_BV2(cur_md)) {
      break;
    }
    weighted += (MD_BV2(cur_md) * MD_BP2(cur_md));

    if (0 == MD_BV3(cur_md)) {
      break;
    }
    weighted += (MD_BV3(cur_md) * MD_BP3(cur_md));

    if (0 == MD_BV4(cur_md)) {
      break;
    }
    weighted += (MD_BV4(cur_md) * MD_BP4(cur_md));

    if (0 == MD_BV5(cur_md)) {
      break;
    }
    weighted += (MD_BV5(cur_md) * MD_BP5(cur_md));
  } while (0);

  return mid - weighted / quoteSum;
}

static inline double hp_ask_volume(const OB* cur_md) {
  if (__glibc_unlikely(MD_IDX_INVALID(cur_md, 1))) {
    return 0;
  }
  return sqrt(MD_AV1(cur_md) + 1);
}

static inline double hp_bid_volume(const OB* cur_md) {
  if (__glibc_unlikely(MD_IDX_INVALID(cur_md, 1))) {
    return 0;
  }
  return sqrt(MD_BV1(cur_md) + 1);
}

static inline double hp_ask_vol_chg_ratio(const OB* last_ref,
                                          const OB* cur_ref) {
  double vol = MD_AV1(cur_ref) + MD_BV1(cur_ref);
  if (__glibc_unlikely(vol == 0)) {
    return 0;
  }

  double rt = 0;
  do {
    if (MD_AP1(cur_ref) - MD_AP1(last_ref) < -1e-6) {
      rt = (MD_BV1(last_ref) + MD_AV1(cur_ref)) / vol;
      break;
    }
    if (MD_AP1(cur_ref) - MD_AP1(last_ref) > 1e-6) {
      rt = -MD_AV1(last_ref) / vol;
      break;
    }

    rt = (MD_AV1(cur_ref) - MD_AV1(last_ref)) / vol;
  } while (0);

  return std::clamp(rt, -200.0, 200.0);
}

static inline double hp_bid_vol_chg_ratio(const OB* last_ref,
                                          const OB* cur_ref) {
  double vol = MD_AV1(cur_ref) + MD_BV1(cur_ref);
  if (__glibc_unlikely(vol == 0)) {
    return 0;
  }

  double rt = 0;
  do {
    if (MD_BP1(cur_ref) - MD_BP1(last_ref) < -1e-6) {
      rt = -MD_BV1(last_ref) / vol;
      break;
    }
    if (MD_BP1(cur_ref) - MD_BP1(last_ref) > 1e-6) {

      rt = (MD_AV1(last_ref) + MD_BV1(cur_ref)) / vol;
      break;
    }

    rt = (MD_BV1(cur_ref) - MD_BV1(last_ref)) / vol;

  } while (0);

  return std::clamp(rt, -200.0, 200.0);
}

static inline double hp_weighted_price1(const OB* cur_ref) {
  if (0 == MD_AV1(cur_ref) + MD_BV1(cur_ref))
    return hp_mid_price(cur_ref);

  double ap = MD_AV1(cur_ref) == 0 ? 0 : MD_AP1(cur_ref);
  double bp = MD_BV1(cur_ref) == 0 ? 0 : MD_BP1(cur_ref);
  return (ap * MD_AV1(cur_ref) + bp * MD_BV1(cur_ref)) /
         (MD_AV1(cur_ref) + MD_BV1(cur_ref));
}

static inline double hp_weighted_price2(const OB* cur_ref) {
  if (0 ==
      MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) + MD_BV2(cur_ref)) {
    return hp_mid_price(cur_ref);
  }
  double ap1 = MD_AV1(cur_ref) == 0 ? 0 : MD_AP1(cur_ref);
  double bp1 = MD_BV1(cur_ref) == 0 ? 0 : MD_BP1(cur_ref);
  double ap2 = MD_AV2(cur_ref) == 0 ? 0 : MD_AP2(cur_ref);
  double bp2 = MD_BV2(cur_ref) == 0 ? 0 : MD_BP2(cur_ref);

  return (ap1 * MD_AV1(cur_ref) + bp1 * MD_BV1(cur_ref) +
          ap2 * MD_AV2(cur_ref) + bp2 * MD_BV2(cur_ref)) /
         (MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
          MD_BV2(cur_ref));
}

static inline double hp_weighted_price3(const OB* cur_ref) {
  if (0 == MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
               MD_BV2(cur_ref) + MD_AV3(cur_ref) + MD_BV3(cur_ref)) {
    return hp_mid_price(cur_ref);
  }
  double ap1 = MD_AV1(cur_ref) == 0 ? 0 : MD_AP1(cur_ref);
  double bp1 = MD_BV1(cur_ref) == 0 ? 0 : MD_BP1(cur_ref);
  double ap2 = MD_AV2(cur_ref) == 0 ? 0 : MD_AP2(cur_ref);
  double bp2 = MD_BV2(cur_ref) == 0 ? 0 : MD_BP2(cur_ref);
  double ap3 = MD_AV3(cur_ref) == 0 ? 0 : MD_AP3(cur_ref);
  double bp3 = MD_BV3(cur_ref) == 0 ? 0 : MD_BP3(cur_ref);

  return (ap1 * MD_AV1(cur_ref) + bp1 * MD_BV1(cur_ref) +
          ap2 * MD_AV2(cur_ref) + bp2 * MD_BV2(cur_ref) +
          ap3 * MD_AV3(cur_ref) + bp3 * MD_BV3(cur_ref)) /
         (MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
          MD_BV2(cur_ref) + MD_AV3(cur_ref) + MD_BV3(cur_ref));
}

static inline double hp_weighted_price4(const OB* cur_ref) {
  if (0 == MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
               MD_BV2(cur_ref) + MD_AV3(cur_ref) + MD_BV3(cur_ref) +
               MD_AV4(cur_ref) + MD_BV4(cur_ref)) {
    return hp_mid_price(cur_ref);
  }
  double ap1 = MD_AV1(cur_ref) == 0 ? 0 : MD_AP1(cur_ref);
  double bp1 = MD_BV1(cur_ref) == 0 ? 0 : MD_BP1(cur_ref);
  double ap2 = MD_AV2(cur_ref) == 0 ? 0 : MD_AP2(cur_ref);
  double bp2 = MD_BV2(cur_ref) == 0 ? 0 : MD_BP2(cur_ref);
  double ap3 = MD_AV3(cur_ref) == 0 ? 0 : MD_AP3(cur_ref);
  double bp3 = MD_BV3(cur_ref) == 0 ? 0 : MD_BP3(cur_ref);
  double ap4 = MD_AV4(cur_ref) == 0 ? 0 : MD_AP4(cur_ref);
  double bp4 = MD_BV4(cur_ref) == 0 ? 0 : MD_BP4(cur_ref);

  return (ap1 * MD_AV1(cur_ref) + bp1 * MD_BV1(cur_ref) +
          ap2 * MD_AV2(cur_ref) + bp2 * MD_BV2(cur_ref) +
          ap3 * MD_AV3(cur_ref) + bp3 * MD_BV3(cur_ref) +
          ap4 * MD_AV4(cur_ref) + bp4 * MD_BV4(cur_ref)) /
         (MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
          MD_BV2(cur_ref) + MD_AV3(cur_ref) + MD_BV3(cur_ref) +
          MD_AV4(cur_ref) + MD_BV4(cur_ref));
}

static inline double hp_weighted_price5(const OB* cur_ref) {
  if (0 == MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
               MD_BV2(cur_ref) + MD_AV3(cur_ref) + MD_BV3(cur_ref) +
               MD_AV4(cur_ref) + MD_BV4(cur_ref) + MD_AV5(cur_ref) +
               MD_BV5(cur_ref)) {
    return hp_mid_price(cur_ref);
  }
  double ap1 = MD_AV1(cur_ref) == 0 ? 0 : MD_AP1(cur_ref);
  double bp1 = MD_BV1(cur_ref) == 0 ? 0 : MD_BP1(cur_ref);
  double ap2 = MD_AV2(cur_ref) == 0 ? 0 : MD_AP2(cur_ref);
  double bp2 = MD_BV2(cur_ref) == 0 ? 0 : MD_BP2(cur_ref);
  double ap3 = MD_AV3(cur_ref) == 0 ? 0 : MD_AP3(cur_ref);
  double bp3 = MD_BV3(cur_ref) == 0 ? 0 : MD_BP3(cur_ref);
  double ap4 = MD_AV4(cur_ref) == 0 ? 0 : MD_AP4(cur_ref);
  double bp4 = MD_BV4(cur_ref) == 0 ? 0 : MD_BP4(cur_ref);
  double ap5 = MD_AV5(cur_ref) == 0 ? 0 : MD_AP5(cur_ref);
  double bp5 = MD_BV5(cur_ref) == 0 ? 0 : MD_BP5(cur_ref);

  return (ap1 * MD_AV1(cur_ref) + bp1 * MD_BV1(cur_ref) +
          ap2 * MD_AV2(cur_ref) + bp2 * MD_BV2(cur_ref) +
          ap3 * MD_AV3(cur_ref) + bp3 * MD_BV3(cur_ref) +
          ap4 * MD_AV4(cur_ref) + bp4 * MD_BV4(cur_ref) +
          ap5 * MD_AV5(cur_ref) + bp5 * MD_BV5(cur_ref)) /
         (MD_AV1(cur_ref) + MD_BV1(cur_ref) + MD_AV2(cur_ref) +
          MD_BV2(cur_ref) + MD_AV3(cur_ref) + MD_BV3(cur_ref) +
          MD_AV4(cur_ref) + MD_BV4(cur_ref) + MD_AV5(cur_ref) +
          MD_BV5(cur_ref));
}

static inline double hp_weighted_rtn1(const OB* last_ref, const OB* cur_ref) {
  return hp_weighted_price1(cur_ref) - hp_weighted_price1(last_ref);
}

static inline double hp_weighted_rtn2(const OB* last_ref, const OB* cur_ref) {
  return hp_weighted_price2(cur_ref) - hp_weighted_price2(last_ref);
}

static inline double hp_weighted_rtn3(const OB* last_ref, const OB* cur_ref) {
  return hp_weighted_price3(cur_ref) - hp_weighted_price3(last_ref);
}

static inline double hp_weighted_rtn4(const OB* last_ref, const OB* cur_ref) {
  return hp_weighted_price4(cur_ref) - hp_weighted_price4(last_ref);
}

static inline double hp_weighted_rtn5(const OB* last_ref, const OB* cur_ref) {
  return hp_weighted_price5(cur_ref) - hp_weighted_price5(last_ref);
}

static inline double hp_pct_weighted_rtn1(const OB* last_ref,
                                          const OB* cur_ref) {
  return hp_weighted_rtn1(last_ref, cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_pct_weighted_rtn2(const OB* last_ref,
                                          const OB* cur_ref) {
  return hp_weighted_rtn2(last_ref, cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_pct_weighted_rtn3(const OB* last_ref,
                                          const OB* cur_ref) {
  return hp_weighted_rtn3(last_ref, cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_pct_weighted_rtn4(const OB* last_ref,
                                          const OB* cur_ref) {
  return hp_weighted_rtn4(last_ref, cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_pct_weighted_rtn5(const OB* last_ref,
                                          const OB* cur_ref) {
  return hp_weighted_rtn5(last_ref, cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_pct_weighted_ask(const OB* cur_ref) {
  return hp_weighted_ask(cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_pct_weighted_bid(const OB* cur_ref) {
  return hp_weighted_bid(cur_ref) / hp_mid_price(cur_ref) * 1000;
}

static inline double hp_weighted_ask_rtn(const OB* last_ref,
                                         const OB* cur_ref) {
  return hp_weighted_ask(cur_ref) - hp_weighted_ask(last_ref);
}

static inline double hp_weighted_bid_rtn(const OB* last_ref,
                                         const OB* cur_ref) {
  return hp_weighted_bid(cur_ref) - hp_weighted_bid(last_ref);
}

static inline double hp_pct_weighted_ask_rtn(const OB* last_ref,
                                             const OB* cur_ref) {
  return hp_weighted_ask_rtn(last_ref, cur_ref) / hp_mid_price(last_ref) * 1000;
}

static inline double hp_pct_weighted_bid_rtn(const OB* last_ref,
                                             const OB* cur_ref) {
  return hp_weighted_bid_rtn(last_ref, cur_ref) / hp_mid_price(last_ref) * 1000;
}

static inline double hp_weighted_ask_vol(const OB* cur_ref) {
  return 5 * MD_AV1(cur_ref) + 4 * MD_AV2(cur_ref) + 3 * MD_AV3(cur_ref) +
         2 * MD_AV4(cur_ref) + 1 * MD_AV5(cur_ref);
}

static inline double hp_weighted_bid_vol(const OB* cur_ref) {
  return 5 * MD_BV1(cur_ref) + 4 * MD_BV2(cur_ref) + 3 * MD_BV3(cur_ref) +
         2 * MD_BV4(cur_ref) + 1 * MD_BV5(cur_ref);
}

static inline double hp_weighted_vol_diff_ratio(const OB* cur_ref) {
  double rt = hp_weighted_ask_vol(cur_ref) + hp_weighted_bid_vol(cur_ref);
  if (rt == 0) {
    return 0.0;
  }
  return hp_weighted_ask_vol(cur_ref) / rt - 0.5;
}

static inline double hp_vol_diff_ratio(const OB* cur_ref) {
  double rt = MD_AV1_5(cur_ref) + MD_BV1_5(cur_ref);
  if (rt == 0) {
    return 0;
  }
  return MD_AV1_5(cur_ref) / rt - 0.5;
}

static inline double hp_pct_turnover(const OB* last_ref, const OB* cur_ref) {

  double ap1 = MD_AV1(cur_ref) == 0 ? 0 : MD_AP1(cur_ref);
  double bp1 = MD_BV1(cur_ref) == 0 ? 0 : MD_BP1(cur_ref);
  double ap2 = MD_AV2(cur_ref) == 0 ? 0 : MD_AP2(cur_ref);
  double bp2 = MD_BV2(cur_ref) == 0 ? 0 : MD_BP2(cur_ref);
  double ap3 = MD_AV3(cur_ref) == 0 ? 0 : MD_AP3(cur_ref);
  double bp3 = MD_BV3(cur_ref) == 0 ? 0 : MD_BP3(cur_ref);
  double ap4 = MD_AV4(cur_ref) == 0 ? 0 : MD_AP4(cur_ref);
  double bp4 = MD_BV4(cur_ref) == 0 ? 0 : MD_BP4(cur_ref);
  double ap5 = MD_AV5(cur_ref) == 0 ? 0 : MD_AP5(cur_ref);
  double bp5 = MD_BV5(cur_ref) == 0 ? 0 : MD_BP5(cur_ref);

  double t = ap1 * MD_AV1(cur_ref) + bp1 * MD_BV1(cur_ref) +
             ap2 * MD_AV2(cur_ref) + bp2 * MD_BV2(cur_ref) +
             ap3 * MD_AV3(cur_ref) + bp3 * MD_BV3(cur_ref) +
             ap4 * MD_AV4(cur_ref) + bp4 * MD_BV4(cur_ref) +
             ap5 * MD_AV5(cur_ref) + bp5 * MD_BV5(cur_ref);
  if (t == 0) {
    return 0;
  }

  return (MD_TURNOVER(cur_ref) - MD_TURNOVER(last_ref)) / t;
}

static inline double hp_pct_liquidity_ask(const OB* cur_ref) {
  double rt = MD_AV1_5(cur_ref);
  if (rt == 0) {
    return 0;
  }
  return MD_AV1(cur_ref) / rt;
}

static inline double hp_pct_liquidity_bid(const OB* cur_ref) {
  double rt = MD_BV1_5(cur_ref);
  if (rt == 0) {
    return 0;
  }
  return MD_BV1(cur_ref) / rt;
}