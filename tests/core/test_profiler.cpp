// The scoped CPU profiler: the call tree, calls and times per frame, smoothing, and when it
// records. A fake clock makes the times exact.

#include <astra2d/core/assert.hpp>
#include <astra2d/core/profiler.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using astra2d::ProfileNode;
using astra2d::Profiler;
using astra2d::ProfileScope;

namespace {

// Seconds; advanced by the tests. A plain function so it converts to Profiler::Clock.
double g_now = 0.0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the fake clock

double fakeClock()
{
    return g_now;
}

void advanceMs(double ms)
{
    g_now += ms / 1000.0;
}

class ProfilerTests : public ::testing::Test {
protected:
    void SetUp() override
    {
        g_now = 0.0;
        profiler().setEnabled(true);
    }

    // The node called @p name under @p parent, or nullptr.
    [[nodiscard]] const ProfileNode* child(int parent, const std::string& name) const
    {
        for (const int index : profiler().nodes()[static_cast<std::size_t>(parent)].children) {
            if (profiler().nodes()[static_cast<std::size_t>(index)].name == name) {
                return &profiler().nodes()[static_cast<std::size_t>(index)];
            }
        }
        return nullptr;
    }

    [[nodiscard]] Profiler& profiler() { return m_profiler; }
    [[nodiscard]] const Profiler& profiler() const { return m_profiler; }

private:
    Profiler m_profiler{fakeClock, 0.5};
};

} // namespace

TEST_F(ProfilerTests, RecordsNestedScopesAsATree)
{
    profiler().beginFrame();
    {
        const ProfileScope update(profiler(), "update");
        advanceMs(1.0);
        {
            const ProfileScope physics(profiler(), "physics");
            advanceMs(3.0);
        }
    }
    advanceMs(2.0);
    profiler().endFrame();

    const auto& nodes = profiler().nodes();
    ASSERT_EQ(nodes.size(), 3U);
    EXPECT_EQ(nodes[0].name, "frame");
    EXPECT_EQ(nodes[0].depth, 0);
    EXPECT_DOUBLE_EQ(nodes[0].ms, 6.0);
    const ProfileNode* update = child(0, "update");
    ASSERT_NE(update, nullptr);
    EXPECT_EQ(update->depth, 1);
    EXPECT_EQ(update->calls, 1);
    EXPECT_NEAR(update->ms, 4.0, 1e-9); // children included
    ASSERT_EQ(update->children.size(), 1U);
    const ProfileNode& physics = nodes[static_cast<std::size_t>(update->children[0])];
    EXPECT_EQ(physics.name, "physics");
    EXPECT_EQ(physics.depth, 2);
    EXPECT_NEAR(physics.ms, 3.0, 1e-9);
    EXPECT_EQ(profiler().frames(), 1U);
}

TEST_F(ProfilerTests, RepeatedScopeIsOneNodeWithSeveralCalls)
{
    profiler().beginFrame();
    for (int step = 0; step < 3; ++step) {
        const ProfileScope fixed(profiler(), "fixed update");
        advanceMs(2.0);
    }
    profiler().endFrame();
    ASSERT_EQ(profiler().nodes().size(), 2U);
    EXPECT_EQ(profiler().nodes()[1].calls, 3);
    EXPECT_NEAR(profiler().nodes()[1].ms, 6.0, 1e-9);
}

TEST_F(ProfilerTests, SameNameUnderDifferentParentsGivesTwoNodes)
{
    profiler().beginFrame();
    {
        const ProfileScope a(profiler(), "fixed update");
        const ProfileScope inner(profiler(), "physics");
    }
    {
        const ProfileScope b(profiler(), "update");
        const ProfileScope inner(profiler(), "physics");
    }
    profiler().endFrame();
    EXPECT_EQ(profiler().nodes().size(), 5U);
    EXPECT_NE(child(1, "physics"), nullptr);
    EXPECT_NE(child(3, "physics"), nullptr);
}

TEST_F(ProfilerTests, NodesKeepTheirIndexAcrossFrames)
{
    for (int frame = 0; frame < 3; ++frame) {
        profiler().beginFrame();
        {
            const ProfileScope update(profiler(), std::string("up") + "date"); // a temporary name
            advanceMs(1.0);
        }
        profiler().endFrame();
    }
    ASSERT_EQ(profiler().nodes().size(), 2U);
    EXPECT_EQ(profiler().nodes()[1].name, "update");
    EXPECT_EQ(profiler().nodes()[1].calls, 1);
    EXPECT_EQ(profiler().frames(), 3U);
}

TEST_F(ProfilerTests, ScopeThatDidNotRunReportsZero)
{
    profiler().beginFrame();
    {
        const ProfileScope spawn(profiler(), "spawn");
        advanceMs(4.0);
    }
    profiler().endFrame();
    profiler().beginFrame();
    profiler().endFrame();
    EXPECT_EQ(profiler().nodes()[1].calls, 0);
    EXPECT_DOUBLE_EQ(profiler().nodes()[1].ms, 0.0);
    EXPECT_DOUBLE_EQ(profiler().nodes()[1].averageMs, 2.0); // halfway down with smoothing 0.5
}

TEST_F(ProfilerTests, AverageStartsAtTheFirstFrameThenMovesBySmoothing)
{
    const auto frame = [&](double ms) {
        profiler().beginFrame();
        advanceMs(ms);
        profiler().endFrame();
    };
    frame(10.0);
    EXPECT_NEAR(profiler().nodes()[0].averageMs, 10.0, 1e-9);
    frame(20.0);
    EXPECT_NEAR(profiler().nodes()[0].averageMs, 15.0, 1e-9);
    frame(20.0);
    EXPECT_NEAR(profiler().nodes()[0].averageMs, 17.5, 1e-9);
}

TEST_F(ProfilerTests, ReenablingRestartsTheAverages)
{
    profiler().beginFrame();
    advanceMs(10.0);
    profiler().endFrame();
    profiler().setEnabled(false);
    profiler().setEnabled(true);
    EXPECT_EQ(profiler().frames(), 0U);
    profiler().beginFrame();
    advanceMs(30.0);
    profiler().endFrame();
    EXPECT_NEAR(profiler().nodes()[0].averageMs, 30.0, 1e-9); // not 20: the old value is forgotten
}

TEST_F(ProfilerTests, DisabledProfilerRecordsNothing)
{
    profiler().setEnabled(false);
    profiler().beginFrame();
    EXPECT_FALSE(profiler().isRecording());
    {
        const ProfileScope update(profiler(), "update");
    }
    profiler().endFrame();
    EXPECT_TRUE(profiler().nodes().empty());
    EXPECT_EQ(profiler().frames(), 0U);
}

TEST_F(ProfilerTests, EnablingTakesEffectAtTheNextFrame)
{
    profiler().setEnabled(false);
    profiler().beginFrame();
    profiler().setEnabled(true);
    EXPECT_FALSE(profiler().isRecording()); // this frame started disabled
    profiler().endFrame();
    profiler().beginFrame();
    EXPECT_TRUE(profiler().isRecording());
    profiler().endFrame();
    EXPECT_EQ(profiler().frames(), 1U);
}

TEST_F(ProfilerTests, ScopesOutsideAFrameAreNotRecorded)
{
    {
        const ProfileScope stray(profiler(), "headless");
    }
    profiler().beginFrame();
    profiler().endFrame();
    {
        const ProfileScope after(profiler(), "after the frame");
    }
    EXPECT_EQ(profiler().nodes().size(), 1U); // only the root
}

TEST_F(ProfilerTests, BeginFrameDropsAnUnfinishedFrame)
{
    profiler().beginFrame();
    advanceMs(100.0); // e.g. minimized
    profiler().beginFrame();
    advanceMs(5.0);
    profiler().endFrame();
    EXPECT_NEAR(profiler().nodes()[0].ms, 5.0, 1e-9);
    EXPECT_EQ(profiler().frames(), 1U);
}

TEST_F(ProfilerTests, ResetForgetsTheTree)
{
    profiler().beginFrame();
    {
        const ProfileScope update(profiler(), "update");
    }
    profiler().endFrame();
    profiler().reset();
    EXPECT_TRUE(profiler().nodes().empty());
    EXPECT_EQ(profiler().frames(), 0U);
    EXPECT_TRUE(profiler().isEnabled());
}

TEST_F(ProfilerTests, MacroUsesTheGlobalProfiler)
{
    Profiler& global = Profiler::global();
    global.reset();
    global.setEnabled(true);
    global.beginFrame();
    {
        ASTRA_PROFILE_SCOPE("macro scope");
        ASTRA_PROFILE_SCOPE("second on another line");
    }
    global.endFrame();
    global.setEnabled(false);
    ASSERT_EQ(global.nodes().size(), 3U);
    EXPECT_EQ(global.nodes()[1].name, "macro scope");
    EXPECT_EQ(global.nodes()[2].name, "second on another line");
    EXPECT_EQ(global.nodes()[2].parent, 1);
    global.reset();
}

TEST(ProfilerSettingsTests, RejectsSmoothingOutsideZeroToOne)
{
    EXPECT_THROW(Profiler(nullptr, 0.0), std::invalid_argument);
    EXPECT_THROW(Profiler(nullptr, 1.5), std::invalid_argument);
    EXPECT_NO_THROW(Profiler(nullptr, 1.0));
}

TEST(ProfilerDeathTest, EndingAFrameWithAnOpenScopeAsserts)
{
    Profiler profiler;
    profiler.setEnabled(true);
    profiler.beginFrame();
    profiler.beginScope("open");
#if ASTRA_ASSERTS_ENABLED
    EXPECT_DEATH(profiler.endFrame(), "open");
#endif
    profiler.endScope();
    profiler.endFrame();
}
