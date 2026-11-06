#include "astra2d/core/input.hpp"

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
