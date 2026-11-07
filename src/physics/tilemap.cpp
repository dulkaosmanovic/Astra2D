#include "astra2d/physics/tilemap.hpp"

#include "astra2d/core/assert.hpp"

#include <algorithm>

namespace astra2d {

TileCollision TileGrid::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return TileCollision::Empty;
    }
    // set() fills cells lazily, so a grid that was only given its size has none yet.
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                              static_cast<std::size_t>(x);
    return index < cells.size() ? cells[index] : TileCollision::Empty;
}

void TileGrid::set(int x, int y, TileCollision tile)
{
    ASTRA_ASSERT(x >= 0 && y >= 0 && x < width && y < height, "tile outside the grid");
    cells.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                 TileCollision::Empty);
    cells[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
          static_cast<std::size_t>(x)] = tile;
}

namespace {

bool solidRow(const TileGrid& grid, int x, int width, int y)
{
    for (int i = x; i < x + width; ++i) {
        if (grid.at(i, y) != TileCollision::Solid) {
            return false;
        }
    }
    return true;
}

bool solidColumn(const TileGrid& grid, int x, int y, int height)
{
    for (int j = y; j < y + height; ++j) {
        if (grid.at(x, j) != TileCollision::Solid) {
            return false;
        }
    }
    return true;
}

} // namespace

namespace {

// The run of equal tiles starting at column x of row y: its end column (exclusive).
int runEnd(const TileGrid& grid, int x, int y)
{
    const TileCollision kind = grid.at(x, y);
    int end = x + 1;
    while (end < grid.width && grid.at(end, y) == kind) {
        ++end;
    }
    return end;
}

// Edges of `r` that are not completely covered by solid tiles.
std::uint8_t exposedEdges(const TileGrid& grid, const TileRect& r)
{
    unsigned mask = 0;
    mask |= solidRow(grid, r.x, r.width, r.y - 1) ? 0U : kBoxBottom;
    mask |= solidColumn(grid, r.x + r.width, r.y, r.height) ? 0U : kBoxRight;
    mask |= solidRow(grid, r.x, r.width, r.y + r.height) ? 0U : kBoxTop;
    mask |= solidColumn(grid, r.x - 1, r.y, r.height) ? 0U : kBoxLeft;
    if (r.kind == TileCollision::OneWay) {
        mask &= kBoxTop; // a platform is only stood on
    }
    return static_cast<std::uint8_t>(mask);
}

} // namespace

std::vector<TileRect> mergeTiles(const TileGrid& grid)
{
    // Fewer cells are fine (the rest are Empty, see TileGrid::at()); more mean a wrong size.
    ASTRA_ASSERT(grid.cells.size() <=
                     static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height),
                 "TileGrid::cells must have at most width * height tiles");
    std::vector<TileRect> rects;
    std::vector<std::size_t> open; // rectangles that reach the previous row
    std::vector<std::size_t> nextOpen;
    for (int y = 0; y < grid.height; ++y) {
        nextOpen.clear();
        for (int x = 0; x < grid.width;) {
            const TileCollision kind = grid.at(x, y);
            if (kind == TileCollision::Empty) {
                ++x;
                continue;
            }
            const int end = runEnd(grid, x, y);
            const TileRect run{.x = x, .y = y, .width = end - x, .height = 1, .kind = kind};
            // The same run in the row below: grow that rectangle instead of starting one.
            const auto below = std::ranges::find_if(open, [&](std::size_t candidate) {
                const TileRect& r = rects[candidate];
                return r.x == run.x && r.width == run.width && r.kind == run.kind;
            });
            if (below != open.end()) {
                ++rects[*below].height;
                nextOpen.push_back(*below);
            } else {
                nextOpen.push_back(rects.size());
                rects.push_back(run);
            }
            x = end;
        }
        std::swap(open, nextOpen);
    }
    for (TileRect& r : rects) {
        r.edgeMask = exposedEdges(grid, r);
    }
    return rects;
}

std::vector<ShapeId> addTileShapes(PhysicsWorld& world, BodyId body, const TileGrid& grid,
                                   const ShapeDef& material)
{
    std::vector<ShapeId> result;
    for (const TileRect& r : mergeTiles(grid)) {
        if (r.edgeMask == 0) {
            continue; // completely enclosed (e.g. a platform under a solid block)
        }
        const glm::vec2 size =
            glm::vec2(static_cast<float>(r.width), static_cast<float>(r.height)) * grid.tileSize;
        const glm::vec2 corner =
            grid.origin +
            (glm::vec2(static_cast<float>(r.x), static_cast<float>(r.y)) * grid.tileSize);
        ShapeDef def = material;
        def.type = ShapeType::Box;
        def.polygon = makeBox(0.5F * size, corner + (0.5F * size));
        def.edgeMask = r.edgeMask;
        def.oneWay = r.kind == TileCollision::OneWay;
        result.push_back(world.addShape(body, def));
    }
    return result;
}

} // namespace astra2d
