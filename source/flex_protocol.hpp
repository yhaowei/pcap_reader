#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <optional>
#include <string>
#include <vector>

struct FlexPacketHeader {
    uint8_t multicast_group = 0;
    uint8_t system_reboots = 0;
    uint32_t sequence = 0;
    std::string issue_code;
    uint32_t update_number = 0;
    uint8_t packet_number = 0;
    uint8_t total_packets = 0;
    uint8_t utility_flag = 0;
    uint8_t message_count = 0;
};

struct FlexTag {
    char type = '\0';
    uint8_t length = 0;
    std::vector<uint8_t> data;
};

struct UdpPayload {
    std::string src_ip;
    std::string dst_ip;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    std::vector<uint8_t> payload;
};

struct ParsedFlexMessage {
    UdpPayload udp;
    FlexPacketHeader header;
    std::vector<FlexTag> tags;
};

constexpr std::size_t kFlexPacketHeaderLen = 26;

struct FlexStats {
    std::size_t total_pcap_packets = 0;
    std::size_t udp_packets = 0;
    std::size_t parsed_messages = 0;
    std::size_t parse_errors = 0;
    std::size_t maintenance_packets = 0;
    std::array<std::size_t, 256> tag_counts{};
};

std::optional<UdpPayload> extract_udp_payload(
    const std::vector<uint8_t>& frame, uint32_t link_type);

bool parse_flex_message(const UdpPayload& udp, ParsedFlexMessage& out, std::string& error);

void print_flex_message(const ParsedFlexMessage& msg, std::ostream& os);
void print_flex_stats(const FlexStats& stats, std::ostream& os);
