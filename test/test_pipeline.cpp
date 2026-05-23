#include "auction.hpp"
#include "flex_tags.hpp"
#include "pipeline.hpp"
#include "venue_loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

constexpr uint64_t kPriceScale = 10000;

void add_order(IssueOrderBook& book, uint32_t id, char side, uint64_t yen, uint64_t qty) {
    ATag tag;
    tag.order_id = id;
    tag.side = side;
    tag.quantity = qty;
    tag.price = yen * kPriceScale;
    book.add(tag);
}

}  // namespace

int main() {
    const std::filesystem::path fixtures =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "venue_mixed.json";

    VenueStore venue;
    std::string error;
    expect(venue.load(fixtures.string(), error), error.c_str());

    const auto stocks = venue.stock_symbols_for_channel(51);
    expect(stocks.size() == 2, "two stocks on channel 51");
    expect(stocks[0] == "ALT" && stocks[1] == "TEST", "sorted stock symbols");

    const VenueInstrument* bond = venue.find("BOND");
    expect(bond != nullptr && !bond->is_stock(), "bond excluded from stocks");

    const VenueInstrument* test_inst = venue.find("TEST");
    expect(test_inst != nullptr && test_inst->unit_of_trading == 100, "unit of trading loaded");

    OrderBookManager books;
    add_order(books.book_for("TEST"), 1, 'B', 100, 100);
    add_order(books.book_for("TEST"), 2, 'S', 100, 150);
  // BOND has PCAP activity but must not appear in stock auction output
    add_order(books.book_for("BOND"), 3, 'B', 100, 100);
    add_order(books.book_for("BOND"), 4, 'S', 100, 100);

    const std::vector<AuctionResult> results = compute_all_auctions(books, venue, 51);
    expect(results.size() == 2, "CSV scope is all channel stocks");

    bool found_test = false;
    bool found_alt = false;
    for (const AuctionResult& row : results) {
        expect(row.symbol != "BOND", "bond must not be in stock results");
        if (row.symbol == "TEST") {
            found_test = true;
            expect(row.valid, "TEST should have valid IAP");
            expect(row.iav == 100, "TEST matched volume");
        }
        if (row.symbol == "ALT") {
            found_alt = true;
            expect(!row.valid, "ALT has no book activity");
            expect(row.iav == 0, "ALT IAV zero");
        }
    }
    expect(found_test && found_alt, "both channel stocks present");

    const std::filesystem::path csv_path =
        std::filesystem::temp_directory_path() / "mbo_pipeline_test.csv";
    write_auction_csv(results, csv_path.string(), error);
    expect(error.empty(), error.c_str());

    std::ifstream in(csv_path);
    std::string header;
    expect(static_cast<bool>(std::getline(in, header)), "csv header line");
    expect(header == "symbol,iap,iav", "csv header format");

    std::string line;
    int rows = 0;
    while (std::getline(in, line)) {
        ++rows;
        expect(line.find(',') != std::string::npos, "csv row has commas");
    }
    expect(rows == 2, "csv row count");

    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto p051 = root / "20241105_051.test.pcap";
    if (std::filesystem::exists(p051)) {
        PipelineOutput output;
        expect(
            run_pipeline(
                (root / "TseVenue.20241105.json").string(),
                {p051.string()},
                output,
                error,
                51,
                false),
            error.c_str());
        VenueStore full_venue;
        expect(full_venue.load((root / "TseVenue.20241105.json").string(), error), error.c_str());
        expect(
            output.results.size() == full_venue.stock_symbols_for_channel(51).size(),
            "channel 51 stock count from venue");
        std::size_t valid = 0;
        for (const AuctionResult& row : output.results) {
            if (row.valid) {
                ++valid;
            }
            expect(row.symbol != "BOND", "real run excludes non-stocks");
        }
        expect(valid > 0, "some symbols have valid IAP from sample PCAP");
    }

    std::cout << "test_pipeline: all passed\n";
    return 0;
}
