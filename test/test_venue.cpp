#include "venue_loader.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const std::filesystem::path fixtures =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "venue_mini.json";

    VenueStore venue;
    std::string error;
    expect(venue.load(fixtures.string(), error), error.c_str());

    const VenueInstrument* inst = venue.find("TEST");
    expect(inst != nullptr, "TEST not found");
    expect(inst->symbol == "TEST", "symbol mismatch");
    expect(inst->multicast_group == 51, "channel mismatch");
    expect(inst->tick_size_table == 3, "tick table mismatch");
    expect(inst->base_price_yen == 100, "base price mismatch");
    expect(inst->min_price_yen == 50, "min price mismatch");
    expect(inst->max_price_yen == 150, "max price mismatch");

    const auto symbols = venue.symbols_for_channel(51);
    expect(!symbols.empty(), "expected symbols on channel 51");
    expect(symbols.front() == "TEST", "TEST should be on channel 51");

    const auto ch = venue.detect_channel_from_name("20241105_051.test.pcap");
    expect(ch.has_value() && *ch == 51, "channel detect from filename");

    std::cout << "test_venue: all passed\n";
    return 0;
}
