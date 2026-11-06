#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// How a texture is sampled between texels.
enum class TextureFilter {
    Nearest, ///< Sharp texels; the right choice for pixel art.
    Linear,  ///< Smooth interpolation; for photos and scaled-down art.
};

/// What happens to texture coordinates outside [0, 1].
enum class TextureWrap {
    ClampToEdge, ///< Repeat the edge texel.
    Repeat,      ///< Tile the texture.
};

/// Sampling settings of a Texture2D.
struct TextureSettings {
    TextureFilter filter = TextureFilter::Nearest; ///< Filtering when magnified or minified.
    TextureWrap wrap = TextureWrap::ClampToEdge;   ///< Wrapping in both directions.
};

/// A 2D RGBA8 texture. Owns the OpenGL texture object; move-only.
///
/// Images are stored with their **top row at texture coordinate v = 1**, so a quad with
/// uv (0, 0) at its bottom-left corner shows the image upright in a y-up world.
class Texture2D {
public:
    /// Creates a texture from tightly packed RGBA8 pixels, top row first (as in image files).
    /// @p pixels must hold `width * height * 4` bytes.
    Texture2D(int width, int height, std::span<const std::uint8_t> pixels,
              const TextureSettings& settings = {});

    /// A texture with storage for @p width x @p height RGBA8 texels but undefined contents, e.g.
    /// the colour target of a Framebuffer.
    /// @throws std::invalid_argument if the size is not positive.
    [[nodiscard]] static Texture2D empty(int width, int height,
                                         const TextureSettings& settings = {});

    /// Loads a PNG, JPEG, BMP or TGA file. Images without alpha are converted to RGBA.
    /// @throws std::runtime_error if the file cannot be read or decoded.
    [[nodiscard]] static Texture2D fromFile(const std::filesystem::path& path,
                                            const TextureSettings& settings = {});

    ~Texture2D();
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    /// Takes over the texture of @p other, which becomes empty.
    Texture2D(Texture2D&& other) noexcept;
    /// Releases the current texture and takes over the one of @p other.
    Texture2D& operator=(Texture2D&& other) noexcept;

    /// Binds the texture to texture unit @p slot (`GL_TEXTURE0 + slot`).
    void bind(int slot = 0) const;

    /// Width in texels.
    [[nodiscard]] int width() const { return m_width; }
    /// Height in texels.
    [[nodiscard]] int height() const { return m_height; }
    /// Size in texels.
    [[nodiscard]] glm::ivec2 size() const { return {m_width, m_height}; }
    /// The OpenGL texture name, for direct GL calls. Unique while the texture exists.
    [[nodiscard]] std::uint32_t id() const { return m_texture; }

private:
    // Validates the size and returns the pixels with the rows in OpenGL's bottom-up order.
    static std::vector<std::uint8_t> flipRows(int width, int height,
                                              std::span<const std::uint8_t> pixels);
    // Creates the OpenGL texture from bottom-row-first pixels, or with undefined contents.
    Texture2D(int width, int height, const std::uint8_t* bottomUpPixels,
              const TextureSettings& settings);

    std::uint32_t m_texture = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace astra2d

/// @}
