#pragma once

#include "order_book.hpp"
#include "venue_loader.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

struct AuctionResult {
    std::string symbol;
    std::string iap;
    uint64_t iav = 0;
    bool valid = false;
};

AuctionResult compute_indicative_auction(
    const std::string& symbol,
    const IssueOrderBook& book,
    const VenueInstrument* instrument);

void export_auction_result_json(const AuctionResult& result, std::ostream& os);

std::vector<AuctionResult> compute_all_auctions(
    const OrderBookManager& books,
    const VenueStore& venue,
    uint32_t channel_filter = 0);
