#include <astra2d/core/assert.hpp>
#include <astra2d/core/log.hpp>

#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_color_sinks.h>

TEST(AssertTests, PassingAssertDoesNothing)
{
    ASTRA_ASSERT(1 + 1 == 2);
    ASTRA_ASSERT(true, "with a message {}", 42);
    SUCCEED();
}

TEST(AssertDeathTest, FailingAssertAbortsWithMessage)
{
#if ASTRA_ASSERTS_ENABLED
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const int value = 3;
    EXPECT_DEATH(ASTRA_ASSERT(value == 4, "value was {}", value), "value == 4.*value was 3");
#else
    GTEST_SKIP() << "assertions are disabled in this build";
#endif
}

TEST(AssertTests, DisabledAssertDoesNotEvaluateCondition)
{
#if ASTRA_ASSERTS_ENABLED
    GTEST_SKIP() << "assertions are enabled in this build";
#else
    int calls = 0;
    ASTRA_ASSERT(++calls == 0, "never formatted {}", calls);
    EXPECT_EQ(calls, 0);
#endif
}

TEST(LogTests, LoggersAreAvailableWithoutInit)
{
    EXPECT_EQ(astra2d::log::engine().name(), "ASTRA");
    EXPECT_EQ(astra2d::log::game().name(), "GAME");
    ASTRA_CORE_TRACE("engine logger works: {}", 1);
    ASTRA_TRACE("game logger works: {}", 2);
}

TEST(LogTests, LoggersAreNotInTheGlobalRegistry)
{
    // User code may create its own "GAME" logger or call drop_all() without affecting the engine.
    (void)astra2d::log::game();
    EXPECT_EQ(spdlog::get("GAME"), nullptr);
    const auto own = spdlog::stderr_color_mt("GAME");
    spdlog::drop_all();
    EXPECT_EQ(astra2d::log::game().name(), "GAME");
    ASTRA_TRACE("still works after drop_all");
}
