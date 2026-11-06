#pragma once

#include <vector>

/// @addtogroup renderer
/// @{

namespace astra2d {

/// A frame-based animation: which sprite sheet frames to show, and how fast.
struct SpriteAnimation {
    std::vector<int> frames;    ///< Sprite sheet frame indices, in playback order.
    float frameDuration = 0.1F; ///< Seconds each frame is shown (must be > 0).
    bool loop = true;           ///< Start over after the last frame, or stop on it.
};

/// Plays a SpriteAnimation: advances time and tells which frame to draw.
///
/// @code
/// m_player.play(m_runAnimation);      // in onUpdate(), when the state changes
/// m_player.update(deltaTime);
/// renderer.drawSprite(pos, size, sheet.frame(m_player.frame()));
/// @endcode
///
/// The animation is not copied: it must outlive the player or be replaced with play() first.
/// Pure logic without OpenGL, so it is unit-tested directly.
class AnimationPlayer {
public:
    /// Starts @p animation. Calling it again with the animation that is already playing keeps its
    /// progress, so it can be called every frame; pass @p restart to start over anyway.
    /// @throws std::invalid_argument if the animation has no frames or frameDuration <= 0.
    void play(const SpriteAnimation& animation, bool restart = false);
    /// The player keeps a pointer to the animation, so a temporary is rejected at compile time.
    void play(const SpriteAnimation&& animation, bool restart = false) = delete;

    /// Stops playback; frame() then returns 0.
    void stop();

    /// Advances playback by @p deltaTime seconds, scaled by speed().
    void update(float deltaTime);

    /// Sprite sheet frame index to draw now, or 0 if nothing is playing.
    [[nodiscard]] int frame() const;

    /// Position in the animation's frame list (0 to frames.size() - 1).
    [[nodiscard]] int frameIndex() const { return m_index; }

    /// True when a non-looping animation has reached its last frame.
    [[nodiscard]] bool finished() const { return m_finished; }

    /// True while an animation is set (also when it has finished).
    [[nodiscard]] bool isPlaying() const { return m_animation != nullptr; }

    /// The animation being played, or nullptr.
    [[nodiscard]] const SpriteAnimation* animation() const { return m_animation; }

    /// Sets the playback speed multiplier (1 = normal, 0 = paused; negative values are clamped
    /// to 0).
    void setSpeed(float speed);
    /// Playback speed multiplier.
    [[nodiscard]] float speed() const { return m_speed; }

private:
    const SpriteAnimation* m_animation = nullptr;
    float m_time = 0.0F; // seconds since the current loop started
    float m_speed = 1.0F;
    int m_index = 0;
    bool m_finished = false;
};

} // namespace astra2d

/// @}
