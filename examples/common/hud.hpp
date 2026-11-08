#pragma once

// Shared by the examples: a dark panel behind HUD text, so the text stays readable over any scene.

#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <string_view>

namespace examples {

inline constexpr glm::vec4 kPanelFill{0.04F, 0.05F, 0.09F, 0.82F};
inline constexpr glm::vec4 kPanelBorder{0.36F, 0.42F, 0.62F, 0.9F};
inline constexpr glm::vec4 kPanelText{0.92F, 0.94F, 1.0F, 1.0F};

/// A panel covering @p min to @p max: a translucent fill and a @p border thick outline inside it.
inline void drawPanel(astra2d::Renderer2D& renderer, glm::vec2 min, glm::vec2 max,
                      float border = 2.0F, glm::vec4 borderColor = kPanelBorder)
{
    const glm::vec2 center = (min + max) * 0.5F;
    const glm::vec2 size = max - min;
    renderer.drawQuad(center, size, kPanelFill);
    if (border > 0.0F) {
        renderer.drawRect(center, size, borderColor, border);
    }
}

/// @p text (lines separated by '\n') with the top-left of its first line at @p topLeft, on a panel
/// that leaves half a line of space around it. Returns the panel's size.
inline glm::vec2 drawTextPanel(astra2d::Renderer2D& renderer, const astra2d::Font& font,
                               std::string_view text, glm::vec2 topLeft, float size,
                               glm::vec4 color = kPanelText, float lineSpacing = 1.0F)
{
    const float padding = size * 0.75F;
    const glm::vec2 extent = font.atlas().measure(text, size, lineSpacing);
    // The last line's spacing is space below the text that the panel does not need.
    const float height = extent.y - (size * (lineSpacing - 1.0F));
    const glm::vec2 min{topLeft.x - padding, topLeft.y - height - padding};
    const glm::vec2 max{topLeft.x + extent.x + padding, topLeft.y + padding};
    drawPanel(renderer, min, max, std::max(1.0F, size / 8.0F));
    renderer.drawText(font, text, topLeft, size, color, lineSpacing);
    return max - min;
}

} // namespace examples
