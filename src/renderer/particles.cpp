#include "astra2d/renderer/particles.hpp"

#include "astra2d/renderer/renderer2d.hpp"

#include <algorithm>
#include <cmath>

namespace astra2d {

ParticleEmitter::ParticleEmitter(const ParticleSettings& settings, std::size_t capacity,
                                 std::uint32_t seed)
    : m_settings(settings)
    , m_capacity(capacity)
    , m_random(seed)
{
    m_particles.reserve(capacity);
}

float ParticleEmitter::random(float min, float max)
{
    return std::uniform_real_distribution<float>(min, max)(m_random);
}

void ParticleEmitter::emit(glm::vec2 position, int count)
{
    const ParticleSettings& s = m_settings;
    const float baseAngle = std::atan2(s.direction.y, s.direction.x);
    const float halfSpread = std::max(s.spread, 0.0F) * 0.5F;
    const float lifeRange = std::clamp(s.lifetimeVariation, 0.0F, 1.0F);
    const float speedRange = std::clamp(s.speedVariation, 0.0F, 1.0F);
    for (int i = 0; i < count && m_particles.size() < m_capacity; ++i) {
        const float angle = baseAngle + random(-halfSpread, halfSpread);
        const float speed = s.speed * random(1.0F - speedRange, 1.0F + speedRange);
        m_particles.push_back(
            {.position =
                 position + glm::vec2{random(-s.area.x, s.area.x), random(-s.area.y, s.area.y)},
             .velocity = speed * glm::vec2{std::cos(angle), std::sin(angle)},
             .lifetime = std::max(s.lifetime * random(1.0F - lifeRange, 1.0F + lifeRange), 1e-4F),
             // Upright unless they spin (pixel art stays aligned); spinning ones start anywhere.
             .rotation = s.spin > 0.0F ? random(0.0F, 6.2831853F) : 0.0F,
             .spin = s.spin > 0.0F ? random(-s.spin, s.spin) : 0.0F});
    }
}

void ParticleEmitter::stream(glm::vec2 position, float perSecond, float deltaTime)
{
    m_streamDebt += std::max(perSecond, 0.0F) * std::max(deltaTime, 0.0F);
    const float whole = std::floor(m_streamDebt);
    m_streamDebt -= whole;
    emit(position, static_cast<int>(whole));
}

void ParticleEmitter::update(float deltaTime)
{
    // Drag as a factor per frame that does not depend on the frame rate.
    const float keep = std::exp(-std::max(m_settings.damping, 0.0F) * deltaTime);
    for (std::size_t i = 0; i < m_particles.size();) {
        Particle& p = m_particles[i];
        p.age += deltaTime;
        if (p.age >= p.lifetime) {
            p = m_particles.back(); // order does not matter: swap with the last and drop it
            m_particles.pop_back();
            continue;
        }
        p.velocity = (p.velocity + (m_settings.gravity * deltaTime)) * keep;
        p.position += p.velocity * deltaTime;
        p.rotation += p.spin * deltaTime;
        ++i;
    }
}

void ParticleEmitter::draw(Renderer2D& renderer) const
{
    const ParticleSettings& s = m_settings;
    for (const Particle& p : m_particles) {
        const float t = p.age / p.lifetime;
        const float size = s.startSize + ((s.endSize - s.startSize) * t);
        renderer.draw({.position = p.position,
                       .size = glm::vec2(size),
                       .rotation = p.rotation,
                       .color = s.startColor + ((s.endColor - s.startColor) * t),
                       .texture = s.sprite.texture,
                       .region = s.sprite.region});
    }
}

void ParticleEmitter::clear()
{
    m_particles.clear();
    m_streamDebt = 0.0F;
}

} // namespace astra2d
