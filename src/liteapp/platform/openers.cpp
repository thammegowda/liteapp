#include "liteapp/platform/openers.h"

#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;
#endif

namespace liteapp::platform {
namespace {

bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool path_is_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

#if defined(_WIN32)
Result<std::wstring> utf8_to_wide(std::string_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(Error{"invalid_url", "URL is too long"});
    }
    const auto size = static_cast<int>(value.size());
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, nullptr, 0);
    if (required <= 0) return std::unexpected(Error{"invalid_url", "URL is not valid UTF-8"});
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, wide.data(), required) != required) {
        return std::unexpected(Error{"invalid_url", "URL conversion failed"});
    }
    return wide;
}

Result<void> reveal_windows(const std::filesystem::path& file) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        return std::unexpected(Error{"open_path", "Windows shell initialization failed"});
    }
    auto* item = ILCreateFromPathW(file.c_str());
    auto* parent = item ? ILCloneFull(item) : nullptr;
    HRESULT result = E_FAIL;
    if (item && parent && ILRemoveLastID(parent)) {
        const auto* child = ILFindLastID(item);
        result = SHOpenFolderAndSelectItems(parent, 1, &child, 0);
    }
    ILFree(parent);
    ILFree(item);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (FAILED(result)) {
        return std::unexpected(Error{"open_path", "Windows File Explorer could not reveal the file"});
    }
    return {};
}
#else
bool spawn_opener(const char* program, const std::vector<std::string>& arguments) {
    std::vector<char*> values;
    values.reserve(arguments.size() + 2U);
    values.push_back(const_cast<char*>(program));
    for (const auto& argument : arguments) values.push_back(const_cast<char*>(argument.c_str()));
    values.push_back(nullptr);

    pid_t process = 0;
    if (posix_spawnp(&process, program, nullptr, nullptr, values.data(), environ) != 0) return false;
    int status = 0;
    while (waitpid(process, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

}  // namespace

std::optional<std::string> environment(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value || *value == '\0') {
        std::free(value);
        return std::nullopt;
    }
    std::string result{value};
    std::free(value);
    return result;
#else
    if (const auto* value = std::getenv(name); value && *value) return std::string{value};
    return std::nullopt;
#endif
}

Result<void> open_url(std::string_view url) {
    if (url.empty()) return std::unexpected(Error{"invalid_url", "URL must not be empty"});
#if defined(_WIN32)
    auto wide_url = utf8_to_wide(url);
    if (!wide_url) return std::unexpected(wide_url.error());
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", wide_url->c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) return {};
#elif defined(__APPLE__)
    if (spawn_opener("open", {std::string{url}})) return {};
#else
    const std::string value{url};
    if (spawn_opener("xdg-open", {value})) return {};
#endif
    return std::unexpected(Error{"open_failed", "no supported URL opener was available"});
}

Result<void> open_path(const std::filesystem::path& path) {
    if (!path_exists(path)) return std::unexpected(Error{"open_path", "filesystem item does not exist"});
#if defined(_WIN32)
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) return {};
#elif defined(__APPLE__)
    if (spawn_opener("open", {path.string()})) return {};
#else
    const auto value = path.string();
    if (spawn_opener("xdg-open", {value})) return {};
#endif
    return std::unexpected(Error{"open_path", "no associated native application was available"});
}

Result<void> reveal_in_file_manager(const std::filesystem::path& file) {
    if (!path_is_file(file)) return std::unexpected(Error{"open_path", "file does not exist"});
#if defined(_WIN32)
    return reveal_windows(file);
#elif defined(__APPLE__)
    if (spawn_opener("open", {"-R", file.string()})) return {};
    return std::unexpected(Error{"open_path", "no supported file manager was available"});
#else
    const auto parent = file.parent_path().string();
    if (spawn_opener("xdg-open", {parent})) return {};
    return std::unexpected(Error{"open_path", "no supported file manager was available"});
#endif
}

}  // namespace liteapp::platform