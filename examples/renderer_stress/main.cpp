// renderer_stress: many moving sprites, drawn batched or one draw call per sprite.
//
// Used for evaluation E1 ("How much does batching help?").
//
// Interactive:
//   renderer_stress [--sprites N] [--naive]
//   B toggles batching, Up/Down doubles/halves the sprite count, V toggles vsync, Escape quits.
//
// Benchmark (prints one CSV line to stdout and exits; vsync is turned off):
//   renderer_stress --sprites 10000 [--naive] --frames 600 [--warmup 120] [--header]
//
// The scene is always rendered into an off-screen framebuffer of kTargetWidth x kTargetHeight
// pixels and then copied to the window. The measured work is therefore the same whatever size the
// window manager gives the window (a tiling window manager ignores the requested size).
//
// Four times are measured per frame:
//   render_ms  begin() .. end() + glFinish(): the renderer's CPU work and the GPU drawing it causes
//   cpu_ms     begin() .. end() on the CPU, without waiting for the GPU: building the vertices and
//              submitting them and the draw calls to the driver
//   gpu_ms     the GPU's own time for the commands of begin() .. end() (a GL_TIME_ELAPSED query):
//              vertex processing, drawing and blending the pixels
//   frame_ms   from one frame to the next: also the sprite update, the copy to the window, the
//              buffer swap and the compositor; shown for context, noisier
// render_ms is about cpu_ms plus the part of gpu_ms that does not overlap with the CPU work.
//
// CSV columns: mode, sprites, frames, width, height, render_mean_ms, render_stddev_ms,
//              render_min_ms, render_max_ms, frame_mean_ms, frame_stddev_ms, draw_calls, quads,
//              cpu_mean_ms, cpu_stddev_ms, gpu_mean_ms, gpu_stddev_ms
//              (draw_calls and quads are per frame)

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/framebuffer.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>

#include "../common/example_assets.hpp"

#include <glad/gl.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <numbers>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Options {
    int sprites = 10'000;
    bool naive = false;
    int frames = 0; // 0 = interactive
    int warmup = 120;
    bool header = false;
};

int parseCount(std::string_view text, std::string_view option)
{
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0) {
        throw std::invalid_argument(
            std::format("{} expects a non-negative number, got '{}'", option, text));
    }
    return value;
}

Options parseOptions(std::span<char*> args)
{
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(std::format("{} needs a value", arg));
            }
            return args[++i];
        };
        if (arg == "--sprites") {
            options.sprites = parseCount(next(), arg);
        } else if (arg == "--naive") {
            options.naive = true;
        } else if (arg == "--frames") {
            options.frames = parseCount(next(), arg);
        } else if (arg == "--warmup") {
            options.warmup = parseCount(next(), arg);
        } else if (arg == "--header") {
            options.header = true;
        } else {
            throw std::invalid_argument(std::format("Unknown option '{}'", arg));
        }
    }
    return options;
}

constexpr int kTargetWidth = 1280;
constexpr int kTargetHeight = 720;

struct Summary {
    double mean = 0.0;
    double stddev = 0.0;
    double min = 0.0;
    double max = 0.0;
};

Summary summarize(const std::vector<double>& samples)
{
    if (samples.empty()) {
        return {};
    }
    const auto count = static_cast<double>(samples.size());
    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / count;
    double variance = 0.0;
    for (const double sample : samples) {
        variance += (sample - mean) * (sample - mean);
    }
    const auto [minIt, maxIt] = std::ranges::minmax_element(samples);
    return {.mean = mean,
            .stddev = std::sqrt(variance / std::max(count - 1.0, 1.0)),
            .min = *minIt,
            .max = *maxIt};
}

struct Sprite {
    glm::vec2 position;
    glm::vec2 velocity;
    float rotation;
    float spin;
    glm::vec4 tint;
    int frame;
};

/// A GL_TIME_ELAPSED query object (OpenGL 3.3 core): the GPU time of the commands between begin()
/// and end().
class TimerQuery {
public:
    TimerQuery() { glGenQueries(1, &m_id); }
    ~TimerQuery() { glDeleteQueries(1, &m_id); }
    TimerQuery(const TimerQuery&) = delete;
    TimerQuery& operator=(const TimerQuery&) = delete;
    TimerQuery(TimerQuery&&) = delete;
    TimerQuery& operator=(TimerQuery&&) = delete;

    void begin() const { glBeginQuery(GL_TIME_ELAPSED, m_id); }
    static void end() { glEndQuery(GL_TIME_ELAPSED); }

    /// Waits for the result if the GPU has not finished yet; after glFinish() it is ready.
    [[nodiscard]] double milliseconds() const
    {
        GLuint64 nanoseconds = 0;
        glGetQueryObjectui64v(m_id, GL_QUERY_RESULT, &nanoseconds);
        return static_cast<double>(nanoseconds) / 1.0e6;
    }

private:
    GLuint m_id = 0;
};

class RendererStress : public astra2d::Application {
public:
    explicit RendererStress(const Options& options)
        : Application({.window = {.title = "Astra2D renderer_stress",
                                  .width = 1280,
                                  .height = 720,
                                  .vsync = options.frames == 0}})
        , m_options(options)
        , m_assets(examples::assetsDirectory())
        , m_sheet(m_assets.texture("textures/sprites.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_camera(720.0F, {kTargetWidth, kTargetHeight}) // fixed: the off-screen target
        , m_target(kTargetWidth, kTargetHeight)
    {
        m_renderer.setBatching(!options.naive);
        resize(options.sprites);
        if (benchmarking()) {
            m_frameTimes.reserve(static_cast<std::size_t>(options.frames));
            m_renderTimes.reserve(static_cast<std::size_t>(options.frames));
            m_cpuTimes.reserve(static_cast<std::size_t>(options.frames));
            m_gpuTimes.reserve(static_cast<std::size_t>(options.frames));
        }
    }

protected:
    void onUpdate(float dt) override
    {
        const auto& in = input();
        if (in.isPressed(astra2d::Key::Escape)) {
            quit();
        }
        if (!benchmarking()) {
            if (in.isPressed(astra2d::Key::B)) {
                m_renderer.setBatching(!m_renderer.batching());
            }
            if (in.isPressed(astra2d::Key::Up)) {
                resize(std::min(static_cast<int>(m_sprites.size()) * 2, 1'000'000));
            }
            if (in.isPressed(astra2d::Key::Down)) {
                resize(std::max(static_cast<int>(m_sprites.size()) / 2, 1));
            }
            if (in.isPressed(astra2d::Key::V)) {
                m_vsync = !m_vsync;
                window().setVsync(m_vsync);
            }
        }

        // Bounce inside the visible area.
        const glm::vec2 min = m_camera.visibleMin();
        const glm::vec2 max = m_camera.visibleMax();
        for (Sprite& sprite : m_sprites) {
            sprite.position += sprite.velocity * dt;
            sprite.rotation += sprite.spin * dt;
            for (int axis = 0; axis < 2; ++axis) {
                if (sprite.position[axis] < min[axis] || sprite.position[axis] > max[axis]) {
                    sprite.velocity[axis] = -sprite.velocity[axis];
                    sprite.position[axis] = std::clamp(sprite.position[axis], min[axis], max[axis]);
                }
            }
        }
    }

    // The camera keeps the off-screen target's size; the window size only affects present().

    void onRender() override
    {
        const auto start = std::chrono::steady_clock::now();
        m_target.bind();
        glClearColor(0.05F, 0.05F, 0.07F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        m_renderer.resetStats();
        const auto cpuStart = std::chrono::steady_clock::now();
        m_gpuTimer.begin();
        m_renderer.begin(m_camera);
        for (const Sprite& sprite : m_sprites) {
            m_renderer.drawSprite(sprite.position, {kSize, kSize},
                                  m_frames.at(static_cast<std::size_t>(sprite.frame)), sprite.tint,
                                  sprite.rotation);
        }
        m_renderer.end();
        TimerQuery::end();
        const auto cpuEnd = std::chrono::steady_clock::now();

        // glFinish makes the measured time include the GPU work, not only command submission.
        glFinish();
        const double renderMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        const FrameTimes times{
            .render = renderMs,
            .cpu = std::chrono::duration<double, std::milli>(cpuEnd - cpuStart).count(),
            .gpu = m_gpuTimer.milliseconds()};

        m_target.blitToWindow(window().framebufferWidth(), window().framebufferHeight(), true,
                              astra2d::TextureFilter::Linear);
        measureFrame(times);
    }

private:
    static constexpr float kSize = 16.0F;

    [[nodiscard]] bool benchmarking() const { return m_options.frames > 0; }

    void resize(int count)
    {
        // A fixed seed on purpose: every benchmark run draws the same scene.
        std::mt19937 rng(12345); // NOLINT(bugprone-random-generator-seed)
        std::uniform_real_distribution<float> unit(0.0F, 1.0F);
        const glm::vec2 min = m_camera.visibleMin();
        const glm::vec2 size = m_camera.visibleSize();

        m_frames.clear();
        for (int i = 0; i < m_sheet.layout().frameCount(); ++i) {
            m_frames.push_back(m_sheet.frame(i));
        }

        m_sprites.resize(static_cast<std::size_t>(count));
        for (Sprite& sprite : m_sprites) {
            const float angle = unit(rng) * 2.0F * std::numbers::pi_v<float>;
            const float speed = 40.0F + (unit(rng) * 160.0F);
            sprite = {.position = min + (glm::vec2{unit(rng), unit(rng)} * size),
                      .velocity = speed * glm::vec2{std::cos(angle), std::sin(angle)},
                      .rotation = unit(rng) * 2.0F * std::numbers::pi_v<float>,
                      .spin = (unit(rng) - 0.5F) * 4.0F,
                      .tint = {0.6F + (0.4F * unit(rng)), 0.6F + (0.4F * unit(rng)),
                               0.6F + (0.4F * unit(rng)), 1.0F},
                      .frame = static_cast<int>(unit(rng) * static_cast<float>(m_frames.size())) %
                               static_cast<int>(m_frames.size())};
        }
        ASTRA_INFO("{} sprites", count);
    }

    struct FrameTimes {
        double render = 0.0;
        double cpu = 0.0;
        double gpu = 0.0;
    };

    void measureFrame(const FrameTimes& times)
    {
        const double renderMs = times.render;
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(now - m_lastFrame).count();
        m_lastFrame = now;
        ++m_frameNumber;

        if (benchmarking()) {
            // Frame 1 is skipped even without warm-up: its time includes the start-up.
            if (m_frameNumber > std::max(m_options.warmup, 1)) {
                m_frameTimes.push_back(frameMs);
                m_renderTimes.push_back(renderMs);
                m_cpuTimes.push_back(times.cpu);
                m_gpuTimes.push_back(times.gpu);
            }
            if (std::cmp_equal(m_frameTimes.size(), m_options.frames)) {
                printResult();
                quit();
            }
            return;
        }

        m_titleTimer += frameMs;
        m_titleRender += renderMs;
        ++m_titleFrames;
        if (m_titleTimer >= 500.0) {
            const auto& stats = m_renderer.stats();
            window().setTitle(
                std::format("renderer_stress | {} sprites | {} | render {:.2f} ms | "
                            "frame {:.2f} ms ({:.0f} FPS) | {} draw calls",
                            m_sprites.size(), m_renderer.batching() ? "batched" : "naive",
                            m_titleRender / m_titleFrames, m_titleTimer / m_titleFrames,
                            1000.0 * m_titleFrames / m_titleTimer, stats.drawCalls));
            m_titleTimer = 0.0;
            m_titleRender = 0.0;
            m_titleFrames = 0;
        }
    }

    void printResult() const
    {
        const Summary render = summarize(m_renderTimes);
        const Summary frame = summarize(m_frameTimes);
        const Summary cpu = summarize(m_cpuTimes);
        const Summary gpu = summarize(m_gpuTimes);
        if (m_options.header) {
            std::puts("mode,sprites,frames,width,height,render_mean_ms,render_stddev_ms,"
                      "render_min_ms,render_max_ms,frame_mean_ms,frame_stddev_ms,draw_calls,quads,"
                      "cpu_mean_ms,cpu_stddev_ms,gpu_mean_ms,gpu_stddev_ms");
        }
        const auto& stats = m_renderer.stats();
        std::puts(std::format("{},{},{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{},{},"
                              "{:.4f},{:.4f},{:.4f},{:.4f}",
                              m_renderer.batching() ? "batched" : "naive", m_sprites.size(),
                              m_frameTimes.size(), kTargetWidth, kTargetHeight, render.mean,
                              render.stddev, render.min, render.max, frame.mean, frame.stddev,
                              stats.drawCalls, stats.quads, cpu.mean, cpu.stddev, gpu.mean,
                              gpu.stddev)
                      .c_str());
        std::fflush(stdout);
    }

    Options m_options;
    astra2d::AssetCache m_assets;
    astra2d::SpriteSheet m_sheet;
    astra2d::OrthographicCamera m_camera;
    astra2d::Framebuffer m_target;
    astra2d::Renderer2D m_renderer;
    std::vector<astra2d::Sprite> m_frames;
    std::vector<Sprite> m_sprites;

    std::chrono::steady_clock::time_point m_lastFrame = std::chrono::steady_clock::now();
    int m_frameNumber = 0;
    std::vector<double> m_frameTimes;
    std::vector<double> m_renderTimes;
    std::vector<double> m_cpuTimes;
    std::vector<double> m_gpuTimes;
    TimerQuery m_gpuTimer;
    double m_titleTimer = 0.0;
    double m_titleRender = 0.0;
    int m_titleFrames = 0;
    bool m_vsync = true;
};

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options =
            parseOptions(std::span<char*>(argv, static_cast<std::size_t>(argc)));
        RendererStress app(options);
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
