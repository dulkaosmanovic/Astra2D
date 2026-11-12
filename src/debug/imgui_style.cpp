#include "imgui_style.hpp"

#include <imgui.h>

#include <algorithm>

namespace astra2d::detail {

namespace {

constexpr ImVec4 rgba(int r, int g, int b, float a = 1.0F)
{
    return {static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F,
            static_cast<float>(b) / 255.0F, a};
}

void applyColors(ImGuiStyle& style)
{
    const ImVec4 accent = rgba(42, 127, 174);
    const ImVec4 accentLight = rgba(70, 160, 210);
    const ImVec4 panel = rgba(36, 46, 66);
    auto& c = style.Colors;
    c[ImGuiCol_Text] = rgba(222, 229, 240);
    c[ImGuiCol_TextDisabled] = rgba(126, 138, 158);
    c[ImGuiCol_WindowBg] = rgba(14, 18, 28, 0.97F);
    c[ImGuiCol_ChildBg] = rgba(10, 13, 21, 0.6F);
    c[ImGuiCol_PopupBg] = rgba(18, 23, 34, 0.97F);
    c[ImGuiCol_Border] = rgba(60, 74, 98, 0.6F);
    c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0.0F);
    c[ImGuiCol_FrameBg] = panel;
    c[ImGuiCol_FrameBgHovered] = rgba(46, 60, 86);
    c[ImGuiCol_FrameBgActive] = rgba(54, 72, 104);
    c[ImGuiCol_TitleBg] = rgba(18, 24, 36);
    c[ImGuiCol_TitleBgActive] = rgba(24, 44, 66);
    c[ImGuiCol_TitleBgCollapsed] = rgba(18, 24, 36, 0.8F);
    c[ImGuiCol_MenuBarBg] = rgba(20, 26, 38);
    c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0.0F);
    c[ImGuiCol_ScrollbarGrab] = rgba(60, 74, 98);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(80, 98, 128);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accentLight;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentLight;
    c[ImGuiCol_Button] = rgba(38, 50, 72);
    c[ImGuiCol_ButtonHovered] = rgba(52, 92, 130);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = rgba(42, 127, 174, 0.30F);
    c[ImGuiCol_HeaderHovered] = rgba(42, 127, 174, 0.50F);
    c[ImGuiCol_HeaderActive] = rgba(42, 127, 174, 0.70F);
    c[ImGuiCol_Separator] = rgba(60, 74, 98, 0.6F);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accentLight;
    c[ImGuiCol_ResizeGrip] = rgba(42, 127, 174, 0.25F);
    c[ImGuiCol_ResizeGripHovered] = rgba(42, 127, 174, 0.6F);
    c[ImGuiCol_ResizeGripActive] = accentLight;
    c[ImGuiCol_Tab] = rgba(26, 34, 50);
    c[ImGuiCol_TabHovered] = rgba(52, 92, 130);
    c[ImGuiCol_TabSelected] = rgba(36, 70, 102);
    c[ImGuiCol_TabSelectedOverline] = accentLight;
    c[ImGuiCol_TabDimmed] = rgba(22, 28, 40);
    c[ImGuiCol_TabDimmedSelected] = rgba(30, 50, 74);
    c[ImGuiCol_PlotLines] = accentLight;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentLight;
    c[ImGuiCol_TableHeaderBg] = rgba(24, 32, 48);
    c[ImGuiCol_TableBorderStrong] = rgba(60, 74, 98, 0.8F);
    c[ImGuiCol_TableBorderLight] = rgba(48, 60, 80, 0.6F);
    c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0.0F);
    c[ImGuiCol_TableRowBgAlt] = rgba(255, 255, 255, 0.03F);
    c[ImGuiCol_TextSelectedBg] = rgba(42, 127, 174, 0.45F);
    c[ImGuiCol_TreeLines] = rgba(60, 74, 98, 0.8F);
}

} // namespace

void applyStyle(float scale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle(); // the default sizes, scaled below
    applyColors(style);
    style.WindowRounding = 8.0F;
    style.ChildRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 6.0F;
    style.GrabRounding = 4.0F;
    style.ScrollbarRounding = 6.0F;
    style.TabRounding = 5.0F;
    style.WindowPadding = ImVec2(10.0F, 10.0F);
    style.FramePadding = ImVec2(7.0F, 4.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
    style.ScrollbarSize = 11.0F;
    style.GrabMinSize = 10.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.WindowTitleAlign = ImVec2(0.02F, 0.5F);
    style.SeparatorTextBorderSize = 2.0F;
    scale = std::clamp(scale, 0.5F, 4.0F);
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    // A scalable font instead of the 13 px bitmap one, so text stays sharp at any scale.
    ImFontAtlas& fonts = *ImGui::GetIO().Fonts;
    fonts.Clear();
    fonts.AddFontDefaultVector();
    style.FontSizeBase = 15.0F;
}

} // namespace astra2d::detail
