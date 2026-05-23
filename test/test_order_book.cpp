#include "flex_protocol.hpp"
#include "flex_tags.hpp"
#include "order_book.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

void write_be_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<uint8_t>(v & 0xff));
}

void write_be_u64(std::vector<uint8_t>& buf, uint64_t v) {
    write_be_u32(buf, static_cast<uint32_t>((v >> 32) & 0xffffffff));
    write_be_u32(buf, static_cast<uint32_t>(v & 0xffffffff));
}

void write_be_u48(std::vector<uint8_t>& buf, uint64_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 40) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 32) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    buf.push_back(static_cast<uint8_t>(v & 0xff));
}

ATag make_a(uint32_t id, char side, uint64_t qty, uint64_t price_raw, uint8_t mod = 0) {
    ATag tag;
    tag.order_id = id;
    tag.side = side;
    tag.quantity = qty;
    tag.price = price_raw;
    tag.modification_flag = mod;
    return tag;
}

std::vector<uint8_t> make_flex_payload_with_a(
    const std::string& issue,
    uint32_t order_id,
    char side,
    uint64_t qty,
    uint64_t price_raw) {
    std::vector<uint8_t> payload(26, 0);
    payload[0] = 51;
    payload[25] = 1;
    std::string padded = issue;
    padded.resize(12, ' ');
    std::memcpy(payload.data() + 6, padded.data(), 12);

    payload.push_back(26);
    payload.push_back('A');
    write_be_u32(payload, 0);
    write_be_u32(payload, order_id);
    payload.push_back(static_cast<uint8_t>(side));
    write_be_u48(payload, qty);
    write_be_u64(payload, price_raw);
    payload.push_back(0);
    payload.push_back(0);
    return payload;
}

}  // namespace

int main() {
    IssueOrderBook book("TEST");

    expect(book.add(make_a(1, 'B', 100, 1000000)), "add bid");
    expect(book.order_count() == 1, "one order");
    expect(book.stats().adds == 1, "add stat");

    expect(book.add(make_a(1, 'B', 200, 1000000, 1)), "qty modify");
    expect(book.order_count() == 1, "still one order after qty modify");
    expect(book.orders().at(1).quantity == 200, "modified qty");

    expect(book.add(make_a(1, 'B', 200, 990000, 0)), "price modify");
    expect(book.orders().at(1).price == 990000, "modified price");

    expect(book.remove(DTag{.order_id = 1, .side = 'B'}), "delete");
    expect(book.order_count() == 0, "empty after delete");

    expect(book.add(make_a(2, 'S', 500, 1010000)), "add ask");
    expect(book.execute(2, 'S', 200), "partial execute");
    expect(book.orders().at(2).quantity == 300, "remaining qty");

    expect(book.execute(2, 'S', 300), "full execute");
    expect(book.order_count() == 0, "removed after full execute");

    ParsedFlexMessage reset_msg;
    reset_msg.header.issue_code = "TEST";
    FlexTag rtag;
    rtag.type = 'R';
    rtag.length = 1;
    rtag.data = {1};
    reset_msg.tags.push_back(rtag);

    OrderBookManager mgr;
    mgr.book_for("TEST").add(make_a(4, 'B', 50, 1000000));
    expect(mgr.find_book("TEST")->order_count() == 1, "book has order");
    mgr.apply_message(reset_msg);
    expect(mgr.find_book("TEST")->order_count() == 0, "reset cleared book");

    std::string error;
    UdpPayload udp;
    udp.payload = make_flex_payload_with_a("TEST", 10, 'B', 100, 1000000);
    ParsedFlexMessage parsed;
    expect(parse_flex_message(udp, parsed, error), error.c_str());
    expect(parsed.header.issue_code == "TEST", "parsed issue");
    expect(parsed.tags.size() == 1 && parsed.tags[0].type == 'A', "parsed A tag");

    OrderBookManager from_udp;
    from_udp.apply_message(parsed);
    expect(from_udp.find_book("TEST") != nullptr, "book created from UDP");
    expect(from_udp.find_book("TEST")->order_count() == 1, "order from UDP");

    std::cout << "test_order_book: all passed\n";
    return 0;
}
