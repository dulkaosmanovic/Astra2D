// The platformer example's game logic, which does not need OpenGL: text and Tiled levels (and the
// shipped levels themselves), and the movement rules with coyote time and jump buffering.

#include "../examples/platformer/controller.hpp"
#include "../examples/platformer/level.hpp"

#include <astra2d/scene/tiled_map.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

using platformer::JumpController;
using platformer::Level;
using platformer::MoveTuning;
using platformer::ThingKind;
namespace tile = platformer::tile;

namespace {

constexpr float kDt = 1.0F / 60.0F;

const char* const kSmallLevel = R"(; a comment
name: Test Hills
hint: jump!
map:
...o.....
.P...-e.F
#####^###
)";

void expectError(const std::string& text, const std::string& message)
{
    try {
        (void)platformer::parseTextLevel(text);
        FAIL() << "expected an error containing '" << message << "'";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Text levels

TEST(PlatformerLevelTests, ParsesATextLevel)
{
    const Level level = platformer::parseTextLevel(kSmallLevel);
    EXPECT_EQ(level.name, "Test Hills");
    EXPECT_EQ(level.hint, "jump!");
    EXPECT_EQ(level.width, 9);
    EXPECT_EQ(level.height, 3);
    EXPECT_EQ(level.size(), glm::vec2(9.0F * 16.0F, 3.0F * 16.0F));
    // Row 0 is the bottom line of the map.
    EXPECT_EQ(level.at(level.ground, 0, 0), tile::kGrass);     // the border counts as ground
    EXPECT_EQ(level.at(level.ground, 6, 0), tile::kGrassLeft); // spikes on its left
    EXPECT_EQ(level.at(level.ground, 5, 0), tile::kSpikes);
    EXPECT_EQ(level.at(level.ground, 4, 0), tile::kGrassRight); // spikes on its right
    EXPECT_EQ(level.at(level.ground, 5, 1), tile::kPlank);
    EXPECT_EQ(level.at(level.ground, 0, 2), tile::kNone);
    EXPECT_EQ(level.at(level.ground, -1, 0), tile::kNone);

    EXPECT_EQ(level.solid.at(0, 0), astra2d::TileCollision::Solid);
    EXPECT_EQ(level.solid.at(5, 0), astra2d::TileCollision::Empty); // spikes do not collide
    EXPECT_EQ(level.platforms.at(5, 1), astra2d::TileCollision::OneWay);
    ASSERT_EQ(level.hazards.size(), 1U);
    EXPECT_EQ(level.hazards[0].x, 5);
    EXPECT_EQ(level.hazards[0].y, 0);

    EXPECT_EQ(level.playerStart(), glm::vec2(1.5F * 16.0F, 1.5F * 16.0F));
    EXPECT_EQ(level.count(ThingKind::Coin), 1);
    EXPECT_EQ(level.count(ThingKind::Enemy), 1);
    EXPECT_EQ(level.count(ThingKind::Goal), 1);
}

TEST(PlatformerLevelTests, GroundGetsGrassOnTopAndDirtBelow)
{
    const Level level = platformer::parseTextLevel("map:\n.P.F.\n.###.\n.###.\n");
    EXPECT_EQ(level.at(level.ground, 1, 1), tile::kGrassLeft);
    EXPECT_EQ(level.at(level.ground, 2, 1), tile::kGrass);
    EXPECT_EQ(level.at(level.ground, 3, 1), tile::kGrassRight);
    EXPECT_EQ(level.at(level.ground, 2, 0), tile::kDirt);
    const Level pillar = platformer::parseTextLevel("map:\nP.F\n.#.\n");
    EXPECT_EQ(pillar.at(pillar.ground, 1, 0), tile::kGrassSingle);
}

TEST(PlatformerLevelTests, ShortRowsArePaddedAndDecorationIsSeparate)
{
    const Level level = platformer::parseTextLevel("map:\nP\nbF.....\n#######\n");
    EXPECT_EQ(level.width, 7);
    EXPECT_EQ(level.at(level.decor, 0, 1), tile::kBush);
    EXPECT_EQ(level.at(level.ground, 0, 1), tile::kNone);
    EXPECT_EQ(level.solid.at(0, 1), astra2d::TileCollision::Empty);
}

TEST(PlatformerLevelTests, MalformedTextLevelsThrow)
{
    expectError("name: x\n", "no 'map:'");
    expectError("map:\n\n", "no rows");
    expectError("colour: red\nmap:\nPF\n", "line 1: unknown key 'colour'");
    expectError("this is not a key\n", "line 1");
    expectError("; c\nmap:\nP?F\n", "line 3, column 2: unknown tile '?'");
    expectError("map:\n...F\n####\n", "exactly one player start, not 0");
    expectError("map:\nP.PF\n####\n", "exactly one player start, not 2");
    expectError("map:\nP...\n####\n", "no goal");
}

TEST(PlatformerLevelTests, ToleratesWindowsLineEnds)
{
    const Level level = platformer::parseTextLevel("name: crlf\r\nmap:\r\nPF\r\n##\r\n");
    EXPECT_EQ(level.name, "crlf");
    EXPECT_EQ(level.width, 2);
}

// ---------------------------------------------------------------------------------------------
// Tiled levels

TEST(PlatformerLevelTests, MakesALevelFromATiledMap)
{
    const nlohmann::json map = nlohmann::json::parse(R"({
      "type": "map", "orientation": "orthogonal", "infinite": false,
      "width": 3, "height": 2, "tilewidth": 16, "tileheight": 16,
      "properties": [{"name": "name", "type": "string", "value": "Tiled Test"}],
      "tilesets": [{"firstgid": 1, "name": "tiles", "tilewidth": 16, "tileheight": 16,
                    "tilecount": 16, "columns": 8,
                    "tiles": [{"id": 0, "properties": [{"name": "collision", "type": "string", "value": "solid"}]},
                              {"id": 6, "properties": [{"name": "collision", "type": "string", "value": "oneway"}]},
                              {"id": 7, "properties": [{"name": "hazard", "type": "bool", "value": true}]}]}],
      "layers": [
        {"type": "tilelayer", "name": "decor", "width": 3, "height": 2, "data": [9, 0, 0, 0, 0, 0]},
        {"type": "tilelayer", "name": "ground", "width": 3, "height": 2, "data": [0, 7, 0, 1, 8, 1]},
        {"type": "objectgroup", "name": "objects", "objects": [
          {"id": 1, "type": "player", "point": true, "x": 8, "y": 8},
          {"id": 2, "type": "goal", "point": true, "x": 40, "y": 8},
          {"id": 3, "type": "coin", "x": 16, "y": 0, "width": 16, "height": 16}]}
      ]})");
    const Level level = platformer::levelFromTiled(astra2d::parseTiledMap(map));
    EXPECT_EQ(level.name, "Tiled Test");
    EXPECT_EQ(level.width, 3);
    EXPECT_EQ(level.height, 2);
    EXPECT_EQ(level.at(level.ground, 0, 0), 0); // Tiled's bottom row is row 0
    EXPECT_EQ(level.at(level.ground, 1, 1), tile::kPlank);
    EXPECT_EQ(level.at(level.decor, 0, 1), 8);
    EXPECT_EQ(level.solid.at(0, 0), astra2d::TileCollision::Solid);
    EXPECT_EQ(level.solid.at(1, 0), astra2d::TileCollision::Empty);
    EXPECT_EQ(level.platforms.at(1, 1), astra2d::TileCollision::OneWay);
    ASSERT_EQ(level.hazards.size(), 1U);
    EXPECT_EQ(level.hazards[0].x, 1);
    EXPECT_EQ(level.hazards[0].y, 0);
    EXPECT_EQ(level.playerStart(), glm::vec2(8.0F, 24.0F));
    EXPECT_EQ(level.count(ThingKind::Coin), 1);
}

TEST(PlatformerLevelTests, TiledLevelErrors)
{
    nlohmann::json map = nlohmann::json::parse(R"({
      "type": "map", "width": 2, "height": 1, "tilewidth": 16, "tileheight": 16,
      "layers": [{"type": "objectgroup", "name": "objects", "objects": []}]})");
    EXPECT_THROW((void)platformer::levelFromTiled(astra2d::parseTiledMap(map)),
                 std::runtime_error); // no ground layer
    map["layers"].push_back(
        {{"type", "tilelayer"}, {"name", "ground"}, {"width", 2}, {"height", 1}, {"data", {0, 0}}});
    map["layers"][0]["objects"].push_back({{"id", 7}, {"type", "dragon"}, {"x", 0}, {"y", 0}});
    try {
        (void)platformer::levelFromTiled(astra2d::parseTiledMap(map));
        FAIL();
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("unknown class 'dragon'"), std::string::npos);
    }
    map["tilewidth"] = 32;
    EXPECT_THROW((void)platformer::levelFromTiled(astra2d::parseTiledMap(map)), std::runtime_error);
}

// The levels that ship with the game load and are complete: a start, a goal, coins, enemies, and
// every thing inside the level.
TEST(PlatformerLevelTests, ShippedLevelsAreValid)
{
    const std::filesystem::path levels = std::filesystem::path(ASTRA2D_ASSETS_DIR) / "levels";
    for (const char* file : {"level1.txt", "level2.tmj", "level3.tmj"}) {
        SCOPED_TRACE(file);
        const Level level = platformer::loadLevel(levels / file);
        EXPECT_FALSE(level.name.empty());
        EXPECT_FALSE(level.hint.empty());
        EXPECT_GE(level.count(ThingKind::Coin), 10);
        EXPECT_GE(level.count(ThingKind::Enemy), 2);
        EXPECT_GE(level.count(ThingKind::Checkpoint), 1);
        for (const platformer::Thing& thing : level.things) {
            EXPECT_GT(thing.position.x, 0.0F);
            EXPECT_LT(thing.position.x, level.size().x);
            EXPECT_GT(thing.position.y, 0.0F);
            EXPECT_LT(thing.position.y, level.size().y);
        }
        // The player starts standing on something solid.
        const glm::vec2 start = level.playerStart() / platformer::kTileSize;
        EXPECT_EQ(level.solid.at(static_cast<int>(start.x), static_cast<int>(start.y) - 1),
                  astra2d::TileCollision::Solid);
    }
    EXPECT_THROW((void)platformer::loadLevel(levels / "missing.txt"), std::runtime_error);
}

// ---------------------------------------------------------------------------------------------
// Movement

TEST(PlatformerControllerTests, JumpsWhenPressedOnTheGround)
{
    JumpController jump;
    EXPECT_FALSE(jump.update(kDt, true, false));
    EXPECT_TRUE(jump.update(kDt, true, true));
    EXPECT_FALSE(jump.update(kDt, true, false)); // one press, one jump
}

TEST(PlatformerControllerTests, CannotJumpInTheAirWithoutCoyoteTime)
{
    JumpController jump;
    EXPECT_FALSE(jump.update(kDt, false, true));
    EXPECT_FALSE(jump.update(kDt, false, true));
}

TEST(PlatformerControllerTests, CoyoteTimeAllowsALateJump)
{
    const MoveTuning tuning;
    JumpController jump;
    jump.update(kDt, true, false);
    // Walked off a ledge: still jumps within the coyote time...
    const int lateSteps = static_cast<int>(tuning.coyoteTime / kDt) - 1;
    for (int i = 0; i < lateSteps; ++i) {
        EXPECT_FALSE(jump.update(kDt, false, false));
    }
    EXPECT_TRUE(jump.update(kDt, false, true));
    // ...but only once: a second press in the air does nothing.
    EXPECT_FALSE(jump.update(kDt, false, true));

    // Too late.
    JumpController late;
    late.update(kDt, true, false);
    for (int i = 0; i < static_cast<int>(tuning.coyoteTime / kDt) + 2; ++i) {
        late.update(kDt, false, false);
    }
    EXPECT_EQ(late.coyoteLeft(), 0.0F);
    EXPECT_FALSE(late.update(kDt, false, true));
}

TEST(PlatformerControllerTests, BufferedPressJumpsOnLanding)
{
    const MoveTuning tuning;
    JumpController jump;
    EXPECT_FALSE(jump.update(kDt, false, true)); // pressed while falling
    const int stepsBeforeLanding = static_cast<int>(tuning.jumpBufferTime / kDt) - 2;
    for (int i = 0; i < stepsBeforeLanding; ++i) {
        EXPECT_FALSE(jump.update(kDt, false, false));
    }
    EXPECT_GT(jump.bufferLeft(), 0.0F);
    EXPECT_TRUE(jump.update(kDt, true, false)); // landed: the jump happens now

    // Pressed too early: forgotten by the time of landing.
    JumpController early;
    early.update(kDt, false, true);
    for (int i = 0; i < static_cast<int>(tuning.jumpBufferTime / kDt) + 2; ++i) {
        early.update(kDt, false, false);
    }
    EXPECT_FALSE(early.update(kDt, true, false));
}

TEST(PlatformerControllerTests, ResetForgetsBothGraceTimes)
{
    JumpController jump;
    jump.update(kDt, true, false);
    jump.update(kDt, false, true);
    jump.reset();
    EXPECT_EQ(jump.coyoteLeft(), 0.0F);
    EXPECT_EQ(jump.bufferLeft(), 0.0F);
    JumpController custom(0.0F, 0.0F); // no grace at all
    custom.update(kDt, true, false);
    EXPECT_FALSE(custom.update(kDt, false, true));
}

TEST(PlatformerControllerTests, HorizontalVelocityAcceleratesAndStops)
{
    const MoveTuning t;
    float v = 0.0F;
    v = platformer::horizontalVelocity(v, 1.0F, true, t, kDt);
    EXPECT_NEAR(v, t.groundAccel * kDt, 1e-3F);
    for (int i = 0; i < 60; ++i) {
        v = platformer::horizontalVelocity(v, 1.0F, true, t, kDt);
    }
    EXPECT_EQ(v, t.runSpeed); // capped
    // Releasing stops faster on the ground than in the air.
    const float ground = platformer::horizontalVelocity(v, 0.0F, true, t, kDt);
    const float air = platformer::horizontalVelocity(v, 0.0F, false, t, kDt);
    EXPECT_LT(ground, air);
    // Turning around uses the deceleration.
    EXPECT_NEAR(platformer::horizontalVelocity(v, -1.0F, true, t, kDt), v - (t.groundDecel * kDt),
                1e-3F);
    // Stick input beyond 1 is clamped.
    EXPECT_LE(platformer::horizontalVelocity(t.runSpeed, 3.0F, true, t, kDt), t.runSpeed);
}

TEST(PlatformerControllerTests, VerticalVelocityJumpCutAndTerminalSpeed)
{
    const MoveTuning t;
    EXPECT_EQ(platformer::verticalVelocity(0.0F, true, true, t), t.jumpSpeed);
    EXPECT_EQ(platformer::verticalVelocity(t.jumpSpeed, false, true, t), t.jumpSpeed); // held
    EXPECT_EQ(platformer::verticalVelocity(t.jumpSpeed, false, false, t), t.jumpSpeed * t.jumpCut);
    EXPECT_EQ(platformer::verticalVelocity(-50.0F, false, false, t), -50.0F); // falling: no cut
    EXPECT_EQ(platformer::verticalVelocity(-1000.0F, false, true, t), -t.maxFallSpeed);
    EXPECT_EQ(platformer::approach(0.0F, 5.0F, 2.0F), 2.0F);
    EXPECT_EQ(platformer::approach(4.0F, 5.0F, 2.0F), 5.0F);
    EXPECT_EQ(platformer::approach(0.0F, -5.0F, 2.0F), -2.0F);
}

TEST(PlatformerControllerTests, JumpReachesFourTilesButNotFive)
{
    // The level design relies on it: platforms are at most 3 tiles above the ground, gaps at most
    // 3 tiles wide. Rising uses the normal gravity (gravityScale 1 while moving up).
    const MoveTuning t;
    constexpr float kGravity = 1200.0F;
    const float apex = t.jumpSpeed * t.jumpSpeed / (2.0F * kGravity);
    EXPECT_GT(apex, 3.5F * platformer::kTileSize);
    EXPECT_LT(apex, 5.0F * platformer::kTileSize);
    const float airTime =
        (t.jumpSpeed / kGravity) + std::sqrt(2.0F * apex / (kGravity * t.fallGravityScale));
    EXPECT_GT(airTime * t.runSpeed, 4.0F * platformer::kTileSize); // clears a 3-tile gap
}
