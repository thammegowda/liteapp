#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <liteapp/test_assets.h>

#include "liteapp/vfs/filesystem.h"

namespace {

std::size_t replace_all(
    std::vector<std::byte>& bytes,
    std::string_view from,
    std::string_view to) {
    if (from.size() != to.size()) return 0;
    std::size_t replacements = 0;
    for (std::size_t index = 0; index + from.size() <= bytes.size(); ++index) {
        bool matches = true;
        for (std::size_t offset = 0; offset < from.size(); ++offset) {
            if (bytes[index + offset] != static_cast<std::byte>(from[offset])) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        for (std::size_t offset = 0; offset < to.size(); ++offset) {
            bytes[index + offset] = static_cast<std::byte>(to[offset]);
        }
        ++replacements;
        index += from.size() - 1U;
    }
    return replacements;
}

std::optional<std::size_t> find_signature(
    const std::vector<std::byte>& bytes,
    std::array<std::byte, 4> signature) {
    const auto found = std::ranges::search(bytes, signature);
    if (found.empty()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(bytes.begin(), found.begin()));
}

std::string text(std::span<const std::byte> bytes) {
    std::string result(bytes.size(), '\0');
    std::ranges::transform(bytes, result.begin(), [](std::byte value) {
        return static_cast<char>(std::to_integer<unsigned char>(value));
    });
    return result;
}

}  // namespace

TEST(VirtualFilesystem, InfersHttpContentTypesFromPaths) {
    EXPECT_EQ(liteapp::vfs::content_type("index.HTML"), "text/html; charset=utf-8");
    EXPECT_EQ(liteapp::vfs::content_type("app.mjs"), "text/javascript; charset=utf-8");
    EXPECT_EQ(liteapp::vfs::content_type("icons/logo.svg"), "image/svg+xml");
    EXPECT_EQ(liteapp::vfs::content_type("icons/logo.png"), "image/png");
    EXPECT_EQ(liteapp::vfs::content_type("model.wasm"), "application/wasm");
    EXPECT_EQ(liteapp::vfs::content_type("weights.bin"), "application/octet-stream");
}

TEST(VirtualFilesystem, LazilyReadsAndSharesEmbeddedFilesAcrossThreads) {
    const auto created = liteapp::vfs::FileSystem::from_zip(liteapp::test_assets::archive());
    ASSERT_TRUE(created);
    const auto filesystem = *created;
    std::array<std::shared_ptr<const std::vector<std::byte>>, 8> contents;
    std::vector<std::jthread> readers;
    readers.reserve(contents.size());
    for (std::size_t index = 0; index < contents.size(); ++index) {
        readers.emplace_back([filesystem, &contents, index] {
            const auto result = filesystem.read("/index.html");
            if (result && *result) contents[index] = (**result).contents;
        });
    }
    for (auto& reader : readers) reader.join();

    for (const auto& content : contents) {
        ASSERT_TRUE(content);
        EXPECT_NE(text(*content).find("<!doctype html><title>embedded fixture</title>"), std::string::npos);
        EXPECT_EQ(content, contents.front());
    }
    const auto info = filesystem.stat("/index.html");
    ASSERT_TRUE(info);
    ASSERT_TRUE(*info);
    EXPECT_EQ((**info).path, "index.html");
    EXPECT_EQ((**info).name, "index.html");
    EXPECT_EQ((**info).extension, ".html");
    EXPECT_EQ((**info).media_type, "text/html");
    EXPECT_EQ((**info).text_encoding, "utf-8");
    EXPECT_EQ((**info).size, contents.front()->size());
    const auto module = filesystem.stat("module.mjs");
    ASSERT_TRUE(module);
    ASSERT_TRUE(*module);
    EXPECT_EQ((**module).media_type, "text/javascript");
    EXPECT_EQ((**module).text_encoding, "utf-8");
    const auto missing = filesystem.read("missing.txt");
    ASSERT_TRUE(missing);
    EXPECT_FALSE(*missing);
    EXPECT_FALSE(filesystem.read("../index.html"));
    EXPECT_FALSE(filesystem.read("C:/index.html"));
    const std::string nul_path{"index.html\0ignored", 18};
    EXPECT_FALSE(filesystem.read(nul_path));
}

TEST(VirtualFilesystem, RejectsUnsafeAndDuplicateArchivePaths) {
    const auto archive = liteapp::test_assets::archive();
    std::vector<std::byte> unsafe{archive.begin(), archive.end()};
    ASSERT_GE(replace_all(unsafe, "index.html", "../bad.txt"), 2);
    const auto unsafe_filesystem = liteapp::vfs::FileSystem::from_zip(unsafe);
    ASSERT_TRUE(unsafe_filesystem);
    EXPECT_FALSE(unsafe_filesystem->read("other.html"));

    std::vector<std::byte> duplicate{archive.begin(), archive.end()};
    ASSERT_GE(replace_all(duplicate, "other.html", "index.html"), 2);
    const auto duplicate_filesystem = liteapp::vfs::FileSystem::from_zip(duplicate);
    ASSERT_TRUE(duplicate_filesystem);
    EXPECT_FALSE(duplicate_filesystem->read("index.html"));
}

TEST(VirtualFilesystem, RejectsMalformedAndOversizedArchives) {
    constexpr std::array malformed{
        std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04},
    };
    const auto malformed_filesystem = liteapp::vfs::FileSystem::from_zip(malformed);
    ASSERT_TRUE(malformed_filesystem);
    EXPECT_FALSE(malformed_filesystem->read("index.html"));

    const auto limited = liteapp::vfs::FileSystem::from_zip(
        liteapp::test_assets::archive(),
        liteapp::vfs::Limits{
            .max_archive_bytes = 1024U * 1024U,
            .max_files = 8,
            .max_file_bytes = 8,
            .max_total_bytes = 16,
            .max_path_bytes = 128,
        });
    ASSERT_TRUE(limited);
    EXPECT_FALSE(limited->read("index.html"));
}

TEST(VirtualFilesystem, RejectsEncryptedEntriesAndCrcFailures) {
    const auto archive = liteapp::test_assets::archive();
    std::vector<std::byte> encrypted{archive.begin(), archive.end()};
    const auto central = find_signature(
        encrypted,
        {std::byte{0x50}, std::byte{0x4b}, std::byte{0x01}, std::byte{0x02}});
    ASSERT_TRUE(central);
    encrypted[*central + 8U] |= std::byte{0x01};
    const auto encrypted_filesystem = liteapp::vfs::FileSystem::from_zip(encrypted);
    ASSERT_TRUE(encrypted_filesystem);
    EXPECT_FALSE(encrypted_filesystem->read("index.html"));

    std::vector<std::byte> corrupt{archive.begin(), archive.end()};
    const auto corrupt_central = find_signature(
        corrupt,
        {std::byte{0x50}, std::byte{0x4b}, std::byte{0x01}, std::byte{0x02}});
    ASSERT_TRUE(corrupt_central);
    corrupt[*corrupt_central + 16U] ^= std::byte{0x01};
    const auto corrupt_filesystem = liteapp::vfs::FileSystem::from_zip(corrupt);
    ASSERT_TRUE(corrupt_filesystem);
    EXPECT_FALSE(corrupt_filesystem->read("index.html"));
}
