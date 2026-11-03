#include "astra2d/core/timestep.hpp"

#include "astra2d/core/assert.hpp"

#include <algorithm>

namespace astra2d {

FixedTimestep::FixedTimestep(double step, double maxFrameTime)
    : m_step(step)
    , m_maxFrameTime(maxFrameTime)
{
    ASTRA_ASSERT(step > 0.0, "Fixed step must be positive, got {}", step);
    ASTRA_ASSERT(maxFrameTime >= step, "maxFrameTime ({}) must be at least one step ({})",
                 maxFrameTime, step);
}

int FixedTimestep::advance(double frameTime)
{
    m_accumulator += std::clamp(frameTime, 0.0, m_maxFrameTime);

    int steps = 0;
    while (m_accumulator >= m_step) {
        m_accumulator -= m_step;
        ++steps;
    }
    return steps;
}

} // namespace astra2d
