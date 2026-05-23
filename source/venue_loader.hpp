#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct VenueInstrument {
    std::string symbol;
    uint32_t multicast_group = 0;
    uint32_t tick_size_table = 3;
    uint64_t unit_of_trading = 1;
    uint8_t security_type = 0;
    uint64_t base_price_yen = 0;
    uint64_t min_price_yen = 0;
    uint64_t max_price_yen = 0;

    bool is_stock() const { return security_type >= 1 && security_type <= 4; }
};

class VenueStore {
public:
    bool load(const std::string& path, std::string& error);

    const VenueInstrument* find(const std::string& symbol) const;
    const std::unordered_map<std::string, VenueInstrument>& instruments() const {
        return instruments_;
    }

    std::vector<std::string> symbols_for_channel(uint32_t channel) const;
    std::vector<std::string> stock_symbols_for_channel(uint32_t channel) const;
    std::vector<std::string> all_stock_symbols() const;

    static bool is_stock_security_type(uint8_t security_type);

    std::optional<uint32_t> detect_channel_from_name(const std::string& name) const;

private:
    std::unordered_map<std::string, VenueInstrument> instruments_;
};
