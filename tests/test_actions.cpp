#include <astra2d/core/actions.hpp>
#include <astra2d/core/assert.hpp>

#include <gtest/gtest.h>

#include <array>

using astra2d::ActionMap;
using astra2d::GamepadAxis;
using astra2d::GamepadButton;
using astra2d::Input;
using astra2d::Key;
using astra2d::MouseButton;

namespace {

void press(Input& input, Key key)
{
    input.onKey(static_cast<int>(key), true);
}
void release(Input& input, Key key)
{
    input.onKey(static_cast<int>(key), false);
}

struct Pad {
    std::array<bool, Input::kGamepadButtonCount> buttons{};
    std::array<float, Input::kGamepadAxisCount> axes{0, 0, 0, 0, -1, -1}; // triggers released

    void send(Input& input, int index = 0) const { input.onGamepadState(index, buttons, axes); }
};

} // namespace

TEST(ActionTests, AnyBindingTriggersTheAction)
{
    ActionMap actions;
    actions.bind("jump", Key::Space).bind("jump", Key::W).bind("jump", MouseButton::Right);
    Input input;
    input.beginFrame();
    press(input, Key::W);
    EXPECT_TRUE(actions.isPressed("jump", input));
    EXPECT_TRUE(actions.isHeld("jump", input));

    input.beginFrame();
    release(input, Key::W);
    input.onMouseButton(static_cast<int>(MouseButton::Right), true);
    EXPECT_TRUE(actions.isHeld("jump", input));
}

TEST(ActionTests, SecondBindingWhileHeldDoesNotPressAgain)
{
    ActionMap actions;
    actions.bind("jump", Key::Space).bind("jump", Key::W);
    Input input;
    input.beginFrame();
    press(input, Key::Space);
    input.beginFrame();
    press(input, Key::W); // Space still held
    EXPECT_FALSE(actions.isPressed("jump", input));

    input.beginFrame();
    release(input, Key::Space); // W still held
    EXPECT_FALSE(actions.isReleased("jump", input));
    EXPECT_TRUE(actions.isHeld("jump", input));

    input.beginFrame();
    release(input, Key::W);
    EXPECT_TRUE(actions.isReleased("jump", input));
    EXPECT_FALSE(actions.isHeld("jump", input));
}

TEST(ActionTests, KeyPairAxis)
{
    ActionMap actions;
    actions.bindAxis("move_x", Key::A, Key::D);
    Input input;
    input.beginFrame();
    EXPECT_FLOAT_EQ(actions.axis("move_x", input), 0.0F);
    press(input, Key::A);
    EXPECT_FLOAT_EQ(actions.axis("move_x", input), -1.0F);
    press(input, Key::D);
    EXPECT_FLOAT_EQ(actions.axis("move_x", input), 0.0F); // both cancel out
    release(input, Key::A);
    EXPECT_FLOAT_EQ(actions.axis("move_x", input), 1.0F);
}

TEST(ActionTests, StrongestAxisBindingWins)
{
    ActionMap actions;
    actions.bindAxis("move_x", Key::A, Key::D).bindAxis("move_x", GamepadAxis::LeftX);
    Input input;
    input.setDeadZone(0.0F);
    Pad pad;
    pad.axes[0] = 0.5F;
    input.beginFrame();
    pad.send(input);
    EXPECT_FLOAT_EQ(actions.axis("move_x", input), 0.5F);
    press(input, Key::A);
    EXPECT_FLOAT_EQ(actions.axis("move_x", input), -1.0F);
}

TEST(ActionTests, AnalogAxisScaleInverts)
{
    ActionMap actions;
    actions.bindAxis("look", GamepadAxis::RightX, -1.0F);
    Input input;
    input.setDeadZone(0.0F);
    Pad pad;
    pad.axes[2] = 0.75F;
    input.beginFrame();
    pad.send(input);
    EXPECT_FLOAT_EQ(actions.axis("look", input), -0.75F);
}

TEST(ActionTests, GamepadBindingsReadTheirOwnGamepad)
{
    ActionMap player1(0);
    ActionMap player2(1);
    player1.bind("jump", GamepadButton::A);
    player2.bind("jump", GamepadButton::A);
    Input input;
    Pad pressed;
    pressed.buttons[static_cast<int>(GamepadButton::A)] = true;
    input.beginFrame();
    Pad{}.send(input, 0);
    pressed.send(input, 1);
    EXPECT_FALSE(player1.isHeld("jump", input));
    EXPECT_TRUE(player2.isPressed("jump", input));
}

TEST(ActionTests, UnbindAndHas)
{
    ActionMap actions;
    actions.bind("jump", Key::Space);
    EXPECT_TRUE(actions.has("jump"));
    EXPECT_FALSE(actions.has("fly"));
    actions.unbind("jump");
    EXPECT_FALSE(actions.has("jump"));
}

TEST(ActionDeathTest, UnknownActionIsAProgrammingError)
{
#if ASTRA_ASSERTS_ENABLED
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    const ActionMap actions;
    const Input input;
    EXPECT_DEATH((void)actions.isPressed("jmup", input), "Unknown action 'jmup'");
#else
    const ActionMap actions;
    const Input input;
    EXPECT_FALSE(actions.isPressed("jmup", input));
#endif
}
