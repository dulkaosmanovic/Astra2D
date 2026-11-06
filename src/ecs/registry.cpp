#include "astra2d/ecs/registry.hpp"

#include <atomic>
#include <stdexcept>

namespace astra2d {

namespace detail {

std::size_t nextComponentId()
{
    // Shared by all registries, which may live on different threads (one thread each).
    static std::atomic<std::size_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace detail

Entity Registry::create()
{
    std::uint32_t index = 0;
    if (!m_free.empty()) {
        index = m_free.back();
        m_free.pop_back();
    } else {
        if (m_generations.size() >= Entity::kMaxEntities) {
            throw std::length_error("Registry: too many entities (the 20-bit index is exhausted)");
        }
        index = static_cast<std::uint32_t>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(false);
        m_dying.push_back(false);
    }
    m_alive[index] = true;
    ++m_aliveCount;
    return Entity::fromParts(index, m_generations[index]);
}

void Registry::destroy(Entity entity)
{
    ASTRA_ASSERT(valid(entity), "destroy on an invalid entity");
    const std::uint32_t index = entity.index();
    if (m_dying[index]) {
        return; // a handler destroys the entity that is already being destroyed
    }
    m_dying[index] = true;
    // Newest pools first; the order only matters to onDestroy handlers, which may still read the
    // entity's other components. By index, not by iterator: a handler that uses a component type
    // for the first time adds a pool and may reallocate m_pools. Pools added meanwhile have higher
    // indices and cannot hold this entity (adding components to it is not allowed, see emplace()).
    for (std::size_t i = m_pools.size(); i-- > 0;) {
        if (m_pools[i]) {
            m_pools[i]->remove(*this, entity);
        }
    }
    // Defensive, for release builds where emplace()'s assertion is compiled out: a component a
    // handler added to the dying entity anyway is dropped, so no pool keeps a dead entry that a
    // later entity with the same index could pick up.
    for (const auto& pool : m_pools) {
        if (pool) {
            pool->discard(entity);
        }
    }
    m_dying[index] = false;
    // A handler may have destroyed the entity already (e.g. a recursive destroy).
    if (!valid(entity)) {
        return;
    }
    m_alive[index] = false;
    --m_aliveCount;
    // A slot whose generation would wrap around is retired instead of reused, so an old handle
    // can never become valid again.
    if (m_generations[index] < Entity::kGenerationMask) {
        ++m_generations[index];
        m_free.push_back(index);
    }
}

void Registry::clear()
{
    // Generations are kept (destroy() increments them), so handles from before clear() stay
    // invalid after new entities reuse their slots.
    forEach([this](Entity entity) {
        if (valid(entity)) {
            destroy(entity);
        }
    });
}

} // namespace astra2d
