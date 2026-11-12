// The engine's ImGui theme: sizes and text follow the monitor's content scale.

#include "debug/imgui_style.hpp"
#include "headless_imgui.hpp"

#include <gtest/gtest.h>
#include <imgui.h>

#include <cmath>

TEST(ImGuiStyleTests, ScalesSizesAndTextByTheContentScale)
{
    const HeadlessImGui imgui;
    astra2d::detail::applyStyle(1.0F);
    const float padding = ImGui::GetStyle().FramePadding.x;
    astra2d::detail::applyStyle(1.5F);
    // ImGui rounds scaled sizes down to whole pixels.
    EXPECT_FLOAT_EQ(ImGui::GetStyle().FramePadding.x, std::floor(1.5F * padding));
    EXPECT_FLOAT_EQ(ImGui::GetStyle().FontScaleDpi, 1.5F);
}

TEST(ImGuiStyleTests, ClampsAnUnusableScale)
{
    const HeadlessImGui imgui;
    astra2d::detail::applyStyle(0.0F); // a window reporting no scale
    EXPECT_FLOAT_EQ(ImGui::GetStyle().FontScaleDpi, 0.5F);
}
