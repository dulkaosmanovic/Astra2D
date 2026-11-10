#include "profiler_panel.hpp"

#include "astra2d/core/profiler.hpp"
#include "imgui_helpers.hpp"

#include <imgui.h>

#include <cstddef>
#include <vector>

namespace astra2d::detail {

namespace {

// One row: the scope's tree node and its numbers. Returns true if it is open and has children,
// i.e. its children follow and a TreePop() is due after them.
bool drawRow(const ProfileNode& node, double frameMs)
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
    text("{:.1f} %", frameMs > 0.0 ? 100.0 * node.averageMs / frameMs : 0.0);
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
        if (drawRow(node, frameMs)) {
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
    if (!ImGui::BeginTable("profiler", 4, kFlags)) {
        return;
    }
    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ms (avg)");
    ImGui::TableSetupColumn("frame");
    ImGui::TableSetupColumn("calls");
    ImGui::TableHeadersRow();
    drawTree(nodes);
    ImGui::EndTable();
}

} // namespace astra2d::detail
