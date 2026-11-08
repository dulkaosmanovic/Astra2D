// Tiled JSON maps: layers, tilesets (embedded and external), objects, properties, flip flags,
// coordinate conversion, collision grids and the errors for unsupported maps.

#include <astra2d/physics/tilemap.hpp>
#include <astra2d/scene/tiled_map.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using astra2d::TileCollision;
using astra2d::TiledMap;
using nlohmann::json;

namespace {

// 4x3 map, 16 px tiles, as Tiled saves it (only the fields that matter, plus a few it adds).
json sampleMap()
{
    return json::parse(R"({
      "type": "map", "version": "1.10", "tiledversion": "1.10.2", "orientation": "orthogonal",
      "renderorder": "right-down", "infinite": false,
      "width": 4, "height": 3, "tilewidth": 16, "tileheight": 16,
      "properties": [{"name": "title", "type": "string", "value": "Caves"},
                     {"name": "gravity", "type": "float", "value": 30.5},
                     {"name": "dark", "type": "bool", "value": true}],
      "tilesets": [{
        "firstgid": 1, "name": "tiles", "tilewidth": 16, "tileheight": 16, "tilecount": 8,
        "columns": 4, "margin": 1, "spacing": 2, "image": "../textures/tiles.png",
        "imagewidth": 72, "imageheight": 36,
        "tiles": [{"id": 0, "properties": [{"name": "collision", "type": "string", "value": "solid"}]},
                  {"id": 1, "type": "oneway"},
                  {"id": 2, "class": "solid"},
                  {"id": 5, "properties": [{"name": "damage", "type": "int", "value": 2}]}]
      }],
      "layers": [
        {"type": "tilelayer", "name": "ground", "width": 4, "height": 3, "visible": true,
         "opacity": 1, "x": 0, "y": 0,
         "data": [0, 0, 2, 2,
                  0, 0, 0, 0,
                  1, 1, 3, 2147483649]},
        {"type": "group", "name": "things", "layers": [
          {"type": "objectgroup", "name": "objects", "objects": [
            {"id": 1, "name": "start", "type": "player", "x": 8, "y": 24, "width": 0,
             "height": 0, "point": true},
            {"id": 2, "name": "", "class": "coin", "x": 32, "y": 16, "width": 16, "height": 16,
             "properties": [{"name": "value", "type": "int", "value": 5}]},
            {"id": 3, "name": "", "type": "spike", "gid": 6, "x": 48, "y": 48, "width": 16,
             "height": 16}
          ]}
        ]},
        {"type": "imagelayer", "name": "sky", "image": "sky.png"}
      ]
    })");
}

class TiledFileTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir = std::filesystem::temp_directory_path() /
                ("astra2d_tiled_" +
                 std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::create_directories(m_dir / "levels");
    }
    void TearDown() override { std::filesystem::remove_all(m_dir); }
    void write(const std::filesystem::path& file, const json& j) const
    {
        std::ofstream(m_dir / file) << j.dump(2);
    }
    [[nodiscard]] const std::filesystem::path& dir() const { return m_dir; }

private:
    std::filesystem::path m_dir;
};

} // namespace

TEST(TiledMapTests, ReadsTheMapAndItsProperties)
{
    const TiledMap map = astra2d::parseTiledMap(sampleMap());
    EXPECT_EQ(map.width, 4);
    EXPECT_EQ(map.height, 3);
    EXPECT_EQ(map.tileSize, glm::ivec2(16, 16));
    EXPECT_EQ(map.properties.string("title"), "Caves");
    EXPECT_FLOAT_EQ(map.properties.number("gravity"), 30.5F);
    EXPECT_TRUE(map.properties.boolean("dark"));
    EXPECT_FALSE(map.properties.has("missing"));
    EXPECT_EQ(map.properties.integer("missing", 7), 7);
    astra2d::TiledProperties big;
    big.values["seed"] = "123456789"; // more digits than a float holds
    big.values["half"] = "2.5";
    EXPECT_EQ(big.integer("seed"), 123456789);
    EXPECT_EQ(big.integer("half"), 3);
    EXPECT_FLOAT_EQ(map.properties.number("title", -1.0F), -1.0F); // not a number
}

TEST(TiledMapTests, ReadsTilesetsAndResolvesGids)
{
    const TiledMap map = astra2d::parseTiledMap(sampleMap());
    ASSERT_EQ(map.tilesets.size(), 1U);
    const astra2d::TiledTileset& tiles = map.tilesets[0];
    EXPECT_EQ(tiles.columns, 4);
    EXPECT_EQ(tiles.margin, 1);
    EXPECT_EQ(tiles.spacing, 2);
    EXPECT_EQ(tiles.image, std::filesystem::path("../textures/tiles.png"));

    EXPECT_FALSE(map.tile(0).has_value());
    EXPECT_FALSE(map.tile(9).has_value()); // beyond the tileset
    ASSERT_TRUE(map.tile(1).has_value());
    const astra2d::TiledTile solid = map.tile(1).value_or(astra2d::TiledTile{});
    EXPECT_EQ(solid.id, 0);
    ASSERT_NE(solid.properties, nullptr);
    EXPECT_EQ(solid.properties->string("collision"), "solid");
    EXPECT_EQ(map.tile(2).value_or(astra2d::TiledTile{}).type, "oneway");
    EXPECT_EQ(map.tile(3).value_or(astra2d::TiledTile{}).type, "solid"); // Tiled 1.9 "class"

    // gid 1 with the horizontal flip bit.
    ASSERT_TRUE(map.tile(2147483649U).has_value());
    const astra2d::TiledTile flipped = map.tile(2147483649U).value_or(astra2d::TiledTile{});
    EXPECT_EQ(flipped.id, 0);
    EXPECT_TRUE(flipped.flipX);
    EXPECT_FALSE(flipped.flipY);
}

TEST(TiledMapTests, FlattensGroupsAndSkipsImageLayers)
{
    const TiledMap map = astra2d::parseTiledMap(sampleMap());
    ASSERT_EQ(map.tileLayers.size(), 1U);
    ASSERT_EQ(map.objectLayers.size(), 1U);
    ASSERT_NE(map.tileLayer("ground"), nullptr);
    EXPECT_EQ(map.tileLayer("sky"), nullptr);
    EXPECT_EQ(map.tileLayer("ground")->at(2, 0), 2U);
    EXPECT_EQ(map.tileLayer("ground")->at(9, 9), 0U);

    const auto* objects = map.objectLayer("objects");
    ASSERT_NE(objects, nullptr);
    ASSERT_EQ(objects->objects.size(), 3U);
    EXPECT_EQ(objects->objects[0].type, "player");
    EXPECT_TRUE(objects->objects[0].point);
    EXPECT_EQ(objects->objects[1].type, "coin"); // "class"
    EXPECT_EQ(objects->objects[1].properties.integer("value"), 5);
    EXPECT_EQ(objects->objects[2].gid, 6U);
}

TEST(TiledMapTests, ConvertsToWorldCoordinates)
{
    const TiledMap map = astra2d::parseTiledMap(sampleMap());
    // y flips: the map's top edge (Tiled y = 0) is 3 tiles up.
    EXPECT_EQ(map.toWorld({0.0F, 0.0F}, 1.0F), glm::vec2(0.0F, 3.0F));
    EXPECT_EQ(map.toWorld({16.0F, 48.0F}, 2.0F), glm::vec2(2.0F, 0.0F));
    EXPECT_EQ(map.cellCenter(0, 0, 1.0F), glm::vec2(0.5F, 2.5F));
    EXPECT_EQ(map.cellCenter(3, 2, 16.0F), glm::vec2(56.0F, 8.0F));

    const auto& objects = map.objectLayer("objects")->objects;
    EXPECT_EQ(map.objectCenter(objects[0], 1.0F), glm::vec2(0.5F, 1.5F)); // a point
    EXPECT_EQ(map.objectCenter(objects[1], 1.0F), glm::vec2(2.5F, 1.5F)); // top-left corner
    EXPECT_EQ(map.objectCenter(objects[2], 1.0F), glm::vec2(3.5F, 0.5F)); // tile: bottom-left
    EXPECT_EQ(map.objectSize(objects[1], 16.0F), glm::vec2(16.0F));
}

TEST(TiledMapTests, MakesACollisionGridWithRowsFromTheBottom)
{
    const TiledMap map = astra2d::parseTiledMap(sampleMap());
    const astra2d::TileGrid grid = astra2d::makeTileGrid(map, *map.tileLayer("ground"), 16.0F);
    EXPECT_EQ(grid.width, 4);
    EXPECT_EQ(grid.height, 3);
    EXPECT_EQ(grid.tileSize, 16.0F);
    EXPECT_EQ(grid.origin, glm::vec2(0.0F));
    // Tiled's bottom row (1, 1, 3, flipped 1) is TileGrid row 0.
    EXPECT_EQ(grid.at(0, 0), TileCollision::Solid);
    EXPECT_EQ(grid.at(2, 0), TileCollision::Solid);
    EXPECT_EQ(grid.at(3, 0), TileCollision::Solid); // flipped tiles keep their properties
    EXPECT_EQ(grid.at(0, 1), TileCollision::Empty);
    EXPECT_EQ(grid.at(2, 2), TileCollision::OneWay); // Tiled's top row
    EXPECT_EQ(grid.at(0, 2), TileCollision::Empty);

    // A custom rule: everything that is not empty is solid.
    const auto all =
        astra2d::makeTileGrid(map, *map.tileLayer("ground"), 1.0F,
                              [](const astra2d::TiledTile&) { return TileCollision::Solid; });
    EXPECT_EQ(all.at(2, 2), TileCollision::Solid);
    EXPECT_EQ(astra2d::mergeTiles(all).size(), 2U); // the bottom row and the top-right pair
}

TEST(TiledMapTests, ReadsBase64LayerData)
{
    json j = sampleMap();
    auto& layer = j["layers"][0];
    // 12 little-endian 32-bit ids, all 0 but a 2 at index 2, Base64-encoded (48 bytes: no
    // padding).
    std::vector<std::uint8_t> bytes(48, 0);
    bytes[8] = 2;
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string data;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (static_cast<unsigned>(bytes[i]) << 16U) |
                           (static_cast<unsigned>(bytes[i + 1]) << 8U) | bytes[i + 2];
        for (int k = 3; k >= 0; --k) {
            data += kAlphabet[(n >> (6U * static_cast<unsigned>(k))) & 63U];
        }
    }
    layer["encoding"] = "base64";
    layer["data"] = data;
    const TiledMap map = astra2d::parseTiledMap(j);
    EXPECT_EQ(map.tileLayer("ground")->at(2, 0), 2U);
    EXPECT_EQ(map.tileLayer("ground")->at(3, 2), 0U);
}

TEST(TiledMapTests, UnsupportedMapsThrowWithAHint)
{
    const auto expectError = [](const json& j, const std::string& message) {
        try {
            (void)astra2d::parseTiledMap(j);
            FAIL() << "expected an error containing '" << message << "'";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
        }
    };
    json infinite = sampleMap();
    infinite["infinite"] = true;
    expectError(infinite, "Infinite");
    json iso = sampleMap();
    iso["orientation"] = "isometric";
    expectError(iso, "orthogonal");
    json compressed = sampleMap();
    compressed["layers"][0]["compression"] = "zlib";
    expectError(compressed, "CSV");
    json shortData = sampleMap();
    shortData["layers"][0]["data"] = json::array({1, 2});
    expectError(shortData, "instead of 4 x 3");
    json noWidth = sampleMap();
    noWidth.erase("width");
    expectError(noWidth, "'width'");
    json wrongType = sampleMap();
    wrongType["height"] = "three";
    expectError(wrongType, "Tiled map");
    expectError(json::array(), "not a map");
    json xml = sampleMap();
    xml["tilesets"][0] = {{"firstgid", 1}, {"source", "tiles.tsx"}};
    expectError(xml, "JSON (.tsj)");
}

TEST_F(TiledFileTest, LoadsExternalTilesetsRelativeToTheMap)
{
    json map = sampleMap();
    json tileset = map["tilesets"][0];
    tileset.erase("firstgid");
    tileset["image"] = "tiles.png";
    std::filesystem::create_directories(dir() / "tilesets");
    write("tilesets/tiles.tsj", tileset);
    map["tilesets"][0] = {{"firstgid", 1}, {"source", "../tilesets/tiles.tsj"}};
    write("levels/caves.tmj", map);

    const TiledMap loaded = astra2d::loadTiledMap(dir() / "levels" / "caves.tmj");
    ASSERT_EQ(loaded.tilesets.size(), 1U);
    EXPECT_EQ(loaded.tilesets[0].firstGid, 1);
    // Relative to the map's directory, like the tileset itself.
    EXPECT_EQ(loaded.tilesets[0].image, std::filesystem::path("../tilesets/tiles.png"));
    const astra2d::TiledTile solid = loaded.tile(1).value_or(astra2d::TiledTile{});
    ASSERT_NE(solid.properties, nullptr);
    EXPECT_EQ(solid.properties->string("collision"), "solid");
}

TEST_F(TiledFileTest, FileErrorsNameTheFile)
{
    EXPECT_THROW((void)astra2d::loadTiledMap(dir() / "missing.tmj"), std::runtime_error);
    std::ofstream(dir() / "broken.tmj") << "{ not json";
    try {
        (void)astra2d::loadTiledMap(dir() / "broken.tmj");
        FAIL();
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("broken.tmj"), std::string::npos);
    }
    json map = sampleMap();
    map["tilesets"][0] = {{"firstgid", 1}, {"source", "nowhere.tsj"}};
    write("levels/lost.tmj", map);
    EXPECT_THROW((void)astra2d::loadTiledMap(dir() / "levels" / "lost.tmj"), std::runtime_error);
}
