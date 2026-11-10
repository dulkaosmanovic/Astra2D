#pragma once

// The profiler section of DebugOverlay: the Profiler's call tree as an ImGui table.

namespace astra2d {
class Profiler;
}

namespace astra2d::detail {

/// Draws the call tree of @p profiler's last frames into the current ImGui window: per scope the
/// average time, its share of the frame, and the calls in the last frame.
void drawProfilerTable(const Profiler& profiler);

} // namespace astra2d::detail
