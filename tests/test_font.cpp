// Font baking and text layout, on the CPU (no OpenGL), with the vendored Press Start 2P font.
#include <astra2d/renderer/font.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>
#include <vector>

using astra2d::FontAtlas;

namespace {

const FontAtlas& font()
{
    // Press Start 2P is monospaced and designed for 8 px.
    static const FontAtlas atlas =
        FontAtlas::fromFile(ASTRA2D_ASSETS_DIR "/fonts/PressStart2P-Regular.ttf", 8.0F, 256);
    return atlas;
}

} // namespace

TEST(FontTests, BakesPrintableAsciiAndLatinExtended)
{
    EXPECT_TRUE(font().contains(U'A'));
    EXPECT_TRUE(font().contains(U'~'));
    EXPECT_TRUE(font().contains(U' '));
    EXPECT_GT(font().glyph(U'A').advance, 0.0F);
    EXPECT_GT(font().glyph(U'A').size.y, 0.0F);
    EXPECT_EQ(font().glyph(U' ').size, glm::vec2(0.0F)); // blank: no quad
    EXPECT_FLOAT_EQ(font().pixelHeight(), 8.0F);
}

TEST(FontTests, UnknownCharactersUseTheFallback)
{
    const auto& question = font().glyph(U'?');
    const auto& emoji = font().glyph(U'\U0001F600');
    EXPECT_EQ(emoji.advance, question.advance);
    EXPECT_EQ(emoji.region.uvMin, question.region.uvMin);
}

TEST(FontTests, MeasureUsesAdvancesAndLines)
{
    const float advance = font().glyph(U'A').advance; // monospaced
    EXPECT_EQ(font().measure(""), glm::vec2(0.0F));
    EXPECT_FLOAT_EQ(font().measure("AAA").x, 3.0F * advance);
    EXPECT_FLOAT_EQ(font().measure("AAA").y, font().lineHeight());
    const glm::vec2 twoLines = font().measure("A\nAAAA");
    EXPECT_FLOAT_EQ(twoLines.x, 4.0F * advance);
    EXPECT_FLOAT_EQ(twoLines.y, 2.0F * font().lineHeight());
    EXPECT_FLOAT_EQ(font().measure("AA", 16.0F).x, 2.0F * 2.0F * advance); // scaled 2x
    EXPECT_FLOAT_EQ(font().measure("A\nA", 0.0F, 1.5F).y, 3.0F * font().lineHeight());
}

TEST(FontTests, LayoutPlacesGlyphsLeftToRightAndLinesDownwards)
{
    const float advance = font().glyph(U'H').advance;
    const auto placed = font().layout("HH\nH", {100.0F, 200.0F}, 8.0F); // same glyph twice
    ASSERT_EQ(placed.size(), 3U);
    EXPECT_NEAR(placed[1].center.x - placed[0].center.x, advance, 1e-4F);
    EXPECT_NEAR(placed[0].center.y - placed[2].center.y, font().lineHeight(), 1e-4F); // y up
    // Everything lies below the top-left corner and to its right.
    for (const auto& glyph : placed) {
        EXPECT_GE(glyph.center.x - (glyph.size.x * 0.5F), 100.0F - 1e-4F);
        EXPECT_LE(glyph.center.y + (glyph.size.y * 0.5F), 200.0F + 1e-4F);
    }
}

TEST(FontTests, LayoutSkipsBlanksButAdvances)
{
    const auto placed = font().layout("A A", {0.0F, 0.0F}, 8.0F);
    ASSERT_EQ(placed.size(), 2U);
    EXPECT_NEAR(placed[1].center.x - placed[0].center.x, 2.0F * font().glyph(U'A').advance, 1e-4F);
}

TEST(FontTests, Utf8Decoding)
{
    const std::string_view text = "a\xC4\x8D\xE2\x82\xAC\xF0\x9F\x98\x80"; // a č € 😀
    std::size_t i = 0;
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'a');
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'č');
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'€');
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'\U0001F600');
    EXPECT_EQ(i, text.size());
}

TEST(FontTests, InvalidUtf8BecomesReplacementCharacter)
{
    const std::string_view text = "\x80"
                                  "a"
                                  "\xC4"; // stray continuation, ASCII, truncated
    std::size_t i = 0;
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'�');
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'a');
    EXPECT_EQ(FontAtlas::decodeUtf8(text, i), U'�');
    EXPECT_EQ(i, text.size());
}

TEST(FontTests, BosnianLettersAreInTheAtlasOrFallBack)
{
    // "čćđšž" lays out as five glyphs either way (in the font, or as '?').
    EXPECT_EQ(font().layout("\xC4\x8D\xC4\x87\xC4\x91\xC5\xA1\xC5\xBE", {0, 0}, 8.0F).size(), 5U);
}

TEST(FontTests, InvalidInputThrows)
{
    const std::vector<std::uint8_t> garbage(100, 0x42);
    EXPECT_THROW(FontAtlas(garbage, 8.0F), std::runtime_error);
    EXPECT_THROW((void)FontAtlas::fromFile("no/such/font.ttf", 8.0F), std::runtime_error);
    EXPECT_THROW(
        (void)FontAtlas::fromFile(ASTRA2D_ASSETS_DIR "/fonts/PressStart2P-Regular.ttf", 64.0F, 32),
        std::runtime_error); // atlas too small
}
