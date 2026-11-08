#pragma once

// How each planet of the platformer looks behind its tiles: plain data, drawn by game.cpp.

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <string_view>

namespace platformer {

/// Frames of planets.png, in the order of the route.
namespace planet {
inline constexpr int kNone = -1;
inline constexpr int kVerda = 0;
inline constexpr int kCrystalMoon = 1;
inline constexpr int kNimbus = 2;
inline constexpr int kPyros = 3;
inline constexpr int kEarth = 4;
inline constexpr int kCount = 5;
} // namespace planet

/// A tiling texture that scrolls slower than the world (parallax).
struct Layer {
    std::string_view texture; ///< Under the assets directory; empty: no layer.
    glm::vec2 parallax{1.0F}; ///< How fast it scrolls compared to the world.
    float y = 0.0F;           ///< World height of the strip's centre.
    glm::vec4 tint{1.0F};     ///< Multiplies the texture.
    glm::vec4 fill{0.0F};     ///< Fills the view below the strip (alpha 0: nothing).
    bool fillsView = false;   ///< Tiles the whole view instead of a strip (stars, the cave wall).
};

/// A planet's sky: a gradient, stars, the next planet hanging in it, and up to three layers.
struct Look {
    glm::vec4 skyTop;              ///< Colour at the top of the screen.
    glm::vec4 skyBottom;           ///< Colour at the bottom.
    bool stars = false;            ///< Stars over the gradient (not in a cave).
    int skyPlanet = planet::kNone; ///< Seen in the sky: the next stop of the route.
    glm::vec4 ground{1.0F};        ///< Tints the tiles, so shared tiles fit each planet.
    std::array<Layer, 3> layers{}; ///< Back to front.
};

// The bottom colours of mountains.png, hills.png and volcanoes.png, to fill below the strips
// (times the tint).
inline constexpr glm::vec4 kMountainFill{0.23F, 0.26F, 0.43F, 1.0F};
inline constexpr glm::vec4 kHillFill{0.13F, 0.29F, 0.24F, 1.0F};
inline constexpr glm::vec4 kVolcanoFill{0.204F, 0.118F, 0.157F, 1.0F};
/// The stars of every open sky.
inline constexpr Layer kStars{
    .texture = "textures/stars.png", .parallax = {0.02F, 0.02F}, .fillsView = true};

/// One per level, in level order: Verda, the Crystal Moon, Nimbus and Pyros.
inline constexpr std::array<Look, 4> kLooks = {{
    {.skyTop = {0.02F, 0.02F, 0.07F, 1.0F},
     .skyBottom = {0.16F, 0.40F, 0.48F, 1.0F},
     .stars = true,
     .skyPlanet = planet::kCrystalMoon,
     .layers = {{{.texture = "textures/mountains.png",
                  .parallax = {0.15F, 0.3F},
                  .y = 70.0F,
                  .tint = {0.75F, 1.0F, 1.0F, 1.0F},
                  .fill = {0.17F, 0.26F, 0.43F, 1.0F}},
                 {.texture = "textures/hills.png",
                  .parallax = {0.4F, 0.5F},
                  .y = 40.0F,
                  .fill = kHillFill}}}},
    {.skyTop = {0.02F, 0.02F, 0.06F, 1.0F},
     .skyBottom = {0.08F, 0.08F, 0.18F, 1.0F},
     .ground = {0.80F, 0.90F, 1.0F, 1.0F},
     .layers = {{{.texture = "textures/cave.png",
                  .parallax = {0.3F, 0.3F},
                  .tint = {0.26F, 0.32F, 0.50F, 1.0F},
                  .fillsView = true}}}},
    {.skyTop = {0.03F, 0.02F, 0.10F, 1.0F},
     .skyBottom = {0.78F, 0.40F, 0.58F, 1.0F},
     .stars = true,
     .skyPlanet = planet::kPyros,
     .ground = {0.95F, 0.80F, 1.0F, 1.0F},
     .layers = {{{.texture = "textures/clouds.png",
                  .parallax = {0.05F, 0.1F},
                  .y = 72.0F,
                  .tint = {1.0F, 0.75F, 0.90F, 0.7F}},
                 {.texture = "textures/clouds.png",
                  .parallax = {0.35F, 0.4F},
                  .y = 50.0F,
                  .tint = {1.0F, 0.88F, 0.95F, 0.95F}}}}},
    {.skyTop = {0.03F, 0.01F, 0.03F, 1.0F},
     .skyBottom = {0.50F, 0.13F, 0.07F, 1.0F},
     .stars = true,
     .skyPlanet = planet::kEarth,
     .layers = {{{.texture = "textures/mountains.png",
                  .parallax = {0.12F, 0.25F},
                  .y = 50.0F,
                  .tint = {0.55F, 0.25F, 0.25F, 1.0F},
                  .fill = {0.127F, 0.065F, 0.108F, 1.0F}},
                 {.texture = "textures/volcanoes.png",
                  .parallax = {0.35F, 0.45F},
                  .y = 45.0F,
                  .fill = kVolcanoFill}}}},
}};

} // namespace platformer
