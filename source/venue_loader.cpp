#include "venue_loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace {

std::string read_file(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Failed to open venue file: " + path;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::optional<std::string> extract_quoted_value(
    const std::string& block, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = block.find(pattern);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    std::size_t q = block.find('"', key_pos + pattern.size());
    if (q == std::string::npos) {
        return std::nullopt;
    }
    std::size_t start = q + 1;
    std::size_t end = block.find('"', start);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return block.substr(start, end - start);
}

std::optional<uint64_t> extract_number_value(
    const std::string& block, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = block.find(pattern);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    std::size_t colon = block.find(':', key_pos + pattern.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t i = colon + 1;
    while (i < block.size() && std::isspace(static_cast<unsigned char>(block[i]))) {
        ++i;
    }
    std::size_t j = i;
    while (j < block.size() && (std::isdigit(static_cast<unsigned char>(block[j])) || block[j] == '-')) {
        ++j;
    }
    if (j == i) {
        return std::nullopt;
    }
    return std::stoull(block.substr(i, j - i));
}

std::optional<uint8_t> parse_security_type(const std::string& block) {
    if (auto quoted = extract_quoted_value(block, "securityType")) {
        const std::string& s = *quoted;
        if (s.size() == 2 && s[0] == '0' && s[1] >= '1' && s[1] <= '4') {
            return static_cast<uint8_t>(s[1] - '0');
        }
        if (s.size() == 1 && s[0] >= '1' && s[0] <= '4') {
            return static_cast<uint8_t>(s[0] - '0');
        }
        return 0;
    }
    if (auto v = extract_number_value(block, "securityType")) {
        if (*v >= 1 && *v <= 4) {
            return static_cast<uint8_t>(*v);
        }
        return 0;
    }
    return std::nullopt;
}

}  // namespace

bool VenueStore::is_stock_security_type(uint8_t security_type) {
    return security_type >= 1 && security_type <= 4;
}

bool VenueStore::load(const std::string& path, std::string& error) {
    const std::string content = read_file(path, error);
    if (content.empty() && !error.empty()) {
        return false;
    }

    instruments_.clear();

    const std::string marker = "\"TseFullInstrument\"";
    std::size_t pos = 0;
    while ((pos = content.find(marker, pos)) != std::string::npos) {
        const std::size_t block_start = pos;
        const std::size_t block_end = std::min(block_start + 4000, content.size());
        const std::string block = content.substr(block_start, block_end - block_start);

        auto symbol = extract_quoted_value(block, "exchSymbol");
        if (!symbol) {
            pos += marker.size();
            continue;
        }

        VenueInstrument inst;
        inst.symbol = *symbol;
        while (!inst.symbol.empty() && inst.symbol.back() == ' ') {
            inst.symbol.pop_back();
        }

        if (auto v = extract_number_value(block, "multicastGroupChannelId")) {
            inst.multicast_group = static_cast<uint32_t>(*v);
        }
        if (auto v = extract_number_value(block, "tickSizeTable")) {
            inst.tick_size_table = static_cast<uint32_t>(*v);
        }
        if (auto v = extract_number_value(block, "unitOfTrading")) {
            inst.unit_of_trading = *v;
        }
        if (auto st = parse_security_type(block)) {
            inst.security_type = *st;
        }
        if (auto v = extract_number_value(block, "basePrice")) {
            inst.base_price_yen = *v;
        }
        if (auto v = extract_number_value(block, "minPrice")) {
            inst.min_price_yen = *v;
        }
        if (auto v = extract_number_value(block, "maxPrice")) {
            inst.max_price_yen = *v;
        }

        instruments_[inst.symbol] = inst;
        pos += marker.size();
    }

    if (instruments_.empty()) {
        error = "No instruments found in venue file";
        return false;
    }
    return true;
}

const VenueInstrument* VenueStore::find(const std::string& symbol) const {
    const auto it = instruments_.find(symbol);
    if (it == instruments_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<std::string> VenueStore::symbols_for_channel(uint32_t channel) const {
    std::vector<std::string> out;
    for (const auto& [sym, inst] : instruments_) {
        if (inst.multicast_group == channel) {
            out.push_back(sym);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> VenueStore::stock_symbols_for_channel(uint32_t channel) const {
    std::vector<std::string> out;
    for (const auto& [sym, inst] : instruments_) {
        if (inst.multicast_group == channel && inst.is_stock()) {
            out.push_back(sym);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> VenueStore::all_stock_symbols() const {
    std::vector<std::string> out;
    for (const auto& [sym, inst] : instruments_) {
        if (inst.is_stock()) {
            out.push_back(sym);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<uint32_t> VenueStore::detect_channel_from_name(const std::string& name) const {
    static const std::regex pattern(R"(_(\d{2,3})\.)");
    std::smatch match;
    if (std::regex_search(name, match, pattern)) {
        return static_cast<uint32_t>(std::stoul(match[1].str()));
    }
    std::unordered_map<uint32_t, std::size_t> counts;
    for (const auto& [sym, inst] : instruments_) {
        (void)sym;
        counts[inst.multicast_group]++;
    }
    if (counts.size() == 1) {
        return counts.begin()->first;
    }
    return std::nullopt;
}
