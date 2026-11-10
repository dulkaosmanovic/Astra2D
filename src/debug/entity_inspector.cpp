#include "astra2d/debug/entity_inspector.hpp"

#include "astra2d/scene/scene.hpp"
#include "component_widgets.hpp"
#include "entity_rows.hpp"
#include "imgui_helpers.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>

namespace astra2d {

namespace {

// One row of the entity list, indented by its depth, with an arrow if it has children. Updates
// @p expanded when the arrow is clicked; returns true when the row itself is clicked.
bool drawRow(const Registry& registry, const detail::EntityRow& row, bool selected,
             std::unordered_set<Entity>& expanded)
{
    ImGui::PushID(static_cast<int>(row.entity.value()));
    const float indent = static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0F) {
        ImGui::Indent(indent);
    }
    const int flags =
        detail::flags(ImGuiTreeNodeFlags_SpanAvailWidth, ImGuiTreeNodeFlags_NoTreePushOnOpen,
                      ImGuiTreeNodeFlags_OpenOnArrow, row.hasChildren ? 0 : ImGuiTreeNodeFlags_Leaf,
                      selected ? ImGuiTreeNodeFlags_Selected : 0);
    // The open state is kept by the inspector, not by ImGui, because entityRows() needs it.
    ImGui::SetNextItemOpen(expanded.contains(row.entity));
    const bool open = ImGui::TreeNodeEx(detail::entityLabel(registry, row.entity).c_str(), flags);
    const bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
    if (row.hasChildren && open) {
        expanded.insert(row.entity);
    } else {
        expanded.erase(row.entity);
    }
    if (indent > 0.0F) {
        ImGui::Unindent(indent);
    }
    ImGui::PopID();
    return clicked;
}

} // namespace

EntityInspector::EntityInspector()
{
    detail::addBuiltInInspectors(*this);
}

void EntityInspector::addInspector(std::string name, Has has, Inspect inspect)
{
    m_inspectors.push_back(
        {.name = std::move(name), .has = std::move(has), .inspect = std::move(inspect)});
}

std::vector<std::string> EntityInspector::componentNames() const
{
    std::vector<std::string> names;
    names.reserve(m_inspectors.size());
    for (const ComponentInspector& inspector : m_inspectors) {
        names.push_back(inspector.name);
    }
    return names;
}

void EntityInspector::draw(Scene& scene)
{
    const Registry& registry = scene.registry();
    if (!registry.valid(m_selected)) {
        m_selected = kNullEntity;
    }
    std::erase_if(m_expanded, [&](Entity entity) { return !registry.valid(entity); });
    drawList(scene);
    drawSelected(scene);
}

void EntityInspector::drawList(Scene& scene)
{
    const Registry& registry = scene.registry();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter by name", &m_filter);
    const std::vector<detail::EntityRow> rows = detail::entityRows(scene, m_expanded, m_filter);
    if (m_filter.empty()) {
        detail::text("{} entities", registry.alive());
    } else {
        detail::text("{} of {} entities", rows.size(), registry.alive());
    }

    const float height = 12.0F * ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginChild("entities", ImVec2(0.0F, height),
                          detail::flags(ImGuiChildFlags_Borders, ImGuiChildFlags_ResizeY))) {
        // Only the visible rows are drawn, so a scene with thousands of entities stays cheap.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const detail::EntityRow& row = rows[static_cast<std::size_t>(i)];
                if (drawRow(registry, row, row.entity == m_selected, m_expanded)) {
                    m_selected = row.entity;
                }
            }
        }
    }
    ImGui::EndChild();
}

void EntityInspector::drawSelected(Scene& scene)
{
    Registry& registry = scene.registry();
    if (m_selected.isNull()) {
        detail::hint("Select an entity to inspect it.");
        return;
    }
    ImGui::SeparatorText(detail::entityLabel(registry, m_selected).c_str());
    const Entity parent = scene.parent(m_selected);
    if (!parent.isNull()) {
        ImGui::TextUnformatted("Parent");
        ImGui::SameLine();
        if (ImGui::SmallButton(detail::entityLabel(registry, parent).c_str())) {
            m_selected = parent;
            return; // the next frame shows the parent
        }
    }
    for (const ComponentInspector& inspector : m_inspectors) {
        if (!inspector.has(registry, m_selected)) {
            continue;
        }
        if (ImGui::CollapsingHeader(inspector.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID(inspector.name.c_str());
            inspector.inspect(registry, m_selected);
            ImGui::PopID();
        }
    }
}

} // namespace astra2d
