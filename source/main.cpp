#include "flex_protocol.hpp"
#include "order_book.hpp"
#include "pcap_io.hpp"
#include "pipeline.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string pcap_dir;
    std::vector<std::string> pcap_paths;
    std::string venue_path;
    std::string csv_out;
    std::size_t max_print = 20;
    bool print_all = false;
    std::size_t book_depth = 0;
    std::string show_issue;
    std::string history_issue;
    bool list_issues = false;
    bool export_books_json = false;
    bool export_pipeline_json = false;
    bool quiet = false;
    bool show_progress = false;
};

const char* link_type_name(uint32_t link_type) {
    switch (link_type) {
        case 0:
            return "NULL/BSD loopback";
        case 1:
            return "Ethernet";
        case 101:
            return "Raw IP";
        case 113:
            return "Linux cooked capture";
        default:
            return "unknown";
    }
}

bool parse_args(int argc, char* argv[], Options& opts) {
    if (argc < 2) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") {
            opts.print_all = true;
        } else if (arg == "--quiet") {
            opts.quiet = true;
        } else if (arg == "--progress") {
            opts.show_progress = true;
        } else if (arg == "--list-issues") {
            opts.list_issues = true;
            opts.quiet = true;
        } else if (arg == "--export-books-json") {
            opts.export_books_json = true;
            opts.quiet = true;
        } else if (arg == "--export-pipeline-json") {
            opts.export_pipeline_json = true;
            opts.quiet = true;
        } else if (arg == "--pcap-dir" && i + 1 < argc) {
            opts.pcap_dir = argv[++i];
        } else if (arg == "--venue" && i + 1 < argc) {
            opts.venue_path = argv[++i];
        } else if (arg == "--csv-out" && i + 1 < argc) {
            opts.csv_out = argv[++i];
        } else if (arg == "--max" && i + 1 < argc) {
            opts.max_print = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--depth" && i + 1 < argc) {
            opts.book_depth = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "--issue" && i + 1 < argc) {
            opts.show_issue = argv[++i];
        } else if (arg == "--history-json" && i + 1 < argc) {
            opts.history_issue = argv[++i];
            opts.quiet = true;
        } else if (arg == "--help" || arg == "-h") {
            return false;
        } else if (arg.rfind('-', 0) == 0) {
            std::cerr << "Unknown option: " << arg << '\n';
            return false;
        } else {
            opts.pcap_paths.push_back(arg);
        }
    }

    if (!opts.pcap_dir.empty()) {
        opts.pcap_paths = list_pcap_files_in_directory(opts.pcap_dir);
    }

    return !opts.pcap_paths.empty();
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <file.pcap> [more.pcap ...] [options]\n"
              << "       " << prog << " --pcap-dir <folder> [options]\n"
              << "  --venue FILE          TseVenue JSON (required for IAP/IAV)\n"
              << "  --export-pipeline-json  Full pipeline JSON (results + books)\n"
              << "  --csv-out FILE        Write symbol,iap,iav CSV\n"
              << "  --export-books-json   Final order books as JSON\n"
              << "  --history-json CODE   Per-update snapshots (JSONL)\n"
              << "  --list-issues         Issue codes in PCAP (JSON array)\n"
              << "  --pcap-dir DIR        Load all .pcap files, merge by timestamp\n"
              << "  --quiet               Suppress human-readable output\n"
              << "  --progress            PCAP load progress on stderr\n"
              << "  --issue CODE          Show final book for one issue\n"
              << "  --depth N             Book depth (0 = all levels)\n";
}

bool is_maintenance_payload(const std::vector<uint8_t>& payload) {
    if (payload.size() <= kFlexPacketHeaderLen) {
        return true;
    }
    for (std::size_t i = kFlexPacketHeaderLen; i < payload.size(); ++i) {
        if (payload[i] != ' ') {
            return false;
        }
    }
    return true;
}

std::string tag_summary(const ParsedFlexMessage& msg) {
    std::string s;
    for (const FlexTag& tag : msg.tags) {
        if (!s.empty()) {
            s += ',';
        }
        s += tag.type;
    }
    return s;
}

void export_history_snapshot(
    std::ostream& os,
    std::size_t packet_index,
    const std::string& ts,
    const ParsedFlexMessage& msg,
    const IssueOrderBook& book,
    std::size_t depth) {
    os << "{\"packet\":" << packet_index << ",\"ts\":\"" << ts << '"'
       << ",\"seq\":" << msg.header.sequence
       << ",\"update\":" << msg.header.update_number
       << ",\"tags\":\"" << tag_summary(msg) << "\",";
    book.export_json(os, depth);
    os << "}\n";
}

bool process_pcap(const PcapFile& pcap, OrderBookManager& order_books, Options& opts) {
    std::string error;
    std::size_t printed = 0;

    for (std::size_t i = 0; i < pcap.packets.size(); ++i) {
        const PcapPacket& pkt = pcap.packets[i];
        const auto udp = extract_udp_payload(pkt.data, pcap.link_type);
        if (!udp) {
            continue;
        }

        if (is_maintenance_payload(udp->payload)) {
            continue;
        }

        ParsedFlexMessage msg;
        if (!parse_flex_message(*udp, msg, error)) {
            if (!opts.quiet) {
                std::cerr << "Packet " << i << " parse error: " << error << '\n';
            }
            continue;
        }

        order_books.apply_message(msg);

        if (!opts.history_issue.empty() && msg.header.issue_code == opts.history_issue) {
            const IssueOrderBook* book = order_books.find_book(opts.history_issue);
            if (book) {
                export_history_snapshot(
                    std::cout,
                    i,
                    format_pcap_timestamp(pkt.ts_sec, pkt.ts_frac, pcap.nanosecond_ts),
                    msg,
                    *book,
                    opts.book_depth);
            }
        }

        if (!opts.quiet && (opts.print_all || printed < opts.max_print)) {
            std::cout << '[' << i << "] ts="
                      << format_pcap_timestamp(
                             pkt.ts_sec, pkt.ts_frac, pcap.nanosecond_ts)
                      << '\n';
            print_flex_message(msg, std::cout);
            std::cout << '\n';
            ++printed;
        }
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage(argv[0]);
        return 1;
    }

    if (!opts.venue_path.empty() &&
        (opts.export_pipeline_json || !opts.csv_out.empty())) {
        PipelineOutput output;
        std::string error;
        if (!run_pipeline(
                opts.venue_path,
                opts.pcap_paths,
                output,
                error,
                0,
                opts.show_progress)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (!opts.csv_out.empty()) {
            write_auction_csv(output.results, opts.csv_out, error);
            if (!error.empty()) {
                std::cerr << error << '\n';
                return 1;
            }
        }
        if (opts.export_pipeline_json) {
            export_pipeline_json(output, std::cout, opts.book_depth);
        }
        return 0;
    }

    PcapFile pcap;
    std::string error;
    PcapLoadOptions load_opts;
    load_opts.show_progress = opts.show_progress;
    if (!read_pcaps(opts.pcap_paths, pcap, error, nullptr, &load_opts)) {
        std::cerr << error << '\n';
        return 1;
    }

    if (!opts.quiet && !opts.export_books_json && !opts.list_issues &&
        !opts.export_pipeline_json) {
        std::cout << "PCAP inputs: " << opts.pcap_paths.size() << " file(s)\n";
        for (const std::string& p : opts.pcap_paths) {
            std::cout << "  " << p << '\n';
        }
        std::cout << "  version: " << pcap.version_major << '.' << pcap.version_minor
                  << '\n';
        std::cout << "  snaplen: " << pcap.snaplen << '\n';
        std::cout << "  link type: " << pcap.link_type << " ("
                  << link_type_name(pcap.link_type) << ")\n";
        std::cout << "  timestamp: "
                  << (pcap.nanosecond_ts ? "nanoseconds" : "microseconds") << '\n';
        std::cout << "  packets (merged): " << pcap.packets.size() << "\n\n";
    }

    OrderBookManager order_books;
    process_pcap(pcap, order_books, opts);

    if (opts.export_books_json) {
        order_books.export_all_books_json(std::cout, opts.book_depth);
        return 0;
    }

    if (opts.list_issues) {
        std::cout << "[";
        bool first = true;
        for (const auto& [issue, book] : order_books.books()) {
            (void)book;
            if (!first) {
                std::cout << ',';
            }
            first = false;
            std::cout << '"' << issue << '"';
        }
        std::cout << "]\n";
        return 0;
    }

    if (!opts.quiet) {
        if (!opts.show_issue.empty()) {
            order_books.print_book(opts.show_issue, std::cout, opts.book_depth);
        } else if (opts.history_issue.empty()) {
            order_books.print_all_books(std::cout, opts.book_depth);
        }
    }

    return 0;
}
