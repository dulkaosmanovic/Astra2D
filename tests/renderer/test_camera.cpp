#include <astra2d/renderer/camera.hpp>

#include <glm/vec4.hpp>
#include <gtest/gtest.h>

#include <numbers>
#include <stdexcept>

using astra2d::OrthographicCamera;

namespace {

void expectNear(glm::vec2 actual, glm::vec2 expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-3F);
    EXPECT_NEAR(actual.y, expected.y, 1e-3F);
}

} // namespace

TEST(CameraTests, DefaultMapsOneUnitToOnePixelAt720p)
{
    const OrthographicCamera camera; // view height 720, viewport 1280x720
    expectNear(camera.visibleSize(), {1280.0F, 720.0F});
    expectNear(camera.visibleMin(), {-640.0F, -360.0F});
    expectNear(camera.visibleMax(), {640.0F, 360.0F});
}

TEST(CameraTests, WidthFollowsAspectRatio)
{
    OrthographicCamera camera(10.0F, {800, 400});
    expectNear(camera.visibleSize(), {20.0F, 10.0F});
    camera.setViewportSize(400, 400);
    expectNear(camera.visibleSize(), {10.0F, 10.0F});
}

TEST(CameraTests, ZoomShrinksVisibleArea)
{
    OrthographicCamera camera(10.0F, {100, 100});
    camera.setZoom(2.0F);
    expectNear(camera.visibleSize(), {5.0F, 5.0F});
}

TEST(CameraTests, MinimizedViewportKeepsLastAspect)
{
    OrthographicCamera camera(10.0F, {200, 100});
    camera.setViewportSize(0, 0);
    EXPECT_EQ(camera.viewportSize(), glm::ivec2(200, 100));
}

TEST(CameraTests, ScreenCornersMapToVisibleCorners)
{
    OrthographicCamera camera(10.0F, {200, 100});
    camera.setPosition({3.0F, 4.0F});
    // Screen origin is the top-left corner, y down.
    expectNear(camera.screenToWorld({0.0F, 0.0F}), {3.0F - 10.0F, 4.0F + 5.0F});
    expectNear(camera.screenToWorld({200.0F, 100.0F}), {3.0F + 10.0F, 4.0F - 5.0F});
    expectNear(camera.screenToWorld({100.0F, 50.0F}), {3.0F, 4.0F});
}

TEST(CameraTests, ScreenToWorldAndBackRoundTrips)
{
    OrthographicCamera camera(37.0F, {1920, 1080});
    camera.setPosition({-12.5F, 7.25F});
    camera.setZoom(1.7F);
    for (const glm::vec2 screen : {glm::vec2{0, 0}, glm::vec2{1919, 1079}, glm::vec2{640, 211}}) {
        expectNear(camera.worldToScreen(camera.screenToWorld(screen)), screen);
    }
}

TEST(CameraTests, ViewProjectionMapsVisibleCornersToClipSpace)
{
    OrthographicCamera camera(10.0F, {200, 100});
    camera.setPosition({1.0F, 2.0F});
    const glm::vec2 corner = camera.visibleMax();
    const glm::vec4 clip = camera.viewProjection() * glm::vec4(corner, 0.0F, 1.0F);
    EXPECT_NEAR(clip.x, 1.0F, 1e-5F);
    EXPECT_NEAR(clip.y, 1.0F, 1e-5F);
    const glm::vec4 centre = camera.viewProjection() * glm::vec4(1.0F, 2.0F, 0.0F, 1.0F);
    EXPECT_NEAR(centre.x, 0.0F, 1e-5F);
    EXPECT_NEAR(centre.y, 0.0F, 1e-5F);
}

TEST(CameraTests, FitAreaKeepsTheAreaVisibleAtAnyAspect)
{
    OrthographicCamera camera(1.0F, {200, 100}); // wide: the height limits
    camera.fitArea({100.0F, 40.0F});
    expectNear(camera.visibleSize(), {100.0F, 50.0F});
    camera.setViewportSize(100, 200); // tall: now the width limits
    expectNear(camera.visibleSize(), {100.0F, 200.0F});
    camera.setViewportSize(250, 100); // exactly the area's aspect
    expectNear(camera.visibleSize(), {100.0F, 40.0F});
}

TEST(CameraTests, SetViewHeightEndsFitArea)
{
    OrthographicCamera camera(1.0F, {100, 100});
    camera.fitArea({50.0F, 50.0F});
    camera.setViewHeight(10.0F);
    camera.setViewportSize(100, 400);
    expectNear(camera.visibleSize(), {2.5F, 10.0F});
}

TEST(CameraTests, PixelPerfectUsesWholePixelsPerUnit)
{
    OrthographicCamera camera(1.0F, {1280, 720});
    camera.setPixelPerfect(224.0F);
    EXPECT_FLOAT_EQ(camera.pixelsPerUnit(), 3.0F);
    EXPECT_FLOAT_EQ(camera.viewHeight(), 240.0F);
    camera.setViewportSize(1920, 1080); // kept after a resize
    EXPECT_FLOAT_EQ(camera.pixelsPerUnit(), 5.0F);
    camera.setViewportSize(100, 100); // never below one pixel per unit
    EXPECT_FLOAT_EQ(camera.pixelsPerUnit(), 1.0F);
    EXPECT_TRUE(camera.pixelPerfect());
    camera.setViewHeight(10.0F);
    EXPECT_FALSE(camera.pixelPerfect());
}

TEST(CameraTests, PixelPerfectDrawsFromAWholePixel)
{
    OrthographicCamera camera(1.0F, {200, 100});
    camera.setPixelPerfect(50.0F); // 2 pixels per unit
    camera.setPosition({10.3F, 0.0F});
    // 10.3 units are 20.6 pixels: drawn from pixel 21, i.e. 10.5 units.
    const glm::vec4 origin = camera.viewProjection() * glm::vec4(10.5F, 0.0F, 0.0F, 1.0F);
    EXPECT_NEAR(origin.x, 0.0F, 1e-5F);
    EXPECT_FLOAT_EQ(camera.position().x, 10.3F); // the position itself is kept
}

TEST(CameraTests, InvalidValuesThrow)
{
    EXPECT_THROW(OrthographicCamera{0.0F}, std::invalid_argument);
    OrthographicCamera camera;
    EXPECT_THROW(camera.setZoom(0.0F), std::invalid_argument);
    EXPECT_THROW(camera.setZoom(-1.0F), std::invalid_argument);
    EXPECT_THROW(camera.setViewHeight(-5.0F), std::invalid_argument);
    EXPECT_THROW(camera.fitArea({0.0F, 1.0F}), std::invalid_argument);
    EXPECT_THROW(camera.setPixelPerfect(0.0F), std::invalid_argument);
}

// ---------------------------------------------------------------------------------------------
// Rotation, screen space, parallax

TEST(CameraTests, RotationTurnsScreenToWorldMapping)
{
    OrthographicCamera camera(10.0F, {100, 100});
    camera.setRotation(std::numbers::pi_v<float> / 2.0F); // quarter turn counter-clockwise
    // The right edge of the screen now looks along +y in the world.
    expectNear(camera.screenToWorld({100.0F, 50.0F}), {0.0F, 5.0F});
    expectNear(camera.worldToScreen({0.0F, 5.0F}), {100.0F, 50.0F});
}

TEST(CameraTests, RotatedRoundTrip)
{
    OrthographicCamera camera(30.0F, {640, 480});
    camera.setPosition({4.0F, -2.0F});
    camera.setRotation(0.7F);
    camera.setZoom(1.3F);
    for (const glm::vec2 screen : {glm::vec2{0, 0}, glm::vec2{639, 479}, glm::vec2{100, 300}}) {
        expectNear(camera.worldToScreen(camera.screenToWorld(screen)), screen);
    }
}

TEST(CameraTests, RotatedBoundsContainTheWholeView)
{
    OrthographicCamera camera(10.0F, {200, 100}); // view 20 x 10
    camera.setRotation(std::numbers::pi_v<float> / 2.0F);
    // Turned by 90 degrees, the 20 x 10 view covers 10 x 20 of the world.
    expectNear(camera.visibleMax() - camera.visibleMin(), {10.0F, 20.0F});
    for (const glm::vec2 corner : {glm::vec2{0, 0}, glm::vec2{200, 100}, glm::vec2{0, 100}}) {
        const glm::vec2 world = camera.screenToWorld(corner);
        EXPECT_GE(world.x, camera.visibleMin().x - 1e-4F);
        EXPECT_LE(world.y, camera.visibleMax().y + 1e-4F);
    }
}

TEST(CameraTests, ScreenSpaceMapsOneUnitToOnePixelFromBottomLeft)
{
    const OrthographicCamera hud = OrthographicCamera::screenSpace(800, 600);
    expectNear(hud.screenToWorld({0.0F, 600.0F}), {0.0F, 0.0F});     // bottom-left pixel
    expectNear(hud.screenToWorld({800.0F, 0.0F}), {800.0F, 600.0F}); // top-right pixel
}

TEST(CameraTests, ParallaxScalesThePositionOnly)
{
    OrthographicCamera camera(10.0F, {100, 100});
    camera.setPosition({100.0F, 40.0F});
    camera.setZoom(2.0F);
    const OrthographicCamera far = camera.withParallax({0.25F, 0.0F});
    expectNear(far.position(), {25.0F, 0.0F});
    EXPECT_FLOAT_EQ(far.zoom(), 2.0F);
    expectNear(camera.position(), {100.0F, 40.0F}); // original unchanged
}

TEST(CameraTests, ViewProjectionAgreesWithScreenToWorldWhenRotated)
{
    // What the GPU draws (viewProjection) and what picking computes (screenToWorld) must match.
    OrthographicCamera camera(12.0F, {300, 200});
    camera.setPosition({7.0F, -3.0F});
    camera.setRotation(-1.1F);
    camera.setZoom(0.8F);
    for (const glm::vec2 screen : {glm::vec2{0, 0}, glm::vec2{300, 200}, glm::vec2{75, 160}}) {
        const glm::vec2 world = camera.screenToWorld(screen);
        const glm::vec4 clip = camera.viewProjection() * glm::vec4(world, 0.0F, 1.0F);
        const glm::vec2 expectedNdc{((screen.x / 300.0F) * 2.0F) - 1.0F,
                                    1.0F - ((screen.y / 200.0F) * 2.0F)};
        EXPECT_NEAR(clip.x, expectedNdc.x, 1e-4F);
        EXPECT_NEAR(clip.y, expectedNdc.y, 1e-4F);
    }
}
