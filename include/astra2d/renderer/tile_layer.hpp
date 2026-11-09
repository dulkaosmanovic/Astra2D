#pragma once

#include <glm/vec2.hpp>

#include <vector>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// A grid of tiles to draw, e.g. the ground of a level: one sprite sheet frame per cell, drawn by
/// Renderer2D::drawTileLayer(). Laid out like TileGrid (the collision side): cell (0, 0) is at the
/// bottom left. A layer from a Tiled map comes from makeTileLayer().
///
/// @code
/// astra2d::TileLayer ground{.width = 64, .height = 14, .tileSize = 16.0F};
/// ground.set(3, 0, kGrassFrame);
/// m_renderer.drawTileLayer(ground, m_tiles); // only the tiles the camera sees
/// @endcode
struct TileLayer {
    /// The frame of an empty cell.
    static constexpr int kEmpty = -1;

    int width = 0;          ///< Columns.
    int height = 0;         ///< Rows.
    float tileSize = 1.0F;  ///< Side of a tile in world units.
    glm::vec2 origin{0.0F}; ///< World position of the bottom-left corner of cell (0, 0).
    /// `width * height` sprite sheet frames, row by row from the bottom; kEmpty draws nothing. May
    /// be shorter (even empty): missing cells are empty; set() fills it up.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Wmissing-field-initializers
    std::vector<int> frames{};

    /// The frame at column @p x and row @p y; kEmpty outside the layer.
    [[nodiscard]] int at(int x, int y) const;
    /// Sets the frame at column @p x and row @p y. @pre inside the layer.
    void set(int x, int y, int frame);
    /// World position of the centre of the cell at column @p x and row @p y.
    [[nodiscard]] glm::vec2 cellCenter(int x, int y) const;
    /// Equal if all fields are equal.
    friend bool operator==(const TileLayer&, const TileLayer&) = default;
};

} // namespace astra2d

/// @}
