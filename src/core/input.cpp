#include "astra2d/core/input.hpp"

namespace astra2d {

namespace {

template <typename Array>
bool hasBit(const Array& states, int index, std::uint8_t bit)
{
    if (index < 0 || index >= static_cast<int>(states.size())) {
        return false;
    }
    return (states[static_cast<std::size_t>(index)] & bit) != 0;
}

} // namespace

bool Input::isPressed(Key key) const { return hasBit(m_keys, static_cast<int>(key), Pressed); }
bool Input::isHeld(Key key) const { return hasBit(m_keys, static_cast<int>(key), Down); }
bool Input::isReleased(Key key) const { return hasBit(m_keys, static_cast<int>(key), Released); }

bool Input::isPressed(MouseButton button) const
{
    return hasBit(m_mouseButtons, static_cast<int>(button), Pressed);
}

bool Input::isHeld(MouseButton button) const
{
    return hasBit(m_mouseButtons, static_cast<int>(button), Down);
}

bool Input::isReleased(MouseButton button) const
{
    return hasBit(m_mouseButtons, static_cast<int>(button), Released);
}

void Input::beginFrame()
{
    constexpr auto kKeepDown = static_cast<std::uint8_t>(Down);
    for (auto& state : m_keys) {
        state &= kKeepDown;
    }
    for (auto& state : m_mouseButtons) {
        state &= kKeepDown;
    }
    m_previousMousePosition = m_mousePosition;
    m_scrollDelta = glm::vec2(0.0F);
}

void Input::apply(std::uint8_t& state, bool down)
{
    const bool wasDown = (state & Down) != 0;
    if (down && !wasDown) {
        state |= Down | Pressed;
    } else if (!down && wasDown) {
        state = static_cast<std::uint8_t>((state & ~Down) | Released);
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
