#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "liteapp/core/base64.h"
#include "liteapp/service/event_hub.h"
#include "liteapp/workspace/git.h"
#include "liteapp/workspace/services.h"
#include "liteapp/workspace/workspace.h"
#include "support.h"

#if defined(__unix__) || defined(__APPLE__)

namespace {
using Json = nlohmann::json;
using liteapp::workspace::EntryKind;
using liteapp::workspace::Workspace;

using liteapp::testing::TemporaryDirectory;

void write_file(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

// Collects the protocol messages of an in-process service session.
class Client {
public:
    explicit Client(const liteapp::ServiceRouter& router)
        : session_(router.open({.send_text = [this](std::string_view message) {
              const std::lock_guard lock(mutex_);
              messages_.push_back(Json::parse(message));
              changed_.notify_all();
              return true;
          }})) {}

    void send(const std::string& id, const std::string& method, Json payload = Json::object()) {
        ASSERT_TRUE(session_->receive_text(
            Json{{"version", 1}, {"kind", "request"}, {"id", id}, {"method", method}, {"payload", payload}}.dump()));
    }

    void cancel(const std::string& id) {
        ASSERT_TRUE(session_->receive_text(Json{{"version", 1}, {"kind", "cancel"}, {"id", id}}.dump()));
    }

    // Waits for the first message of a request matching `type` (any terminal type when empty).
    Json wait(
        const std::string& id,
        const std::string& type = {},
        std::chrono::seconds timeout = std::chrono::seconds{10}) {
        std::unique_lock lock(mutex_);
        Json found;
        changed_.wait_for(lock, timeout, [&] {
            for (const auto& message : messages_) {
                if (message.value("id", "") != id) continue;
                const auto kind = message.value("type", "");
                const bool terminal = kind == "completed" || kind == "error" || kind == "cancelled";
                if (type.empty() ? terminal : kind == type) {
                    found = message;
                    return true;
                }
            }
            return false;
        });
        return found;
    }

    // Concatenated "output" event data of a terminal request, decoded from base64.
    std::string output(const std::string& id) {
        const std::lock_guard lock(mutex_);
        std::string text;
        for (const auto& message : messages_)
            if (message.value("id", "") == id && message.value("type", "") == "output")
                text += liteapp::base64_decode(message["payload"]["data"].get<std::string>()).value_or("");
        return text;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<Json> messages_;
    std::unique_ptr<liteapp::ServiceSession> session_;
};

}  // namespace

TEST(FrameworkWorkspace, ConfinesPathsToTheRoot) {
    TemporaryDirectory directory;
    const auto& root = directory.path;
    write_file(root / "src/main.cc", "int main() {}\n");
    write_file(root / ".git/config", "[core]\n");
    std::filesystem::create_directory_symlink("/etc", root / "outside");
    auto workspace = Workspace::open(root);
    ASSERT_TRUE(workspace) << workspace.error().message;

    for (const auto* path : {"../secret", "/etc/passwd", ".git/config", ".GIT/config", ".liteapp-123", "src/../x"}) {
        auto read = workspace->read(path);
        ASSERT_FALSE(read) << path;
        EXPECT_EQ(read.error().code, "invalid") << path;
    }
    EXPECT_FALSE(workspace->read("outside/passwd")) << "a symlinked directory escaped the root";
    EXPECT_FALSE(workspace->write("outside/x", "x"));

    auto entries = workspace->list("");
    ASSERT_TRUE(entries);
    ASSERT_EQ(entries->size(), 2) << ".git must be hidden";
    EXPECT_EQ((*entries)[0].name, "src");
    EXPECT_EQ((*entries)[0].kind, EntryKind::directory);
    EXPECT_EQ((*entries)[1].name, "outside");
    EXPECT_EQ((*entries)[1].kind, EntryKind::other) << "symlinks are listed but not followed";
    EXPECT_EQ(*workspace->normalize("./src//main.cc", false), "src/main.cc");
    EXPECT_EQ(*workspace->normalize(".", true), "");
}

TEST(FrameworkWorkspace, WritesAtomicallyAndDetectsConflicts) {
    TemporaryDirectory directory;
    liteapp::workspace::Options options;
    options.limits.max_file_bytes = 64;
    auto workspace = Workspace::open(directory.path, options);
    ASSERT_TRUE(workspace);

    auto created = workspace->write("notes.txt", "first", 0);
    ASSERT_TRUE(created) << created.error().message;
    auto again = workspace->write("notes.txt", "second", 0);
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, "conflict") << "modified_ns 0 must only create";
    auto read = workspace->read("notes.txt");
    ASSERT_TRUE(read);
    EXPECT_EQ(read->content, "first");
    EXPECT_EQ(read->modified_ns, *created);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_TRUE(workspace->write("notes.txt", "elsewhere"));
    auto stale = workspace->write("notes.txt", "mine", read->modified_ns);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code, "conflict");
    EXPECT_EQ(workspace->read("notes.txt")->content, "elsewhere");

    write_file(directory.path / "image.bin", std::string("\x89PNG\0\x01", 6));
    EXPECT_EQ(workspace->read("image.bin").error().code, "binary");
    EXPECT_EQ(workspace->write("big.txt", std::string(65, 'x')).error().code, "too_large");
    EXPECT_EQ(workspace->write("bad.txt", "\xC3").error().code, "invalid");
    ASSERT_TRUE(workspace->make_directory("docs"));
    ASSERT_TRUE(workspace->write("docs/a.md", "# A"));
    for (const auto& entry : std::filesystem::directory_iterator(directory.path))
        EXPECT_FALSE(entry.path().filename().string().starts_with(".liteapp-")) << "temporary file left behind";
}

TEST(FrameworkWorkspace, ReadsPagesOnCharacterBoundaries) {
    TemporaryDirectory directory;
    write_file(directory.path / "text.txt", "ab\xC3\xA9" "cd");
    auto workspace = Workspace::open(directory.path);
    ASSERT_TRUE(workspace);
    auto first = workspace->read_page("text.txt", 0, 3);
    ASSERT_TRUE(first) << first.error().message;
    EXPECT_EQ(first->content, "ab") << "a page must not split a UTF-8 character";
    ASSERT_TRUE(first->next_offset);
    EXPECT_EQ(*first->next_offset, 2U);
    EXPECT_EQ(first->total_bytes, 6U);
    auto rest = workspace->read_page("text.txt", *first->next_offset, 16);
    ASSERT_TRUE(rest);
    EXPECT_EQ(rest->content, "\xC3\xA9" "cd");
    EXPECT_FALSE(rest->next_offset);
    EXPECT_FALSE(workspace->read_page("text.txt", 7, 4));
}

TEST(FrameworkWorkspace, BoundsCommands) {
    TemporaryDirectory directory;
    liteapp::workspace::Options options;
    options.limits.max_command_output = 8;
    options.limits.command_timeout = std::chrono::milliseconds{300};
    auto workspace = Workspace::open(directory.path, options);
    ASSERT_TRUE(workspace);
    auto echoed = workspace->execute({"/bin/sh", "-c", "pwd; exit 3"});
    ASSERT_TRUE(echoed);
    EXPECT_EQ(echoed->exit_code, 3);
    EXPECT_TRUE(echoed->truncated) << "output beyond max_command_output must be dropped";
    EXPECT_EQ(echoed->output.size(), 8U);
    auto slow = workspace->execute({"/bin/sh", "-c", "sleep 5"});
    ASSERT_TRUE(slow);
    EXPECT_TRUE(slow->timed_out);
    std::stop_source stop;
    std::jthread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        stop.request_stop();
    });
    auto stopped = workspace->execute({"/bin/sh", "-c", "sleep 5"}, stop.get_token());
    ASSERT_FALSE(stopped);
    EXPECT_EQ(stopped.error().code, "cancelled");
}

TEST(FrameworkWorkspace, ReportsGitChanges) {
    if (std::system("git --version > /dev/null 2>&1") != 0) GTEST_SKIP() << "git is not installed";
    TemporaryDirectory directory;
    const auto root = directory.path;
    write_file(root / "sub/kept.txt", "old\n");
    write_file(root / "sub/removed.txt", "gone\n");
    const auto git = "git -C '" + root.string() + "' -c user.name=t -c user.email=t@example.com ";
    ASSERT_EQ(std::system((git + "init -q && " + git + "add -A && " + git + "commit -qm init").c_str()), 0);
    write_file(root / "sub/kept.txt", "new\n");
    std::filesystem::remove(root / "sub/removed.txt");
    write_file(root / "sub/fresh.txt", "fresh\n");
    auto workspace = Workspace::open(root / "sub");
    ASSERT_TRUE(workspace);
    auto repository = liteapp::workspace::Git::find(*workspace);
    ASSERT_TRUE(repository);
    std::map<std::string, std::string> changes;
    for (const auto& change : repository->status()) changes[change.path] = change.status;
    EXPECT_EQ(changes, (std::map<std::string, std::string>{
                           {"fresh.txt", "untracked"}, {"kept.txt", "modified"}, {"removed.txt", "deleted"}}))
        << "paths are relative to the workspace, not the repository";
    EXPECT_EQ(repository->head_text("kept.txt"), "old\n");
    EXPECT_FALSE(repository->head_text("fresh.txt"));
}

TEST(FrameworkWorkspace, ServesFilesGitAndTerminals) {
    TemporaryDirectory directory;
    auto workspace = Workspace::open(directory.path);
    ASSERT_TRUE(workspace);
    std::vector<std::string> written;
    liteapp::workspace::WorkspaceServices services(
        *workspace, {.files_written = [&](const std::vector<std::string>& paths) { written = paths; }});
    liteapp::ServiceRouter router;
    services.register_methods(router);
    Client client(router);

    client.send("write", "fs.write", {{"path", "./a.txt"}, {"content", "hello"}, {"modified_ns", 0}});
    const auto saved = client.wait("write");
    ASSERT_EQ(saved.value("type", ""), "completed") << saved.dump();
    EXPECT_EQ(written, std::vector<std::string>{"a.txt"});
    client.send("read", "fs.read", {{"path", "a.txt"}});
    const auto read = client.wait("read");
    EXPECT_EQ(read["payload"]["content"], "hello");
    EXPECT_EQ(read["payload"]["modified_ns"], saved["payload"]["modified_ns"]);
    ASSERT_TRUE(read["payload"]["modified_ns"].is_string()) << "nanosecond times must survive JavaScript numbers";
    client.send("update", "fs.write",
                {{"path", "a.txt"}, {"content", "updated"}, {"modified_ns", read["payload"]["modified_ns"]}});
    EXPECT_EQ(client.wait("update").value("type", ""), "completed");
    client.send("conflict", "fs.write", {{"path", "a.txt"}, {"content", "x"}, {"modified_ns", 1}});
    EXPECT_EQ(client.wait("conflict")["payload"]["code"], "conflict");
    client.send("list", "fs.list", {{"path", ""}});
    EXPECT_EQ(client.wait("list")["payload"]["entries"][0]["name"], "a.txt");
    client.send("status", "git.status");
    EXPECT_EQ(client.wait("status")["payload"]["repository"], false);

    client.send("shell", "terminal.open", {{"cols", 80}, {"rows", 24}});
    const auto opened = client.wait("shell", "opened");
    ASSERT_TRUE(opened.contains("payload")) << "terminal did not open";
    client.send("input", "terminal.input", {{"id", opened["payload"]["id"]}, {"data", "echo liteapp-$((40+2))\n"}});
    for (int attempt = 0; attempt < 200 && client.output("shell").find("liteapp-42") == std::string::npos; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    EXPECT_NE(client.output("shell").find("liteapp-42"), std::string::npos) << client.output("shell");
    client.send("exit", "terminal.input", {{"id", opened["payload"]["id"]}, {"data", "exit 7\n"}});
    const auto exited = client.wait("shell");
    EXPECT_EQ(exited.value("type", ""), "completed");
    EXPECT_EQ(exited["payload"]["exit_code"], 7);
}

TEST(FrameworkEventHub, ReplaysTheOpenRecordingToLateSubscribers) {
    liteapp::EventHub hub;
    liteapp::ServiceRouter router;
    router.register_async_method(
        "events.subscribe",
        [&](liteapp::ServiceContext&, liteapp::ServiceResponder responder) { hub.subscribe(std::move(responder)); });
    hub.begin_recording();
    hub.publish("delta", {{"text", "a"}});
    hub.publish("files_changed", {{"paths", {"x"}}}, false);
    Client late(router);
    late.send("events", "events.subscribe");
    const auto replayed = late.wait("events", "delta");
    EXPECT_EQ(replayed["payload"]["text"], "a");
    hub.publish("delta", {{"text", "b"}});
    hub.end_recording();
    Client after(router);
    after.send("events", "events.subscribe");
    // Subscribing runs on a router thread; publish until the new subscriber is registered.
    for (int attempt = 0; attempt < 200 && after.wait("events", "done", std::chrono::seconds{0}).is_null(); ++attempt) {
        hub.publish("done", Json::object());
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    EXPECT_FALSE(after.wait("events", "done", std::chrono::seconds{0}).is_null());
    EXPECT_TRUE(after.wait("events", "delta", std::chrono::seconds{0}).is_null())
        << "a closed recording replays nothing";
    EXPECT_TRUE(late.wait("events", "files_changed", std::chrono::seconds{0}).is_null())
        << "unrecorded events are not replayed";
    hub.close();
}

#endif
