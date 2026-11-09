#include "astra2d/renderer/font.hpp"

#include <stb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace astra2d {

namespace {

struct CodepointRange {
    char32_t first;
    char32_t last; // inclusive
};

// Printable ASCII, Latin-1 Supplement (no control characters) and Latin Extended-A.
constexpr std::array<CodepointRange, 2> kRanges{
    {{.first = 0x20, .last = 0x7E}, {.first = 0xA0, .last = 0x17F}}};
constexpr char32_t kFallback = U'?';
constexpr char32_t kReplacement = 0xFFFD;

std::vector<std::uint8_t> readBinary(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Cannot open font '{}'", path.string()));
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

FontAtlas::FontAtlas(std::span<const std::uint8_t> ttf, float pixelHeight, int atlasSize)
    : m_atlasSize(atlasSize)
    , m_pixelHeight(pixelHeight)
{
    if (!std::isfinite(pixelHeight) || pixelHeight <= 0.0F || atlasSize <= 0) {
        throw std::invalid_argument(
            std::format("Invalid font size {} or atlas size {}", pixelHeight, atlasSize));
    }

    // shortcut: stb_truetype does no bounds checks; only data shorter than the 12-byte header is
    // caught here, so fonts must be trusted game assets (a truncated real font is read out of
    // range).
    constexpr std::size_t kHeaderSize = 12;
    if (ttf.size() < kHeaderSize) {
        throw std::runtime_error("Not a TrueType font");
    }
    stbtt_fontinfo info{};
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0 || stbtt_InitFont(&info, ttf.data(), offset) == 0) {
        throw std::runtime_error("Not a TrueType font");
    }
    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    m_ascent = static_cast<float>(ascent) * scale;
    m_lineHeight = static_cast<float>(ascent - descent + lineGap) * scale;

    // Pack all ranges into one coverage bitmap, 1 texel of padding between glyphs.
    const auto texels = static_cast<std::size_t>(atlasSize) * static_cast<std::size_t>(atlasSize);
    std::vector<std::uint8_t> coverage(texels);
    stbtt_pack_context packer{};
    if (stbtt_PackBegin(&packer, coverage.data(), atlasSize, atlasSize, 0, 1, nullptr) == 0) {
        throw std::runtime_error("Cannot start packing the font atlas");
    }
    std::vector<std::vector<stbtt_packedchar>> packed;
    std::vector<stbtt_pack_range> ranges;
    packed.reserve(kRanges.size());
    for (const CodepointRange& range : kRanges) {
        packed.emplace_back(range.last - range.first + 1);
        ranges.push_back({.font_size = pixelHeight,
                          .first_unicode_codepoint_in_range = static_cast<int>(range.first),
                          .array_of_unicode_codepoints = nullptr,
                          .num_chars = static_cast<int>(packed.back().size()),
                          .chardata_for_range = packed.back().data(),
                          .h_oversample = 0,
                          .v_oversample = 0});
    }
    const int ok = stbtt_PackFontRanges(&packer, ttf.data(), 0, ranges.data(),
                                        static_cast<int>(ranges.size()));
    stbtt_PackEnd(&packer);
    if (ok == 0) {
        throw std::runtime_error(std::format("Font atlas of {0}x{0} is too small for {1} px glyphs",
                                             atlasSize, pixelHeight));
    }

    // Glyph metrics: stb_truetype is y-down from the baseline; the engine is y-up.
    const auto size = static_cast<float>(atlasSize);
    for (std::size_t r = 0; r < kRanges.size(); ++r) {
        for (std::size_t i = 0; i < packed[r].size(); ++i) {
            const stbtt_packedchar& c = packed[r][i];
            const auto codepoint = static_cast<char32_t>(kRanges.at(r).first + i);
            if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) == 0) {
                continue; // not in this font: drawn as the fallback '?'
            }
            Glyph glyph{.offset = {c.xoff, -c.yoff2},
                        .size = {c.xoff2 - c.xoff, c.yoff2 - c.yoff},
                        .advance = c.xadvance,
                        // The atlas is uploaded top row first, so v counts from the bottom.
                        .region = {.uvMin = {static_cast<float>(c.x0) / size,
                                             1.0F - (static_cast<float>(c.y1) / size)},
                                   .uvMax = {static_cast<float>(c.x1) / size,
                                             1.0F - (static_cast<float>(c.y0) / size)}}};
            if (c.x1 == c.x0 || c.y1 == c.y0) {
                glyph.size = glm::vec2(0.0F); // blank (space) or not in the font
            }
            m_glyphs.emplace(codepoint, glyph);
        }
    }
    // glyph() falls back to '?'; a font without it draws unknown characters as nothing instead of
    // throwing on every draw.
    m_glyphs.try_emplace(kFallback);

    // White RGBA with coverage as alpha, so the tint colour gives the text colour.
    m_pixels.resize(texels * 4);
    for (std::size_t i = 0; i < texels; ++i) {
        m_pixels[(i * 4) + 0] = 255;
        m_pixels[(i * 4) + 1] = 255;
        m_pixels[(i * 4) + 2] = 255;
        m_pixels[(i * 4) + 3] = coverage[i];
    }
}

FontAtlas FontAtlas::fromFile(const std::filesystem::path& path, float pixelHeight, int atlasSize)
{
    const std::vector<std::uint8_t> data = readBinary(path);
    try {
        return {data, pixelHeight, atlasSize};
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::format("{} (font '{}')", error.what(), path.string()));
    }
}

const Glyph& FontAtlas::glyph(char32_t codepoint) const
{
    const auto it = m_glyphs.find(codepoint);
    return it != m_glyphs.end() ? it->second : m_glyphs.at(kFallback);
}

char32_t FontAtlas::decodeUtf8(std::string_view text, std::size_t& index)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(index);
    std::size_t length = 0;
    char32_t codepoint = 0;
    if (lead < 0x80U) {
        ++index;
        return lead;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2;
        codepoint = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3;
        codepoint = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4;
        codepoint = lead & 0x07U;
    } else {
        ++index;
        return kReplacement; // stray continuation byte or invalid lead
    }
    if (index + length > text.size()) {
        ++index;
        return kReplacement; // truncated
    }
    for (std::size_t i = 1; i < length; ++i) {
        const unsigned char next = byte(index + i);
        if ((next & 0xC0U) != 0x80U) {
            ++index;
            return kReplacement;
        }
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    index += length;
    return codepoint;
}

glm::vec2 FontAtlas::measure(std::string_view text, float size, float lineSpacing) const
{
    if (text.empty()) {
        return glm::vec2(0.0F);
    }
    const float scale = size > 0.0F ? size / m_pixelHeight : 1.0F;
    float widest = 0.0F;
    float line = 0.0F;
    int lines = 1;
    for (std::size_t i = 0; i < text.size();) {
        const char32_t codepoint = decodeUtf8(text, i);
        if (codepoint == U'\n') {
            widest = std::max(widest, line);
            line = 0.0F;
            ++lines;
            continue;
        }
        line += glyph(codepoint).advance;
    }
    widest = std::max(widest, line);
    return glm::vec2{widest, static_cast<float>(lines) * m_lineHeight * lineSpacing} * scale;
}

std::vector<PlacedGlyph> FontAtlas::layout(std::string_view text, glm::vec2 position, float size,
                                           float lineSpacing, glm::vec2 anchor) const
{
    std::vector<PlacedGlyph> placed;
    placed.reserve(text.size());
    const float scale = size / m_pixelHeight;
    const glm::vec2 block = measure(text, size, lineSpacing);
    const float left = position.x - (anchor.x * block.x);
    const float top = position.y + ((1.0F - anchor.y) * block.y);
    // Where a line starts: the block's left edge plus anchor.x of the space the line leaves free.
    const auto lineStart = [&](std::size_t from) {
        if (anchor.x == 0.0F) {
            return left;
        }
        const std::size_t end = text.find('\n', from);
        const float width =
            measure(text.substr(from, end == std::string_view::npos ? end : end - from), size).x;
        return left + (anchor.x * (block.x - width));
    };
    glm::vec2 pen{lineStart(0), top - (m_ascent * scale)}; // on the first baseline
    for (std::size_t i = 0; i < text.size();) {
        const char32_t codepoint = decodeUtf8(text, i);
        if (codepoint == U'\n') {
            pen = {lineStart(i), pen.y - (m_lineHeight * lineSpacing * scale)};
            continue;
        }
        const Glyph& g = glyph(codepoint);
        if (g.size.x > 0.0F && g.size.y > 0.0F) {
            const glm::vec2 quadSize = g.size * scale;
            placed.push_back({.center = pen + (g.offset * scale) + (quadSize * 0.5F),
                              .size = quadSize,
                              .region = g.region});
        }
        pen.x += g.advance * scale;
    }
    return placed;
}

Font::Font(FontAtlas atlas, const TextureSettings& settings)
    : m_atlas(std::move(atlas))
    , m_texture(m_atlas.size().x, m_atlas.size().y, m_atlas.pixels(), settings)
{
}

Font Font::fromFile(const std::filesystem::path& path, float pixelHeight,
                    const TextureSettings& settings)
{
    return Font(FontAtlas::fromFile(path, pixelHeight), settings);
}

} // namespace astra2d
