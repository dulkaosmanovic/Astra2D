#include "astra2d/debug/physics_debug_draw.hpp"

#include "astra2d/renderer/renderer2d.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace astra2d {

namespace {

class Collector {
public:
    Collector(const PhysicsWorld& world, const PhysicsDebugSettings& settings)
        : m_world(&world)
        , m_settings(&settings)
    {
    }

    std::vector<DebugLine> collect()
    {
        m_world->forEachBody([this](BodyId body) { addBody(body); });
        if (m_settings->contacts) {
            addContacts();
        }
        return std::move(m_lines);
    }

private:
    void line(glm::vec2 from, glm::vec2 to, glm::vec4 color)
    {
        m_lines.push_back({.from = from, .to = to, .color = color});
    }

    [[nodiscard]] glm::vec4 bodyColor(BodyId body) const
    {
        const PhysicsDebugColors& colors = m_settings->colors;
        switch (m_world->type(body)) {
        case BodyType::Static:
            return colors.staticBody;
        case BodyType::Kinematic:
            return colors.kinematicBody;
        case BodyType::Dynamic:
            break;
        }
        return m_world->isAwake(body) ? colors.awakeBody : colors.sleepingBody;
    }

    void addBody(BodyId body)
    {
        const Pose pose = m_world->pose(body);
        const glm::vec4 color = bodyColor(body);
        for (const ShapeId shape : m_world->shapes(body)) {
            const ShapeDef& def = m_world->shapeDef(shape);
            if (m_settings->shapes) {
                glm::vec4 shapeColor = color;
                if (def.isTrigger) {
                    shapeColor = m_settings->colors.trigger;
                } else if (def.oneWay) {
                    shapeColor = m_settings->colors.oneWay;
                }
                if (def.type == ShapeType::Circle) {
                    addCircle(pose, def.circle, shapeColor);
                } else {
                    addPolygon(pose, def.polygon, def.edgeMask, shapeColor);
                }
            }
            if (m_settings->boundingBoxes) {
                addBox(m_world->shapeAabb(shape), m_settings->colors.boundingBox);
            }
        }
        if (m_settings->velocities && m_world->type(body) != BodyType::Static &&
            m_world->isAwake(body)) {
            const glm::vec2 center = m_world->worldCenter(body);
            addArrow(center, center + (m_world->velocity(body) * m_settings->velocityScale),
                     m_settings->colors.velocity);
        }
    }

    void addCircle(const Pose& pose, const Circle& circle, glm::vec4 color)
    {
        const glm::vec2 center = pose.apply(circle.center);
        const int segments = std::max(m_settings->circleSegments, 3);
        const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>(segments);
        // Starts at the body's rotation, so the circle visibly turns with it.
        const float start = std::atan2(pose.rotation.s, pose.rotation.c);
        glm::vec2 previous = center + (circle.radius * glm::vec2(std::cos(start), std::sin(start)));
        for (int i = 1; i <= segments; ++i) {
            const float angle = start + (step * static_cast<float>(i));
            const glm::vec2 next =
                center + (circle.radius * glm::vec2(std::cos(angle), std::sin(angle)));
            line(previous, next, color);
            previous = next;
        }
        line(center, center + pose.rotation.apply({circle.radius, 0.0F}), color); // the spoke
    }

    void addPolygon(const Pose& pose, const Polygon& polygon, std::uint8_t edgeMask,
                    glm::vec4 color)
    {
        // Edges that cannot collide (the inner edges of merged tiles) are drawn faint: the
        // debug view shows why nothing catches on the seams.
        glm::vec4 faint = color;
        faint.a *= m_settings->colors.disabledEdgeAlpha;
        const auto points = polygon.points();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const bool enabled = ((static_cast<unsigned>(edgeMask) >> i) & 1U) != 0;
            line(pose.apply(points[i]), pose.apply(points[(i + 1) % points.size()]),
                 enabled ? color : faint);
        }
    }

    void addBox(const Aabb& box, glm::vec4 color)
    {
        const glm::vec2 a = box.min;
        const glm::vec2 b{box.max.x, box.min.y};
        const glm::vec2 c = box.max;
        const glm::vec2 d{box.min.x, box.max.y};
        line(a, b, color);
        line(b, c, color);
        line(c, d, color);
        line(d, a, color);
    }

    void addArrow(glm::vec2 from, glm::vec2 to, glm::vec4 color)
    {
        const glm::vec2 shaft = to - from;
        const float length = glm::length(shaft);
        if (length <= m_settings->lineWidth) {
            return; // standing still: nothing to show
        }
        line(from, to, color);
        // Two head strokes at ±25° from the reversed shaft, a quarter of its length.
        const glm::vec2 back = -shaft / length * (0.25F * length);
        const Rotation turn = Rotation::fromAngle(0.44F);
        line(to, to + turn.apply(back), color);
        line(to, to + turn.applyInverse(back), color);
    }

    void addContacts()
    {
        const float half = 0.5F * m_settings->contactSize;
        const glm::vec4 color = m_settings->colors.contact;
        for (const ContactInfo& contact : m_world->contacts()) {
            if (!contact.touching) {
                continue;
            }
            const Manifold& manifold = contact.manifold;
            for (int i = 0; i < manifold.count; ++i) {
                const glm::vec2 p = manifold.points[static_cast<std::size_t>(i)].point;
                line(p - glm::vec2{half, half}, p + glm::vec2{half, half}, color); // a cross
                line(p - glm::vec2{half, -half}, p + glm::vec2{half, -half}, color);
                line(p, p + (manifold.normal * m_settings->normalLength), color);
            }
        }
    }

    const PhysicsWorld* m_world;
    const PhysicsDebugSettings* m_settings;
    std::vector<DebugLine> m_lines;
};

} // namespace

std::vector<DebugLine> collectPhysicsDebug(const PhysicsWorld& world,
                                           const PhysicsDebugSettings& settings)
{
    return Collector(world, settings).collect();
}

void drawPhysicsDebug(Renderer2D& renderer, const PhysicsWorld& world,
                      const PhysicsDebugSettings& settings)
{
    for (const DebugLine& line : collectPhysicsDebug(world, settings)) {
        renderer.drawLine(line.from, line.to, line.color, settings.lineWidth);
    }
}

} // namespace astra2d
