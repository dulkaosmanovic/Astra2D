#include "astra2d/renderer/camera_follow.hpp"

#include "astra2d/renderer/camera.hpp"

#include <glm/common.hpp>

#include <algorithm>
#include <cmath>

namespace astra2d {

CameraFollow::CameraFollow(float speed)
    : m_speed(speed)
{
}

void CameraFollow::setBounds(glm::vec2 min, glm::vec2 max)
{
    m_min = glm::min(min, max);
    m_max = glm::max(min, max);
    m_hasBounds = true;
}

void CameraFollow::clearBounds()
{
    m_hasBounds = false;
}

void CameraFollow::update(glm::vec2 target, float deltaTime, const OrthographicCamera& camera)
{
    // 1 - e^(-speed * dt) of the way: two half steps cover the same distance as one whole step.
    const float t = m_speed > 0.0F ? 1.0F - std::exp(-m_speed * std::max(deltaTime, 0.0F)) : 1.0F;
    m_position = clamped(m_position + ((target - m_position) * t), camera);
}

void CameraFollow::snap(glm::vec2 target, const OrthographicCamera& camera)
{
    m_position = clamped(target, camera);
}

glm::vec2 CameraFollow::clamped(glm::vec2 position, const OrthographicCamera& camera) const
{
    if (!m_hasBounds) {
        return position;
    }
    const glm::vec2 half = camera.visibleSize() * 0.5F;
    const glm::vec2 low = m_min + half;
    // Bounds smaller than the view: the view starts at their low edge.
    const glm::vec2 high = glm::max(low, m_max - half);
    return glm::clamp(position, low, high);
}

} // namespace astra2d
