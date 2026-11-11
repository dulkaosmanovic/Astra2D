#include "profiler_panel.hpp"

#include "astra2d/core/profiler.hpp"
#include "imgui_helpers.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace astra2d::detail {

namespace {

// The time of a scope that is not in its children: work done directly in it, or in code without
// its own scope.
double selfMs(const std::vector<ProfileNode>& nodes, const ProfileNode& node)
{
    double children = 0.0;
    for (const int child : node.children) {
        children += nodes[static_cast<std::size_t>(child)].averageMs;
    }
    return std::max(node.averageMs - children, 0.0);
}

// One row: the scope's tree node and its numbers. Returns true if it is open and has children,
// i.e. its children follow and a TreePop() is due after them.
bool drawRow(const std::vector<ProfileNode>& nodes, const ProfileNode& node, double frameMs)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const bool leaf = node.children.empty();
    const int flags = detail::flags(
        ImGuiTreeNodeFlags_SpanFullWidth, node.depth < 2 ? ImGuiTreeNodeFlags_DefaultOpen : 0,
        leaf ? ImGuiTreeNodeFlags_Leaf : 0, leaf ? ImGuiTreeNodeFlags_NoTreePushOnOpen : 0);
    // Siblings have different names, so the name is a unique ID under its parent's tree node.
    const bool open = ImGui::TreeNodeEx(node.name.c_str(), flags);
    ImGui::TableNextColumn();
    text("{:.3f}", node.averageMs);
    ImGui::TableNextColumn();
    text("{:.3f}", selfMs(nodes, node));
    ImGui::TableNextColumn();
    text("{:.2f}", node.peakMs);
    ImGui::TableNextColumn();
    const double share = frameMs > 0.0 ? node.averageMs / frameMs : 0.0;
    const std::string label = std::format("{:.1f} %", 100.0 * share);
    ImGui::ProgressBar(static_cast<float>(share), ImVec2(-FLT_MIN, 0.0F), label.c_str());
    ImGui::TableNextColumn();
    text("{}", node.calls);
    return open && !leaf;
}

// Depth-first with an explicit stack; kTreePop marks where an open node's children end.
void drawTree(const std::vector<ProfileNode>& nodes)
{
    constexpr int kTreePop = -1;
    const double frameMs = nodes[0].averageMs;
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int index = stack.back();
        stack.pop_back();
        if (index == kTreePop) {
            ImGui::TreePop();
            continue;
        }
        const ProfileNode& node = nodes[static_cast<std::size_t>(index)];
        if (drawRow(nodes, node, frameMs)) {
            stack.push_back(kTreePop);
            stack.insert(stack.end(), node.children.rbegin(), node.children.rend());
        }
    }
}

} // namespace

void drawProfilerTable(const Profiler& profiler)
{
    const std::vector<ProfileNode>& nodes = profiler.nodes();
    if (nodes.empty() || profiler.frames() == 0) {
        ImGui::TextUnformatted("No frame recorded yet.");
        return;
    }
    constexpr int kFlags = detail::flags(ImGuiTableFlags_RowBg, ImGuiTableFlags_BordersInnerV,
                                         ImGuiTableFlags_Resizable, ImGuiTableFlags_SizingFixedFit);
    if (!ImGui::BeginTable("profiler", 6, kFlags)) {
        return;
    }
    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ms");
    ImGui::TableSetupColumn("self");
    ImGui::TableSetupColumn("peak");
    ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthFixed, 6.0F * ImGui::GetFontSize());
    ImGui::TableSetupColumn("calls");
    ImGui::TableHeadersRow();
    drawTree(nodes);
    ImGui::EndTable();
}

void drawFrameBreakdown(const Profiler& profiler, double budgetMs)
{
    const std::vector<ProfileNode>& nodes = profiler.nodes();
    if (nodes.empty() || profiler.frames() == 0) {
        return;
    }
    // A colour per phase, in the order the phases first ran; muted so white text stays readable.
    constexpr std::array<ImU32, 6> kColors = {
        IM_COL32(66, 133, 196, 255), IM_COL32(214, 128, 52, 255),  IM_COL32(84, 166, 98, 255),
        IM_COL32(190, 84, 84, 255),  IM_COL32(146, 108, 188, 255), IM_COL32(150, 150, 150, 255)};
    const ProfileNode& frame = nodes[0];
    // The bar is the budget wide, or the frame if it is longer, so an overrun reaches past the
    // budget mark.
    const double scaleMs = std::max(frame.averageMs, budgetMs);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    draw.AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                       ImGui::GetColorU32(ImGuiCol_FrameBg));

    float x = origin.x;
    std::size_t color = 0;
    for (const int index : frame.children) {
        const ProfileNode& phase = nodes[static_cast<std::size_t>(index)];
        const float w = static_cast<float>(phase.averageMs / scaleMs) * width;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + w, origin.y + height);
        draw.AddRectFilled(min, max, kColors.at(color++ % kColors.size()));
        const ImVec2 size = ImGui::CalcTextSize(phase.name.c_str());
        if (size.x + 4.0F < w) {
            draw.AddText(ImVec2(min.x + 2.0F, min.y + ((height - size.y) * 0.5F)), IM_COL32_WHITE,
                         phase.name.c_str());
        }
        if (w > 0.0F && ImGui::IsMouseHoveringRect(min, max)) {
            ImGui::BeginTooltip();
            text("{}: {:.3f} ms", phase.name, phase.averageMs);
            ImGui::EndTooltip();
        }
        x += w;
    }
    const float budgetX = origin.x + (static_cast<float>(budgetMs / scaleMs) * width);
    draw.AddLine(ImVec2(budgetX, origin.y - 2.0F), ImVec2(budgetX, origin.y + height + 2.0F),
                 IM_COL32(255, 255, 255, 200), 2.0F);
    ImGui::Dummy(ImVec2(width, height));
    text("frame {:.2f} ms of a {:.2f} ms budget ({:.0f} FPS)", frame.averageMs, budgetMs,
         1000.0 / budgetMs);
}

} // namespace astra2d::detail
