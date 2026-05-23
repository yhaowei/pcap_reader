#include "auction.hpp"
#include "flex_tags.hpp"
#include "venue_loader.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

constexpr uint64_t kPriceScale = 10000;

uint64_t yen_to_raw(uint64_t yen) {
    return yen * kPriceScale;
}

void add_order(IssueOrderBook& book, uint32_t id, char side, uint64_t yen, uint64_t qty) {
    ATag tag;
    tag.order_id = id;
    tag.side = side;
    tag.quantity = qty;
    tag.price = yen_to_raw(yen);
    book.add(tag);
}

void add_order_raw(IssueOrderBook& book, uint32_t id, char side, uint64_t price_raw, uint64_t qty) {
    ATag tag;
    tag.order_id = id;
    tag.side = side;
    tag.quantity = qty;
    tag.price = price_raw;
    book.add(tag);
}

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

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

int main() {
    const std::filesystem::path fixtures =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "venue_mini.json";

    VenueStore venue;
    std::string error;
    expect(venue.load(fixtures.string(), error), error.c_str());

    const VenueInstrument* inst = venue.find("TEST");
    expect(inst != nullptr, "TEST instrument missing");

    IssueOrderBook book("TEST");
    add_order(book, 1, 'B', 100, 100);
    add_order(book, 2, 'B', 99, 200);
    add_order(book, 3, 'S', 100, 150);
    add_order(book, 4, 'S', 101, 50);

    AuctionResult result = compute_indicative_auction("TEST", book, inst);
    expect(result.valid, "expected valid IAP for crossing book");
    expect(result.iav > 0, "expected positive IAV");
    expect(result.iap == "100", "IAP should be on tick grid (whole yen)");

    IssueOrderBook off_tick("TEST");
    const uint64_t off_grid = yen_to_raw(100) + 1;
    add_order_raw(off_tick, 20, 'B', off_grid, 100);
    add_order_raw(off_tick, 21, 'S', off_grid, 100);
    AuctionResult snapped = compute_indicative_auction("TEST", off_tick, inst);
    expect(snapped.valid, "off-tick orders should still produce valid IAP");
    expect(snapped.iap == "100", "IAP snapped to nearest tick");

    expect(prefer_on_final_tie(100, 99, 100), "reference price wins final tie vs lower");
    expect(!prefer_on_final_tie(99, 100, 100), "lower price loses when ref is incumbent");
    expect(prefer_on_final_tie(101, 99, 100), "symmetric tie chooses higher price");
    expect(!prefer_on_final_tie(99, 101, 100), "symmetric tie does not choose lower");

    IssueOrderBook symmetric("TEST");
    add_order(symmetric, 30, 'B', 99, 100);
    add_order(symmetric, 31, 'B', 101, 100);
    add_order(symmetric, 32, 'S', 99, 100);
    add_order(symmetric, 33, 'S', 101, 100);
    AuctionResult sym = compute_indicative_auction("TEST", symmetric, inst);
    expect(sym.valid, "symmetric book around base should be valid");
    expect(sym.iap == "100", "IAP at reference when book crosses base (min imbalance)");

    IssueOrderBook bids_only("TEST");
    add_order(bids_only, 10, 'B', 99, 100);
    AuctionResult no_asks = compute_indicative_auction("TEST", bids_only, inst);
    expect(!no_asks.valid, "book without asks should be invalid");
    expect(no_asks.iav == 0, "IAV should be zero without asks");

    IssueOrderBook asks_only("TEST");
    add_order(asks_only, 11, 'S', 100, 100);
    AuctionResult no_bids = compute_indicative_auction("TEST", asks_only, inst);
    expect(!no_bids.valid, "book without bids should be invalid");

    AuctionResult unknown = compute_indicative_auction("UNKNOWN", book, nullptr);
    expect(!unknown.valid, "unknown symbol should be invalid");

    IssueOrderBook market_book("TEST");
    add_order_raw(market_book, 40, 'B', kMarketPrice, 100);
    add_order(market_book, 41, 'S', 100, 100);
    AuctionResult market = compute_indicative_auction("TEST", market_book, inst);
    expect(market.valid, "market bid with limit ask should be valid");
    expect(market.iav > 0, "market order book produces volume");

    std::cout << "test_auction: all passed\n";
    return 0;
}
