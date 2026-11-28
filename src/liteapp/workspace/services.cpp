#include "liteapp/workspace/services.h"

#include "liteapp/core/base64.h"

#include <charconv>
#include <cstdint>
#include <string_view>
#include <utility>

namespace liteapp::workspace {
namespace {

using Json = nlohmann::json;

std::string text(const Json& payload, const char* key) {
    return payload.contains(key) && payload[key].is_string() ? payload[key].get<std::string>() : std::string{};
}

// Modification times are nanoseconds since 1970, beyond JavaScript's exact integers, so they travel as decimal
// strings. Writes also accept small integers (0 means "must not exist").
std::string timestamp(std::int64_t value) {
    return std::to_string(value);
}

Result<std::optional<std::int64_t>> expected_timestamp(const Json& payload) {
    if (!payload.contains("modified_ns") || payload["modified_ns"].is_null()) return std::nullopt;
    const auto& value = payload["modified_ns"];
    if (value.is_number_integer()) return value.get<std::int64_t>();
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        std::int64_t parsed = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (error == std::errc{} && end == text.data() + text.size()) return parsed;
    }
    return std::unexpected(Error{"invalid", "modified_ns must be a decimal string or an integer"});
}

TerminalSize size(const Json& payload) {
    return {.columns = payload.value("cols", 80), .rows = payload.value("rows", 24)};
}

const char* kind_name(EntryKind kind) {
    switch (kind) {
        case EntryKind::file:
            return "file";
        case EntryKind::directory:
            return "directory";
        case EntryKind::other:
            break;
    }
    return "other";
}

}  // namespace

WorkspaceServices::WorkspaceServices(Workspace workspace, ServiceOptions options)
    : workspace_(std::move(workspace)), options_(std::move(options)), git_(Git::find(workspace_)) {
    if (options_.terminals) terminals_ = std::make_unique<Terminals>(workspace_.root());
}

WorkspaceServices::~WorkspaceServices() { close(); }

const Workspace& WorkspaceServices::workspace() const { return workspace_; }

const std::optional<Git>& WorkspaceServices::git() const { return git_; }

void WorkspaceServices::close() {
    if (terminals_) terminals_->close_all();
}

void WorkspaceServices::register_methods(ServiceRouter& router) {
    router.register_method("fs.list", [this](ServiceContext& context) -> Result<Json> {
        auto entries = workspace_.list(text(context.payload(), "path"));
        if (!entries) return std::unexpected(entries.error());
        auto result = Json::array();
        for (const auto& entry : *entries)
            result.push_back({{"name", entry.name}, {"kind", kind_name(entry.kind)}, {"size", entry.size}});
        return Json{{"entries", std::move(result)}};
    });
    router.register_method("fs.read", [this](ServiceContext& context) -> Result<Json> {
        auto path = workspace_.normalize(text(context.payload(), "path"), false);
        if (!path) return std::unexpected(path.error());
        auto file = workspace_.read(*path);
        if (!file) return std::unexpected(file.error());
        return Json{
            {"path", *path}, {"content", std::move(file->content)}, {"modified_ns", timestamp(file->modified_ns)}};
    });
    router.register_method("fs.write", [this](ServiceContext& context) -> Result<Json> {
        const auto& payload = context.payload();
        auto path = workspace_.normalize(text(payload, "path"), false);
        if (!path) return std::unexpected(path.error());
        if (!payload.contains("content") || !payload["content"].is_string())
            return std::unexpected(Error{"invalid", "content must be text"});
        auto expected = expected_timestamp(payload);
        if (!expected) return std::unexpected(expected.error());
        auto written = workspace_.write(*path, payload["content"].get<std::string>(), *expected);
        if (!written) return std::unexpected(written.error());
        if (options_.files_written) options_.files_written({*path});
        return Json{{"path", *path}, {"modified_ns", timestamp(*written)}};
    });
    router.register_method("fs.mkdir", [this](ServiceContext& context) -> Result<Json> {
        auto path = workspace_.normalize(text(context.payload(), "path"), false);
        if (!path) return std::unexpected(path.error());
        if (auto made = workspace_.make_directory(*path); !made) return std::unexpected(made.error());
        if (options_.files_written) options_.files_written({*path});
        return Json{{"path", *path}};
    });
    router.register_method("git.status", [this](ServiceContext&) -> Result<Json> {
        auto changes = Json::array();
        if (git_)
            for (auto& change : git_->status())
                changes.push_back({{"path", std::move(change.path)}, {"status", std::move(change.status)}});
        return Json{{"repository", git_.has_value()}, {"changes", std::move(changes)}};
    });
    router.register_method("git.diff", [this](ServiceContext& context) -> Result<Json> {
        if (!git_) return std::unexpected(Error{"unsupported", "the workspace is not a Git repository"});
        auto path = workspace_.normalize(text(context.payload(), "path"), false);
        if (!path) return std::unexpected(path.error());
        auto old = git_->head_text(*path);
        auto current = workspace_.read(*path);
        if (!current && current.error().code != "io") return std::unexpected(current.error());
        return Json{{"path", *path},
                    {"old", old.value_or("")},
                    {"old_exists", old.has_value()},
                    {"new", current ? current->content : std::string{}},
                    {"new_exists", current.has_value()},
                    {"modified_ns", timestamp(current ? current->modified_ns : 0)}};
    });
    if (!terminals_) return;
    router.register_async_method("terminal.open", [this](ServiceContext& context, ServiceResponder responder) {
        auto shared = std::make_shared<ServiceResponder>(std::move(responder));
        auto opened = terminals_->open(
            size(context.payload()),
            {.output = [shared](std::string_view data) {
                 return shared->emit("output", {{"data", base64_encode(data)}});
             },
             .active = [shared] { return !shared->cancelled(); },
             .exited = [shared](int code) { static_cast<void>(shared->complete({{"exit_code", code}})); }});
        if (!opened) {
            static_cast<void>(shared->fail(opened.error()));
            return;
        }
        static_cast<void>(shared->emit("opened", {{"id", *opened}}));
    });
    router.register_method("terminal.input", [this](ServiceContext& context) -> Result<Json> {
        const auto& payload = context.payload();
        if (auto written = terminals_->write(payload.value("id", std::int64_t{-1}), text(payload, "data")); !written)
            return std::unexpected(written.error());
        return Json::object();
    });
    router.register_method("terminal.resize", [this](ServiceContext& context) -> Result<Json> {
        const auto& payload = context.payload();
        if (auto resized = terminals_->resize(payload.value("id", std::int64_t{-1}), size(payload)); !resized)
            return std::unexpected(resized.error());
        return Json::object();
    });
}

}  // namespace liteapp::workspace
