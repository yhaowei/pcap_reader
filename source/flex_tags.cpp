#include "flex_tags.hpp"

#include <iomanip>
#include <limits>
#include <sstream>

namespace {

uint32_t read_be_u32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint64_t read_be_u64(const uint8_t* p) {
    return (static_cast<uint64_t>(read_be_u32(p)) << 32) | read_be_u32(p + 4);
}

uint64_t read_be_u48(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 40) |
           (static_cast<uint64_t>(p[1]) << 32) |
           (static_cast<uint64_t>(p[2]) << 24) |
           (static_cast<uint64_t>(p[3]) << 16) |
           (static_cast<uint64_t>(p[4]) << 8) | p[5];
}

}  // namespace

std::string format_price_raw(uint64_t raw) {
    if (is_market_price(raw)) {
        return "MARKET";
    }
    const uint64_t whole = raw / 10000;
    const uint64_t frac = raw % 10000;
    std::ostringstream os;
    os << whole << '.' << std::setw(4) << std::setfill('0') << frac;
    return os.str();
}

bool is_market_price(uint64_t raw) {
    return raw == std::numeric_limits<uint64_t>::max();
}

std::optional<ATag> parse_a_tag(const FlexTag& tag) {
    if (tag.type != 'A' || tag.data.size() < 26) {
        return std::nullopt;
    }
    const uint8_t* d = tag.data.data();
    ATag a;
    a.time_us = read_be_u32(d + 1);
    a.order_id = read_be_u32(d + 5);
    a.side = static_cast<char>(d[9]);
    a.quantity = read_be_u48(d + 10);
    a.price = read_be_u64(d + 16);
    a.order_condition = d[24];
    a.modification_flag = d[25];
    if (a.side != 'B' && a.side != 'S') {
        return std::nullopt;
    }
    return a;
}

std::optional<DTag> parse_d_tag(const FlexTag& tag) {
    if (tag.type != 'D' || tag.data.size() < 11) {
        return std::nullopt;
    }
    const uint8_t* d = tag.data.data();
    DTag out;
    out.time_us = read_be_u32(d + 1);
    out.order_id = read_be_u32(d + 5);
    out.side = static_cast<char>(d[9]);
    out.modification_flag = d[10];
    if (out.side != 'B' && out.side != 'S') {
        return std::nullopt;
    }
    return out;
}

std::optional<ETag> parse_e_tag(const FlexTag& tag) {
    if (tag.type != 'E' || tag.data.size() < 20) {
        return std::nullopt;
    }
    const uint8_t* d = tag.data.data();
    ETag out;
    out.time_us = read_be_u32(d + 1);
    out.order_id = read_be_u32(d + 5);
    out.side = static_cast<char>(d[9]);
    out.volume = read_be_u48(d + 10);
    out.match_id = read_be_u32(d + 16);
    if (out.side != 'B' && out.side != 'S') {
        return std::nullopt;
    }
    return out;
}

std::optional<CTag> parse_c_tag(const FlexTag& tag) {
    if (tag.type != 'C' || tag.data.size() < 29) {
        return std::nullopt;
    }
    const uint8_t* d = tag.data.data();
    CTag out;
    out.time_us = read_be_u32(d + 1);
    out.order_id = read_be_u32(d + 5);
    out.side = static_cast<char>(d[9]);
    out.volume = read_be_u48(d + 10);
    out.match_id = read_be_u32(d + 16);
    out.execution_price = read_be_u64(d + 20);
    if (out.side != 'B' && out.side != 'S') {
        return std::nullopt;
    }
    return out;
}

std::optional<uint8_t> parse_r_tag_phase(const FlexTag& tag) {
    if (tag.type != 'R' || tag.data.empty()) {
        return std::nullopt;
    }
    const uint8_t phase = tag.data[0];
    if (phase == 1 || phase == 2) {
        return phase;
    }
    if (tag.data.size() >= 1 && tag.data[0] == 'R' && tag.data.size() >= 2) {
        const uint8_t v = tag.data[1];
        if (v == 1 || v == 2) {
            return v;
        }
    }
    return std::nullopt;
}
