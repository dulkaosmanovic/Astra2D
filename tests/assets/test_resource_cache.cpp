#include <astra2d/assets/resource_cache.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

using astra2d::ResourceCache;

TEST(ResourceCacheTests, LoadsOnceAndReturnsTheSameObject)
{
    ResourceCache<std::string> cache;
    int loads = 0;
    const auto loader = [&] {
        ++loads;
        return std::string("pixels");
    };
    const std::string& first = cache.get("player.png", loader);
    const std::string& second = cache.get("player.png", loader);
    EXPECT_EQ(loads, 1);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(cache.size(), 1U);
}

TEST(ResourceCacheTests, ReferencesStayValidWhenMoreResourcesAreAdded)
{
    ResourceCache<int> cache;
    int& first = cache.get("0", [] { return 0; });
    for (int i = 1; i < 1000; ++i) { // forces rehashing
        (void)cache.get(std::to_string(i), [i] { return i; });
    }
    first = 42;
    EXPECT_EQ(cache.get("0", [] { return -1; }), 42);
}

TEST(ResourceCacheTests, FailedLoadStoresNothing)
{
    ResourceCache<int> cache;
    EXPECT_THROW((void)cache.get("missing", []() -> int { throw std::runtime_error("no file"); }),
                 std::runtime_error);
    EXPECT_FALSE(cache.contains("missing"));
    EXPECT_EQ(cache.get("missing", [] { return 1; }), 1); // retried
}

TEST(ResourceCacheTests, SupportsMoveOnlyTypes)
{
    ResourceCache<std::unique_ptr<int>> cache;
    const auto& value = cache.get("a", [] { return std::make_unique<int>(5); });
    EXPECT_EQ(*value, 5);
}

TEST(ResourceCacheTests, EraseAndClear)
{
    ResourceCache<int> cache;
    (void)cache.get("a", [] { return 1; });
    (void)cache.get("b", [] { return 2; });
    cache.erase("a");
    EXPECT_FALSE(cache.contains("a"));
    EXPECT_TRUE(cache.contains("b"));
    cache.erase("not there"); // no-op
    cache.clear();
    EXPECT_EQ(cache.size(), 0U);
}
