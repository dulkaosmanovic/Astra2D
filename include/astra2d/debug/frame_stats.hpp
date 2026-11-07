#pragma once

#include "astra2d/renderer/renderer2d.hpp"

#include <cstdint>
#include <string>

/// @addtogroup debug
/// @{

namespace astra2d {

/// The numbers of one FrameStats report interval.
struct FrameReport {
    int frames = 0;         ///< Frames in the interval.
    float fps = 0.0F;       ///< Frames per second.
    float averageMs = 0.0F; ///< Mean frame time in milliseconds.
    float minMs = 0.0F;     ///< Shortest frame.
    float maxMs = 0.0F;     ///< Longest frame (spikes show here, not in the mean).
    float drawCalls = 0.0F; ///< Draw calls per frame, on average.
    float quads = 0.0F;     ///< Quads per frame, on average.
};

/// Measures frames per second, frame times and render statistics over intervals, e.g. to log them
/// once a second. Pure arithmetic, so it is unit-tested.
///
/// @code
/// // in onRender(), after drawing:
/// if (m_frameStats.addFrame(m_frameTime, m_renderer.stats())) {
///     ASTRA_INFO("{}", m_frameStats.summary()); // "60.0 FPS | frame 16.67 ms (min ...)..."
/// }
/// m_renderer.resetStats();
/// @endcode
class FrameStats {
public:
    /// Reports every @p interval seconds (at least one frame each).
    /// @throws std::invalid_argument if @p interval is not positive.
    explicit FrameStats(float interval = 1.0F);

    /// Adds a frame that took @p seconds and drew @p render. Returns true when an interval has
    /// ended: report() and summary() then describe it.
    bool addFrame(float seconds, const RenderStats& render = {});
    /// The last finished interval (all zero before the first one).
    [[nodiscard]] const FrameReport& report() const { return m_report; }
    /// The last report as one line of text, for the log or the window title.
    [[nodiscard]] std::string summary() const;
    /// Forgets the interval in progress, e.g. after a pause, so it does not distort the next
    /// report.
    void restart();

private:
    float m_interval;
    float m_elapsed = 0.0F;
    int m_frames = 0;
    float m_min = 0.0F;
    float m_max = 0.0F;
    std::uint64_t m_drawCalls = 0;
    std::uint64_t m_quads = 0;
    FrameReport m_report;
};

} // namespace astra2d

/// @}
