// InputGate: which presses and releases reach the game while ImGui wants the mouse or keyboard.

#include "debug/input_gate.hpp"

#include <gtest/gtest.h>

using Gate = astra2d::detail::InputGate<8>;

TEST(InputGateTests, EverythingPassesWhileImGuiDoesNotWantTheDevice)
{
    Gate gate;
    EXPECT_TRUE(gate.press(3, false));
    EXPECT_TRUE(gate.repeat(3, false));
    EXPECT_TRUE(gate.release(3, false));
}

TEST(InputGateTests, PressAndItsReleaseStayWithImGui)
{
    Gate gate;
    EXPECT_FALSE(gate.press(3, true)); // e.g. a click on an ImGui window
    EXPECT_FALSE(gate.repeat(3, false));
    EXPECT_FALSE(gate.release(3, false)); // released after leaving it: still not the game's
}

TEST(InputGateTests, ReleaseOfAPassedPressAlwaysPasses)
{
    Gate gate;
    EXPECT_TRUE(gate.press(3, false));    // a drag that starts in the game...
    EXPECT_TRUE(gate.release(3, true));   // ...and ends over ImGui: the game must see it end
    EXPECT_FALSE(gate.release(3, false)); // only once
}

TEST(InputGateTests, CodesAreTrackedSeparately)
{
    Gate gate;
    EXPECT_TRUE(gate.press(1, false));
    EXPECT_FALSE(gate.press(2, true));
    EXPECT_FALSE(gate.release(2, false));
    EXPECT_TRUE(gate.release(1, false));
}

TEST(InputGateTests, UnknownCodesFollowImGuiOnly)
{
    Gate gate;
    EXPECT_TRUE(gate.press(-1, false));
    EXPECT_FALSE(gate.press(8, true));
    EXPECT_TRUE(gate.release(-1, false));
    EXPECT_FALSE(gate.repeat(100, true));
}
