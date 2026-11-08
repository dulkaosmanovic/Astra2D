// Renderer2D API checks that need no OpenGL context: compile-time lifetime rules and settings
// validation (which happens before any OpenGL call).
#include <astra2d/renderer/animation.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>
#include <astra2d/renderer/texture.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

using astra2d::AnimationPlayer;
using astra2d::Renderer2D;
using astra2d::SpriteAnimation;
using astra2d::SpriteSheet;
using astra2d::Texture2D;

// ---------------------------------------------------------------------------------------------
// Objects that are kept by reference or drawn later must not be temporaries.

// A sheet built from a temporary texture would dangle at the end of the statement.
static_assert(!std::is_constructible_v<SpriteSheet, Texture2D&&, glm::ivec2>);
static_assert(!std::is_constructible_v<SpriteSheet, const Texture2D&&, glm::ivec2>);
static_assert(std::is_constructible_v<SpriteSheet, const Texture2D&, glm::ivec2>);
static_assert(std::is_constructible_v<SpriteSheet, Texture2D&, glm::ivec2>);

// The player keeps a pointer to the animation.
template <typename Animation>
concept Playable = requires(AnimationPlayer player, Animation&& animation) {
    player.play(std::forward<Animation>(animation));
};
static_assert(!Playable<SpriteAnimation>); // temporary
static_assert(!Playable<const SpriteAnimation>);
static_assert(Playable<const SpriteAnimation&>);
static_assert(Playable<SpriteAnimation&>);

// A texture passed to drawQuad() is drawn at end(), after a temporary would be gone.
template <typename Texture>
concept DrawableTexture = requires(Renderer2D renderer, Texture&& texture) {
    renderer.drawQuad(glm::vec2{}, glm::vec2{}, std::forward<Texture>(texture));
};
static_assert(!DrawableTexture<Texture2D>);
static_assert(DrawableTexture<const Texture2D&>);
static_assert(DrawableTexture<Texture2D&>);

TEST(RendererApiTests, LifetimeRulesAreCheckedAtCompileTime)
{
    SUCCEED() << "see the static_asserts in this file";
}

// ---------------------------------------------------------------------------------------------
// Settings are validated in every build, before an OpenGL context is needed.

TEST(RendererApiTests, ZeroQuadsPerBatchIsRejected)
{
    // Used to drop every quad silently in release builds.
    EXPECT_THROW(Renderer2D({.maxQuadsPerBatch = 0}), std::invalid_argument);
}

TEST(RendererApiTests, TooManyQuadsPerBatchIsRejected)
{
    EXPECT_THROW(Renderer2D({.maxQuadsPerBatch = Renderer2D::kMaxQuadsPerBatch + 1}),
                 std::invalid_argument);
}
