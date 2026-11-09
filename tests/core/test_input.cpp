#include <astra2d/core/input.hpp>

#include <gtest/gtest.h>

#include <array>

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

} // namespace

TEST(InputTests, InitiallyNothingIsDown)
{
    const Input input;
    EXPECT_FALSE(input.isPressed(Key::Space));
    EXPECT_FALSE(input.isHeld(Key::Space));
    EXPECT_FALSE(input.isReleased(Key::Space));
    EXPECT_FALSE(input.isHeld(MouseButton::Left));
}

TEST(InputTests, PressHoldReleaseSequence)
{
    Input input;

    // Frame 1: key goes down.
    input.beginFrame();
    press(input, Key::Space);
    EXPECT_TRUE(input.isPressed(Key::Space));
    EXPECT_TRUE(input.isHeld(Key::Space));
    EXPECT_FALSE(input.isReleased(Key::Space));

    // Frame 2: still down, no new event.
    input.beginFrame();
    EXPECT_FALSE(input.isPressed(Key::Space));
    EXPECT_TRUE(input.isHeld(Key::Space));
    EXPECT_FALSE(input.isReleased(Key::Space));

    // Frame 3: key goes up.
    input.beginFrame();
    release(input, Key::Space);
    EXPECT_FALSE(input.isPressed(Key::Space));
    EXPECT_FALSE(input.isHeld(Key::Space));
    EXPECT_TRUE(input.isReleased(Key::Space));

    // Frame 4: idle again.
    input.beginFrame();
    EXPECT_FALSE(input.isPressed(Key::Space));
    EXPECT_FALSE(input.isHeld(Key::Space));
    EXPECT_FALSE(input.isReleased(Key::Space));
}

TEST(InputTests, TapWithinOneFrameReportsPressAndRelease)
{
    Input input;
    input.beginFrame();
    press(input, Key::J);
    release(input, Key::J);

    EXPECT_TRUE(input.isPressed(Key::J));
    EXPECT_TRUE(input.isReleased(Key::J));
    EXPECT_FALSE(input.isHeld(Key::J));

    input.beginFrame();
    EXPECT_FALSE(input.isPressed(Key::J));
    EXPECT_FALSE(input.isReleased(Key::J));
}

TEST(InputTests, RepeatedDownEventDoesNotRetrigger)
{
    Input input;
    input.beginFrame();
    press(input, Key::A);

    input.beginFrame();
    press(input, Key::A); // e.g. a duplicate event from the platform
    EXPECT_FALSE(input.isPressed(Key::A));
    EXPECT_TRUE(input.isHeld(Key::A));
}

TEST(InputTests, KeysAreIndependent)
{
    Input input;
    input.beginFrame();
    press(input, Key::Left);
    EXPECT_TRUE(input.isHeld(Key::Left));
    EXPECT_FALSE(input.isHeld(Key::Right));
}

TEST(InputTests, MouseButtonsTrackStateLikeKeys)
{
    Input input;
    input.beginFrame();
    input.onMouseButton(static_cast<int>(MouseButton::Right), true);
    EXPECT_TRUE(input.isPressed(MouseButton::Right));
    EXPECT_TRUE(input.isHeld(MouseButton::Right));
    EXPECT_FALSE(input.isHeld(MouseButton::Left));

    input.beginFrame();
    input.onMouseButton(static_cast<int>(MouseButton::Right), false);
    EXPECT_TRUE(input.isReleased(MouseButton::Right));
    EXPECT_FALSE(input.isHeld(MouseButton::Right));
}

TEST(InputTests, OutOfRangeCodesAreIgnored)
{
    Input input;
    input.beginFrame();
    input.onKey(-1, true); // GLFW_KEY_UNKNOWN
    input.onKey(100000, true);
    input.onMouseButton(-1, true);
    input.onMouseButton(100, true);
    // Out-of-range values are what this test checks, so the analyzer's enum range check is
    // silenced. NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid on purpose
    EXPECT_FALSE(input.isHeld(static_cast<Key>(-1)));
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid on purpose
    EXPECT_FALSE(input.isHeld(static_cast<Key>(100000)));
}

TEST(InputTests, HighestCodesAreTracked)
{
    // Key::Menu is GLFW_KEY_LAST and Button8 is GLFW_MOUSE_BUTTON_LAST: the last valid indices.
    Input input;
    input.beginFrame();
    press(input, Key::Menu);
    input.onMouseButton(static_cast<int>(MouseButton::Button8), true);
    EXPECT_TRUE(input.isPressed(Key::Menu));
    EXPECT_TRUE(input.isHeld(MouseButton::Button8));

    // One past the end is ignored.
    input.onKey(static_cast<int>(Key::Menu) + 1, true);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid on purpose
    EXPECT_FALSE(input.isHeld(static_cast<Key>(static_cast<int>(Key::Menu) + 1)));
}

TEST(InputTests, MouseDeltaIsRelativeToPreviousFrame)
{
    Input input;
    input.onMouseMove({10.0F, 20.0F});
    input.beginFrame();
    EXPECT_EQ(input.mouseDelta(), glm::vec2(0.0F));

    input.onMouseMove({15.0F, 18.0F});
    input.onMouseMove({16.0F, 25.0F});
    EXPECT_EQ(input.mousePosition(), glm::vec2(16.0F, 25.0F));
    EXPECT_EQ(input.mouseDelta(), glm::vec2(6.0F, 5.0F));

    input.beginFrame();
    EXPECT_EQ(input.mouseDelta(), glm::vec2(0.0F));
}

TEST(InputTests, ScrollAccumulatesWithinFrameAndResets)
{
    Input input;
    input.beginFrame();
    input.onScroll({0.0F, 1.0F});
    input.onScroll({0.0F, 2.0F});
    EXPECT_EQ(input.scrollDelta(), glm::vec2(0.0F, 3.0F));

    input.beginFrame();
    EXPECT_EQ(input.scrollDelta(), glm::vec2(0.0F));
}

TEST(InputTests, ReleaseAllReleasesHeldKeys)
{
    Input input;
    input.beginFrame();
    press(input, Key::D);
    input.onMouseButton(static_cast<int>(MouseButton::Left), true);

    input.beginFrame();
    input.releaseAll();
    EXPECT_FALSE(input.isHeld(Key::D));
    EXPECT_TRUE(input.isReleased(Key::D));
    EXPECT_FALSE(input.isHeld(MouseButton::Left));
    EXPECT_TRUE(input.isReleased(MouseButton::Left));
    // Keys that were never down are not reported as released.
    EXPECT_FALSE(input.isReleased(Key::W));
}

// ---------------------------------------------------------------------------------------------
// Fixed steps

namespace {

// One fixed step: what isPressed() / isReleased() report inside it.
struct StepView {
    bool pressed;
    bool released;
};
StepView step(Input& input, Key key)
{
    input.beginFixedStep();
    const StepView view{.pressed = input.isPressed(key), .released = input.isReleased(key)};
    input.endFixedStep();
    return view;
}

} // namespace

TEST(InputFixedStepTests, APressInAFrameWithoutStepsReachesTheNextStep)
{
    Input input;
    input.beginFrame();
    press(input, Key::Space);
    // No fixed step this frame (a fast display); the next frame runs one.
    input.beginFrame();
    EXPECT_FALSE(input.isPressed(Key::Space)); // the frame's own view has moved on
    EXPECT_TRUE(step(input, Key::Space).pressed);
    EXPECT_FALSE(step(input, Key::Space).pressed);
}

TEST(InputFixedStepTests, APressIsSeenByOnlyOneOfSeveralStepsInAFrame)
{
    Input input;
    input.beginFrame();
    press(input, Key::Space);
    EXPECT_TRUE(step(input, Key::Space).pressed);
    EXPECT_FALSE(step(input, Key::Space).pressed);
    // onUpdate() after the steps still sees the frame's press.
    EXPECT_TRUE(input.isPressed(Key::Space));
}

TEST(InputFixedStepTests, ATapBetweenStepsReportsPressAndRelease)
{
    Input input;
    input.beginFrame();
    press(input, Key::Space);
    input.beginFrame();
    release(input, Key::Space);
    const StepView view = step(input, Key::Space);
    EXPECT_TRUE(view.pressed);
    EXPECT_TRUE(view.released);
    EXPECT_FALSE(input.inFixedStep());
}

TEST(InputFixedStepTests, ClearingForgetsPressesNoStepHasSeen)
{
    Input input;
    input.beginFrame();
    press(input, Key::Enter); // closes a menu in onUpdate(), in a frame without fixed steps
    input.clearFixedStepPresses();
    EXPECT_TRUE(input.isPressed(Key::Enter)); // the frame still saw it
    const StepView view = step(input, Key::Enter);
    EXPECT_FALSE(view.pressed);
    EXPECT_TRUE(input.isHeld(Key::Enter));
}

TEST(InputFixedStepTests, MouseAndGamepadButtonsWorkInStepsToo)
{
    Input input;
    input.beginFrame();
    input.onMouseButton(static_cast<int>(MouseButton::Left), true);
    std::array<bool, Input::kGamepadButtonCount> buttons{};
    buttons[static_cast<std::size_t>(astra2d::GamepadButton::A)] = true;
    input.onGamepadState(0, buttons,
                         std::array<float, Input::kGamepadAxisCount>{0, 0, 0, 0, -1, -1});
    input.beginFrame();
    input.beginFixedStep();
    EXPECT_TRUE(input.isPressed(MouseButton::Left));
    EXPECT_TRUE(input.isPressed(astra2d::GamepadButton::A));
    input.endFixedStep();
    input.beginFixedStep();
    EXPECT_FALSE(input.isPressed(MouseButton::Left));
    EXPECT_TRUE(input.isHeld(MouseButton::Left));
    input.endFixedStep();
}

// ---------------------------------------------------------------------------------------------
// Gamepads

namespace {

struct PadState {
    std::array<bool, Input::kGamepadButtonCount> buttons{};
    // GLFW conventions: stick y down positive, triggers -1 (released) to 1.
    std::array<float, Input::kGamepadAxisCount> axes{0, 0, 0, 0, -1, -1};
};

void send(Input& input, const PadState& pad, int index = 0)
{
    input.onGamepadState(index, pad.buttons, pad.axes);
}

} // namespace

using astra2d::GamepadAxis;
using astra2d::GamepadButton;

TEST(GamepadTests, DisconnectedByDefault)
{
    const Input input;
    EXPECT_FALSE(input.isGamepadConnected(0));
    EXPECT_FALSE(input.isHeld(GamepadButton::A));
    EXPECT_FLOAT_EQ(input.axis(GamepadAxis::LeftX), 0.0F);
}

TEST(GamepadTests, PressedAndReleasedComeFromComparingPolls)
{
    Input input;
    PadState pad;
    input.beginFrame();
    send(input, pad);
    EXPECT_TRUE(input.isGamepadConnected(0));

    pad.buttons[static_cast<int>(GamepadButton::A)] = true;
    input.beginFrame();
    send(input, pad);
    EXPECT_TRUE(input.isPressed(GamepadButton::A));

    input.beginFrame();
    send(input, pad); // still down
    EXPECT_FALSE(input.isPressed(GamepadButton::A));
    EXPECT_TRUE(input.isHeld(GamepadButton::A));

    pad.buttons[static_cast<int>(GamepadButton::A)] = false;
    input.beginFrame();
    send(input, pad);
    EXPECT_TRUE(input.isReleased(GamepadButton::A));
}

TEST(GamepadTests, StickYIsFlippedToUpAndTriggersStartAtZero)
{
    Input input;
    input.setDeadZone(0.0F);
    PadState pad;
    pad.axes[static_cast<int>(GamepadAxis::LeftY)] = -1.0F; // GLFW: pushed up
    pad.axes[static_cast<int>(GamepadAxis::RightTrigger)] = 1.0F;
    input.beginFrame();
    send(input, pad);
    EXPECT_FLOAT_EQ(input.axis(GamepadAxis::LeftY), 1.0F);
    EXPECT_FLOAT_EQ(input.axis(GamepadAxis::LeftTrigger), 0.0F);
    EXPECT_FLOAT_EQ(input.axis(GamepadAxis::RightTrigger), 1.0F);
}

TEST(GamepadTests, RadialDeadZoneIgnoresDriftAndRescales)
{
    Input input;
    input.setDeadZone(0.2F);
    PadState pad;
    pad.axes[0] = 0.1F;
    pad.axes[1] = -0.1F; // length 0.14 < 0.2
    input.beginFrame();
    send(input, pad);
    EXPECT_EQ(input.leftStick(), glm::vec2(0.0F));

    pad.axes[0] = 0.6F; // length 0.6 -> (0.6 - 0.2) / 0.8 = 0.5
    pad.axes[1] = 0.0F;
    send(input, pad);
    EXPECT_NEAR(input.axis(GamepadAxis::LeftX), 0.5F, 1e-6F);

    pad.axes[0] = 1.0F;
    send(input, pad);
    EXPECT_NEAR(input.axis(GamepadAxis::LeftX), 1.0F, 1e-6F);
}

TEST(GamepadTests, DisconnectReleasesHeldButtons)
{
    Input input;
    PadState pad;
    pad.buttons[static_cast<int>(GamepadButton::Start)] = true;
    input.beginFrame();
    send(input, pad);
    input.beginFrame();
    input.onGamepadDisconnected(0);
    EXPECT_FALSE(input.isGamepadConnected(0));
    EXPECT_FALSE(input.isHeld(GamepadButton::Start));
    EXPECT_TRUE(input.isReleased(GamepadButton::Start));
}

TEST(GamepadTests, InvalidIndicesAreIgnored)
{
    Input input;
    const PadState pad;
    send(input, pad, -1);
    send(input, pad, Input::kMaxGamepads);
    EXPECT_FALSE(input.isGamepadConnected(-1));
    EXPECT_FALSE(input.isGamepadConnected(Input::kMaxGamepads));
    EXPECT_FALSE(input.isHeld(GamepadButton::A, 99));
}
