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
        return true;
    default:
        return false;
    }
}

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
    case '=':
        return grid.at(x, y - 1) == '=' ? tile::kStone : tile::kStoneTop;
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
    default:
        return tile::kNone;
    }
}

bool thingKind(char c, ThingKind& kind)
{
    switch (c) {
    case 'P':
        kind = ThingKind::Player;
        return true;
    case 'o':
        kind = ThingKind::Coin;
        return true;
    case 'e':
        kind = ThingKind::Enemy;
        return true;
    case 'C':
        kind = ThingKind::Checkpoint;
        return true;
    case 'F':
        kind = ThingKind::Goal;
        return true;
    default:
        return false;
    }
}

Level emptyLevel(int width, int height)
{
    Level level;
    level.width = width;
    level.height = height;
    const auto cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    level.ground.assign(cells, tile::kNone);
    level.decor.assign(cells, tile::kNone);
    for (astra2d::TileGrid* grid : {&level.solid, &level.platforms}) {
        grid->width = width;
        grid->height = height;
        grid->tileSize = kTileSize;
    }
    return level;
}

std::size_t cell(const Level& level, int x, int y)
{
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(level.width)) +
           static_cast<std::size_t>(x);
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
    ThingKind kind{};
    const bool isThing = thingKind(c, kind);
    if (frame == tile::kNone && decor == tile::kNone && !isThing && c != '.' && c != ' ') {
        fail(std::format("line {}, column {}: unknown tile '{}'", line, x + 1, c));
    }
    level.ground[cell(level, x, y)] = frame;
    level.decor[cell(level, x, y)] = decor;
    if (isSolidFrame(frame)) {
        level.solid.set(x, y, astra2d::TileCollision::Solid);
    } else if (frame == tile::kPlank) {
        level.platforms.set(x, y, astra2d::TileCollision::OneWay);
    } else if (frame == tile::kSpikes || frame == tile::kLava) {
        level.hazards.push_back({.x = x, .y = y, .frame = frame});
    }
    if (isThing) {
        level.things.push_back({.kind = kind, .position = level.cellCenter(x, y)});
    }
}

// The frames of a Tiled tile layer into @p frames; hazard tiles into the level's hazards.
void readTiledLayer(const astra2d::TiledMap& map, const astra2d::TiledTileLayer& layer,
                    std::vector<int>& frames, bool hazards, Level& level)
{
    for (int row = 0; row < layer.height; ++row) {
        for (int x = 0; x < layer.width; ++x) {
            const auto found = map.tile(layer.at(x, row));
            const int y = map.height - 1 - row;
            if (!found || x >= level.width || y < 0) {
                continue;
            }
            const astra2d::TiledTile& t = *found;
            frames[cell(level, x, y)] = t.id;
            if (hazards && t.properties != nullptr && t.properties->boolean("hazard")) {
                level.hazards.push_back({.x = x, .y = y, .frame = t.id});
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
    using Kind = std::pair<std::string_view, ThingKind>;
    static constexpr std::array<Kind, 5> kKinds = {{{"player", ThingKind::Player},
                                                    {"coin", ThingKind::Coin},
                                                    {"enemy", ThingKind::Enemy},
                                                    {"checkpoint", ThingKind::Checkpoint},
                                                    {"goal", ThingKind::Goal}}};
    for (const astra2d::TiledObject& object : objects->objects) {
        const auto* const kind = std::ranges::find(kKinds, object.type, &Kind::first);
        if (kind == kKinds.end()) {
            fail(std::format("object {} has the unknown class '{}'", object.id, object.type));
        }
        level.things.push_back(
            {.kind = kind->second, .position = map.objectCenter(object, kTileSize)});
    }
}

} // namespace

int Level::at(const std::vector<int>& layer, int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return tile::kNone;
    }
    return layer[cell(*this, x, y)];
}

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

    readTiledLayer(map, *ground, level.ground, true, level);
    if (const auto* decor = map.tileLayer("decor")) {
        readTiledLayer(map, *decor, level.decor, false, level);
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
