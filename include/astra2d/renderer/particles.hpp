#pragma once

#include "astra2d/renderer/sprite.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

/// @addtogroup renderer
/// @{

namespace astra2d {

class Renderer2D;

/// What the particles of a ParticleEmitter look like and how they move. Every particle gets its
/// own random share of the variations. Meant for designated initializers:
/// @code
/// ParticleEmitter sparks({.lifetime = 0.5F, .speed = 120.0F, .gravity = {0, -300},
///                         .startColor = gold, .endColor = {1, 0.4F, 0, 0}});
/// @endcode
struct ParticleSettings {
    float lifetime = 1.0F;           ///< Seconds a particle lives.
    float lifetimeVariation = 0.0F;  ///< Up to this fraction shorter or longer (0 to 1).
    glm::vec2 direction{0.0F, 1.0F}; ///< Main direction of the start velocity.
    /// Width of the cone of start directions, in radians, centred on the direction: 0 sends all
    /// particles the same way, 2π in every direction (the default).
    float spread = 6.2831853F;
    float speed = 60.0F;         ///< Start speed in world units per second.
    float speedVariation = 0.0F; ///< Up to this fraction slower or faster (0 to 1).
    glm::vec2 gravity{0.0F};     ///< Acceleration, world units per second squared.
    float damping = 0.0F;        ///< Fraction of the velocity lost per second (air drag).
    /// Half size of the box around the emit position that particles start in; (0, 0) starts them
    /// all on the point.
    glm::vec2 area{0.0F};
    float startSize = 4.0F;     ///< Size (width and height) when a particle starts...
    float endSize = 0.0F;       ///< ...and when it dies; it changes evenly in between.
    glm::vec4 startColor{1.0F}; ///< Colour (or tint of the sprite) at the start...
    glm::vec4 endColor{1.0F, 1.0F, 1.0F, 0.0F}; ///< ...and at the end: by default it fades out.
    /// Largest rotation speed, radians per second; each particle starts at a random angle and turns
    /// at a random speed up to this, either way. 0 keeps them upright.
    float spin = 0.0F;
    /// Texture of every particle; a null texture draws solid squares. The texture must outlive the
    /// emitter's draw() calls.
    Sprite sprite{};
};

/// A pool of short-lived particles for bursts, dust, sparks and smoke (like Godot's
/// CPUParticles2D). The particles are simulated on the CPU and drawn as quads through the
/// Renderer2D batch, so thousands of them are a handful of draw calls.
///
/// @code
/// m_sparks.emit(coinPosition, 24);                      // a burst
/// m_smoke.stream(exhaustPosition, 30.0F, deltaTime);     // 30 particles per second
/// m_sparks.update(deltaTime);                            // every frame
/// m_sparks.draw(m_renderer);                             // between begin() and end()
/// @endcode
///
/// The pool has a fixed capacity: no allocation after construction, and particles that do not
/// fit are not emitted. Deterministic for a given seed and sequence of calls, so it is
/// unit-tested; draw() is the only part that needs OpenGL.
class ParticleEmitter {
public:
    /// @param settings Look and motion of the particles; can be changed later with settings().
    /// @param capacity Most particles alive at once.
    /// @param seed     Different seeds give different (but each repeatable) particles.
    explicit ParticleEmitter(const ParticleSettings& settings = {}, std::size_t capacity = 512,
                             std::uint32_t seed = 1);

    /// Starts @p count particles at @p position (within ParticleSettings::area).
    void emit(glm::vec2 position, int count);
    /// Starts particles at @p position at a rate of @p perSecond, for a frame of @p deltaTime
    /// seconds: a steady stream (smoke, exhaust) at any frame rate.
    void stream(glm::vec2 position, float perSecond, float deltaTime);
    /// Moves the particles and removes those whose time is up.
    void update(float deltaTime);
    /// Draws the living particles. Must be called between Renderer2D::begin() and end().
    void draw(Renderer2D& renderer) const;
    /// Removes every particle.
    void clear();

    /// Particles alive.
    [[nodiscard]] std::size_t count() const { return m_particles.size(); }
    /// Most particles alive at once.
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    /// The settings; changes affect particles emitted afterwards (look and gravity: all).
    [[nodiscard]] ParticleSettings& settings() { return m_settings; }
    /// @copydoc settings()
    [[nodiscard]] const ParticleSettings& settings() const { return m_settings; }

    /// One particle, for inspection (e.g. in tests).
    struct Particle {
        glm::vec2 position{0.0F}; ///< World position.
        glm::vec2 velocity{0.0F}; ///< World units per second.
        float age = 0.0F;         ///< Seconds alive.
        float lifetime = 1.0F;    ///< Seconds it lives in all.
        float rotation = 0.0F;    ///< Radians.
        float spin = 0.0F;        ///< Radians per second.
    };
    /// The living particles, in no particular order.
    [[nodiscard]] const std::vector<Particle>& particles() const { return m_particles; }

private:
    [[nodiscard]] float random(float min, float max);

    ParticleSettings m_settings;
    std::size_t m_capacity;
    std::vector<Particle> m_particles;
    std::minstd_rand m_random;
    float m_streamDebt = 0.0F; // particles owed by stream(), less than one
};

} // namespace astra2d

/// @}
