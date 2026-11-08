#pragma once

#include "astra2d/core/assert.hpp"
#include "astra2d/ecs/entity.hpp"
#include "astra2d/core/signal.hpp"
#include "astra2d/ecs/sparse_set.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

/// @addtogroup ecs
/// @{

namespace astra2d {

namespace detail {

// A small number per component type, assigned on first use, to index the registry's pools
// directly (no hashing). Shared by all registries.
std::size_t nextComponentId();

template <typename Component>
std::size_t componentId()
{
    static const std::size_t id = nextComponentId();
    return id;
}

} // namespace detail

template <typename... Components>
class View;

/// Owns all entities and their components.
///
/// An entity is only an id (Entity); its data are the components attached to it, plain structs
/// stored per type in a ComponentPool (a sparse set). Systems are functions that iterate a View
/// of the entities that have certain components:
///
/// @code
/// Registry registry;
/// const Entity ball = registry.create();
/// registry.emplace<Position>(ball, glm::vec2{0, 0});
/// registry.emplace<Velocity>(ball, glm::vec2{10, 0});
///
/// registry.view<Position, Velocity>().each([dt](Position& p, const Velocity& v) {
///     p.value += v.value * dt;
/// });
/// @endcode
///
/// - Components are any movable types; no base class or registration is needed.
/// - Using a destroyed (stale) handle, or getting a component an entity does not have, is a
///   programmer error caught by ASTRA_ASSERT; tryGet() and valid() are the checking variants.
/// - References to a component stay valid until a component of the same type is added or removed.
/// - Not thread-safe.
class Registry {
public:
    Registry() = default;
    ~Registry() = default;
    // Pools hold signals with handlers that may capture this registry: not copyable or movable.
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    // --- Entities ------------------------------------------------------------------------------

    /// Creates an entity without components. Reuses the slot of a destroyed entity if there is
    /// one, with a new generation.
    /// @throws std::length_error if Entity::kMaxEntities entities are alive.
    [[nodiscard]] Entity create();

    /// Removes all components of @p entity (emitting their onDestroy signals) and frees its slot.
    /// onDestroy handlers may read the entity's components and change other entities, but must
    /// not add components to the entity being destroyed.
    /// @pre valid(entity).
    void destroy(Entity entity);

    /// True if @p entity was created by this registry and not destroyed since.
    [[nodiscard]] bool valid(Entity entity) const
    {
        const std::uint32_t index = entity.index();
        return index < m_generations.size() && m_alive[index] &&
               m_generations[index] == entity.generation();
    }

    /// Number of living entities.
    [[nodiscard]] std::size_t alive() const { return m_aliveCount; }

    /// Calls @p function with every living entity, in index order.
    template <typename Function>
    void forEach(Function function) const
    {
        for (std::uint32_t index = 0; index < m_generations.size(); ++index) {
            if (m_alive[index]) {
                function(Entity::fromParts(index, m_generations[index]));
            }
        }
    }

    /// Destroys every entity, emitting onDestroy for each component. Old handles stay invalid.
    void clear();

    // --- Components ------------------------------------------------------------------------------

    /// Adds a @p Component built from @p args to @p entity (aggregates use brace initialization,
    /// so `emplace<Position>(e, glm::vec2{1, 2})` works for `struct Position { glm::vec2 value;
    /// }`).
    /// @pre valid(entity), the entity does not have the component yet and is not being destroyed.
    /// @throws std::logic_error if an onConstruct handler removed the new component again.
    template <typename Component, typename... Args>
    Component& emplace(Entity entity, Args&&... args)
    {
        ASTRA_ASSERT(valid(entity), "emplace on an invalid entity");
        ASTRA_ASSERT(!m_dying[entity.index()],
                     "cannot add components to an entity that is being destroyed");
        ComponentPool<Component>& pool = assure<Component>();
        ASTRA_ASSERT(!pool.contains(entity), "entity already has this component");
        return pool.emplace(*this, entity, std::forward<Args>(args)...);
    }

    /// Adds the component, or replaces it (emitting onDestroy, then onConstruct) if present.
    /// @pre valid(entity).
    template <typename Component, typename... Args>
    Component& emplaceOrReplace(Entity entity, Args&&... args)
    {
        ASTRA_ASSERT(valid(entity), "emplaceOrReplace on an invalid entity");
        ASTRA_ASSERT(!m_dying[entity.index()],
                     "cannot add components to an entity that is being destroyed");
        ComponentPool<Component>& pool = assure<Component>();
        pool.remove(*this, entity);
        return pool.emplace(*this, entity, std::forward<Args>(args)...);
    }

    /// Removes the component from @p entity, emitting onDestroy first. Returns false if the entity
    /// did not have it. @pre valid(entity).
    template <typename Component>
    bool remove(Entity entity)
    {
        ASTRA_ASSERT(valid(entity), "remove on an invalid entity");
        ComponentPool<Component>* pool = poolOf<Component>();
        return pool != nullptr && pool->remove(*this, entity);
    }

    /// True if @p entity has all of @p Components. False for invalid entities.
    template <typename... Components>
    [[nodiscard]] bool has(Entity entity) const
    {
        static_assert(sizeof...(Components) > 0);
        if (!valid(entity)) {
            return false;
        }
        return (containedIn<Components>(entity) && ...);
    }

    /// The component of @p entity. @pre the entity is valid and has the component.
    template <typename Component>
    [[nodiscard]] Component& get(Entity entity)
    {
        auto* component = tryGet<Component>(entity);
        ASTRA_ASSERT(component != nullptr, "entity does not have the requested component");
        return *component;
    }
    /// The component of @p entity. @pre the entity is valid and has the component.
    template <typename Component>
    [[nodiscard]] const Component& get(Entity entity) const
    {
        const auto* component = tryGet<Component>(entity);
        ASTRA_ASSERT(component != nullptr, "entity does not have the requested component");
        return *component;
    }

    /// The component of @p entity, or nullptr if the entity is invalid or does not have it.
    template <typename Component>
    [[nodiscard]] Component* tryGet(Entity entity)
    {
        ComponentPool<Component>* pool = poolOf<Component>();
        return pool != nullptr && valid(entity) && pool->contains(entity) ? &pool->get(entity)
                                                                          : nullptr;
    }
    /// The component of @p entity, or nullptr if the entity is invalid or does not have it.
    template <typename Component>
    [[nodiscard]] const Component* tryGet(Entity entity) const
    {
        const ComponentPool<Component>* pool = poolOf<Component>();
        return pool != nullptr && valid(entity) && pool->contains(entity) ? &pool->get(entity)
                                                                          : nullptr;
    }

    /// Number of entities that have @p Component.
    template <typename Component>
    [[nodiscard]] std::size_t count() const
    {
        const ComponentPool<Component>* pool = poolOf<Component>();
        return pool != nullptr ? pool->size() : 0;
    }

    /// The entities that have all of @p Components. See View for the iteration rules.
    template <typename... Components>
    [[nodiscard]] View<Components...> view()
    {
        return View<Components...>(*this);
    }

    /// The storage of @p Component, created on first use.
    template <typename Component>
    [[nodiscard]] ComponentPool<Component>& storage()
    {
        return assure<Component>();
    }

    // --- Signals ---------------------------------------------------------------------------------

    /// Emitted after a @p Component was added to an entity: `(Registry&, Entity)`.
    /// @code
    /// Connection c = registry.onConstruct<Enemy>().connect([](Registry&, Entity e) { ... });
    /// @endcode
    template <typename Component>
    [[nodiscard]] Signal<Registry&, Entity>& onConstruct()
    {
        return assure<Component>().onConstruct();
    }

    /// Emitted before a @p Component is removed from an entity, also when the entity is destroyed;
    /// the component can still be read in the handler.
    template <typename Component>
    [[nodiscard]] Signal<Registry&, Entity>& onDestroy()
    {
        return assure<Component>().onDestroy();
    }

private:
    template <typename Component>
    ComponentPool<Component>& assure()
    {
        static_assert(std::is_move_constructible_v<Component> &&
                          std::is_move_assignable_v<Component>,
                      "components must be movable (they move when others are removed)");
        static_assert(!std::is_reference_v<Component> && !std::is_const_v<Component>,
                      "use the plain component type");
        const std::size_t id = detail::componentId<Component>();
        if (id >= m_pools.size()) {
            m_pools.resize(id + 1);
        }
        if (!m_pools[id]) {
            m_pools[id] = std::make_unique<ComponentPool<Component>>();
        }
        return static_cast<ComponentPool<Component>&>(*m_pools[id]);
    }

    template <typename Component>
    [[nodiscard]] ComponentPool<Component>* poolOf()
    {
        const std::size_t id = detail::componentId<Component>();
        return id < m_pools.size() ? static_cast<ComponentPool<Component>*>(m_pools[id].get())
                                   : nullptr;
    }

    template <typename Component>
    [[nodiscard]] const ComponentPool<Component>* poolOf() const
    {
        const std::size_t id = detail::componentId<Component>();
        return id < m_pools.size() ? static_cast<const ComponentPool<Component>*>(m_pools[id].get())
                                   : nullptr;
    }

    template <typename Component>
    [[nodiscard]] bool containedIn(Entity entity) const
    {
        const ComponentPool<Component>* pool = poolOf<Component>();
        return pool != nullptr && pool->contains(entity);
    }

    template <typename... Components>
    friend class View;

    std::vector<std::unique_ptr<SparseSet>> m_pools; // by detail::componentId
    std::vector<std::uint32_t> m_generations;        // current generation per index
    std::vector<bool> m_alive;                       // per index
    std::vector<bool> m_dying;                       // per index: inside destroy()
    std::vector<std::uint32_t> m_free;               // destroyed indices to reuse
    std::size_t m_aliveCount = 0;
};

/// The entities that have all of @p Components, and their components.
///
/// Iteration walks the **smallest** of the component pools and checks the others with a sparse
/// lookup, so `view<Transform, Boss>()` costs as many steps as there are bosses, not transforms.
///
/// @code
/// registry.view<Transform, Velocity>().each([dt](Entity e, Transform& t, Velocity& v) { ... });
/// registry.view<Transform, Velocity>().each([dt](Transform& t, Velocity& v) { ... });
/// for (const Entity e : registry.view<Enemy>()) { ... }
/// @endcode
///
/// **Changing the registry while iterating.** Entities are visited from the end of the pool to the
/// front, so while a view is iterated it is safe to:
/// - remove components from, or destroy, the entity being visited;
/// - add components to any entity, as long as they are not of a type in @p Components;
/// - create entities (they are not visited).
///
/// Destroying *other* entities, or adding components of the iterated types, can move an entity
/// that was already visited into a position still to come, so it is visited twice. Collect such
/// entities and change them after the loop (Scene::destroyLater() does this for destruction).
template <typename... Components>
class View {
    static_assert(sizeof...(Components) > 0, "a view needs at least one component type");

public:
    /// A view of @p registry. Pools that do not exist yet make the view empty.
    explicit View(Registry& registry)
        : m_registry(&registry)
        , m_pools{registry.poolOf<Components>()...}
        , m_leading(smallestPool())
    {
    }

    /// Calls @p function for every matching entity, as `function(Entity, Components&...)` or
    /// `function(Components&...)`.
    template <typename Function>
    void each(Function function)
    {
        if (m_leading == nullptr) {
            return;
        }
        for (std::size_t i = m_leading->size(); i-- > 0;) {
            if (i >= m_leading->size()) {
                continue; // more than one entity was removed during the last call
            }
            const Entity entity = m_leading->entities()[i];
            // One sparse lookup per non-leading pool; the leading pool's position is i.
            const std::tuple<Components*...> components{componentAt<Components>(entity, i)...};
            if (((std::get<Components*>(components) == nullptr) || ...)) {
                continue;
            }
            if constexpr (std::is_invocable_v<Function&, Entity, Components&...>) {
                function(entity, *std::get<Components*>(components)...);
            } else {
                function(*std::get<Components*>(components)...);
            }
        }
    }

    /// The component @p Component (one of @p Components) of @p entity. @pre contains(entity).
    template <typename Component>
    [[nodiscard]] Component& get(Entity entity)
    {
        return std::get<ComponentPool<Component>*>(m_pools)->get(entity);
    }

    /// True if @p entity has all of @p Components.
    [[nodiscard]] bool contains(Entity entity) const
    {
        return m_leading != nullptr && m_registry->valid(entity) && matches(entity);
    }

    /// Exact number of matching entities (walks the smallest pool).
    [[nodiscard]] std::size_t count() const
    {
        if (m_leading == nullptr) {
            return 0;
        }
        std::size_t result = 0;
        for (const Entity entity : m_leading->entities()) {
            result += matches(entity) ? 1U : 0U;
        }
        return result;
    }

    /// Iterates the matching entities, in the same back-to-front order as each().
    class Iterator {
    public:
        using value_type = Entity;              ///< Iterated type.
        using difference_type = std::ptrdiff_t; ///< Distance type.

        Iterator() = default;
        /// Iterator positioned before element @p position of the leading pool.
        Iterator(const View* view, std::size_t position)
            : m_view(view)
            , m_position(position)
        {
            skipNonMatching();
        }
        /// The current entity.
        [[nodiscard]] Entity operator*() const
        {
            return m_view->m_leading->entities()[m_position - 1];
        }
        /// Moves to the next matching entity.
        Iterator& operator++()
        {
            --m_position;
            skipNonMatching();
            return *this;
        }
        /// Moves to the next matching entity.
        Iterator operator++(int)
        {
            Iterator old = *this;
            ++*this;
            return old;
        }
        /// Iterators are equal at the same position.
        friend bool operator==(const Iterator& a, const Iterator& b)
        {
            return a.m_position == b.m_position;
        }

    private:
        void skipNonMatching()
        {
            while (m_position > 0) {
                const SparseSet& leading = *m_view->m_leading;
                m_position = std::min(m_position, leading.size());
                if (m_position == 0 || m_view->matches(leading.entities()[m_position - 1])) {
                    return;
                }
                --m_position;
            }
        }

        const View* m_view = nullptr;
        std::size_t m_position = 0; // one past the current element
    };

    /// First matching entity.
    [[nodiscard]] Iterator begin() const
    {
        return Iterator(this, m_leading != nullptr ? m_leading->size() : 0);
    }
    /// End of the iteration.
    [[nodiscard]] Iterator end() const { return Iterator(this, 0); }

private:
    // The pool with the fewest entities, or nullptr if a pool does not exist (empty view).
    [[nodiscard]] const SparseSet* smallestPool() const
    {
        const std::array<const SparseSet*, sizeof...(Components)> pools{
            static_cast<const SparseSet*>(std::get<ComponentPool<Components>*>(m_pools))...};
        const SparseSet* smallest = nullptr;
        for (const SparseSet* pool : pools) {
            if (pool == nullptr) {
                return nullptr;
            }
            if (smallest == nullptr || pool->size() < smallest->size()) {
                smallest = pool;
            }
        }
        return smallest;
    }

    // The component of @p entity in its pool, or nullptr; @p leadingIndex is the entity's
    // position in the leading pool, which saves the lookup there.
    template <typename Component>
    [[nodiscard]] Component* componentAt(Entity entity, std::size_t leadingIndex) const
    {
        ComponentPool<Component>* pool = std::get<ComponentPool<Component>*>(m_pools);
        if (static_cast<const SparseSet*>(pool) == m_leading) {
            return &pool->components()[leadingIndex];
        }
        // Entities that got their components in the same order sit at the same position in every
        // pool; checking that first replaces the sparse lookup with a sequential read.
        if (leadingIndex < pool->size() && pool->entities()[leadingIndex] == entity) {
            return &pool->components()[leadingIndex];
        }
        const std::size_t index = pool->indexOf(entity);
        return index != SparseSet::npos ? &pool->components()[index] : nullptr;
    }

    [[nodiscard]] bool matches(Entity entity) const
    {
        return (std::get<ComponentPool<Components>*>(m_pools)->contains(entity) && ...);
    }

    Registry* m_registry;
    std::tuple<ComponentPool<Components>*...> m_pools;
    const SparseSet* m_leading;
};

} // namespace astra2d

/// @}
