#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <gtest/gtest.h>

#include "liteapp/fs/watcher.h"

TEST(Filesystem, ReportsNestedFileEvents) {
    const auto root = std::filesystem::temp_directory_path() / "liteapp-watcher-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");
    std::mutex mutex;
    std::condition_variable event;
    bool observed = false;
    {
        const std::array roots{root};
        auto watcher = liteapp::fs::Watcher::create(roots, [&](const auto& path, auto) {
            if (path.filename() == "session.jsonl") {
                std::scoped_lock lock{mutex};
                observed = true;
                event.notify_all();
            }
        });
        ASSERT_TRUE(watcher);
        std::ofstream{root / "nested" / "session.jsonl"} << "event";
        std::unique_lock lock{mutex};
        EXPECT_TRUE(event.wait_for(lock, std::chrono::seconds{5}, [&] { return observed; }));
    }
    std::filesystem::remove_all(root);
}

