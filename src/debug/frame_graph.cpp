#include "frame_graph.hpp"

#include "astra2d/debug/frame_history.hpp"
#include "imgui_helpers.hpp"
#include "imgui_style.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>

namespace astra2d::detail {

unsigned budgetColor(float ms, float budgetMs)
{
    if (ms <= 1.05F * budgetMs) {
        return OverlayColors::kGood;
    }
    return ms <= 2.0F * budgetMs ? OverlayColors::kWarn : OverlayColors::kBad;
}

void drawFrameGraph(const FrameHistory& history, float budgetMs)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 3.2F * ImGui::GetFrameHeight();
    const ImVec2 end(origin.x + width, origin.y + height);
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    draw.AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
    ImGui::Dummy(ImVec2(width, height));
    if (history.empty()) {
        return;
    }

    // The 60 FPS budget sits at half height until a spike needs more room.
    const float top = std::max(1.15F * history.max(), 2.0F * budgetMs);
    const float pad = 3.0F;
    const float plotHeight = height - (2.0F * pad);
    const auto yOf = [&](float ms) { return end.y - pad - (ms / top * plotHeight); };
    // One slot per frame of the capacity, so the bars keep their width while the history fills.
    const float slot = (width - (2.0F * pad)) / static_cast<float>(history.capacity());
    const float start = end.x - pad - (slot * static_cast<float>(history.size()));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsItemHovered();
    for (std::size_t i = 0; i < history.size(); ++i) {
        const float ms = history[i];
        const float x = start + (slot * static_cast<float>(i));
        const ImVec2 min(x, yOf(ms));
        const ImVec2 max(x + std::max(slot - 1.0F, 1.0F), end.y - pad);
        const bool here = hovered && mouse.x >= x && mouse.x < x + slot;
        draw.AddRectFilled(min, max, here ? IM_COL32_WHITE : budgetColor(ms, budgetMs));
        if (here) {
            ImGui::BeginTooltip();
            text("{:.2f} ms ({:.0f} FPS)", ms, ms > 0.0F ? 1000.0F / ms : 0.0F);
            ImGui::EndTooltip();
        }
    }

    const ImU32 lineColor = IM_COL32(255, 255, 255, 70);
    const ImU32 labelColor = IM_COL32(255, 255, 255, 140);
    for (const auto& [ms, label] :
         {std::pair{budgetMs, "60 FPS"}, std::pair{2.0F * budgetMs, "30 FPS"}}) {
        if (ms < top) {
            const float y = yOf(ms);
            draw.AddLine(ImVec2(origin.x + pad, y), ImVec2(end.x - pad, y), lineColor);
            const ImVec2 size = ImGui::CalcTextSize(label);
            draw.AddText(ImVec2(end.x - pad - size.x - 2.0F, y - size.y), labelColor, label);
        }
    }
    const std::string latest = std::format("{:.2f} ms", history.latest());
    draw.AddText(ImVec2(origin.x + pad + 4.0F, origin.y + pad), labelColor, latest.c_str());
}

} // namespace astra2d::detail
