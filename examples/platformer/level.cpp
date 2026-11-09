#include "level.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace platformer {

namespace {

[[noreturn]] void fail(std::string_view message)
{
    throw std::runtime_error(std::string(message));
}

constexpr bool isSolidFrame(int frame)
{
    switch (frame) {
    case tile::kGrass:
    case tile::kDirt:
    case tile::kGrassLeft:
    case tile::kGrassRight:
    case tile::kStoneTop:
    case tile::kStone:
    case tile::kCrate:
    case tile::kBrick:
    case tile::kGrassSingle:
    case tile::kIceTop:
    case tile::kIce:
    case tile::kBasaltTop:
    case tile::kBasalt:
    case tile::kMossTop:
    case tile::kMoss:
        return true;
    default:
        return false;
    }
}

constexpr bool isOneWayFrame(int frame)
{
    return frame == tile::kPlank || frame == tile::kCloud;
}

// How a thing is written in a text map and named in a Tiled map.
struct ThingCode {
    char symbol;
    std::string_view name;
    ThingKind kind;
};
constexpr std::array<ThingCode, 11> kThingCodes = {{
    {.symbol = 'P', .name = "player", .kind = ThingKind::Player},
    {.symbol = 'o', .name = "coin", .kind = ThingKind::Coin},
    {.symbol = 'e', .name = "crawler", .kind = ThingKind::Crawler},
    {.symbol = 'v', .name = "flyer", .kind = ThingKind::Flyer},
    {.symbol = 'k', .name = "spiker", .kind = ThingKind::Spiker},
    {.symbol = 'j', .name = "spring", .kind = ThingKind::Spring},
    {.symbol = 'H', .name = "heart", .kind = ThingKind::Heart},
    {.symbol = 'M', .name = "platform", .kind = ThingKind::Platform},
    {.symbol = 'V', .name = "lift", .kind = ThingKind::Lift},
    {.symbol = 'C', .name = "checkpoint", .kind = ThingKind::Checkpoint},
    {.symbol = 'F', .name = "goal", .kind = ThingKind::Goal},
}};

// The rows of a text map, top first, all the same width.
class TextGrid {
public:
    explicit TextGrid(std::vector<std::string> rows)
        : m_rows(std::move(rows))
    {
    }
    [[nodiscard]] int width() const
    {
        return m_rows.empty() ? 0 : static_cast<int>(m_rows.front().size());
    }
    [[nodiscard]] int height() const { return static_cast<int>(m_rows.size()); }
    // Outside the map counts as ground, so there are no grass edges at the map's border.
    [[nodiscard]] char at(int x, int y) const
    {
        if (x < 0 || y < 0 || y >= height() || x >= width()) {
            return '#';
        }
        return m_rows[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    }

private:
    std::vector<std::string> m_rows;
};

// The tile drawn for character (x, y) of a text map (y from the top): ground gets grass where
// nothing is on top of it, with edges where it ends. Same as choose_tile() in
// scripts/generate_platformer_assets.py, which makes the Tiled levels.
int textTile(const TextGrid& grid, int x, int y)
{
    switch (grid.at(x, y)) {
    case '#': {
        if (grid.at(x, y - 1) == '#') {
            return tile::kDirt;
        }
        const bool left = grid.at(x - 1, y) == '#';
        const bool right = grid.at(x + 1, y) == '#';
        if (left && right) {
            return tile::kGrass;
        }
        if (left) {
            return tile::kGrassRight;
        }
        return right ? tile::kGrassLeft : tile::kGrassSingle;
    }
    case '=': // rock with a top: stone, ice, basalt and moss
        return grid.at(x, y - 1) == '=' ? tile::kStone : tile::kStoneTop;
    case 'I':
        return grid.at(x, y - 1) == 'I' ? tile::kIce : tile::kIceTop;
    case 'R':
        return grid.at(x, y - 1) == 'R' ? tile::kBasalt : tile::kBasaltTop;
    case 'N':
        return grid.at(x, y - 1) == 'N' ? tile::kMoss : tile::kMossTop;
    case '-':
        return tile::kPlank;
    case '^':
        return tile::kSpikes;
    case 'X':
        return tile::kCrate;
    case 'B':
        return tile::kBrick;
    case 'L':
        return tile::kLava;
    case '~':
        return tile::kCloud;
    default:
        return tile::kNone;
    }
}

int decorTile(char c)
{
    switch (c) {
    case 'b':
        return tile::kBush;
    case 'f':
        return tile::kFlower;
    case 'r':
        return tile::kRock;
    case 's':
        return tile::kSign;
    case 'c':
        return tile::kCrystal;
    case 'u':
        return tile::kMushroom;
    case 'g':
        return tile::kTuft;
    case 't':
        return tile::kScorchedFlowers;
    case 'y':
        return tile::kPine;
    case 'a':
        return tile::kAlienPlant;
    case 'd':
        return tile::kDish;
    case 'q':
        return tile::kGeode;
    case 'p':
        return tile::kPuffballs;
    case 'w':
        return tile::kCloudShrub;
    case 'l':
        return tile::kBellFlower;
    case 'x':
        return tile::kCharredTree;
    case 'n':
        return tile::kAshGrass;
    case 'm':
        return tile::kMagmaBoulder;
    default:
        return tile::kNone;
    }
}

Level emptyLevel(int width, int height)
{
    Level level;
    level.width = width;
    level.height = height;
    for (astra2d::TileLayer* layer : {&level.ground, &level.decor}) {
        *layer = {.width = width, .height = height, .tileSize = kTileSize};
    }
    for (astra2d::TileGrid* grid : {&level.solid, &level.platforms}) {
        grid->width = width;
        grid->height = height;
        grid->tileSize = kTileSize;
    }
    return level;
}

void validate(const Level& level)
{
    if (level.width <= 0 || level.height <= 0) {
        fail("the level has no tiles");
    }
    if (level.count(ThingKind::Player) != 1) {
        fail(std::format("the level needs exactly one player start, not {}",
                         level.count(ThingKind::Player)));
    }
    if (level.count(ThingKind::Goal) == 0) {
        fail("the level has no goal");
    }
}

std::string trim(std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t");
    return std::string(text.substr(begin, end - begin + 1));
}

// A text map split into its header and rows (top first, padded to the same width).
struct TextMap {
    std::string name;
    std::string hint;
    std::vector<std::string> rows;
    std::vector<int> rowLines; // line number of each row, for errors
};

// One "key: value" line of the header; sets inMap at "map:".
void readHeaderLine(const std::string& line, int number, TextMap& map, bool& inMap)
{
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        fail(std::format("line {}: expected 'key: value' or 'map:'", number));
    }
    const std::string key = trim(std::string_view(line).substr(0, colon));
    std::string value = trim(std::string_view(line).substr(colon + 1));
    if (key == "map") {
        inMap = true;
    } else if (key == "name") {
        map.name = std::move(value);
    } else if (key == "hint") {
        map.hint = std::move(value);
    } else {
        fail(std::format("line {}: unknown key '{}'", number, key));
    }
}

TextMap readTextMap(std::string_view text)
{
    TextMap map;
    bool inMap = false;
    std::istringstream in{std::string(text)};
    std::string line;
    for (int number = 1; std::getline(in, line); ++number) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (inMap) {
            map.rows.push_back(line);
            map.rowLines.push_back(number);
        } else if (!trim(line).empty() && line.front() != ';') {
            readHeaderLine(line, number, map, inMap);
        }
    }
    while (!map.rows.empty() && trim(map.rows.back()).empty()) { // trailing empty lines
        map.rows.pop_back();
    }
    if (!inMap) {
        fail("no 'map:' line");
    }
    if (map.rows.empty()) {
        fail("the map has no rows");
    }
    const std::size_t width =
        std::ranges::max(map.rows, {}, [](const std::string& r) { return r.size(); }).size();
    for (std::string& row : map.rows) {
        row.resize(width, '.');
    }
    return map;
}

// Fills the cell of character (x, row) of a text map into the level.
void addTextCell(Level& level, const TextGrid& grid, int x, int row, int line)
{
    const int y = grid.height() - 1 - row; // the first row is the top one
    const char c = grid.at(x, row);
    const int frame = textTile(grid, x, row);
    const int decor = decorTile(c);
    const auto* const thing = std::ranges::find(kThingCodes, c, &ThingCode::symbol);
    const bool isThing = thing != kThingCodes.end();
    if (frame == tile::kNone && decor == tile::kNone && !isThing && c != '.' && c != ' ') {
        fail(std::format("line {}, column {}: unknown tile '{}'", line, x + 1, c));
    }
    level.ground.set(x, y, frame);
    level.decor.set(x, y, decor);
    if (isSolidFrame(frame)) {
        level.solid.set(x, y, astra2d::TileCollision::Solid);
    } else if (isOneWayFrame(frame)) {
        level.platforms.set(x, y, astra2d::TileCollision::OneWay);
    } else if (frame == tile::kSpikes || frame == tile::kLava) {
        level.hazards.push_back({.x = x, .y = y, .frame = frame});
    }
    if (isThing) {
        level.things.push_back({.kind = thing->kind, .position = level.cellCenter(x, y)});
    }
}

// The frames of a Tiled tile layer into @p frames; hazard tiles into the level's hazards.
// The tiles of @p layer with the property `hazard` hurt.
void readTiledHazards(const astra2d::TiledMap& map, const astra2d::TiledTileLayer& layer,
                      Level& level)
{
    for (int row = 0; row < layer.height; ++row) {
        for (int x = 0; x < layer.width; ++x) {
            const auto found = map.tile(layer.at(x, row));
            if (found && found->properties != nullptr && found->properties->boolean("hazard")) {
                level.hazards.push_back({.x = x, .y = map.height - 1 - row, .frame = found->id});
            }
        }
    }
}

void readTiledObjects(const astra2d::TiledMap& map, Level& level)
{
    const auto* objects = map.objectLayer("objects");
    if (objects == nullptr) {
        return;
    }
    for (const astra2d::TiledObject& object : objects->objects) {
        const auto* const thing = std::ranges::find(kThingCodes, object.type, &ThingCode::name);
        if (thing == kThingCodes.end()) {
            fail(std::format("object {} has the unknown class '{}'", object.id, object.type));
        }
        level.things.push_back(
            {.kind = thing->kind, .position = map.objectCenter(object, kTileSize)});
    }
}

} // namespace

glm::vec2 Level::cellCenter(int x, int y) const
{
    return glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) * kTileSize;
}

glm::vec2 Level::size() const
{
    return glm::vec2(static_cast<float>(width), static_cast<float>(height)) * kTileSize;
}

glm::vec2 Level::playerStart() const
{
    const auto it = std::ranges::find(things, ThingKind::Player, &Thing::kind);
    return it != things.end() ? it->position : glm::vec2(0.0F);
}

int Level::count(ThingKind kind) const
{
    return static_cast<int>(std::ranges::count(things, kind, &Thing::kind));
}

Level parseTextLevel(std::string_view text)
{
    const TextMap map = readTextMap(text);
    const TextGrid grid(map.rows);
    Level level = emptyLevel(grid.width(), grid.height());
    level.name = map.name;
    level.hint = map.hint;
    for (int row = 0; row < grid.height(); ++row) {
        for (int x = 0; x < grid.width(); ++x) {
            addTextCell(level, grid, x, row, map.rowLines[static_cast<std::size_t>(row)]);
        }
    }
    validate(level);
    return level;
}

Level levelFromTiled(const astra2d::TiledMap& map)
{
    if (map.tileSize != glm::ivec2(16)) {
        fail("the Tiled map must have 16x16 tiles");
    }
    const astra2d::TiledTileLayer* ground = map.tileLayer("ground");
    if (ground == nullptr) {
        fail("the Tiled map has no tile layer 'ground'");
    }
    Level level = emptyLevel(map.width, map.height);
    level.name = map.properties.string("name", "Untitled");
    level.hint = map.properties.string("hint");

    const auto only = [](astra2d::TileCollision kind) {
        return [kind](const astra2d::TiledTile& t) {
            return astra2d::tiledCollision(t) == kind ? kind : astra2d::TileCollision::Empty;
        };
    };
    level.solid =
        astra2d::makeTileGrid(map, *ground, kTileSize, only(astra2d::TileCollision::Solid));
    level.platforms =
        astra2d::makeTileGrid(map, *ground, kTileSize, only(astra2d::TileCollision::OneWay));

    level.ground = astra2d::makeTileLayer(map, *ground, kTileSize);
    readTiledHazards(map, *ground, level);
    if (const auto* decor = map.tileLayer("decor")) {
        level.decor = astra2d::makeTileLayer(map, *decor, kTileSize);
    }
    readTiledObjects(map, level);
    validate(level);
    return level;
}

Level loadLevel(const std::filesystem::path& file)
{
    try {
        if (file.extension() == ".tmj" || file.extension() == ".json") {
            return levelFromTiled(astra2d::loadTiledMap(file));
        }
        const std::ifstream in(file);
        if (!in) {
            fail("cannot open the file");
        }
        std::ostringstream text;
        text << in.rdbuf();
        return parseTextLevel(text.str());
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::format("Level '{}': {}", file.string(), error.what()));
    }
}

} // namespace platformer
