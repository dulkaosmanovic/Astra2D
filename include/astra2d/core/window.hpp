#pragma once

#include <string>

struct GLFWwindow;

namespace astra2d {

/// Settings used when creating a Window.
struct WindowSettings {
    std::string title = "Astra2D";
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

/// Owns a GLFW window and its OpenGL 3.3 core context.
class Window {
public:
    explicit Window(const WindowSettings& settings);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// Processes pending window and input events.
    void pollEvents();

    /// Presents the back buffer.
    void swapBuffers();

    [[nodiscard]] bool shouldClose() const;
    void requestClose();

    /// Size of the framebuffer in pixels (may differ from window size on HiDPI).
    [[nodiscard]] int framebufferWidth() const;
    [[nodiscard]] int framebufferHeight() const;

    [[nodiscard]] GLFWwindow* nativeHandle() const { return m_handle; }

private:
    GLFWwindow* m_handle = nullptr;
};

} // namespace astra2d
