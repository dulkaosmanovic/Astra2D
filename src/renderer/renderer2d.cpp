#include "astra2d/renderer/renderer2d.hpp"

#include "astra2d/core/assert.hpp"
#include "astra2d/core/log.hpp"
#include "astra2d/core/profiler.hpp"
#include "astra2d/renderer/buffer.hpp"
#include "astra2d/renderer/camera.hpp"
#include "astra2d/renderer/font.hpp"
#include "astra2d/renderer/shader.hpp"
#include "astra2d/renderer/texture.hpp"
#include "astra2d/renderer/tile_layer.hpp"
#include "astra2d/renderer/vertex_array.hpp"
#include "quad_batch.hpp"

#include <glad/gl.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace astra2d {

namespace {

// OpenGL 3.3 guarantees at least 16 texture units in the fragment shader, so the shader is written
// for 16 and the renderer uses fewer only if the driver reports fewer.
constexpr int kShaderTextureSlots = 16;

constexpr const char* kBatchVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUv;
layout(location = 3) in int aTextureSlot;

uniform mat4 uViewProjection;

out vec4 vColor;
out vec2 vUv;
flat out int vTextureSlot;

void main()
{
    vColor = aColor;
    vUv = aUv;
    vTextureSlot = aTextureSlot;
    gl_Position = uViewProjection * vec4(aPosition, 0.0, 1.0);
}
)";

// GLSL 3.30 only allows indexing a sampler array with a constant expression, so the texture slot
// is resolved with a switch in which every case uses a constant index.
std::string batchFragmentShader()
{
    std::string source = R"(#version 330 core
in vec4 vColor;
in vec2 vUv;
flat in int vTextureSlot;

uniform sampler2D uTextures[16];

out vec4 fragColor;

void main()
{
    vec4 texel = vec4(1.0);
    switch (vTextureSlot) {
)";
    for (int slot = 0; slot < kShaderTextureSlots; ++slot) {
        source += "    case " + std::to_string(slot) + ": texel = texture(uTextures[" +
                  std::to_string(slot) + "], vUv); break;\n";
    }
    source += R"(    }
    fragColor = texel * vColor;
}
)";
    return source;
}

// The naive path: one unit quad, transformed by a per-quad model matrix, one draw call per quad.
constexpr const char* kSingleVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aCorner; // (0, 0) to (1, 1)

uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform vec4 uUvRect; // xy = uvMin, zw = uvMax

out vec2 vUv;

void main()
{
    vUv = mix(uUvRect.xy, uUvRect.zw, aCorner);
    gl_Position = uViewProjection * uModel * vec4(aCorner, 0.0, 1.0);
}
)";

constexpr const char* kSingleFragmentShader = R"(#version 330 core
in vec2 vUv;

uniform sampler2D uTexture;
uniform vec4 uColor;

out vec4 fragColor;

void main()
{
    fragColor = texture(uTexture, vUv) * uColor;
}
)";

} // namespace

struct Renderer2D::Impl {
    explicit Impl(const Renderer2DSettings& settings)
        : batchShader(kBatchVertexShader, batchFragmentShader())
        , singleShader(kSingleVertexShader, kSingleFragmentShader)
        , vertexBuffer(BufferType::Vertex,
                       settings.maxQuadsPerBatch * 4 * sizeof(detail::QuadVertex),
                       BufferUsage::Dynamic)
        , indexBuffer(BufferType::Index, std::as_bytes(std::span<const std::uint32_t>(
                                             detail::quadIndices(settings.maxQuadsPerBatch))))
        , unitQuadVertices(BufferType::Vertex, std::as_bytes(std::span<const float>(kUnitQuad)))
        , unitQuadIndices(BufferType::Index,
                          std::as_bytes(std::span<const std::uint32_t>(detail::quadIndices(1))))
        , whiteTexture(1, 1, std::array<std::uint8_t, 4>{255, 255, 255, 255})
        , textureSlots(queryTextureSlots())
        , batch(settings.maxQuadsPerBatch, textureSlots, whiteTexture.id())
        , batching(settings.batching)
    {
        batchVao.setVertexBuffer(vertexBuffer,
                                 VertexLayout{{.components = 2},
                                              {.components = 4},
                                              {.components = 2},
                                              {.components = 1, .type = AttributeType::Int}});
        batchVao.setIndexBuffer(indexBuffer);
        singleVao.setVertexBuffer(unitQuadVertices, VertexLayout{{.components = 2}});
        singleVao.setIndexBuffer(unitQuadIndices);

        std::array<int, kShaderTextureSlots> samplers{};
        std::iota(samplers.begin(), samplers.end(), 0);
        batchShader.setIntArray("uTextures", samplers);
        singleShader.setInt("uTexture", 0);

        ASTRA_CORE_INFO("Renderer2D: {} quads per batch, {} texture slots",
                        settings.maxQuadsPerBatch, textureSlots);
    }

    static int queryTextureSlots()
    {
        GLint units = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
        return std::clamp(units, 2, kShaderTextureSlots);
    }

    void flush()
    {
        if (batch.empty()) {
            return;
        }
        vertexBuffer.orphan();
        vertexBuffer.setData(std::as_bytes(batch.vertices()));

        const auto textures = batch.textures();
        for (std::size_t slot = 0; slot < textures.size(); ++slot) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot));
            glBindTexture(GL_TEXTURE_2D, textures[slot]);
        }

        batchShader.bind();
        batchVao.bind();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.quadCount() * 6), GL_UNSIGNED_INT,
                       nullptr);
        ++stats.drawCalls;
        batch.clear();
    }

    void drawSingle(const detail::QuadCorners& corners, glm::vec4 color,
                    const TextureRegion& region, std::uint32_t texture)
    {
        // Maps the unit square onto the quad: (0,0) -> BL, (1,0) -> BR, (0,1) -> TL.
        glm::mat4 model(1.0F);
        model[0] = glm::vec4(corners[1] - corners[0], 0.0F, 0.0F);
        model[1] = glm::vec4(corners[3] - corners[0], 0.0F, 0.0F);
        model[3] = glm::vec4(corners[0], 0.0F, 1.0F);

        singleShader.bind();
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniform4f(colorLocation, color.r, color.g, color.b, color.a);
        glUniform4f(uvRectLocation, region.uvMin.x, region.uvMin.y, region.uvMax.x, region.uvMax.y);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture != 0 ? texture : whiteTexture.id());
        singleVao.bind();
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        ++stats.drawCalls;
    }

    void submit(const detail::QuadCorners& corners, glm::vec4 color, const TextureRegion& region,
                std::uint32_t texture)
    {
        ASTRA_ASSERT(inScene, "Renderer2D: draw call outside begin()/end()");
        ++stats.quads;
        if (!batching) {
            drawSingle(corners, color, region, texture);
            return;
        }
        if (!batch.add(corners, color, region, texture)) {
            flush();
            const bool added = batch.add(corners, color, region, texture);
            ASTRA_ASSERT(added, "A quad must always fit into an empty batch");
            (void)added;
        }
    }

    static constexpr std::array<float, 8> kUnitQuad{0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F};

    Shader batchShader;
    Shader singleShader;
    Buffer vertexBuffer;
    Buffer indexBuffer;
    Buffer unitQuadVertices;
    Buffer unitQuadIndices;
    VertexArray batchVao;
    VertexArray singleVao;
    Texture2D whiteTexture;
    int textureSlots;
    detail::QuadBatch batch;
    RenderStats stats;
    // Looked up once: the naive path sets these for every quad.
    GLint modelLocation = glGetUniformLocation(singleShader.id(), "uModel");
    GLint colorLocation = glGetUniformLocation(singleShader.id(), "uColor");
    GLint uvRectLocation = glGetUniformLocation(singleShader.id(), "uUvRect");
    bool batching;
    bool inScene = false;
    glm::vec2 visibleMin{0.0F}; // of the camera given to begin(), for drawTileLayer()
    glm::vec2 visibleMax{0.0F};
};

namespace {

Renderer2DSettings validated(const Renderer2DSettings& settings)
{
    // Checked in every build: 0 used to drop every quad silently in release builds.
    if (settings.maxQuadsPerBatch == 0 ||
        settings.maxQuadsPerBatch > Renderer2D::kMaxQuadsPerBatch) {
        throw std::invalid_argument(
            std::format("Renderer2DSettings::maxQuadsPerBatch must be in [1, {}], got {}",
                        Renderer2D::kMaxQuadsPerBatch, settings.maxQuadsPerBatch));
    }
    return settings;
}

} // namespace

Renderer2D::Renderer2D(const Renderer2DSettings& settings)
    : m_impl(std::make_unique<Impl>(validated(settings)))
{
}

Renderer2D::~Renderer2D() = default;

void Renderer2D::clear(glm::vec4 color)
{
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer2D::begin(const OrthographicCamera& camera)
{
    ASTRA_ASSERT(!m_impl->inScene, "Renderer2D::begin() called twice without end()");
    m_impl->inScene = true;
    m_impl->visibleMin = camera.visibleMin();
    m_impl->visibleMax = camera.visibleMax();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    const glm::mat4 viewProjection = camera.viewProjection();
    m_impl->batchShader.setMat4("uViewProjection", viewProjection);
    m_impl->singleShader.setMat4("uViewProjection", viewProjection);
}

void Renderer2D::end()
{
    ASTRA_ASSERT(m_impl->inScene, "Renderer2D::end() without begin()");
    ASTRA_PROFILE_SCOPE("renderer flush");
    m_impl->flush();
    m_impl->inScene = false;
}

void Renderer2D::draw(const Quad& quad)
{
    TextureRegion region = quad.region;
    if (quad.flipX) {
        std::swap(region.uvMin.x, region.uvMax.x);
    }
    if (quad.flipY) {
        std::swap(region.uvMin.y, region.uvMax.y);
    }
    const std::uint32_t texture = quad.texture != nullptr ? quad.texture->id() : 0;
    m_impl->submit(detail::rectangleCorners(quad.position, quad.size, quad.rotation), quad.color,
                   region, texture);
}

void Renderer2D::drawQuad(glm::vec2 position, glm::vec2 size, glm::vec4 color, float rotation)
{
    draw({.position = position, .size = size, .rotation = rotation, .color = color});
}

void Renderer2D::drawQuad(glm::vec2 position, glm::vec2 size, const Texture2D& texture,
                          glm::vec4 tint, float rotation)
{
    draw({.position = position,
          .size = size,
          .rotation = rotation,
          .color = tint,
          .texture = &texture});
}

void Renderer2D::drawTiled(glm::vec2 position, glm::vec2 size, const Texture2D& texture,
                           glm::vec2 tileSize, glm::vec4 tint, glm::vec2 anchor)
{
    ASTRA_ASSERT(tileSize.x > 0.0F && tileSize.y > 0.0F, "Tile size must be positive");
    // Texture coordinates beyond [0, 1] repeat the texture (TextureWrap::Repeat).
    const glm::vec2 half = size * 0.5F;
    draw({.position = position,
          .size = size,
          .color = tint,
          .texture = &texture,
          .region = {.uvMin = (position - half - anchor) / tileSize,
                     .uvMax = (position + half - anchor) / tileSize}});
}

void Renderer2D::drawSprite(glm::vec2 position, glm::vec2 size, const Sprite& sprite,
                            glm::vec4 tint, float rotation, bool flipX)
{
    draw({.position = position,
          .size = size,
          .rotation = rotation,
          .color = tint,
          .texture = sprite.texture,
          .region = sprite.region,
          .flipX = flipX});
}

void Renderer2D::drawText(const Font& font, std::string_view text, glm::vec2 position, float size,
                          glm::vec4 color, float lineSpacing)
{
    for (const PlacedGlyph& glyph : font.atlas().layout(text, position, size, lineSpacing)) {
        draw({.position = glyph.center,
              .size = glyph.size,
              .color = color,
              .texture = &font.texture(),
              .region = glyph.region});
    }
}

void Renderer2D::drawText(const Font& font, std::string_view text, glm::vec2 position,
                          const TextStyle& style)
{
    const auto draw = [&](glm::vec2 at, glm::vec4 color) {
        for (const PlacedGlyph& glyph :
             font.atlas().layout(text, at, style.size, style.lineSpacing, style.anchor)) {
            this->draw({.position = glyph.center,
                        .size = glyph.size,
                        .color = color,
                        .texture = &font.texture(),
                        .region = glyph.region});
        }
    };
    if (style.shadowOffset != glm::vec2(0.0F)) {
        draw(position + style.shadowOffset, style.shadowColor);
    }
    draw(position, style.color);
}

void Renderer2D::drawTileLayer(const TileLayer& layer, const SpriteSheet& sheet, glm::vec4 tint)
{
    if (layer.width <= 0 || layer.height <= 0 || layer.tileSize <= 0.0F) {
        return;
    }
    // Only the cells the camera sees (one more on each side for tiles drawn larger than a cell).
    const glm::vec2 min = (m_impl->visibleMin - layer.origin) / layer.tileSize;
    const glm::vec2 max = (m_impl->visibleMax - layer.origin) / layer.tileSize;
    const int x0 = std::max(static_cast<int>(std::floor(min.x)) - 1, 0);
    const int y0 = std::max(static_cast<int>(std::floor(min.y)) - 1, 0);
    const int x1 = std::min(static_cast<int>(std::ceil(max.x)) + 1, layer.width - 1);
    const int y1 = std::min(static_cast<int>(std::ceil(max.y)) + 1, layer.height - 1);
    const glm::vec2 size(layer.tileSize);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (const int frame = layer.at(x, y); frame != TileLayer::kEmpty) {
                drawSprite(layer.cellCenter(x, y), size, sheet.frame(frame), tint);
            }
        }
    }
}

void Renderer2D::drawLine(glm::vec2 from, glm::vec2 to, glm::vec4 color, float thickness)
{
    detail::QuadCorners corners{};
    if (detail::lineCorners(from, to, thickness, corners)) {
        m_impl->submit(corners, color, TextureRegion{}, 0);
    }
}

void Renderer2D::drawRect(glm::vec2 position, glm::vec2 size, glm::vec4 color, float thickness)
{
    // Four bars inside the rectangle: full-width top and bottom, left and right between them.
    const float t = std::min({thickness, size.x * 0.5F, size.y * 0.5F});
    const glm::vec2 half = size * 0.5F;
    drawQuad({position.x, position.y + half.y - (t * 0.5F)}, {size.x, t}, color);
    drawQuad({position.x, position.y - half.y + (t * 0.5F)}, {size.x, t}, color);
    const float sideHeight = size.y - (2.0F * t);
    if (sideHeight > 0.0F) {
        drawQuad({position.x - half.x + (t * 0.5F), position.y}, {t, sideHeight}, color);
        drawQuad({position.x + half.x - (t * 0.5F), position.y}, {t, sideHeight}, color);
    }
}

void Renderer2D::drawCircle(glm::vec2 center, float radius, glm::vec4 color, float thickness,
                            int segments)
{
    segments = std::max(segments, 3);
    const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>(segments);
    glm::vec2 previous = center + glm::vec2{radius, 0.0F};
    for (int i = 1; i <= segments; ++i) {
        const float angle = step * static_cast<float>(i);
        const glm::vec2 point = center + (radius * glm::vec2{std::cos(angle), std::sin(angle)});
        drawLine(previous, point, color, thickness);
        previous = point;
    }
}

void Renderer2D::setBatching(bool enabled)
{
    if (enabled != m_impl->batching && m_impl->inScene) {
        m_impl->flush();
    }
    m_impl->batching = enabled;
}

bool Renderer2D::batching() const
{
    return m_impl->batching;
}

const RenderStats& Renderer2D::stats() const
{
    return m_impl->stats;
}

void Renderer2D::resetStats()
{
    m_impl->stats = {};
}

} // namespace astra2d
