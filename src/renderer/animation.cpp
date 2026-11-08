#include "astra2d/renderer/animation.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace astra2d {

void AnimationPlayer::play(const SpriteAnimation& animation, bool restart)
{
    if (animation.frames.empty()) {
        throw std::invalid_argument("Sprite animation has no frames");
    }
    if (!std::isfinite(animation.frameDuration) || animation.frameDuration <= 0.0F) {
        throw std::invalid_argument(std::format("Sprite animation frame duration must be positive, "
                                                "got {}",
                                                animation.frameDuration));
    }
    if (&animation == m_animation && !restart) {
        return;
    }
    m_animation = &animation;
    m_time = 0.0F;
    m_index = 0;
    m_finished = false;
}

void AnimationPlayer::stop()
{
    m_animation = nullptr;
    m_time = 0.0F;
    m_index = 0;
    m_finished = false;
}

void AnimationPlayer::update(float deltaTime)
{
    if (m_animation == nullptr || m_finished || !std::isfinite(deltaTime) || deltaTime <= 0.0F) {
        return;
    }

    const auto count = static_cast<int>(m_animation->frames.size());
    const float length = m_animation->frameDuration * static_cast<float>(count);
    m_time += deltaTime * m_speed;

    if (m_animation->loop) {
        // fmod instead of a loop, so a long pause does not step through many cycles.
        m_time = std::fmod(m_time, length);
    } else if (m_time >= length) {
        m_time = length;
        m_index = count - 1;
        m_finished = true;
        return;
    }
    m_index = std::clamp(static_cast<int>(m_time / m_animation->frameDuration), 0, count - 1);
}

int AnimationPlayer::frame() const
{
    if (m_animation == nullptr) {
        return 0;
    }
    return m_animation->frames[static_cast<std::size_t>(m_index)];
}

void AnimationPlayer::setSpeed(float speed)
{
    m_speed = std::isfinite(speed) ? std::max(speed, 0.0F) : 0.0F;
}

} // namespace astra2d
