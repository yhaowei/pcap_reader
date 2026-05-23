#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PcapPacket {
    uint32_t ts_sec = 0;
    uint32_t ts_frac = 0;
    std::vector<uint8_t> data;
    std::string source_path;
};

struct PcapFile {
    uint16_t version_major = 0;
    uint16_t version_minor = 0;
    uint32_t snaplen = 0;
    uint32_t link_type = 0;
    bool nanosecond_ts = false;
    bool swap_endian = false;
    std::vector<PcapPacket> packets;
};

struct PcapLoadOptions {
    bool show_progress = false;
    std::size_t file_index = 1;
    std::size_t file_count = 1;
};

inline constexpr const char* kPcapProgressPrefix = "[pcap-progress]";

uint64_t pcap_timestamp_key(uint32_t ts_sec, uint32_t ts_frac, bool nanoseconds);

bool read_pcap_file(
    const std::string& path,
    PcapFile& out,
    std::string& error,
    const PcapLoadOptions* options = nullptr);

// Loads multiple PCAPs: merges by timestamp when captures share a timeline
// (overlapping or adjacent ranges); otherwise concatenates in input order.
// If loaded_paths is non-null, receives the list of files actually loaded (after format filter).
bool read_pcaps(
    const std::vector<std::string>& paths,
    PcapFile& out,
    std::string& error,
    std::vector<std::string>* loaded_paths = nullptr,
    const PcapLoadOptions* options = nullptr);

// Always merges all packets and sorts globally by timestamp.
bool read_pcaps_sorted(
    const std::vector<std::string>& paths,
    PcapFile& out,
    std::string& error,
    std::vector<std::string>* loaded_paths = nullptr,
    const PcapLoadOptions* options = nullptr);

std::vector<std::string> list_pcap_files_in_directory(const std::string& directory);

void sort_packets_by_time(PcapFile& pcap);

std::string format_pcap_timestamp(uint32_t ts_sec, uint32_t ts_frac, bool nanoseconds);
