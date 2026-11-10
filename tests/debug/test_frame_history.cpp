// FrameHistory: the ring buffer behind the overlay's frame time graph.

#include <astra2d/debug/frame_history.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

using astra2d::FrameHistory;

TEST(FrameHistoryTests, KeepsValuesOldestFirstUntilFull)
{
    FrameHistory history(3);
    EXPECT_TRUE(history.empty());
    history.push(1.0F);
    history.push(2.0F);
    ASSERT_EQ(history.size(), 2U);
    EXPECT_FLOAT_EQ(history[0], 1.0F);
    EXPECT_FLOAT_EQ(history[1], 2.0F);
    EXPECT_FLOAT_EQ(history.latest(), 2.0F);
    EXPECT_EQ(history.offset(), 0U);
}

TEST(FrameHistoryTests, ReplacesTheOldestOnceFull)
{
    FrameHistory history(3);
    for (const float value : {1.0F, 2.0F, 3.0F, 4.0F, 5.0F}) {
        history.push(value);
    }
    ASSERT_EQ(history.size(), 3U);
    EXPECT_FLOAT_EQ(history[0], 3.0F);
    EXPECT_FLOAT_EQ(history[1], 4.0F);
    EXPECT_FLOAT_EQ(history[2], 5.0F);
    EXPECT_FLOAT_EQ(history.latest(), 5.0F);
    // The raw storage read from offset() wraps around to the same order.
    EXPECT_FLOAT_EQ(history.data()[history.offset()], 3.0F);
}

TEST(FrameHistoryTests, MaxAndMean)
{
    FrameHistory history(4);
    EXPECT_FLOAT_EQ(history.max(), 0.0F);
    EXPECT_FLOAT_EQ(history.mean(), 0.0F);
    for (const float value : {2.0F, 8.0F, 4.0F, 6.0F}) {
        history.push(value);
    }
    EXPECT_FLOAT_EQ(history.max(), 8.0F);
    EXPECT_FLOAT_EQ(history.mean(), 5.0F);
    history.push(1.0F); // replaces the 2
    EXPECT_FLOAT_EQ(history.mean(), 4.75F);
}

TEST(FrameHistoryTests, ClearStartsOver)
{
    FrameHistory history(2);
    history.push(1.0F);
    history.push(2.0F);
    history.push(3.0F);
    history.clear();
    EXPECT_TRUE(history.empty());
    history.push(7.0F);
    EXPECT_FLOAT_EQ(history[0], 7.0F);
    EXPECT_EQ(history.capacity(), 2U);
}

TEST(FrameHistoryTests, RejectsZeroCapacity)
{
    EXPECT_THROW(FrameHistory(0), std::invalid_argument);
}
