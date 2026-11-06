#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

/// @addtogroup ecs
/// @{

namespace astra2d {

/// A handle to an entity: a 32-bit value made of an **index** (low 20 bits) and a **generation**
/// (high 12 bits).
///
/// The index selects a slot in the registry and in every component storage. When an entity is
/// destroyed its slot is reused for a later entity, but with the generation increased by one. An
/// old handle to the destroyed entity keeps the old generation, so Registry::valid() can tell that
/// it is stale instead of silently referring to the new entity (the "ABA problem" of plain
/// indices).
///
/// Entity is a trivially copyable value type: store it, compare it, use it as a map key. The
/// default value is the null entity, which is never valid.
class Entity {
public:
    /// Bits of the index: up to 2^20 - 1 = 1 048 575 entities alive at the same time.
    static constexpr std::uint32_t kIndexBits = 20;
    /// Bits of the generation: a slot can be reused 2^12 - 1 = 4095 times before it is retired.
    static constexpr std::uint32_t kGenerationBits = 12;
    /// Mask of the index bits.
    static constexpr std::uint32_t kIndexMask = (1U << kIndexBits) - 1U;
    /// Mask of the generation bits (after shifting them down).
    static constexpr std::uint32_t kGenerationMask = (1U << kGenerationBits) - 1U;
    /// The index of the null entity; never used by a real entity.
    static constexpr std::uint32_t kNullIndex = kIndexMask;
    /// Number of usable indices.
    static constexpr std::uint32_t kMaxEntities = kNullIndex;

    /// The null entity.
    constexpr Entity() = default;

    /// Builds a handle from its parts (both are masked to their bit widths).
    [[nodiscard]] static constexpr Entity fromParts(std::uint32_t index, std::uint32_t generation)
    {
        return Entity{(index & kIndexMask) | ((generation & kGenerationMask) << kIndexBits)};
    }
    /// Rebuilds a handle from value(), e.g. after storing it as a plain number.
    [[nodiscard]] static constexpr Entity fromValue(std::uint32_t value) { return Entity{value}; }

    /// Slot index (low 20 bits).
    [[nodiscard]] constexpr std::uint32_t index() const { return m_value & kIndexMask; }
    /// Generation (high 12 bits).
    [[nodiscard]] constexpr std::uint32_t generation() const
    {
        return (m_value >> kIndexBits) & kGenerationMask;
    }
    /// The whole 32-bit value.
    [[nodiscard]] constexpr std::uint32_t value() const { return m_value; }

    /// True for the null entity (any generation).
    [[nodiscard]] constexpr bool isNull() const { return index() == kNullIndex; }
    /// True unless this is the null entity. It may still be stale; see Registry::valid().
    constexpr explicit operator bool() const { return !isNull(); }

    /// Handles are equal when index and generation are equal.
    friend constexpr bool operator==(Entity, Entity) = default;

private:
    constexpr explicit Entity(std::uint32_t value)
        : m_value(value)
    {
    }

    std::uint32_t m_value = kNullIndex;
};

/// The null entity, e.g. "no parent".
inline constexpr Entity kNullEntity{};

static_assert(sizeof(Entity) == 4, "Entity must stay a 32-bit handle");

} // namespace astra2d

/// Hash support, so Entity can be a key of std::unordered_map / std::unordered_set.
template <>
struct std::hash<astra2d::Entity> {
    /// Hashes the 32-bit value.
    std::size_t operator()(astra2d::Entity entity) const noexcept
    {
        return std::hash<std::uint32_t>{}(entity.value());
    }
};

/// @}
