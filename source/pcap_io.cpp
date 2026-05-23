#include "pcap_io.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

constexpr uint32_t kMagicMicroBe = 0xa1b2c3d4;
constexpr uint32_t kMagicMicroLe = 0xd4c3b2a1;
constexpr uint32_t kMagicNanoBe = 0xa1b23c4d;
constexpr uint32_t kMagicNanoLe = 0x4d3cb2a1;

uint32_t swap32(uint32_t v) {
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

uint16_t swap16(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

uint32_t fix32(uint32_t v, bool swap_endian) {
    return swap_endian ? swap32(v) : v;
}

uint16_t fix16(uint16_t v, bool swap_endian) {
    return swap_endian ? swap16(v) : v;
}

struct RawGlobalHeader {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct RawRecordHeader {
    uint32_t ts_sec;
    uint32_t ts_frac;
    uint32_t incl_len;
    uint32_t orig_len;
};

bool read_exact(std::ifstream& in, void* buf, std::size_t size) {
    in.read(static_cast<char*>(buf), static_cast<std::streamsize>(size));
    return in.gcount() == static_cast<std::streamsize>(size);
}

bool is_pcap_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".pcap" || ext == ".pcapng";
}

bool parse_pcap_global_header(
    const RawGlobalHeader& gh,
    uint16_t& version_major,
    uint16_t& version_minor,
    uint32_t& snaplen,
    uint32_t& link_type,
    bool& nanosecond_ts,
    bool& swap_endian,
    std::string& error) {
    swap_endian = false;
    nanosecond_ts = false;

    switch (gh.magic) {
        case kMagicMicroBe:
            break;
        case kMagicMicroLe:
            swap_endian = true;
            break;
        case kMagicNanoBe:
            nanosecond_ts = true;
            break;
        case kMagicNanoLe:
            swap_endian = true;
            nanosecond_ts = true;
            break;
        default:
            error = "Not a PCAP file (unsupported magic)";
            return false;
    }

    version_major = fix16(gh.version_major, swap_endian);
    version_minor = fix16(gh.version_minor, swap_endian);
    snaplen = fix32(gh.snaplen, swap_endian);
    link_type = fix32(gh.network, swap_endian);
    return true;
}

}  // namespace

uint64_t pcap_timestamp_key(uint32_t ts_sec, uint32_t ts_frac, bool nanoseconds) {
    if (nanoseconds) {
        return static_cast<uint64_t>(ts_sec) * 1'000'000'000ULL +
               static_cast<uint64_t>(ts_frac);
    }
    return static_cast<uint64_t>(ts_sec) * 1'000'000ULL + static_cast<uint64_t>(ts_frac);
}

std::string format_pcap_timestamp(uint32_t ts_sec, uint32_t ts_frac, bool nanoseconds) {
    std::ostringstream os;
    os << ts_sec << '.';
    if (nanoseconds) {
        os << std::setw(9) << std::setfill('0') << ts_frac;
    } else {
        os << std::setw(6) << std::setfill('0') << ts_frac;
    }
    return os.str();
}

void sort_packets_by_time(PcapFile& pcap) {
    std::sort(
        pcap.packets.begin(),
        pcap.packets.end(),
        [&](const PcapPacket& a, const PcapPacket& b) {
            return pcap_timestamp_key(a.ts_sec, a.ts_frac, pcap.nanosecond_ts) <
                   pcap_timestamp_key(b.ts_sec, b.ts_frac, pcap.nanosecond_ts);
        });
}

std::vector<std::string> list_pcap_files_in_directory(const std::string& directory) {
    std::vector<std::string> paths;
    namespace fs = std::filesystem;

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!is_pcap_extension(entry.path())) {
            continue;
        }
        paths.push_back(entry.path().string());
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

namespace {

std::string path_basename(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

void emit_pcap_progress(const PcapLoadOptions* options, const std::string& message) {
    if (options == nullptr || !options->show_progress) {
        return;
    }
    std::cerr << kPcapProgressPrefix << ' ' << message << std::endl;
}

}  // namespace

bool read_pcap_file(
    const std::string& path,
    PcapFile& out,
    std::string& error,
    const PcapLoadOptions* options) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Failed to open: " + path;
        return false;
    }

    std::uintmax_t file_size = 0;
    {
        std::error_code ec;
        file_size = std::filesystem::file_size(path, ec);
        if (ec) {
            file_size = 0;
        }
    }

    if (options != nullptr && options->show_progress) {
        std::ostringstream msg;
        msg << "Loading file " << options->file_index << '/' << options->file_count << ": "
            << path_basename(path);
        if (file_size > 0) {
            msg << " (" << file_size << " bytes)";
        }
        emit_pcap_progress(options, msg.str());
    }

    RawGlobalHeader gh{};
    if (!read_exact(in, &gh, sizeof(gh))) {
        error = "File too small for PCAP global header: " + path;
        return false;
    }

    bool swap_endian = false;
    bool nanosecond_ts = false;
    if (!parse_pcap_global_header(
            gh,
            out.version_major,
            out.version_minor,
            out.snaplen,
            out.link_type,
            nanosecond_ts,
            swap_endian,
            error)) {
        error = error + ": " + path;
        return false;
    }
    out.nanosecond_ts = nanosecond_ts;
    out.swap_endian = swap_endian;
    out.packets.clear();

    std::size_t index = 0;
    std::size_t last_reported_pct = 0;
    constexpr std::size_t kPacketReportInterval = 50000;
    while (in) {
        RawRecordHeader rh{};
        if (!read_exact(in, &rh, sizeof(rh))) {
            if (in.eof()) {
                break;
            }
            error = "Unexpected EOF reading packet header at " + path + " index " +
                    std::to_string(index);
            return false;
        }

        const uint32_t incl_len = fix32(rh.incl_len, swap_endian);
        if (incl_len > out.snaplen) {
            error = "Packet " + std::to_string(index) + " in " + path +
                    ": incl_len exceeds snaplen";
            return false;
        }

        PcapPacket pkt;
        pkt.ts_sec = fix32(rh.ts_sec, swap_endian);
        pkt.ts_frac = fix32(rh.ts_frac, swap_endian);
        pkt.source_path = path;
        pkt.data.resize(incl_len);

        if (incl_len > 0 && !read_exact(in, pkt.data.data(), incl_len)) {
            error = "Unexpected EOF reading packet data at " + path + " index " +
                    std::to_string(index);
            return false;
        }

        out.packets.push_back(std::move(pkt));
        ++index;

        if (options != nullptr && options->show_progress) {
            bool report = (index % kPacketReportInterval == 0);
            if (file_size > 0) {
                const std::streampos pos = in.tellg();
                if (pos >= 0) {
                    const std::size_t pct = static_cast<std::size_t>(
                        (static_cast<double>(pos) * 100.0) / static_cast<double>(file_size));
                    if (pct >= last_reported_pct + 5 || pct == 100) {
                        report = true;
                        last_reported_pct = pct;
                    }
                }
            }
            if (report) {
                std::ostringstream msg;
                msg << "  " << path_basename(path) << ": " << index << " packets";
                if (file_size > 0 && in.tellg() >= 0) {
                    msg << " (~" << last_reported_pct << "%)";
                }
                emit_pcap_progress(options, msg.str());
            }
        }
    }

    if (options != nullptr && options->show_progress) {
        std::ostringstream msg;
        msg << "Loaded file " << options->file_index << '/' << options->file_count << ": "
            << path_basename(path) << " (" << index << " packets)";
        emit_pcap_progress(options, msg.str());
    }

    return true;
}

namespace {

struct PcapTimeRange {
    uint64_t first = 0;
    uint64_t last = 0;
    bool valid = false;
};

constexpr uint64_t kMaxGapNanos = 60'000'000'000ULL;   // 60 seconds
constexpr uint64_t kMaxGapMicros = 60'000'000ULL;

struct PcapFormat {
    uint32_t link_type = 0;
    uint32_t snaplen = 0;
    bool nanosecond_ts = false;
    bool swap_endian = false;
};

bool same_pcap_format(const PcapFormat& a, const PcapFormat& b) {
    return a.link_type == b.link_type && a.nanosecond_ts == b.nanosecond_ts;
}

std::string format_resolution_label(bool nanosecond_ts) {
    return nanosecond_ts ? "nanosecond" : "microsecond";
}

bool peek_pcap_format(const std::string& path, PcapFormat& format, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Failed to open: " + path;
        return false;
    }

    RawGlobalHeader gh{};
    if (!read_exact(in, &gh, sizeof(gh))) {
        error = "File too small for PCAP global header: " + path;
        return false;
    }

    uint16_t vmaj = 0;
    uint16_t vmin = 0;
    bool nanosecond_ts = false;
    std::string parse_error;
    if (!parse_pcap_global_header(
            gh,
            vmaj,
            vmin,
            format.snaplen,
            format.link_type,
            nanosecond_ts,
            format.swap_endian,
            parse_error)) {
        error = parse_error + ": " + path;
        return false;
    }
    format.nanosecond_ts = nanosecond_ts;
    (void)vmaj;
    (void)vmin;
    return true;
}

bool filter_paths_by_format(std::vector<std::string>& paths, std::string& error) {
    if (paths.empty()) {
        error = "No PCAP files to load";
        return false;
    }

    PcapFormat reference{};
    if (!peek_pcap_format(paths.front(), reference, error)) {
        return false;
    }
    const std::string reference_path = paths.front();

    std::vector<std::string> kept;
    kept.reserve(paths.size());

    for (const std::string& path : paths) {
        PcapFormat fmt;
        if (!peek_pcap_format(path, fmt, error)) {
            return false;
        }
        if (same_pcap_format(reference, fmt)) {
            kept.push_back(path);
            continue;
        }

        std::cerr << "Skipping incompatible PCAP: " << path << " (link type " << fmt.link_type
                  << ", " << format_resolution_label(fmt.nanosecond_ts)
                  << " timestamps) does not match " << reference_path << " (link type "
                  << reference.link_type << ", "
                  << format_resolution_label(reference.nanosecond_ts) << " timestamps)\n";
    }

    if (kept.empty()) {
        error = "No compatible PCAP files to load";
        return false;
    }

    if (kept.size() == 1 && paths.size() > 1) {
        for (std::size_t i = 1; i < paths.size(); ++i) {
            PcapFormat fmt;
            if (!peek_pcap_format(paths[i], fmt, error)) {
                return false;
            }
            if (!same_pcap_format(reference, fmt)) {
                error = "Inconsistent PCAP format across files: " + reference_path + " uses " +
                        format_resolution_label(reference.nanosecond_ts) +
                        " timestamps (link type " + std::to_string(reference.link_type) +
                        "), but " + paths[i] + " uses " +
                        format_resolution_label(fmt.nanosecond_ts) + " timestamps (link type " +
                        std::to_string(fmt.link_type) +
                        "). Load only captures with the same PCAP format, or use a folder that "
                        "does not mix sample/test files.";
                return false;
            }
        }
    }

    paths = std::move(kept);
    return true;
}

bool scan_pcap_time_range(
    const std::string& path,
    PcapTimeRange& range,
    bool nanosecond_ts,
    bool swap_endian,
    uint32_t snaplen,
    std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Failed to open: " + path;
        return false;
    }

    in.seekg(static_cast<std::streamoff>(sizeof(RawGlobalHeader)));

    std::size_t index = 0;
    while (in) {
        RawRecordHeader rh{};
        if (!read_exact(in, &rh, sizeof(rh))) {
            if (in.eof()) {
                break;
            }
            error = "Unexpected EOF reading packet header at " + path + " index " +
                    std::to_string(index);
            return false;
        }

        const uint32_t incl_len = fix32(rh.incl_len, swap_endian);
        if (incl_len > snaplen) {
            error = "Packet " + std::to_string(index) + " in " + path +
                    ": incl_len exceeds snaplen";
            return false;
        }

        const uint32_t ts_sec = fix32(rh.ts_sec, swap_endian);
        const uint32_t ts_frac = fix32(rh.ts_frac, swap_endian);
        const uint64_t key = pcap_timestamp_key(ts_sec, ts_frac, nanosecond_ts);
        if (!range.valid) {
            range.first = key;
            range.last = key;
            range.valid = true;
        } else {
            if (key < range.first) {
                range.first = key;
            }
            if (key > range.last) {
                range.last = key;
            }
        }

        in.seekg(static_cast<std::streamoff>(incl_len), std::ios::cur);
        ++index;
    }

    return true;
}

bool ranges_overlap(const PcapTimeRange& a, const PcapTimeRange& b) {
    if (!a.valid || !b.valid) {
        return false;
    }
    return a.first <= b.last && b.first <= a.last;
}

bool pcaps_share_time_sequence(
    const std::vector<PcapTimeRange>& ranges, bool nanosecond_ts) {
    std::vector<const PcapTimeRange*> valid;
    valid.reserve(ranges.size());
    for (const PcapTimeRange& r : ranges) {
        if (r.valid) {
            valid.push_back(&r);
        }
    }

    if (valid.size() <= 1) {
        return true;
    }

    for (std::size_t i = 0; i < valid.size(); ++i) {
        for (std::size_t j = i + 1; j < valid.size(); ++j) {
            if (ranges_overlap(*valid[i], *valid[j])) {
                return true;
            }
        }
    }

    const uint64_t max_gap = nanosecond_ts ? kMaxGapNanos : kMaxGapMicros;
    std::vector<const PcapTimeRange*> order = valid;
    std::sort(order.begin(), order.end(), [](const PcapTimeRange* a, const PcapTimeRange* b) {
        return a->first < b->first;
    });

    for (std::size_t i = 0; i + 1 < order.size(); ++i) {
        const PcapTimeRange& cur = *order[i];
        const PcapTimeRange& next = *order[i + 1];
        if (next.first > cur.last && next.first - cur.last > max_gap) {
            return false;
        }
    }
    return true;
}

bool merge_pcap_metadata(
    bool& meta_initialized,
    PcapFile& out,
    const PcapFile& chunk,
    const std::string& path,
    std::string& error) {
    if (!meta_initialized) {
        out.version_major = chunk.version_major;
        out.version_minor = chunk.version_minor;
        out.snaplen = chunk.snaplen;
        out.link_type = chunk.link_type;
        out.nanosecond_ts = chunk.nanosecond_ts;
        out.swap_endian = chunk.swap_endian;
        meta_initialized = true;
        return true;
    }
    if (out.link_type != chunk.link_type || out.nanosecond_ts != chunk.nanosecond_ts) {
        error = "Inconsistent PCAP format between " +
                (out.packets.empty() ? path : out.packets.front().source_path) + " (link type " +
                std::to_string(out.link_type) + ", " +
                format_resolution_label(out.nanosecond_ts) + " timestamps) and " + path +
                " (link type " + std::to_string(chunk.link_type) + ", " +
                format_resolution_label(chunk.nanosecond_ts) + " timestamps)";
        return false;
    }
    return true;
}

bool append_pcap_file(
    const std::string& path,
    PcapFile& out,
    bool meta_initialized,
    std::string& error,
    const PcapLoadOptions* options) {
    PcapFile chunk;
    if (!read_pcap_file(path, chunk, error, options)) {
        return false;
    }

    bool init = meta_initialized;
    if (!merge_pcap_metadata(init, out, chunk, path, error)) {
        return false;
    }

    if (out.packets.empty()) {
        out.packets = std::move(chunk.packets);
    } else {
        out.packets.insert(
            out.packets.end(),
            std::make_move_iterator(chunk.packets.begin()),
            std::make_move_iterator(chunk.packets.end()));
    }
    return true;
}

}  // namespace

bool read_pcaps_sorted(
    const std::vector<std::string>& paths,
    PcapFile& out,
    std::string& error,
    std::vector<std::string>* loaded_paths,
    const PcapLoadOptions* options) {
    if (paths.empty()) {
        error = "No PCAP files to load";
        return false;
    }

    std::vector<std::string> path_copy = paths;
    if (!filter_paths_by_format(path_copy, error)) {
        return false;
    }

    if (options != nullptr && options->show_progress) {
        std::ostringstream msg;
        msg << "Loading " << path_copy.size() << " PCAP file(s) (merge by time)";
        emit_pcap_progress(options, msg.str());
    }

    out = PcapFile{};
    bool meta_initialized = false;
    for (std::size_t i = 0; i < path_copy.size(); ++i) {
        PcapLoadOptions per_file;
        if (options != nullptr) {
            per_file = *options;
            per_file.file_index = i + 1;
            per_file.file_count = path_copy.size();
        }
        if (!append_pcap_file(
                path_copy[i],
                out,
                meta_initialized,
                error,
                options != nullptr ? &per_file : nullptr)) {
            return false;
        }
        meta_initialized = true;
    }

    if (options != nullptr && options->show_progress) {
        std::ostringstream msg;
        msg << "Sorting " << out.packets.size() << " packets by timestamp";
        emit_pcap_progress(options, msg.str());
    }
    sort_packets_by_time(out);

    if (loaded_paths != nullptr) {
        *loaded_paths = std::move(path_copy);
    }
    return true;
}

bool read_pcaps(
    const std::vector<std::string>& paths,
    PcapFile& out,
    std::string& error,
    std::vector<std::string>* loaded_paths,
    const PcapLoadOptions* options) {
    if (paths.empty()) {
        error = "No PCAP files to load";
        return false;
    }

    std::vector<std::string> path_copy = paths;
    if (!filter_paths_by_format(path_copy, error)) {
        return false;
    }

    if (options != nullptr && options->show_progress) {
        std::ostringstream msg;
        msg << "Found " << path_copy.size() << " compatible PCAP file(s)";
        emit_pcap_progress(options, msg.str());
    }

    PcapFormat ref_fmt;
    if (!peek_pcap_format(path_copy.front(), ref_fmt, error)) {
        return false;
    }

    std::vector<PcapTimeRange> ranges;
    ranges.reserve(path_copy.size());
    for (std::size_t i = 0; i < path_copy.size(); ++i) {
        if (options != nullptr && options->show_progress) {
            std::ostringstream msg;
            msg << "Scanning file " << (i + 1) << '/' << path_copy.size() << ": "
                << path_basename(path_copy[i]);
            emit_pcap_progress(options, msg.str());
        }
        PcapTimeRange range;
        if (!scan_pcap_time_range(
                path_copy[i],
                range,
                ref_fmt.nanosecond_ts,
                ref_fmt.swap_endian,
                ref_fmt.snaplen,
                error)) {
            return false;
        }
        ranges.push_back(range);
    }

    const bool merge_by_time = pcaps_share_time_sequence(ranges, ref_fmt.nanosecond_ts);

    if (options != nullptr && options->show_progress) {
        emit_pcap_progress(
            options,
            std::string("Load mode: ") + (merge_by_time ? "merge by timestamp" : "file order"));
    }

    out = PcapFile{};
    bool meta_initialized = false;
    for (std::size_t i = 0; i < path_copy.size(); ++i) {
        PcapLoadOptions per_file;
        if (options != nullptr) {
            per_file = *options;
            per_file.file_index = i + 1;
            per_file.file_count = path_copy.size();
        }
        PcapFile chunk;
        if (!read_pcap_file(
                path_copy[i],
                chunk,
                error,
                options != nullptr ? &per_file : nullptr)) {
            return false;
        }
        if (!merge_pcap_metadata(meta_initialized, out, chunk, path_copy[i], error)) {
            return false;
        }
        if (!merge_by_time) {
            sort_packets_by_time(chunk);
        }
        out.packets.insert(
            out.packets.end(),
            std::make_move_iterator(chunk.packets.begin()),
            std::make_move_iterator(chunk.packets.end()));
        meta_initialized = true;
    }

    if (merge_by_time) {
        if (options != nullptr && options->show_progress) {
            std::ostringstream msg;
            msg << "Sorting " << out.packets.size() << " packets by timestamp";
            emit_pcap_progress(options, msg.str());
        }
        sort_packets_by_time(out);
    }

    if (options != nullptr && options->show_progress) {
        emit_pcap_progress(
            options,
            "PCAP load complete: " + std::to_string(out.packets.size()) + " packets total");
    }

    if (loaded_paths != nullptr) {
        *loaded_paths = std::move(path_copy);
    }
    return true;
}
