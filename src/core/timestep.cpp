#include "astra2d/core/timestep.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace astra2d {

FixedTimestep::FixedTimestep(double step, double maxFrameTime)
    : m_step(step)
    , m_maxFrameTime(maxFrameTime)
{
    // Checked in every build, not only with asserts: a step of zero would make advance() loop
    // forever.
    if (!std::isfinite(step) || step <= 0.0) {
        throw std::invalid_argument(
            std::format("Fixed step must be positive and finite, got {}", step));
    }
    if (!std::isfinite(maxFrameTime) || maxFrameTime < step) {
        throw std::invalid_argument(std::format(
            "maxFrameTime ({}) must be finite and at least one step ({})", maxFrameTime, step));
    }
}

int FixedTimestep::advance(double frameTime)
{
    // NaN would poison the accumulator and stop the simulation for good.
    if (!std::isfinite(frameTime)) {
        frameTime = 0.0;
    }
    if (paused()) {
        // The accumulator keeps its value, so drawing stays at the same interpolated state.
        const int steps = m_stepRequested ? 1 : 0;
        m_stepRequested = false;
        m_gameTime = static_cast<double>(steps) * m_step;
        return steps;
    }
    // Scaled before clamping, so a fast-forward cannot cause the spiral of death either.
    m_gameTime = std::clamp(frameTime * m_timeScale, 0.0, m_maxFrameTime);
    m_accumulator += m_gameTime;

    int steps = 0;
    while (m_accumulator >= m_step) {
        m_accumulator -= m_step;
        ++steps;
    }
    return steps;
}

void FixedTimestep::setTimeScale(double scale)
{
    if (!std::isfinite(scale) || scale < 0.0) {
        throw std::invalid_argument(
            std::format("Time scale must be zero or positive and finite, got {}", scale));
    }
    m_timeScale = scale;
    if (!paused()) {
        m_stepRequested = false;
    }
}

} // namespace astra2d
