#pragma once

#include <glm/vec2.hpp>

/// @addtogroup renderer
/// @{

namespace astra2d {

class Texture2D;

/// A rectangle in texture coordinates. The default covers the whole texture.
struct TextureRegion {
    glm::vec2 uvMin{0.0F, 0.0F}; ///< Bottom-left corner (u, v).
    glm::vec2 uvMax{1.0F, 1.0F}; ///< Top-right corner (u, v).
};

/// A part of a texture to draw: a whole texture, or one frame of a sprite sheet.
struct Sprite {
    /// Not owned. Must stay alive until the batch it is drawn in is flushed, i.e. until
    /// Renderer2D::end(), not only until the draw call returns.
    const Texture2D* texture = nullptr;
    TextureRegion region{}; ///< Part of the texture to show.
};

/// The grid of equally sized frames in a sprite sheet image, without the texture itself.
///
/// Frames are numbered row by row, starting at the **top-left** of the image (the order in which
/// sheets are usually drawn). @p margin is the border around the grid and @p spacing the gap
/// between neighbouring frames, both in texels.
///
/// The class only does arithmetic, so it does not need OpenGL and is unit-tested directly.
class SpriteSheetLayout {
public:
    /// @throws std::invalid_argument if the sizes are not positive or no frame fits.
    SpriteSheetLayout(glm::ivec2 textureSize, glm::ivec2 frameSize, glm::ivec2 margin = {0, 0},
                      glm::ivec2 spacing = {0, 0});

    /// Number of frame columns that fit in the texture.
    [[nodiscard]] int columns() const { return m_columns; }
    /// Number of frame rows that fit in the texture.
    [[nodiscard]] int rows() const { return m_rows; }
    /// columns() * rows().
    [[nodiscard]] int frameCount() const { return m_columns * m_rows; }
    /// Size of one frame in texels.
    [[nodiscard]] glm::ivec2 frameSize() const { return m_frameSize; }

    /// Texture coordinates of frame @p index (row-major from the top-left).
    /// @throws std::out_of_range if @p index is not in [0, frameCount()).
    [[nodiscard]] TextureRegion region(int index) const;
    /// Texture coordinates of the frame at @p column, @p row (row 0 is the top row).
    /// @throws std::out_of_range if the cell is outside the grid.
    [[nodiscard]] TextureRegion region(int column, int row) const;

private:
    glm::ivec2 m_textureSize;
    glm::ivec2 m_frameSize;
    glm::ivec2 m_margin;
    glm::ivec2 m_spacing;
    int m_columns = 0;
    int m_rows = 0;
};

/// A texture split into a grid of frames.
///
/// @code
/// const SpriteSheet sheet(texture, {16, 16});
/// renderer.drawSprite(position, {1, 1}, sheet.frame(animation.frame()));
/// @endcode
class SpriteSheet {
public:
    /// @p texture is not owned and must outlive the sheet. See SpriteSheetLayout for the rest.
    SpriteSheet(const Texture2D& texture, glm::ivec2 frameSize, glm::ivec2 margin = {0, 0},
                glm::ivec2 spacing = {0, 0});
    /// A temporary texture would be destroyed at the end of the statement, leaving the sheet
    /// dangling, so it is rejected at compile time. Keep the texture in a variable or AssetCache.
    SpriteSheet(const Texture2D&& texture, glm::ivec2 frameSize, glm::ivec2 margin = {0, 0},
                glm::ivec2 spacing = {0, 0}) = delete;

    /// The sprite for frame @p index (row-major from the top-left).
    [[nodiscard]] Sprite frame(int index) const;
    /// The sprite for the frame at @p column, @p row.
    [[nodiscard]] Sprite frame(int column, int row) const;

    /// The frame grid.
    [[nodiscard]] const SpriteSheetLayout& layout() const { return m_layout; }
    /// The texture the frames come from.
    [[nodiscard]] const Texture2D& texture() const { return *m_texture; }

private:
    const Texture2D* m_texture;
    SpriteSheetLayout m_layout;
};

} // namespace astra2d

/// @}
