#include "flex_protocol.hpp"

#include <iomanip>
#include <sstream>

namespace {

uint16_t read_be_u16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t read_be_u32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint64_t read_be_u64(const uint8_t* p) {
    return (static_cast<uint64_t>(read_be_u32(p)) << 32) | read_be_u32(p + 4);
}

uint64_t read_be_u48(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 40) |
           (static_cast<uint64_t>(p[1]) << 32) |
           (static_cast<uint64_t>(p[2]) << 24) |
           (static_cast<uint64_t>(p[3]) << 16) |
           (static_cast<uint64_t>(p[4]) << 8) | p[5];
}

std::string read_char_field(const uint8_t* p, std::size_t len) {
    std::string s(reinterpret_cast<const char*>(p), len);
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    return s;
}

std::string format_price(uint64_t raw) {
    const uint64_t whole = raw / 10000;
    const uint64_t frac = raw % 10000;
    std::ostringstream os;
    os << whole << '.' << std::setw(4) << std::setfill('0') << frac;
    return os.str();
}

std::string ipv4_to_string(const uint8_t* p) {
    std::ostringstream os;
    os << static_cast<unsigned>(p[0]) << '.' << static_cast<unsigned>(p[1]) << '.'
       << static_cast<unsigned>(p[2]) << '.' << static_cast<unsigned>(p[3]);
    return os.str();
}

bool parse_ipv4_udp(const uint8_t* ip, std::size_t ip_len, UdpPayload& out) {
    if (ip_len < 20) {
        return false;
    }
    const uint8_t version_ihl = ip[0];
    const uint8_t ihl = static_cast<uint8_t>(version_ihl & 0x0f);
    const std::size_t ip_header_len = static_cast<std::size_t>(ihl) * 4;
    if (ip_header_len < 20 || ip_len < ip_header_len) {
        return false;
    }
    if ((version_ihl >> 4) != 4) {
        return false;
    }
    if (ip[9] != 17) {
        return false;
    }

    const uint16_t total_len = read_be_u16(ip + 2);
    if (total_len < ip_header_len + 8 || ip_len < total_len) {
        return false;
    }

    out.src_ip = ipv4_to_string(ip + 12);
    out.dst_ip = ipv4_to_string(ip + 16);

    const uint8_t* udp = ip + ip_header_len;
    const std::size_t udp_len = total_len - ip_header_len;
    if (udp_len < 8) {
        return false;
    }

    out.src_port = read_be_u16(udp);
    out.dst_port = read_be_u16(udp + 2);
    const uint16_t udp_total = read_be_u16(udp + 4);
    if (udp_total < 8 || udp_total > udp_len) {
        return false;
    }

    const std::size_t payload_len = udp_total - 8;
    out.payload.assign(udp + 8, udp + 8 + payload_len);
    return true;
}

void print_tag_body(const FlexTag& tag, std::ostream& os) {
    const uint8_t* d = tag.data.data();
    const std::size_t n = tag.data.size();
    if (n == 0) {
        return;
    }

    switch (tag.type) {
        case 'T':
            if (n >= 5) {
                os << " time_sec=" << read_be_u32(d + 1);
            }
            break;
        case 'O':
            if (n >= 18) {
                os << " time_us=" << read_be_u32(d + 1)
                   << " market_status=" << static_cast<unsigned>(d[5])
                   << " status=\"" << read_char_field(d + 6, 2) << '"'
                   << " book_center=" << format_price(read_be_u64(d + 10));
            }
            break;
        case 'K':
            if (n >= 46) {
                os << " time_us=" << read_be_u32(d + 1)
                   << " side=" << static_cast<char>(d[5])
                   << " total_vol=" << read_be_u48(d + 6)
                   << " last_price=" << format_price(read_be_u64(d + 18))
                   << " match_id=" << read_be_u32(d + 26);
            }
            break;
        case 'A':
            if (n >= 26) {
                os << " time_us=" << read_be_u32(d + 1)
                   << " order_id=" << read_be_u32(d + 5)
                   << " side=" << static_cast<char>(d[9])
                   << " qty=" << read_be_u48(d + 10)
                   << " price=" << format_price(read_be_u64(d + 16));
            }
            break;
        case 'E':
            if (n >= 20) {
                os << " time_us=" << read_be_u32(d + 1)
                   << " order_id=" << read_be_u32(d + 5)
                   << " side=" << static_cast<char>(d[9])
                   << " vol=" << read_be_u48(d + 10)
                   << " match_id=" << read_be_u32(d + 16);
            }
            break;
        case 'C':
            if (n >= 29) {
                os << " time_us=" << read_be_u32(d + 1)
                   << " order_id=" << read_be_u32(d + 5)
                   << " side=" << static_cast<char>(d[9])
                   << " vol=" << read_be_u48(d + 10)
                   << " exec_price=" << format_price(read_be_u64(d + 20));
            }
            break;
        case 'D':
            if (n >= 11) {
                os << " time_us=" << read_be_u32(d + 1)
                   << " order_id=" << read_be_u32(d + 5)
                   << " side=" << static_cast<char>(d[9]);
            }
            break;
        case 'L':
            if (n >= 3) {
                os << " test_mode=" << static_cast<unsigned>(d[1])
                   << " start_end=" << static_cast<unsigned>(d[2]);
            }
            break;
        case 'R':
            os << " reset_phase=" << static_cast<unsigned>(d[0]);
            break;
        default:
            os << " raw=" << n << "B";
            break;
    }
}

}  // namespace

std::optional<UdpPayload> extract_udp_payload(
    const std::vector<uint8_t>& frame, uint32_t link_type) {
    std::size_t offset = 0;

    if (link_type == 0) {
        if (frame.size() < 4) {
            return std::nullopt;
        }
        const uint32_t family = read_be_u32(frame.data());
        if (family != 2) {
            return std::nullopt;
        }
        offset = 4;
    } else if (link_type == 1) {
        if (frame.size() < 14) {
            return std::nullopt;
        }
        offset = 12;
        uint16_t ether_type = read_be_u16(frame.data() + offset);
        offset += 2;

        if (ether_type == 0x8100) {
            if (frame.size() < offset + 4) {
                return std::nullopt;
            }
            offset += 2;
            ether_type = read_be_u16(frame.data() + offset);
            offset += 2;
        }

        if (ether_type != 0x0800) {
            return std::nullopt;
        }
    } else if (link_type == 101) {
        offset = 0;
    } else {
        return std::nullopt;
    }

    if (offset >= frame.size()) {
        return std::nullopt;
    }

    UdpPayload out;
    if (!parse_ipv4_udp(frame.data() + offset, frame.size() - offset, out)) {
        return std::nullopt;
    }
    return out;
}

bool parse_flex_message(const UdpPayload& udp, ParsedFlexMessage& out, std::string& error) {
    out.udp = udp;
    const auto& payload = udp.payload;

    if (payload.size() < kFlexPacketHeaderLen) {
        error = "UDP payload shorter than 26-byte packet header";
        return false;
    }

    const uint8_t* p = payload.data();
    out.header.multicast_group = p[0];
    out.header.system_reboots = p[1];
    out.header.sequence = read_be_u32(p + 2);
    out.header.issue_code = read_char_field(p + 6, 12);
    out.header.update_number = read_be_u32(p + 18);
    out.header.packet_number = p[22];
    out.header.total_packets = p[23];
    out.header.utility_flag = p[24];
    out.header.message_count = p[25];

    std::size_t offset = kFlexPacketHeaderLen;
    out.tags.clear();
    out.tags.reserve(out.header.message_count);

    for (uint8_t i = 0; i < out.header.message_count; ++i) {
        if (offset >= payload.size()) {
            error = "Unexpected end of payload reading tag length";
            return false;
        }

        const uint8_t tag_len = payload[offset++];
        if (tag_len == 0) {
            error = "Zero tag length at tag index " + std::to_string(i);
            return false;
        }
        if (offset + tag_len > payload.size()) {
            error = "Tag length exceeds payload at tag index " + std::to_string(i);
            return false;
        }

        FlexTag tag;
        tag.length = tag_len;
        tag.data.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                        payload.begin() + static_cast<std::ptrdiff_t>(offset + tag_len));
        tag.type = tag.data.empty() ? '\0' : static_cast<char>(tag.data[0]);
        out.tags.push_back(std::move(tag));
        offset += tag_len;
    }

    if (offset != payload.size()) {
        error = "Trailing bytes after tags: " + std::to_string(payload.size() - offset);
        return false;
    }

    return true;
}

void print_flex_message(const ParsedFlexMessage& msg, std::ostream& os) {
    const auto& h = msg.header;
    os << "  FLEX mcast_grp=" << static_cast<unsigned>(h.multicast_group)
       << " seq=" << h.sequence
       << " reboots=" << static_cast<unsigned>(h.system_reboots)
       << " issue=\"" << h.issue_code << '"'
       << " update=" << h.update_number
       << " util=" << static_cast<unsigned>(h.utility_flag)
       << " tags=" << static_cast<unsigned>(h.message_count) << '\n';
    os << "       UDP " << msg.udp.src_ip << ':' << msg.udp.src_port << " -> "
       << msg.udp.dst_ip << ':' << msg.udp.dst_port << '\n';

    for (const FlexTag& tag : msg.tags) {
        os << "       [" << tag.type << "] len=" << static_cast<unsigned>(tag.length);
        print_tag_body(tag, os);
        os << '\n';
    }
}

void print_flex_stats(const FlexStats& stats, std::ostream& os) {
    os << "\n=== Summary ===\n";
    os << "PCAP packets:      " << stats.total_pcap_packets << '\n';
    os << "UDP payloads:      " << stats.udp_packets << '\n';
    os << "Parsed messages:   " << stats.parsed_messages << '\n';
    os << "Parse errors:      " << stats.parse_errors << '\n';
    os << "Maintenance (skip):" << stats.maintenance_packets << '\n';
    os << "Tag counts:\n";
    static constexpr char kKnownTags[] = "TOKAECDLR";
    for (char c : kKnownTags) {
        const auto count = stats.tag_counts[static_cast<unsigned char>(c)];
        if (count > 0) {
            os << "  " << c << ": " << count << '\n';
        }
    }
}
