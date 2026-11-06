#include <astra2d/core/paths.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

TEST(PathsTests, ExecutableDirectoryContainsTheTestBinary)
{
    const std::filesystem::path directory = astra2d::executableDirectory();
    EXPECT_TRUE(directory.is_absolute());
    EXPECT_TRUE(std::filesystem::exists(directory / "engine_tests"));
}

TEST(PathsTests, FindDirectoryReturnsTheFirstExistingOne)
{
    const std::filesystem::path existing = std::filesystem::temp_directory_path();
    const std::filesystem::path missing = existing / "astra2d-no-such-directory";
    EXPECT_EQ(astra2d::findDirectory({missing, existing}), existing);
}

TEST(PathsTests, FindDirectorySkipsFiles)
{
    const std::filesystem::path file = astra2d::executableDirectory() / "engine_tests";
    const std::filesystem::path directory = astra2d::executableDirectory();
    EXPECT_EQ(astra2d::findDirectory({file, directory}), directory);
}

TEST(PathsTests, FindDirectoryThrowsWhenNoneExists)
{
    EXPECT_THROW((void)astra2d::findDirectory({"/astra2d/missing/a", "/astra2d/missing/b"}),
                 std::runtime_error);
}
