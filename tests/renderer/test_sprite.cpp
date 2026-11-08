#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/sprite.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

using astra2d::AnimationPlayer;
using astra2d::SpriteAnimation;
using astra2d::SpriteSheetLayout;
using astra2d::TextureRegion;

namespace {

void expectRegion(const TextureRegion& region, glm::vec2 uvMin, glm::vec2 uvMax)
{
    EXPECT_FLOAT_EQ(region.uvMin.x, uvMin.x);
    EXPECT_FLOAT_EQ(region.uvMin.y, uvMin.y);
    EXPECT_FLOAT_EQ(region.uvMax.x, uvMax.x);
    EXPECT_FLOAT_EQ(region.uvMax.y, uvMax.y);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// SpriteSheetLayout

TEST(SpriteSheetTests, GridWithoutMarginOrSpacing)
{
    const SpriteSheetLayout layout({64, 32}, {16, 16});
    EXPECT_EQ(layout.columns(), 4);
    EXPECT_EQ(layout.rows(), 2);
    EXPECT_EQ(layout.frameCount(), 8);
}

TEST(SpriteSheetTests, FrameZeroIsTopLeftOfImage)
{
    // Images are uploaded with their top row at v = 1, so the top-left frame has the highest v.
    const SpriteSheetLayout layout({64, 32}, {16, 16});
    expectRegion(layout.region(0), {0.0F, 0.5F}, {0.25F, 1.0F});
    expectRegion(layout.region(3), {0.75F, 0.5F}, {1.0F, 1.0F});
    expectRegion(layout.region(4), {0.0F, 0.0F}, {0.25F, 0.5F}); // second row
    expectRegion(layout.region(1, 1), {0.25F, 0.0F}, {0.5F, 0.5F});
}

TEST(SpriteSheetTests, MarginAndSpacingAreSkipped)
{
    // 2 px margin, 16 px frames, 4 px gaps: 2 + 16 + 4 + 16 + 2 = 40 texels.
    const SpriteSheetLayout layout({40, 20}, {16, 16}, {2, 2}, {4, 4});
    EXPECT_EQ(layout.columns(), 2);
    EXPECT_EQ(layout.rows(), 1);
    const TextureRegion second = layout.region(1);
    EXPECT_FLOAT_EQ(second.uvMin.x, 22.0F / 40.0F);
    EXPECT_FLOAT_EQ(second.uvMax.x, 38.0F / 40.0F);
    EXPECT_FLOAT_EQ(second.uvMax.y, 1.0F - (2.0F / 20.0F));
    EXPECT_FLOAT_EQ(second.uvMin.y, 1.0F - (18.0F / 20.0F));
}

TEST(SpriteSheetTests, PartialFramesAtTheEdgeAreIgnored)
{
    const SpriteSheetLayout layout({50, 16}, {16, 16});
    EXPECT_EQ(layout.columns(), 3); // 2 texels left over
}

TEST(SpriteSheetTests, InvalidLayoutsThrow)
{
    EXPECT_THROW(SpriteSheetLayout({16, 16}, {0, 16}), std::invalid_argument);
    EXPECT_THROW(SpriteSheetLayout({16, 16}, {32, 32}), std::invalid_argument);
    EXPECT_THROW(SpriteSheetLayout({16, 16}, {8, 8}, {-1, 0}), std::invalid_argument);
    const SpriteSheetLayout layout({32, 32}, {16, 16});
    EXPECT_THROW((void)layout.region(4), std::out_of_range);
    EXPECT_THROW((void)layout.region(-1), std::out_of_range);
    EXPECT_THROW((void)layout.region(2, 0), std::out_of_range);
}

// ---------------------------------------------------------------------------------------------
// AnimationPlayer

TEST(AnimationTests, NothingPlayingShowsFrameZero)
{
    const AnimationPlayer player;
    EXPECT_FALSE(player.isPlaying());
    EXPECT_EQ(player.frame(), 0);
}

TEST(AnimationTests, AdvancesThroughFrames)
{
    const SpriteAnimation walk{.frames = {4, 5, 6}, .frameDuration = 0.5F};
    AnimationPlayer player;
    player.play(walk);
    EXPECT_EQ(player.frame(), 4);
    player.update(0.25F);
    EXPECT_EQ(player.frame(), 4);
    player.update(0.25F);
    EXPECT_EQ(player.frame(), 5);
    player.update(0.5F);
    EXPECT_EQ(player.frame(), 6);
}

TEST(AnimationTests, LoopingWrapsAround)
{
    const SpriteAnimation spin{.frames = {0, 1}, .frameDuration = 0.5F, .loop = true};
    AnimationPlayer player;
    player.play(spin);
    player.update(1.0F); // one full cycle
    EXPECT_EQ(player.frame(), 0);
    player.update(10.5F); // many cycles at once
    EXPECT_EQ(player.frame(), 1);
    EXPECT_FALSE(player.finished());
}

TEST(AnimationTests, NonLoopingStopsOnLastFrame)
{
    const SpriteAnimation die{.frames = {7, 8, 9}, .frameDuration = 0.5F, .loop = false};
    AnimationPlayer player;
    player.play(die);
    player.update(1.0F);
    EXPECT_EQ(player.frame(), 9);
    EXPECT_FALSE(player.finished());
    player.update(0.5F);
    EXPECT_TRUE(player.finished());
    player.update(5.0F);
    EXPECT_EQ(player.frame(), 9);
}

TEST(AnimationTests, PlayingTheSameAnimationKeepsProgress)
{
    const SpriteAnimation run{.frames = {0, 1, 2}, .frameDuration = 0.5F};
    AnimationPlayer player;
    player.play(run);
    player.update(0.5F);
    player.play(run); // e.g. called every frame while running
    EXPECT_EQ(player.frame(), 1);
    player.play(run, true);
    EXPECT_EQ(player.frame(), 0);
}

TEST(AnimationTests, SwitchingAnimationStartsFromTheBeginning)
{
    const SpriteAnimation idle{.frames = {0, 1}, .frameDuration = 0.5F};
    const SpriteAnimation jump{.frames = {5, 6}, .frameDuration = 0.5F};
    AnimationPlayer player;
    player.play(idle);
    player.update(0.5F);
    player.play(jump);
    EXPECT_EQ(player.frame(), 5);
}

TEST(AnimationTests, SpeedScalesTime)
{
    const SpriteAnimation run{.frames = {0, 1, 2, 3}, .frameDuration = 0.5F};
    AnimationPlayer player;
    player.play(run);
    player.setSpeed(2.0F);
    player.update(0.5F);
    EXPECT_EQ(player.frame(), 2);
    player.setSpeed(0.0F); // paused
    player.update(10.0F);
    EXPECT_EQ(player.frame(), 2);
    player.setSpeed(-3.0F);
    EXPECT_FLOAT_EQ(player.speed(), 0.0F);
}

TEST(AnimationTests, InvalidAnimationsThrow)
{
    AnimationPlayer player;
    const SpriteAnimation empty{};
    EXPECT_THROW(player.play(empty), std::invalid_argument);
    const SpriteAnimation zeroDuration{.frames = {0}, .frameDuration = 0.0F};
    EXPECT_THROW(player.play(zeroDuration), std::invalid_argument);
}
