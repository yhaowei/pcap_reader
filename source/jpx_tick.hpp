#pragma once

#include <cstdint>
#include <string>

// JPX tick size tables: https://www.jpx.co.jp/english/equities/trading/domestic/07.html

constexpr uint64_t kPriceScale = 10000;

double tick_size_yen(double price_yen, uint32_t table_id);
double round_to_tick(double price_yen, uint32_t table_id);

// FLEX prices use 1/10000 yen units (same scale as order book raw prices).
uint64_t price_raw_tick_step(uint64_t price_raw, uint32_t table_id);
uint64_t snap_price_raw(uint64_t price_raw, uint32_t table_id);

// Format a tick-aligned raw price with the correct number of decimal places.
std::string format_price_raw_to_tick(uint64_t price_raw, uint32_t table_id);
