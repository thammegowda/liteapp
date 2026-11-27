#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "liteapp/core/result.h"

namespace liteapp::workspace {

struct Limits {
    std::size_t max_file_bytes{4U * 1024U * 1024U};
    std::size_t max_directory_entries{20000};
    std::size_t max_command_output{8U * 1024U * 1024U};
    std::chrono::milliseconds command_timeout{20000};
};

struct Options {
    Limits limits{};
    // Atomic writes go through hidden siblings with this prefix; such names are never listed or addressable.
    std::string temporary_prefix{".liteapp-"};
};

enum class EntryKind { file, directory, other };

struct Entry {
    std::string name;
    EntryKind kind{EntryKind::other};
    std::uint64_t size{0};
};

struct Text {
    std::string content;
    std::int64_t modified_ns{0};  // pass back to write() to detect concurrent edits
};

struct Page {
    std::string content;
    std::uint64_t offset{0};
    std::optional<std::uint64_t> next_offset;  // set when the file continues past this page
    std::uint64_t total_bytes{0};
};

struct CommandResult {
    int exit_code{0};
    std::string output;
    bool truncated{false};
    bool timed_out{false};
};

// A directory tree that a local UI or agent may read and edit. Every operation resolves paths component by
// component relative to an open root descriptor without following symlinks, so a path can never leave the root.
// Absolute paths, "..", ".git" and the temporary prefix are rejected. execute() runs programs with the user's
// privileges; it is not a sandbox.
//
// Error codes: "invalid" (path or argument), "io" (operating-system failure), "conflict" (changed on disk since
// read), "binary" (not UTF-8 text), "too_large", "cancelled", and "unsupported" (platform without POSIX *at APIs).
class Workspace {
public:
    [[nodiscard]] static Result<Workspace> open(const std::filesystem::path& root, Options options = {});

    // Directories first, then files, each sorted by name. "" lists the root.
    [[nodiscard]] Result<std::vector<Entry>> list(std::string_view path) const;
    // A whole UTF-8 text file up to max_file_bytes.
    [[nodiscard]] Result<Text> read(std::string_view path) const;
    // Up to max_bytes of UTF-8 text starting at a byte offset, ending on a character boundary.
    [[nodiscard]] Result<Page> read_page(
        std::string_view path,
        std::uint64_t offset,
        std::size_t max_bytes,
        std::stop_token stop = {}) const;
    // Atomically creates or replaces a UTF-8 text file. With expected_modified_ns, fails with "conflict" if the file
    // changed since it was read (0 expects that it does not exist). Returns the new modification time.
    [[nodiscard]] Result<std::int64_t> write(
        std::string_view path,
        std::string_view content,
        std::optional<std::int64_t> expected_modified_ns = std::nullopt,
        std::stop_token stop = {}) const;
    [[nodiscard]] Result<void> make_directory(std::string_view path) const;
    // Runs a program (looked up on PATH) in the root without a shell, bounded by the command timeout and output
    // limit. Stopping or timing out kills the program's whole process group.
    [[nodiscard]] Result<CommandResult> execute(
        const std::vector<std::string>& arguments,
        std::stop_token stop = {}) const;

    // Checks a client-supplied path and returns its normalized form ("" for the root when allowed).
    [[nodiscard]] Result<std::string> normalize(std::string_view path, bool allow_root) const;
    [[nodiscard]] const std::filesystem::path& root() const;
    [[nodiscard]] const Options& options() const;

private:
    struct State;
    explicit Workspace(std::shared_ptr<const State> state);
    std::shared_ptr<const State> state_;
};

}  // namespace liteapp::workspace
