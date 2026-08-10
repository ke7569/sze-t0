#include "snapshot_legacy15_model.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string weights = argc > 1 ? argv[1] : "snapshot_legacy15.bin";
    const std::string scaler = argc > 2 ? argv[2] : "scaler.json";
    const std::size_t iterations = argc > 3 ? static_cast<std::size_t>(std::stoull(argv[3])) : 100000U;
    sze_snapshot15::Model model; std::string error;
    if (!model.load(weights, scaler, &error)) { std::cerr << error << '\n'; return 1; }
    std::array<float, 36> factors{};
    for (std::size_t i = 0; i < factors.size(); ++i) factors[i] = static_cast<float>(i) * 0.001f;
    sze_snapshot15::State state; std::vector<std::uint64_t> timings; timings.reserve(iterations);
    float result = 0.0f;
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        if (!model.predict(factors, &state, &result, &error)) { std::cerr << error << '\n'; return 1; }
        const auto end = std::chrono::steady_clock::now();
        timings.push_back(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }
    std::sort(timings.begin(), timings.end());
    std::uint64_t total = 0; for (std::uint64_t value : timings) total += value;
    const auto percentile = [&](double p) { return timings[static_cast<std::size_t>(p * (timings.size() - 1))]; };
    std::cout << "iterations=" << iterations << " prediction=" << result
              << " avg_ns=" << (total / iterations) << " p50_ns=" << percentile(0.50)
              << " p95_ns=" << percentile(0.95) << " p99_ns=" << percentile(0.99) << '\n';
    return 0;
}
