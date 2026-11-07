#pragma once

#include "astra2d/physics/world.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <vector>

/// @addtogroup debug
/// @{

namespace astra2d {

class Renderer2D;

/// Colours of the physics debug view.
struct PhysicsDebugColors {
    glm::vec4 staticBody{0.65F, 0.67F, 0.72F, 1.0F};   ///< Outline of a static body's shapes.
    glm::vec4 kinematicBody{0.40F, 0.62F, 1.0F, 1.0F}; ///< Outline of a kinematic body's shapes.
    glm::vec4 awakeBody{0.35F, 0.95F, 0.45F, 1.0F};    ///< Outline of an awake dynamic body.
    glm::vec4 sleepingBody{0.30F, 0.50F, 0.35F, 1.0F}; ///< Outline of a sleeping dynamic body.
    glm::vec4 trigger{1.0F, 0.85F, 0.20F, 1.0F};       ///< Outline of a trigger shape.
    glm::vec4 oneWay{1.0F, 0.55F, 0.20F, 1.0F};        ///< Outline of a one-way platform.
    glm::vec4 boundingBox{0.80F, 0.40F, 0.90F, 0.6F};  ///< Bounding boxes (broad phase).
    glm::vec4 contact{1.0F, 0.25F, 0.25F, 1.0F};       ///< Contact points and normals.
    glm::vec4 velocity{0.20F, 0.90F, 1.0F, 1.0F};      ///< Velocity arrows.
    /// Alpha multiplier of edges that cannot collide (edgeMask), e.g. the inner edges between
    /// merged tiles.
    float disabledEdgeAlpha = 0.3F;
};

/// What the physics debug view shows, and how large. Lengths are in world units, so a game
/// measured in pixels uses larger values than one measured in metres.
struct PhysicsDebugSettings {
    bool shapes = true;          ///< Outlines of all shapes (circles with a spoke for rotation).
    bool boundingBoxes = false;  ///< The shapes' bounding boxes, as the broad phase sees them.
    bool contacts = true;        ///< Contact points with their normals (touching contacts only).
    bool velocities = true;      ///< An arrow from each moving body's centre of mass.
    float lineWidth = 0.03F;     ///< Thickness of all lines.
    float contactSize = 0.1F;    ///< Size of the cross that marks a contact point.
    float normalLength = 0.4F;   ///< Length of a drawn contact normal.
    float velocityScale = 0.1F;  ///< Arrow length per unit of speed (seconds of motion shown).
    int circleSegments = 20;     ///< Lines per circle.
    PhysicsDebugColors colors{}; ///< Colours.
};

/// One line of the debug view.
struct DebugLine {
    glm::vec2 from{0.0F};  ///< Start in world units.
    glm::vec2 to{0.0F};    ///< End in world units.
    glm::vec4 color{1.0F}; ///< Colour.
};

/// The lines that show the state of @p world: shapes, bounding boxes, contacts and velocities, as
/// chosen in @p settings. Needs no OpenGL; drawPhysicsDebug() draws them.
[[nodiscard]] std::vector<DebugLine> collectPhysicsDebug(const PhysicsWorld& world,
                                                         const PhysicsDebugSettings& settings = {});

/// Draws the debug view of @p world (see collectPhysicsDebug()) with @p renderer, which must be
/// between begin() and end() with the camera the world is seen through; usually on top of
/// everything else.
///
/// @code
/// if (m_showPhysics) {
///     astra2d::drawPhysicsDebug(m_renderer, m_physics.world(), {.lineWidth = 1.0F});
/// }
/// @endcode
void drawPhysicsDebug(Renderer2D& renderer, const PhysicsWorld& world,
                      const PhysicsDebugSettings& settings = {});

} // namespace astra2d

/// @}
