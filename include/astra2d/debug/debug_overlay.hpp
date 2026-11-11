#pragma once

#include "astra2d/debug/entity_inspector.hpp"
#include "astra2d/debug/frame_history.hpp"
#include "astra2d/debug/frame_stats.hpp"
#include "astra2d/debug/imgui_layer.hpp"
#include "astra2d/renderer/renderer2d.hpp"

#include <glm/vec2.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/// @addtogroup debug
/// @{

namespace astra2d {

class Application;
class OrthographicCamera;
class Scene;
class SceneSerializer;

/// An in-game debug window drawn with Dear ImGui, hidden until shown (the examples use F1).
///
/// Its "Debug" window has:
/// - **Frame**: a graph of the last frame times with the 60 and 30 FPS budgets, FPS, mean / min /
///   max frame time, a bar of where the frame's time goes (update, render, ...), draw calls and
///   quads;
/// - **Time**: pause, single steps and slow motion for the game (Application::setTimeScale());
/// - **Profiler**: the call tree of Profiler::global() with average, self and peak times. The
///   profiler records only while the overlay is visible;
/// - **Toggles**: the game's debug switches (addToggle()), e.g. the physics debug view;
/// - the game's own sections (addPanel()).
///
/// With a scene (setScene()), an "Entities" window lists its entities and inspects the selected
/// one (see EntityInspector); drawSelection() outlines that entity in the game. With the scene's
/// camera (setCamera()), *Pick* selects an entity by clicking it in the game and *Move* drags
/// entities with the mouse; with a serializer (setSceneFile()), *Save scene* writes the scene to a
/// file.
///
/// @code
/// astra2d::DebugOverlay m_overlay{*this}; // a member of the Application
///
/// void onStart() override
/// {
///     m_overlay.setScene(&m_scene);
///     m_overlay.addToggle("Physics debug view (F3)", &m_showPhysics);
/// }
/// void onUpdate(float dt) override
/// {
///     if (input().isPressed(astra2d::Key::F1)) { m_overlay.toggle(); }
/// }
/// void onRender() override
/// {
///     m_renderer.begin(m_camera);
///     ... // draw the world
///     m_overlay.drawSelection(m_renderer, 1.0F);
///     m_renderer.end();
///     m_overlay.addFrame(m_renderer.stats());
///     m_renderer.resetStats();
///     m_overlay.draw(); // last: on top of everything
/// }
/// @endcode
class DebugOverlay {
public:
    /// Starts ImGui on @p app's window (see ImGuiLayer); the time controls act on @p app, which
    /// must outlive the overlay. The overlay starts hidden, or shown if the environment variable
    /// `ASTRA2D_DEBUG_OVERLAY` is `1` (e.g. for screenshots and smoke tests).
    /// @throws std::runtime_error if ImGui cannot be started.
    explicit DebugOverlay(Application& app);
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
    /// The camera the scene is seen through, for picking and moving entities with the mouse (the
    /// "Pick" and "Move" buttons in the "Entities" window); nullptr (the default) hides the
    /// buttons. It must outlive the overlay or be replaced first.
    void setCamera(const OrthographicCamera* camera);
    /// Adds a "Save scene" button to the "Entities" window that writes the scene to @p path with
    /// @p serializer (SceneSerializer::saveFile()), which has the game's components registered and
    /// must outlive the overlay; nullptr removes the button. Loading is left to the game, which
    /// knows what has to happen around it.
    void setSceneFile(const SceneSerializer* serializer, std::filesystem::path path);
    /// The entity list and inspector, e.g. to add inspectors for the game's components.
    [[nodiscard]] EntityInspector& inspector() { return m_inspector; }

    /// Adds a checkbox called @p label for @p value, which must outlive the overlay.
    void addToggle(std::string label, bool* value);
    /// Adds a section called @p title to the "Debug" window; @p draw adds its ImGui widgets
    /// (`#include <imgui.h>`) each frame while the section is open.
    void addPanel(std::string title, std::function<void()> draw);

    /// Records the current frame, which drew @p render, for the frame section; its time is the
    /// real Application::frameTime(). Call once per frame, also while hidden, so the numbers are
    /// current when it is shown.
    void addFrame(const RenderStats& render = {});

    /// Outlines the entity selected in the "Entities" window (its sprite, else its collider) with
    /// lines @p lineWidth world units thick, and while picking or moving, faintly, the entity
    /// under the cursor. Call between Renderer2D::begin() and end() with the camera the scene is
    /// seen through. Draws nothing while the overlay is hidden or nothing is selected.
    void drawSelection(Renderer2D& renderer, float lineWidth = 1.0F) const;

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
    void drawTimeSection();
    void drawProfilerSection();
    void drawEntitiesWindow();
    // What a click in the game does while the overlay is shown.
    enum class MouseTool {
        None, // the game gets it
        Pick, // selects the entity under it, then back to None
        Move, // selects the entity under it and drags it, until right click
    };

    void drawMouseTools();
    void drawSaveButton();
    void setMouseTool(MouseTool tool);
    void updateMouseTool();

    Application* m_app;
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
    float m_runningScale = 1.0F; // the time scale "Pause" returns to
    const OrthographicCamera* m_camera = nullptr;
    MouseTool m_tool = MouseTool::None;
    Entity m_hovered;             // under the cursor while a tool is on
    Entity m_dragged;             // held by the mouse in MouseTool::Move
    glm::vec2 m_grabOffset{0.0F}; // from the cursor to the dragged entity, in world units
    const SceneSerializer* m_serializer = nullptr;
    std::filesystem::path m_sceneFile;
    std::string m_saveStatus;      // the outcome of the last save
    glm::vec2 m_displaySize{0.0F}; // as of the last frame, to notice resizes
};

} // namespace astra2d

/// @}
