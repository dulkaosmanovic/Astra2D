#include "astra2d/debug/debug_overlay.hpp"

#include "astra2d/core/application.hpp"
#include "astra2d/core/log.hpp"
#include "astra2d/core/profiler.hpp"
#include "astra2d/renderer/camera.hpp"
#include "astra2d/scene/scene.hpp"
#include "astra2d/scene/serializer.hpp"
#include "imgui_helpers.hpp"
#include "entity_moving.hpp"
#include "entity_rows.hpp"
#include "entity_picking.hpp"
#include "frame_graph.hpp"
#include "imgui_style.hpp"
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
#include <vector>

namespace astra2d {

namespace {

constexpr float kMargin = 10.0F;
constexpr double kBudgetMs = 1000.0 / 60.0; // the frame budget the breakdown bar is measured on

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
        drawBadges();
        drawSelectionLabel();
    }
    m_imgui.endFrame();
}

void DebugOverlay::drawDebugWindow()
{
    ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin), ImGuiCond_FirstUseEver);
    const float em = ImGui::GetFontSize(); // sized in lines of text, so it scales with the font
    ImGui::SetNextWindowSize(ImVec2(34.0F * em, 40.0F * em), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Astra2D debug  (F1)###Debug")) {
        drawHeader();
        drawFrameSection();
        ImGui::Spacing();
        if (ImGui::BeginTabBar("sections")) {
            if (ImGui::BeginTabItem("Profiler")) {
                drawProfilerSection();
                ImGui::EndTabItem();
            }
            if ((!m_toggles.empty() || !m_panels.empty()) && ImGui::BeginTabItem("Game")) {
                drawGameSection();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// The frame rate in large type, coloured by the budget, then the time controls.
void DebugOverlay::drawHeader()
{
    const FrameReport& report = m_frameStats.report();
    const ImU32 color = report.frames == 0
                            ? ImGui::GetColorU32(ImGuiCol_Text)
                            : detail::budgetColor(report.averageMs, static_cast<float>(kBudgetMs));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8F);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    detail::text("{:.0f}", report.fps);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::BeginGroup();
    detail::hint("FPS");
    detail::text("{:.2f} ms  (min {:.2f}, max {:.2f})", report.averageMs, report.minMs,
                 report.maxMs);
    ImGui::EndGroup();
    drawTimeSection();
    ImGui::Separator();
}

void DebugOverlay::drawFrameSection()
{
    detail::drawFrameGraph(m_frameTimes, static_cast<float>(kBudgetMs));
    detail::drawFrameBreakdown(Profiler::global(), kBudgetMs);
    std::string stats =
        std::format("{} draw calls  ·  {} quads", m_render.drawCalls, m_render.quads);
    if (m_scene != nullptr) {
        stats += std::format("  ·  {} entities", m_scene->registry().alive());
    }
    detail::hint(stats);
}

// Pause, single steps and the time scale, as one toolbar.
void DebugOverlay::drawTimeSection()
{
    Application& app = *m_app;
    const bool paused = app.timeScale() == 0.0;
    const auto toggled = [](const char* label, bool on) {
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        const bool clicked = ImGui::Button(label);
        if (on) {
            ImGui::PopStyleColor();
        }
        return clicked;
    };
    if (toggled(paused ? "Resume" : "Pause", paused)) {
        app.setTimeScale(paused ? m_runningScale : 0.0);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button("Step")) {
        app.stepOnce(); // one fixed step in the next frame
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0F, 2.0F * ImGui::GetStyle().ItemSpacing.x);
    for (const float scale : {0.25F, 0.5F, 1.0F, 2.0F}) {
        const std::string label = std::format("{}x", scale);
        if (toggled(label.c_str(), m_runningScale == scale)) {
            m_runningScale = scale;
            if (!paused) {
                app.setTimeScale(scale);
            }
        }
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##time scale", &m_runningScale, 0.05F, 4.0F, "%.2fx",
                           ImGuiSliderFlags_Logarithmic) &&
        !paused) {
        app.setTimeScale(m_runningScale);
    }
}

void DebugOverlay::drawProfilerSection()
{
    if (ImGui::Checkbox("Freeze", &m_profilerPaused)) {
        setVisible(m_visible); // applies the freeze to the profiler
    }
    ImGui::SameLine();
    detail::hint("times in ms, averaged over recent frames");
    detail::drawProfilerTable(Profiler::global());
}

void DebugOverlay::drawGameSection()
{
    for (const Toggle& toggle : m_toggles) {
        ImGui::Checkbox(toggle.label.c_str(), toggle.value);
    }
    for (const Panel& panel : m_panels) {
        ImGui::PushID(panel.title.c_str());
        ImGui::SeparatorText(panel.title.c_str());
        panel.draw();
        ImGui::PopID();
    }
}

// Pills at the top of the screen for states that change how the game behaves, so they are seen
// even with the windows collapsed: paused, slow motion, a mouse tool.
void DebugOverlay::drawBadges()
{
    std::vector<std::pair<std::string, ImU32>> badges;
    const double scale = m_app->timeScale();
    if (scale == 0.0) {
        badges.emplace_back("PAUSED", detail::OverlayColors::kBad);
    } else if (scale != 1.0) {
        badges.emplace_back(std::format("{:.2g}x SPEED", scale), detail::OverlayColors::kWarn);
    }
    if (m_tool == MouseTool::Pick) {
        badges.emplace_back("PICK: click an entity", detail::OverlayColors::kAccent);
    } else if (m_tool == MouseTool::Move) {
        badges.emplace_back("MOVE: drag entities", detail::OverlayColors::kAccent);
    }
    if (badges.empty()) {
        return;
    }
    const ImVec2 pad(1.5F * ImGui::GetStyle().FramePadding.x,
                     1.5F * ImGui::GetStyle().FramePadding.y);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    float total = -gap;
    for (const auto& [label, color] : badges) {
        total += ImGui::CalcTextSize(label.c_str()).x + (2.0F * pad.x) + gap;
    }
    ImDrawList& draw = *ImGui::GetForegroundDrawList();
    float x = (ImGui::GetIO().DisplaySize.x - total) * 0.5F;
    const float y = kMargin;
    for (const auto& [label, color] : badges) {
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 max(x + size.x + (2.0F * pad.x), y + size.y + (2.0F * pad.y));
        draw.AddRectFilled(ImVec2(x, y), max, color, max.y - y);
        draw.AddText(ImVec2(x + pad.x, y + pad.y), IM_COL32_WHITE, label.c_str());
        x = max.x + gap;
    }
}

// The selected entity's name just above its outline in the game.
void DebugOverlay::drawSelectionLabel()
{
    const Entity selected = m_inspector.selected();
    if (m_scene == nullptr || m_camera == nullptr || !m_scene->registry().valid(selected)) {
        return;
    }
    const auto corners =
        detail::selectionOutline(*m_scene, selected, 0.0F, m_app->interpolationAlpha());
    // worldToScreen() gives framebuffer pixels; ImGui draws in screen coordinates.
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float left = FLT_MAX;
    float right = -FLT_MAX;
    float top = FLT_MAX;
    for (const glm::vec2 corner : corners) {
        const glm::vec2 p = m_camera->worldToScreen(corner);
        left = std::min(left, p.x / scale.x);
        right = std::max(right, p.x / scale.x);
        top = std::min(top, p.y / scale.y);
    }
    const std::string label = detail::entityLabel(m_scene->registry(), selected);
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const ImVec2 min((((left + right) - size.x) * 0.5F) - pad.x, top - size.y - (3.0F * pad.y));
    const ImVec2 max(min.x + size.x + (2.0F * pad.x), min.y + size.y + (2.0F * pad.y));
    ImDrawList& draw = *ImGui::GetBackgroundDrawList(); // behind the windows
    draw.AddRectFilled(min, max, IM_COL32(14, 18, 28, 200), ImGui::GetStyle().FrameRounding);
    draw.AddText(ImVec2(min.x + pad.x, min.y + pad.y), IM_COL32(255, 222, 90, 255), label.c_str());
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
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(24.0F * em, std::min(display.y - (2.0F * kMargin), 44.0F * em)),
                             when);
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
    const auto toolButton = [this](const char* label, MouseTool tool, const char* help) {
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
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(help);
            ImGui::EndTooltip();
        }
    };
    toolButton("Pick", MouseTool::Pick, "Select an entity by clicking it in the game");
    ImGui::SameLine();
    toolButton("Move", MouseTool::Move, "Drag entities in the game with the mouse");
    if (m_tool != MouseTool::None) {
        ImGui::SameLine();
        detail::hint("right click to stop");
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
        ImGui::SetCurrentContext(m_imgui.context()); // its clock, also with other contexts
        const auto pulse = static_cast<float>(0.5 + (0.5 * std::sin(8.0 * ImGui::GetTime())));
        outline(selected, {1.0F, 0.85F + (0.15F * pulse), 0.2F, 0.6F + (0.4F * pulse)});
    }
}

} // namespace astra2d
