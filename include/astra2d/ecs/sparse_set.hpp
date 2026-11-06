#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/ecs/signal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

/// @addtogroup ecs
/// @{

namespace astra2d {

class Registry;

/// The set of entities that have one component type, without the components themselves.
///
/// Two arrays make up a sparse set:
/// - **dense**: the entities, packed without gaps. Iterating a component type walks this array
///   (and the components stored in the same order), so it touches only memory that is in use.
/// - **sparse**: for every entity index, the position of that entity in the dense array. It makes
///   contains(), add and remove O(1). It is split into pages of kPageSize entries that are only
///   allocated when an entity in their range gets the component, so a component that only a few
///   entities with high indices have does not cost a full-size array.
///
/// Removal moves the last dense element into the gap ("swap and pop"), so the order of the dense
/// array changes and positions are not stable.
///
/// ComponentPool adds the component values; this base part is what the Registry needs to treat
/// all component types alike (e.g. to remove an entity from every pool when it is destroyed).
class SparseSet {
public:
    /// Entries per sparse page.
    static constexpr std::size_t kPageSize = 4096;

    SparseSet() = default;
    virtual ~SparseSet() = default;
    SparseSet(const SparseSet&) = delete;
    SparseSet& operator=(const SparseSet&) = delete;
    SparseSet(SparseSet&&) = delete;
    SparseSet& operator=(SparseSet&&) = delete;

    /// True if @p entity (this exact generation) is in the set.
    [[nodiscard]] bool contains(Entity entity) const
    {
        const std::uint32_t position = find(entity.index());
        return position != kAbsent && m_dense[position] == entity;
    }

    /// Position of @p entity in entities(). @pre contains(entity).
    [[nodiscard]] std::size_t position(Entity entity) const { return find(entity.index()); }

    /// Returned by indexOf() for an entity that is not in the set.
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    /// Position of @p entity in entities(), or npos if it is not in the set: contains() and
    /// position() in one sparse lookup.
    [[nodiscard]] std::size_t indexOf(Entity entity) const
    {
        const std::uint32_t position = find(entity.index());
        return position != kAbsent && m_dense[position] == entity ? position : npos;
    }

    /// Number of entities in the set.
    [[nodiscard]] std::size_t size() const { return m_dense.size(); }
    /// True if the set is empty.
    [[nodiscard]] bool empty() const { return m_dense.empty(); }

    /// The entities, packed, in storage order.
    [[nodiscard]] std::span<const Entity> entities() const { return m_dense; }

    /// Removes @p entity and its component if present: emits onDestroy first, while the component
    /// can still be read. Returns true if it was present.
    bool remove(Registry& registry, Entity entity)
    {
        if (!contains(entity)) {
            return false;
        }
        m_onDestroy.emit(registry, entity);
        // A handler may already have removed it.
        if (!contains(entity)) {
            return true;
        }
        erase(entity);
        return true;
    }

    /// Removes @p entity and its component if present, without emitting signals.
    void discard(Entity entity)
    {
        if (contains(entity)) {
            erase(entity);
        }
    }

    /// Emitted after a component of this type was added: `(Registry&, Entity)`.
    [[nodiscard]] Signal<Registry&, Entity>& onConstruct() { return m_onConstruct; }
    /// Emitted before a component of this type is removed (also when its entity is destroyed),
    /// while it can still be read: `(Registry&, Entity)`.
    [[nodiscard]] Signal<Registry&, Entity>& onDestroy() { return m_onDestroy; }

    /// Removes every entity without emitting signals.
    virtual void clear()
    {
        m_dense.clear();
        m_sparse.clear();
    }

protected:
    /// Sparse entry of an entity index that is not in the set.
    static constexpr std::uint32_t kAbsent = 0xFFFFFFFFU;

    /// Appends @p entity to the dense array. @pre !contains(entity).
    void insertEntity(Entity entity)
    {
        slot(entity.index()) = static_cast<std::uint32_t>(m_dense.size());
        m_dense.push_back(entity);
    }

    /// Removes @p entity with swap-and-pop and lets the subclass do the same with its values.
    void erase(Entity entity)
    {
        const std::uint32_t position = find(entity.index());
        const auto last = static_cast<std::uint32_t>(m_dense.size() - 1);
        if (position != last) {
            const Entity moved = m_dense[last];
            m_dense[position] = moved;
            slot(moved.index()) = position;
        }
        swapAndPopValue(position, last);
        m_dense.pop_back();
        slot(entity.index()) = kAbsent;
    }

    /// Moves the value at @p last to @p position (unless equal) and removes the last value.
    virtual void swapAndPopValue(std::size_t position, std::size_t last) = 0;

    /// Emits onConstruct for @p entity.
    void constructed(Registry& registry, Entity entity) { m_onConstruct.emit(registry, entity); }

private:
    using Page = std::array<std::uint32_t, kPageSize>;

    [[nodiscard]] std::uint32_t find(std::uint32_t index) const
    {
        const std::size_t page = index / kPageSize;
        if (page >= m_sparse.size() || !m_sparse[page]) {
            return kAbsent;
        }
        return (*m_sparse[page])[index % kPageSize];
    }

    std::uint32_t& slot(std::uint32_t index)
    {
        const std::size_t page = index / kPageSize;
        if (page >= m_sparse.size()) {
            m_sparse.resize(page + 1);
        }
        if (!m_sparse[page]) {
            m_sparse[page] = std::make_unique<Page>();
            m_sparse[page]->fill(kAbsent);
        }
        return (*m_sparse[page])[index % kPageSize];
    }

    std::vector<std::unique_ptr<Page>> m_sparse;
    std::vector<Entity> m_dense;
    Signal<Registry&, Entity> m_onConstruct;
    Signal<Registry&, Entity> m_onDestroy;
};

/// A sparse set that also stores one @p Component per entity, packed in the same order as the
/// entities, so iteration reads both arrays front to back.
///
/// References to components stay valid until a component of the same type is added (the array
/// may grow) or removed (the last one moves into the gap).
template <typename Component>
class ComponentPool final : public SparseSet {
public:
    /// Adds @p entity with a component constructed from @p args. @pre !contains(entity).
    template <typename... Args>
    Component& emplace(Registry& registry, Entity entity, Args&&... args)
    {
        if constexpr (std::is_aggregate_v<Component>) {
            m_components.push_back(Component{std::forward<Args>(args)...});
        } else {
            m_components.emplace_back(std::forward<Args>(args)...);
        }
        insertEntity(entity);
        constructed(registry, entity);
        // Looked up again: an onConstruct handler may have added components and grown the array,
        // or even removed this one (or destroyed the entity), so there is nothing to return.
        const std::size_t index = indexOf(entity);
        if (index == npos) {
            throw std::logic_error("an onConstruct handler removed the component it was notified "
                                   "about, so emplace() has nothing to return");
        }
        return m_components[index];
    }

    /// The component of @p entity. @pre contains(entity).
    [[nodiscard]] Component& get(Entity entity) { return m_components[position(entity)]; }
    /// The component of @p entity. @pre contains(entity).
    [[nodiscard]] const Component& get(Entity entity) const
    {
        return m_components[position(entity)];
    }

    /// The components, in the same order as entities().
    [[nodiscard]] std::span<Component> components() { return m_components; }
    /// The components, in the same order as entities().
    [[nodiscard]] std::span<const Component> components() const { return m_components; }

    /// Removes every entity and component without emitting signals.
    // ComponentPool is always created by Registry::assure(), which instantiates every member.
    // NOLINTNEXTLINE(portability-template-virtual-member-function)
    void clear() override
    {
        SparseSet::clear();
        m_components.clear();
    }

protected:
    // NOLINTNEXTLINE(portability-template-virtual-member-function)
    void swapAndPopValue(std::size_t position, std::size_t last) override
    {
        if (position != last) {
            m_components[position] = std::move(m_components[last]);
        }
        m_components.pop_back();
    }

private:
    std::vector<Component> m_components;
};

} // namespace astra2d

/// @}
