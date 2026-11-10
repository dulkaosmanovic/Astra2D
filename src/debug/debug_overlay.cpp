#include "astra2d/debug/debug_overlay.hpp"

#include "astra2d/core/profiler.hpp"
#include "astra2d/scene/scene.hpp"
#include "imgui_helpers.hpp"
#include "profiler_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace astra2d {

namespace {

constexpr float kMargin = 10.0F;

} // namespace

DebugOverlay::DebugOverlay(Window& window)
    : m_imgui(window)
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

void DebugOverlay::addFrame(float seconds, const RenderStats& render)
{
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
    }
    m_imgui.endFrame();
}

void DebugOverlay::drawDebugWindow()
{
    ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0F, 520.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85F);
    if (ImGui::Begin("Debug")) {
        if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawFrameSection();
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
    }
    detail::text("{} draw calls, {} quads", m_render.drawCalls, m_render.quads);
    if (m_scene != nullptr) {
        detail::text("{} entities", m_scene->registry().alive());
    }
}

void DebugOverlay::drawProfilerSection()
{
    if (ImGui::Checkbox("Pause", &m_profilerPaused)) {
        setVisible(m_visible); // applies the pause to the profiler
    }
    ImGui::SameLine();
    detail::hint("(times in ms, averaged over recent frames)");
    detail::drawProfilerTable(Profiler::global());
}

void DebugOverlay::drawEntitiesWindow()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = 340.0F;
    ImGui::SetNextWindowPos(ImVec2(display.x - width - kMargin, kMargin), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, std::min(display.y - (2.0F * kMargin), 640.0F)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85F);
    if (ImGui::Begin("Entities")) {
        m_inspector.draw(*m_scene);
    }
    ImGui::End();
}

} // namespace astra2d
