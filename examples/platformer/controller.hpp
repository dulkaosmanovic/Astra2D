#pragma once

// How the player moves: tuning values, horizontal acceleration and the jump rules (coyote time and
// jump buffering). Pure functions of the input and the state, without the engine, so they are
// unit-tested (tests/test_platformer.cpp).

namespace platformer {

/// Movement tuning, in world units (a tile is 16) and seconds.
struct MoveTuning {
    float runSpeed = 125.0F;       ///< Top horizontal speed.
    float groundAccel = 1400.0F;   ///< Speeding up on the ground.
    float groundDecel = 2000.0F;   ///< Slowing down (or turning) on the ground.
    float airAccel = 900.0F;       ///< Steering in the air.
    float airDecel = 400.0F;       ///< Slowing down in the air without input.
    float jumpSpeed = 390.0F;      ///< Upward speed at take-off: about 4 tiles high.
    float jumpCut = 0.45F;         ///< Releasing jump while rising caps the speed at this fraction.
    float fallGravityScale = 1.7F; ///< Falling is faster than rising, which feels snappier.
    float maxFallSpeed = 420.0F;   ///< Terminal speed.
    float stompBounce = 260.0F;    ///< Upward speed after landing on an enemy.
    float coyoteTime = 0.1F;       ///< A jump still works this long after walking off a ledge.
    float jumpBufferTime = 0.12F;  ///< A jump pressed this long before landing happens on landing.
};

/// Decides when a jump starts.
///
/// - **Coyote time**: for a moment after the player runs off a ledge, a jump still works, as if
///   they were on the ground. Without it, a jump pressed one frame late does nothing, which feels
///   like a dropped input.
/// - **Jump buffering**: a jump pressed shortly before landing is remembered and happens when the
///   player lands, instead of being lost.
///
/// Call update() once per fixed step.
class JumpController {
public:
    /// With the times of MoveTuning's defaults.
    JumpController() = default;
    /// With other grace times, in seconds.
    JumpController(float coyoteTime, float bufferTime);

    /// Advances by @p dt seconds. @p grounded: standing on something (and not moving up);
    /// @p jumpPressed: the jump button went down in this step. Returns true if a jump starts now.
    bool update(float dt, bool grounded, bool jumpPressed);
    /// Forgets the remembered press and the ledge grace time, e.g. after a bounce or a respawn.
    void reset();

    /// Seconds of coyote time left (0 when it does not apply).
    [[nodiscard]] float coyoteLeft() const { return m_coyoteLeft; }
    /// Seconds a buffered press is still remembered (0 when none is).
    [[nodiscard]] float bufferLeft() const { return m_bufferLeft; }

private:
    float m_coyoteTime = MoveTuning{}.coyoteTime;
    float m_bufferTime = MoveTuning{}.jumpBufferTime;
    float m_coyoteLeft = 0.0F;
    float m_bufferLeft = 0.0F;
};

/// Moves @p current towards @p target by at most @p maxDelta.
[[nodiscard]] float approach(float current, float target, float maxDelta);

/// The horizontal velocity after one step of @p dt seconds with stick/key input @p moveX in
/// [-1, 1]: accelerates towards `moveX * runSpeed`, with more grip on the ground than in the air.
[[nodiscard]] float horizontalVelocity(float velocity, float moveX, bool grounded,
                                       const MoveTuning& tuning, float dt);

/// The vertical velocity after the jump rules: @p jumping starts a jump, releasing the button
/// (@p jumpHeld false) while rising cuts it short, and falls are capped at maxFallSpeed.
[[nodiscard]] float verticalVelocity(float velocity, bool jumping, bool jumpHeld,
                                     const MoveTuning& tuning);

} // namespace platformer
