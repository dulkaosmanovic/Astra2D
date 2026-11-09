// ParticleEmitter on the CPU: emission, motion, lifetime and the fixed pool (draw() needs OpenGL).
#include <astra2d/renderer/particles.hpp>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

using astra2d::ParticleEmitter;

TEST(ParticleTests, BurstStartsAtThePointAndMovesAtTheSpeed)
{
    ParticleEmitter emitter({.lifetime = 1.0F, .speed = 50.0F});
    emitter.emit({10.0F, 20.0F}, 16);
    ASSERT_EQ(emitter.count(), 16U);
    emitter.update(0.1F);
    for (const auto& p : emitter.particles()) {
        EXPECT_NEAR(glm::length(p.velocity), 50.0F, 1e-3F);
        EXPECT_NEAR(glm::distance(p.position, glm::vec2{10.0F, 20.0F}), 5.0F, 1e-3F);
    }
}

TEST(ParticleTests, DirectionAndSpreadLimitTheStartVelocity)
{
    ParticleEmitter emitter({.direction = {1.0F, 0.0F}, .spread = 0.5F, .speed = 10.0F});
    emitter.emit({}, 100);
    for (const auto& p : emitter.particles()) {
        // Within 0.25 rad of +x.
        EXPECT_GE(p.velocity.x, (10.0F * std::cos(0.25F)) - 1e-3F);
    }
}

TEST(ParticleTests, ParticlesDieAtTheEndOfTheirLifetime)
{
    ParticleEmitter emitter({.lifetime = 0.5F, .lifetimeVariation = 0.2F});
    emitter.emit({}, 50);
    emitter.update(0.39F); // all live at least 0.4 s
    EXPECT_EQ(emitter.count(), 50U);
    emitter.update(0.22F); // and at most 0.6 s
    EXPECT_EQ(emitter.count(), 0U);
}

TEST(ParticleTests, GravityPullsAndDampingSlows)
{
    ParticleEmitter falling({.speed = 0.0F, .gravity = {0.0F, -100.0F}});
    falling.emit({}, 1);
    falling.update(0.5F);
    EXPECT_NEAR(falling.particles()[0].velocity.y, -50.0F, 1e-3F);

    ParticleEmitter slowed(
        {.direction = {1.0F, 0.0F}, .spread = 0.0F, .speed = 100.0F, .damping = 2.0F});
    slowed.emit({}, 1);
    slowed.update(0.5F);
    EXPECT_NEAR(slowed.particles()[0].velocity.x, 100.0F * std::exp(-1.0F), 1e-2F);
}

TEST(ParticleTests, ThePoolDoesNotGrowAndClearEmptiesIt)
{
    ParticleEmitter emitter({}, 8);
    emitter.emit({}, 20);
    EXPECT_EQ(emitter.count(), 8U);
    EXPECT_EQ(emitter.capacity(), 8U);
    emitter.clear();
    EXPECT_EQ(emitter.count(), 0U);
}

TEST(ParticleTests, AStreamEmitsAtItsRateAtAnyFrameRate)
{
    ParticleEmitter fast({.lifetime = 10.0F});
    ParticleEmitter slow({.lifetime = 10.0F});
    for (int i = 0; i < 100; ++i) {
        fast.stream({}, 30.0F, 0.01F); // 1 s at 100 fps
    }
    for (int i = 0; i < 30; ++i) {
        slow.stream({}, 30.0F, 1.0F / 30.0F); // 1 s at 30 fps
    }
    EXPECT_NEAR(static_cast<float>(fast.count()), 30.0F, 1.0F);
    EXPECT_NEAR(static_cast<float>(slow.count()), 30.0F, 1.0F);
}

TEST(ParticleTests, TheSameSeedGivesTheSameParticles)
{
    ParticleEmitter a({.speedVariation = 0.5F}, 64, 7);
    ParticleEmitter b({.speedVariation = 0.5F}, 64, 7);
    a.emit({}, 10);
    b.emit({}, 10);
    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(a.particles()[i].velocity, b.particles()[i].velocity);
    }
}
