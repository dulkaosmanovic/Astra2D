#pragma once

// The overlay's frame time graph: one bar per frame, coloured by how it fits the frame budget.

namespace astra2d {
class FrameHistory;
}

namespace astra2d::detail {

/// The colour of a frame that took @p ms against a budget of @p budgetMs: good within it (a 5 %
/// tolerance for timer noise), a warning up to twice it, bad beyond (ImGui ABGR).
[[nodiscard]] unsigned budgetColor(float ms, float budgetMs);

/// Draws @p history (milliseconds per frame, oldest first) as bars across the available width,
/// the newest at the right, with lines at the 60 and 30 FPS budgets and the latest time in the
/// corner. Hovering a bar shows its time.
void drawFrameGraph(const FrameHistory& history, float budgetMs);

} // namespace astra2d::detail
