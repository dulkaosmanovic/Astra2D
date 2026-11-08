#include <astra2d/assets/file_watcher.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using astra2d::FileWatcher;
namespace fs = std::filesystem;

namespace {

// A fresh directory per test, removed afterwards.
class FileWatcherTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / (std::string("astra2d-watch-") + info->name());
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }
    void TearDown() override { fs::remove_all(m_dir); }

    fs::path write(const std::string& name, const std::string& text)
    {
        const fs::path file = m_dir / name;
        std::ofstream(file) << text;
        return file;
    }

    // Moves the modification time forward explicitly: file system timestamps can be too coarse
    // for two writes within one test to differ.
    static void touchLater(const fs::path& file, int seconds)
    {
        fs::last_write_time(file, fs::last_write_time(file) + std::chrono::seconds(seconds));
    }

    [[nodiscard]] const fs::path& dir() const { return m_dir; }

private:
    fs::path m_dir;
};

} // namespace

TEST_F(FileWatcherTest, NothingChangedNothingReported)
{
    FileWatcher watcher;
    watcher.watch(write("a.frag", "1"));
    EXPECT_TRUE(watcher.poll().empty());
}

TEST_F(FileWatcherTest, ModifiedFileIsReportedOnce)
{
    FileWatcher watcher;
    const fs::path file = write("a.frag", "1");
    watcher.watch(file);
    touchLater(file, 2);
    const auto changed = watcher.poll();
    ASSERT_EQ(changed.size(), 1U);
    EXPECT_EQ(changed[0], file);
    EXPECT_TRUE(watcher.poll().empty());
}

TEST_F(FileWatcherTest, DeletedFileIsReportedWhenItComesBack)
{
    FileWatcher watcher;
    const fs::path file = write("a.frag", "1");
    watcher.watch(file);
    fs::remove(file); // an editor saving by delete + re-create
    EXPECT_TRUE(watcher.poll().empty());
    write("a.frag", "2");
    touchLater(file, 3);
    EXPECT_EQ(watcher.poll().size(), 1U);
}

TEST_F(FileWatcherTest, FileCreatedAfterWatchingIsReported)
{
    FileWatcher watcher;
    const fs::path file = dir() / "later.png";
    watcher.watch(file);
    EXPECT_TRUE(watcher.poll().empty());
    write("later.png", "x");
    EXPECT_EQ(watcher.poll().size(), 1U);
}

TEST_F(FileWatcherTest, FileWatchedTwiceIsReportedOnce)
{
    FileWatcher watcher;
    const fs::path file = write("a.frag", "1");
    watcher.watch(file);
    watcher.watch(file);
    touchLater(file, 2);
    EXPECT_EQ(watcher.poll().size(), 1U);
}
