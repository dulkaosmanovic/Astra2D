#pragma once

#include "astra2d/renderer/sprite.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

/// @addtogroup renderer
/// @{

namespace astra2d {

class Font;
class OrthographicCamera;
class Texture2D;

/// Settings used when creating a Renderer2D.
struct Renderer2DSettings {
    /// Quads collected before a batch is drawn. Larger batches mean fewer draw calls but a bigger
    /// vertex buffer (36 bytes per vertex, 4 vertices per quad). Must be in
    /// [1, Renderer2D::kMaxQuadsPerBatch].
    std::size_t maxQuadsPerBatch = 10'000;

    /// Start with batching on. See Renderer2D::setBatching().
    bool batching = true;
};

/// Counters since the last Renderer2D::resetStats().
struct RenderStats {
    std::uint32_t drawCalls = 0; ///< `glDrawElements` calls.
    std::uint32_t quads = 0;     ///< Quads drawn (lines, rectangle and circle outlines included).
};

/// Everything that describes one quad. Meant for designated initializers:
/// @code
/// renderer.draw({.position = {0, 0}, .size = {32, 32}, .rotation = angle, .color = red});
/// @endcode
struct Quad {
    glm::vec2 position{0.0F}; ///< Centre in world units.
    glm::vec2 size{1.0F};     ///< Width and height in world units.
    float rotation = 0.0F;    ///< Counter-clockwise, in radians, around the centre.
    glm::vec4 color{1.0F};    ///< Colour, or tint multiplied with the texture.
    /// nullptr draws a solid colour. Not owned; must stay alive until end().
    const Texture2D* texture = nullptr;
    TextureRegion region{}; ///< Part of the texture to show.
    bool flipX = false;     ///< Mirror the texture horizontally.
    bool flipY = false;     ///< Mirror the texture vertically.
};

/// Batched 2D renderer for coloured and textured quads, sprites and debug lines.
///
/// Quads submitted between begin() and end() are collected into one vertex buffer and drawn with
/// as few draw calls as possible: a batch is drawn when it is full, when all texture slots are in
/// use, or at end(). Quads are drawn in submission order (later quads on top), with alpha blending.
///
/// @code
/// m_renderer.begin(m_camera);
/// m_renderer.drawQuad({0, 0}, {100, 100}, {1, 0, 0, 1});
/// m_renderer.drawSprite(m_playerPosition, {32, 32}, m_sheet.frame(m_animation.frame()));
/// m_renderer.drawRect(m_bounds.center, m_bounds.size, {0, 1, 0, 1}, 2.0F);
/// m_renderer.end();
/// @endcode
///
/// Needs a current OpenGL 3.3 context: create it after the Window, e.g. as a member of the
/// Application subclass (members are constructed after the window exists).
///
/// @warning Textures are drawn when the batch is flushed, not when a draw function returns, so
/// every texture used in a scene must stay alive until end(). A texture destroyed earlier may be
/// replaced by another one that OpenGL gives the same name, and the quad shows the wrong image.
class Renderer2D {
public:
    /// Largest allowed Renderer2DSettings::maxQuadsPerBatch: 4 vertices per quad must stay
    /// addressable by 32-bit indices, and the vertex buffer at about 150 MB at most.
    static constexpr std::size_t kMaxQuadsPerBatch = 1U << 20U;

    /// Creates the shaders, buffers and the white texture used for solid colours.
    /// @throws std::invalid_argument if the settings are out of range (checked before any OpenGL
    ///         call, in every build).
    explicit Renderer2D(const Renderer2DSettings& settings = {});
    ~Renderer2D();
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;
    Renderer2D(Renderer2D&&) = delete;
    Renderer2D& operator=(Renderer2D&&) = delete;

    /// Starts a scene seen through @p camera. Sets alpha blending and binds the renderer's shader.
    void begin(const OrthographicCamera& camera);
    /// Draws everything still in the batch. Must follow begin().
    void end();

    /// Submits a quad. Must be called between begin() and end().
    void draw(const Quad& quad);

    /// Solid-coloured rectangle centred on @p position.
    void drawQuad(glm::vec2 position, glm::vec2 size, glm::vec4 color, float rotation = 0.0F);
    /// Whole @p texture on a rectangle centred on @p position, multiplied by @p tint.
    void drawQuad(glm::vec2 position, glm::vec2 size, const Texture2D& texture,
                  glm::vec4 tint = glm::vec4(1.0F), float rotation = 0.0F);
    /// A temporary texture would be destroyed before end() draws it, so it is rejected at compile
    /// time.
    void drawQuad(glm::vec2 position, glm::vec2 size, const Texture2D&& texture,
                  glm::vec4 tint = glm::vec4(1.0F), float rotation = 0.0F) = delete;
    /// Fills a rectangle centred on @p position with @p texture repeated every @p tileSize world
    /// units. The pattern is fixed in the world, with a tile's bottom-left corner at @p anchor, so
    /// it stays in place while the rectangle moves: drawing the camera's visible area gives an
    /// endless background. For a strip one tile high, put the anchor on the strip's bottom edge.
    /// The texture must use TextureWrap::Repeat.
    void drawTiled(glm::vec2 position, glm::vec2 size, const Texture2D& texture, glm::vec2 tileSize,
                   glm::vec4 tint = glm::vec4(1.0F), glm::vec2 anchor = glm::vec2(0.0F));
    /// A temporary texture would be destroyed before end() draws it.
    void drawTiled(glm::vec2 position, glm::vec2 size, const Texture2D&& texture,
                   glm::vec2 tileSize, glm::vec4 tint = glm::vec4(1.0F),
                   glm::vec2 anchor = glm::vec2(0.0F)) = delete;

    /// @p sprite (e.g. a sprite sheet frame) on a rectangle centred on @p position.
    void drawSprite(glm::vec2 position, glm::vec2 size, const Sprite& sprite,
                    glm::vec4 tint = glm::vec4(1.0F), float rotation = 0.0F, bool flipX = false);

    /// Draws UTF-8 @p text with the top-left of its first line at @p position, @p size world units
    /// per Font::atlas().pixelHeight() (a pixel font baked at 8 px and drawn at size 16 is twice
    /// as large). '\\n' starts a new line, @p lineSpacing line heights further down. Text is
    /// batched like sprites.
    void drawText(const Font& font, std::string_view text, glm::vec2 position, float size,
                  glm::vec4 color = glm::vec4(1.0F), float lineSpacing = 1.0F);
    /// A temporary font would be destroyed before end() draws the text.
    void drawText(const Font&& font, std::string_view text, glm::vec2 position, float size,
                  glm::vec4 color = glm::vec4(1.0F), float lineSpacing = 1.0F) = delete;

    /// Line from @p from to @p to, @p thickness world units wide.
    void drawLine(glm::vec2 from, glm::vec2 to, glm::vec4 color, float thickness = 1.0F);
    /// Outline of a rectangle centred on @p position; the outline lies inside the rectangle.
    void drawRect(glm::vec2 position, glm::vec2 size, glm::vec4 color, float thickness = 1.0F);
    /// Outline of a circle, approximated by @p segments lines.
    void drawCircle(glm::vec2 center, float radius, glm::vec4 color, float thickness = 1.0F,
                    int segments = 32);

    /// Turns batching on or off. Off draws every quad with its own draw call and per-quad
    /// uniforms (the "naive" approach), which the renderer_stress benchmark compares against.
    /// Changing it in the middle of a scene draws the current batch first.
    void setBatching(bool enabled);
    /// True if quads are batched.
    [[nodiscard]] bool batching() const;

    /// Draw calls and quads since the last resetStats().
    [[nodiscard]] const RenderStats& stats() const;
    /// Sets the counters to zero, e.g. at the start of every frame.
    void resetStats();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace astra2d

/// @}
