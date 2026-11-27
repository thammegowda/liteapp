#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "liteapp/service/router.h"
#include "liteapp/workspace/git.h"
#include "liteapp/workspace/terminal.h"
#include "liteapp/workspace/workspace.h"

namespace liteapp::workspace {

struct ServiceOptions {
    // Called after a client writes a file through fs.write, e.g. to publish a "files_changed" event.
    std::function<void(const std::vector<std::string>& paths)> files_written;
    bool terminals{true};
};

// The IDE back end for one workspace, as service methods:
//
//   fs.list {path}                    -> {entries: [{name, kind: "file"|"directory"|"other", size}]}
//   fs.read {path}                    -> {path, content, modified_ns}
//   fs.write {path, content, modified_ns?} -> {path, modified_ns}   (modified_ns 0 creates; mismatch: "conflict")
//     modified_ns is a decimal string: nanosecond times exceed JavaScript's exact integers.
//   fs.mkdir {path}                   -> {path}
//   git.status {}                     -> {repository, changes: [{path, status}]}
//   git.diff {path}                   -> {path, old, old_exists, new, new_exists, modified_ns}   (HEAD vs disk)
//   terminal.open {cols, rows}        -> async: "opened" {id}, "output" {data: base64}; completes {exit_code}
//   terminal.input {id, data}         -> {}
//   terminal.resize {id, cols, rows}  -> {}
//
// The browser counterparts are web/ide/{file-tree,editor,terminal}.mjs.
class WorkspaceServices {
public:
    explicit WorkspaceServices(Workspace workspace, ServiceOptions options = {});
    ~WorkspaceServices();
    WorkspaceServices(const WorkspaceServices&) = delete;
    WorkspaceServices& operator=(const WorkspaceServices&) = delete;

    // The router must not outlive this object.
    void register_methods(ServiceRouter& router);
    [[nodiscard]] const Workspace& workspace() const;
    // The containing Git repository, found once at construction.
    [[nodiscard]] const std::optional<Git>& git() const;
    // Closes every terminal; later terminal.open requests fail.
    void close();

private:
    Workspace workspace_;
    ServiceOptions options_;
    std::optional<Git> git_;
    std::unique_ptr<Terminals> terminals_;
};

}  // namespace liteapp::workspace
