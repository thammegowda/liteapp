#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liteapp/core/result.h"

namespace liteapp::vfs {

[[nodiscard]] std::string content_type(const std::filesystem::path& path);

struct Limits {
    std::size_t max_archive_bytes{16U * 1024U * 1024U};
    std::size_t max_files{256};
    std::size_t max_file_bytes{8U * 1024U * 1024U};
    std::size_t max_total_bytes{32U * 1024U * 1024U};
    std::size_t max_path_bytes{1024};
};

struct FileInfo {
    std::string path;
    std::string name;
    std::string extension;
    std::string media_type;
    std::optional<std::string> text_encoding;
    std::size_t size{0};
};

struct File {
    FileInfo info;
    std::shared_ptr<const std::vector<std::byte>> contents;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return *contents; }
};

class FileSystem {
public:
    [[nodiscard]] static Result<FileSystem> from_zip(
        std::span<const std::byte> archive,
        Limits limits = {});

    [[nodiscard]] Result<std::optional<File>> read(std::string_view path) const;
    [[nodiscard]] Result<std::optional<FileInfo>> stat(std::string_view path) const;

private:
    struct Impl;

    explicit FileSystem(std::shared_ptr<Impl> impl) : impl_{std::move(impl)} {}

    std::shared_ptr<Impl> impl_;
};

}  // namespace liteapp::vfs
