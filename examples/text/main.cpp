// text: fonts and text rendering.
//
// A Font bakes the glyphs of a TrueType file into a texture once (FontAtlas); drawText() then
// draws each character as a textured quad, batched with everything else. Text can live in the
// world (it moves and zooms with the camera) or on the screen (a HUD, using a screen-space camera).
//
// Controls:
//   mouse wheel   zoom the world (the HUD stays the same size)
//   Escape        quit

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>

#include "../common/example_assets.hpp"
#include "../common/hud.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <string_view>

namespace {

// Press Start 2P is a pixel font designed for 8 px; drawing it at whole multiples of 8 keeps it
// sharp.
constexpr float kFontPixels = 8.0F;
// TextStyle::anchor for text centred on its position.
constexpr glm::vec2 kCentre{0.5F, 0.5F};

class Text : public astra2d::Application {
public:
    Text()
        : Application({.window = {.title = "Astra2D: text", .width = 960, .height = 540}})
        , m_assets(examples::assetsDirectory())
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"),
                                         kFontPixels))
        , m_camera(540.0F, {window().framebufferWidth(), window().framebufferHeight()})
    {
        m_camera.fitArea({560.0F, 380.0F});
    }

protected:
    void onUpdate(float dt) override
    {
        m_time += dt;
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
        if (input().scrollDelta().y != 0.0F) {
            const float zoom = m_camera.zoom() * std::pow(1.1F, input().scrollDelta().y);
            m_camera.setZoom(std::clamp(zoom, 0.25F, 4.0F));
        }
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        astra2d::Renderer2D::clear({0.07F, 0.07F, 0.10F, 1.0F});

        // World text: position and size in world units.
        m_renderer.begin(m_camera);
        // anchor {0.5, 0.5}: the centre of the text is at the position. A shadow is a second,
        // offset copy drawn first.
        m_renderer.drawText(m_font, "ASTRA2D", {0.0F, 140.0F},
                            {.size = 48.0F,
                             .color = {1.0F, 0.85F, 0.3F, 1.0F},
                             .anchor = kCentre,
                             .shadowOffset = {4.0F, -4.0F}});
        m_renderer.drawText(m_font, "text is batched like sprites", {0.0F, 70.0F},
                            {.size = 16.0F, .anchor = kCentre});

        // UTF-8 works for the baked ranges (ASCII, Latin-1, Latin Extended-A).
        m_renderer.drawText(m_font, "Čćđšž ÀÉÎÕÜ ñ ß", {0.0F, 20.0F},
                            {.size = 16.0F, .color = {0.6F, 0.9F, 1.0F, 1.0F}, .anchor = kCentre});

        // Several lines with extra spacing between them, each centred; anchor {0.5, 1} hangs the
        // block from the middle of its top edge.
        m_renderer.drawText(m_font, "line one\nsecond line\nthe third line", {0.0F, -8.0F},
                            {.size = 16.0F,
                             .color = {0.8F, 0.8F, 0.8F, 1.0F},
                             .lineSpacing = 1.8F,
                             .anchor = {0.5F, 1.0F}});

        // Text is coloured per call, so a wave of colours is one call per character.
        const std::string_view wave = "colour per character";
        const float waveWidth = m_font.atlas().measure(wave, 16.0F).x;
        const float glyph = waveWidth / static_cast<float>(wave.size()); // monospaced font
        for (std::size_t i = 0; i < wave.size(); ++i) {
            const float phase = (m_time * 3.0F) + (static_cast<float>(i) * 0.4F);
            const glm::vec4 color{0.5F + (0.5F * std::sin(phase)), 0.6F,
                                  0.5F + (0.5F * std::cos(phase)), 1.0F};
            const float y = -150.0F + (4.0F * std::sin(phase));
            const float x = (-waveWidth * 0.5F) + (static_cast<float>(i) * glyph);
            m_renderer.drawText(m_font, wave.substr(i, 1), {x, y}, 16.0F, color);
        }
        m_renderer.end();

        // Screen text (HUD): one world unit is one pixel, (0, 0) the bottom-left corner.
        const int width = std::max(window().framebufferWidth(), 1);
        const int height = std::max(window().framebufferHeight(), 1);
        m_renderer.begin(astra2d::OrthographicCamera::screenSpace(width, height));
        const std::string hud = std::format("zoom {:.2f} (wheel)   {} draw calls last frame",
                                            m_camera.zoom(), m_drawCalls);
        // Small, in the bottom-right corner: the demo text fills the middle of the window.
        // (measure() gives the size of a text block, here to right-align the panel.)
        examples::drawTextPanel(
            m_renderer, m_font, hud,
            {static_cast<float>(width) - m_font.atlas().measure(hud, 8.0F).x - 14.0F, 22.0F}, 8.0F);
        m_renderer.end();

        m_drawCalls = m_renderer.stats().drawCalls;
        m_renderer.resetStats();
    }

private:
    astra2d::AssetCache m_assets;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;
    float m_time = 0.0F;
    std::uint32_t m_drawCalls = 0;
};

} // namespace

int main()
{
    try {
        Text app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
