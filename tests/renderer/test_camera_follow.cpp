#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_follow.hpp>

#include <gtest/gtest.h>

using astra2d::CameraFollow;
using astra2d::OrthographicCamera;

namespace {

// 20 x 10 world units visible.
OrthographicCamera camera()
{
    return OrthographicCamera(10.0F, {200, 100});
}

} // namespace

TEST(CameraFollowTests, CatchesUpTheSameWayAtAnyFrameRate)
{
    const OrthographicCamera view = camera();
    CameraFollow once(2.0F);
    CameraFollow twice(2.0F);
    once.update({10.0F, 0.0F}, 0.5F, view);
    twice.update({10.0F, 0.0F}, 0.25F, view);
    twice.update({10.0F, 0.0F}, 0.25F, view);
    EXPECT_NEAR(once.position().x, twice.position().x, 1e-4F);
    // 1 - e^-1 of the way after 1 / speed seconds.
    EXPECT_NEAR(once.position().x, 6.3212F, 1e-3F);
}

TEST(CameraFollowTests, ZeroSpeedFollowsWithoutDelay)
{
    CameraFollow follow(0.0F);
    follow.update({3.0F, 4.0F}, 0.01F, camera());
    EXPECT_FLOAT_EQ(follow.position().x, 3.0F);
    EXPECT_FLOAT_EQ(follow.position().y, 4.0F);
}

TEST(CameraFollowTests, BoundsKeepTheViewInsideTheLevel)
{
    const OrthographicCamera view = camera();
    CameraFollow follow;
    follow.setBounds({0.0F, 0.0F}, {100.0F, 50.0F});
    follow.snap({-30.0F, 80.0F}, view);
    EXPECT_FLOAT_EQ(follow.position().x, 10.0F); // half the view width from the left edge
    EXPECT_FLOAT_EQ(follow.position().y, 45.0F); // half the view height below the top
    follow.clearBounds();
    follow.snap({-30.0F, 80.0F}, view);
    EXPECT_FLOAT_EQ(follow.position().x, -30.0F);
}

TEST(CameraFollowTests, BoundsSmallerThanTheViewShowItsLowEdge)
{
    CameraFollow follow;
    follow.setBounds({0.0F, 0.0F}, {12.0F, 4.0F});
    follow.snap({100.0F, 100.0F}, camera());
    EXPECT_FLOAT_EQ(follow.position().x, 10.0F);
    EXPECT_FLOAT_EQ(follow.position().y, 5.0F);
}
