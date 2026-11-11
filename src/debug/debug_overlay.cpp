#include "astra2d/debug/debug_overlay.hpp"

#include "astra2d/core/application.hpp"
#include "astra2d/core/log.hpp"
#include "astra2d/core/profiler.hpp"
#include "astra2d/renderer/camera.hpp"
#include "astra2d/scene/scene.hpp"
#include "astra2d/scene/serializer.hpp"
#include "imgui_helpers.hpp"
#include "entity_moving.hpp"
#include "entity_picking.hpp"
#include "profiler_panel.hpp"
#include "selection_outline.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace astra2d {

namespace {

constexpr float kMargin = 10.0F;
constexpr double kBudgetMs = 1000.0 / 60.0; // the frame budget the breakdown bar is measured on

// Horizontal marks across the last item (the frame graph) at @p ms on a scale of 0 to @p top.
void budgetLine(float ms, float top, const char* label)
{
    if (ms >= top) {
        return;
    }
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const ImVec2 min(ImGui::GetItemRectMin().x + padding.x, ImGui::GetItemRectMin().y + padding.y);
    const ImVec2 max(ImGui::GetItemRectMax().x - padding.x, ImGui::GetItemRectMax().y - padding.y);
    const float y = max.y - ((ms / top) * (max.y - min.y));
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(255, 220, 120, 110);
    draw.AddLine(ImVec2(min.x, y), ImVec2(max.x, y), color);
    draw.AddText(ImVec2(min.x + 2.0F, y - ImGui::GetFontSize()), color, label);
}

} // namespace

DebugOverlay::DebugOverlay(Application& app)
    : m_app(&app)
    , m_imgui(app.window())
{
    const char* shown = std::getenv("ASTRA2D_DEBUG_OVERLAY"); // NOLINT(concurrency-mt-unsafe)
    if (shown != nullptr && std::string_view(shown) == "1") {
        setVisible(true);
    }
}

DebugOverlay::~DebugOverlay()
{
    Profiler::global().setEnabled(false);
}

void DebugOverlay::setVisible(bool visible)
{
    m_visible = visible;
    Profiler::global().setEnabled(m_visible && !m_profilerPaused);
    if (!visible) {
        setMouseTool(MouseTool::None);
    }
}

void DebugOverlay::setCamera(const OrthographicCamera* camera)
{
    m_camera = camera;
    if (camera == nullptr) {
        setMouseTool(MouseTool::None);
    }
}

void DebugOverlay::setSceneFile(const SceneSerializer* serializer, std::filesystem::path path)
{
    m_serializer = serializer;
    m_sceneFile = std::move(path);
    m_saveStatus.clear();
}

void DebugOverlay::setMouseTool(MouseTool tool)
{
    m_tool = tool;
    // The clicks of a tool must not also act in the game.
    m_imgui.setCaptureMouse(tool != MouseTool::None);
    m_hovered = kNullEntity;
    m_dragged = kNullEntity;
}

void DebugOverlay::updateMouseTool()
{
    if (m_scene == nullptr || m_camera == nullptr) {
        setMouseTool(MouseTool::None);
        return;
    }
    const Registry& registry = m_scene->registry();
    // Input's cursor position is in framebuffer pixels, as screenToWorld() expects (ImGui's is in
    // screen coordinates, which differ on HiDPI displays).
    const glm::vec2 mouse = m_camera->screenToWorld(m_app->input().mousePosition());
    if (!m_dragged.isNull()) {
        if (registry.valid(m_dragged) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            detail::moveEntityTo(*m_scene, m_dragged, mouse + m_grabOffset);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            return;
        }
        m_dragged = kNullEntity; // released, or the entity is gone
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || ImGui::IsAnyItemActive()) {
        m_hovered = kNullEntity; // over the overlay: its own widgets get the click
        return;
    }
    m_hovered = detail::entityAt(*m_scene, mouse, m_app->interpolationAlpha());
    ImGui::SetMouseCursor(m_tool == MouseTool::Move && !m_hovered.isNull()
                              ? ImGuiMouseCursor_ResizeAll
                              : ImGuiMouseCursor_Hand);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        setMouseTool(MouseTool::None);
        return;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }
    if (!m_hovered.isNull()) {
        m_inspector.select(m_hovered);
    }
    if (m_tool == MouseTool::Pick) {
        setMouseTool(MouseTool::None); // one pick, like a browser's element picker
    } else if (!m_hovered.isNull()) {
        m_dragged = m_hovered; // held where it was grabbed, not by its centre
        m_grabOffset = m_scene->worldTransform(m_dragged).position - mouse;
    }
}

// NOLINTNEXTLINE(readability-non-const-parameter): ImGui::Checkbox writes through it later
void DebugOverlay::addToggle(std::string label, bool* value)
{
    m_toggles.push_back({.label = std::move(label), .value = value});
}

void DebugOverlay::addPanel(std::string title, std::function<void()> draw)
{
    m_panels.push_back({.title = std::move(title), .draw = std::move(draw)});
}

void DebugOverlay::addFrame(const RenderStats& render)
{
    const float seconds = m_app->frameTime();
    m_frameTimes.push(1000.0F * seconds);
    m_frameStats.addFrame(seconds, render);
    m_render = render;
}

void DebugOverlay::draw()
{
    m_imgui.beginFrame();
    if (m_visible) {
        drawDebugWindow();
        if (m_scene != nullptr) {
            drawEntitiesWindow();
        }
        if (m_tool != MouseTool::None) {
            updateMouseTool();
        }
    }
    m_imgui.endFrame();
}

void DebugOverlay::drawDebugWindow()
{
    ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0F, 600.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85F);
    if (ImGui::Begin("Debug")) {
        if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawFrameSection();
        }
        if (ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawTimeSection();
        }
        if (ImGui::CollapsingHeader("Profiler", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawProfilerSection();
        }
        if (!m_toggles.empty() &&
            ImGui::CollapsingHeader("Toggles", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const Toggle& toggle : m_toggles) {
                ImGui::Checkbox(toggle.label.c_str(), toggle.value);
            }
        }
        for (const Panel& panel : m_panels) {
            if (ImGui::CollapsingHeader(panel.title.c_str())) {
                ImGui::PushID(panel.title.c_str());
                panel.draw();
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void DebugOverlay::drawFrameSection()
{
    const FrameReport& report = m_frameStats.report();
    detail::text("{:.1f} FPS, {:.2f} ms (min {:.2f}, max {:.2f})", report.fps, report.averageMs,
                 report.minMs, report.maxMs);
    if (!m_frameTimes.empty()) {
        // The scale grows with spikes, but never below 20 ms, so a steady 60 FPS sits near the
        // middle instead of filling the graph with noise.
        const float top = std::max(1.2F * m_frameTimes.max(), 20.0F);
        const std::string overlay = std::format("{:.2f} ms", m_frameTimes.latest());
        ImGui::PlotLines("##frame times", m_frameTimes.data(),
                         static_cast<int>(m_frameTimes.size()),
                         static_cast<int>(m_frameTimes.offset()), overlay.c_str(), 0.0F, top,
                         ImVec2(-FLT_MIN, 60.0F));
        budgetLine(1000.0F / 60.0F, top, "60 FPS");
        budgetLine(1000.0F / 30.0F, top, "30 FPS");
    }
    detail::drawFrameBreakdown(Profiler::global(), kBudgetMs);
    detail::text("{} draw calls, {} quads", m_render.drawCalls, m_render.quads);
    if (m_scene != nullptr) {
        detail::text("{} entities", m_scene->registry().alive());
    }
}

void DebugOverlay::drawTimeSection()
{
    Application& app = *m_app;
    bool paused = app.timeScale() == 0.0;
    if (ImGui::Checkbox("Pause", &paused)) {
        app.setTimeScale(paused ? 0.0 : m_runningScale);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button("Step")) {
        app.stepOnce(); // one fixed step in the next frame
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    for (const float scale : {0.25F, 1.0F}) {
        const std::string label = std::format("{}x", scale);
        if (ImGui::SmallButton(label.c_str())) {
            m_runningScale = scale;
            app.setTimeScale(scale);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##time scale", &m_runningScale, 0.05F, 4.0F, "time scale %.2fx",
                           ImGuiSliderFlags_Logarithmic) &&
        !paused) {
        app.setTimeScale(m_runningScale);
    }
}

void DebugOverlay::drawProfilerSection()
{
    if (ImGui::Checkbox("Freeze", &m_profilerPaused)) {
        setVisible(m_visible); // applies the pause to the profiler
    }
    ImGui::SameLine();
    detail::hint("(times in ms, averaged over recent frames)");
    detail::drawProfilerTable(Profiler::global());
}

void DebugOverlay::drawEntitiesWindow()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // Kept at the right edge when the game window is resized (a tiling window manager often
    // resizes it right after it opens); otherwise where the user put it.
    const glm::vec2 size{display.x, display.y};
    const bool resized = size != m_displaySize;
    m_displaySize = size;
    const ImGuiCond when = resized ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    ImGui::SetNextWindowPos(ImVec2(display.x - kMargin, kMargin), when, ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(340.0F, std::min(display.y - (2.0F * kMargin), 640.0F)), when);
    ImGui::SetNextWindowBgAlpha(0.85F);
    if (ImGui::Begin("Entities")) {
        if (m_camera != nullptr) {
            drawMouseTools();
        }
        if (m_serializer != nullptr) {
            drawSaveButton();
        }
        m_inspector.draw(*m_scene);
    }
    ImGui::End();
}

void DebugOverlay::drawMouseTools()
{
    const auto toolButton = [this](const char* label, MouseTool tool) {
        const bool active = m_tool == tool;
        if (active) { // shown pressed while it is on
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label)) {
            setMouseTool(active ? MouseTool::None : tool);
        }
        if (active) {
            ImGui::PopStyleColor();
        }
    };
    toolButton("Pick", MouseTool::Pick);
    ImGui::SameLine();
    toolButton("Move", MouseTool::Move);
    ImGui::SameLine();
    switch (m_tool) {
    case MouseTool::None:
        detail::hint("click or drag entities in the game");
        break;
    case MouseTool::Pick:
        detail::hint("click an entity (right click: cancel)");
        break;
    case MouseTool::Move:
        detail::hint("drag entities (right click: done)");
        break;
    }
}

void DebugOverlay::drawSaveButton()
{
    if (ImGui::Button("Save scene")) {
        try {
            m_serializer->saveFile(*m_scene, m_sceneFile);
            m_saveStatus = std::format("saved {} entities", m_scene->registry().alive());
            ASTRA_CORE_INFO("Saved the scene to {}", m_sceneFile.string());
        } catch (const std::exception& error) {
            m_saveStatus = std::string("failed: ") + error.what();
            ASTRA_CORE_ERROR("Saving the scene failed: {}", error.what());
        }
    }
    ImGui::SameLine();
    detail::hint(m_saveStatus.empty() ? m_sceneFile.filename().string() : m_saveStatus);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        detail::text("{}", m_sceneFile.string());
        ImGui::EndTooltip();
    }
}

void DebugOverlay::drawSelection(Renderer2D& renderer, float lineWidth) const
{
    if (!m_visible || m_scene == nullptr) {
        return;
    }
    const Registry& registry = m_scene->registry();
    const auto outline = [&](Entity entity, glm::vec4 color) {
        const std::array<glm::vec2, 4> corners = detail::selectionOutline(
            *m_scene, entity, 4.0F * lineWidth, m_app->interpolationAlpha());
        for (std::size_t i = 0; i < corners.size(); ++i) {
            renderer.drawLine(corners[i], corners[(i + 1) % corners.size()], color, lineWidth);
        }
    };
    const Entity selected = m_inspector.selected();
    if (m_tool != MouseTool::None && registry.valid(m_hovered) && m_hovered != selected) {
        outline(m_hovered, {1.0F, 1.0F, 1.0F, 0.5F}); // what a click would pick
    }
    if (registry.valid(selected)) {
        // Pulses, so it stands out against any background.
        const auto pulse = static_cast<float>(0.5 + (0.5 * std::sin(8.0 * ImGui::GetTime())));
        outline(selected, {1.0F, 0.85F + (0.15F * pulse), 0.2F, 0.6F + (0.4F * pulse)});
    }
}

} // namespace astra2d
