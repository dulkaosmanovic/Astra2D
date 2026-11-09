#include "astra2d/renderer/tile_layer.hpp"

#include "astra2d/core/assert.hpp"

#include <cstddef>

namespace astra2d {

int TileLayer::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return kEmpty;
    }
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                              static_cast<std::size_t>(x);
    return index < frames.size() ? frames[index] : kEmpty;
}

void TileLayer::set(int x, int y, int frame)
{
    ASTRA_ASSERT(x >= 0 && y >= 0 && x < width && y < height, "tile outside the layer");
    frames.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), kEmpty);
    frames[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
           static_cast<std::size_t>(x)] = frame;
}

glm::vec2 TileLayer::cellCenter(int x, int y) const
{
    return origin +
           (glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) * tileSize);
}

} // namespace astra2d
