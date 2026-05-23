#include "auction.hpp"

#include "flex_tags.hpp"
#include "jpx_tick.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace {

uint64_t effective_price(uint64_t raw, Side side, const VenueInstrument& inst) {
    if (raw != kMarketPrice) {
        return raw;
    }
    if (side == Side::Buy) {
        return static_cast<uint64_t>(inst.max_price_yen) * kPriceScale;
    }
    return static_cast<uint64_t>(inst.min_price_yen) * kPriceScale;
}

uint64_t tick_price(uint64_t raw, const VenueInstrument& inst) {
    return snap_price_raw(raw, inst.tick_size_table);
}

void accumulate(
    const IssueOrderBook& book,
    const VenueInstrument& inst,
    std::map<uint64_t, uint64_t, std::greater<uint64_t>>& bid_qty,
    std::map<uint64_t, uint64_t>& ask_qty) {
    for (const auto& [id, order] : book.orders()) {
        (void)id;
        const uint64_t px = tick_price(effective_price(order.price, order.side, inst), inst);
        if (is_market_price(px)) {
            continue;
        }
        if (order.side == Side::Buy) {
            bid_qty[px] += order.quantity;
        } else {
            ask_qty[px] += order.quantity;
        }
    }
}

void add_tick_candidates(
    std::set<uint64_t>& candidates,
    uint64_t low_raw,
    uint64_t high_raw,
    uint32_t tick_table) {
    if (low_raw > high_raw) {
        return;
    }
    const uint64_t step = price_raw_tick_step((low_raw + high_raw) / 2, tick_table);
    const uint64_t start = snap_price_raw(low_raw, tick_table);
    const uint64_t end = snap_price_raw(high_raw, tick_table);
    for (uint64_t px = start; px <= end; px += step) {
        candidates.insert(px);
    }
}

uint64_t buy_at_or_above(
    const std::map<uint64_t, uint64_t, std::greater<uint64_t>>& bid_qty, uint64_t price) {
    uint64_t total = 0;
    for (const auto& [px, qty] : bid_qty) {
        if (px >= price) {
            total += qty;
        }
    }
    return total;
}

uint64_t sell_at_or_below(const std::map<uint64_t, uint64_t>& ask_qty, uint64_t price) {
    uint64_t total = 0;
    for (const auto& [px, qty] : ask_qty) {
        if (px <= price) {
            total += qty;
        }
    }
    return total;
}

// TSE Itayose final tie (after max volume, min imbalance, min |P - ref|):
// use reference price if it is among the tied candidates; otherwise if |P-ref| is
// equal on both sides (symmetric), choose the higher price.
bool prefer_on_final_tie(uint64_t price, uint64_t best_price, uint64_t ref) {
    if (ref > 0 && price == ref) {
        return true;
    }
    if (ref > 0 && best_price == ref) {
        return false;
    }
    return price > best_price;
}

}  // namespace

// TSE Itayose price selection (simplified):
// 1) Max matched volume  2) Min |Buy-Sell|  3) Min |P - reference|
// 4) Reference price if tied; else higher price (not lower) when |P-ref| is equal.
AuctionResult compute_indicative_auction(
    const std::string& symbol,
    const IssueOrderBook& book,
    const VenueInstrument* instrument) {
    AuctionResult out;
    out.symbol = symbol;
    if (!instrument) {
        return out;
    }

    std::map<uint64_t, uint64_t, std::greater<uint64_t>> bid_qty;
    std::map<uint64_t, uint64_t> ask_qty;
    accumulate(book, *instrument, bid_qty, ask_qty);

    if (bid_qty.empty() || ask_qty.empty()) {
        return out;
    }

    uint64_t low_raw = std::numeric_limits<uint64_t>::max();
    uint64_t high_raw = 0;
    if (!bid_qty.empty()) {
        low_raw = std::min(low_raw, bid_qty.rbegin()->first);
        high_raw = std::max(high_raw, bid_qty.begin()->first);
    }
    if (!ask_qty.empty()) {
        low_raw = std::min(low_raw, ask_qty.begin()->first);
        high_raw = std::max(high_raw, ask_qty.rbegin()->first);
    }
    if (instrument->min_price_yen > 0) {
        low_raw = std::min(
            low_raw, tick_price(static_cast<uint64_t>(instrument->min_price_yen) * kPriceScale, *instrument));
    }
    if (instrument->max_price_yen > 0) {
        high_raw = std::max(
            high_raw, tick_price(static_cast<uint64_t>(instrument->max_price_yen) * kPriceScale, *instrument));
    }

    std::set<uint64_t> candidates;
    if (low_raw <= high_raw) {
        add_tick_candidates(candidates, low_raw, high_raw, instrument->tick_size_table);
    }

    const uint64_t ref = instrument->base_price_yen > 0
                             ? tick_price(
                                   static_cast<uint64_t>(instrument->base_price_yen) * kPriceScale,
                                   *instrument)
                             : 0;

    uint64_t best_price = 0;
    uint64_t best_vol = 0;
    uint64_t best_imb = std::numeric_limits<uint64_t>::max();
    uint64_t best_ref_dist = std::numeric_limits<uint64_t>::max();
    bool have_best = false;

    for (uint64_t price : candidates) {
        const uint64_t buy = buy_at_or_above(bid_qty, price);
        const uint64_t sell = sell_at_or_below(ask_qty, price);
        const uint64_t matched = std::min(buy, sell);
        if (matched == 0) {
            continue;
        }

        const uint64_t imb = buy > sell ? buy - sell : sell - buy;
        const uint64_t ref_dist = ref > 0 ? (price > ref ? price - ref : ref - price) : 0;

        const bool better = !have_best || matched > best_vol ||
                            (matched == best_vol && imb < best_imb) ||
                            (matched == best_vol && imb == best_imb && ref_dist < best_ref_dist) ||
                            (matched == best_vol && imb == best_imb && ref_dist == best_ref_dist &&
                             prefer_on_final_tie(price, best_price, ref));

        if (better) {
            have_best = true;
            best_price = price;
            best_vol = matched;
            best_imb = imb;
            best_ref_dist = ref_dist;
        }
    }

    if (!have_best || best_vol == 0) {
        return out;
    }

    out.valid = true;
    best_price = snap_price_raw(best_price, instrument->tick_size_table);
    if (best_price == kMarketPrice) {
        out.iap = "MARKET";
    } else {
        out.iap = format_price_raw_to_tick(best_price, instrument->tick_size_table);
    }
    out.iav = best_vol;
    return out;
}

void export_auction_result_json(const AuctionResult& result, std::ostream& os) {
    os << "{\"symbol\":\"" << result.symbol << "\",\"iap\":\"";
    for (char c : result.iap) {
        if (c == '"' || c == '\\') {
            os << '\\';
        }
        os << c;
    }
    os << "\",\"iav\":" << result.iav << ",\"valid\":" << (result.valid ? "true" : "false") << '}';
}

std::vector<AuctionResult> compute_all_auctions(
    const OrderBookManager& books,
    const VenueStore& venue,
    uint32_t channel_filter) {
    std::vector<AuctionResult> results;

    const std::vector<std::string> symbols = channel_filter != 0
                                                 ? venue.stock_symbols_for_channel(channel_filter)
                                                 : venue.all_stock_symbols();

    results.reserve(symbols.size());
    for (const std::string& symbol : symbols) {
        const VenueInstrument* inst = venue.find(symbol);
        if (inst == nullptr || !inst->is_stock()) {
            continue;
        }
        const IssueOrderBook* b = books.find_book(symbol);
        if (b != nullptr) {
            results.push_back(compute_indicative_auction(symbol, *b, inst));
        } else {
            AuctionResult empty;
            empty.symbol = symbol;
            results.push_back(empty);
        }
    }

    return results;
}
