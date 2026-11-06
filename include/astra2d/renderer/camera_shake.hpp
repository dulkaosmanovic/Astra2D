#pragma once

#include <glm/vec2.hpp>

#include <cstdint>

/// @addtogroup renderer
/// @{

namespace astra2d {

class OrthographicCamera;

/// Settings of a CameraShake.
struct CameraShakeSettings {
    glm::vec2 maxOffset{12.0F, 12.0F}; ///< Largest displacement in world units (at trauma 1).
    float maxAngle = 0.05F;            ///< Largest rotation in radians (at trauma 1).
    float frequency = 18.0F;           ///< How fast the shake moves, in noise cycles per second.
    float decay = 1.2F;                ///< Trauma lost per second.
};

/// Trauma-based screen shake (after Squirrel Eiserloh, "Math for Game Programmers: Juicing Your
/// Cameras With Math", GDC 2016).
///
/// Events add *trauma* in [0, 1] (an explosion 0.6, a small hit 0.2); it decays over time. The
/// shake grows with trauma squared, so small hits barely move the camera and big ones clearly
/// do, and it follows smooth noise instead of random jumps, so it reads as a shake, not jitter.
///
/// @code
/// m_shake.addTrauma(0.5F);                          // on impact
/// m_shake.update(deltaTime);                        // every frame
/// m_shake.apply(m_camera, m_playerPosition);        // before Renderer2D::begin()
/// @endcode
///
/// Deterministic for a given seed and sequence of calls, and free of OpenGL, so it is unit-tested.
class CameraShake {
public:
    /// @param settings Strength, speed and decay of the shake.
    /// @param seed     Different seeds give different (but each repeatable) shake patterns.
    explicit CameraShake(const CameraShakeSettings& settings = {}, std::uint32_t seed = 1);

    /// Adds @p amount to the trauma, which stays within [0, 1].
    void addTrauma(float amount);
    /// Advances the noise and lets trauma decay by settings().decay per second.
    void update(float deltaTime);
    /// Removes all trauma: the shake stops at once.
    void reset();

    /// Current trauma in [0, 1].
    [[nodiscard]] float trauma() const { return m_trauma; }
    /// Current displacement; within ±maxOffset * trauma².
    [[nodiscard]] glm::vec2 offset() const;
    /// Current rotation in radians; within ±maxAngle * trauma².
    [[nodiscard]] float angle() const;

    /// Places @p camera at @p position + offset() with rotation @p rotation + angle().
    void apply(OrthographicCamera& camera, glm::vec2 position, float rotation = 0.0F) const;

    /// The settings.
    [[nodiscard]] const CameraShakeSettings& settings() const { return m_settings; }

private:
    [[nodiscard]] float noise(std::uint32_t channel) const;

    CameraShakeSettings m_settings;
    std::uint32_t m_seed;
    float m_time = 0.0F;
    float m_trauma = 0.0F;
};

} // namespace astra2d

/// @}
