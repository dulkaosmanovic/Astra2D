#pragma once

#include "astra2d/core/input.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

/// @addtogroup core
/// @{

namespace astra2d {

/// Maps game actions ("jump", "move_x") to keys, mouse buttons and gamepad input.
///
/// Game code asks about actions instead of physical inputs, so controls can be changed (or offered
/// on keyboard and gamepad at once) without touching the game logic:
///
/// @code
/// ActionMap actions;
/// actions.bind("jump", Key::Space).bind("jump", GamepadButton::A);
/// actions.bindAxis("move_x", Key::A, Key::D)
///        .bindAxis("move_x", Key::Left, Key::Right)
///        .bindAxis("move_x", GamepadAxis::LeftX);
///
/// if (actions.isPressed("jump", input())) { ... }
/// velocity.x = actions.axis("move_x", input()) * speed;
/// @endcode
///
/// - **Button actions** are held while *any* of their bindings is held. isPressed() is true in the
///   frame the action becomes held, isReleased() in the frame the last binding is let go, so
///   pressing a second binding while the first is held does not trigger the action again.
/// - **Axis actions** return the strongest of their bindings, in [-1, 1]: a key pair gives -1, 0
///   or 1, a gamepad axis its analog value.
/// - Gamepad bindings read the gamepad given to the constructor, so each local player can have
///   their own map.
///
/// Asking for an action that was never bound is a programming error (usually a typo): it fails an
/// assertion in debug builds and reads as "not active" in release builds.
///
/// Pure logic over Input, without GLFW, so it is unit-tested directly.
class ActionMap {
public:
    /// A physical button that can trigger a button action.
    using Button = std::variant<Key, MouseButton, GamepadButton>;

    /// @param gamepad Index of the gamepad that gamepad bindings read (0 to kMaxGamepads - 1).
    explicit ActionMap(int gamepad = 0);

    /// Adds @p button as a trigger of the button action @p action. Returns `*this` for chaining.
    ActionMap& bind(std::string_view action, Button button);

    /// Adds two buttons as an axis: @p negative gives -1, @p positive gives +1, both or neither 0.
    ActionMap& bindAxis(std::string_view axis, Button negative, Button positive);
    /// Adds a gamepad axis, multiplied by @p scale (e.g. -1 to invert it).
    ActionMap& bindAxis(std::string_view axis, GamepadAxis gamepadAxis, float scale = 1.0F);

    /// Removes all bindings of @p action (button or axis).
    void unbind(std::string_view action);
    /// Removes all bindings.
    void clear();
    /// True if @p action has at least one binding.
    [[nodiscard]] bool has(std::string_view action) const;

    /// True in the frame @p action becomes held.
    [[nodiscard]] bool isPressed(std::string_view action, const Input& input) const;
    /// True while any binding of @p action is held.
    [[nodiscard]] bool isHeld(std::string_view action, const Input& input) const;
    /// True in the frame the last held binding of @p action is released.
    [[nodiscard]] bool isReleased(std::string_view action, const Input& input) const;

    /// Value of the axis action @p axis in [-1, 1]: the binding with the largest magnitude wins.
    [[nodiscard]] float axis(std::string_view axis, const Input& input) const;

    /// The gamepad that gamepad bindings read.
    [[nodiscard]] int gamepad() const { return m_gamepad; }

private:
    struct ButtonPair {
        Button negative;
        Button positive;
    };
    struct AnalogAxis {
        GamepadAxis axis;
        float scale;
    };
    struct Action {
        std::vector<Button> buttons;
        std::vector<std::variant<ButtonPair, AnalogAxis>> axes;
    };
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    [[nodiscard]] const Action* find(std::string_view action) const;
    [[nodiscard]] bool held(const Button& button, const Input& input) const;
    [[nodiscard]] bool pressed(const Button& button, const Input& input) const;
    [[nodiscard]] bool released(const Button& button, const Input& input) const;

    std::unordered_map<std::string, Action, StringHash, std::equal_to<>> m_actions;
    int m_gamepad;
};

} // namespace astra2d

/// @}
