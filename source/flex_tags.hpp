#pragma once

#include "flex_protocol.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

constexpr uint64_t kMarketPrice = std::numeric_limits<uint64_t>::max();

struct ATag {
    uint32_t time_us = 0;
    uint32_t order_id = 0;
    char side = '\0';
    uint64_t quantity = 0;
    uint64_t price = 0;
    uint8_t order_condition = 0;
    uint8_t modification_flag = 0;
};

struct DTag {
    uint32_t time_us = 0;
    uint32_t order_id = 0;
    char side = '\0';
    uint8_t modification_flag = 0;
};

struct ETag {
    uint32_t time_us = 0;
    uint32_t order_id = 0;
    char side = '\0';
    uint64_t volume = 0;
    uint32_t match_id = 0;
};

struct CTag {
    uint32_t time_us = 0;
    uint32_t order_id = 0;
    char side = '\0';
    uint64_t volume = 0;
    uint32_t match_id = 0;
    uint64_t execution_price = 0;
};

std::optional<ATag> parse_a_tag(const FlexTag& tag);
std::optional<DTag> parse_d_tag(const FlexTag& tag);
std::optional<ETag> parse_e_tag(const FlexTag& tag);
std::optional<CTag> parse_c_tag(const FlexTag& tag);
std::optional<uint8_t> parse_r_tag_phase(const FlexTag& tag);

std::string format_price_raw(uint64_t raw);
bool is_market_price(uint64_t raw);
