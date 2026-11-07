#include "astra2d/debug/frame_stats.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace astra2d {

FrameStats::FrameStats(float interval)
    : m_interval(interval)
{
    if (!(interval > 0.0F)) {
        throw std::invalid_argument("FrameStats interval must be positive");
    }
}

bool FrameStats::addFrame(float seconds, const RenderStats& render)
{
    seconds = std::max(seconds, 0.0F);
    m_min = m_frames == 0 ? seconds : std::min(m_min, seconds);
    m_max = m_frames == 0 ? seconds : std::max(m_max, seconds);
    ++m_frames;
    m_elapsed += seconds;
    m_drawCalls += render.drawCalls;
    m_quads += render.quads;
    if (m_elapsed < m_interval) {
        return false;
    }
    const auto frames = static_cast<float>(m_frames);
    m_report = {.frames = m_frames,
                .fps = frames / m_elapsed,
                .averageMs = 1000.0F * m_elapsed / frames,
                .minMs = 1000.0F * m_min,
                .maxMs = 1000.0F * m_max,
                .drawCalls = static_cast<float>(m_drawCalls) / frames,
                .quads = static_cast<float>(m_quads) / frames};
    restart();
    return true;
}

std::string FrameStats::summary() const
{
    const FrameReport& r = m_report;
    return std::format("{:.1f} FPS | frame {:.2f} ms (min {:.2f}, max {:.2f}) | {:.1f} draw calls, "
                       "{:.0f} quads per frame",
                       r.fps, r.averageMs, r.minMs, r.maxMs, r.drawCalls, r.quads);
}

void FrameStats::restart()
{
    m_elapsed = 0.0F;
    m_frames = 0;
    m_min = 0.0F;
    m_max = 0.0F;
    m_drawCalls = 0;
    m_quads = 0;
}

} // namespace astra2d
