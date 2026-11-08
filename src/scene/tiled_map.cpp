#include "astra2d/scene/tiled_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace astra2d {

using nlohmann::json;

namespace {

[[noreturn]] void fail(std::string_view message)
{
    throw std::runtime_error(std::format("Tiled map: {}", message));
}

// A required field, with a message naming it.
const json& field(const json& j, std::string_view name, std::string_view where)
{
    const auto it = j.find(name);
    if (it == j.end()) {
        fail(std::format("{} has no '{}'", where, name));
    }
    return *it;
}

template <typename T>
T value(const json& j, std::string_view name, T fallback)
{
    if (const auto it = j.find(name); it != j.end() && !it->is_null()) {
        return it->get<T>();
    }
    return fallback;
}

TiledProperties readProperties(const json& j)
{
    TiledProperties result;
    const auto it = j.find("properties");
    if (it == j.end()) {
        return result;
    }
    for (const json& p : *it) {
        const auto& v = field(p, "value", "a property");
        result.values[field(p, "name", "a property").get<std::string>()] =
            v.is_string() ? v.get<std::string>() : v.dump();
    }
    return result;
}

std::string readClass(const json& j)
{
    // Tiled 1.9 called it "class", the versions before and after "type".
    const auto type = value<std::string>(j, "type", {});
    return type.empty() ? value<std::string>(j, "class", {}) : type;
}

std::vector<std::uint8_t> decodeBase64(std::string_view text)
{
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> bytes;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') {
            continue;
        }
        const auto index = kAlphabet.find(c);
        if (index == std::string_view::npos) {
            fail("invalid Base64 tile data");
        }
        buffer = (buffer << 6U) | static_cast<std::uint32_t>(index);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(
                static_cast<std::uint8_t>((buffer >> static_cast<unsigned>(bits)) & 0xFFU));
        }
    }
    return bytes;
}

TiledTileLayer readTileLayer(const json& j)
{
    TiledTileLayer layer;
    layer.name = value<std::string>(j, "name", {});
    const std::string where = std::format("tile layer '{}'", layer.name);
    layer.width = field(j, "width", where).get<int>();
    layer.height = field(j, "height", where).get<int>();
    layer.visible = value(j, "visible", true);
    layer.opacity = value(j, "opacity", 1.0F);
    layer.properties = readProperties(j);
    if (!value<std::string>(j, "compression", {}).empty()) {
        fail(std::format("{} is compressed; set the layer format to CSV in the map properties",
                         where));
    }
    const json& data = field(j, "data", where);
    if (value<std::string>(j, "encoding", "csv") == "base64") {
        const auto bytes = decodeBase64(data.get<std::string>());
        for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) { // little-endian 32-bit ids
            layer.gids.push_back(static_cast<std::uint32_t>(bytes[i]) |
                                 (static_cast<std::uint32_t>(bytes[i + 1]) << 8U) |
                                 (static_cast<std::uint32_t>(bytes[i + 2]) << 16U) |
                                 (static_cast<std::uint32_t>(bytes[i + 3]) << 24U));
        }
    } else {
        layer.gids = data.get<std::vector<std::uint32_t>>();
    }
    if (layer.width < 0 || layer.height < 0 ||
        layer.gids.size() !=
            static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height)) {
        fail(std::format("{} has {} tiles instead of {} x {}", where, layer.gids.size(),
                         layer.width, layer.height));
    }
    return layer;
}

TiledObjectLayer readObjectLayer(const json& j)
{
    TiledObjectLayer layer;
    layer.name = value<std::string>(j, "name", {});
    layer.visible = value(j, "visible", true);
    layer.properties = readProperties(j);
    for (const json& o : value(j, "objects", json::array())) {
        TiledObject object;
        object.id = value(o, "id", 0);
        object.name = value<std::string>(o, "name", {});
        object.type = readClass(o);
        object.position = {value(o, "x", 0.0F), value(o, "y", 0.0F)};
        object.size = {value(o, "width", 0.0F), value(o, "height", 0.0F)};
        object.rotation = value(o, "rotation", 0.0F);
        object.point = value(o, "point", false);
        object.gid = value<std::uint32_t>(o, "gid", 0);
        object.properties = readProperties(o);
        layer.objects.push_back(std::move(object));
    }
    return layer;
}

void readTileset(const json& j, const std::filesystem::path& directory, TiledMap& map)
{
    TiledTileset tileset;
    tileset.firstGid = field(j, "firstgid", "a tileset").get<int>();
    const json* source = &j;
    json external;
    std::filesystem::path imageDirectory; // the image is relative to the tileset's own file
    if (const auto it = j.find("source"); it != j.end()) {
        const std::filesystem::path file = directory / it->get<std::string>();
        if (file.extension() == ".tsx") {
            fail(std::format("tileset '{}' is XML; export it as JSON (.tsj) in Tiled",
                             file.string()));
        }
        std::ifstream in(file);
        if (!in) {
            fail(std::format("cannot open tileset '{}'", file.string()));
        }
        try {
            external = json::parse(in);
        } catch (const json::exception& error) {
            fail(std::format("tileset '{}': {}", file.string(), error.what()));
        }
        source = &external;
        imageDirectory = std::filesystem::path(it->get<std::string>()).parent_path();
    }
    const json& t = *source;
    tileset.name = value<std::string>(t, "name", {});
    const std::string where = std::format("tileset '{}'", tileset.name);
    tileset.tileSize = {field(t, "tilewidth", where).get<int>(),
                        field(t, "tileheight", where).get<int>()};
    tileset.tileCount = field(t, "tilecount", where).get<int>();
    tileset.columns = value(t, "columns", 0);
    tileset.margin = value(t, "margin", 0);
    tileset.spacing = value(t, "spacing", 0);
    if (const auto image = t.find("image"); image != t.end()) {
        tileset.image = (imageDirectory / image->get<std::string>()).lexically_normal();
        tileset.imageSize = {value(t, "imagewidth", 0), value(t, "imageheight", 0)};
    }
    for (const json& tile : value(t, "tiles", json::array())) {
        const int id = field(tile, "id", where).get<int>();
        if (TiledProperties p = readProperties(tile); !p.values.empty()) {
            tileset.properties[id] = std::move(p);
        }
        if (std::string type = readClass(tile); !type.empty()) {
            tileset.classes[id] = std::move(type);
        }
    }
    map.tilesets.push_back(std::move(tileset));
}

void readLayers(const json& layers, TiledMap& map)
{
    // Group layers are flattened in drawing order. An explicit stack instead of recursion, so a
    // deeply nested file cannot overflow the call stack.
    std::vector<std::pair<const json*, std::size_t>> stack{{&layers, 0}};
    while (!stack.empty()) {
        auto& [list, next] = stack.back();
        if (next >= list->size()) {
            stack.pop_back();
            continue;
        }
        const json& layer = (*list)[next++];
        const auto type = field(layer, "type", "a layer").get<std::string>();
        if (type == "tilelayer") {
            map.tileLayers.push_back(readTileLayer(layer));
        } else if (type == "objectgroup") {
            map.objectLayers.push_back(readObjectLayer(layer));
        } else if (const auto children = layer.find("layers");
                   type == "group" && children != layer.end()) {
            stack.emplace_back(&*children, 0); // `list` and `next` are not used after this
        }
        // Image layers carry no game data.
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// TiledProperties

bool TiledProperties::has(std::string_view name) const
{
    return values.contains(name);
}

std::string TiledProperties::string(std::string_view name, std::string_view fallback) const
{
    const auto it = values.find(name);
    return it != values.end() ? it->second : std::string(fallback);
}

float TiledProperties::number(std::string_view name, float fallback) const
{
    const auto it = values.find(name);
    if (it == values.end()) {
        return fallback;
    }
    float result = 0.0F;
    const std::string& text = it->second;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    return error == std::errc{} && end == text.data() + text.size() ? result : fallback;
}

int TiledProperties::integer(std::string_view name, int fallback) const
{
    const auto it = values.find(name);
    if (it == values.end()) {
        return fallback;
    }
    // Read as an int first: a float holds integers exactly only up to 2^24.
    int result = 0;
    const std::string& text = it->second;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error == std::errc{} && end == text.data() + text.size()) {
        return result;
    }
    return static_cast<int>(std::lround(number(name, static_cast<float>(fallback)))); // "2.5"
}

bool TiledProperties::boolean(std::string_view name, bool fallback) const
{
    const auto it = values.find(name);
    return it != values.end() ? it->second == "true" : fallback;
}

// ---------------------------------------------------------------------------------------------
// TiledTileLayer, TiledMap

std::uint32_t TiledTileLayer::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 0;
    }
    return gids[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
                static_cast<std::size_t>(x)];
}

const TiledTileLayer* TiledMap::tileLayer(std::string_view name) const
{
    const auto it = std::ranges::find(tileLayers, name, &TiledTileLayer::name);
    return it != tileLayers.end() ? &*it : nullptr;
}

const TiledObjectLayer* TiledMap::objectLayer(std::string_view name) const
{
    const auto it = std::ranges::find(objectLayers, name, &TiledObjectLayer::name);
    return it != objectLayers.end() ? &*it : nullptr;
}

std::optional<TiledTile> TiledMap::tile(std::uint32_t gid) const
{
    const std::uint32_t id = gid & ~kTiledFlipMask;
    if (id == 0) {
        return std::nullopt;
    }
    // The tileset with the largest firstGid not above the id.
    const TiledTileset* tileset = nullptr;
    for (const TiledTileset& t : tilesets) {
        if (std::cmp_less_equal(t.firstGid, id)) {
            tileset = &t;
        }
    }
    if (tileset == nullptr) {
        return std::nullopt;
    }
    const int local = static_cast<int>(id) - tileset->firstGid;
    if (local >= tileset->tileCount) {
        return std::nullopt;
    }
    TiledTile result{.tileset = tileset,
                     .id = local,
                     .flipX = (gid & 0x80000000U) != 0,
                     .flipY = (gid & 0x40000000U) != 0,
                     .flipDiagonal = (gid & 0x20000000U) != 0,
                     .properties = nullptr,
                     .type = {}};
    if (const auto p = tileset->properties.find(local); p != tileset->properties.end()) {
        result.properties = &p->second;
    }
    if (const auto c = tileset->classes.find(local); c != tileset->classes.end()) {
        result.type = c->second;
    }
    return result;
}

glm::vec2 TiledMap::toWorld(glm::vec2 pixels, float worldTileSize) const
{
    const glm::vec2 tiles{pixels.x / static_cast<float>(tileSize.x),
                          static_cast<float>(height) - (pixels.y / static_cast<float>(tileSize.y))};
    return tiles * worldTileSize;
}

glm::vec2 TiledMap::objectSize(const TiledObject& object, float worldTileSize) const
{
    return {object.size.x / static_cast<float>(tileSize.x) * worldTileSize,
            object.size.y / static_cast<float>(tileSize.y) * worldTileSize};
}

glm::vec2 TiledMap::objectCenter(const TiledObject& object, float worldTileSize) const
{
    // A tile object's position is its bottom-left corner, a shape's its top-left one.
    const float down = object.gid != 0 ? -0.5F : 0.5F;
    const glm::vec2 center =
        object.position + glm::vec2(0.5F * object.size.x, down * object.size.y);
    return toWorld(center, worldTileSize);
}

glm::vec2 TiledMap::cellCenter(int x, int y, float worldTileSize) const
{
    return glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(height - y) - 0.5F) *
           worldTileSize;
}

// ---------------------------------------------------------------------------------------------
// Loading

TiledMap parseTiledMap(const json& j, const std::filesystem::path& directory)
{
    try {
        if (!j.is_object() || value<std::string>(j, "type", "map") != "map") {
            fail("not a map");
        }
        if (value<std::string>(j, "orientation", "orthogonal") != "orthogonal") {
            fail("only orthogonal maps are supported");
        }
        if (value(j, "infinite", false)) {
            fail("infinite maps are not supported; turn off \"Infinite\" in the map properties");
        }
        TiledMap map;
        map.width = field(j, "width", "the map").get<int>();
        map.height = field(j, "height", "the map").get<int>();
        map.tileSize = {field(j, "tilewidth", "the map").get<int>(),
                        field(j, "tileheight", "the map").get<int>()};
        if (map.width <= 0 || map.height <= 0 || map.tileSize.x <= 0 || map.tileSize.y <= 0) {
            fail("the map and tile sizes must be positive");
        }
        map.properties = readProperties(j);
        for (const json& tileset : value(j, "tilesets", json::array())) {
            readTileset(tileset, directory, map);
        }
        std::ranges::sort(map.tilesets, {}, &TiledTileset::firstGid);
        readLayers(field(j, "layers", "the map"), map);
        return map;
    } catch (const json::exception& error) {
        fail(error.what()); // a field of the wrong type
    }
}

TiledMap loadTiledMap(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) {
        throw std::runtime_error(std::format("Cannot open Tiled map '{}'", file.string()));
    }
    json j;
    try {
        j = json::parse(in);
    } catch (const json::exception& error) {
        throw std::runtime_error(std::format("Tiled map '{}': {}", file.string(), error.what()));
    }
    try {
        return parseTiledMap(j, file.parent_path());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::format("{} ({})", error.what(), file.string()));
    }
}

TileCollision tiledCollision(const TiledTile& tile)
{
    std::string kind = tile.properties != nullptr ? tile.properties->string("collision") : "";
    if (kind.empty()) {
        kind = tile.type;
    }
    if (kind == "solid") {
        return TileCollision::Solid;
    }
    if (kind == "oneway" || kind == "one-way") {
        return TileCollision::OneWay;
    }
    return TileCollision::Empty;
}

TileGrid makeTileGrid(const TiledMap& map, const TiledTileLayer& layer, float worldTileSize,
                      const TiledCollisionRule& rule)
{
    TileGrid grid;
    grid.width = layer.width;
    grid.height = layer.height;
    grid.tileSize = worldTileSize;
    // The layer may be smaller than the map; it starts at the map's top-left corner.
    grid.origin = {0.0F, static_cast<float>(map.height - layer.height) * worldTileSize};
    for (int y = 0; y < layer.height; ++y) {
        for (int x = 0; x < layer.width; ++x) {
            if (const auto tile = map.tile(layer.at(x, y))) {
                if (const TileCollision kind = rule(*tile); kind != TileCollision::Empty) {
                    grid.set(x, layer.height - 1 - y, kind);
                }
            }
        }
    }
    return grid;
}

} // namespace astra2d
