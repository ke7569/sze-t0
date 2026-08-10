#ifndef SZE_SNAPSHOT_LEGACY15_FACTORS_H
#define SZE_SNAPSHOT_LEGACY15_FACTORS_H

#include <array>
#include <cstdint>
#include <string>

namespace sze_snapshot15 {

struct Snapshot {
    std::string symbol;
    std::string trading_day;
    std::uint64_t exchange_time_ms = 0;
    double last_price = 0.0;
    std::int64_t volume = 0;
    double turnover = 0.0;
    std::array<double, 5> ask_prices{};
    std::array<double, 5> bid_prices{};
    std::array<std::int64_t, 5> ask_volumes{};
    std::array<std::int64_t, 5> bid_volumes{};
};

bool valid_snapshot(const Snapshot& value);
bool build_factors(const Snapshot& start, const Snapshot& current,
                   std::array<float, 36>* output);

}  // namespace sze_snapshot15
#endif
