#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>
#include <astra2d/scene/serializer.hpp>

#include <gtest/gtest.h>

#include <array>
#include <clocale>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using astra2d::Entity;
using astra2d::Scene;
using astra2d::SceneSerializer;
using astra2d::Transform;
using nlohmann::json;

namespace {

// A game component that refers to another entity.
struct Target {
    Entity entity;
    float distance = 0.0F;
};

void registerTarget(SceneSerializer& serializer)
{
    serializer.registerComponent<Target>(
        "Target", [](const Target& t, const SceneSerializer::SaveContext& context) {
        return json{{"entity", context.id(t.entity)}, {"distance", t.distance}};
    }, [](const json& j, const SceneSerializer::LoadContext& context) {
        return Target{.entity = context.entity(j.at("entity")),
                      .distance = j.at("distance").get<float>()};
    });
}

} // namespace

TEST(SerializerTests, RoundTripKeepsComponentsAndHierarchy)
{
    Scene original;
    const Entity sun = original.createEntity("sun", {1.0F, 2.0F});
    original.registry().get<Transform>(sun).rotation = 0.5F;
    const Entity earth = original.createEntity("earth", {10.0F, 0.0F});
    const Entity moon = original.createEntity("moon", {2.0F, 0.0F});
    const Entity mars = original.createEntity("mars", {15.0F, 0.0F});
    original.setParent(earth, original.find("sun"));
    original.setParent(mars, sun);
    original.setParent(moon, earth);
    original.registry().emplace<astra2d::RigidBody>(
        moon, astra2d::RigidBody{.type = astra2d::BodyType::Kinematic, .velocity = {1.0F, -2.0F}});
    original.registry().emplace<astra2d::Collider>(
        moon, astra2d::Collider{
                  .shape = astra2d::ColliderShape::Circle, .radius = 3.0F, .isTrigger = true});
    original.registry().emplace<astra2d::SpriteRenderer>(
        mars, astra2d::SpriteRenderer{
                  .sprite = {}, .size = {4.0F, 4.0F}, .tint = {1, 0, 0, 1}, .layer = 2});

    original.registry().emplace<astra2d::Lifetime>(
        mars, astra2d::Lifetime{.duration = 2.5F, .age = 0.5F});
    const SceneSerializer serializer;
    const json saved = serializer.save(original);

    Scene loaded;
    const std::vector<Entity> entities = serializer.load(loaded, saved);
    ASSERT_EQ(entities.size(), 4U);

    const Entity loadedSun = loaded.find("sun");
    const Entity loadedEarth = loaded.find("earth");
    const Entity loadedMoon = loaded.find("moon");
    const Entity loadedMars = loaded.find("mars");
    EXPECT_EQ(loaded.parent(loadedMoon), loadedEarth);
    EXPECT_EQ(loaded.children(loadedSun), (std::vector<Entity>{loadedEarth, loadedMars}));
    EXPECT_FLOAT_EQ(loaded.registry().get<Transform>(loadedSun).rotation, 0.5F);
    EXPECT_FLOAT_EQ(loaded.worldTransform(loadedMoon).position.x,
                    original.worldTransform(moon).position.x);

    const auto& body = loaded.registry().get<astra2d::RigidBody>(loadedMoon);
    EXPECT_EQ(body.type, astra2d::BodyType::Kinematic);
    EXPECT_FLOAT_EQ(body.velocity.y, -2.0F);
    const auto& collider = loaded.registry().get<astra2d::Collider>(loadedMoon);
    EXPECT_EQ(collider.shape, astra2d::ColliderShape::Circle);
    EXPECT_TRUE(collider.isTrigger);
    const auto& sprite = loaded.registry().get<astra2d::SpriteRenderer>(loadedMars);
    EXPECT_EQ(sprite.layer, 2);
    EXPECT_EQ(sprite.sprite.texture, nullptr);
    EXPECT_EQ(loaded.registry().get<astra2d::Lifetime>(loadedMars),
              (astra2d::Lifetime{.duration = 2.5F, .age = 0.5F}));

    // Saving the loaded scene gives the same document.
    EXPECT_EQ(serializer.save(loaded), saved);
}

TEST(SerializerTests, RoundTripKeepsPhysicsComponents)
{
    Scene original;
    const Entity crate = original.createEntity("crate");
    original.registry().emplace<astra2d::RigidBody>(crate,
                                                    astra2d::RigidBody{.mass = 2.5F,
                                                                       .angularVelocity = 1.5F,
                                                                       .fixedRotation = true,
                                                                       .bullet = true,
                                                                       .allowSleep = false});
    original.registry().emplace<astra2d::Collider>(
        crate, astra2d::Collider{.shape = astra2d::ColliderShape::Polygon,
                                 .points = {{0, 0}, {1, 0}, {0, 1}},
                                 .oneWay = true,
                                 .category = 0x0004,
                                 .mask = 0x00F0});
    const Entity level = original.createEntity("level");
    astra2d::TilemapCollider tiles{.friction = 0.25F};
    tiles.grid.width = 3;
    tiles.grid.height = 2;
    tiles.grid.tileSize = 16.0F;
    tiles.grid.cells = {astra2d::TileCollision::Solid,  astra2d::TileCollision::Solid,
                        astra2d::TileCollision::Solid,  astra2d::TileCollision::Empty,
                        astra2d::TileCollision::OneWay, astra2d::TileCollision::Empty};
    original.registry().emplace<astra2d::TilemapCollider>(level, tiles);

    const SceneSerializer serializer;
    const json saved = serializer.save(original);
    // Rows as they look: top first.
    EXPECT_EQ(saved["entities"][1]["components"]["TilemapCollider"]["rows"],
              json::array({".-.", "###"}));

    Scene loaded;
    (void)serializer.load(loaded, saved);
    EXPECT_EQ(loaded.registry().get<astra2d::RigidBody>(loaded.find("crate")),
              original.registry().get<astra2d::RigidBody>(crate));
    EXPECT_EQ(loaded.registry().get<astra2d::Collider>(loaded.find("crate")),
              original.registry().get<astra2d::Collider>(crate));
    EXPECT_EQ(loaded.registry().get<astra2d::TilemapCollider>(loaded.find("level")), tiles);
}

TEST(SerializerTests, GameComponentsWithEntityReferences)
{
    Scene scene;
    const Entity hunter = scene.createEntity("hunter");
    const Entity prey = scene.createEntity("prey");
    scene.registry().emplace<Target>(hunter, Target{.entity = prey, .distance = 5.0F});

    SceneSerializer serializer;
    registerTarget(serializer);
    const json saved = serializer.save(scene);

    Scene loaded;
    serializer.load(loaded, saved);
    const Target& target = loaded.registry().get<Target>(loaded.find("hunter"));
    EXPECT_EQ(target.entity, loaded.find("prey"));
    EXPECT_FLOAT_EQ(target.distance, 5.0F);
}

TEST(SerializerTests, MissingFieldsKeepDefaultsAndUnknownComponentsAreSkipped)
{
    const json file = json::parse(R"({
        "format": "astra2d-scene", "version": 1,
        "entities": [
            { "id": 0, "parent": null,
              "components": { "Transform": { "position": [5, 6] }, "FutureThing": 42 } }
        ]
    })");
    Scene scene;
    const SceneSerializer serializer;
    const auto entities = serializer.load(scene, file);
    ASSERT_EQ(entities.size(), 1U);
    const Transform& t = scene.registry().get<Transform>(entities[0]);
    EXPECT_FLOAT_EQ(t.position.y, 6.0F);
    EXPECT_FLOAT_EQ(t.scale.x, 1.0F); // default
}

TEST(SerializerTests, LoadAddsToTheScene)
{
    Scene scene;
    scene.createEntity("existing");
    Scene source;
    source.createEntity("new");
    const SceneSerializer serializer;
    serializer.load(scene, serializer.save(source));
    EXPECT_EQ(scene.registry().alive(), 2U);
}

TEST(SerializerTests, BadFilesThrowAndLeaveTheSceneUnchanged)
{
    const SceneSerializer serializer;
    const auto expectRejected = [&](const char* text, const char* message) {
        Scene scene;
        scene.createEntity("existing");
        try {
            serializer.load(scene, json::parse(text));
            FAIL() << "expected an exception for " << text;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
        }
        EXPECT_EQ(scene.registry().alive(), 1U) << "partially loaded entities must be removed";
    };

    expectRejected(R"({"entities": []})", "Not an Astra2D scene");
    expectRejected(R"({"format": "astra2d-scene", "version": 99, "entities": []})", "version 99");
    expectRejected(R"({"format": "astra2d-scene", "version": 1,
                       "entities": [{"id": 0, "parent": 7, "components": {}}]})",
                   "not in the file");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": 1, "components": {}},
                       {"id": 1, "parent": 0, "components": {}}]})",
                   "cycle");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": null,
                        "components": {"Transform": {"position": [1]}}}]})",
                   "Transform");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": null,
                        "components": {"RigidBody": {"type": "floating"}}}]})",
                   "floating");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": null,
                        "components": {"SpriteRenderer": {"texture": "a.png"}}}]})",
                   "no AssetCache");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": null,
                        "components": {"Collider": {"halfExtents": [0, 1]}}}]})",
                   "halfExtents");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": null,
                        "components": {"Collider": {"shape": "circle", "radius": 0}}}]})",
                   "radius");
    expectRejected(R"({"format": "astra2d-scene", "version": 1, "entities": [
                       {"id": 0, "parent": null,
                        "components": {"TilemapCollider": {"tileSize": 0, "rows": ["#"]}}}]})",
                   "tileSize");
}

TEST(SerializerTests, FileRoundTrip)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "astra2d_test_scene.json";
    Scene scene;
    scene.createEntity("saved", {1.0F, 1.0F});
    const SceneSerializer serializer;
    serializer.saveFile(scene, path);
    serializer.saveFile(scene, path);                              // replaces the existing file
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp")); // renamed, not left behind

    Scene loaded;
    serializer.loadFile(loaded, path);
    EXPECT_FALSE(loaded.find("saved").isNull());
    std::filesystem::remove(path);

    EXPECT_THROW(serializer.loadFile(loaded, path), std::runtime_error); // now missing
}

TEST(SerializerTests, FloatsAreWrittenShortAndReadBackExactly)
{
    Scene scene;
    const Entity entity = scene.createEntity("e", {0.1F, 1.0F / 3.0F});
    const SceneSerializer serializer;
    const json saved = serializer.save(scene);
    const std::string text = saved.dump();
    EXPECT_NE(text.find("[0.1,"), std::string::npos) << text; // not 0.10000000149011612

    Scene loaded;
    serializer.load(loaded, saved);
    const glm::vec2 position = loaded.registry().get<Transform>(loaded.find("e")).position;
    EXPECT_EQ(position.x, 0.1F); // bit-exact round trip
    EXPECT_EQ(position.y, 1.0F / 3.0F);
    (void)entity;
}

TEST(SerializerTests, WronglyTypedHeaderFieldsThrowRuntimeError)
{
    // json::value() would have thrown json::type_error, which callers catching
    // std::runtime_error (as documented) did not expect.
    const SceneSerializer serializer;
    Scene scene;
    EXPECT_THROW(serializer.load(scene, json::parse(R"({"format": "astra2d-scene", "version": "1",
                                                        "entities": []})")),
                 std::runtime_error);
    EXPECT_THROW(serializer.load(scene, json::parse(R"({"format": 5, "version": 1,
                                                        "entities": []})")),
                 std::runtime_error);
    EXPECT_THROW(serializer.load(scene, json::parse("[1, 2]")), std::runtime_error);
}

TEST(SerializerTests, CommaDecimalLocaleDoesNotTruncateFloats)
{
    // A game may call setlocale(LC_ALL, ""), e.g. with a Bosnian, Croatian or German locale. The
    // float conversion must not depend on it.
    // setlocale() is process-wide; the tests run on one thread, and the old locale is restored.
    const std::array<const char*, 4> locales{"de_DE.UTF-8", "hr_HR.UTF-8", "bs_BA.UTF-8",
                                             "de_DE.utf8"};
    const std::string previous =
        std::setlocale(LC_NUMERIC, nullptr); // NOLINT(concurrency-mt-unsafe)
    const char* chosen = nullptr;
    for (const char* name : locales) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr) { // NOLINT(concurrency-mt-unsafe)
            chosen = name;
            break;
        }
    }
    if (chosen == nullptr) {
        GTEST_SKIP() << "no comma-decimal locale installed";
    }
    Scene scene;
    scene.createEntity("e", {0.3F, 2.5F});
    const SceneSerializer serializer;
    const json saved = serializer.save(scene);
    std::setlocale(LC_NUMERIC, previous.c_str()); // NOLINT(concurrency-mt-unsafe)

    Scene loaded;
    serializer.load(loaded, saved);
    const glm::vec2 position = loaded.registry().get<Transform>(loaded.find("e")).position;
    EXPECT_EQ(position.x, 0.3F) << "locale " << chosen;
    EXPECT_EQ(position.y, 2.5F);
}

TEST(SerializerTests, OnlyTheSizeTheColliderShapeUsesIsChecked)
{
    // A circle's unused halfExtents and a box's unused radius may be anything.
    const SceneSerializer serializer;
    Scene scene;
    EXPECT_NO_THROW(serializer.load(scene, json::parse(R"({"format": "astra2d-scene", "version": 1,
        "entities": [
          {"id": 0, "parent": null, "components": {"Collider":
            {"shape": "circle", "radius": 1, "halfExtents": [0, -1]}}},
          {"id": 1, "parent": null, "components": {"Collider":
            {"shape": "box", "halfExtents": [1, 1], "radius": 0}}}]})")));
    EXPECT_EQ(scene.registry().storage<astra2d::Collider>().size(), 2U);
}
