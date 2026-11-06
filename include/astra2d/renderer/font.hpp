#pragma once

#include "astra2d/renderer/sprite.hpp"
#include "astra2d/renderer/texture.hpp"

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// Metrics and atlas position of one character, in pixels of the baked size (y up).
struct Glyph {
    glm::vec2 offset{0.0F}; ///< From the pen position on the baseline to the quad's bottom-left.
    glm::vec2 size{0.0F};   ///< Size of the glyph's quad; (0, 0) for blank characters (space).
    float advance = 0.0F;   ///< How far the pen moves after this character.
    TextureRegion region{}; ///< Where the glyph is in the atlas texture.
};

/// One character placed by FontAtlas::layout(), ready to be drawn as a quad.
struct PlacedGlyph {
    glm::vec2 center;     ///< Centre of the quad.
    glm::vec2 size;       ///< Size of the quad.
    TextureRegion region; ///< Where the glyph is in the atlas texture.
};

/// A TrueType font rasterized ("baked") into an image at one pixel size, with the metrics needed
/// to lay out text. CPU only: Font adds the texture. Uses stb_truetype.
///
/// Contains printable ASCII, Latin-1 and Latin Extended-A (U+0020 to U+007E and U+00A0 to
/// U+017F), which covers most European languages (for example č, ć, đ, š, ž), as far as the font
/// has them. Other characters are drawn as '?'. Text is UTF-8.
class FontAtlas {
public:
    /// Bakes the font in @p ttf (the contents of a .ttf file) at @p pixelHeight pixels from the
    /// highest ascender to the lowest descender, into a square atlas of @p atlasSize texels.
    /// @throws std::runtime_error if the data is not a usable font or the atlas is too small.
    FontAtlas(std::span<const std::uint8_t> ttf, float pixelHeight, int atlasSize = 512);
    /// Reads a .ttf file and bakes it; see the constructor.
    /// @throws std::runtime_error if the file cannot be read or baked.
    [[nodiscard]] static FontAtlas fromFile(const std::filesystem::path& path, float pixelHeight,
                                            int atlasSize = 512);

    /// Glyph for @p codepoint, or for '?' if the atlas does not contain it.
    [[nodiscard]] const Glyph& glyph(char32_t codepoint) const;
    /// True if the atlas contains @p codepoint.
    [[nodiscard]] bool contains(char32_t codepoint) const { return m_glyphs.contains(codepoint); }

    /// The size the font was baked at.
    [[nodiscard]] float pixelHeight() const { return m_pixelHeight; }
    /// Distance from the baseline to the top of the tallest glyphs, in pixels.
    [[nodiscard]] float ascent() const { return m_ascent; }
    /// Distance from one baseline to the next, in pixels.
    [[nodiscard]] float lineHeight() const { return m_lineHeight; }

    /// Size of @p text drawn @p size units high (default: the baked size): the widest line by
    /// the number of lines times the line height times @p lineSpacing.
    [[nodiscard]] glm::vec2 measure(std::string_view text, float size = 0.0F,
                                    float lineSpacing = 1.0F) const;

    /// Places the characters of @p text with the top-left of the first line at @p topLeft, scaled
    /// so that pixelHeight() becomes @p size units (y up; '\\n' starts a new line, lineHeight()
    /// times @p lineSpacing further down). Blank characters produce no quad.
    [[nodiscard]] std::vector<PlacedGlyph> layout(std::string_view text, glm::vec2 topLeft,
                                                  float size, float lineSpacing = 1.0F) const;

    /// Width and height of the atlas image.
    [[nodiscard]] glm::ivec2 size() const { return {m_atlasSize, m_atlasSize}; }
    /// The atlas as RGBA8, top row first: white, with the glyph coverage in alpha, so text takes
    /// the tint colour when drawn.
    [[nodiscard]] std::span<const std::uint8_t> pixels() const { return m_pixels; }

    /// Decodes the UTF-8 character starting at @p index and moves @p index past it. Invalid
    /// sequences decode to U+FFFD and skip one byte.
    [[nodiscard]] static char32_t decodeUtf8(std::string_view text, std::size_t& index);

private:
    std::unordered_map<char32_t, Glyph> m_glyphs;
    std::vector<std::uint8_t> m_pixels;
    int m_atlasSize;
    float m_pixelHeight;
    float m_ascent = 0.0F;
    float m_lineHeight = 0.0F;
};

/// A FontAtlas with its texture, ready for Renderer2D::drawText().
///
/// @code
/// const Font font = Font::fromFile("assets/fonts/PressStart2P-Regular.ttf", 8.0F);
/// renderer.drawText(font, "Score: 120", {16, 700}, 16.0F, {1, 1, 1, 1});
/// @endcode
///
/// Pixel fonts look sharp when baked at their design size, drawn at whole multiples of it and
/// placed on whole pixels, with TextureFilter::Nearest (the default). Smooth fonts look better
/// baked at the size they are drawn at, with TextureFilter::Linear.
class Font {
public:
    /// Uploads @p atlas as a texture. Needs an OpenGL context.
    explicit Font(FontAtlas atlas, const TextureSettings& settings = {});
    /// Bakes the .ttf file at @p path and uploads it; see FontAtlas.
    /// @throws std::runtime_error if the file cannot be read or baked.
    [[nodiscard]] static Font fromFile(const std::filesystem::path& path, float pixelHeight,
                                       const TextureSettings& settings = {});

    /// Glyph metrics and layout.
    [[nodiscard]] const FontAtlas& atlas() const { return m_atlas; }
    /// The atlas texture.
    [[nodiscard]] const Texture2D& texture() const { return m_texture; }

private:
    FontAtlas m_atlas;
    Texture2D m_texture;
};

} // namespace astra2d

/// @}
