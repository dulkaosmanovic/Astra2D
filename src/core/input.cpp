#include "astra2d/core/input.hpp"

#include <algorithm>
#include <cmath>

namespace astra2d {

namespace {

template <typename Array>
bool hasBit(const Array& states, int index, std::uint8_t bit)
{
    if (index < 0 || index >= static_cast<int>(states.size())) {
        return false;
    }
    return (states[static_cast<std::size_t>(index)] & bit) != 0U;
}

} // namespace

bool Input::isPressed(Key key) const
{
    return hasBit(m_keys, static_cast<int>(key), kPressed);
}
bool Input::isHeld(Key key) const
{
    return hasBit(m_keys, static_cast<int>(key), kDown);
}
bool Input::isReleased(Key key) const
{
    return hasBit(m_keys, static_cast<int>(key), kReleased);
}

bool Input::isPressed(MouseButton button) const
{
    return hasBit(m_mouseButtons, static_cast<int>(button), kPressed);
}

bool Input::isHeld(MouseButton button) const
{
    return hasBit(m_mouseButtons, static_cast<int>(button), kDown);
}

bool Input::isReleased(MouseButton button) const
{
    return hasBit(m_mouseButtons, static_cast<int>(button), kReleased);
}

void Input::beginFrame()
{
    for (auto& state : m_keys) {
        state &= kDown;
    }
    for (auto& state : m_mouseButtons) {
        state &= kDown;
    }
    for (Gamepad& gamepad : m_gamepads) {
        for (auto& state : gamepad.buttons) {
            state &= kDown;
        }
    }
    m_previousMousePosition = m_mousePosition;
    m_scrollDelta = glm::vec2(0.0F);
}

void Input::apply(std::uint8_t& state, bool down)
{
    const bool wasDown = (state & kDown) != 0U;
    if (down && !wasDown) {
        state |= static_cast<std::uint8_t>(kDown | kPressed);
    } else if (!down && wasDown) {
        // Clear kDown, keep kPressed (a tap within one frame reports both), set kReleased.
        state &= kPressed;
        state |= kReleased;
    }
    // Repeated events with the same state (e.g. key repeat) change nothing.
}

void Input::onKey(int key, bool down)
{
    if (key >= 0 && key < kKeyCount) {
        apply(m_keys[static_cast<std::size_t>(key)], down);
    }
}

void Input::onMouseButton(int button, bool down)
{
    if (button >= 0 && button < kMouseButtonCount) {
        apply(m_mouseButtons[static_cast<std::size_t>(button)], down);
    }
}

void Input::onMouseMove(glm::vec2 position)
{
    m_mousePosition = position;
}

void Input::onScroll(glm::vec2 offset)
{
    m_scrollDelta += offset;
}

const Input::Gamepad* Input::gamepadAt(int gamepad) const
{
    if (gamepad < 0 || gamepad >= kMaxGamepads) {
        return nullptr;
    }
    return &m_gamepads[static_cast<std::size_t>(gamepad)];
}

bool Input::isGamepadConnected(int gamepad) const
{
    const Gamepad* pad = gamepadAt(gamepad);
    return pad != nullptr && pad->connected;
}

bool Input::isPressed(GamepadButton button, int gamepad) const
{
    const Gamepad* pad = gamepadAt(gamepad);
    return pad != nullptr && hasBit(pad->buttons, static_cast<int>(button), kPressed);
}

bool Input::isHeld(GamepadButton button, int gamepad) const
{
    const Gamepad* pad = gamepadAt(gamepad);
    return pad != nullptr && hasBit(pad->buttons, static_cast<int>(button), kDown);
}

bool Input::isReleased(GamepadButton button, int gamepad) const
{
    const Gamepad* pad = gamepadAt(gamepad);
    return pad != nullptr && hasBit(pad->buttons, static_cast<int>(button), kReleased);
}

glm::vec2 Input::stick(int gamepad, GamepadAxis xAxis, GamepadAxis yAxis) const
{
    const Gamepad* pad = gamepadAt(gamepad);
    if (pad == nullptr || !pad->connected) {
        return glm::vec2(0.0F);
    }
    const glm::vec2 raw{pad->axes.at(static_cast<std::size_t>(xAxis)),
                        pad->axes.at(static_cast<std::size_t>(yAxis))};
    // Radial dead zone: the stick's distance from the centre decides, not each axis on its own,
    // so diagonals are not cut off. The remaining range is rescaled to start at 0.
    const float length = std::sqrt((raw.x * raw.x) + (raw.y * raw.y));
    if (length <= m_deadZone) {
        return glm::vec2(0.0F);
    }
    const float scaled = std::min((length - m_deadZone) / (1.0F - m_deadZone), 1.0F);
    return raw * (scaled / length);
}

glm::vec2 Input::leftStick(int gamepad) const
{
    return stick(gamepad, GamepadAxis::LeftX, GamepadAxis::LeftY);
}

glm::vec2 Input::rightStick(int gamepad) const
{
    return stick(gamepad, GamepadAxis::RightX, GamepadAxis::RightY);
}

float Input::axis(GamepadAxis axis, int gamepad) const
{
    switch (axis) {
    case GamepadAxis::LeftX:
        return leftStick(gamepad).x;
    case GamepadAxis::LeftY:
        return leftStick(gamepad).y;
    case GamepadAxis::RightX:
        return rightStick(gamepad).x;
    case GamepadAxis::RightY:
        return rightStick(gamepad).y;
    case GamepadAxis::LeftTrigger:
    case GamepadAxis::RightTrigger:
        break;
    }
    const Gamepad* pad = gamepadAt(gamepad);
    if (pad == nullptr || !pad->connected) {
        return 0.0F;
    }
    return pad->axes.at(static_cast<std::size_t>(axis));
}

void Input::setDeadZone(float deadZone)
{
    m_deadZone = std::isfinite(deadZone) ? std::clamp(deadZone, 0.0F, 0.95F) : 0.0F;
}

void Input::onGamepadState(int gamepad, std::span<const bool, kGamepadButtonCount> buttons,
                           std::span<const float, kGamepadAxisCount> axes)
{
    if (gamepad < 0 || gamepad >= kMaxGamepads) {
        return;
    }
    Gamepad& pad = m_gamepads[static_cast<std::size_t>(gamepad)];
    pad.connected = true;
    // Polled state: comparing with the previous frame's Down bit gives pressed/released.
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        apply(pad.buttons[i], buttons[i]);
    }
    for (std::size_t i = 0; i < axes.size(); ++i) {
        const float value = std::isfinite(axes[i]) ? std::clamp(axes[i], -1.0F, 1.0F) : 0.0F;
        pad.axes[i] = value;
    }
    // Convert to the engine convention: sticks y up, triggers 0 (released) to 1.
    for (const auto yAxis : {GamepadAxis::LeftY, GamepadAxis::RightY}) {
        pad.axes.at(static_cast<std::size_t>(yAxis)) *= -1.0F;
    }
    for (const auto trigger : {GamepadAxis::LeftTrigger, GamepadAxis::RightTrigger}) {
        float& value = pad.axes.at(static_cast<std::size_t>(trigger));
        value = (value + 1.0F) * 0.5F;
    }
}

void Input::onGamepadDisconnected(int gamepad)
{
    if (gamepad < 0 || gamepad >= kMaxGamepads) {
        return;
    }
    Gamepad& pad = m_gamepads[static_cast<std::size_t>(gamepad)];
    for (auto& state : pad.buttons) {
        apply(state, false); // held buttons report a release
    }
    pad.axes = {};
    pad.connected = false;
}

void Input::releaseAll()
{
    for (auto& state : m_keys) {
        apply(state, false);
    }
    for (auto& state : m_mouseButtons) {
        apply(state, false);
    }
}

} // namespace astra2d
