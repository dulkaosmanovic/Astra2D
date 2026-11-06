#pragma once

#include "astra2d/physics/world.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <vector>

/// @addtogroup physics
/// @{

namespace astra2d {

/// What a tile is made of, for collision.
enum class TileCollision : std::uint8_t {
    Empty = 0,  ///< No collision.
    Solid = 1,  ///< Collides from every side.
    OneWay = 2, ///< A platform that collides only from above (see ShapeDef::oneWay).
};

/// A grid of tiles for collision, e.g. a platformer level. Cell (0, 0) is at the bottom left.
struct TileGrid {
    int width = 0;          ///< Columns.
    int height = 0;         ///< Rows.
    float tileSize = 1.0F;  ///< Side of a tile in world units.
    glm::vec2 origin{0.0F}; ///< Bottom-left corner of cell (0, 0), relative to the body.
    /// `width * height` tiles, row by row from the bottom.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Wmissing-field-initializers
    std::vector<TileCollision> cells{};

    /// The tile at column @p x and row @p y; Empty outside the grid.
    [[nodiscard]] TileCollision at(int x, int y) const;
    /// Sets the tile at column @p x and row @p y. @pre inside the grid.
    void set(int x, int y, TileCollision tile);
    /// Equal if all fields are equal.
    friend bool operator==(const TileGrid&, const TileGrid&) = default;
};

/// A rectangle of tiles that becomes one box shape.
struct TileRect {
    int x = 0;                                 ///< First column.
    int y = 0;                                 ///< First row.
    int width = 1;                             ///< Columns.
    int height = 1;                            ///< Rows.
    TileCollision kind = TileCollision::Solid; ///< Solid or one-way.
    /// Edges that can collide (kBoxBottom, kBoxRight, kBoxTop, kBoxLeft): an edge entirely covered
    /// by solid tiles is inside the level and is turned off, so objects do not catch on the seams
    /// between rectangles.
    std::uint8_t edgeMask = 0x0F;
};

/// Covers the non-empty tiles of @p grid with few rectangles: each row is split into runs of equal
/// tiles, and runs with the same columns in consecutive rows are merged. Fewer shapes make the
/// broad phase cheaper, and a flat floor is one shape without seams.
[[nodiscard]] std::vector<TileRect> mergeTiles(const TileGrid& grid);

/// Adds the rectangles of mergeTiles(@p grid) to @p body (usually a static body) as box shapes
/// with the material, filter and user data of @p material (its geometry is ignored). One-way
/// rectangles become one-way platforms that collide only on their top edge. Returns the shapes.
std::vector<ShapeId> addTileShapes(PhysicsWorld& world, BodyId body, const TileGrid& grid,
                                   const ShapeDef& material = {});

} // namespace astra2d

/// @}
