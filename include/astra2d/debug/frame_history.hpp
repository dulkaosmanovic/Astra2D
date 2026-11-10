#pragma once

#include <cstddef>
#include <vector>

/// @addtogroup debug
/// @{

namespace astra2d {

/// The last few values of a per-frame measurement (e.g. frame times in milliseconds), oldest
/// first, for a graph. A ring buffer: once full, each new value replaces the oldest. Pure
/// arithmetic, so it is unit-tested.
class FrameHistory {
public:
    /// Keeps the last @p capacity values.
    /// @throws std::invalid_argument if @p capacity is 0.
    explicit FrameHistory(std::size_t capacity = 240);

    /// Adds @p value as the newest one.
    void push(float value);
    /// Forgets all values.
    void clear();

    /// Number of values kept (up to capacity()).
    [[nodiscard]] std::size_t size() const { return m_values.size(); }
    /// The most values kept.
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    /// True before the first push().
    [[nodiscard]] bool empty() const { return m_values.empty(); }

    /// The value @p i places after the oldest (0: the oldest).
    /// @pre i < size().
    [[nodiscard]] float operator[](std::size_t i) const;
    /// The newest value. @pre !empty().
    [[nodiscard]] float latest() const;
    /// The largest value (0 if empty).
    [[nodiscard]] float max() const;
    /// The mean of the values (0 if empty).
    [[nodiscard]] float mean() const;

    /// The storage, for a graph that reads a ring buffer (such as ImGui::PlotLines): size()
    /// values, starting at offset() and wrapping around.
    [[nodiscard]] const float* data() const { return m_values.data(); }
    /// Index in data() of the oldest value.
    [[nodiscard]] std::size_t offset() const { return m_values.size() < m_capacity ? 0 : m_next; }

private:
    std::size_t m_capacity;
    std::vector<float> m_values;
    std::size_t m_next = 0; // where the next value goes once full
};

} // namespace astra2d

/// @}
