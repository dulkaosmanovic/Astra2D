#pragma once

/// @addtogroup core
/// @{

namespace astra2d {

/// Fixed-timestep accumulator ("Fix Your Timestep!", Glenn Fiedler).
///
/// Each frame, the real elapsed time is added to an accumulator and consumed in fixed steps.
/// Physics and gameplay that must be deterministic run once per step; rendering runs once per
/// frame and can use alpha() to interpolate between the last two simulation states.
///
/// @code
/// const int steps = timestep.advance(frameTime);
/// for (int i = 0; i < steps; ++i) fixedUpdate(timestep.step());
/// render(timestep.alpha());
/// @endcode
///
/// To avoid the "spiral of death" (a slow frame causing more steps, which makes the next frame
/// even slower), the frame time is clamped to maxFrameTime() before it is accumulated.
class FixedTimestep {
public:
    /// @param step         Length of one simulation step in seconds (must be > 0).
    /// @param maxFrameTime Longest frame time accepted per advance() call, in seconds
    ///                     (must be at least @p step).
    /// @throws std::invalid_argument if either value is out of range or not finite.
    explicit FixedTimestep(double step = 1.0 / 60.0, double maxFrameTime = 0.25);

    /// Adds @p frameTime seconds and returns how many fixed steps should run this frame.
    /// Negative and non-finite (NaN, infinite) frame times are treated as zero.
    int advance(double frameTime);

    /// Length of one simulation step in seconds.
    [[nodiscard]] double step() const { return m_step; }

    /// Longest frame time accepted by advance(), in seconds.
    [[nodiscard]] double maxFrameTime() const { return m_maxFrameTime; }

    /// Time left in the accumulator after the last advance(), in [0, step()).
    [[nodiscard]] double accumulator() const { return m_accumulator; }

    /// Interpolation factor between the previous and current simulation state, in [0, 1).
    [[nodiscard]] double alpha() const { return m_accumulator / m_step; }

    /// Clears the accumulated time (e.g. after loading a level).
    void reset() { m_accumulator = 0.0; }

private:
    double m_step;
    double m_maxFrameTime;
    double m_accumulator = 0.0;
};

} // namespace astra2d

/// @}
