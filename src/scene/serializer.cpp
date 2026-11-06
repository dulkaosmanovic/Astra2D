#include "astra2d/scene/serializer.hpp"

#include "astra2d/assets/asset_cache.hpp"
#include "astra2d/core/log.hpp"
#include "astra2d/renderer/texture.hpp"
#include "astra2d/scene/components.hpp"
#include "astra2d/scene/scene.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <exception>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace astra2d {

namespace {

using nlohmann::json;

constexpr std::string_view kFormat = "astra2d-scene";

// JSON numbers are doubles; a float such as 0.3F would be written as 0.30000001192092896. Going
// through the shortest decimal text that reads back as the same float gives 0.3, which reads back
// exactly.
json number(float value)
{
    return std::stod(std::format("{}", value));
}

json toJson(glm::vec2 v)
{
    return json::array({number(v.x), number(v.y)});
}

json toJson(glm::vec4 v)
{
    return json::array({number(v.x), number(v.y), number(v.z), number(v.w)});
}

glm::vec2 vec2From(const json& j)
{
    if (!j.is_array() || j.size() != 2) {
        throw std::runtime_error(std::format("expected [x, y], got {}", j.dump()));
    }
    return {j[0].get<float>(), j[1].get<float>()};
}

glm::vec4 vec4From(const json& j)
{
    if (!j.is_array() || j.size() != 4) {
        throw std::runtime_error(std::format("expected [r, g, b, a], got {}", j.dump()));
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

// Reads an optional member: missing members keep the component's default value, so files stay
// short and new fields can be added without breaking old files.
template <typename T>
void read(const json& object, const char* key, T& value)
{
    if (const auto it = object.find(key); it != object.end()) {
        if constexpr (std::is_same_v<T, glm::vec2>) {
            value = vec2From(*it);
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
            value = vec4From(*it);
        } else {
            value = it->template get<T>();
        }
    }
}

template <typename Enum>
struct EnumName {
    Enum value;
    const char* name;
};

template <typename Enum, std::size_t N>
const char* enumToString(Enum value, const EnumName<Enum> (&names)[N]) // NOLINT(*-avoid-c-arrays)
{
    for (const auto& entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return names[0].name;
}

template <typename Enum, std::size_t N>
Enum enumFromJson(const json& j, const EnumName<Enum> (&names)[N]) // NOLINT(*-avoid-c-arrays)
{
    const auto text = j.get<std::string>();
    for (const auto& entry : names) {
        if (text == entry.name) {
            return entry.value;
        }
    }
    throw std::runtime_error(std::format("unknown value \"{}\"", text));
}

// NOLINTBEGIN(*-avoid-c-arrays)
constexpr EnumName<BodyType> kBodyTypes[] = {{.value = BodyType::Static, .name = "static"},
                                             {.value = BodyType::Dynamic, .name = "dynamic"},
                                             {.value = BodyType::Kinematic, .name = "kinematic"}};
constexpr EnumName<ColliderShape> kShapes[] = {{.value = ColliderShape::Box, .name = "box"},
                                               {.value = ColliderShape::Circle, .name = "circle"}};
constexpr EnumName<TextureFilter> kFilters[] = {
    {.value = TextureFilter::Nearest, .name = "nearest"},
    {.value = TextureFilter::Linear, .name = "linear"}};
constexpr EnumName<TextureWrap> kWraps[] = {{.value = TextureWrap::ClampToEdge, .name = "clamp"},
                                            {.value = TextureWrap::Repeat, .name = "repeat"}};
// NOLINTEND(*-avoid-c-arrays)

} // namespace

// ---------------------------------------------------------------------------------------------
// Contexts

json SceneSerializer::SaveContext::id(Entity entity) const
{
    const auto it = m_ids.find(entity);
    return it != m_ids.end() ? json(it->second) : json(nullptr);
}

Entity SceneSerializer::LoadContext::entity(const json& id) const
{
    if (id.is_null()) {
        return kNullEntity;
    }
    const auto index = id.get<std::size_t>();
    if (index >= m_entities.size()) {
        throw std::runtime_error(std::format("entity id {} is not in the file", index));
    }
    return m_entities[index];
}

// ---------------------------------------------------------------------------------------------
// Built-in components

SceneSerializer::SceneSerializer(AssetCache* assets)
    : m_assets(assets)
{
    registerBuiltIns();
}

void SceneSerializer::addConverter(Converter converter)
{
    for (Converter& existing : m_converters) {
        if (existing.name == converter.name) {
            existing = std::move(converter);
            return;
        }
    }
    m_converters.push_back(std::move(converter));
}

void SceneSerializer::registerBuiltIns()
{
    registerComponent<Name>("Name", [](const Name& name, const SaveContext&) {
        return json(name.value);
    }, [](const json& j, const LoadContext&) { return Name{j.get<std::string>()}; });

    registerComponent<Transform>("Transform", [](const Transform& t, const SaveContext&) {
        return json{{"position", toJson(t.position)},
                    {"rotation", number(t.rotation)},
                    {"scale", toJson(t.scale)}};
    }, [](const json& j, const LoadContext&) {
        Transform t;
        read(j, "position", t.position);
        read(j, "rotation", t.rotation);
        read(j, "scale", t.scale);
        return t;
    });

    registerComponent<SpriteRenderer>("SpriteRenderer",
                                      [](const SpriteRenderer& s, const SaveContext& context) {
        json j{{"size", toJson(s.size)},
               {"tint", toJson(s.tint)},
               {"layer", s.layer},
               {"flipX", s.flipX},
               {"visible", s.visible},
               {"uvMin", toJson(s.sprite.region.uvMin)},
               {"uvMax", toJson(s.sprite.region.uvMax)}};
        if (s.sprite.texture == nullptr) {
            j["texture"] = nullptr;
            return j;
        }
        const auto source = context.assets() != nullptr
                                ? context.assets()->textureSource(*s.sprite.texture)
                                : std::nullopt;
        if (!source) {
            throw std::runtime_error("a sprite's texture was not loaded through the AssetCache "
                                     "given to the SceneSerializer, so it has no path to save");
        }
        j["texture"] = source->path.generic_string();
        j["filter"] = enumToString(source->settings.filter, kFilters);
        j["wrap"] = enumToString(source->settings.wrap, kWraps);
        return j;
    }, [](const json& j, const LoadContext& context) {
        SpriteRenderer s;
        read(j, "size", s.size);
        read(j, "tint", s.tint);
        read(j, "layer", s.layer);
        read(j, "flipX", s.flipX);
        read(j, "visible", s.visible);
        read(j, "uvMin", s.sprite.region.uvMin);
        read(j, "uvMax", s.sprite.region.uvMax);
        if (const auto texture = j.find("texture"); texture != j.end() && !texture->is_null()) {
            if (context.assets() == nullptr) {
                throw std::runtime_error("a sprite has a texture, but the SceneSerializer has no "
                                         "AssetCache to load it");
            }
            TextureSettings settings;
            if (const auto filter = j.find("filter"); filter != j.end()) {
                settings.filter = enumFromJson(*filter, kFilters);
            }
            if (const auto wrap = j.find("wrap"); wrap != j.end()) {
                settings.wrap = enumFromJson(*wrap, kWraps);
            }
            s.sprite.texture = &context.assets()->texture(texture->get<std::string>(), settings);
        }
        return s;
    });

    registerComponent<RigidBody>("RigidBody", [](const RigidBody& b, const SaveContext&) {
        return json{{"type", enumToString(b.type, kBodyTypes)},
                    {"velocity", toJson(b.velocity)},
                    {"mass", number(b.mass)},
                    {"gravityScale", number(b.gravityScale)},
                    {"linearDamping", number(b.linearDamping)}};
    }, [](const json& j, const LoadContext&) {
        RigidBody b;
        if (const auto type = j.find("type"); type != j.end()) {
            b.type = enumFromJson(*type, kBodyTypes);
        }
        read(j, "velocity", b.velocity);
        read(j, "mass", b.mass);
        read(j, "gravityScale", b.gravityScale);
        read(j, "linearDamping", b.linearDamping);
        return b;
    });

    registerComponent<Collider>("Collider", [](const Collider& c, const SaveContext&) {
        return json{{"shape", enumToString(c.shape, kShapes)},
                    {"halfExtents", toJson(c.halfExtents)},
                    {"radius", number(c.radius)},
                    {"offset", toJson(c.offset)},
                    {"restitution", number(c.restitution)},
                    {"friction", number(c.friction)},
                    {"isTrigger", c.isTrigger}};
    }, [](const json& j, const LoadContext&) {
        Collider c;
        if (const auto shape = j.find("shape"); shape != j.end()) {
            c.shape = enumFromJson(*shape, kShapes);
        }
        read(j, "halfExtents", c.halfExtents);
        read(j, "radius", c.radius);
        read(j, "offset", c.offset);
        read(j, "restitution", c.restitution);
        read(j, "friction", c.friction);
        read(j, "isTrigger", c.isTrigger);
        return c;
    });
}

// ---------------------------------------------------------------------------------------------
// Save and load

json SceneSerializer::save(const Scene& scene) const
{
    const Registry& registry = scene.registry();

    // Depth-first order: roots by index, each followed by its subtree with siblings in order.
    // An explicit stack instead of recursion, so deep hierarchies cannot overflow the stack.
    std::vector<Entity> order;
    std::vector<Entity> stack;
    registry.forEach([&](Entity root) {
        if (!scene.parent(root).isNull()) {
            return;
        }
        stack.push_back(root);
        while (!stack.empty()) {
            const Entity entity = stack.back();
            stack.pop_back();
            order.push_back(entity);
            const std::vector<Entity> children = scene.children(entity);
            stack.insert(stack.end(), children.rbegin(), children.rend());
        }
    });

    SaveContext context;
    context.m_assets = m_assets;
    for (std::size_t i = 0; i < order.size(); ++i) {
        context.m_ids.emplace(order[i], i);
    }

    json entities = json::array();
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Entity entity = order[i];
        json components = json::object();
        for (const Converter& converter : m_converters) {
            try {
                if (auto value = converter.save(registry, entity, context)) {
                    components[converter.name] = std::move(*value);
                }
            } catch (const std::exception& error) {
                throw std::runtime_error(std::format("Cannot save {} of entity {}: {}",
                                                     converter.name, i, error.what()));
            }
        }
        entities.push_back(json{{"id", i},
                                {"parent", context.id(scene.parent(entity))},
                                {"components", std::move(components)}});
    }
    return json{{"format", kFormat}, {"version", kVersion}, {"entities", std::move(entities)}};
}

namespace {

// The "entities" array of a scene document, after checking format and version.
const json& entitiesOf(const json& document)
{
    if (!document.is_object() || document.value("format", std::string{}) != kFormat) {
        throw std::runtime_error(R"(Not an Astra2D scene (missing "format": "astra2d-scene"))");
    }
    const int version = document.value("version", 0);
    if (version != SceneSerializer::kVersion) {
        throw std::runtime_error(std::format("Unsupported scene version {} (expected {})", version,
                                             SceneSerializer::kVersion));
    }
    const auto entities = document.find("entities");
    if (entities == document.end() || !entities->is_array()) {
        throw std::runtime_error(R"(Scene has no "entities" array)");
    }
    return *entities;
}

} // namespace

void SceneSerializer::loadComponents(Registry& registry, Entity entity, const json& entry,
                                     std::size_t index, const LoadContext& context) const
{
    if (const auto id = entry.find("id"); id != entry.end() && id->get<std::size_t>() != index) {
        throw std::runtime_error("ids must be 0, 1, 2, ... in file order");
    }
    for (const auto& [name, value] : entry.at("components").items()) {
        const auto converter = std::ranges::find(m_converters, name, &Converter::name);
        if (converter == m_converters.end()) {
            ASTRA_CORE_WARN("Scene file: skipping unknown component '{}'", name);
            continue;
        }
        try {
            converter->load(registry, entity, value, context);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::format("{}: {}", name, error.what()));
        }
    }
}

void SceneSerializer::linkParent(Scene& scene, Entity entity, const json& entry,
                                 const LoadContext& context)
{
    const Entity parent = context.entity(entry.value("parent", json()));
    if (parent.isNull()) {
        return;
    }
    // A file with a cycle must not reach setParent()'s assertion.
    for (Entity ancestor = parent; !ancestor.isNull(); ancestor = scene.parent(ancestor)) {
        if (ancestor == entity) {
            throw std::runtime_error("the parents form a cycle");
        }
    }
    scene.setParent(entity, parent);
}

std::vector<Entity> SceneSerializer::load(Scene& scene, const json& document) const
{
    const json& entities = entitiesOf(document);
    Registry& registry = scene.registry();
    LoadContext context;
    context.m_assets = m_assets;
    context.m_entities.reserve(entities.size());
    // Every entity first, so components and parents can refer to any of them.
    for (std::size_t i = 0; i < entities.size(); ++i) {
        context.m_entities.push_back(registry.create());
    }

    std::size_t current = 0;
    try {
        for (; current < entities.size(); ++current) {
            loadComponents(registry, context.m_entities[current], entities[current], current,
                           context);
        }
        // Parents last, in file order, so siblings keep their order.
        for (current = 0; current < entities.size(); ++current) {
            linkParent(scene, context.m_entities[current], entities[current], context);
        }
    } catch (const std::exception& error) {
        for (const Entity entity : context.m_entities) {
            if (registry.valid(entity)) {
                registry.destroy(entity);
            }
        }
        throw std::runtime_error(
            std::format("Cannot load scene entity {}: {}", current, error.what()));
    }
    return context.m_entities;
}

void SceneSerializer::saveFile(const Scene& scene, const std::filesystem::path& path) const
{
    const json data = save(scene);
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error(std::format("Cannot write scene file '{}'", path.string()));
    }
    file << data.dump(2) << '\n';
    if (!file) {
        throw std::runtime_error(std::format("Error writing scene file '{}'", path.string()));
    }
}

std::vector<Entity> SceneSerializer::loadFile(Scene& scene, const std::filesystem::path& path) const
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(std::format("Cannot open scene file '{}'", path.string()));
    }
    json data;
    try {
        data = json::parse(file);
    } catch (const json::parse_error& error) {
        throw std::runtime_error(
            std::format("Scene file '{}' is not valid JSON: {}", path.string(), error.what()));
    }
    try {
        return load(scene, data);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::format("{} ('{}')", error.what(), path.string()));
    }
}

} // namespace astra2d
