// post_processing: render targets and full-screen effects.
//
// The scene is drawn into a Framebuffer (a texture) instead of the window. A full-screen effect,
// a fragment shader, then draws that texture to the window, changing every pixel on the way.
// Rendering into a smaller framebuffer and scaling it up gives a chunky pixel-art look.
//
// Controls:
//   1  no effect (plain copy)       3  wave distortion (inline shader)
//   2  grayscale (inline shader)    4  CRT (examples/assets/shaders/crt.frag, hot reloaded)
//   Tab                             toggle low-resolution rendering (1/4 of the window)
//   Escape                          quit

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/framebuffer.hpp>
#include <astra2d/renderer/post_process.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/shader.hpp>
#include <astra2d/renderer/sprite.hpp>

#include "../common/example_assets.hpp"

#include <glad/gl.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <format>

namespace {

// Effects get uTexture (the scene), uResolution (output size in pixels), uTime (seconds) and the
// texture coordinate vUv from the built-in full-screen vertex shader.
constexpr const char* kGrayscale = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTexture;
out vec4 fragColor;

void main()
{
    vec3 color = texture(uTexture, vUv).rgb;
    fragColor = vec4(vec3(dot(color, vec3(0.299, 0.587, 0.114))), 1.0);
}
)";

constexpr const char* kWave = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTexture;
uniform float uTime;
out vec4 fragColor;

void main()
{
    vec2 uv = vUv + vec2(sin(vUv.y * 20.0 + uTime * 3.0), cos(vUv.x * 14.0 + uTime * 2.0)) * 0.006;
    fragColor = texture(uTexture, uv);
}
)";

enum class Effect { None, Grayscale, Wave, Crt };

constexpr int kLowResDivisor = 4;

class PostProcessing : public astra2d::Application {
public:
    PostProcessing()
        : Application(
              {.window = {.title = "Astra2D: post-processing", .width = 960, .height = 540}})
        , m_assets(examples::assetsDirectory())
        , m_sheet(m_assets.texture("textures/sprites.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_camera(270.0F, windowSize())
        , m_scene(windowSize().x, windowSize().y)
        , m_grayscale(astra2d::PostProcess::createEffect(kGrayscale))
        , m_wave(astra2d::PostProcess::createEffect(kWave))
    {
        m_camera.fitArea({320.0F, 260.0F});
        (void)m_assets.effect("shaders/crt.frag"); // report a broken shader at start-up
    }

protected:
    void onUpdate(float dt) override
    {
        m_time += dt;
        const auto& in = input();
        if (in.isPressed(astra2d::Key::Escape)) {
            quit();
        }
        if (in.isPressed(astra2d::Key::Num1)) {
            m_effect = Effect::None;
        } else if (in.isPressed(astra2d::Key::Num2)) {
            m_effect = Effect::Grayscale;
        } else if (in.isPressed(astra2d::Key::Num3)) {
            m_effect = Effect::Wave;
        } else if (in.isPressed(astra2d::Key::Num4)) {
            m_effect = Effect::Crt;
        }
        if (in.isPressed(astra2d::Key::Tab)) {
            m_lowRes = !m_lowRes;
            resizeScene();
        }
        m_reloadTimer += dt;
        if (m_reloadTimer > 0.5F) {
            m_reloadTimer = 0.0F;
            m_assets.reloadChanged(); // edit crt.frag while this runs
        }
        updateTitle();
    }

    void onResize(int width, int height) override
    {
        m_camera.setViewportSize(width, height);
        resizeScene();
    }

    void onRender() override
    {
        // 1. Draw the scene into the framebuffer. bind() also sets the viewport to its size.
        m_scene.bind();
        glClearColor(0.12F, 0.10F, 0.22F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        drawScene();

        // 2. Draw the framebuffer's texture to the window through the chosen effect.
        const glm::ivec2 size = windowSize();
        astra2d::Framebuffer::bindDefault(size.x, size.y);
        switch (m_effect) {
        case Effect::None:
            m_postProcess.copy(m_scene.colorTexture());
            break;
        case Effect::Grayscale:
            m_postProcess.apply(m_scene.colorTexture(), m_grayscale);
            break;
        case Effect::Wave:
            m_postProcess.apply(m_scene.colorTexture(), m_wave, m_time);
            break;
        case Effect::Crt:
            m_postProcess.apply(m_scene.colorTexture(), m_assets.effect("shaders/crt.frag"),
                                m_time);
            break;
        }
    }

private:
    [[nodiscard]] glm::ivec2 windowSize() const
    {
        return {std::max(window().framebufferWidth(), 1),
                std::max(window().framebufferHeight(), 1)};
    }

    // The camera shows the same part of the world at any framebuffer size, so changing the
    // resolution only changes how fine the picture is.
    void resizeScene()
    {
        const glm::ivec2 size = windowSize() / (m_lowRes ? kLowResDivisor : 1);
        m_scene.resize(std::max(size.x, 1), std::max(size.y, 1));
    }

    void drawScene()
    {
        m_renderer.begin(m_camera);
        for (int i = 0; i < 12; ++i) {
            const float angle = (m_time * 0.6F) + (static_cast<float>(i) * 0.5236F);
            const glm::vec2 position{std::cos(angle) * 110.0F, std::sin(angle) * 90.0F};
            const glm::vec4 color{0.5F + (0.5F * std::sin(angle)), 0.4F,
                                  0.5F + (0.5F * std::cos(angle)), 1.0F};
            m_renderer.drawQuad(position, {28.0F, 28.0F}, color, angle * 2.0F);
        }
        const int coinFrame = 4 + (static_cast<int>(m_time * 8.0F) % 4);
        m_renderer.drawSprite({0.0F, 0.0F}, {64.0F, 64.0F}, m_sheet.frame(coinFrame));
        m_renderer.drawCircle({0.0F, 0.0F}, 60.0F, {1.0F, 1.0F, 1.0F, 0.8F}, 2.0F, 48);
        m_renderer.end();
    }

    void updateTitle()
    {
        constexpr std::array<const char*, 4> kNames{"none", "grayscale", "wave", "CRT"};
        const int index = static_cast<int>(m_effect);
        if (index != m_titleEffect || m_lowRes != m_titleLowRes) {
            m_titleEffect = index;
            m_titleLowRes = m_lowRes;
            window().setTitle(std::format("Astra2D: post-processing ({}, {} resolution)",
                                          kNames.at(static_cast<std::size_t>(index)),
                                          m_lowRes ? "low" : "full"));
        }
    }

    astra2d::AssetCache m_assets;
    astra2d::SpriteSheet m_sheet;
    astra2d::OrthographicCamera m_camera;
    astra2d::Framebuffer m_scene;
    astra2d::Renderer2D m_renderer;
    astra2d::PostProcess m_postProcess;
    astra2d::Shader m_grayscale;
    astra2d::Shader m_wave;

    Effect m_effect = Effect::Crt;
    bool m_lowRes = false;
    int m_titleEffect = -1;
    bool m_titleLowRes = false;
    float m_time = 0.0F;
    float m_reloadTimer = 0.0F;
};

} // namespace

int main()
{
    try {
        PostProcessing app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
