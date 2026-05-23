#include "jpx_tick.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace {

struct Bracket {
    double upper_yen;
    double tick_yen;
};

const std::vector<Bracket> kTopix500 = {
    {1000, 0.1}, {3000, 0.5}, {10000, 1}, {30000, 5}, {100000, 10},
    {300000, 50}, {1000000, 100}, {3000000, 500}, {10000000, 1000},
    {30000000, 5000},
};

const std::vector<Bracket> kEtfUnitOne = {
    {10000, 1}, {30000, 5}, {100000, 10}, {300000, 50}, {1000000, 100},
    {3000000, 500}, {10000000, 1000}, {30000000, 5000},
};

const std::vector<Bracket> kOther = {
    {3000, 1}, {5000, 5}, {30000, 10}, {50000, 50}, {300000, 100},
    {500000, 500}, {3000000, 1000}, {5000000, 5000}, {30000000, 10000},
    {50000000, 50000},
};

const std::vector<Bracket>& table_for(uint32_t table_id) {
    if (table_id == 1) {
        return kTopix500;
    }
    if (table_id == 2) {
        return kEtfUnitOne;
    }
    return kOther;
}

}  // namespace

double tick_size_yen(double price_yen, uint32_t table_id) {
    const auto& brackets = table_for(table_id);
    for (const auto& b : brackets) {
        if (price_yen <= b.upper_yen) {
            return b.tick_yen;
        }
    }
    return brackets.back().tick_yen;
}

double round_to_tick(double price_yen, uint32_t table_id) {
    const double tick = tick_size_yen(price_yen, table_id);
    if (tick <= 0) {
        return price_yen;
    }
    return static_cast<double>(static_cast<int64_t>(price_yen / tick + 0.5)) * tick;
}

uint64_t price_raw_tick_step(uint64_t price_raw, uint32_t table_id) {
    const double yen = static_cast<double>(price_raw) / static_cast<double>(kPriceScale);
    const double tick = tick_size_yen(yen, table_id);
    if (tick <= 0) {
        return 1;
    }
    const double step_raw = tick * static_cast<double>(kPriceScale);
    const auto step = static_cast<uint64_t>(std::llround(step_raw));
    return step > 0 ? step : 1;
}

uint64_t snap_price_raw(uint64_t price_raw, uint32_t table_id) {
    const uint64_t step = price_raw_tick_step(price_raw, table_id);
    return ((price_raw + step / 2) / step) * step;
}

namespace {

int decimal_places_for_tick(double tick_yen) {
    for (int places = 0; places <= 4; ++places) {
        const double scaled = tick_yen * std::pow(10.0, places);
        if (std::fabs(scaled - std::round(scaled)) < 1e-6) {
            return places;
        }
    }
    return 4;
}

}  // namespace

std::string format_price_raw_to_tick(uint64_t price_raw, uint32_t table_id) {
    const double yen = static_cast<double>(snap_price_raw(price_raw, table_id)) /
                       static_cast<double>(kPriceScale);
    const int decimals = decimal_places_for_tick(tick_size_yen(yen, table_id));

    std::ostringstream os;
    os << std::fixed << std::setprecision(decimals) << yen;
    std::string s = os.str();
    if (decimals > 0) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s;
}
