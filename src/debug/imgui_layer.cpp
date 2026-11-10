#include "astra2d/debug/imgui_layer.hpp"

#include "astra2d/core/window.hpp"
#include "input_gate.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h> // ImGui::GetIO(context), for callbacks of a context that is not current

#include <algorithm>
#include <memory>
#include <utility>
#include <stdexcept>
#include <vector>

namespace astra2d {

// ---------------------------------------------------------------------------------------------
// Input forwarding
//
// ImGui's GLFW backend could install its callbacks and call the Window's from them, but then the
// game would also get every click on an ImGui window and every key typed into a text field. So the
// layer installs its own: each event goes to ImGui, and on to the Window (and Input) as an
// InputGate decides.

struct detail::ImGuiForwarding {
    GLFWwindow* window = nullptr;
    ImGuiContext* context = nullptr;
    // The Window's callbacks, called after ImGui's.
    GLFWwindowfocusfun focus = nullptr;
    GLFWcursorenterfun enter = nullptr;
    GLFWcursorposfun cursor = nullptr;
    GLFWmousebuttonfun button = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    detail::InputGate<GLFW_KEY_LAST + 1> keys;
    detail::InputGate<GLFW_MOUSE_BUTTON_LAST + 1> buttons;

    [[nodiscard]] const ImGuiIO& io() const { return ImGui::GetIO(context); }
};

namespace {

// GLFW callbacks are plain functions, and the window's user pointer belongs to Window, so the
// layers are found by window here. Main thread only, like GLFW.
std::vector<detail::ImGuiForwarding*> g_forwarding; // NOLINT(*-avoid-non-const-global-variables)

detail::ImGuiForwarding& forwardingOf(GLFWwindow* window)
{
    const auto found = std::ranges::find(g_forwarding, window, &detail::ImGuiForwarding::window);
    return **found; // installed for every window that has these callbacks
}

// Whether a key or button event reaches the Window; see InputGate.
template <std::size_t N>
bool passOn(detail::InputGate<N>& gate, int code, int action, bool imguiWants)
{
    switch (action) {
    case GLFW_PRESS:
        return gate.press(code, imguiWants);
    case GLFW_RELEASE:
        return gate.release(code, imguiWants);
    default:
        return gate.repeat(code, imguiWants);
    }
}

void onFocus(GLFWwindow* window, int focused)
{
    ImGui_ImplGlfw_WindowFocusCallback(window, focused);
    const detail::ImGuiForwarding& f = forwardingOf(window);
    if (f.focus != nullptr) {
        f.focus(window, focused);
    }
}

void onCursorEnter(GLFWwindow* window, int entered)
{
    ImGui_ImplGlfw_CursorEnterCallback(window, entered);
    const detail::ImGuiForwarding& f = forwardingOf(window);
    if (f.enter != nullptr) {
        f.enter(window, entered);
    }
}

void onCursorPos(GLFWwindow* window, double x, double y)
{
    ImGui_ImplGlfw_CursorPosCallback(window, x, y);
    const detail::ImGuiForwarding& f = forwardingOf(window);
    if (f.cursor != nullptr) {
        f.cursor(window, x, y);
    }
}

void onMouseButton(GLFWwindow* window, int button, int action, int mods)
{
    detail::ImGuiForwarding& f = forwardingOf(window);
    const bool imguiWants = f.io().WantCaptureMouse; // decided before ImGui sees this click
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
    if (passOn(f.buttons, button, action, imguiWants) && f.button != nullptr) {
        f.button(window, button, action, mods);
    }
}

void onScroll(GLFWwindow* window, double x, double y)
{
    ImGui_ImplGlfw_ScrollCallback(window, x, y);
    const detail::ImGuiForwarding& f = forwardingOf(window);
    if (!f.io().WantCaptureMouse && f.scroll != nullptr) {
        f.scroll(window, x, y);
    }
}

void onKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    detail::ImGuiForwarding& f = forwardingOf(window);
    const bool imguiWants = f.io().WantCaptureKeyboard;
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    if (passOn(f.keys, key, action, imguiWants) && f.key != nullptr) {
        f.key(window, key, scancode, action, mods);
    }
}

void onChar(GLFWwindow* window, unsigned int character)
{
    ImGui_ImplGlfw_CharCallback(window, character);
    const detail::ImGuiForwarding& f = forwardingOf(window);
    if (!f.io().WantCaptureKeyboard && f.character != nullptr) {
        f.character(window, character);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// ImGuiLayer

ImGuiLayer::ImGuiLayer(Window& window)
    : m_window(&window)
    , m_forwarding(std::make_unique<detail::ImGuiForwarding>())
    , m_context(ImGui::CreateContext())
{
    IMGUI_CHECKVERSION();
    m_window->makeContextCurrent();
    ImGui::SetCurrentContext(m_context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not saved next to the game
    ImGui::StyleColorsDark();

    GLFWwindow* handle = window.nativeHandle();
    if (!ImGui_ImplGlfw_InitForOpenGL(handle, false)) { // callbacks: installed below
        ImGui::DestroyContext(m_context);
        throw std::runtime_error("Failed to start the ImGui GLFW backend");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(m_context);
        throw std::runtime_error("Failed to start the ImGui OpenGL 3 backend");
    }

    detail::ImGuiForwarding& f = *m_forwarding;
    f.window = handle;
    f.context = m_context;
    g_forwarding.push_back(&f);
    f.focus = glfwSetWindowFocusCallback(handle, onFocus);
    f.enter = glfwSetCursorEnterCallback(handle, onCursorEnter);
    f.cursor = glfwSetCursorPosCallback(handle, onCursorPos);
    f.button = glfwSetMouseButtonCallback(handle, onMouseButton);
    f.scroll = glfwSetScrollCallback(handle, onScroll);
    f.key = glfwSetKeyCallback(handle, onKey);
    f.character = glfwSetCharCallback(handle, onChar);
}

ImGuiLayer::~ImGuiLayer()
{
    const detail::ImGuiForwarding& f = *m_forwarding;
    GLFWwindow* handle = f.window;
    // The Window gets its own callbacks back.
    glfwSetWindowFocusCallback(handle, f.focus);
    glfwSetCursorEnterCallback(handle, f.enter);
    glfwSetCursorPosCallback(handle, f.cursor);
    glfwSetMouseButtonCallback(handle, f.button);
    glfwSetScrollCallback(handle, f.scroll);
    glfwSetKeyCallback(handle, f.key);
    glfwSetCharCallback(handle, f.character);
    std::erase(g_forwarding, m_forwarding.get());

    m_window->makeContextCurrent(); // the backend deletes its OpenGL objects
    ImGui::SetCurrentContext(m_context);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(m_context);
}

void ImGuiLayer::beginFrame()
{
    ImGui::SetCurrentContext(m_context);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame()
{
    ImGui::SetCurrentContext(m_context);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::wantsMouse() const
{
    return m_forwarding->io().WantCaptureMouse;
}

bool ImGuiLayer::wantsKeyboard() const
{
    return m_forwarding->io().WantCaptureKeyboard;
}

} // namespace astra2d
