#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "liteapp/core/result.h"

namespace liteapp::platform {

[[nodiscard]] std::optional<std::string> environment(const char* name);
[[nodiscard]] Result<void> open_url(std::string_view url);
[[nodiscard]] Result<void> open_path(const std::filesystem::path& path);
[[nodiscard]] Result<void> reveal_in_file_manager(const std::filesystem::path& file);

}  // namespace liteapp::platform