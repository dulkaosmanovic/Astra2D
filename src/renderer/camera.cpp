#include "astra2d/renderer/camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace astra2d {

namespace {

float requirePositive(float value, const char* name)
{
    if (!std::isfinite(value) || value <= 0.0F) {
        throw std::invalid_argument(std::format("Camera {} must be positive, got {}", name, value));
    }
    return value;
}

} // namespace

OrthographicCamera::OrthographicCamera(float viewHeight, glm::ivec2 viewport)
    : m_viewport(1, 1)
    , m_viewHeight(requirePositive(viewHeight, "view height"))
{
    setViewportSize(viewport.x, viewport.y);
}

void OrthographicCamera::setViewportSize(int width, int height)
{
    if (width > 0 && height > 0) {
        m_viewport = {width, height};
        applyFit();
    }
}

void OrthographicCamera::setPosition(glm::vec2 position)
{
    m_position = position;
}

void OrthographicCamera::setZoom(float zoom)
{
    m_zoom = requirePositive(zoom, "zoom");
}

void OrthographicCamera::setViewHeight(float viewHeight)
{
    m_viewHeight = requirePositive(viewHeight, "view height");
    m_fitArea = glm::vec2(0.0F);
}

void OrthographicCamera::fitArea(glm::vec2 area)
{
    m_fitArea = {requirePositive(area.x, "fit width"), requirePositive(area.y, "fit height")};
    applyFit();
}

void OrthographicCamera::applyFit()
{
    if (m_fitArea.y <= 0.0F) {
        return;
    }
    const float aspect = static_cast<float>(m_viewport.x) / static_cast<float>(m_viewport.y);
    // Tall enough for the area's height, and for its width at this aspect ratio.
    m_viewHeight = std::max(m_fitArea.y, m_fitArea.x / aspect);
}

OrthographicCamera OrthographicCamera::screenSpace(int width, int height)
{
    OrthographicCamera camera(static_cast<float>(std::max(height, 1)),
                              {std::max(width, 1), std::max(height, 1)});
    camera.setPosition(glm::vec2(camera.viewportSize()) * 0.5F);
    return camera;
}

void OrthographicCamera::setRotation(float rotation)
{
    m_rotation = std::isfinite(rotation) ? rotation : 0.0F;
}

OrthographicCamera OrthographicCamera::withParallax(glm::vec2 factor) const
{
    OrthographicCamera layer = *this;
    layer.m_position = m_position * factor;
    return layer;
}

glm::vec2 OrthographicCamera::rotate(glm::vec2 v, float angle)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {(c * v.x) - (s * v.y), (s * v.x) + (c * v.y)};
}

glm::vec2 OrthographicCamera::visibleMin() const
{
    return m_position - visibleHalfExtents();
}

glm::vec2 OrthographicCamera::visibleMax() const
{
    return m_position + visibleHalfExtents();
}

glm::vec2 OrthographicCamera::visibleHalfExtents() const
{
    // Axis-aligned bounds of the (possibly rotated) view rectangle.
    const glm::vec2 half = visibleSize() * 0.5F;
    const float c = std::abs(std::cos(m_rotation));
    const float s = std::abs(std::sin(m_rotation));
    return {(c * half.x) + (s * half.y), (s * half.x) + (c * half.y)};
}

glm::vec2 OrthographicCamera::visibleSize() const
{
    const float aspect = static_cast<float>(m_viewport.x) / static_cast<float>(m_viewport.y);
    const float height = m_viewHeight / m_zoom;
    return {height * aspect, height};
}

glm::mat4 OrthographicCamera::viewProjection() const
{
    const glm::vec2 half = visibleSize() * 0.5F;
    const glm::mat4 projection = glm::ortho(-half.x, half.x, -half.y, half.y, -1.0F, 1.0F);
    // The camera turns by m_rotation, so the world turns the other way around the camera.
    glm::mat4 view = glm::rotate(glm::mat4(1.0F), -m_rotation, glm::vec3(0.0F, 0.0F, 1.0F));
    view = glm::translate(view, glm::vec3(-m_position, 0.0F));
    return projection * view;
}

glm::vec2 OrthographicCamera::screenToWorld(glm::vec2 screen) const
{
    const glm::vec2 viewport(m_viewport);
    // Pixels -> normalized device coordinates in [-1, 1], flipping y (screen y points down).
    const glm::vec2 ndc{((screen.x / viewport.x) * 2.0F) - 1.0F,
                        1.0F - ((screen.y / viewport.y) * 2.0F)};
    return m_position + rotate(ndc * (visibleSize() * 0.5F), m_rotation);
}

glm::vec2 OrthographicCamera::worldToScreen(glm::vec2 world) const
{
    const glm::vec2 ndc = rotate(world - m_position, -m_rotation) / (visibleSize() * 0.5F);
    const glm::vec2 viewport(m_viewport);
    return {(ndc.x + 1.0F) * 0.5F * viewport.x, (1.0F - ndc.y) * 0.5F * viewport.y};
}

} // namespace astra2d
