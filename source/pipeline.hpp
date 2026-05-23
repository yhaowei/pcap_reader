#pragma once

#include "auction.hpp"
#include "order_book.hpp"
#include "venue_loader.hpp"

#include <ostream>
#include <string>
#include <vector>

struct PipelineOutput {
    std::vector<std::string> pcap_files;
    std::vector<AuctionResult> results;
    OrderBookManager books;
};

bool run_pipeline(
    const std::string& venue_path,
    const std::vector<std::string>& pcap_paths,
    PipelineOutput& out,
    std::string& error,
    uint32_t channel_filter = 0,
    bool show_pcap_progress = false);

void write_auction_csv(const std::vector<AuctionResult>& results, const std::string& path, std::string& error);

void export_pipeline_json(
    const PipelineOutput& output,
    std::ostream& os,
    std::size_t book_depth = 0);
