#pragma once

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <span>

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

/// Gamepad buttons in the standard (Xbox-style) layout. Values match GLFW gamepad button codes.
enum class GamepadButton : int {
    A = 0,           ///< Bottom face button (Cross on PlayStation).
    B = 1,           ///< Right face button (Circle).
    X = 2,           ///< Left face button (Square).
    Y = 3,           ///< Top face button (Triangle).
    LeftBumper = 4,  ///< L1.
    RightBumper = 5, ///< R1.
    Back = 6,        ///< Select / Share.
    Start = 7,       ///< Start / Options.
    Guide = 8,       ///< Xbox / PS button.
    LeftThumb = 9,   ///< Left stick pressed.
    RightThumb = 10, ///< Right stick pressed.
    DpadUp = 11,     ///< D-pad up.
    DpadRight = 12,  ///< D-pad right.
    DpadDown = 13,   ///< D-pad down.
    DpadLeft = 14,   ///< D-pad left.
};

/// Gamepad analog axes. Values match GLFW gamepad axis codes.
enum class GamepadAxis : int {
    LeftX = 0,        ///< Left stick, -1 (left) to 1 (right).
    LeftY = 1,        ///< Left stick, -1 (down) to 1 (up).
    RightX = 2,       ///< Right stick, -1 (left) to 1 (right).
    RightY = 3,       ///< Right stick, -1 (down) to 1 (up).
    LeftTrigger = 4,  ///< 0 (released) to 1 (fully pressed).
    RightTrigger = 5, ///< 0 (released) to 1 (fully pressed).
};

/// Per-frame keyboard, mouse and gamepad state.
///
/// For every key, mouse button and gamepad button three queries are available:
/// - isPressed(): went down during the current frame,
/// - isHeld(): is currently down,
/// - isReleased(): went up during the current frame.
///
/// Pressed/released are tracked from events rather than by comparing two snapshots, so a tap that
/// is pressed and released between two frames still reports both isPressed() and isReleased().
///
/// Gamepads are polled once per frame (GLFW reports them as state, not as events), so for them a
/// press and release between two frames is not seen. Up to kMaxGamepads gamepads with a known
/// mapping (GLFW's gamepad database) are supported; stick Y axes are flipped so that up is
/// positive, matching the y-up world. Stick input inside deadZone() is ignored.
///
/// The class does not depend on GLFW: the Window forwards events to the on*() methods and calls
/// beginFrame() before polling events. This keeps it unit-testable.
///
/// Pressed/released work in both of the Application's update hooks. onUpdate() sees what happened
/// during the current frame. onFixedUpdate() runs zero or several times per frame, so while a
/// fixed step runs (between beginFixedStep() and endFixedStep(), which the Application calls) they
/// report what happened since the previous fixed step instead: a press is seen by exactly one fixed
/// step, also when a frame runs no step or two.
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

    /// Number of gamepads that can be used at once.
    static constexpr int kMaxGamepads = 4;
    /// Number of GamepadButton values.
    static constexpr int kGamepadButtonCount = static_cast<int>(GamepadButton::DpadLeft) + 1;
    /// Number of GamepadAxis values.
    static constexpr int kGamepadAxisCount = static_cast<int>(GamepadAxis::RightTrigger) + 1;

    /// True if gamepad @p gamepad (0 to kMaxGamepads - 1) is connected.
    [[nodiscard]] bool isGamepadConnected(int gamepad = 0) const;
    /// True if @p button of @p gamepad went down this frame.
    [[nodiscard]] bool isPressed(GamepadButton button, int gamepad = 0) const;
    /// True while @p button of @p gamepad is down.
    [[nodiscard]] bool isHeld(GamepadButton button, int gamepad = 0) const;
    /// True if @p button of @p gamepad went up this frame.
    [[nodiscard]] bool isReleased(GamepadButton button, int gamepad = 0) const;
    /// Value of @p axis of @p gamepad (see GamepadAxis for the ranges); 0 if not connected.
    /// Stick axes have the dead zone applied (radially, per stick).
    [[nodiscard]] float axis(GamepadAxis axis, int gamepad = 0) const;
    /// Left stick as a vector, dead zone applied; length at most 1.
    [[nodiscard]] glm::vec2 leftStick(int gamepad = 0) const;
    /// Right stick as a vector, dead zone applied; length at most 1.
    [[nodiscard]] glm::vec2 rightStick(int gamepad = 0) const;

    /// Sets the stick dead zone in [0, 1). Deflections shorter than this read as 0, and the rest
    /// of the range is rescaled to start at 0, so small drift is ignored without a jump.
    void setDeadZone(float deadZone);
    /// Stick dead zone (default 0.15).
    [[nodiscard]] float deadZone() const { return m_deadZone; }

    /// @name Event interface (called by the platform layer)
    /// @{

    /// Starts a new frame: clears this frame's pressed/released flags and the scroll delta. Called
    /// by Window::pollEvents().
    void beginFrame();

    /// Starts a fixed step: until endFixedStep(), pressed/released report what happened since the
    /// previous fixed step. Called by Application around onFixedUpdate().
    void beginFixedStep() { m_inFixedStep = true; }
    /// Ends a fixed step: clears the fixed-step pressed/released flags.
    void endFixedStep();
    /// True between beginFixedStep() and endFixedStep().
    [[nodiscard]] bool inFixedStep() const { return m_inFixedStep; }
    /// Forgets presses and releases that no fixed step has seen yet, such as the key that closed
    /// a menu in onUpdate(), so it does not also act in the game's next fixed step (like Unity's
    /// Input.ResetInputAxes()). Held state is kept. Reach it through Application::window().input().
    void clearFixedStepPresses() { keepOnly(allBut(bits(kFixedPressed, kFixedReleased))); }

    /// Records a key event. @p key is a GLFW key code; unknown codes are ignored.
    void onKey(int key, bool down);
    /// Records a mouse button event. @p button is a GLFW button code; unknown codes are ignored.
    void onMouseButton(int button, bool down);
    /// Records the new cursor position in window coordinates.
    void onMouseMove(glm::vec2 position);
    /// Adds a scroll wheel offset to this frame's scrollDelta().
    void onScroll(glm::vec2 offset);

    /// Records the polled state of a connected gamepad. @p buttons and @p axes are in GLFW order;
    /// stick Y axes as GLFW reports them (down positive), triggers from -1 to 1. Out-of-range
    /// gamepad indices are ignored.
    void onGamepadState(int gamepad, std::span<const bool, kGamepadButtonCount> buttons,
                        std::span<const float, kGamepadAxisCount> axes);
    /// Records that a gamepad is not connected: its buttons are released and axes reset.
    void onGamepadDisconnected(int gamepad);

    /// Releases all held keys and buttons (e.g. when the window loses focus). Keys still down when
    /// focus returns are not reported as held until they are pressed again.
    void releaseAll();

    /// @}

private:
    // The last enumerators equal GLFW_KEY_LAST and GLFW_MOUSE_BUTTON_LAST (checked in window.cpp).
    static constexpr int kKeyCount = static_cast<int>(Key::Menu) + 1;
    static constexpr int kMouseButtonCount = static_cast<int>(MouseButton::Button8) + 1;

    // Combines state bits as unsigned values (std::uint8_t operands would become signed int).
    template <typename... Bits>
    static constexpr std::uint8_t bits(Bits... values)
    {
        return static_cast<std::uint8_t>((0U | ... | static_cast<unsigned>(values)));
    }
    // Every bit except those in @p values.
    static constexpr std::uint8_t allBut(std::uint8_t values)
    {
        return static_cast<std::uint8_t>(~static_cast<unsigned>(values));
    }

    // Per key/button state bits.
    static constexpr std::uint8_t kDown = 1U << 0U;
    static constexpr std::uint8_t kPressed = 1U << 1U;
    static constexpr std::uint8_t kReleased = 1U << 2U;
    // The same since the last fixed step, kept across frames until endFixedStep().
    static constexpr std::uint8_t kFixedPressed = 1U << 3U;
    static constexpr std::uint8_t kFixedReleased = 1U << 4U;
    // kDown, kFixedPressed and kFixedReleased (bits() cannot be used here: the class is not
    // complete yet).
    static constexpr std::uint8_t kKeptByFrame = (1U << 0U) | (1U << 3U) | (1U << 4U);

    static void apply(std::uint8_t& state, bool down);
    void keepOnly(std::uint8_t bits);
    [[nodiscard]] std::uint8_t pressedBit() const
    {
        return m_inFixedStep ? kFixedPressed : kPressed;
    }
    [[nodiscard]] std::uint8_t releasedBit() const
    {
        return m_inFixedStep ? kFixedReleased : kReleased;
    }

    struct Gamepad {
        bool connected = false;
        std::array<std::uint8_t, kGamepadButtonCount> buttons{};
        std::array<float, kGamepadAxisCount> axes{}; // engine convention: y up, triggers 0..1
    };

    [[nodiscard]] const Gamepad* gamepadAt(int gamepad) const;
    [[nodiscard]] glm::vec2 stick(int gamepad, GamepadAxis xAxis, GamepadAxis yAxis) const;

    std::array<std::uint8_t, kKeyCount> m_keys{};
    std::array<std::uint8_t, kMouseButtonCount> m_mouseButtons{};
    std::array<Gamepad, kMaxGamepads> m_gamepads{};
    float m_deadZone = 0.15F;
    glm::vec2 m_mousePosition{0.0F};
    glm::vec2 m_previousMousePosition{0.0F};
    glm::vec2 m_scrollDelta{0.0F};
    bool m_inFixedStep = false;
};

} // namespace astra2d

/// @}
