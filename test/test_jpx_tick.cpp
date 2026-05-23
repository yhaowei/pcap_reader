#include "jpx_tick.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

constexpr uint64_t kScale = 10000;

uint64_t yen(uint64_t y) {
    return y * kScale;
}

}  // namespace

int main() {
    expect(std::fabs(tick_size_yen(500, 1) - 0.1) < 1e-9, "table1 low tick");
    expect(std::fabs(tick_size_yen(1000, 1) - 0.1) < 1e-9, "table1 at 1000");
    expect(std::fabs(tick_size_yen(1000.1, 1) - 0.5) < 1e-9, "table1 above 1000");

    expect(snap_price_raw(yen(1000) + 2000, 1) == yen(1000), "snap down to 1000 tick");
    expect(format_price_raw_to_tick(yen(1742) + 5000, 1) == "1742.5", "format half-yen tick");

    expect(std::fabs(tick_size_yen(2000, 3) - 1.0) < 1e-9, "table3 default tick");
    expect(format_price_raw_to_tick(yen(100), 3) == "100", "table3 whole yen");

    std::cout << "test_jpx_tick: all passed\n";
    return 0;
}
