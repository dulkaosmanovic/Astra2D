#include "controller.hpp"

#include <algorithm>
#include <cmath>

namespace platformer {

JumpController::JumpController(float coyoteTime, float bufferTime)
    : m_coyoteTime(coyoteTime)
    , m_bufferTime(bufferTime)
{
}

bool JumpController::update(float dt, bool grounded, bool jumpPressed)
{
    m_coyoteLeft = grounded ? m_coyoteTime : std::max(m_coyoteLeft - dt, 0.0F);
    m_bufferLeft = jumpPressed ? m_bufferTime : std::max(m_bufferLeft - dt, 0.0F);
    if (m_bufferLeft > 0.0F && m_coyoteLeft > 0.0F) {
        reset(); // one press, one jump; no second jump from the same grace time
        return true;
    }
    return false;
}

void JumpController::reset()
{
    m_coyoteLeft = 0.0F;
    m_bufferLeft = 0.0F;
}

float approach(float current, float target, float maxDelta)
{
    return current < target ? std::min(current + maxDelta, target)
                            : std::max(current - maxDelta, target);
}

float horizontalVelocity(float velocity, float moveX, bool grounded, const MoveTuning& tuning,
                         float dt)
{
    const float target = std::clamp(moveX, -1.0F, 1.0F) * tuning.runSpeed;
    // Speeding up in the direction of motion uses the acceleration; stopping or turning around
    // the (stronger) deceleration.
    const bool speedingUp = std::abs(target) > 0.0F && (velocity * target >= 0.0F) &&
                            std::abs(target) > std::abs(velocity);
    float rate = 0.0F;
    if (grounded) {
        rate = speedingUp ? tuning.groundAccel : tuning.groundDecel;
    } else {
        rate = speedingUp || target != 0.0F ? tuning.airAccel : tuning.airDecel;
    }
    return approach(velocity, target, rate * dt);
}

float verticalVelocity(float velocity, bool jumping, bool jumpHeld, const MoveTuning& tuning)
{
    if (jumping) {
        velocity = tuning.jumpSpeed;
    } else if (!jumpHeld) {
        velocity = std::min(velocity, tuning.jumpSpeed * tuning.jumpCut); // short hop
    }
    return std::max(velocity, -tuning.maxFallSpeed);
}

} // namespace platformer
