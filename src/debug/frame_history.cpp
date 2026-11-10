#include "astra2d/debug/frame_history.hpp"

#include "astra2d/core/assert.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace astra2d {

FrameHistory::FrameHistory(std::size_t capacity)
    : m_capacity(capacity)
{
    if (capacity == 0) {
        throw std::invalid_argument("FrameHistory capacity must be positive");
    }
    m_values.reserve(capacity);
}

void FrameHistory::push(float value)
{
    if (m_values.size() < m_capacity) {
        m_values.push_back(value);
        return;
    }
    m_values[m_next] = value;
    m_next = (m_next + 1) % m_capacity;
}

void FrameHistory::clear()
{
    m_values.clear();
    m_next = 0;
}

float FrameHistory::operator[](std::size_t i) const
{
    ASTRA_ASSERT(i < m_values.size(), "FrameHistory index {} out of range ({})", i,
                 m_values.size());
    return m_values[(offset() + i) % m_values.size()];
}

float FrameHistory::latest() const
{
    ASTRA_ASSERT(!m_values.empty(), "FrameHistory::latest() on an empty history");
    return (*this)[m_values.size() - 1];
}

float FrameHistory::max() const
{
    return m_values.empty() ? 0.0F : std::ranges::max(m_values);
}

float FrameHistory::mean() const
{
    if (m_values.empty()) {
        return 0.0F;
    }
    return std::accumulate(m_values.begin(), m_values.end(), 0.0F) /
           static_cast<float>(m_values.size());
}

} // namespace astra2d
