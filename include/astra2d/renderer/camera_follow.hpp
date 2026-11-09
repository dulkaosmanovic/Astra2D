#pragma once

#include <glm/vec2.hpp>

/// @addtogroup renderer
/// @{

namespace astra2d {

class OrthographicCamera;

/// A camera position that follows a target smoothly and stays inside the level (like Godot's
/// Camera2D with position smoothing and limits).
///
/// Every frame the position moves a part of the way to the target that depends only on the time
/// passed, so the camera eases in the same way at any frame rate. Then it is clamped so the view
/// never shows anything outside the bounds; along an axis where the bounds are smaller than the
/// view, the view starts at their low edge (left, bottom).
///
/// @code
/// m_follow.setBounds({0, 0}, m_level.size());
/// m_follow.snap(player, m_camera);                         // when a level starts
/// m_follow.update(player + lookAhead, deltaTime, m_camera); // every frame
/// m_shake.apply(m_camera, m_follow.position());            // or m_camera.setPosition(...)
/// @endcode
///
/// Free of OpenGL, so it is unit-tested.
class CameraFollow {
public:
    /// @param speed How quickly the camera catches up, per second: after 1 / @p speed seconds
    ///              it has covered about 63 % of the way. 0 or less follows without delay.
    explicit CameraFollow(float speed = 6.0F);

    /// Keeps the view inside the rectangle from @p min to @p max (world units).
    void setBounds(glm::vec2 min, glm::vec2 max);
    /// Lets the view go anywhere.
    void clearBounds();
    /// True after setBounds().
    [[nodiscard]] bool hasBounds() const { return m_hasBounds; }

    /// Moves towards @p target, then keeps the view of @p camera (its visibleSize()) in bounds.
    void update(glm::vec2 target, float deltaTime, const OrthographicCamera& camera);
    /// Jumps to @p target (kept in bounds), e.g. when a level starts or after a respawn.
    void snap(glm::vec2 target, const OrthographicCamera& camera);

    /// Where the camera should be.
    [[nodiscard]] glm::vec2 position() const { return m_position; }
    /// Catch-up speed per second.
    [[nodiscard]] float speed() const { return m_speed; }
    /// Sets the catch-up speed (see the constructor).
    void setSpeed(float speed) { m_speed = speed; }

private:
    [[nodiscard]] glm::vec2 clamped(glm::vec2 position, const OrthographicCamera& camera) const;

    float m_speed;
    glm::vec2 m_position{0.0F};
    glm::vec2 m_min{0.0F};
    glm::vec2 m_max{0.0F};
    bool m_hasBounds = false;
};

} // namespace astra2d

/// @}
