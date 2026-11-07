#include "astra2d/scene/serializer.hpp"

#include "astra2d/assets/asset_cache.hpp"
#include "astra2d/core/log.hpp"
#include "astra2d/renderer/texture.hpp"
#include "astra2d/scene/components.hpp"
#include "astra2d/scene/scene.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace astra2d {

namespace {

using nlohmann::json;

constexpr std::string_view kFormat = "astra2d-scene";

// JSON numbers are doubles; a float such as 0.3F would be written as 0.30000001192092896. Going
// through the shortest decimal text that reads back as the same float gives 0.3, which reads back
// exactly. to_chars/from_chars ignore the C locale, so a game that sets a comma-decimal locale
// (setlocale(LC_ALL, "")) still writes 0.3 and not 0.
json number(float value)
{
    std::array<char, 32> text{};
    const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
    double result = 0.0;
    std::from_chars(text.data(), written.ptr, result);
    return result;
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
constexpr EnumName<ColliderShape> kShapes[] = {
    {.value = ColliderShape::Box, .name = "box"},
    {.value = ColliderShape::Circle, .name = "circle"},
    {.value = ColliderShape::Polygon, .name = "polygon"}};
constexpr EnumName<TextureFilter> kFilters[] = {
    {.value = TextureFilter::Nearest, .name = "nearest"},
    {.value = TextureFilter::Linear, .name = "linear"}};
constexpr EnumName<TextureWrap> kWraps[] = {{.value = TextureWrap::ClampToEdge, .name = "clamp"},
                                            {.value = TextureWrap::Repeat, .name = "repeat"}};
// NOLINTEND(*-avoid-c-arrays)

// ---------------------------------------------------------------------------------------------
// Physics components

json saveRigidBody(const RigidBody& b, const SceneSerializer::SaveContext& /*context*/)
{
    return json{{"type", enumToString(b.type, kBodyTypes)},
                {"velocity", toJson(b.velocity)},
                {"mass", number(b.mass)},
                {"gravityScale", number(b.gravityScale)},
                {"linearDamping", number(b.linearDamping)},
                {"angularVelocity", number(b.angularVelocity)},
                {"angularDamping", number(b.angularDamping)},
                {"fixedRotation", b.fixedRotation},
                {"bullet", b.bullet},
                {"allowSleep", b.allowSleep}};
}

RigidBody loadRigidBody(const json& j, const SceneSerializer::LoadContext& /*context*/)
{
    RigidBody b;
    if (const auto type = j.find("type"); type != j.end()) {
        b.type = enumFromJson(*type, kBodyTypes);
    }
    read(j, "velocity", b.velocity);
    read(j, "mass", b.mass);
    read(j, "gravityScale", b.gravityScale);
    read(j, "linearDamping", b.linearDamping);
    read(j, "angularVelocity", b.angularVelocity);
    read(j, "angularDamping", b.angularDamping);
    read(j, "fixedRotation", b.fixedRotation);
    read(j, "bullet", b.bullet);
    read(j, "allowSleep", b.allowSleep);
    if (!(b.mass > 0.0F)) {
        throw std::runtime_error("mass must be positive");
    }
    return b;
}

json saveCollider(const Collider& c, const SceneSerializer::SaveContext& /*context*/)
{
    json points = json::array();
    for (const glm::vec2 p : c.points) {
        points.push_back(toJson(p));
    }
    return json{{"shape", enumToString(c.shape, kShapes)},
                {"halfExtents", toJson(c.halfExtents)},
                {"radius", number(c.radius)},
                {"offset", toJson(c.offset)},
                {"restitution", number(c.restitution)},
                {"friction", number(c.friction)},
                {"isTrigger", c.isTrigger},
                {"points", points},
                {"oneWay", c.oneWay},
                {"category", c.category},
                {"mask", c.mask}};
}

Collider loadCollider(const json& j, const SceneSerializer::LoadContext& /*context*/)
{
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
    if (const auto points = j.find("points"); points != j.end()) {
        for (const json& p : *points) {
            c.points.push_back(vec2From(p));
        }
    }
    read(j, "oneWay", c.oneWay);
    read(j, "category", c.category);
    read(j, "mask", c.mask);
    // Only the size the shape uses must be valid. Written so that NaN fails too.
    if (c.shape == ColliderShape::Box && (!(c.halfExtents.x > 0.0F) || !(c.halfExtents.y > 0.0F))) {
        throw std::runtime_error("collider halfExtents must be positive");
    }
    if (c.shape == ColliderShape::Circle && !(c.radius > 0.0F)) {
        throw std::runtime_error("collider radius must be positive");
    }
    return c;
}

// Tiles as text rows, top row first, like the level looks: '#' solid, '-' one-way, '.' empty.
char tileChar(TileCollision tile)
{
    switch (tile) {
    case TileCollision::Solid:
        return '#';
    case TileCollision::OneWay:
        return '-';
    case TileCollision::Empty:
        break;
    }
    return '.';
}

TileCollision tileFromChar(char c)
{
    switch (c) {
    case '#':
        return TileCollision::Solid;
    case '-':
        return TileCollision::OneWay;
    case '.':
        return TileCollision::Empty;
    default:
        throw std::runtime_error(std::format("unknown tile '{}'", c));
    }
}

json saveTilemap(const TilemapCollider& t, const SceneSerializer::SaveContext& /*context*/)
{
    json rows = json::array();
    for (int y = t.grid.height - 1; y >= 0; --y) {
        std::string row;
        for (int x = 0; x < t.grid.width; ++x) {
            row += tileChar(t.grid.at(x, y));
        }
        rows.push_back(row);
    }
    return json{{"tileSize", number(t.grid.tileSize)},
                {"origin", toJson(t.grid.origin)},
                {"rows", rows},
                {"friction", number(t.friction)},
                {"restitution", number(t.restitution)},
                {"category", t.category},
                {"mask", t.mask}};
}

TilemapCollider loadTilemap(const json& j, const SceneSerializer::LoadContext& /*context*/)
{
    TilemapCollider t;
    read(j, "tileSize", t.grid.tileSize);
    read(j, "origin", t.grid.origin);
    read(j, "friction", t.friction);
    read(j, "restitution", t.restitution);
    read(j, "category", t.category);
    read(j, "mask", t.mask);
    if (!(t.grid.tileSize > 0.0F)) {
        throw std::runtime_error("tileSize must be positive");
    }
    const auto rows = j.at("rows").get<std::vector<std::string>>();
    const std::size_t width = rows.empty() ? 0 : rows.front().size();
    t.grid.height = static_cast<int>(rows.size());
    t.grid.width = static_cast<int>(width);
    t.grid.cells.assign(rows.size() * width, TileCollision::Empty);
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (rows[r].size() != width) {
            throw std::runtime_error("tile rows must all have the same length");
        }
        const int y = t.grid.height - 1 - static_cast<int>(r); // the first row is the top one
        for (std::size_t x = 0; x < width; ++x) {
            t.grid.set(static_cast<int>(x), y, tileFromChar(rows[r][x]));
        }
    }
    return t;
}

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

    registerComponent<RigidBody>("RigidBody", saveRigidBody, loadRigidBody);
    registerComponent<Collider>("Collider", saveCollider, loadCollider);
    registerComponent<TilemapCollider>("TilemapCollider", saveTilemap, loadTilemap);
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
    // Checked by type first: json::value() would throw json::type_error for a wrongly typed
    // field, and load() promises std::runtime_error.
    const auto format = document.is_object() ? document.find("format") : document.end();
    if (format == document.end() || !format->is_string() || *format != kFormat) {
        throw std::runtime_error(R"(Not an Astra2D scene (missing "format": "astra2d-scene"))");
    }
    const auto version = document.find("version");
    if (version == document.end() || !version->is_number_integer() ||
        version->get<std::int64_t>() != SceneSerializer::kVersion) {
        throw std::runtime_error(std::format("Unsupported scene version {} (expected {})",
                                             version == document.end() ? "(none)" : version->dump(),
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
    // Written next to the target and renamed over it, so a failed write (full disk, crash) leaves
    // the old file intact instead of a truncated one.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary);
        if (!file) {
            throw std::runtime_error(std::format("Cannot write scene file '{}'", path.string()));
        }
        file << data.dump(2) << '\n';
        file.close();
        if (!file) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error(std::format("Error writing scene file '{}'", path.string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error(
            std::format("Cannot replace scene file '{}': {}", path.string(), error.message()));
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
