#pragma once

// Decides which key or mouse button events reach the game while ImGui is on screen (see
// ImGuiLayer): a press only while ImGui does not want the device, a release only if its press got
// through, so the game never sees a key stuck down or a release without a press.

#include <bitset>
#include <cstddef>

namespace astra2d::detail {

/// For @p N codes (keys or mouse buttons, numbered from 0).
template <std::size_t N>
class InputGate {
public:
    /// Whether a press of @p code reaches the game, given whether ImGui wants the device now.
    /// Codes outside [0, N) (unknown keys) pass unless ImGui wants the device; they are not
    /// tracked.
    bool press(int code, bool imguiWants)
    {
        if (inRange(code)) {
            m_down.set(static_cast<std::size_t>(code), !imguiWants);
        }
        return !imguiWants;
    }

    /// Whether a release of @p code reaches the game: only if its press did.
    bool release(int code, bool imguiWants)
    {
        if (!inRange(code)) {
            return !imguiWants;
        }
        const bool passed = m_down.test(static_cast<std::size_t>(code));
        m_down.reset(static_cast<std::size_t>(code));
        return passed;
    }

    /// Whether a key repeat of @p code reaches the game: while its press is through.
    [[nodiscard]] bool repeat(int code, bool imguiWants) const
    {
        return inRange(code) ? m_down.test(static_cast<std::size_t>(code)) : !imguiWants;
    }

private:
    [[nodiscard]] static bool inRange(int code)
    {
        return code >= 0 && static_cast<std::size_t>(code) < N;
    }

    std::bitset<N> m_down; // presses that reached the game and were not released yet
};

} // namespace astra2d::detail
