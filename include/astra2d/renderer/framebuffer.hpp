#pragma once

#include "astra2d/renderer/texture.hpp"

#include <glm/vec2.hpp>

#include <cstdint>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// An off-screen render target: drawing goes into a texture instead of the window.
///
/// Use it to render at a fixed internal resolution (pixel-art games, benchmarks), or as the input
/// of post-processing effects:
///
/// @code
/// m_scene.bind();                       // draw into the framebuffer
/// renderer.begin(camera); ...; renderer.end();
/// Framebuffer::bindDefault(window().framebufferWidth(), window().framebufferHeight());
/// m_postProcess.apply(m_scene.colorTexture(), m_effect);   // draw it to the window
/// @endcode
///
/// Owns the OpenGL framebuffer object and its colour texture (RGBA8, no depth); move-only.
class Framebuffer {
public:
    /// @param width         Width in pixels (> 0).
    /// @param height        Height in pixels (> 0).
    /// @param colorSettings Sampling of the colour texture when it is drawn afterwards.
    /// @throws std::invalid_argument if the size is not positive.
    /// @throws std::runtime_error if the driver reports the framebuffer as incomplete.
    Framebuffer(int width, int height, const TextureSettings& colorSettings = {});
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    /// Takes over the framebuffer of @p other, which becomes empty.
    Framebuffer(Framebuffer&& other) noexcept;
    /// Releases the current framebuffer and takes over the one of @p other.
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    /// Directs drawing into this framebuffer and sets the viewport to its size.
    void bind() const;
    /// Directs drawing back to the window and sets the viewport to @p width x @p height.
    static void bindDefault(int width, int height);

    /// Changes the size. The contents are lost; the colour texture is a new OpenGL texture (the
    /// colorTexture() object stays the same).
    void resize(int width, int height);

    /// Copies the image to the window's framebuffer, scaled to fill a @p windowWidth x
    /// @p windowHeight window. With @p keepAspect the image keeps its aspect ratio and the rest is
    /// cleared to black (letterboxing). Nearest filtering keeps pixel art sharp; it does not
    /// change the current framebuffer binding afterwards beyond binding the window.
    void blitToWindow(int windowWidth, int windowHeight, bool keepAspect = true,
                      TextureFilter filter = TextureFilter::Nearest) const;

    /// The texture that receives the colour output.
    [[nodiscard]] const Texture2D& colorTexture() const { return m_color; }
    /// Width in pixels.
    [[nodiscard]] int width() const { return m_color.width(); }
    /// Height in pixels.
    [[nodiscard]] int height() const { return m_color.height(); }
    /// Size in pixels.
    [[nodiscard]] glm::ivec2 size() const { return m_color.size(); }
    /// The OpenGL framebuffer name, for direct GL calls.
    [[nodiscard]] std::uint32_t id() const { return m_framebuffer; }

private:
    void attach();

    Texture2D m_color;
    TextureSettings m_colorSettings;
    std::uint32_t m_framebuffer = 0;
};

} // namespace astra2d

/// @}
