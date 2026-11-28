#include "liteapp/workspace/git.h"

#include <utility>

namespace liteapp::workspace {

Git::Git(Workspace workspace, std::string prefix) : workspace_(std::move(workspace)), prefix_(std::move(prefix)) {}

std::optional<Git> Git::find(const Workspace& workspace) {
    auto prefix = workspace.execute({"git", "rev-parse", "--show-prefix"});
    if (!prefix || prefix->exit_code != 0) return std::nullopt;
    auto value = std::move(prefix->output);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return Git(workspace, std::move(value));
}

std::vector<GitChange> Git::status() const {
    std::vector<GitChange> result;
    auto status = workspace_.execute(
        {"git", "-c", "core.quotepath=off", "status", "--porcelain=v1", "-z", "--untracked-files=all", "--", "."});
    if (!status || status->exit_code != 0) return result;
    const auto& output = status->output;
    for (std::size_t start = 0; start + 3 < output.size();) {
        const auto end = output.find('\0', start);
        if (end == std::string::npos) break;
        const auto code = output.substr(start, 2);
        auto path = output.substr(start + 3, end - start - 3);
        start = end + 1;
        if (code[0] == 'R' || code[0] == 'C') {
            const auto original = output.find('\0', start);  // renames list the original path next
            start = original == std::string::npos ? output.size() : original + 1;
        }
        if (!path.starts_with(prefix_)) continue;
        path.erase(0, prefix_.size());
        const char* kind = code == "??"                             ? "untracked"
                           : code.find('D') != std::string::npos   ? "deleted"
                           : code.find('A') != std::string::npos   ? "added"
                           : code[0] == 'R'                        ? "renamed"
                                                                   : "modified";
        result.push_back({std::move(path), kind});
    }
    return result;
}

std::optional<std::string> Git::head_text(std::string_view path) const {
    auto normalized = workspace_.normalize(path, false);
    if (!normalized) return std::nullopt;
    auto old = workspace_.execute({"git", "show", "HEAD:./" + *normalized});
    if (!old || old->exit_code != 0 || old->truncated) return std::nullopt;
    return std::move(old->output);
}

}  // namespace liteapp::workspace
