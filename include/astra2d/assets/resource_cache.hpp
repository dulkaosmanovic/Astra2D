#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

/// @addtogroup assets
/// @{

namespace astra2d {

/// Loads each resource once and hands out references to it.
///
/// Resources are stored behind stable pointers, so references stay valid until the entry is
/// erased or the cache is cleared or destroyed. A failed load (the loader throws) stores nothing,
/// so the next request tries again.
///
/// @code
/// ResourceCache<Texture2D> textures;
/// Texture2D& player = textures.get("player.png", [] { return Texture2D::fromFile("player.png");
/// });
/// @endcode
///
/// @tparam T Resource type; may be move-only.
template <typename T>
class ResourceCache {
public:
    /// Returns the resource stored under @p key, calling @p load (which returns a `T`) only if
    /// there is none yet.
    template <typename Loader>
    T& get(std::string_view key, Loader&& load)
    {
        if (const auto it = m_resources.find(key); it != m_resources.end()) {
            return *it->second;
        }
        auto resource = std::make_unique<T>(std::forward<Loader>(load)());
        T& reference = *resource;
        m_resources.emplace(std::string(key), std::move(resource));
        return reference;
    }

    /// The resource stored under @p key, or nullptr.
    [[nodiscard]] T* find(std::string_view key)
    {
        const auto it = m_resources.find(key);
        return it != m_resources.end() ? it->second.get() : nullptr;
    }

    /// True if a resource is stored under @p key.
    [[nodiscard]] bool contains(std::string_view key) const
    {
        return m_resources.find(key) != m_resources.end();
    }

    /// Removes the resource under @p key, if any. References to it become dangling.
    void erase(std::string_view key)
    {
        if (const auto it = m_resources.find(key); it != m_resources.end()) {
            m_resources.erase(it);
        }
    }

    /// Removes all resources. All references become dangling.
    void clear() { m_resources.clear(); }

    /// Number of stored resources.
    [[nodiscard]] std::size_t size() const { return m_resources.size(); }

private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    std::unordered_map<std::string, std::unique_ptr<T>, StringHash, std::equal_to<>> m_resources;
};

} // namespace astra2d

/// @}
