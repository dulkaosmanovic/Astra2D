#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>

/// @addtogroup core
/// @{

namespace astra2d {

/// Keyboard keys. Values match GLFW key codes, so no translation table is needed.
// clang-format off: consecutive codes are kept on one line.
enum class Key : int {
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,
    World1 = 161, ///< Non-US key #1.
    World2 = 162, ///< Non-US key #2.
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,
    Keypad0 = 320, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal = 330,
    KeypadDivide = 331,
    KeypadMultiply = 332,
    KeypadSubtract = 333,
    KeypadAdd = 334,
    KeypadEnter = 335,
    KeypadEqual = 336,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,
};
// clang-format on

/// Mouse buttons. Values match GLFW mouse button codes.
enum class MouseButton : int {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4,
    Button6 = 5,
    Button7 = 6,
    Button8 = 7,
};

/// Per-frame keyboard and mouse state.
///
/// For every key and mouse button three queries are available:
/// - isPressed(): went down during the current frame,
/// - isHeld(): is currently down,
/// - isReleased(): went up during the current frame.
///
/// Pressed/released are tracked from events rather than by comparing two snapshots, so a tap that
/// is pressed and released between two frames still reports both isPressed() and isReleased().
///
/// The class does not depend on GLFW: the Window forwards events to the on*() methods and calls
/// beginFrame() before polling events. This keeps it unit-testable.
///
/// @note Pressed/released last one *frame*, not one fixed step. Read them in
///       Application::onUpdate(), not in Application::onFixedUpdate(), which may run zero or
///       several times per frame.
class Input {
public:
    /// True if @p key went down during the current frame.
    [[nodiscard]] bool isPressed(Key key) const;
    /// True while @p key is down.
    [[nodiscard]] bool isHeld(Key key) const;
    /// True if @p key went up during the current frame.
    [[nodiscard]] bool isReleased(Key key) const;

    /// True if @p button went down during the current frame.
    [[nodiscard]] bool isPressed(MouseButton button) const;
    /// True while @p button is down.
    [[nodiscard]] bool isHeld(MouseButton button) const;
    /// True if @p button went up during the current frame.
    [[nodiscard]] bool isReleased(MouseButton button) const;

    /// Cursor position in framebuffer pixels (origin top-left), the same units as the viewport and
    /// Application::onResize(), also on HiDPI displays.
    [[nodiscard]] glm::vec2 mousePosition() const { return m_mousePosition; }

    /// Cursor movement since the previous frame.
    [[nodiscard]] glm::vec2 mouseDelta() const { return m_mousePosition - m_previousMousePosition; }

    /// Scroll wheel offset accumulated during the current frame.
    [[nodiscard]] glm::vec2 scrollDelta() const { return m_scrollDelta; }

    /// @name Event interface (called by the platform layer)
    /// @{

    /// Starts a new frame: clears pressed/released flags and the scroll delta. Called by
    /// Window::pollEvents().
    void beginFrame();

    /// Records a key event. @p key is a GLFW key code; unknown codes are ignored.
    void onKey(int key, bool down);
    /// Records a mouse button event. @p button is a GLFW button code; unknown codes are ignored.
    void onMouseButton(int button, bool down);
    /// Records the new cursor position in window coordinates.
    void onMouseMove(glm::vec2 position);
    /// Adds a scroll wheel offset to this frame's scrollDelta().
    void onScroll(glm::vec2 offset);

    /// Releases all held keys and buttons (e.g. when the window loses focus). Keys still down when
    /// focus returns are not reported as held until they are pressed again.
    void releaseAll();

    /// @}

private:
    // The last enumerators equal GLFW_KEY_LAST and GLFW_MOUSE_BUTTON_LAST (checked in window.cpp).
    static constexpr int kKeyCount = static_cast<int>(Key::Menu) + 1;
    static constexpr int kMouseButtonCount = static_cast<int>(MouseButton::Button8) + 1;

    // Per key/button state bits.
    static constexpr std::uint8_t kDown = 1U << 0U;
    static constexpr std::uint8_t kPressed = 1U << 1U;
    static constexpr std::uint8_t kReleased = 1U << 2U;

    static void apply(std::uint8_t& state, bool down);

    std::array<std::uint8_t, kKeyCount> m_keys{};
    std::array<std::uint8_t, kMouseButtonCount> m_mouseButtons{};
    glm::vec2 m_mousePosition{0.0F};
    glm::vec2 m_previousMousePosition{0.0F};
    glm::vec2 m_scrollDelta{0.0F};
};

} // namespace astra2d

/// @}
