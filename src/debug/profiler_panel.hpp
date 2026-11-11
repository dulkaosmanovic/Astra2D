#pragma once

// The profiler parts of DebugOverlay: the Profiler's call tree as an ImGui table, and the frame
// split into its phases as a bar.

namespace astra2d {
class Profiler;
}

namespace astra2d::detail {

/// Draws the call tree of @p profiler's last frames into the current ImGui window: per scope the
/// average time, its self time (not in its children), its recent peak, its share of the frame,
/// and the calls in the last frame.
void drawProfilerTable(const Profiler& profiler);

/// Draws one bar split into the frame's top-level scopes (update, render, ...), on the scale of
/// @p budgetMs (e.g. 16.67 for 60 FPS) with a mark at the budget; hovering a part names it.
void drawFrameBreakdown(const Profiler& profiler, double budgetMs);

} // namespace astra2d::detail
