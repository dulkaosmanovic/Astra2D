#pragma once

#include "astra2d/debug/entity_inspector.hpp"
#include "astra2d/debug/frame_history.hpp"
#include "astra2d/debug/frame_stats.hpp"
#include "astra2d/debug/imgui_layer.hpp"
#include "astra2d/renderer/renderer2d.hpp"

#include <functional>
#include <string>
#include <vector>

/// @addtogroup debug
/// @{

namespace astra2d {

class Scene;
class Window;

/// An in-game debug window drawn with Dear ImGui, hidden until shown (the examples use F1).
///
/// Its "Debug" window has:
/// - **Frame**: a graph of the last frame times, FPS, mean / min / max frame time, draw calls and
///   quads (from addFrame());
/// - **Profiler**: the call tree of Profiler::global(), averaged over recent frames. The profiler
///   records only while the overlay is visible;
/// - **Toggles**: the game's debug switches (addToggle()), e.g. the physics debug view;
/// - the game's own sections (addPanel()).
///
/// With a scene (setScene()), an "Entities" window lists its entities and inspects the selected
/// one (see EntityInspector).
///
/// @code
/// // members, after the Application's window exists:
/// astra2d::DebugOverlay m_overlay{window()};
///
/// void onStart() override
/// {
///     m_overlay.setScene(&m_scene);
///     m_overlay.addToggle("Physics debug view (F3)", &m_showPhysics);
/// }
/// void onUpdate(float dt) override
/// {
///     m_frameTime = dt;
///     if (input().isPressed(astra2d::Key::F1)) { m_overlay.toggle(); }
///     if (!m_overlay.wantsMouse() && input().isPressed(astra2d::MouseButton::Left)) { ... }
/// }
/// void onRender() override
/// {
///     ... // draw the game
///     m_overlay.addFrame(m_frameTime, m_renderer.stats());
///     m_renderer.resetStats();
///     m_overlay.draw(); // last: on top of everything
/// }
/// @endcode
class DebugOverlay {
public:
    /// Starts ImGui on @p window (see ImGuiLayer). The overlay starts hidden, or shown if the
    /// environment variable `ASTRA2D_DEBUG_OVERLAY` is `1` (e.g. for screenshots and smoke tests).
    /// @throws std::runtime_error if ImGui cannot be started.
    explicit DebugOverlay(Window& window);
    /// Stops ImGui and turns Profiler::global() off.
    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;
    DebugOverlay(DebugOverlay&&) = delete;
    DebugOverlay& operator=(DebugOverlay&&) = delete;

    /// Shows or hides the overlay, and turns Profiler::global() on or off with it.
    void setVisible(bool visible);
    /// True while the overlay is shown.
    [[nodiscard]] bool isVisible() const { return m_visible; }
    /// Shows the overlay if it is hidden and hides it otherwise.
    void toggle() { setVisible(!m_visible); }

    /// The scene whose entities the "Entities" window shows; nullptr (the default) hides that
    /// window. The scene must outlive the overlay or be replaced first.
    void setScene(Scene* scene) { m_scene = scene; }
    /// The entity list and inspector, e.g. to add inspectors for the game's components.
    [[nodiscard]] EntityInspector& inspector() { return m_inspector; }

    /// Adds a checkbox called @p label for @p value, which must outlive the overlay.
    void addToggle(std::string label, bool* value);
    /// Adds a section called @p title to the "Debug" window; @p draw adds its ImGui widgets
    /// (`#include <imgui.h>`) each frame while the section is open.
    void addPanel(std::string title, std::function<void()> draw);

    /// Records a frame that took @p seconds and drew @p render, for the frame section. Call once
    /// per frame, also while hidden, so the numbers are current when it is shown.
    void addFrame(float seconds, const RenderStats& render = {});

    /// Draws the overlay into the current framebuffer, on top of what is there. Call once per
    /// frame at the end of Application::onRender(), also while hidden (ImGui then only takes the
    /// frame's input events).
    void draw();

    /// True when the overlay uses the mouse (the cursor is over one of its windows): the game
    /// should then ignore clicks and the scroll wheel.
    [[nodiscard]] bool wantsMouse() const { return m_imgui.wantsMouse(); }
    /// True when the overlay uses the keyboard (a text field is active): the game should then
    /// ignore key presses.
    [[nodiscard]] bool wantsKeyboard() const { return m_imgui.wantsKeyboard(); }

private:
    struct Toggle {
        std::string label;
        bool* value;
    };
    struct Panel {
        std::string title;
        std::function<void()> draw;
    };

    void drawDebugWindow();
    void drawFrameSection();
    void drawProfilerSection();
    void drawEntitiesWindow();

    ImGuiLayer m_imgui;
    EntityInspector m_inspector;
    FrameHistory m_frameTimes;     // milliseconds, for the graph
    FrameStats m_frameStats{0.5F}; // FPS and min / mean / max, twice a second
    RenderStats m_render;          // the last frame's
    Scene* m_scene = nullptr;
    std::vector<Toggle> m_toggles;
    std::vector<Panel> m_panels;
    bool m_visible = false;
    bool m_profilerPaused = false;
};

} // namespace astra2d

/// @}
