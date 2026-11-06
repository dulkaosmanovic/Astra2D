#include "astra2d/core/actions.hpp"

#include "astra2d/core/assert.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <variant>

namespace astra2d {

namespace {

// Calls the Input query that matches the button's type.
template <typename Query>
bool query(const ActionMap::Button& button, int gamepad, const Query& ask)
{
    return std::visit([&](auto value) {
        if constexpr (std::is_same_v<decltype(value), GamepadButton>) {
            return ask(value, gamepad);
        } else {
            return ask(value);
        }
    }, button);
}

} // namespace

ActionMap::ActionMap(int gamepad)
    : m_gamepad(gamepad)
{
    ASTRA_ASSERT(gamepad >= 0 && gamepad < Input::kMaxGamepads, "Gamepad index {} out of range",
                 gamepad);
}

ActionMap& ActionMap::bind(std::string_view action, Button button)
{
    m_actions[std::string(action)].buttons.push_back(button);
    return *this;
}

ActionMap& ActionMap::bindAxis(std::string_view axis, Button negative, Button positive)
{
    m_actions[std::string(axis)].axes.emplace_back(
        ButtonPair{.negative = negative, .positive = positive});
    return *this;
}

ActionMap& ActionMap::bindAxis(std::string_view axis, GamepadAxis gamepadAxis, float scale)
{
    m_actions[std::string(axis)].axes.emplace_back(AnalogAxis{.axis = gamepadAxis, .scale = scale});
    return *this;
}

void ActionMap::unbind(std::string_view action)
{
    if (const auto it = m_actions.find(action); it != m_actions.end()) {
        m_actions.erase(it);
    }
}

void ActionMap::clear()
{
    m_actions.clear();
}

bool ActionMap::has(std::string_view action) const
{
    return m_actions.contains(action);
}

const ActionMap::Action* ActionMap::find(std::string_view action) const
{
    const auto it = m_actions.find(action);
    if (it == m_actions.end()) {
        ASTRA_ASSERT(false, "Unknown action '{}' (not bound; a typo?)", action);
        return nullptr;
    }
    return &it->second;
}

bool ActionMap::held(const Button& button, const Input& input) const
{
    return query(button, m_gamepad, [&](auto... args) { return input.isHeld(args...); });
}

bool ActionMap::pressed(const Button& button, const Input& input) const
{
    return query(button, m_gamepad, [&](auto... args) { return input.isPressed(args...); });
}

bool ActionMap::released(const Button& button, const Input& input) const
{
    return query(button, m_gamepad, [&](auto... args) { return input.isReleased(args...); });
}

bool ActionMap::isHeld(std::string_view action, const Input& input) const
{
    const Action* bound = find(action);
    return bound != nullptr &&
           std::ranges::any_of(bound->buttons, [&](const Button& b) { return held(b, input); });
}

bool ActionMap::isPressed(std::string_view action, const Input& input) const
{
    const Action* bound = find(action);
    if (bound == nullptr) {
        return false;
    }
    // Pressed this frame, unless another binding was already held before (then the action was
    // active already): every binding that is held now must have been pressed this frame.
    bool anyPressed = false;
    for (const Button& button : bound->buttons) {
        const bool wentDown = pressed(button, input);
        anyPressed = anyPressed || wentDown;
        if (held(button, input) && !wentDown) {
            return false;
        }
    }
    return anyPressed;
}

bool ActionMap::isReleased(std::string_view action, const Input& input) const
{
    const Action* bound = find(action);
    if (bound == nullptr) {
        return false;
    }
    // Released this frame, and no other binding keeps the action held.
    bool anyReleased = false;
    for (const Button& button : bound->buttons) {
        if (held(button, input)) {
            return false;
        }
        anyReleased = anyReleased || released(button, input);
    }
    return anyReleased;
}

float ActionMap::axis(std::string_view axis, const Input& input) const
{
    const Action* bound = find(axis);
    if (bound == nullptr) {
        return 0.0F;
    }
    float strongest = 0.0F;
    for (const auto& binding : bound->axes) {
        float value = 0.0F;
        if (const auto* pair = std::get_if<ButtonPair>(&binding)) {
            value = (held(pair->positive, input) ? 1.0F : 0.0F) -
                    (held(pair->negative, input) ? 1.0F : 0.0F);
        } else {
            const auto& analog = std::get<AnalogAxis>(binding);
            value = input.axis(analog.axis, m_gamepad) * analog.scale;
        }
        if (std::abs(value) > std::abs(strongest)) {
            strongest = value;
        }
    }
    return std::clamp(strongest, -1.0F, 1.0F);
}

} // namespace astra2d
