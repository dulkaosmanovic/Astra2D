#include "astra2d/renderer/camera_shake.hpp"

#include "astra2d/renderer/camera.hpp"

#include <algorithm>
#include <cmath>

namespace astra2d {

namespace {

// Integer hash (from "lowbias32", Chris Wellons) mapped to a gradient in [-1, 1].
float gradient(std::uint32_t x)
{
    x ^= x >> 16U;
    x *= 0x7feb352dU;
    x ^= x >> 15U;
    x *= 0x846ca68bU;
    x ^= x >> 16U;
    return (static_cast<float>(x) / 4294967295.0F * 2.0F) - 1.0F;
}

// 1D gradient (Perlin) noise: smooth, 0 at whole numbers, roughly within [-0.5, 0.5].
float gradientNoise(float t, std::uint32_t seed)
{
    const float cell = std::floor(t);
    const float f = t - cell;
    const auto i = static_cast<std::uint32_t>(static_cast<std::int64_t>(cell));
    const float g0 = gradient(i ^ seed);
    const float g1 = gradient((i + 1U) ^ seed);
    const float fade = f * f * (3.0F - (2.0F * f));
    return (g0 * f) + (((g1 * (f - 1.0F)) - (g0 * f)) * fade);
}

} // namespace

CameraShake::CameraShake(const CameraShakeSettings& settings, std::uint32_t seed)
    : m_settings(settings)
    , m_seed(seed * 0x9E3779B9U)
{
}

void CameraShake::addTrauma(float amount)
{
    if (std::isfinite(amount)) {
        m_trauma = std::clamp(m_trauma + amount, 0.0F, 1.0F);
    }
}

void CameraShake::update(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F) {
        return;
    }
    m_time += deltaTime;
    // Keep the time small so the float noise input does not lose precision in long sessions.
    constexpr float kWrap = 4096.0F;
    if (m_time > kWrap) {
        m_time -= kWrap;
    }
    m_trauma = std::max(m_trauma - (m_settings.decay * deltaTime), 0.0F);
}

void CameraShake::reset()
{
    m_trauma = 0.0F;
}

float CameraShake::noise(std::uint32_t channel) const
{
    // Each channel samples the noise with its own seed and phase (gradient noise is 0 at whole
    // numbers, so equal phases would make all channels pass through 0 together). x2 brings the
    // noise to about [-1, 1].
    const float phase = 0.31F + (0.37F * static_cast<float>(channel));
    const float value = 2.0F * gradientNoise((m_time * m_settings.frequency) + phase,
                                             m_seed + (channel * 0x632BE5ABU));
    return std::clamp(value, -1.0F, 1.0F);
}

glm::vec2 CameraShake::offset() const
{
    const float shake = m_trauma * m_trauma;
    return {m_settings.maxOffset.x * shake * noise(0), m_settings.maxOffset.y * shake * noise(1)};
}

float CameraShake::angle() const
{
    return m_settings.maxAngle * m_trauma * m_trauma * noise(2);
}

void CameraShake::apply(OrthographicCamera& camera, glm::vec2 position, float rotation) const
{
    camera.setPosition(position + offset());
    camera.setRotation(rotation + angle());
}

} // namespace astra2d
