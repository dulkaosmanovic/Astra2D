#pragma once

#include "astra2d/physics/tilemap.hpp"

#include <glm/vec2.hpp>

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// @addtogroup scene
/// @{

namespace astra2d {

/// Custom properties of a Tiled map, layer, tile or object (Tiled's "Custom Properties" panel).
/// Values are kept as text: strings as they are, numbers as written, booleans as "true"/"false".
struct TiledProperties {
    std::map<std::string, std::string, std::less<>> values; ///< Name to value.

    /// True if the property @p name exists.
    [[nodiscard]] bool has(std::string_view name) const;
    /// The value of @p name, or @p fallback if it does not exist.
    [[nodiscard]] std::string string(std::string_view name, std::string_view fallback = {}) const;
    /// The value of @p name as a number, or @p fallback if it does not exist or is not a number.
    [[nodiscard]] float number(std::string_view name, float fallback = 0.0F) const;
    /// The value of @p name as an integer, or @p fallback.
    [[nodiscard]] int integer(std::string_view name, int fallback = 0) const;
    /// The value of @p name as a boolean ("true" is true), or @p fallback.
    [[nodiscard]] bool boolean(std::string_view name, bool fallback = false) const;
};

/// A tileset of a Tiled map: an image cut into equally sized tiles.
struct TiledTileset {
    int firstGid = 1;                          ///< Global id of the first tile in the map.
    std::string name;                          ///< Name in Tiled.
    glm::ivec2 tileSize{0};                    ///< Tile size in pixels.
    int tileCount = 0;                         ///< Number of tiles.
    int columns = 0;                           ///< Tiles per row of the image.
    int margin = 0;                            ///< Border around the tiles in the image, in pixels.
    int spacing = 0;                           ///< Gap between tiles in the image, in pixels.
    std::filesystem::path image;               ///< The image, relative to the map file's directory.
    glm::ivec2 imageSize{0};                   ///< Image size in pixels.
    std::map<int, TiledProperties> properties; ///< Custom properties by local tile id.
    std::map<int, std::string> classes;        ///< Class ("type") by local tile id.
};

/// A tile layer: one global tile id per cell (0 = empty).
struct TiledTileLayer {
    std::string name;     ///< Layer name.
    int width = 0;        ///< Columns.
    int height = 0;       ///< Rows.
    bool visible = true;  ///< Visibility toggle in Tiled.
    float opacity = 1.0F; ///< Opacity in Tiled.
    /// Global tile ids with Tiled's flip flags, row by row from the **top**, as Tiled stores them.
    std::vector<std::uint32_t> gids;
    TiledProperties properties; ///< Custom properties.

    /// The gid at column @p x and row @p y (row 0 at the top); 0 outside the layer.
    [[nodiscard]] std::uint32_t at(int x, int y) const;
};

/// An object of an object layer, in Tiled's pixel coordinates (y down from the map's top).
struct TiledObject {
    int id = 0;                 ///< Unique id in the map.
    std::string name;           ///< Name.
    std::string type;           ///< Class (called "type" before Tiled 1.9).
    glm::vec2 position{0.0F};   ///< Top-left corner (bottom-left for a tile object, see gid).
    glm::vec2 size{0.0F};       ///< Width and height; zero for a point.
    float rotation = 0.0F;      ///< Clockwise rotation in degrees.
    bool point = false;         ///< A point object.
    std::uint32_t gid = 0;      ///< Tile of a tile object (0 for shapes).
    TiledProperties properties; ///< Custom properties.
};

/// An object layer.
struct TiledObjectLayer {
    std::string name;                 ///< Layer name.
    bool visible = true;              ///< Visibility toggle in Tiled.
    std::vector<TiledObject> objects; ///< Objects in drawing order.
    TiledProperties properties;       ///< Custom properties.
};

/// A tile of a TiledMap, found from a global id with TiledMap::tile().
struct TiledTile {
    const TiledTileset* tileset = nullptr; ///< Its tileset.
    int id = 0;                            ///< Local id in the tileset (its frame index).
    bool flipX = false;                    ///< Mirrored horizontally.
    bool flipY = false;                    ///< Mirrored vertically.
    bool flipDiagonal = false;             ///< Mirrored across the diagonal (rotated).
    /// Custom properties of the tile, or nullptr if it has none.
    const TiledProperties* properties = nullptr;
    /// Class of the tile, or empty.
    std::string_view type;
};

/// A map made with the Tiled editor (https://www.mapeditor.org), saved as JSON (.tmj / .json).
///
/// Supported: orthogonal, finite maps; tile layers stored as CSV or uncompressed Base64; object
/// layers; group layers (flattened); embedded and external JSON tilesets (.tsj); custom properties.
/// Image layers are skipped. Infinite maps, compressed layers and XML tilesets (.tsx) are reported
/// as errors, with what to change in Tiled.
///
/// Tiled measures in pixels with y pointing down from the map's top-left corner; the engine uses
/// world units with y up. toWorld() converts, given how large a tile is in the world, and
/// makeTileGrid() turns a layer into collision tiles.
///
/// @code
/// const auto map = astra2d::loadTiledMap(assets / "levels/level2.tmj");
/// const auto* ground = map.tileLayer("ground");
/// registry.emplace<astra2d::TilemapCollider>(level, {.grid = astra2d::makeTileGrid(map,
/// *ground, 16.0F)}); for (const auto& object : map.objectLayer("objects")->objects) {
///     if (object.type == "coin") spawnCoin(map.objectCenter(object, 16.0F));
/// }
/// @endcode
struct TiledMap {
    int width = 0;                              ///< Columns.
    int height = 0;                             ///< Rows.
    glm::ivec2 tileSize{0};                     ///< Tile size in pixels.
    TiledProperties properties;                 ///< Custom map properties.
    std::vector<TiledTileset> tilesets;         ///< By ascending firstGid.
    std::vector<TiledTileLayer> tileLayers;     ///< In drawing order (bottom first).
    std::vector<TiledObjectLayer> objectLayers; ///< In drawing order.

    /// The tile layer called @p name, or nullptr.
    [[nodiscard]] const TiledTileLayer* tileLayer(std::string_view name) const;
    /// The object layer called @p name, or nullptr.
    [[nodiscard]] const TiledObjectLayer* objectLayer(std::string_view name) const;
    /// The tile with global id @p gid (flip flags allowed), or nothing for 0 or an unknown id.
    [[nodiscard]] std::optional<TiledTile> tile(std::uint32_t gid) const;

    /// A point in Tiled pixels as a world position, with tiles @p worldTileSize units large and
    /// the map's bottom-left corner at the origin.
    [[nodiscard]] glm::vec2 toWorld(glm::vec2 pixels, float worldTileSize) const;
    /// The world position of the centre of @p object (of a point object: the point).
    [[nodiscard]] glm::vec2 objectCenter(const TiledObject& object, float worldTileSize) const;
    /// The world size of @p object.
    [[nodiscard]] glm::vec2 objectSize(const TiledObject& object, float worldTileSize) const;
    /// The world position of the centre of the cell at column @p x, row @p y (row 0 at the top).
    [[nodiscard]] glm::vec2 cellCenter(int x, int y, float worldTileSize) const;
};

/// Bits of a global tile id that are flip flags (Tiled sets the highest four).
inline constexpr std::uint32_t kTiledFlipMask = 0xF0000000U;

/// Reads a Tiled map from @p file; external tilesets are resolved relative to its directory.
/// @throws std::runtime_error if the file cannot be read or is not a supported map.
[[nodiscard]] TiledMap loadTiledMap(const std::filesystem::path& file);
/// Reads a Tiled map from parsed JSON; external tilesets are resolved relative to @p directory.
/// @throws std::runtime_error if it is not a supported map.
[[nodiscard]] TiledMap parseTiledMap(const nlohmann::json& json,
                                     const std::filesystem::path& directory = {});

/// Decides what a tile is made of, for makeTileGrid().
using TiledCollisionRule = std::function<TileCollision(const TiledTile&)>;

/// The default TiledCollisionRule: the tile's custom property `collision` ("solid", or "oneway" /
/// "one-way" for a platform), otherwise its class ("solid" or "oneway"); anything else is Empty.
[[nodiscard]] TileCollision tiledCollision(const TiledTile& tile);

/// The collision tiles of @p layer, for a TilemapCollider at the map's bottom-left corner (the
/// origin of toWorld()), with tiles @p worldTileSize units large. Rows are flipped, as TileGrid
/// counts them from the bottom.
[[nodiscard]] TileGrid makeTileGrid(const TiledMap& map, const TiledTileLayer& layer,
                                    float worldTileSize,
                                    const TiledCollisionRule& rule = tiledCollision);

} // namespace astra2d

/// @}
