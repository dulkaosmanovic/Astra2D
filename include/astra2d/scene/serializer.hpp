#pragma once

#include "astra2d/ecs/entity.hpp"
#include "astra2d/ecs/registry.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// @addtogroup scene
/// @{

namespace astra2d {

class AssetCache;
class Scene;

/// Saves scenes to JSON and loads them back.
///
/// A scene file lists the entities, each with the components that have a registered converter
/// and its parent in the hierarchy:
///
/// @code{.json}
/// {
///   "format": "astra2d-scene", "version": 1,
///   "entities": [
///     { "id": 0, "parent": null, "components": { "Name": "sun", "Transform": { ... } } },
///     { "id": 1, "parent": 0,    "components": { "Name": "earth", ... } }
///   ]
/// }
/// @endcode
///
/// Built in: Name, Transform, SpriteRenderer (textures are stored by path, so an AssetCache is
/// needed), RigidBody, Collider; the hierarchy (Relationship) is stored as `parent`. Components of
/// the game, or ones that refer to run-time data the engine cannot name (Animator points at
/// animations), are added with registerComponent():
///
/// @code
/// struct Health { int value; };
/// serializer.registerComponent<Health>(
///     "Health",
///     [](const Health& h, const SceneSerializer::SaveContext&) { return nlohmann::json(h.value);
///     },
///     [](const nlohmann::json& j, const SceneSerializer::LoadContext&) {
///         return Health{j.get<int>()};
///     });
/// @endcode
///
/// Entity references inside components are saved with SaveContext::id() and restored with
/// LoadContext::entity(), because entity handles are different after loading.
class SceneSerializer {
public:
    /// Passed to save converters.
    class SaveContext {
    public:
        /// The id @p entity has in the file (`null` for the null entity or an unsaved one).
        [[nodiscard]] nlohmann::json id(Entity entity) const;
        /// The asset cache given to the serializer, or nullptr.
        [[nodiscard]] const AssetCache* assets() const { return m_assets; }

    private:
        friend class SceneSerializer;
        const AssetCache* m_assets = nullptr;
        std::unordered_map<Entity, std::size_t> m_ids;
    };

    /// Passed to load converters.
    class LoadContext {
    public:
        /// The entity created for file id @p id (`null` gives the null entity).
        /// @throws std::runtime_error for an id that is not in the file.
        [[nodiscard]] Entity entity(const nlohmann::json& id) const;
        /// The asset cache given to the serializer, or nullptr.
        [[nodiscard]] AssetCache* assets() const { return m_assets; }

    private:
        friend class SceneSerializer;
        AssetCache* m_assets = nullptr;
        std::vector<Entity> m_entities;
    };

    /// Converts a component to JSON.
    template <typename Component>
    using SaveFunction = std::function<nlohmann::json(const Component&, const SaveContext&)>;
    /// Builds a component from JSON; may throw (nlohmann::json::exception, std::runtime_error).
    template <typename Component>
    using LoadFunction = std::function<Component(const nlohmann::json&, const LoadContext&)>;

    /// A serializer with the built-in components. @p assets resolves texture paths when loading
    /// and finds them when saving; without it, scenes with textured sprites cannot be saved or
    /// loaded (sprites without a texture can).
    explicit SceneSerializer(AssetCache* assets = nullptr);

    /// Adds a converter for @p Component, stored under @p name in the file. Registering a name
    /// again replaces the converter.
    template <typename Component>
    void registerComponent(std::string_view name, SaveFunction<Component> save,
                           LoadFunction<Component> load)
    {
        Converter converter{
            .name = std::string(name),
            .save = [save = std::move(save)](
                        const Registry& registry, Entity entity,
                        const SaveContext& context) -> std::optional<nlohmann::json> {
            const auto* component = registry.tryGet<Component>(entity);
            if (component == nullptr) {
                return std::nullopt;
            }
            return save(*component, context);
        },
            .load =
                [load = std::move(load)](Registry& registry, Entity entity,
                                         const nlohmann::json& json, const LoadContext& context) {
            registry.emplaceOrReplace<Component>(entity, load(json, context));
        },
        };
        addConverter(std::move(converter));
    }

    /// The scene as JSON. Entities are listed depth-first (each parent before its children,
    /// siblings in order), so loading rebuilds the same hierarchy.
    /// @throws std::runtime_error if a sprite's texture cannot be named (no AssetCache, or a
    /// texture that was not loaded through it).
    [[nodiscard]] nlohmann::json save(const Scene& scene) const;

    /// Adds the entities of @p json to @p scene (the scene is not cleared first) and returns them
    /// in file order. Unknown component names are skipped with a warning. All or nothing: on an
    /// error, the entities created so far are destroyed again.
    /// @throws std::runtime_error describing the first problem (with the entity id).
    std::vector<Entity> load(Scene& scene, const nlohmann::json& document) const;

    /// save() written to @p path, indented.
    /// @throws std::runtime_error if the file cannot be written.
    void saveFile(const Scene& scene, const std::filesystem::path& path) const;

    /// load() from the JSON file at @p path.
    /// @throws std::runtime_error if the file cannot be read or parsed.
    std::vector<Entity> loadFile(Scene& scene, const std::filesystem::path& path) const;

    /// Format version written to and accepted from files.
    static constexpr int kVersion = 1;

private:
    struct Converter {
        std::string name;
        std::function<std::optional<nlohmann::json>(const Registry&, Entity, const SaveContext&)>
            save;
        std::function<void(Registry&, Entity, const nlohmann::json&, const LoadContext&)> load;
    };

    void addConverter(Converter converter);
    void loadComponents(Registry& registry, Entity entity, const nlohmann::json& entry,
                        std::size_t index, const LoadContext& context) const;
    static void linkParent(Scene& scene, Entity entity, const nlohmann::json& entry,
                           const LoadContext& context);
    void registerBuiltIns();

    AssetCache* m_assets;
    std::vector<Converter> m_converters;
};

} // namespace astra2d

/// @}
