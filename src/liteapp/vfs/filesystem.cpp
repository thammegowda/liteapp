#include "liteapp/vfs/filesystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <miniz.h>

namespace liteapp::vfs {
namespace {

Error archive_error(std::string message) {
    return {"invalid_archive", std::move(message)};
}

std::optional<std::string> normalized_path(std::string_view source, std::size_t max_path_bytes) {
    while (source.starts_with('/')) source.remove_prefix(1);
    if (source.empty() || source.size() > max_path_bytes || source.find('\\') != std::string_view::npos ||
        source.find('\0') != std::string_view::npos ||
        (source.size() > 1U && std::isalpha(static_cast<unsigned char>(source[0])) && source[1] == ':')) {
        return std::nullopt;
    }
    std::size_t start = 0;
    while (start <= source.size()) {
        const auto end = source.find('/', start);
        const auto component = source.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") return std::nullopt;
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return std::string{source};
}

struct ContentType {
    std::string extension;
    std::string media_type;
    std::optional<std::string> encoding;
};

ContentType classify(const std::filesystem::path& filesystem_path) {
    auto extension = filesystem_path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    auto media_type = std::string{"application/octet-stream"};
    std::optional<std::string> encoding;
    if (extension == ".html") media_type = "text/html";
    else if (extension == ".css") media_type = "text/css";
    else if (extension == ".js" || extension == ".mjs") media_type = "text/javascript";
    else if (extension == ".json") media_type = "application/json";
    else if (extension == ".svg") media_type = "image/svg+xml";
    else if (extension == ".txt") media_type = "text/plain";
    else if (extension == ".png") media_type = "image/png";
    else if (extension == ".jpg" || extension == ".jpeg") media_type = "image/jpeg";
    else if (extension == ".webp") media_type = "image/webp";
    else if (extension == ".ico") media_type = "image/x-icon";
    else if (extension == ".woff2") media_type = "font/woff2";
    else if (extension == ".wasm") media_type = "application/wasm";
    if (media_type.starts_with("text/") || extension == ".js" || extension == ".mjs" ||
        extension == ".json") {
        encoding = "utf-8";
    }
    return {
        .extension = std::move(extension),
        .media_type = std::move(media_type),
        .encoding = std::move(encoding),
    };
}

FileInfo file_info(std::string path, std::size_t size) {
    const auto filesystem_path = std::filesystem::path{path};
    auto type = classify(filesystem_path);
    return FileInfo{
        .path = std::move(path),
        .name = filesystem_path.filename().string(),
        .extension = std::move(type.extension),
        .media_type = std::move(type.media_type),
        .text_encoding = std::move(type.encoding),
        .size = size,
    };
}

class ZipReader {
public:
    explicit ZipReader(std::span<const std::byte> bytes) {
        mz_zip_zero_struct(&archive_);
        initialized_ = mz_zip_reader_init_mem(&archive_, bytes.data(), bytes.size(), 0) != 0;
    }

    ~ZipReader() {
        if (initialized_) static_cast<void>(mz_zip_reader_end(&archive_));
    }

    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] mz_zip_archive* get() noexcept { return &archive_; }

private:
    mz_zip_archive archive_{};
    bool initialized_{false};
};

}  // namespace

std::string content_type(const std::filesystem::path& path) {
    auto type = classify(path);
    if (type.encoding) type.media_type += "; charset=" + *type.encoding;
    return type.media_type;
}

struct FileSystem::Impl {
    explicit Impl(std::span<const std::byte> bytes, Limits configured_limits)
        : archive{bytes.begin(), bytes.end()}, limits{configured_limits} {}

    void load() {
        ZipReader reader{archive};
        if (!reader.initialized()) {
            failure = archive_error("embedded ZIP cannot be opened");
            return;
        }
        const auto count = static_cast<std::size_t>(mz_zip_reader_get_num_files(reader.get()));
        if (count > limits.max_files) {
            failure = archive_error("embedded ZIP contains too many entries");
            return;
        }
        std::size_t total_size = 0;
        for (std::size_t index = 0; index < count; ++index) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(reader.get(), static_cast<mz_uint>(index), &stat)) {
                failure = archive_error("embedded ZIP entry metadata is invalid");
                return;
            }
            const auto filename_size = static_cast<std::size_t>(
                mz_zip_reader_get_filename(reader.get(), static_cast<mz_uint>(index), nullptr, 0));
            if (filename_size < 2U || filename_size > limits.max_path_bytes + 1U) {
                failure = archive_error("embedded ZIP entry path is invalid");
                return;
            }
            std::string filename(filename_size, '\0');
            if (mz_zip_reader_get_filename(reader.get(), static_cast<mz_uint>(index), filename.data(),
                                           static_cast<mz_uint>(filename.size())) != filename_size) {
                failure = archive_error("embedded ZIP entry path cannot be read");
                return;
            }
            filename.pop_back();
            if (stat.m_is_directory) continue;
            const auto path = normalized_path(filename, limits.max_path_bytes);
            if (!path || *path != filename) {
                failure = archive_error("embedded ZIP contains an unsafe entry path");
                return;
            }
            if (stat.m_is_encrypted || !stat.m_is_supported) {
                failure = archive_error("embedded ZIP contains an unsupported entry");
                return;
            }
            if (stat.m_uncomp_size > limits.max_file_bytes ||
                stat.m_uncomp_size > std::numeric_limits<std::size_t>::max()) {
                failure = archive_error("embedded ZIP entry exceeds the file limit");
                return;
            }
            const auto file_size = static_cast<std::size_t>(stat.m_uncomp_size);
            if (file_size > limits.max_total_bytes - total_size) {
                failure = archive_error("embedded ZIP exceeds the expanded size limit");
                return;
            }
            auto contents = std::make_shared<std::vector<std::byte>>(file_size);
            std::byte empty_buffer{};
            auto* output = contents->empty() ? static_cast<void*>(&empty_buffer)
                                             : static_cast<void*>(contents->data());
            if (!mz_zip_reader_extract_to_mem(
                    reader.get(), static_cast<mz_uint>(index), output, contents->size(), 0)) {
                failure = archive_error("embedded ZIP entry extraction failed");
                return;
            }
            if (!files.emplace(*path, File{file_info(*path, file_size), std::move(contents)}).second) {
                failure = archive_error("embedded ZIP contains duplicate entry paths");
                return;
            }
            total_size += file_size;
        }
        archive.clear();
        archive.shrink_to_fit();
    }

    std::vector<std::byte> archive;
    Limits limits;
    std::once_flag load_once;
    std::optional<Error> failure;
    std::unordered_map<std::string, File> files;
};

Result<FileSystem> FileSystem::from_zip(std::span<const std::byte> archive, Limits limits) {
    if (archive.empty()) return std::unexpected(archive_error("embedded ZIP is empty"));
    if (archive.size() > limits.max_archive_bytes) {
        return std::unexpected(archive_error("embedded ZIP exceeds the archive size limit"));
    }
    if (limits.max_files == 0 || limits.max_file_bytes == 0 || limits.max_total_bytes == 0 ||
        limits.max_path_bytes == 0) {
        return std::unexpected(archive_error("embedded ZIP limits must be nonzero"));
    }
    return FileSystem{std::make_shared<Impl>(archive, limits)};
}

Result<std::optional<File>> FileSystem::read(std::string_view path) const {
    const auto normalized = normalized_path(path, impl_->limits.max_path_bytes);
    if (!normalized) return std::unexpected(archive_error("virtual file path is invalid"));
    std::call_once(impl_->load_once, [this] { impl_->load(); });
    if (impl_->failure) return std::unexpected(*impl_->failure);
    const auto found = impl_->files.find(*normalized);
    if (found == impl_->files.end()) return std::optional<File>{};
    return std::optional<File>{found->second};
}

Result<std::optional<FileInfo>> FileSystem::stat(std::string_view path) const {
    const auto file = read(path);
    if (!file) return std::unexpected(file.error());
    if (!*file) return std::optional<FileInfo>{};
    return std::optional<FileInfo>{(**file).info};
}

}  // namespace liteapp::vfs
