// PhysicsWorld's contact solver: sequential impulses with warm starting, a block solver for two
// point contacts, restitution, position correction with pseudo velocities, and integration.

#include "world_impl.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace astra2d {

namespace {

// Solves the two normal impulses of a constraint together: the linear complementarity problem
//   vn = K x + b,  x >= 0,  vn >= 0,  x_i vn_i = 0
// for the accumulated impulses x, by trying which points are pushing (both, one, none). Solving
// them one after the other instead lets the order show: a box resting on two points slowly turns,
// and a tall stack leans. `velocity(i)` is the current relative normal velocity at point i,
// `target[i]` the velocity it must reach at least, `apply(i, delta)` applies an impulse change.
template <typename Velocity, typename Apply>
void solveBlock(const Constraint& c, std::array<float, 2>& impulses, std::array<float, 2> target,
                Velocity velocity, Apply apply)
{
    const float a1 = impulses[0];
    const float a2 = impulses[1];
    // Velocities with the current impulses taken out: vn = K x + b.
    const float b1 = velocity(0) - target[0] - ((c.k11 * a1) + (c.k12 * a2));
    const float b2 = velocity(1) - target[1] - ((c.k12 * a1) + (c.k22 * a2));
    const auto set = [&](float x1, float x2) {
        apply(0, x1 - a1);
        apply(1, x2 - a2);
        impulses = {x1, x2};
    };
    // Both points push.
    const float x1 = -((c.inverse11 * b1) + (c.inverse12 * b2));
    const float x2 = -((c.inverse12 * b1) + (c.inverse22 * b2));
    if (x1 >= 0.0F && x2 >= 0.0F) {
        set(x1, x2);
        return;
    }
    // Only the first pushes; the second separates.
    const float only1 = -b1 / c.k11;
    if (only1 >= 0.0F && (c.k12 * only1) + b2 >= 0.0F) {
        set(only1, 0.0F);
        return;
    }
    // Only the second.
    const float only2 = -b2 / c.k22;
    if (only2 >= 0.0F && (c.k12 * only2) + b1 >= 0.0F) {
        set(0.0F, only2);
        return;
    }
    // Neither: both separate.
    if (b1 >= 0.0F && b2 >= 0.0F) {
        set(0.0F, 0.0F);
    }
}

} // namespace

void PhysicsWorld::Impl::solve(float dt)
{
    // Approach velocities are taken before gravity is added: an impact bounces with the speed it
    // arrived with, not with one more step of gravity (which would gain energy at every bounce).
    prepareConstraints();
    integrateForces(dt);
    warmStart();
    // Sequential impulses: solve each point in turn, many times; clamping the accumulated impulse
    // (not each increment) lets later passes correct earlier ones.
    for (int iteration = 0; iteration < settings.velocityIterations; ++iteration) {
        for (Constraint& constraint : constraints) {
            solveFriction(constraint);
            solveNormal(constraint, 1.0F / dt);
        }
    }
    applyRestitution();
    storeImpulses();
    solvePositions(dt);
    integratePositions(dt);
}

void PhysicsWorld::Impl::prepareConstraints()
{
    constraints.clear();
    for (std::uint32_t c = 0; c < contacts.size(); ++c) {
        const Contact& contact = contacts[c];
        if (!contact.solid() || contact.manifold.count == 0) {
            continue;
        }
        const ShapeData& shapeA = shapes[contact.shapeA];
        const ShapeData& shapeB = shapes[contact.shapeB];
        const Body& bodyA = bodies[shapeA.body];
        const Body& bodyB = bodies[shapeB.body];
        if (!(bodyA.isDynamic() && bodyA.awake) && !(bodyB.isDynamic() && bodyB.awake)) {
            continue;
        }
        Constraint constraint;
        constraint.contact = c;
        constraint.bodyA = shapeA.body;
        constraint.bodyB = shapeB.body;
        constraint.invMassA = bodyA.invMass;
        constraint.invInertiaA = bodyA.invInertia;
        constraint.invMassB = bodyB.invMass;
        constraint.invInertiaB = bodyB.invInertia;
        constraint.normal = contact.manifold.normal;
        constraint.friction = std::sqrt(shapeA.def.friction * shapeB.def.friction);
        constraint.restitution = std::max(shapeA.def.restitution, shapeB.def.restitution);
        constraint.count = contact.manifold.count;
        for (int i = 0; i < constraint.count; ++i) {
            const auto k = static_cast<std::size_t>(i);
            preparePoint(constraint, contact.manifold.points[k], constraint.points[k]);
        }
        if (constraint.count == 2) {
            prepareBlock(constraint);
        }
        constraints.push_back(constraint);
    }
}

void PhysicsWorld::Impl::preparePoint(const Constraint& constraint, const ManifoldPoint& mp,
                                      ConstraintPoint& cp) const
{
    const Body& bodyA = bodies[constraint.bodyA];
    const Body& bodyB = bodies[constraint.bodyB];
    const glm::vec2 n = constraint.normal;
    const glm::vec2 t(n.y, -n.x);
    cp.rA = mp.point - bodyA.center;
    cp.rB = mp.point - bodyB.center;
    // Effective mass along a direction: how much an impulse along it changes the relative
    // velocity there, inverted.
    const auto effectiveMass = [&](glm::vec2 direction) {
        const float rA = cross(cp.rA, direction);
        const float rB = cross(cp.rB, direction);
        const float k = constraint.invMassA + constraint.invMassB +
                        (constraint.invInertiaA * rA * rA) + (constraint.invInertiaB * rB * rB);
        return k > 0.0F ? 1.0F / k : 0.0F;
    };
    cp.normalMass = effectiveMass(n);
    cp.tangentMass = effectiveMass(t);
    cp.normalImpulse = mp.normalImpulse;
    cp.tangentImpulse = mp.tangentImpulse;
    cp.separation = mp.separation;
    cp.relativeVelocity = glm::dot(relativeVelocity(constraint, cp), n);
}

void PhysicsWorld::Impl::prepareBlock(Constraint& constraint)
{
    const glm::vec2 n = constraint.normal;
    const ConstraintPoint& p1 = constraint.points[0];
    const ConstraintPoint& p2 = constraint.points[1];
    const float rn1A = cross(p1.rA, n);
    const float rn1B = cross(p1.rB, n);
    const float rn2A = cross(p2.rA, n);
    const float rn2B = cross(p2.rB, n);
    const float mass = constraint.invMassA + constraint.invMassB;
    constraint.k11 =
        mass + (constraint.invInertiaA * rn1A * rn1A) + (constraint.invInertiaB * rn1B * rn1B);
    constraint.k22 =
        mass + (constraint.invInertiaA * rn2A * rn2A) + (constraint.invInertiaB * rn2B * rn2B);
    constraint.k12 =
        mass + (constraint.invInertiaA * rn1A * rn2A) + (constraint.invInertiaB * rn1B * rn2B);
    const float determinant = (constraint.k11 * constraint.k22) - (constraint.k12 * constraint.k12);
    // Nearly parallel rows (points very close together): solve them one by one.
    constexpr float kMaxCondition = 1000.0F;
    if (constraint.k11 * constraint.k11 < kMaxCondition * determinant) {
        constraint.block = true;
        constraint.inverse11 = constraint.k22 / determinant;
        constraint.inverse12 = -constraint.k12 / determinant;
        constraint.inverse22 = constraint.k11 / determinant;
    }
}

void PhysicsWorld::Impl::integrateForces(float dt)
{
    int awake = 0;
    for (Body& b : bodies) {
        if (!b.alive || !b.isDynamic() || !b.awake) {
            continue;
        }
        ++awake;
        b.velocity += dt * ((b.gravityScale * settings.gravity) + (b.invMass * b.force));
        b.angularVelocity += dt * b.invInertia * b.torque;
        b.velocity *= 1.0F / (1.0F + (dt * b.linearDamping));
        b.angularVelocity *= 1.0F / (1.0F + (dt * b.angularDamping));
    }
    stats.awakeBodies = awake;
}

void PhysicsWorld::Impl::applyImpulse(const Constraint& constraint, const ConstraintPoint& cp,
                                      glm::vec2 impulse)
{
    Body& a = bodies[constraint.bodyA];
    Body& b = bodies[constraint.bodyB];
    a.velocity -= constraint.invMassA * impulse;
    a.angularVelocity -= constraint.invInertiaA * cross(cp.rA, impulse);
    b.velocity += constraint.invMassB * impulse;
    b.angularVelocity += constraint.invInertiaB * cross(cp.rB, impulse);
}

glm::vec2 PhysicsWorld::Impl::relativeVelocity(const Constraint& constraint,
                                               const ConstraintPoint& cp) const
{
    const Body& a = bodies[constraint.bodyA];
    const Body& b = bodies[constraint.bodyB];
    return b.velocity + cross(b.angularVelocity, cp.rB) - a.velocity -
           cross(a.angularVelocity, cp.rA);
}

void PhysicsWorld::Impl::warmStart()
{
    // Last step's impulses again: a resting stack starts close to the solution.
    for (const Constraint& constraint : constraints) {
        const glm::vec2 n = constraint.normal;
        const glm::vec2 t(n.y, -n.x);
        for (int i = 0; i < constraint.count; ++i) {
            const ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
            applyImpulse(constraint, cp, (cp.normalImpulse * n) + (cp.tangentImpulse * t));
        }
    }
}

void PhysicsWorld::Impl::solveFriction(Constraint& constraint)
{
    // Before the normal impulse: friction is limited by it, which the normal pass then refines.
    const glm::vec2 t(constraint.normal.y, -constraint.normal.x);
    for (int i = 0; i < constraint.count; ++i) {
        ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
        const float vt = glm::dot(relativeVelocity(constraint, cp), t);
        const float maxFriction = constraint.friction * cp.normalImpulse;
        const float newImpulse =
            std::clamp(cp.tangentImpulse - (cp.tangentMass * vt), -maxFriction, maxFriction);
        const float delta = newImpulse - cp.tangentImpulse;
        cp.tangentImpulse = newImpulse;
        applyImpulse(constraint, cp, delta * t);
    }
}

void PhysicsWorld::Impl::solveNormal(Constraint& constraint, float invDt)
{
    // A speculative point (still apart) may close its gap in this step, no more: its normal
    // velocity must reach at least -gap / dt.
    const glm::vec2 n = constraint.normal;
    if (constraint.block) {
        std::array<float, 2> impulses = {constraint.points[0].normalImpulse,
                                         constraint.points[1].normalImpulse};
        const std::array<float, 2> target = {
            -std::max(constraint.points[0].separation, 0.0F) * invDt,
            -std::max(constraint.points[1].separation, 0.0F) * invDt};
        solveBlock(constraint, impulses, target, [&](int i) {
            return glm::dot(
                relativeVelocity(constraint, constraint.points[static_cast<std::size_t>(i)]), n);
        }, [&](int i, float delta) {
            applyImpulse(constraint, constraint.points[static_cast<std::size_t>(i)], delta * n);
        });
        for (std::size_t i = 0; i < 2; ++i) {
            constraint.points[i].normalImpulse = impulses[i];
            constraint.points[i].maxNormalImpulse =
                std::max(constraint.points[i].maxNormalImpulse, impulses[i]);
        }
        return;
    }
    for (int i = 0; i < constraint.count; ++i) {
        ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
        const float vn = glm::dot(relativeVelocity(constraint, cp), n);
        const float gap = std::max(cp.separation, 0.0F) * invDt;
        const float newImpulse = std::max(cp.normalImpulse - (cp.normalMass * (vn + gap)), 0.0F);
        const float delta = newImpulse - cp.normalImpulse;
        cp.normalImpulse = newImpulse;
        cp.maxNormalImpulse = std::max(cp.maxNormalImpulse, newImpulse);
        applyImpulse(constraint, cp, delta * n);
    }
}

void PhysicsWorld::Impl::applyRestitution()
{
    // Points that were approaching fast bounce back with e times that speed.
    const float threshold = settings.restitutionThreshold * scale();
    for (Constraint& constraint : constraints) {
        if (constraint.restitution == 0.0F) {
            continue;
        }
        for (int i = 0; i < constraint.count; ++i) {
            ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
            if (cp.relativeVelocity > -threshold || cp.maxNormalImpulse == 0.0F) {
                continue;
            }
            const float vn = glm::dot(relativeVelocity(constraint, cp), constraint.normal);
            const float target = -constraint.restitution * cp.relativeVelocity;
            const float newImpulse =
                std::max(cp.normalImpulse - (cp.normalMass * (vn - target)), 0.0F);
            const float delta = newImpulse - cp.normalImpulse;
            cp.normalImpulse = newImpulse;
            applyImpulse(constraint, cp, delta * constraint.normal);
            constraint.bounced = true;
        }
    }
}

void PhysicsWorld::Impl::storeImpulses()
{
    // Kept for warm starting the next step.
    for (const Constraint& constraint : constraints) {
        Contact& contact = contacts[constraint.contact];
        for (int i = 0; i < constraint.count; ++i) {
            const auto k = static_cast<std::size_t>(i);
            contact.manifold.points[k].normalImpulse = constraint.points[k].normalImpulse;
            contact.manifold.points[k].tangentImpulse = constraint.points[k].tangentImpulse;
        }
    }
}

void PhysicsWorld::Impl::solvePositions(float dt)
{
    // Overlap removal with pseudo velocities (split impulses): they move the bodies apart but are
    // dropped afterwards, so pushing out of an overlap adds no kinetic energy (no bounce).
    for (Body& b : bodies) {
        b.pseudoVelocity = glm::vec2(0.0F);
        b.pseudoAngularVelocity = 0.0F;
    }
    for (int iteration = 0; iteration < settings.positionIterations; ++iteration) {
        for (Constraint& constraint : constraints) {
            // A bouncing contact moves out of the overlap by itself in this step; pushing it out
            // as well would lift the body, i.e. add potential energy at every bounce.
            if (!constraint.bounced) {
                solvePseudo(constraint, 1.0F / dt);
            }
        }
    }
}

void PhysicsWorld::Impl::solvePseudo(Constraint& constraint, float invDt)
{
    Body& a = bodies[constraint.bodyA];
    Body& b = bodies[constraint.bodyB];
    const glm::vec2 n = constraint.normal;
    const float slop = settings.linearSlop * scale();
    const float beta = settings.positionCorrection;
    const auto pseudoVelocity = [&](const ConstraintPoint& cp) {
        return glm::dot(b.pseudoVelocity + cross(b.pseudoAngularVelocity, cp.rB) -
                            a.pseudoVelocity - cross(a.pseudoAngularVelocity, cp.rA),
                        n);
    };
    const auto applyPseudo = [&](const ConstraintPoint& cp, float delta) {
        const glm::vec2 impulse = delta * n;
        a.pseudoVelocity -= constraint.invMassA * impulse;
        a.pseudoAngularVelocity -= constraint.invInertiaA * cross(cp.rA, impulse);
        b.pseudoVelocity += constraint.invMassB * impulse;
        b.pseudoAngularVelocity += constraint.invInertiaB * cross(cp.rB, impulse);
    };
    // The speed that removes the fraction beta of the overlap beyond the slop in this step.
    const auto target = [&](const ConstraintPoint& cp) {
        return beta * invDt * (-cp.separation - slop);
    };
    // Two overlapping points: together, for the same reason as the velocities.
    if (constraint.block && constraint.points[0].separation < -slop &&
        constraint.points[1].separation < -slop) {
        std::array<float, 2> impulses = {constraint.points[0].pseudoImpulse,
                                         constraint.points[1].pseudoImpulse};
        solveBlock(constraint, impulses,
                   {target(constraint.points[0]), target(constraint.points[1])}, [&](int i) {
            return pseudoVelocity(constraint.points[static_cast<std::size_t>(i)]);
        }, [&](int i, float delta) {
            applyPseudo(constraint.points[static_cast<std::size_t>(i)], delta);
        });
        constraint.points[0].pseudoImpulse = impulses[0];
        constraint.points[1].pseudoImpulse = impulses[1];
        return;
    }
    for (int i = 0; i < constraint.count; ++i) {
        ConstraintPoint& cp = constraint.points[static_cast<std::size_t>(i)];
        if (cp.separation >= -slop) {
            continue;
        }
        const float newImpulse =
            std::max(cp.pseudoImpulse + (cp.normalMass * (target(cp) - pseudoVelocity(cp))), 0.0F);
        applyPseudo(cp, newImpulse - cp.pseudoImpulse);
        cp.pseudoImpulse = newImpulse;
    }
}

void PhysicsWorld::Impl::integratePositions(float dt)
{
    for (Body& b : bodies) {
        if (!b.alive || !b.active()) {
            b.lastMotion = glm::vec2(0.0F);
            continue;
        }
        b.lastMotion = dt * (b.velocity + b.pseudoVelocity);
        b.center += b.lastMotion;
        b.angle += dt * (b.angularVelocity + b.pseudoAngularVelocity);
        b.rotation = Rotation::fromAngle(b.angle);
        b.origin = b.center - b.rotation.apply(b.localCenter);
    }
}

} // namespace astra2d
