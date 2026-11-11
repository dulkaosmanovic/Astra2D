#include "astra2d/scene/scene.hpp"

#include "astra2d/core/assert.hpp"
#include "astra2d/core/profiler.hpp"
#include "astra2d/physics/geometry.hpp"
#include "astra2d/renderer/renderer2d.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

namespace astra2d {

Transform compose(const Transform& parent, const Transform& local)
{
    return {.position = parent.position +
                        Rotation::fromAngle(parent.rotation).apply(local.position * parent.scale),
            .rotation = parent.rotation + local.rotation,
            .scale = parent.scale * local.scale};
}

Transform interpolate(const Transform& from, const Transform& to, float alpha)
{
    return {.position = glm::mix(from.position, to.position, alpha),
            .rotation = from.rotation + ((to.rotation - from.rotation) * alpha),
            .scale = glm::mix(from.scale, to.scale, alpha)};
}

Transform relativeTo(const Transform& parent, const Transform& world)
{
    return {
        .position =
            Rotation::fromAngle(parent.rotation).applyInverse(world.position - parent.position) /
            parent.scale,
        .rotation = world.rotation - parent.rotation,
        .scale = world.scale / parent.scale};
}

Scene::Scene()
{
    // Destroying an entity in any way (destroyEntity(), Registry::destroy(), removing its
    // Relationship) unlinks it, so no other entity keeps a link to a dead one.
    m_relationshipCleanup = m_registry.onDestroy<Relationship>().connect(
        [this](Registry& /*registry*/, Entity entity) { onRelationshipDestroyed(entity); });

    addSystem(SystemPhase::LateUpdate, "animation", [](Scene& scene, float deltaTime) {
        scene.registry().view<Animator, SpriteRenderer>().each(
            [&](Entity entity, Animator& animator, SpriteRenderer& sprite) {
            if (!animator.player.isPlaying()) {
                return;
            }
            animator.player.update(deltaTime);
            if (animator.sheet != nullptr) {
                sprite.sprite = animator.sheet->frame(animator.player.frame());
            }
            if (animator.destroyWhenFinished && animator.player.finished()) {
                scene.destroyLater(entity);
            }
        });
    });
    addSystem(SystemPhase::LateUpdate, "lifetime", [](Scene& scene, float deltaTime) {
        scene.registry().view<Lifetime>().each([&](Entity entity, Lifetime& lifetime) {
            lifetime.age += deltaTime;
            if (lifetime.age >= lifetime.duration) {
                scene.destroyLater(entity);
            }
        });
    });
}

Scene::~Scene() = default;

// ---------------------------------------------------------------------------------------------
// Entities

Entity Scene::createEntity(std::string_view name, glm::vec2 position)
{
    const Entity entity = m_registry.create();
    m_registry.emplace<Transform>(entity, Transform{.position = position});
    if (!name.empty()) {
        m_registry.emplace<Name>(entity, Name{std::string(name)});
    }
    return entity;
}

void Scene::destroyEntity(Entity entity)
{
    ASTRA_ASSERT(m_registry.valid(entity), "destroyEntity on an invalid entity");
    // The subtree in pre-order (every parent before its descendants), then destroyed back to
    // front, so each entity goes before its parent. A loop, not recursion: deep hierarchies cannot
    // overflow the stack.
    std::vector<Entity> subtree{entity};
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        for (const Entity child : children(subtree[i])) {
            subtree.push_back(child);
        }
    }
    for (const Entity doomed : std::views::reverse(subtree)) {
        // An onDestroy handler of an earlier entity may have destroyed this one already.
        if (m_registry.valid(doomed)) {
            m_registry.destroy(doomed);
        }
    }
}

void Scene::destroyLater(Entity entity)
{
    m_destroyQueue.push_back(entity);
}

void Scene::flushDestroyed()
{
    // Swapped out first: destroying may run handlers that queue more entities.
    while (!m_destroyQueue.empty()) {
        std::vector<Entity> queue;
        queue.swap(m_destroyQueue);
        for (const Entity entity : queue) {
            if (m_registry.valid(entity)) {
                destroyEntity(entity);
            }
        }
    }
}

void Scene::clear()
{
    m_destroyQueue.clear();
    m_registry.clear();
}

Entity Scene::find(std::string_view name) const
{
    Entity found = kNullEntity;
    m_registry.forEach([&](Entity entity) {
        if (found.isNull()) {
            const auto* entityName = m_registry.tryGet<Name>(entity);
            if (entityName != nullptr && entityName->value == name) {
                found = entity;
            }
        }
    });
    return found;
}

// ---------------------------------------------------------------------------------------------
// Hierarchy

bool Scene::isAncestor(Entity ancestor, Entity entity) const
{
    for (Entity current = parent(entity); !current.isNull(); current = parent(current)) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

void Scene::setParent(Entity child, Entity parent, bool keepWorldTransform)
{
    ASTRA_ASSERT(m_registry.valid(child), "setParent: invalid child");
    ASTRA_ASSERT(parent.isNull() || m_registry.valid(parent), "setParent: invalid parent");
    ASTRA_ASSERT(parent != child && (parent.isNull() || !isAncestor(child, parent)),
                 "setParent would create a cycle");

    // Only computed when needed: O(depth) per call.
    const Transform world = keepWorldTransform ? worldTransform(child) : Transform{};
    detach(child);
    if (!parent.isNull()) {
        if (!m_registry.has<Relationship>(parent)) {
            m_registry.emplace<Relationship>(parent);
        }
        if (!m_registry.has<Relationship>(child)) {
            m_registry.emplace<Relationship>(child);
        }
        // Append after the last child.
        auto& parentLinks = m_registry.get<Relationship>(parent);
        auto& childLinks = m_registry.get<Relationship>(child);
        childLinks.parent = parent;
        childLinks.previousSibling = parentLinks.lastChild;
        childLinks.nextSibling = kNullEntity;
        if (parentLinks.lastChild.isNull()) {
            parentLinks.firstChild = child;
        } else {
            m_registry.get<Relationship>(parentLinks.lastChild).nextSibling = child;
        }
        parentLinks.lastChild = child;
        ++parentLinks.children;
    }
    if (keepWorldTransform) {
        if (auto* transform = m_registry.tryGet<Transform>(child)) {
            *transform = parent.isNull() ? world : relativeTo(worldTransform(parent), world);
        }
    }
}

void Scene::detach(Entity child)
{
    auto* links = m_registry.tryGet<Relationship>(child);
    if (links == nullptr || links->parent.isNull()) {
        return;
    }
    if (auto* parentLinks = m_registry.tryGet<Relationship>(links->parent)) {
        if (parentLinks->firstChild == child) {
            parentLinks->firstChild = links->nextSibling;
        }
        if (parentLinks->lastChild == child) {
            parentLinks->lastChild = links->previousSibling;
        }
        --parentLinks->children;
    }
    if (auto* previous = m_registry.tryGet<Relationship>(links->previousSibling)) {
        previous->nextSibling = links->nextSibling;
    }
    if (auto* next = m_registry.tryGet<Relationship>(links->nextSibling)) {
        next->previousSibling = links->previousSibling;
    }
    links->parent = kNullEntity;
    links->previousSibling = kNullEntity;
    links->nextSibling = kNullEntity;
}

void Scene::onRelationshipDestroyed(Entity entity)
{
    detach(entity);
    // Children left behind (Registry::destroy() on a parent) become roots.
    auto& links = m_registry.get<Relationship>(entity);
    for (Entity child = links.firstChild; !child.isNull();) {
        auto& childLinks = m_registry.get<Relationship>(child);
        const Entity next = childLinks.nextSibling;
        childLinks.parent = kNullEntity;
        childLinks.previousSibling = kNullEntity;
        childLinks.nextSibling = kNullEntity;
        child = next;
    }
    links.firstChild = kNullEntity;
    links.lastChild = kNullEntity;
    links.children = 0;
}

Entity Scene::parent(Entity entity) const
{
    const auto* links = m_registry.tryGet<Relationship>(entity);
    return links != nullptr ? links->parent : kNullEntity;
}

std::vector<Entity> Scene::children(Entity entity) const
{
    std::vector<Entity> result;
    const auto* links = m_registry.tryGet<Relationship>(entity);
    if (links == nullptr) {
        return result;
    }
    result.reserve(links->children);
    for (Entity child = links->firstChild; !child.isNull();
         child = m_registry.get<Relationship>(child).nextSibling) {
        result.push_back(child);
    }
    return result;
}

Transform Scene::localTransform(Entity entity, float alpha) const
{
    const auto* local = m_registry.tryGet<Transform>(entity);
    if (local == nullptr) {
        return Transform{};
    }
    const auto* history = m_registry.tryGet<Interpolated>(entity);
    if (history == nullptr || !history->valid || alpha >= 1.0F) {
        return *local;
    }
    return interpolate(history->previous, *local, alpha);
}

Transform Scene::worldTransform(Entity entity, float alpha) const
{
    Transform world = localTransform(entity, alpha);
    for (Entity ancestor = parent(entity); !ancestor.isNull(); ancestor = parent(ancestor)) {
        if (m_registry.has<Transform>(ancestor)) {
            world = compose(localTransform(ancestor, alpha), world);
        }
    }
    return world;
}

void Scene::resetInterpolation(Entity entity)
{
    if (auto* history = m_registry.tryGet<Interpolated>(entity)) {
        history->valid = false;
    }
}

// ---------------------------------------------------------------------------------------------
// Systems

void Scene::addSystem(SystemPhase phase, std::string name, System system)
{
    ASTRA_ASSERT(
        std::ranges::none_of(m_systems, [&](const RegisteredSystem& s) { return s.name == name; }),
        "a system called '{}' exists already", name);
    m_systems.push_back({.phase = phase, .name = std::move(name), .run = std::move(system)});
}

bool Scene::removeSystem(std::string_view name)
{
    ASTRA_ASSERT(m_runningSystems == 0, "removeSystem('{}') while systems are running", name);
    return std::erase_if(m_systems, [&](const RegisteredSystem& s) { return s.name == name; }) > 0;
}

std::vector<std::string> Scene::systemNames(SystemPhase phase) const
{
    std::vector<std::string> names;
    for (const RegisteredSystem& system : m_systems) {
        if (system.phase == phase) {
            names.push_back(system.name);
        }
    }
    return names;
}

void Scene::runPhase(SystemPhase phase, float deltaTime)
{
    // By index, up to the systems present now: a system may add systems, which run from the next
    // step on. m_systems is a deque, so appending does not move the running one.
    const std::size_t count = m_systems.size();
    ++m_runningSystems;
    try {
        for (std::size_t i = 0; i < count; ++i) {
            const RegisteredSystem& system = m_systems[i];
            if (system.phase == phase) {
                ASTRA_PROFILE_SCOPE(system.name);
                system.run(*this, deltaTime);
            }
        }
    } catch (...) {
        --m_runningSystems;
        throw;
    }
    --m_runningSystems;
}

void Scene::fixedUpdate(float fixedDeltaTime)
{
    // The state before this step; render() blends from it to the state after it.
    m_registry.view<Transform, Interpolated>().each(
        [](const Transform& transform, Interpolated& history) {
        history.previous = transform;
        history.valid = true;
    });
    runPhase(SystemPhase::FixedUpdate, fixedDeltaTime);
    flushDestroyed();
}

void Scene::update(float deltaTime)
{
    runPhase(SystemPhase::Update, deltaTime);
    runPhase(SystemPhase::LateUpdate, deltaTime);
    flushDestroyed();
}

// ---------------------------------------------------------------------------------------------
// Rendering

void Scene::collectDrawItems(Entity root, float alpha)
{
    // Depth-first with an explicit stack: parents before children, siblings in order.
    std::vector<PendingDraw>& stack = m_drawStack;
    std::vector<Entity>& siblings = m_siblings;
    stack.clear();
    stack.push_back({.entity = root, .parentWorld = Transform{}});
    while (!stack.empty()) {
        const PendingDraw current = stack.back();
        stack.pop_back();
        const Transform world =
            m_registry.has<Transform>(current.entity)
                ? compose(current.parentWorld, localTransform(current.entity, alpha))
                : current.parentWorld;
        if (const auto* sprite = m_registry.tryGet<SpriteRenderer>(current.entity);
            sprite != nullptr && sprite->visible) {
            m_drawItems.push_back({.layer = sprite->layer, .world = world, .sprite = sprite});
        }
        // Pushed in reverse, so the first child is popped first. The buffer keeps its capacity
        // between frames, so rendering does not allocate.
        siblings.clear();
        if (const auto* links = m_registry.tryGet<Relationship>(current.entity)) {
            for (Entity child = links->firstChild; !child.isNull();
                 child = m_registry.get<Relationship>(child).nextSibling) {
                siblings.push_back(child);
            }
        }
        for (const Entity child : std::views::reverse(siblings)) {
            stack.push_back({.entity = child, .parentWorld = world});
        }
    }
}

void Scene::render(Renderer2D& renderer, float alpha)
{
    ASTRA_PROFILE_SCOPE("scene render");
    // World transforms are computed top-down once per frame: every root, then its subtree.
    m_drawItems.clear();
    m_registry.forEach([this, alpha](Entity entity) {
        if (parent(entity).isNull()) {
            collectDrawItems(entity, alpha);
        }
    });
    // Stable: within a layer, the traversal order (parents before children) is kept.
    std::ranges::stable_sort(m_drawItems, {}, &DrawItem::layer);

    for (const DrawItem& item : m_drawItems) {
        const SpriteRenderer& sprite = *item.sprite;
        const glm::vec2 size = sprite.size * glm::abs(item.world.scale);
        if (sprite.sprite.texture != nullptr) {
            renderer.drawSprite(item.world.position, size, sprite.sprite, sprite.tint,
                                item.world.rotation, sprite.flipX);
        } else {
            renderer.drawQuad(item.world.position, size, sprite.tint, item.world.rotation);
        }
    }
}

} // namespace astra2d
