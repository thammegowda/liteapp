#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liteapp/workspace/workspace.h"

namespace liteapp::workspace {

struct GitChange {
    std::string path;    // relative to the workspace root
    std::string status;  // "modified", "added", "deleted", "renamed" or "untracked"
};

// Read-only Git queries for a workspace inside a repository, run through Workspace::execute.
class Git {
public:
    // The repository containing the workspace root; nullopt when there is none or git is not installed.
    [[nodiscard]] static std::optional<Git> find(const Workspace& workspace);

    // Uncommitted changes under the workspace root (git status, including untracked files).
    [[nodiscard]] std::vector<GitChange> status() const;
    // The committed text of a workspace path at HEAD; nullopt when the path is not in HEAD.
    [[nodiscard]] std::optional<std::string> head_text(std::string_view path) const;

private:
    Git(Workspace workspace, std::string prefix);
    Workspace workspace_;
    std::string prefix_;  // the workspace root relative to the repository root, with a trailing slash
};

}  // namespace liteapp::workspace
