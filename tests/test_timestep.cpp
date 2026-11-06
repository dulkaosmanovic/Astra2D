#include <astra2d/core/timestep.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>

using astra2d::FixedTimestep;

// Step sizes and frame times are powers of two, so the arithmetic is exact.

TEST(TimestepTests, FrameShorterThanStepRunsNoSteps)
{
    FixedTimestep ts(0.5, 1.0);
    EXPECT_EQ(ts.advance(0.25), 0);
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.25);
    EXPECT_DOUBLE_EQ(ts.alpha(), 0.5);
}

TEST(TimestepTests, TimeCarriesOverBetweenFrames)
{
    FixedTimestep ts(0.5, 1.0);
    EXPECT_EQ(ts.advance(0.25), 0);
    EXPECT_EQ(ts.advance(0.25), 1);
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0);
}

TEST(TimestepTests, LongFrameRunsMultipleSteps)
{
    FixedTimestep ts(0.125, 1.0);
    EXPECT_EQ(ts.advance(0.5), 4);
    EXPECT_EQ(ts.advance(0.1875), 1); // 0.125 + 0.0625 left over
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0625);
    EXPECT_DOUBLE_EQ(ts.alpha(), 0.5);
}

TEST(TimestepTests, FrameTimeIsClampedToAvoidSpiralOfDeath)
{
    FixedTimestep ts(0.125, 0.25);
    EXPECT_EQ(ts.advance(10.0), 2); // only 0.25 s is accepted
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0);
}

TEST(TimestepTests, NegativeFrameTimeIsIgnored)
{
    FixedTimestep ts(0.5, 1.0);
    EXPECT_EQ(ts.advance(-1.0), 0);
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0);
}

TEST(TimestepTests, ResetClearsAccumulator)
{
    FixedTimestep ts(0.5, 1.0);
    ts.advance(0.25);
    ts.reset();
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0);
    EXPECT_EQ(ts.advance(0.25), 0);
}

TEST(TimestepTests, TotalStepsMatchSimulatedTimeAtUnevenFrameRate)
{
    // 60 Hz simulation driven by a jittery ~144 Hz display for 10 seconds.
    FixedTimestep ts(1.0 / 60.0);
    const std::array<double, 3> frames = {1.0 / 144.0, 1.0 / 120.0, 1.0 / 165.0};
    double elapsed = 0.0;
    long steps = 0;
    for (int i = 0; i < 1440; ++i) {
        const double frame = frames.at(static_cast<std::size_t>(i) % frames.size());
        elapsed += frame;
        steps += ts.advance(frame);
    }
    const double simulated = static_cast<double>(steps) * ts.step();
    EXPECT_NEAR(simulated + ts.accumulator(), elapsed, 1e-9);
    EXPECT_LT(ts.accumulator(), ts.step());
    EXPECT_GE(ts.accumulator(), 0.0);
}

TEST(TimestepTests, DefaultIsSixtyHertz)
{
    const FixedTimestep ts;
    EXPECT_DOUBLE_EQ(ts.step(), 1.0 / 60.0);
}

TEST(TimestepTests, FrameEqualToMaxFrameTimeIsNotClampedAway)
{
    FixedTimestep ts(0.125, 0.25);
    EXPECT_EQ(ts.advance(0.25), 2);
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0);
}

TEST(TimestepTests, NonFiniteFrameTimeIsIgnored)
{
    // A single NaN used to poison the accumulator and stop the simulation for good.
    FixedTimestep ts(0.5, 1.0);
    EXPECT_EQ(ts.advance(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ(ts.advance(std::numeric_limits<double>::infinity()), 0);
    EXPECT_DOUBLE_EQ(ts.accumulator(), 0.0);
    EXPECT_EQ(ts.advance(1.0), 2);
}

TEST(TimestepTests, InvalidStepThrowsInEveryBuild)
{
    // A step of zero used to make advance() loop forever in release builds.
    EXPECT_THROW(FixedTimestep{0.0}, std::invalid_argument);
    EXPECT_THROW(FixedTimestep{-1.0}, std::invalid_argument);
    EXPECT_THROW(FixedTimestep{std::numeric_limits<double>::quiet_NaN()}, std::invalid_argument);
}

TEST(TimestepTests, MaxFrameTimeShorterThanStepThrows)
{
    EXPECT_THROW((FixedTimestep{0.5, 0.25}), std::invalid_argument);
    EXPECT_NO_THROW((FixedTimestep{0.5, 0.5}));
}
