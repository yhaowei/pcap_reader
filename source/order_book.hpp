#pragma once

#include "flex_protocol.hpp"
#include "flex_tags.hpp"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

enum class Side { Buy, Sell };

struct BookOrder {
    uint32_t order_id = 0;
    Side side = Side::Buy;
    uint64_t price = 0;
    uint64_t quantity = 0;
    uint32_t time_us = 0;
};

struct PriceLevel {
    uint64_t price = 0;
    uint64_t total_quantity = 0;
    std::size_t order_count = 0;
};

struct BookUpdateStats {
    std::size_t adds = 0;
    std::size_t modifies = 0;
    std::size_t deletes = 0;
    std::size_t executions = 0;
    std::size_t resets = 0;
};

class IssueOrderBook {
public:
    IssueOrderBook() = default;
    explicit IssueOrderBook(std::string issue_code);

    const std::string& issue_code() const { return issue_code_; }

    void clear();
    bool add(const ATag& tag);
    bool remove(const DTag& tag);
    bool execute(uint32_t order_id, char side, uint64_t volume);
    void apply_reset_start();

    std::size_t order_count() const { return orders_.size(); }
    const std::unordered_map<uint32_t, BookOrder>& orders() const { return orders_; }
    // depth 0 = all levels
    std::vector<PriceLevel> bid_levels(std::size_t depth = 0) const;
    std::vector<PriceLevel> ask_levels(std::size_t depth = 0) const;

    void export_json(std::ostream& os, std::size_t depth = 0) const;

    const BookUpdateStats& stats() const { return stats_; }

private:
    std::string issue_code_;
    std::unordered_map<uint32_t, BookOrder> orders_;
    BookUpdateStats stats_;

    BookOrder* find_order(uint32_t order_id, char side);
};

class OrderBookManager {
public:
    void clear();
    IssueOrderBook& book_for(const std::string& issue_code);
    void apply_message(const ParsedFlexMessage& msg);

    void print_book(const std::string& issue_code, std::ostream& os, std::size_t depth) const;
    void print_all_books(std::ostream& os, std::size_t depth) const;

    const BookUpdateStats& total_stats() const { return total_stats_; }
    const std::unordered_map<std::string, IssueOrderBook>& books() const { return books_; }

    const IssueOrderBook* find_book(const std::string& issue_code) const;
    void export_all_books_json(std::ostream& os, std::size_t depth = 0) const;

private:
    std::unordered_map<std::string, IssueOrderBook> books_;
    BookUpdateStats total_stats_;
};
