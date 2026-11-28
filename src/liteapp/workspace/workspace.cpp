#include "liteapp/workspace/workspace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define LITEAPP_WORKSPACE_POSIX 1
#include "liteapp/core/posix.h"
#endif

namespace liteapp::workspace {
namespace {

std::unexpected<Error> failure(std::string code, std::string message) {
    return std::unexpected(Error{std::move(code), std::move(message)});
}

std::unexpected<Error> invalid(std::string message) {
    return failure("invalid", std::move(message));
}

// The components of a relative path; "." parts are dropped and denied names are rejected.
Result<std::vector<std::string>> components(std::string_view path, bool allow_root, const std::string& reserved) {
    if (path.find('\0') != std::string_view::npos || std::filesystem::path(path).is_absolute())
        return invalid("expected a relative workspace path");
    if (path.empty() && !allow_root) return invalid("expected a nonempty relative workspace path");
    std::vector<std::string> parts;
    for (const auto& part : std::filesystem::path(path)) {
        const auto text = part.string();
        auto folded = text;
        std::ranges::transform(
            folded, folded.begin(), [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
        if (text == ".." || folded == ".git" || text.starts_with(reserved))
            return invalid("parent traversal, .git and reserved temporary paths are denied");
        if (text != "." && !text.empty()) parts.push_back(text);
    }
    if (parts.empty() && !allow_root) return invalid("path must name a file or subdirectory");
    return parts;
}

#if defined(LITEAPP_WORKSPACE_POSIX)
std::unexpected<Error> cancelled() {
    return failure("cancelled", "cancelled");
}

bool valid_utf8(std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        if (lead == 0) return false;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        std::size_t length = 0;
        std::uint32_t code_point = 0;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            code_point = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            code_point = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            code_point = lead & 0x07U;
        } else {
            return false;
        }
        if (index + length > text.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        constexpr std::array<std::uint32_t, 5> minimum{0, 0, 0x80, 0x800, 0x10000};
        if (code_point < minimum[length] || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
            return false;
        index += length;
    }
    return true;
}

using Fd = posix::FileDescriptor;

std::unexpected<Error> io_error(std::string operation) {
    return failure("io", std::move(operation) + ": " + std::strerror(errno));
}

std::int64_t modified_ns(const struct stat& info) {
#if defined(__APPLE__)
    return static_cast<std::int64_t>(info.st_mtimespec.tv_sec) * 1000000000 + info.st_mtimespec.tv_nsec;
#else
    return static_cast<std::int64_t>(info.st_mtim.tv_sec) * 1000000000 + info.st_mtim.tv_nsec;
#endif
}
#endif

}  // namespace

struct Workspace::State {
    std::filesystem::path root;
    Options options;
#if defined(LITEAPP_WORKSPACE_POSIX)
    Fd descriptor;

    Result<Fd> directory(std::span<const std::string> parts) const {
        Fd current(::openat(descriptor.get(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (current.get() < 0) return io_error("open workspace");
        for (const auto& part : parts) {
            Fd next(::openat(current.get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (next.get() < 0) return io_error("open directory " + part);
            current = std::move(next);
        }
        return current;
    }

    // Opens a regular file for reading without following a final symlink.
    Result<std::pair<Fd, struct stat>> open_file(std::span<const std::string> parts) const {
        auto parent = directory(parts.first(parts.size() - 1));
        if (!parent) return std::unexpected(parent.error());
        Fd file(::openat(parent->get(), parts.back().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        if (file.get() < 0) return io_error("open file");
        struct stat info{};
        if (::fstat(file.get(), &info) < 0) return io_error("stat file");
        if (!S_ISREG(info.st_mode)) return invalid("not a regular file");
        return std::pair{std::move(file), info};
    }
#endif
};

Workspace::Workspace(std::shared_ptr<const State> state) : state_(std::move(state)) {}

Result<Workspace> Workspace::open(const std::filesystem::path& root, Options options) {
    const auto& limits = options.limits;
    if (!limits.max_file_bytes || !limits.max_directory_entries || !limits.max_command_output ||
        limits.command_timeout.count() <= 0)
        return invalid("workspace limits must be positive");
    if (options.temporary_prefix.size() < 2 || options.temporary_prefix.front() != '.')
        return invalid("the temporary prefix must be a hidden name");
#if defined(LITEAPP_WORKSPACE_POSIX)
    Fd descriptor(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (descriptor.get() < 0) return io_error("open workspace root");
    auto state = std::make_shared<State>();
    state->root = root;
    state->options = std::move(options);
    state->descriptor = std::move(descriptor);
    return Workspace(std::move(state));
#else
    static_cast<void>(root);
    return failure("unsupported", "workspaces require POSIX descriptor-relative file APIs");
#endif
}

const std::filesystem::path& Workspace::root() const { return state_->root; }

const Options& Workspace::options() const { return state_->options; }

Result<std::string> Workspace::normalize(std::string_view path, bool allow_root) const {
    if (allow_root && path == ".") path = "";
    auto parts = components(path, allow_root, state_->options.temporary_prefix);
    if (!parts) return std::unexpected(parts.error());
    std::string result;
    for (const auto& part : *parts) result += (result.empty() ? "" : "/") + part;
    return result;
}

#if defined(LITEAPP_WORKSPACE_POSIX)

Result<std::vector<Entry>> Workspace::list(std::string_view path) const {
    const auto& reserved = state_->options.temporary_prefix;
    auto parts = components(path == "." ? "" : path, true, reserved);
    if (!parts) return std::unexpected(parts.error());
    auto fd = state_->directory(*parts);
    if (!fd) return std::unexpected(fd.error());
    const auto duplicate = ::fcntl(fd->get(), F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) return io_error("duplicate directory");
    auto* raw = ::fdopendir(duplicate);
    if (!raw) {
        ::close(duplicate);
        return io_error("list directory");
    }
    const auto close_dir = [](DIR* value) { ::closedir(value); };
    std::unique_ptr<DIR, decltype(close_dir)> dir(raw, close_dir);
    std::vector<Entry> result;
    while (true) {
        errno = 0;
        const auto* entry = ::readdir(dir.get());
        if (!entry) {
            if (errno) return io_error("read directory");
            break;
        }
        const std::string name(entry->d_name);
        if (name == "." || name == ".." || name == ".git" || name.starts_with(reserved)) continue;
        if (result.size() >= state_->options.limits.max_directory_entries)
            return invalid("directory exceeds entry limit");
        struct stat info{};
        EntryKind kind = EntryKind::other;
        std::uint64_t size = 0;
        if (::fstatat(fd->get(), name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
            kind = S_ISDIR(info.st_mode) ? EntryKind::directory
                   : S_ISREG(info.st_mode) ? EntryKind::file
                                           : EntryKind::other;
            size = static_cast<std::uint64_t>(std::max<off_t>(info.st_size, 0));
        }
        result.push_back({name, kind, size});
    }
    std::ranges::sort(result, [](const Entry& left, const Entry& right) {
        const bool left_directory = left.kind == EntryKind::directory;
        const bool right_directory = right.kind == EntryKind::directory;
        return left_directory != right_directory ? left_directory : left.name < right.name;
    });
    return result;
}

Result<Text> Workspace::read(std::string_view path) const {
    auto parts = components(path, false, state_->options.temporary_prefix);
    if (!parts) return std::unexpected(parts.error());
    auto opened = state_->open_file(*parts);
    if (!opened) return std::unexpected(opened.error());
    auto& [file, info] = *opened;
    const auto limit = state_->options.limits.max_file_bytes;
    if (static_cast<std::uintmax_t>(info.st_size) > limit)
        return failure("too_large", "file is larger than " + std::to_string(limit) + " bytes");
    std::string content;
    std::array<char, 65536> buffer{};
    for (;;) {
        const auto count = ::read(file.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return io_error("read file");
        if (count == 0) break;
        content.append(buffer.data(), static_cast<std::size_t>(count));
        if (content.size() > limit) return failure("too_large", "file grew while reading");
    }
    if (!valid_utf8(content)) return failure("binary", "binary file");
    return Text{std::move(content), modified_ns(info)};
}

Result<Page> Workspace::read_page(
    std::string_view path,
    std::uint64_t offset,
    std::size_t max_bytes,
    std::stop_token stop) const {
    const auto limit = state_->options.limits.max_file_bytes;
    if (!max_bytes || max_bytes > limit || max_bytes == std::numeric_limits<std::size_t>::max())
        return invalid("max_bytes must be from 1 to " + std::to_string(limit));
    auto parts = components(path, false, state_->options.temporary_prefix);
    if (!parts) return std::unexpected(parts.error());
    auto opened = state_->open_file(*parts);
    if (!opened) return std::unexpected(opened.error());
    auto& [file, info] = *opened;
    if (info.st_size < 0 || offset > static_cast<std::uintmax_t>(info.st_size))
        return invalid("offset exceeds file size");
    if (::lseek(file.get(), static_cast<off_t>(offset), SEEK_SET) < 0) return io_error("seek file");
    std::string content;
    std::array<char, 4096> buffer{};
    while (content.size() <= max_bytes) {
        if (stop.stop_requested()) return cancelled();
        const auto count = ::read(file.get(), buffer.data(), std::min(buffer.size(), max_bytes + 1 - content.size()));
        if (count < 0) {
            if (errno == EINTR) continue;
            return io_error("read file");
        }
        if (count == 0) break;
        content.append(buffer.data(), static_cast<std::size_t>(count));
    }
    const bool truncated = content.size() > max_bytes;
    if (truncated) {
        auto end = max_bytes;
        while (end && (static_cast<unsigned char>(content[end]) & 0xC0) == 0x80) --end;
        if (!end) return invalid("max_bytes is too small for the next UTF-8 character");
        content.resize(end);
    }
    if (!valid_utf8(content)) return invalid("expected UTF-8 text without NUL bytes");
    Page page;
    page.offset = offset;
    page.total_bytes = static_cast<std::uint64_t>(info.st_size);
    if (truncated) page.next_offset = offset + content.size();
    page.content = std::move(content);
    return page;
}

Result<std::int64_t> Workspace::write(
    std::string_view path,
    std::string_view content,
    std::optional<std::int64_t> expected_modified_ns,
    std::stop_token stop) const {
    if (stop.stop_requested()) return cancelled();
    const auto& reserved = state_->options.temporary_prefix;
    auto parts = components(path, false, reserved);
    if (!parts) return std::unexpected(parts.error());
    if (content.size() > state_->options.limits.max_file_bytes)
        return failure("too_large", "content exceeds max_file_bytes");
    if (!valid_utf8(content)) return invalid("expected UTF-8 text without NUL bytes");
    auto parent = state_->directory(std::span(*parts).first(parts->size() - 1));
    if (!parent) return std::unexpected(parent.error());
    struct stat existing{};
    mode_t mode = 0600;
    std::int64_t current = 0;
    if (::fstatat(parent->get(), parts->back().c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(existing.st_mode)) return invalid("writes require a regular file or a new path");
        mode = existing.st_mode & 0777;
        current = modified_ns(existing);
    } else if (errno != ENOENT) {
        return io_error("stat destination");
    }
    if (expected_modified_ns && *expected_modified_ns != current)
        return failure("conflict", current ? "file changed on disk since it was opened" : "file no longer exists");
    static std::atomic<std::uint64_t> sequence{0};
    std::string temporary;
    Fd file;
    for (int attempt = 0; attempt < 32; ++attempt) {
        temporary = reserved + std::to_string(::getpid()) + "-" + std::to_string(sequence.fetch_add(1));
        const auto flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC;
        file = Fd(::openat(parent->get(), temporary.c_str(), flags, 0600));
        if (file.get() >= 0) break;
        if (errno != EEXIST) return io_error("create temporary file");
    }
    if (file.get() < 0) return io_error("create unique temporary file");
    struct Cleanup {
        int parent;
        const std::string& name;
        ~Cleanup() { ::unlinkat(parent, name.c_str(), 0); }
    } cleanup{parent->get(), temporary};
    for (std::size_t offset = 0; offset < content.size();) {
        if (stop.stop_requested()) return cancelled();
        const auto count = ::write(file.get(), content.data() + offset, content.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return io_error("write file");
        offset += static_cast<std::size_t>(count);
    }
    if (::fchmod(file.get(), mode) < 0 || ::fsync(file.get()) < 0) return io_error("flush file");
    struct stat written{};
    if (::fstat(file.get(), &written) < 0) return io_error("stat file");
    if (stop.stop_requested()) return cancelled();
    // Atomic replacement never writes through an existing hard link or a raced symlink.
    if (::renameat(parent->get(), temporary.c_str(), parent->get(), parts->back().c_str()) < 0)
        return io_error("replace file");
    return modified_ns(written);
}

Result<void> Workspace::make_directory(std::string_view path) const {
    auto parts = components(path, false, state_->options.temporary_prefix);
    if (!parts) return std::unexpected(parts.error());
    auto parent = state_->directory(std::span(*parts).first(parts->size() - 1));
    if (!parent) return std::unexpected(parent.error());
    if (::mkdirat(parent->get(), parts->back().c_str(), 0700) < 0) return io_error("create directory");
    return {};
}

Result<CommandResult> Workspace::execute(const std::vector<std::string>& arguments, std::stop_token stop) const {
    if (arguments.empty() || arguments.front().empty()) return invalid("missing program");
    for (const auto& argument : arguments)
        if (argument.find('\0') != std::string::npos) return invalid("arguments must not contain NUL bytes");
    const auto& limits = state_->options.limits;
    std::array<int, 2> pipe_fds{};
    if (::pipe(pipe_fds.data()) < 0) return io_error("create command pipe");
    Fd read_end(pipe_fds[0]);
    Fd write_end(pipe_fds[1]);
    Fd null_input(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    if (null_input.get() < 0) return io_error("open /dev/null");
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    const auto root = state_->descriptor.get();
    const auto pid = ::fork();
    if (pid < 0) return io_error("start command");
    if (pid == 0) {
        posix::reset_child_signals();
        ::setpgid(0, 0);
        if (::fchdir(root) < 0 || ::dup2(null_input.get(), STDIN_FILENO) < 0 ||
            ::dup2(write_end.get(), STDOUT_FILENO) < 0 || ::dup2(write_end.get(), STDERR_FILENO) < 0)
            ::_exit(126);
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    ::setpgid(pid, pid);
    write_end = Fd();
    CommandResult result;
    bool stopped = false;
    const auto deadline = std::chrono::steady_clock::now() + limits.command_timeout;
    for (std::array<char, 4096> buffer{};;) {
        if (stop.stop_requested()) {
            stopped = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            break;
        }
        pollfd ready{.fd = read_end.get(), .events = POLLIN, .revents = 0};
        const auto polled = ::poll(&ready, 1, 100);
        if (polled < 0 && errno != EINTR) break;
        if (polled <= 0) continue;
        const auto count = ::read(read_end.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        const auto room = limits.max_command_output - std::min(limits.max_command_output, result.output.size());
        result.output.append(buffer.data(), std::min(room, static_cast<std::size_t>(count)));
        result.truncated = result.truncated || static_cast<std::size_t>(count) > room;
    }
    // Kill the whole process group so background children do not outlive a finished or stopped call.
    if (result.timed_out || stopped) ::kill(-pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return io_error("wait for command");
    if (!result.timed_out && !stopped) ::kill(-pid, SIGKILL);
    if (stopped) return cancelled();
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}

#else

Result<std::vector<Entry>> Workspace::list(std::string_view) const {
    return failure("unsupported", "workspaces require POSIX");
}
Result<Text> Workspace::read(std::string_view) const { return failure("unsupported", "workspaces require POSIX"); }
Result<Page> Workspace::read_page(std::string_view, std::uint64_t, std::size_t, std::stop_token) const {
    return failure("unsupported", "workspaces require POSIX");
}
Result<std::int64_t> Workspace::write(
    std::string_view, std::string_view, std::optional<std::int64_t>, std::stop_token) const {
    return failure("unsupported", "workspaces require POSIX");
}
Result<void> Workspace::make_directory(std::string_view) const {
    return failure("unsupported", "workspaces require POSIX");
}
Result<CommandResult> Workspace::execute(const std::vector<std::string>&, std::stop_token) const {
    return failure("unsupported", "workspaces require POSIX");
}

#endif

}  // namespace liteapp::workspace
