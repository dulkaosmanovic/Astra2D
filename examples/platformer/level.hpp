#pragma once

// A level of the platformer as plain data: which tile is drawn where, what collides, and where the
// player, coins, enemies, checkpoints and the goal start. Loaded from a text map (.txt) or a Tiled
// map (.tmj); no OpenGL, so it is unit-tested (tests/test_platformer.cpp).

#include <astra2d/physics/tilemap.hpp>
#include <astra2d/scene/tiled_map.hpp>

#include <glm/vec2.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace platformer {

/// Side of a tile in world units (one texel of the 16x16 art is one unit).
inline constexpr float kTileSize = 16.0F;

/// Frames of tiles.png (see scripts/generate_platformer_assets.py).
namespace tile {
inline constexpr int kNone = -1;
inline constexpr int kGrass = 0;
inline constexpr int kDirt = 1;
inline constexpr int kGrassLeft = 2;
inline constexpr int kGrassRight = 3;
inline constexpr int kStoneTop = 4;
inline constexpr int kStone = 5;
inline constexpr int kPlank = 6;
inline constexpr int kSpikes = 7;
inline constexpr int kBush = 8;
inline constexpr int kFlower = 9;
inline constexpr int kRock = 10;
inline constexpr int kSign = 11;
inline constexpr int kCrate = 12;
inline constexpr int kBrick = 13;
inline constexpr int kGrassSingle = 14;
inline constexpr int kLava = 15;
} // namespace tile

/// Something that starts somewhere in the level.
enum class ThingKind { Player, Coin, Enemy, Checkpoint, Goal };

/// A ThingKind at a world position (the centre of its tile).
struct Thing {
    ThingKind kind = ThingKind::Coin; ///< What it is.
    glm::vec2 position{0.0F};         ///< World position.
};

/// A tile that hurts: spikes or lava.
struct Hazard {
    int x = 0;                 ///< Column.
    int y = 0;                 ///< Row, from the bottom.
    int frame = tile::kSpikes; ///< tile::kSpikes or tile::kLava (they hurt in different areas).
};

/// One level. Rows count from the bottom, like astra2d::TileGrid, with the level's bottom-left
/// corner at the world origin.
struct Level {
    std::string name;        ///< Shown when the level starts.
    std::string hint;        ///< A tip shown below the name.
    int width = 0;           ///< Columns.
    int height = 0;          ///< Rows.
    std::vector<int> ground; ///< Tile frame per cell (tile::kNone for empty), row 0 at the bottom.
    std::vector<int> decor;  ///< Decoration drawn behind the ground, same layout.
    astra2d::TileGrid solid; ///< Tiles that collide from every side.
    astra2d::TileGrid platforms; ///< One-way planks (their own collider, so the player can drop).
    std::vector<Hazard> hazards; ///< Spikes and lava.
    std::vector<Thing> things;   ///< Player start, coins, enemies, checkpoints and goals.

    /// Tile frame of @p layer at column @p x, row @p y; tile::kNone outside the level.
    [[nodiscard]] int at(const std::vector<int>& layer, int x, int y) const;
    /// World position of the centre of a cell.
    [[nodiscard]] glm::vec2 cellCenter(int x, int y) const;
    /// Size in world units.
    [[nodiscard]] glm::vec2 size() const;
    /// Where the player starts.
    [[nodiscard]] glm::vec2 playerStart() const;
    /// Number of things of @p kind.
    [[nodiscard]] int count(ThingKind kind) const;
};

/// Parses a text map (format in examples/assets/levels/level1.txt).
/// @throws std::runtime_error with the line number if it is malformed, or if it has no player
///         start, more than one, or no goal.
[[nodiscard]] Level parseTextLevel(std::string_view text);

/// Makes a level from a Tiled map: tile layers "ground" (required; tiles with the property
/// `collision` "solid" / "oneway" collide, tiles with `hazard` hurt) and "decor" (optional), an
/// object layer "objects" whose objects have the class player, coin, enemy, checkpoint or goal,
/// and the map properties `name` and `hint`.
/// @throws std::runtime_error as parseTextLevel(), or if a layer is missing or the tiles are not
///         16x16.
[[nodiscard]] Level levelFromTiled(const astra2d::TiledMap& map);

/// Loads a .txt text map or a .tmj / .json Tiled map.
/// @throws std::runtime_error if the file cannot be read or is not a valid level.
[[nodiscard]] Level loadLevel(const std::filesystem::path& file);

} // namespace platformer
