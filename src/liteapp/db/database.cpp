#include "liteapp/db/database.h"

#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include <sqlite3.h>

namespace liteapp::db {
namespace detail {

struct Connection {
    sqlite3* handle{};
    ~Connection() { if (handle) sqlite3_close_v2(handle); }
};

}  // namespace detail
namespace {

struct StatementCloser { void operator()(sqlite3_stmt* value) const noexcept { sqlite3_finalize(value); } };
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

Error db_error(const detail::Connection& connection, std::string_view operation) {
    return {"sqlite", std::string{operation} + ": " + sqlite3_errmsg(connection.handle)};
}

std::string utf8_path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

Result<void> script(const detail::Connection& connection, std::string_view sql) {
    char* message = nullptr;
    const auto code = sqlite3_exec(connection.handle, std::string{sql}.c_str(), nullptr, nullptr, &message);
    std::unique_ptr<char, decltype(&sqlite3_free)> guard{message, sqlite3_free};
    if (code != SQLITE_OK) {
        return std::unexpected(Error{"sqlite", message ? message : sqlite3_errmsg(connection.handle)});
    }
    return {};
}

Result<Statement> prepare(const detail::Connection& connection, std::string_view sql) {
    if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(Error{"sqlite", "SQL statement is too large"});
    }
    sqlite3_stmt* statement = nullptr;
    const auto code =
        sqlite3_prepare_v2(connection.handle, sql.data(), static_cast<int>(sql.size()), &statement, nullptr);
    if (code != SQLITE_OK) return std::unexpected(db_error(connection, "prepare"));
    return Statement{statement};
}

Result<void> bind(sqlite3_stmt* statement, const std::vector<Value>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto position = static_cast<int>(index + 1U);
        const auto code = std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return sqlite3_bind_null(statement, position);
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return sqlite3_bind_int64(statement, position, value);
            } else if constexpr (std::is_same_v<T, double>) {
                return sqlite3_bind_double(statement, position, value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return sqlite3_bind_text64(
                    statement, position, value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
            } else {
                if (value.empty()) return sqlite3_bind_zeroblob(statement, position, 0);
                return sqlite3_bind_blob64(statement, position, value.data(), value.size(), SQLITE_TRANSIENT);
            }
        }, values[index]);
        if (code != SQLITE_OK) return std::unexpected(Error{"sqlite", sqlite3_errstr(code)});
    }
    return {};
}

Row read_row(sqlite3_stmt* statement) {
    std::vector<Column> columns;
    const auto count = sqlite3_column_count(statement);
    columns.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        Value value;
        const auto type = sqlite3_column_type(statement, index);
        if (type == SQLITE_INTEGER) value = sqlite3_column_int64(statement, index);
        else if (type == SQLITE_FLOAT) value = sqlite3_column_double(statement, index);
        else if (type == SQLITE_TEXT) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
            value = std::string{text, static_cast<std::size_t>(sqlite3_column_bytes(statement, index))};
        } else if (type == SQLITE_BLOB) {
            const auto size = sqlite3_column_bytes(statement, index);
            const auto* bytes = static_cast<const std::byte*>(sqlite3_column_blob(statement, index));
            value = size == 0 ? Blob{} : Blob{bytes, bytes + size};
        } else value = nullptr;
        columns.push_back({sqlite3_column_name(statement, index), std::move(value)});
    }
    return Row{std::move(columns)};
}

Generator<Result<Row>> query_rows(
    std::shared_ptr<detail::Connection> connection,
    std::string sql,
    std::vector<Value> parameters) {
    auto statement = prepare(*connection, sql);
    if (!statement) { co_yield std::unexpected(statement.error()); co_return; }
    if (auto bound = bind(statement->get(), parameters); !bound) {
        co_yield std::unexpected(bound.error());
        co_return;
    }
    while (true) {
        const auto code = sqlite3_step(statement->get());
        if (code == SQLITE_ROW) co_yield read_row(statement->get());
        else if (code == SQLITE_DONE) co_return;
        else { co_yield std::unexpected(db_error(*connection, "query")); co_return; }
    }
}

Result<int> schema_version(const detail::Connection& connection) {
    auto statement = prepare(connection, "PRAGMA user_version");
    if (!statement || sqlite3_step(statement->get()) != SQLITE_ROW) {
        return std::unexpected(db_error(connection, "read schema version"));
    }
    return sqlite3_column_int(statement->get(), 0);
}

}  // namespace

const Value* Row::find(std::string_view name) const noexcept {
    for (const auto& column : columns_) if (column.name == name) return &column.value;
    return nullptr;
}

Result<Database> Database::open(const std::filesystem::path& path) {
    auto connection = std::make_shared<detail::Connection>();
    const auto name = utf8_path(path);
    const auto flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const auto code = sqlite3_open_v2(name.c_str(), &connection->handle, flags, nullptr);
    if (code != SQLITE_OK) {
        const auto* message = connection->handle ? sqlite3_errmsg(connection->handle) : sqlite3_errstr(code);
        return std::unexpected(Error{"sqlite", std::string{"open: "} + message});
    }
    sqlite3_extended_result_codes(connection->handle, 1);
    sqlite3_busy_timeout(connection->handle, 5'000);
    return Database{std::move(connection)};
}

Result<void> Database::execute(std::string_view sql) const { return script(*connection_, sql); }

Result<void> Database::execute(std::string_view sql, std::vector<Value> parameters) const {
    auto statement = prepare(*connection_, sql);
    if (!statement) return std::unexpected(statement.error());
    if (auto bound = bind(statement->get(), parameters); !bound) return bound;
    if (sqlite3_step(statement->get()) != SQLITE_DONE) return std::unexpected(db_error(*connection_, "execute"));
    return {};
}

Generator<Result<Row>> Database::query(std::string sql, std::vector<Value> parameters) const {
    return query_rows(connection_, std::move(sql), std::move(parameters));
}

Result<void> Database::migrate(std::span<const Migration> migrations) const {
    auto current = schema_version(*connection_);
    if (!current) return std::unexpected(current.error());
    int previous = 0;
    for (const auto& migration : migrations) {
        if (migration.version <= previous) return std::unexpected(Error{"migration", "versions must increase"});
        previous = migration.version;
    }
    if (*current > previous && !migrations.empty()) {
        return std::unexpected(Error{"migration", "database schema is newer than this application"});
    }
    if (auto begun = script(*connection_, "BEGIN IMMEDIATE"); !begun) return begun;
    for (const auto& migration : migrations) {
        if (migration.version <= *current) continue;
        if (auto applied = script(*connection_, migration.sql); !applied) {
            static_cast<void>(script(*connection_, "ROLLBACK"));
            return applied;
        }
        if (auto updated = script(*connection_, "PRAGMA user_version=" + std::to_string(migration.version)); !updated) {
            static_cast<void>(script(*connection_, "ROLLBACK"));
            return updated;
        }
    }
    auto committed = script(*connection_, "COMMIT");
    if (!committed) static_cast<void>(script(*connection_, "ROLLBACK"));
    return committed;
}

Result<Transaction> Database::transaction() const {
    if (auto begun = script(*connection_, "BEGIN IMMEDIATE"); !begun) return std::unexpected(begun.error());
    return Transaction{connection_};
}

std::int64_t Database::last_insert_row_id() const noexcept { return sqlite3_last_insert_rowid(connection_->handle); }

Transaction::~Transaction() { if (active_) static_cast<void>(script(*connection_, "ROLLBACK")); }
Transaction::Transaction(Transaction&& other) noexcept
    : connection_{std::move(other.connection_)}, active_{std::exchange(other.active_, false)} {}
Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        if (active_) static_cast<void>(script(*connection_, "ROLLBACK"));
        connection_ = std::move(other.connection_);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}
Result<void> Transaction::commit() {
    if (!active_) return std::unexpected(Error{"transaction", "transaction is no longer active"});
    auto result = script(*connection_, "COMMIT");
    if (result) active_ = false;
    return result;
}

}  // namespace liteapp::db