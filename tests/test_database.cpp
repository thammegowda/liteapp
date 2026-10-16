#include <array>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "liteapp/db/database.h"

namespace {

std::int64_t integer(const liteapp::db::Row& row, std::string_view name) {
    return std::get<std::int64_t>(*row.find(name));
}

}  // namespace

TEST(Database, AppliesAppSchemaAndQueriesRowsLazily) {
    auto database = liteapp::db::Database::open(":memory:");
    ASSERT_TRUE(database);
    auto lazy_rows = database->query("SELECT body FROM notes");
    constexpr std::array migrations{
        liteapp::db::Migration{1, "CREATE TABLE notes(id INTEGER PRIMARY KEY, body TEXT NOT NULL);"},
        liteapp::db::Migration{2, "CREATE INDEX notes_body ON notes(body);"},
    };
    ASSERT_TRUE(database->migrate(migrations));
    ASSERT_TRUE(database->migrate(migrations));
    ASSERT_TRUE(database->execute("INSERT INTO notes(body) VALUES (?)", {std::string{"first"}}));
    ASSERT_TRUE(database->execute("INSERT INTO notes(body) VALUES (?)", {std::string{"second"}}));

    std::vector<std::string> bodies;
    for (const auto& row : lazy_rows) {
        ASSERT_TRUE(row);
        bodies.push_back(std::get<std::string>(*row->find("body")));
    }
    EXPECT_EQ(bodies, (std::vector<std::string>{"first", "second"}));
    for (const auto& row : database->query("PRAGMA user_version")) {
        ASSERT_TRUE(row);
        EXPECT_EQ(std::get<std::int64_t>((*row)[0]), 2);
    }
}

TEST(Database, IncludesFts5ForApplicationIndexes) {
    auto database = liteapp::db::Database::open(":memory:");
    ASSERT_TRUE(database);
    ASSERT_TRUE(database->execute("CREATE VIRTUAL TABLE search USING fts5(body)"));
    ASSERT_TRUE(database->execute("INSERT INTO search(body) VALUES (?)", {std::string{"hello sqlite"}}));
    for (const auto& row : database->query("SELECT body FROM search WHERE search MATCH 'sqlite'")) {
        ASSERT_TRUE(row);
        EXPECT_EQ(std::get<std::string>((*row)[0]), "hello sqlite");
    }
    for (const auto& row : database->query("PRAGMA secure_delete")) {
        ASSERT_TRUE(row);
        EXPECT_EQ(std::get<std::int64_t>((*row)[0]), 1);
    }
}

TEST(Database, SupportsNullBlobAndParameters) {
    auto database = liteapp::db::Database::open(":memory:");
    ASSERT_TRUE(database);
    ASSERT_TRUE(database->execute("CREATE TABLE values_(id INTEGER, data BLOB, note TEXT)"));
    const liteapp::db::Blob blob{std::byte{1}, std::byte{2}};
    ASSERT_TRUE(database->execute("INSERT INTO values_ VALUES (?, ?, ?)", {std::int64_t{7}, blob, nullptr}));
    ASSERT_TRUE(database->execute("INSERT INTO values_ VALUES (?, ?, ?)",
                                  {std::int64_t{8}, liteapp::db::Blob{}, nullptr}));

    for (const auto& row : database->query("SELECT * FROM values_ WHERE id = ?", {std::int64_t{7}})) {
        ASSERT_TRUE(row);
        EXPECT_EQ(std::get<liteapp::db::Blob>(*row->find("data")), blob);
        EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(*row->find("note")));
    }
    for (const auto& row : database->query("SELECT data FROM values_ WHERE id = 8")) {
        ASSERT_TRUE(row);
        EXPECT_TRUE(std::get<liteapp::db::Blob>((*row)[0]).empty());
    }
}

TEST(Database, TransactionRollsBackUnlessCommitted) {
    auto database = liteapp::db::Database::open(":memory:");
    ASSERT_TRUE(database);
    ASSERT_TRUE(database->execute("CREATE TABLE events(value INTEGER)"));
    {
        auto transaction = database->transaction();
        ASSERT_TRUE(transaction);
        ASSERT_TRUE(database->execute("INSERT INTO events VALUES (1)"));
    }
    for (const auto& row : database->query("SELECT count(*) AS count FROM events")) {
        ASSERT_TRUE(row);
        EXPECT_EQ(integer(*row, "count"), 0);
    }
    auto transaction = database->transaction();
    ASSERT_TRUE(transaction);
    ASSERT_TRUE(database->execute("INSERT INTO events VALUES (2)"));
    ASSERT_TRUE(transaction->commit());
    for (const auto& row : database->query("SELECT count(*) AS count FROM events")) {
        ASSERT_TRUE(row);
        EXPECT_EQ(integer(*row, "count"), 1);
    }
}

TEST(Database, ReportsLazyQueryErrors) {
    auto database = liteapp::db::Database::open(":memory:");
    ASSERT_TRUE(database);
    auto rows = database->query("SELECT missing FROM nowhere");
    const auto iterator = rows.begin();
    ASSERT_FALSE(iterator == rows.end());
    EXPECT_FALSE(*iterator);
}