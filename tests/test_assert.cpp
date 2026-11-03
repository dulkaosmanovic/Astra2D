#include <astra2d/core/assert.hpp>
#include <astra2d/core/log.hpp>

#include <gtest/gtest.h>

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

TEST(LogTests, LoggersAreAvailableWithoutInit)
{
    EXPECT_EQ(astra2d::log::engine().name(), "ASTRA");
    EXPECT_EQ(astra2d::log::game().name(), "GAME");
    ASTRA_CORE_TRACE("engine logger works: {}", 1);
    ASTRA_TRACE("game logger works: {}", 2);
}
