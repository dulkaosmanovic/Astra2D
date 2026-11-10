#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// @addtogroup core
/// @{

namespace astra2d {

/// One scope in the Profiler's call tree, with its times from the last finished frame.
///
/// A scope is identified by its name and its parent, so the same ASTRA_PROFILE_SCOPE reached from
/// two places gives two nodes, and a scope entered several times in one frame (a fixed step that
/// runs twice, a scope in a loop) is one node with several calls.
struct ProfileNode {
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Wmissing-field-initializers
    std::string name{}; ///< The name given to the scope ("frame" for the root).
    int parent = -1;    ///< Index of the parent node; -1 for the root.
    int depth = 0;      ///< 0 for the root, 1 for its children, ...
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Wmissing-field-initializers
    std::vector<int> children{}; ///< Indices of the child nodes, in the order they were first seen.
    int calls = 0;               ///< How often the scope ran in the last frame (0: not at all).
    double ms = 0.0;             ///< Total time of those calls in milliseconds, children included.
    /// ms smoothed over recent frames (an exponential moving average), so the numbers can be read
    /// while they change every frame.
    double averageMs = 0.0;
};

/// Measures how long named scopes of code take per frame, as a call tree: a scoped CPU profiler.
///
/// Code marks a scope with ASTRA_PROFILE_SCOPE; Application::run() marks the frames and the main
/// phases of the loop, and the engine marks its own work (scene systems, physics, renderer). The
/// DebugOverlay shows the tree. Recording is off until setEnabled(true), and a scope then costs a
/// branch, so the scopes stay in release builds:
///
/// @code
/// void Game::moveEnemies()
/// {
///     ASTRA_PROFILE_SCOPE("move enemies");
///     ...
/// }
/// // later: Profiler::global().nodes() holds the tree of the last frame
/// @endcode
///
/// Only scopes inside a frame (between beginFrame() and endFrame()) are recorded, so code that
/// runs without a main loop, such as the headless benchmarks, is not measured. Like the rest of
/// the engine, a Profiler must only be used from one thread.
class Profiler {
public:
    /// A clock in seconds; the profiler only uses differences between its readings.
    using Clock = double (*)();

    /// A profiler that reads @p clock (nullptr: std::chrono::steady_clock). Each frame moves
    /// ProfileNode::averageMs the fraction @p smoothing (0 to 1] of the way to the frame's time.
    /// @throws std::invalid_argument if @p smoothing is not in (0, 1].
    explicit Profiler(Clock clock = nullptr, double smoothing = 0.05);

    /// The profiler that ASTRA_PROFILE_SCOPE and Application::run() use.
    [[nodiscard]] static Profiler& global();

    /// Turns recording on or off from the next beginFrame() on, so a frame is always recorded
    /// whole. Turning it on starts the averages afresh.
    void setEnabled(bool enabled);
    /// True if recording is on or will be from the next frame.
    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    /// True between beginFrame() and endFrame() while enabled: scopes are recorded now.
    [[nodiscard]] bool isRecording() const { return m_recording; }

    /// Starts a frame. An unfinished frame (beginFrame() without endFrame()) is dropped.
    /// @pre no scope is open.
    void beginFrame();
    /// Ends the frame: its times become the nodes' ms and calls. Does nothing outside a frame.
    /// @pre every scope opened in the frame is closed.
    void endFrame();

    /// Opens a scope called @p name inside the innermost open scope. Prefer ASTRA_PROFILE_SCOPE,
    /// which closes it again. Does nothing unless isRecording().
    void beginScope(std::string_view name);
    /// Closes the innermost open scope. Does nothing unless isRecording().
    /// @pre a scope is open.
    void endScope();

    /// The call tree: nodes()[0] is the root (the whole frame), its children follow
    /// ProfileNode::children. Nodes are never removed, so indices stay valid until reset().
    [[nodiscard]] const std::vector<ProfileNode>& nodes() const { return m_nodes; }
    /// Number of frames finished since recording was enabled or reset().
    [[nodiscard]] std::size_t frames() const { return m_frames; }

    /// Forgets the tree and all times; the current frame, if any, is dropped.
    /// @pre no scope is open.
    void reset();

private:
    struct OpenScope {
        int node = 0;
        double start = 0.0;
    };
    struct Accumulator {
        int calls = 0;
        double ms = 0.0;
    };

    [[nodiscard]] int childNamed(int parent, std::string_view name);
    void clearAccumulators();

    Clock m_clock;
    double m_smoothing;
    bool m_enabled = false;
    bool m_recording = false;
    std::size_t m_frames = 0;
    std::vector<ProfileNode> m_nodes;
    std::vector<Accumulator> m_current; // per node: the frame in progress
    std::vector<bool> m_measured;       // per node: has an average yet
    std::vector<OpenScope> m_open;      // the root while a frame runs, then the open scopes
};

/// Records the time from its construction to its destruction as a scope of a Profiler.
/// Usually created by ASTRA_PROFILE_SCOPE.
class ProfileScope {
public:
    /// Opens the scope @p name in @p profiler, if it is recording. @p name is copied when the
    /// scope is first seen, so it may be a temporary.
    ProfileScope(Profiler& profiler, std::string_view name)
        : m_profiler(profiler.isRecording() ? &profiler : nullptr)
    {
        if (m_profiler != nullptr) {
            m_profiler->beginScope(name);
        }
    }
    /// Closes the scope.
    ~ProfileScope()
    {
        if (m_profiler != nullptr) {
            m_profiler->endScope();
        }
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    ProfileScope& operator=(ProfileScope&&) = delete;

private:
    Profiler* m_profiler; // nullptr if the scope was not opened
};

} // namespace astra2d

/// @cond
#define ASTRA_PROFILE_CONCAT_INNER(a, b) a##b
#define ASTRA_PROFILE_CONCAT(a, b) ASTRA_PROFILE_CONCAT_INNER(a, b)
/// @endcond

/// Measures the rest of the enclosing block as a scope called @p name (a string) in
/// Profiler::global(); see Profiler.
#define ASTRA_PROFILE_SCOPE(name)                                                    \
    const ::astra2d::ProfileScope ASTRA_PROFILE_CONCAT(astraProfileScope, __LINE__)( \
        ::astra2d::Profiler::global(), name)

/// @}
