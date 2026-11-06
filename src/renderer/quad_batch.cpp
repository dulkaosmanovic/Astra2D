#include "quad_batch.hpp"

#include "astra2d/core/assert.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace astra2d::detail {

QuadCorners rectangleCorners(glm::vec2 center, glm::vec2 size, float rotation)
{
    const glm::vec2 half = size * 0.5F;
    QuadCorners corners{glm::vec2{-half.x, -half.y}, glm::vec2{half.x, -half.y},
                        glm::vec2{half.x, half.y}, glm::vec2{-half.x, half.y}};
    if (rotation != 0.0F) {
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        for (glm::vec2& corner : corners) {
            corner = {(c * corner.x) - (s * corner.y), (s * corner.x) + (c * corner.y)};
        }
    }
    for (glm::vec2& corner : corners) {
        corner += center;
    }
    return corners;
}

bool lineCorners(glm::vec2 from, glm::vec2 to, float thickness, QuadCorners& corners)
{
    const glm::vec2 delta = to - from;
    const float length = glm::length(delta);
    if (length == 0.0F) {
        return false;
    }
    // Offset both ends sideways by half the thickness.
    const glm::vec2 normal = glm::vec2{-delta.y, delta.x} * (thickness * 0.5F / length);
    corners = {from - normal, to - normal, to + normal, from + normal};
    return true;
}

std::vector<std::uint32_t> quadIndices(std::size_t quadCount)
{
    std::vector<std::uint32_t> indices;
    indices.reserve(quadCount * 6);
    for (std::size_t quad = 0; quad < quadCount; ++quad) {
        const auto base = static_cast<std::uint32_t>(quad * 4);
        for (const std::uint32_t corner : {0U, 1U, 2U, 2U, 3U, 0U}) {
            indices.push_back(base + corner);
        }
    }
    return indices;
}

QuadBatch::QuadBatch(std::size_t maxQuads, int textureSlots, std::uint32_t whiteTexture)
    : m_maxQuads(maxQuads)
    , m_textureSlots(static_cast<std::size_t>(textureSlots))
{
    ASTRA_ASSERT(maxQuads > 0);
    ASTRA_ASSERT(textureSlots >= 2, "Need a slot for the white texture and at least one more");
    m_vertices.reserve(maxQuads * 4);
    m_textures.reserve(m_textureSlots);
    m_textures.push_back(whiteTexture);
}

int QuadBatch::slotFor(std::uint32_t texture)
{
    if (texture == 0) {
        return 0; // untextured quads sample the white texture
    }
    // Linear search: at most 16 slots, faster than a map at this size.
    const auto it = std::ranges::find(m_textures, texture);
    if (it != m_textures.end()) {
        return static_cast<int>(it - m_textures.begin());
    }
    if (m_textures.size() == m_textureSlots) {
        return -1;
    }
    m_textures.push_back(texture);
    return static_cast<int>(m_textures.size() - 1);
}

bool QuadBatch::add(const QuadCorners& corners, glm::vec4 color, const TextureRegion& region,
                    std::uint32_t texture)
{
    if (quadCount() == m_maxQuads) {
        return false;
    }
    const int slot = slotFor(texture);
    if (slot < 0) {
        return false;
    }

    const std::array<glm::vec2, 4> uvs{region.uvMin, glm::vec2{region.uvMax.x, region.uvMin.y},
                                       region.uvMax, glm::vec2{region.uvMin.x, region.uvMax.y}};
    for (std::size_t i = 0; i < 4; ++i) {
        m_vertices.push_back({.position = corners.at(i),
                              .color = color,
                              .uv = uvs.at(i),
                              .textureSlot = static_cast<std::int32_t>(slot)});
    }
    return true;
}

void QuadBatch::clear()
{
    m_vertices.clear();
    m_textures.resize(1); // keep the white texture in slot 0
}

} // namespace astra2d::detail
