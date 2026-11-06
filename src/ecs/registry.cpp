#include "astra2d/ecs/registry.hpp"

#include <ranges>
#include <stdexcept>

namespace astra2d {

namespace detail {

std::size_t nextComponentId()
{
    static std::size_t next = 0;
    return next++;
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
    }
    m_alive[index] = true;
    ++m_aliveCount;
    return Entity::fromParts(index, m_generations[index]);
}

void Registry::destroy(Entity entity)
{
    ASTRA_ASSERT(valid(entity), "destroy on an invalid entity");
    // Newest pools first; the order only matters to onDestroy handlers, which may still read the
    // entity's other components.
    for (const auto& pool : std::views::reverse(m_pools)) {
        if (pool) {
            pool->remove(*this, entity);
        }
    }
    // A handler may have destroyed the entity already (e.g. a recursive destroy).
    if (!valid(entity)) {
        return;
    }
    const std::uint32_t index = entity.index();
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
