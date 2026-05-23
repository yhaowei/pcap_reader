#include "pcap_io.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

void write_packet(
    std::ofstream& out,
    uint32_t ts_sec,
    uint32_t ts_frac,
    const std::vector<uint8_t>& payload) {
    const uint32_t incl = static_cast<uint32_t>(payload.size());
    out.write(reinterpret_cast<const char*>(&ts_sec), 4);
    out.write(reinterpret_cast<const char*>(&ts_frac), 4);
    out.write(reinterpret_cast<const char*>(&incl), 4);
    out.write(reinterpret_cast<const char*>(&incl), 4);
    if (!payload.empty()) {
        out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    }
}

void write_pcap(
    const std::filesystem::path& path,
    const std::vector<std::pair<uint32_t, uint32_t>>& timestamps) {
    std::ofstream out(path, std::ios::binary);
    // Same magic as project capture files (nanosecond, BE magic; LE record headers).
    const uint32_t magic = 0xa1b23c4d;
    const uint16_t vmaj = 2;
    const uint16_t vmin = 4;
    const uint32_t zero = 0;
    const uint32_t snaplen = 65535;
    const uint32_t network = 1;
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&vmaj), 2);
    out.write(reinterpret_cast<const char*>(&vmin), 2);
    out.write(reinterpret_cast<const char*>(&zero), 4);
    out.write(reinterpret_cast<const char*>(&zero), 4);
    out.write(reinterpret_cast<const char*>(&snaplen), 4);
    out.write(reinterpret_cast<const char*>(&network), 4);

    const std::vector<uint8_t> payload(64, 0);
    for (const auto& [sec, frac] : timestamps) {
        write_packet(out, sec, frac, payload);
    }
}

uint64_t packet_key(const PcapPacket& pkt, bool nano) {
    return pcap_timestamp_key(pkt.ts_sec, pkt.ts_frac, nano);
}

}  // namespace

int main() {
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "mbo_pcap_test";
    std::filesystem::create_directories(tmp);

    const auto overlapping_a = tmp / "overlap_a.pcap";
    const auto overlapping_b = tmp / "overlap_b.pcap";
    write_pcap(overlapping_a, {{100, 0}, {300, 0}});
    write_pcap(overlapping_b, {{200, 0}, {400, 0}});

    PcapFile merged;
    std::string error;
    expect(
        read_pcaps({overlapping_a.string(), overlapping_b.string()}, merged, error),
        error.c_str());
    expect(merged.packets.size() == 4, "expected 4 packets merged");
    expect(
        packet_key(merged.packets[0], merged.nanosecond_ts) == 100ULL * 1'000'000'000ULL,
        "merged order ts0");
    expect(
        packet_key(merged.packets[1], merged.nanosecond_ts) == 200ULL * 1'000'000'000ULL,
        "merged order ts1");
    expect(
        packet_key(merged.packets[2], merged.nanosecond_ts) == 300ULL * 1'000'000'000ULL,
        "merged order ts2");
    expect(
        packet_key(merged.packets[3], merged.nanosecond_ts) == 400ULL * 1'000'000'000ULL,
        "merged order ts3");

    const auto seq_a = tmp / "seq_a.pcap";
    const auto seq_b = tmp / "seq_b.pcap";
    write_pcap(seq_a, {{100, 0}, {200, 0}});
    write_pcap(seq_b, {{10'000, 0}, {10'100, 0}});

    PcapFile sequential;
    expect(
        read_pcaps({seq_a.string(), seq_b.string()}, sequential, error),
        error.c_str());
    expect(sequential.packets.size() == 4, "expected 4 packets sequential");
    expect(sequential.packets[0].source_path == seq_a.string(), "first chunk from a");
    expect(sequential.packets[2].source_path == seq_b.string(), "second chunk from b");
    expect(
        packet_key(sequential.packets[0], sequential.nanosecond_ts) <
            packet_key(sequential.packets[1], sequential.nanosecond_ts),
        "within-file order preserved a");
    expect(
        packet_key(sequential.packets[2], sequential.nanosecond_ts) <
            packet_key(sequential.packets[3], sequential.nanosecond_ts),
        "within-file order preserved b");

    const auto micro = tmp / "micro.pcap";
    write_pcap(micro, {{50, 0}});
    const uint32_t micro_magic = 0xa1b2c3d4;
    {
        std::fstream f(micro, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(0);
        f.write(reinterpret_cast<const char*>(&micro_magic), 4);
    }

    PcapFile filtered;
    expect(
        read_pcaps({overlapping_a.string(), micro.string(), overlapping_b.string()}, filtered, error),
        error.c_str());
    expect(filtered.packets.size() == 4, "incompatible micro file should be skipped");

    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto p051 = root / "20241105_051.test.pcap";
    const auto p052 = root / "20241105_052.test.pcap";
    if (std::filesystem::exists(p051) && std::filesystem::exists(p052)) {
        PcapFile real;
        std::vector<std::string> loaded;
        expect(read_pcaps({p051.string(), p052.string()}, real, error, &loaded), error.c_str());
        expect(loaded.size() == 2, "both real pcaps should load");
        expect(real.packets.size() == 210990, "051+052 packet count");
        expect(
            packet_key(real.packets[0], real.nanosecond_ts) <=
                packet_key(real.packets.back(), real.nanosecond_ts),
            "merged real pcaps sorted by time");
    }

    std::cout << "test_pcap_io: all passed\n";
    return 0;
}
