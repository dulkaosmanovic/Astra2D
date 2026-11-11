// The overlay's frame time graph: the budget colours, and drawing it in a headless ImGui context
// while the history fills up and once it is full.

#include "debug/frame_graph.hpp"
#include "debug/imgui_style.hpp"
#include "headless_imgui.hpp"

#include <astra2d/debug/frame_history.hpp>

#include <gtest/gtest.h>

using astra2d::FrameHistory;
using astra2d::detail::budgetColor;
using astra2d::detail::OverlayColors;

TEST(FrameGraphTests, ColourFollowsTheBudget)
{
    constexpr float kBudget = 16.0F;
    EXPECT_EQ(budgetColor(10.0F, kBudget), OverlayColors::kGood);
    EXPECT_EQ(budgetColor(16.7F, kBudget), OverlayColors::kGood); // timer noise is tolerated
    EXPECT_EQ(budgetColor(20.0F, kBudget), OverlayColors::kWarn);
    EXPECT_EQ(budgetColor(32.0F, kBudget), OverlayColors::kWarn);
    EXPECT_EQ(budgetColor(40.0F, kBudget), OverlayColors::kBad);
}

TEST(FrameGraphTests, DrawsEmptyFillingAndFullHistories)
{
    HeadlessImGui imgui;
    FrameHistory history(8);
    imgui.frame([&] { astra2d::detail::drawFrameGraph(history, 16.0F); });
    for (int i = 0; i < 20; ++i) { // past the capacity: the oldest frames drop out
        history.push(i % 5 == 0 ? 45.0F : 15.0F);
        imgui.frame([&] { astra2d::detail::drawFrameGraph(history, 16.0F); });
    }
    EXPECT_EQ(history.size(), 8U);
}
