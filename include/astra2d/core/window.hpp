#pragma once

#include "astra2d/core/input.hpp"

#include <cstddef>

#include <glm/vec2.hpp>

#include <memory>
#include <string>

struct GLFWwindow;

namespace astra2d {
class EventDispatcher;
}

/// @addtogroup core
/// @{

namespace astra2d {

/// Settings used when creating a Window.
struct WindowSettings {
    std::string title = "Astra2D"; ///< Text in the title bar.
    int width = 1280;              ///< Initial width in screen coordinates.
    int height = 720;              ///< Initial height in screen coordinates.
    bool vsync = true;             ///< Synchronize buffer swaps with the display refresh rate.
    bool resizable = true;         ///< Allow the user to resize the window.
};

/// Owns a GLFW window and its OpenGL 3.3 core context, and forwards its events to an Input.
///
/// In debug builds of the engine (without `NDEBUG`) the context is a debug context, and OpenGL
/// errors and warnings reported by the driver are logged through the engine logger at the call
/// that caused them.
///
/// GLFW is initialized by the first Window and terminated when the last one is destroyed, so
/// several windows may exist at once. Each window has its own OpenGL context, and OpenGL calls go
/// to whichever context is *current*: creating a Window makes its context current, and
/// makeContextCurrent() switches back. OpenGL objects (textures, buffers, ...) belong to the
/// context they were created in. Like GLFW itself, Window must only be used from the main thread.
class Window {
public:
    /// Creates the window, makes its OpenGL context current and loads OpenGL functions.
    /// @throws std::runtime_error if GLFW, the window or the OpenGL 3.3 context cannot be created.
    explicit Window(const WindowSettings& settings);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// Starts a new Input frame (Input::beginFrame()), processes pending window and input events,
    /// then polls the gamepads. Call once per frame.
    void pollEvents();

    /// Blocks until at least one event arrives, then processes it. Unlike pollEvents(), it does
    /// not start a new Input frame, so events received while waiting are added to the current one.
    void waitEvents();

    /// Presents the back buffer.
    void swapBuffers();

    /// True after the user closed the window or requestClose() was called.
    [[nodiscard]] bool shouldClose() const;

    /// Marks the window to be closed; the main loop stops after the current frame.
    void requestClose();

    /// Makes this window's OpenGL context current, so that following OpenGL calls draw into it.
    /// Application::run() does this for its window before the first frame.
    void makeContextCurrent();

    /// Turns vertical sync on or off (e.g. off for benchmarks, to measure the real frame time).
    /// Applies to this window; the current context is left unchanged.
    void setVsync(bool enabled);

    /// Sets the window title (e.g. to show FPS).
    void setTitle(const std::string& title);

    /// Width of the framebuffer in pixels (may differ from the window size on HiDPI displays).
    [[nodiscard]] int framebufferWidth() const { return m_framebufferWidth; }

    /// Height of the framebuffer in pixels.
    [[nodiscard]] int framebufferHeight() const { return m_framebufferHeight; }

    /// True while the framebuffer has zero size (window minimized).
    [[nodiscard]] bool isMinimized() const
    {
        return m_framebufferWidth == 0 || m_framebufferHeight == 0;
    }

    /// Returns true once after the framebuffer size changed, then false until the next change.
    [[nodiscard]] bool consumeResize();

    /// Input state fed by this window's events.
    [[nodiscard]] Input& input() { return m_input; }

    /// @copydoc input()
    [[nodiscard]] const Input& input() const { return m_input; }

    /// Window events (WindowFocusEvent, GamepadConnectionEvent) are queued on @p events while
    /// polling. Pass nullptr to stop. Application connects its dispatcher automatically.
    void setEventDispatcher(EventDispatcher* events) { m_events = events; }

    /// The underlying GLFW window, for integrations such as ImGui. Prefer the Window API.
    [[nodiscard]] GLFWwindow* nativeHandle() const { return m_handle.get(); }

private:
    // Keeps GLFW initialized while at least one Window exists.
    class GlfwLibrary {
    public:
        GlfwLibrary();
        ~GlfwLibrary();
        GlfwLibrary(const GlfwLibrary&) = delete;
        GlfwLibrary& operator=(const GlfwLibrary&) = delete;
        GlfwLibrary(GlfwLibrary&&) = delete;
        GlfwLibrary& operator=(GlfwLibrary&&) = delete;
    };

    struct HandleDeleter {
        void operator()(GLFWwindow* handle) const noexcept;
    };

    void installCallbacks();
    void pollGamepads();
    void updateCursorScale();
    [[nodiscard]] glm::vec2 toFramebuffer(double x, double y) const;

    // Declaration order matters: the window is destroyed before GLFW is released, and if the
    // constructor throws, the members already created are cleaned up automatically.
    GlfwLibrary m_glfw;
    std::unique_ptr<GLFWwindow, HandleDeleter> m_handle;
    Input m_input;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    glm::vec2 m_cursorScale{1.0F}; // framebuffer pixels per screen coordinate (HiDPI)
    EventDispatcher* m_events = nullptr;
    bool m_resized = false;
};

} // namespace astra2d

/// @}
