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
    m_accumulator += std::clamp(frameTime, 0.0, m_maxFrameTime);

    int steps = 0;
    while (m_accumulator >= m_step) {
        m_accumulator -= m_step;
        ++steps;
    }
    return steps;
}

} // namespace astra2d
