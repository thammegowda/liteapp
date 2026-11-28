#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace liteapp::testing {

// A fresh directory under the system temporary directory, removed with everything in it at scope exit.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view prefix = "liteapp-test") {
        static std::atomic<unsigned> sequence{0};
        const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               (std::string(prefix) + "-" + std::to_string(id) + "-" + std::to_string(sequence++));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

}  // namespace liteapp::testing
