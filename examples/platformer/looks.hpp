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

/// Particles drifting through the view: spores, ice glitter, motes or embers.
struct Weather {
    int count = 0;            ///< How many are in view at once.
    glm::vec4 color{1.0F};    ///< Colour and opacity.
    glm::vec2 velocity{0.0F}; ///< World units per second.
    float sway = 0.0F;        ///< Side-to-side speed, world units per second.
    float size = 1.0F;        ///< Side of the square, world units.
    bool flicker = false;     ///< Fades in and out.
};

/// A planet: its weather and its sky (a gradient, a nebula, stars, the next planet
/// hanging in it, and up to three layers).
struct Look {
    Weather weather;               ///< Drifting in front of everything.
    glm::vec4 skyTop;              ///< Colour at the top of the screen.
    glm::vec4 skyBottom;           ///< Colour at the bottom.
    glm::vec4 nebula{0.0F};        ///< Tints the nebula over the gradient (alpha 0: none).
    bool stars = false;            ///< Stars and shooting stars over the gradient (not in a cave).
    bool rings = false;            ///< The planet's own rings arc across the sky.
    int skyPlanet = planet::kNone; ///< Seen in the sky: the next stop of the route.
    std::array<Layer, 3> layers{}; ///< Back to front.
};

// The bottom colours of the strips, to fill below them.
inline constexpr glm::vec4 kSpireFill{0.251F, 0.259F, 0.486F, 1.0F};
inline constexpr glm::vec4 kFloraFill{0.086F, 0.227F, 0.290F, 1.0F};
inline constexpr glm::vec4 kCrystalFill{0.077F, 0.089F, 0.216F, 1.0F}; // with the layer's tint
inline constexpr glm::vec4 kVolcanoFill{0.204F, 0.118F, 0.157F, 1.0F};
/// The stars and the nebula of every open sky (the nebula is tinted per planet).
inline constexpr Layer kStars{
    .texture = "textures/stars.png", .parallax = {0.02F, 0.02F}, .fillsView = true};
inline constexpr Layer kNebula{
    .texture = "textures/nebula.png", .parallax = {0.01F, 0.01F}, .fillsView = true};

/// One per level, in level order: Verda, the Crystal Moon, Nimbus and Pyros.
inline constexpr std::array<Look, 4> kLooks = {{
    {.weather = {.count = 36,
                 .color = {0.70F, 1.0F, 0.85F, 0.7F},
                 .velocity = {4.0F, 6.0F},
                 .sway = 6.0F,
                 .size = 1.5F,
                 .flicker = true},
     .skyTop = {0.02F, 0.02F, 0.07F, 1.0F},
     .skyBottom = {0.42F, 0.28F, 0.56F, 1.0F},
     .nebula = {0.55F, 1.0F, 0.90F, 0.9F},
     .stars = true,
     .skyPlanet = planet::kCrystalMoon,
     .layers = {{{.texture = "textures/spires.png",
                  .parallax = {0.12F, 0.25F},
                  .y = 80.0F,
                  .fill = kSpireFill},
                 {.texture = "textures/flora.png",
                  .parallax = {0.35F, 0.45F},
                  .y = 36.0F,
                  .fill = kFloraFill}}}},
    {.weather = {.count = 60,
                 .color = {0.80F, 0.95F, 1.0F, 0.8F},
                 .velocity = {-3.0F, -10.0F},
                 .sway = 4.0F,
                 .flicker = true},
     .skyTop = {0.02F, 0.02F, 0.06F, 1.0F},
     .skyBottom = {0.08F, 0.08F, 0.18F, 1.0F},
     .layers = {{{.texture = "textures/cave.png", .parallax = {0.3F, 0.3F}, .fillsView = true},
                 {.texture = "textures/crystals.png",
                  .parallax = {0.5F, 0.55F},
                  .y = 36.0F,
                  .tint = {0.65F, 0.75F, 0.95F, 0.8F},
                  .fill = kCrystalFill}}}},
    {.weather = {.count = 30,
                 .color = {1.0F, 0.85F, 0.95F, 0.6F},
                 .velocity = {10.0F, 2.0F},
                 .sway = 3.0F,
                 .size = 2.0F},
     .skyTop = {0.03F, 0.02F, 0.10F, 1.0F},
     .skyBottom = {0.78F, 0.40F, 0.58F, 1.0F},
     .nebula = {1.0F, 0.70F, 0.95F, 0.7F},
     .stars = true,
     .rings = true,
     .skyPlanet = planet::kPyros,
     .layers = {{{.texture = "textures/clouds.png",
                  .parallax = {0.05F, 0.1F},
                  .y = 72.0F,
                  .tint = {1.0F, 0.75F, 0.90F, 0.7F}},
                 {.texture = "textures/islands.png", .parallax = {0.2F, 0.3F}, .y = 86.0F},
                 {.texture = "textures/clouds.png",
                  .parallax = {0.35F, 0.4F},
                  .y = 50.0F,
                  .tint = {1.0F, 0.88F, 0.95F, 0.95F}}}}},
    {.weather = {.count = 70,
                 .color = {1.0F, 0.55F, 0.20F, 0.9F},
                 .velocity = {3.0F, 22.0F},
                 .sway = 8.0F,
                 .size = 1.5F,
                 .flicker = true},
     .skyTop = {0.03F, 0.01F, 0.03F, 1.0F},
     .skyBottom = {0.50F, 0.13F, 0.07F, 1.0F},
     .nebula = {1.0F, 0.50F, 0.35F, 0.8F},
     .stars = true,
     .skyPlanet = planet::kEarth,
     .layers = {{{.texture = "textures/mountains.png",
                  .parallax = {0.12F, 0.25F},
                  .y = 50.0F,
                  .tint = {0.55F, 0.25F, 0.25F, 1.0F},
                  .fill = {0.127F, 0.065F, 0.108F, 1.0F}},
                 {.texture = "textures/volcanoes.png",
                  .parallax = {0.35F, 0.45F},
                  .y = 61.0F,
                  .fill = kVolcanoFill}}}},
}};

} // namespace platformer
