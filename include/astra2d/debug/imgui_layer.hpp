#pragma once

#include <memory>

struct ImGuiContext;

/// @addtogroup debug
/// @{

namespace astra2d {

class Window;

namespace detail {
struct ImGuiForwarding;
}

/// Dear ImGui on a Window: owns an ImGui context with the GLFW and OpenGL 3 backends.
///
/// DebugOverlay uses one; a tool that wants ImGui without the overlay can use it directly. Between
/// beginFrame() and endFrame() any ImGui call works (`#include <imgui.h>`); endFrame() draws the
/// result on top of whatever was drawn before, so call it last in Application::onRender():
///
/// @code
/// m_imgui.beginFrame();
/// ImGui::Begin("Tuning");
/// ImGui::SliderFloat("Jump speed", &m_jumpSpeed, 5.0F, 20.0F);
/// ImGui::End();
/// m_imgui.endFrame();
/// @endcode
///
/// Every input event reaches ImGui first. Presses reach the Window's Input only while ImGui does
/// not want that device: a click on an ImGui window, the scroll wheel over it and keys typed into
/// a text field do not reach the game (a release does if its press did). wantsMouse() and
/// wantsKeyboard() tell when that is the case. No `imgui.ini` file is written. Create it after
/// the Window and destroy it before.
class ImGuiLayer {
public:
    /// Creates the ImGui context and starts its backends on @p window (making its OpenGL context
    /// current).
    /// @throws std::runtime_error if a backend cannot be started.
    explicit ImGuiLayer(Window& window);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;
    ImGuiLayer(ImGuiLayer&&) = delete;
    ImGuiLayer& operator=(ImGuiLayer&&) = delete;

    /// Makes this layer's context current and starts an ImGui frame. Call once per frame.
    void beginFrame();
    /// Ends the ImGui frame and draws it into the current framebuffer.
    void endFrame();

    /// True when ImGui uses the mouse (the cursor is over one of its windows, or a drag that
    /// started there goes on), as of the last frame, or while setCaptureMouse() is on.
    [[nodiscard]] bool wantsMouse() const;
    /// While @p capture is true, mouse presses and the scroll wheel reach only ImGui, wherever the
    /// cursor is; e.g. so a click in the game picks an entity instead of acting in the game.
    /// Cursor movement still reaches the game.
    void setCaptureMouse(bool capture);
    /// True when ImGui uses the keyboard (a text field is active), as of the last frame.
    [[nodiscard]] bool wantsKeyboard() const;

    /// The ImGui context, for ImGui::SetCurrentContext() when several are used.
    [[nodiscard]] ImGuiContext* context() const { return m_context; }

private:
    Window* m_window;
    std::unique_ptr<detail::ImGuiForwarding> m_forwarding; // the input callbacks
    ImGuiContext* m_context = nullptr;
};

} // namespace astra2d

/// @}
