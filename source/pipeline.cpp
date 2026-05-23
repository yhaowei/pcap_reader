#include "pipeline.hpp"

#include "pcap_io.hpp"

#include <fstream>
#include <iostream>

namespace {

void json_escape(std::ostream& os, const std::string& s) {
    for (char c : s) {
        if (c == '"' || c == '\\') {
            os << '\\';
        }
        os << c;
    }
}

}  // namespace

bool run_pipeline(
    const std::string& venue_path,
    const std::vector<std::string>& pcap_paths,
    PipelineOutput& out,
    std::string& error,
    uint32_t channel_filter,
    bool show_pcap_progress) {
    VenueStore venue;
    if (!venue.load(venue_path, error)) {
        return false;
    }

    if (channel_filter == 0 && !pcap_paths.empty()) {
        const auto detected = venue.detect_channel_from_name(pcap_paths.front());
        if (detected) {
            channel_filter = *detected;
        }
    }

    PcapFile pcap;
    std::vector<std::string> loaded_paths;
    PcapLoadOptions load_opts;
    load_opts.show_progress = show_pcap_progress;
    if (!read_pcaps(pcap_paths, pcap, error, &loaded_paths, &load_opts)) {
        return false;
    }

    out.pcap_files = std::move(loaded_paths);
    out.books.clear();
    out.results.clear();

    if (show_pcap_progress) {
        std::cerr << kPcapProgressPrefix << " Building order books from "
                  << pcap.packets.size() << " packets" << std::endl;
    }

    for (const PcapPacket& pkt : pcap.packets) {
        const auto udp = extract_udp_payload(pkt.data, pcap.link_type);
        if (!udp || udp->payload.size() <= kFlexPacketHeaderLen) {
            continue;
        }
        bool maintenance = true;
        for (std::size_t i = kFlexPacketHeaderLen; i < udp->payload.size(); ++i) {
            if (udp->payload[i] != ' ') {
                maintenance = false;
                break;
            }
        }
        if (maintenance) {
            continue;
        }

        ParsedFlexMessage msg;
        if (!parse_flex_message(*udp, msg, error)) {
            continue;
        }
        out.books.apply_message(msg);
    }

    if (show_pcap_progress) {
        std::cerr << kPcapProgressPrefix << " Computing IAP/IAV" << std::endl;
    }
    out.results = compute_all_auctions(out.books, venue, channel_filter);
    if (show_pcap_progress) {
        std::cerr << kPcapProgressPrefix << " Done" << std::endl;
    }
    return true;
}

void write_auction_csv(const std::vector<AuctionResult>& results, const std::string& path, std::string& error) {
    std::ofstream out(path);
    if (!out) {
        error = "Failed to write CSV: " + path;
        return;
    }
    out << "symbol,iap,iav\n";
    for (const AuctionResult& row : results) {
        out << row.symbol << ',';
        if (row.valid) {
            out << row.iap;
        }
        out << ',' << row.iav << '\n';
    }
}

void export_pipeline_json(
    const PipelineOutput& output,
    std::ostream& os,
    std::size_t book_depth) {
    os << "{\"pcap_files\":[";
    for (std::size_t i = 0; i < output.pcap_files.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        os << '"';
        json_escape(os, output.pcap_files[i]);
        os << '"';
    }
    os << "],\"results\":[";
    for (std::size_t i = 0; i < output.results.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        export_auction_result_json(output.results[i], os);
    }
    os << "],\"books\":{";
    bool first = true;
    for (const auto& [issue, book] : output.books.books()) {
        if (!first) {
            os << ',';
        }
        first = false;
        os << '"';
        json_escape(os, issue);
        os << "\":{";
        book.export_json(os, book_depth);
        os << '}';
    }
    os << "}}\n";
}
