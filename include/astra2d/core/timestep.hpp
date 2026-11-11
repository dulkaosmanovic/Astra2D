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
///
/// For debugging, game time can run slower or faster than real time (setTimeScale()), stop
/// (a scale of 0) and, while stopped, advance by single steps (requestStep()).
class FixedTimestep {
public:
    /// @param step         Length of one simulation step in seconds (must be > 0).
    /// @param maxFrameTime Longest frame time accepted per advance() call, in seconds
    ///                     (must be at least @p step).
    /// @throws std::invalid_argument if either value is out of range or not finite.
    explicit FixedTimestep(double step = 1.0 / 60.0, double maxFrameTime = 0.25);

    /// Adds @p frameTime seconds of real time, times timeScale() and clamped to maxFrameTime(),
    /// and returns how many fixed steps should run this frame. Negative and non-finite (NaN,
    /// infinite) frame times are treated as zero. While paused it returns 1 if a step was
    /// requested and 0 otherwise.
    int advance(double frameTime);

    /// Game seconds that the last advance() added: the scaled frame time, or one step() for a
    /// requested step while paused. What a variable-rate update (Application::onUpdate()) gets.
    [[nodiscard]] double gameTime() const { return m_gameTime; }

    /// Game seconds per real second: 1 is normal, 0.25 slow motion, 0 paused.
    /// @throws std::invalid_argument if @p scale is negative or not finite.
    void setTimeScale(double scale);
    /// Game seconds per real second.
    [[nodiscard]] double timeScale() const { return m_timeScale; }
    /// True while the time scale is 0.
    [[nodiscard]] bool paused() const { return m_timeScale == 0.0; }
    /// While paused, makes the next advance() run exactly one step. Ignored while running.
    void requestStep() { m_stepRequested = paused(); }

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
    double m_timeScale = 1.0;
    double m_gameTime = 0.0;
    bool m_stepRequested = false;
};

} // namespace astra2d

/// @}
