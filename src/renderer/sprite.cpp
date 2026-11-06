#include "astra2d/renderer/sprite.hpp"
#include "astra2d/renderer/animation.hpp"
#include "astra2d/renderer/texture.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace astra2d {

// ---------------------------------------------------------------------------------------------
// SpriteSheetLayout

SpriteSheetLayout::SpriteSheetLayout(glm::ivec2 textureSize, glm::ivec2 frameSize,
                                     glm::ivec2 margin, glm::ivec2 spacing)
    : m_textureSize(textureSize)
    , m_frameSize(frameSize)
    , m_margin(margin)
    , m_spacing(spacing)
{
    if (textureSize.x <= 0 || textureSize.y <= 0 || frameSize.x <= 0 || frameSize.y <= 0) {
        throw std::invalid_argument(
            std::format("Sprite sheet sizes must be positive (texture {}x{}, frame {}x{})",
                        textureSize.x, textureSize.y, frameSize.x, frameSize.y));
    }
    if (margin.x < 0 || margin.y < 0 || spacing.x < 0 || spacing.y < 0) {
        throw std::invalid_argument("Sprite sheet margin and spacing must not be negative");
    }

    // n frames need n * frame + (n - 1) * spacing texels inside the margins.
    const auto fit = [](int available, int frame, int gap) {
        return available < frame ? 0 : ((available - frame) / (frame + gap)) + 1;
    };
    m_columns = fit(textureSize.x - (2 * margin.x), frameSize.x, spacing.x);
    m_rows = fit(textureSize.y - (2 * margin.y), frameSize.y, spacing.y);
    if (frameCount() == 0) {
        throw std::invalid_argument(
            std::format("No {}x{} frame fits in a {}x{} texture with margin {}x{}", frameSize.x,
                        frameSize.y, textureSize.x, textureSize.y, margin.x, margin.y));
    }
}

TextureRegion SpriteSheetLayout::region(int index) const
{
    if (index < 0 || index >= frameCount()) {
        throw std::out_of_range(
            std::format("Sprite sheet frame {} out of range [0, {})", index, frameCount()));
    }
    return region(index % m_columns, index / m_columns);
}

TextureRegion SpriteSheetLayout::region(int column, int row) const
{
    if (column < 0 || column >= m_columns || row < 0 || row >= m_rows) {
        throw std::out_of_range(std::format("Sprite sheet cell ({}, {}) outside {}x{} grid", column,
                                            row, m_columns, m_rows));
    }
    // Texel rectangle, measured from the top-left of the image.
    const glm::vec2 topLeft(m_margin + glm::ivec2(column, row) * (m_frameSize + m_spacing));
    const glm::vec2 bottomRight = topLeft + glm::vec2(m_frameSize);
    const glm::vec2 size(m_textureSize);

    // Textures are uploaded with the image's top row at v = 1, so v counts from the bottom.
    return {.uvMin = {topLeft.x / size.x, 1.0F - (bottomRight.y / size.y)},
            .uvMax = {bottomRight.x / size.x, 1.0F - (topLeft.y / size.y)}};
}

// ---------------------------------------------------------------------------------------------
// SpriteSheet

SpriteSheet::SpriteSheet(const Texture2D& texture, glm::ivec2 frameSize, glm::ivec2 margin,
                         glm::ivec2 spacing)
    : m_texture(&texture)
    , m_layout(texture.size(), frameSize, margin, spacing)
{
}

Sprite SpriteSheet::frame(int index) const
{
    return {.texture = m_texture, .region = m_layout.region(index)};
}

Sprite SpriteSheet::frame(int column, int row) const
{
    return {.texture = m_texture, .region = m_layout.region(column, row)};
}

// ---------------------------------------------------------------------------------------------
// AnimationPlayer

void AnimationPlayer::play(const SpriteAnimation& animation, bool restart)
{
    if (animation.frames.empty()) {
        throw std::invalid_argument("Sprite animation has no frames");
    }
    if (!std::isfinite(animation.frameDuration) || animation.frameDuration <= 0.0F) {
        throw std::invalid_argument(std::format("Sprite animation frame duration must be positive, "
                                                "got {}",
                                                animation.frameDuration));
    }
    if (&animation == m_animation && !restart) {
        return;
    }
    m_animation = &animation;
    m_time = 0.0F;
    m_index = 0;
    m_finished = false;
}

void AnimationPlayer::stop()
{
    m_animation = nullptr;
    m_time = 0.0F;
    m_index = 0;
    m_finished = false;
}

void AnimationPlayer::update(float deltaTime)
{
    if (m_animation == nullptr || m_finished || !std::isfinite(deltaTime) || deltaTime <= 0.0F) {
        return;
    }

    const auto count = static_cast<int>(m_animation->frames.size());
    const float length = m_animation->frameDuration * static_cast<float>(count);
    m_time += deltaTime * m_speed;

    if (m_animation->loop) {
        // fmod instead of a loop, so a long pause does not step through many cycles.
        m_time = std::fmod(m_time, length);
    } else if (m_time >= length) {
        m_time = length;
        m_index = count - 1;
        m_finished = true;
        return;
    }
    m_index = std::clamp(static_cast<int>(m_time / m_animation->frameDuration), 0, count - 1);
}

int AnimationPlayer::frame() const
{
    if (m_animation == nullptr) {
        return 0;
    }
    return m_animation->frames[static_cast<std::size_t>(m_index)];
}

void AnimationPlayer::setSpeed(float speed)
{
    m_speed = std::isfinite(speed) ? std::max(speed, 0.0F) : 0.0F;
}

} // namespace astra2d
