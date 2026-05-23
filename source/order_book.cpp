#include "order_book.hpp"

#include <algorithm>
#include <iomanip>
#include <map>

namespace {

Side parse_side(char c) {
    return c == 'B' ? Side::Buy : Side::Sell;
}

bool side_matches(const BookOrder& order, char side) {
    return (order.side == Side::Buy && side == 'B') ||
           (order.side == Side::Sell && side == 'S');
}

}  // namespace

IssueOrderBook::IssueOrderBook(std::string issue_code)
    : issue_code_(std::move(issue_code)) {}

void IssueOrderBook::clear() {
    orders_.clear();
}

BookOrder* IssueOrderBook::find_order(uint32_t order_id, char side) {
    const auto it = orders_.find(order_id);
    if (it == orders_.end() || !side_matches(it->second, side)) {
        return nullptr;
    }
    return &it->second;
}

bool IssueOrderBook::add(const ATag& tag) {
    const Side side = parse_side(tag.side);
    auto it = orders_.find(tag.order_id);

    if (it != orders_.end()) {
        if (tag.modification_flag == 1) {
            it->second.quantity = tag.quantity;
            it->second.time_us = tag.time_us;
            ++stats_.modifies;
            return true;
        }
        it->second.price = tag.price;
        it->second.quantity = tag.quantity;
        it->second.time_us = tag.time_us;
        it->second.side = side;
        ++stats_.modifies;
        return true;
    }

    BookOrder order;
    order.order_id = tag.order_id;
    order.side = side;
    order.price = tag.price;
    order.quantity = tag.quantity;
    order.time_us = tag.time_us;
    orders_.emplace(tag.order_id, order);
    ++stats_.adds;
    return true;
}

bool IssueOrderBook::remove(const DTag& tag) {
    BookOrder* order = find_order(tag.order_id, tag.side);
    if (!order) {
        return false;
    }

    orders_.erase(tag.order_id);
    ++stats_.deletes;
    return true;
}

bool IssueOrderBook::execute(uint32_t order_id, char side, uint64_t volume) {
    BookOrder* order = find_order(order_id, side);
    if (!order) {
        return false;
    }

    if (volume >= order->quantity) {
        orders_.erase(order_id);
    } else {
        order->quantity -= volume;
    }
    ++stats_.executions;
    return true;
}

void IssueOrderBook::apply_reset_start() {
    clear();
    ++stats_.resets;
}

std::vector<PriceLevel> IssueOrderBook::bid_levels(std::size_t depth) const {
    std::map<uint64_t, PriceLevel, std::greater<uint64_t>> levels;
    for (const auto& [id, order] : orders_) {
        (void)id;
        if (order.side != Side::Buy) {
            continue;
        }
        auto& lvl = levels[order.price];
        lvl.price = order.price;
        lvl.total_quantity += order.quantity;
        ++lvl.order_count;
    }

    std::vector<PriceLevel> out;
    out.reserve(depth ? depth : levels.size());
    for (const auto& [price, lvl] : levels) {
        (void)price;
        out.push_back(lvl);
        if (depth > 0 && out.size() >= depth) {
            break;
        }
    }
    return out;
}

std::vector<PriceLevel> IssueOrderBook::ask_levels(std::size_t depth) const {
    std::map<uint64_t, PriceLevel> levels;
    for (const auto& [id, order] : orders_) {
        (void)id;
        if (order.side != Side::Sell) {
            continue;
        }
        auto& lvl = levels[order.price];
        lvl.price = order.price;
        lvl.total_quantity += order.quantity;
        ++lvl.order_count;
    }

    std::vector<PriceLevel> out;
    out.reserve(depth ? depth : levels.size());
    for (const auto& [price, lvl] : levels) {
        (void)price;
        out.push_back(lvl);
        if (depth > 0 && out.size() >= depth) {
            break;
        }
    }
    return out;
}

void IssueOrderBook::export_json(std::ostream& os, std::size_t depth) const {
    const auto bids = bid_levels(depth);
    const auto asks = ask_levels(depth);

    os << "\"order_count\":" << orders_.size() << ",\"bids\":[";
    for (std::size_t i = 0; i < bids.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        os << "{\"price\":" << bids[i].price << ",\"qty\":" << bids[i].total_quantity
           << ",\"orders\":" << bids[i].order_count << '}';
    }
    os << "],\"asks\":[";
    for (std::size_t i = 0; i < asks.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        os << "{\"price\":" << asks[i].price << ",\"qty\":" << asks[i].total_quantity
           << ",\"orders\":" << asks[i].order_count << '}';
    }
    os << ']';
}

void OrderBookManager::export_all_books_json(std::ostream& os, std::size_t depth) const {
    os << "{\"books\":{";
    bool first = true;
    for (const auto& [issue, book] : books_) {
        if (!first) {
            os << ',';
        }
        first = false;
        os << '"' << issue << "\":{";
        book.export_json(os, depth);
        os << '}';
    }
    os << "}}\n";
}

const IssueOrderBook* OrderBookManager::find_book(const std::string& issue_code) const {
    const auto it = books_.find(issue_code);
    if (it == books_.end()) {
        return nullptr;
    }
    return &it->second;
}

IssueOrderBook& OrderBookManager::book_for(const std::string& issue_code) {
    auto [it, inserted] = books_.try_emplace(issue_code, issue_code);
    return it->second;
}

void OrderBookManager::clear() {
    books_.clear();
    total_stats_ = {};
}

void OrderBookManager::apply_message(const ParsedFlexMessage& msg) {
    const std::string& issue = msg.header.issue_code;
    const bool global_issue = issue.empty();

    auto apply_to_book = [&](IssueOrderBook& book) {
        const BookUpdateStats before = book.stats();

        for (const FlexTag& tag : msg.tags) {
            if (auto phase = parse_r_tag_phase(tag)) {
                if (*phase == 1) {
                    book.apply_reset_start();
                }
                continue;
            }

            if (auto a = parse_a_tag(tag)) {
                book.add(*a);
                continue;
            }

            if (auto d = parse_d_tag(tag)) {
                book.remove(*d);
                continue;
            }

            if (auto e = parse_e_tag(tag)) {
                book.execute(e->order_id, e->side, e->volume);
                continue;
            }

            if (auto c = parse_c_tag(tag)) {
                book.execute(c->order_id, c->side, c->volume);
            }
        }

        const BookUpdateStats after = book.stats();
        total_stats_.adds += after.adds - before.adds;
        total_stats_.modifies += after.modifies - before.modifies;
        total_stats_.deletes += after.deletes - before.deletes;
        total_stats_.executions += after.executions - before.executions;
        total_stats_.resets += after.resets - before.resets;
    };

    if (global_issue) {
        for (auto& [code, book] : books_) {
            (void)code;
            apply_to_book(book);
        }
        return;
    }

    apply_to_book(book_for(issue));
}

void OrderBookManager::print_book(
    const std::string& issue_code, std::ostream& os, std::size_t depth) const {
    const auto it = books_.find(issue_code);
    if (it == books_.end()) {
        os << "No book for issue \"" << issue_code << "\"\n";
        return;
    }

    const IssueOrderBook& book = it->second;
    os << "\n=== Order Book: " << issue_code << " (" << book.order_count()
       << " orders) ===\n";

    os << std::setw(12) << "BID PX" << std::setw(10) << "BID QTY" << std::setw(8)
       << "ORDERS" << " | " << std::setw(12) << "ASK PX" << std::setw(10) << "ASK QTY"
       << std::setw(8) << "ORDERS" << '\n';

    const auto bids = book.bid_levels(depth);
    const auto asks = book.ask_levels(depth);
    const std::size_t rows = std::max(bids.size(), asks.size());

    for (std::size_t i = 0; i < rows; ++i) {
        if (i < bids.size()) {
            os << std::setw(12) << format_price_raw(bids[i].price) << std::setw(10)
               << bids[i].total_quantity << std::setw(8) << bids[i].order_count;
        } else {
            os << std::setw(12) << "" << std::setw(10) << "" << std::setw(8) << "";
        }

        os << " | ";

        if (i < asks.size()) {
            os << std::setw(12) << format_price_raw(asks[i].price) << std::setw(10)
               << asks[i].total_quantity << std::setw(8) << asks[i].order_count;
        }
        os << '\n';
    }
}

void OrderBookManager::print_all_books(std::ostream& os, std::size_t depth) const {
    if (books_.empty()) {
        os << "\n(No order books built)\n";
        return;
    }

    std::vector<std::string> issues;
    issues.reserve(books_.size());
    for (const auto& [issue, book] : books_) {
        (void)book;
        issues.push_back(issue);
    }
    std::sort(issues.begin(), issues.end());

    for (const std::string& issue : issues) {
        print_book(issue, os, depth);
    }
}
