#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/camera_shake.hpp>

#include <gtest/gtest.h>

#include <cmath>

using astra2d::CameraShake;
using astra2d::CameraShakeSettings;

TEST(CameraShakeTests, NoTraumaNoShake)
{
    CameraShake shake;
    for (int i = 0; i < 100; ++i) {
        shake.update(0.016F);
        EXPECT_EQ(shake.offset(), glm::vec2(0.0F));
        EXPECT_FLOAT_EQ(shake.angle(), 0.0F);
    }
}

TEST(CameraShakeTests, TraumaIsClampedAndDecays)
{
    CameraShake shake({.decay = 2.0F});
    shake.addTrauma(0.7F);
    shake.addTrauma(0.7F);
    EXPECT_FLOAT_EQ(shake.trauma(), 1.0F);
    shake.update(0.25F);
    EXPECT_FLOAT_EQ(shake.trauma(), 0.5F);
    shake.update(10.0F);
    EXPECT_FLOAT_EQ(shake.trauma(), 0.0F);
    shake.addTrauma(-1.0F);
    EXPECT_FLOAT_EQ(shake.trauma(), 0.0F);
}

TEST(CameraShakeTests, ShakeIsBoundedByTraumaSquared)
{
    const CameraShakeSettings settings{
        .maxOffset = {10.0F, 20.0F}, .maxAngle = 0.1F, .decay = 0.0F};
    CameraShake shake(settings);
    shake.addTrauma(0.5F); // trauma^2 = 0.25
    bool moved = false;
    for (int i = 0; i < 600; ++i) {
        shake.update(0.01F);
        EXPECT_LE(std::abs(shake.offset().x), 2.5F + 1e-5F);
        EXPECT_LE(std::abs(shake.offset().y), 5.0F + 1e-5F);
        EXPECT_LE(std::abs(shake.angle()), 0.025F + 1e-6F);
        moved = moved || std::abs(shake.offset().x) > 0.5F;
    }
    EXPECT_TRUE(moved);
}

TEST(CameraShakeTests, MotionIsSmooth)
{
    // Noise, not random jumps: consecutive frames at 1 ms stay close together.
    CameraShake shake({.maxOffset = {10.0F, 10.0F}, .frequency = 10.0F, .decay = 0.0F});
    shake.addTrauma(1.0F);
    glm::vec2 previous = shake.offset();
    for (int i = 0; i < 1000; ++i) {
        shake.update(0.001F);
        EXPECT_LT(std::abs(shake.offset().x - previous.x), 1.0F);
        previous = shake.offset();
    }
}

TEST(CameraShakeTests, SameSeedSameShake)
{
    CameraShake a({}, 7);
    CameraShake b({}, 7);
    CameraShake c({}, 8);
    for (CameraShake* shake : {&a, &b, &c}) {
        shake->addTrauma(1.0F);
        shake->update(0.123F);
    }
    EXPECT_EQ(a.offset(), b.offset());
    EXPECT_NE(a.offset(), c.offset());
}

TEST(CameraShakeTests, ApplyOffsetsTheCamera)
{
    CameraShake shake({.decay = 0.0F});
    shake.addTrauma(1.0F);
    shake.update(0.05F);
    astra2d::OrthographicCamera camera;
    shake.apply(camera, {100.0F, 50.0F}, 0.5F);
    EXPECT_EQ(camera.position(), glm::vec2(100.0F, 50.0F) + shake.offset());
    EXPECT_FLOAT_EQ(camera.rotation(), 0.5F + shake.angle());
}
