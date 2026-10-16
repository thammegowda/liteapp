#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "liteapp/core/generator.h"
#include "liteapp/core/result.h"

namespace liteapp::db {

using Blob = std::vector<std::byte>;
using Value = std::variant<std::nullptr_t, std::int64_t, double, std::string, Blob>;

struct Column {
    std::string name;
    Value value;
};

class Row {
public:
    explicit Row(std::vector<Column> columns) : columns_{std::move(columns)} {}
    [[nodiscard]] std::span<const Column> columns() const noexcept { return columns_; }
    [[nodiscard]] const Value* find(std::string_view name) const noexcept;
    [[nodiscard]] const Value& operator[](std::size_t index) const { return columns_.at(index).value; }

private:
    std::vector<Column> columns_;
};

struct Migration {
    int version;
    std::string_view sql;
};

namespace detail { struct Connection; }

class Transaction {
public:
    ~Transaction();
    Transaction(Transaction&&) noexcept;
    Transaction& operator=(Transaction&&) noexcept;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    [[nodiscard]] Result<void> commit();

private:
    friend class Database;
    explicit Transaction(std::shared_ptr<detail::Connection> connection) : connection_{std::move(connection)} {}
    std::shared_ptr<detail::Connection> connection_;
    bool active_{true};
};

class Database {
public:
    [[nodiscard]] static Result<Database> open(const std::filesystem::path& path);

    Database(Database&&) noexcept = default;
    Database& operator=(Database&&) noexcept = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    [[nodiscard]] Result<void> execute(std::string_view sql) const;
    [[nodiscard]] Result<void> execute(std::string_view sql, std::vector<Value> parameters) const;
    [[nodiscard]] Generator<Result<Row>> query(std::string sql, std::vector<Value> parameters = {}) const;
    [[nodiscard]] Result<void> migrate(std::span<const Migration> migrations) const;
    [[nodiscard]] Result<Transaction> transaction() const;
    [[nodiscard]] std::int64_t last_insert_row_id() const noexcept;

private:
    explicit Database(std::shared_ptr<detail::Connection> connection) : connection_{std::move(connection)} {}
    std::shared_ptr<detail::Connection> connection_;
};

}  // namespace liteapp::db