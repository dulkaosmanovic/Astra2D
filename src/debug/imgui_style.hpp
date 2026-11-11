#pragma once

// The look of the engine's ImGui windows: a dark navy theme with the Astra2D blue as its accent,
// rounded corners, and a vector font scaled for the monitor.

namespace astra2d::detail {

/// Colours shared by the theme and the overlay's own drawing (graphs, bars, badges).
struct OverlayColors {
    static constexpr unsigned kAccent = 0xFFAE7F2AU; ///< #2a7fae as ImGui's ABGR.
    static constexpr unsigned kGood = 0xFF6CC86AU;   ///< Within the frame budget.
    static constexpr unsigned kWarn = 0xFF3CC4F0U;   ///< Over it, under twice.
    static constexpr unsigned kBad = 0xFF5A5AE6U;    ///< Twice the budget or more.
};

/// Applies the theme to the current ImGui context, with sizes and the font scaled by @p scale
/// (the window's content scale, e.g. 1.25 on a 125 % display). Call once, before the first frame.
void applyStyle(float scale);

} // namespace astra2d::detail
