#pragma once

// Internal to the renderer (not a public header). Collects quads into one vertex array on the CPU
// and assigns texture slots; Renderer2D uploads and draws the result. Contains no OpenGL calls, so
// the batching rules are unit-tested directly (tests/test_quad_batch.cpp).

#include "astra2d/renderer/sprite.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace astra2d::detail {

/// One vertex as the batch shader reads it. Tightly packed: 9 x 4 bytes.
struct QuadVertex {
    glm::vec2 position;
    glm::vec4 color;
    glm::vec2 uv;
    std::int32_t textureSlot;
};
static_assert(sizeof(QuadVertex) == 36, "QuadVertex must match the vertex layout");

/// Corners of a quad, counter-clockwise from the bottom-left: BL, BR, TR, TL.
using QuadCorners = std::array<glm::vec2, 4>;

/// Corners of a @p size rectangle centred on @p center, rotated by @p rotation radians
/// (counter-clockwise) around its centre.
[[nodiscard]] QuadCorners rectangleCorners(glm::vec2 center, glm::vec2 size, float rotation);

/// Corners of a line from @p from to @p to drawn as a rectangle @p thickness wide. Returns false
/// (and leaves @p corners alone) for a line of zero length.
[[nodiscard]] bool lineCorners(glm::vec2 from, glm::vec2 to, float thickness, QuadCorners& corners);

/// Index pattern for @p quadCount quads: two triangles (0, 1, 2) and (2, 3, 0) per quad.
[[nodiscard]] std::vector<std::uint32_t> quadIndices(std::size_t quadCount);

class QuadBatch {
public:
    /// @param maxQuads     Quads per batch (size of the vertex buffer).
    /// @param textureSlots Texture units available; slot 0 always holds @p whiteTexture, which
    ///                     untextured (coloured) quads sample.
    QuadBatch(std::size_t maxQuads, int textureSlots, std::uint32_t whiteTexture);

    /// Adds a quad. Returns false without adding anything if the batch is full or @p texture
    /// would need a texture slot when none is free; the caller then draws the batch, clears it
    /// and adds the quad again. @p texture 0 means "untextured".
    [[nodiscard]] bool add(const QuadCorners& corners, glm::vec4 color, const TextureRegion& region,
                           std::uint32_t texture);

    /// Removes all quads; slot 0 keeps the white texture.
    void clear();

    [[nodiscard]] bool empty() const { return m_vertices.empty(); }
    [[nodiscard]] std::size_t quadCount() const { return m_vertices.size() / 4; }
    [[nodiscard]] std::size_t maxQuads() const { return m_maxQuads; }

    /// Vertices of all quads, four per quad in QuadCorners order.
    [[nodiscard]] std::span<const QuadVertex> vertices() const { return m_vertices; }
    /// Texture bound to each slot, slot 0 first.
    [[nodiscard]] std::span<const std::uint32_t> textures() const { return m_textures; }

private:
    // Slot of @p texture in this batch, adding it if there is room; -1 if all slots are taken.
    [[nodiscard]] int slotFor(std::uint32_t texture);

    std::size_t m_maxQuads;
    std::size_t m_textureSlots;
    std::vector<QuadVertex> m_vertices;
    std::vector<std::uint32_t> m_textures;
};

} // namespace astra2d::detail
