#include <astra2d/core/input.hpp>

#include <gtest/gtest.h>

using astra2d::Input;
using astra2d::Key;
using astra2d::MouseButton;

namespace {

void press(Input& input, Key key) { input.onKey(static_cast<int>(key), true); }
void release(Input& input, Key key) { input.onKey(static_cast<int>(key), false); }

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
    EXPECT_FALSE(input.isHeld(static_cast<Key>(-1)));
    EXPECT_FALSE(input.isHeld(static_cast<Key>(100000)));
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
