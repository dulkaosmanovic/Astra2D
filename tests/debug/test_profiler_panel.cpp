// The overlay's profiler table, drawn in a headless ImGui context from a recorded call tree.

#include "debug/profiler_panel.hpp"
#include "headless_imgui.hpp"

#include <astra2d/core/profiler.hpp>

#include <gtest/gtest.h>

using astra2d::Profiler;
using astra2d::ProfileScope;

TEST(ProfilerPanelTests, DrawsBeforeAndAfterTheFirstFrame)
{
    Profiler profiler;
    HeadlessImGui imgui;
    imgui.frame([&] { astra2d::detail::drawProfilerTable(profiler); }); // nothing recorded

    profiler.setEnabled(true);
    for (int frame = 0; frame < 3; ++frame) {
        profiler.beginFrame();
        {
            const ProfileScope update(profiler, "update");
            const ProfileScope physics(profiler, "physics");
            const ProfileScope solver(profiler, "solver"); // depth 3: collapsed by default
        }
        {
            const ProfileScope render(profiler, "render");
        }
        profiler.endFrame();
        imgui.frame([&] { astra2d::detail::drawProfilerTable(profiler); });
    }
    EXPECT_EQ(profiler.nodes().size(), 5U);
}
