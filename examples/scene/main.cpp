// scene: the entity component system, the Scene and scene files.
//
// Everything on screen is an entity with components; the game logic is systems:
//   - a solar system built from the parent/child hierarchy: planets are children of rotating
//     "orbit" entities, the moon a child of the earth's orbit, so turning a parent moves the whole
//     branch;
//   - animated coins (SpriteRenderer + Animator) with a Pickup component; clicking one destroys
//     it, and an onDestroy<Pickup> signal handler spawns a burst and counts it, without the click
//     code knowing about either;
//   - the scene can be saved to JSON and loaded back, including the game's own components.
//
// Controls:
//   left click    collect the coin under the cursor
//   Space         spawn a coin at the cursor
//   F5 / F9       save the scene to / load it from scene.json next to the executable
//   R             rebuild the starting scene
//   Escape        quit

#include <astra2d/assets/asset_cache.hpp>
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>
#include <astra2d/core/paths.hpp>
#include <astra2d/ecs/registry.hpp>
#include <astra2d/core/signal.hpp>
#include <astra2d/renderer/camera.hpp>
#include <astra2d/renderer/font.hpp>
#include <astra2d/renderer/renderer2d.hpp>
#include <astra2d/renderer/sprite.hpp>
#include <astra2d/scene/components.hpp>
#include <astra2d/scene/scene.hpp>
#include <astra2d/scene/serializer.hpp>

#include "../common/example_assets.hpp"
#include "../common/hud.hpp"

#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <random>
#include <string>

namespace {

// ---------------------------------------------------------------------------------------------
// The game's own components: plain structs.

/// Turns the entity's Transform at a constant speed (radians per second).
struct Spin {
    float speed = 0.0F;
};

/// Marks a collectible coin.
struct Pickup {
    int value = 1;
};

/// A short-lived effect that grows and fades; its Lifetime destroys it.
struct Burst {};

constexpr float kCoinSize = 32.0F;
constexpr float kBurstTime = 0.4F;
constexpr glm::vec4 kText{0.9F, 0.9F, 0.9F, 1.0F};

class SceneDemo : public astra2d::Application {
public:
    SceneDemo()
        : Application({.window = {.title = "Astra2D: scene", .width = 960, .height = 600}})
        , m_assets(examples::assetsDirectory())
        , m_sheet(m_assets.texture("textures/sprites.png"),
                  {examples::kSpriteFrame, examples::kSpriteFrame},
                  {examples::kSpriteMargin, examples::kSpriteMargin},
                  {examples::kSpriteSpacing, examples::kSpriteSpacing})
        , m_font(astra2d::Font::fromFile(m_assets.resolve("fonts/PressStart2P-Regular.ttf"), 8.0F))
        , m_camera(600.0F, {window().framebufferWidth(), window().framebufferHeight()})
        , m_serializer(&m_assets)
        , m_saveFile(astra2d::executableDirectory() / "scene.json")
    {
        m_camera.fitArea({760.0F, 560.0F});
        addSystems();
        registerComponents();

        // Signals: react to coins disappearing, whoever removes them.
        m_pickupDestroyed = m_scene.registry().onDestroy<Pickup>().connect(
            [this](astra2d::Registry& registry, astra2d::Entity coin) {
            if (m_rebuilding) {
                return; // clearing the scene is not collecting
            }
            m_collected += registry.get<Pickup>(coin).value;
            spawnBurst(m_scene.worldTransform(coin).position);
        });
        buildScene();
    }

protected:
    void onFixedUpdate(float dt) override { m_scene.fixedUpdate(dt); }

    void onUpdate(float dt) override
    {
        const auto& in = input();
        if (in.isPressed(astra2d::Key::Escape)) {
            quit();
        }
        const glm::vec2 mouse = m_camera.screenToWorld(in.mousePosition());
        if (in.isPressed(astra2d::MouseButton::Left)) {
            collectAt(mouse);
        }
        if (in.isPressed(astra2d::Key::Space)) {
            spawnCoin(mouse);
        }
        if (in.isPressed(astra2d::Key::R)) {
            buildScene();
        }
        if (in.isPressed(astra2d::Key::F5)) {
            save();
        }
        if (in.isPressed(astra2d::Key::F9)) {
            load();
        }
        m_scene.update(dt);
    }

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        glClearColor(0.04F, 0.04F, 0.08F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        m_renderer.begin(m_camera);
        m_scene.render(m_renderer); // every SpriteRenderer, sorted by layer
        m_renderer.end();

        const int width = std::max(window().framebufferWidth(), 1);
        const int height = std::max(window().framebufferHeight(), 1);
        m_renderer.begin(astra2d::OrthographicCamera::screenSpace(width, height));
        const std::string hud = std::format(
            "entities {}  coins {}  collected {}\n"
            "click: collect  space: spawn  R: reset\n"
            "F5: save  F9: load  {}",
            m_scene.registry().alive(), m_scene.registry().count<Pickup>(), m_collected, m_status);
        examples::drawTextPanel(m_renderer, m_font, hud,
                                {24.0F, static_cast<float>(height) - 24.0F}, 16.0F, kText, 1.5F);
        m_renderer.end();
    }

private:
    // --- Systems ---------------------------------------------------------------------------------

    void addSystems()
    {
        // Gameplay at the fixed step.
        m_scene.addSystem(astra2d::SystemPhase::FixedUpdate, "spin",
                          [](astra2d::Scene& scene, float dt) {
            scene.registry().view<astra2d::Transform, Spin>().each(
                [dt](astra2d::Transform& transform, const Spin& spin) {
                transform.rotation += spin.speed * dt;
            });
        });
        // Effects once per frame. The built-in "lifetime" system destroys a burst when its
        // Lifetime is up; this one only makes it grow and fade on the way.
        m_scene.addSystem(astra2d::SystemPhase::Update, "bursts", [](astra2d::Scene& scene, float) {
            scene.registry()
                .view<Burst, astra2d::Lifetime, astra2d::Transform, astra2d::SpriteRenderer>()
                .each([](const Burst&, const astra2d::Lifetime& lifetime,
                         astra2d::Transform& transform, astra2d::SpriteRenderer& sprite) {
                const float t = lifetime.progress();
                transform.scale = glm::vec2(1.0F + (2.0F * t));
                sprite.tint.a = 1.0F - t;
            });
        });
    }

    // --- Scene file support for the game's components ----------------------------------------

    void registerComponents()
    {
        using nlohmann::json;
        using Save = astra2d::SceneSerializer::SaveContext;
        using Load = astra2d::SceneSerializer::LoadContext;
        m_serializer.registerComponent<Spin>("Spin", [](const Spin& s, const Save&) {
            return json{{"speed", s.speed}};
        }, [](const json& j, const Load&) { return Spin{j.at("speed").get<float>()}; });
        m_serializer.registerComponent<Pickup>("Pickup", [](const Pickup& p, const Save&) {
            return json{{"value", p.value}};
        }, [](const json& j, const Load&) { return Pickup{j.at("value").get<int>()}; });
        // A tag: nothing to save. The burst's Lifetime is a built-in component.
        m_serializer.registerComponent<Burst>("Burst", [](const Burst&, const Save&) {
            return json::object();
        }, [](const json&, const Load&) { return Burst{}; });
        // Animator points at run-time data (a sheet, an animation), which a file cannot hold, so
        // the game stores a name and maps it back when loading.
        m_serializer.registerComponent<astra2d::Animator>(
            "Animator", [this](const astra2d::Animator& a, const Save&) {
            return json{{"animation", a.player.animation() == &m_coinSpin ? "coin_spin" : ""}};
        }, [this](const json& j, const Load&) {
            astra2d::Animator animator{.sheet = &m_sheet};
            if (j.at("animation") == "coin_spin") {
                animator.player.play(m_coinSpin);
            }
            return animator;
        });
    }

    // --- Building the scene ----------------------------------------------------------------------

    void buildScene()
    {
        m_rebuilding = true;
        m_scene.clear();
        m_rebuilding = false;
        m_collected = 0;

        const astra2d::Entity sun = m_scene.createEntity("sun");
        addQuad(sun, 70.0F, {1.0F, 0.8F, 0.3F, 1.0F});
        m_scene.registry().emplace<Spin>(sun, 0.3F);

        const astra2d::Entity earth =
            addPlanet(sun, "earth", 170.0F, 0.6F, 30.0F, {0.3F, 0.6F, 1.0F, 1.0F});
        addPlanet(earth, "moon", 40.0F, 2.5F, 12.0F, {0.8F, 0.8F, 0.8F, 1.0F});
        addPlanet(sun, "mars", 250.0F, 0.35F, 22.0F, {0.9F, 0.4F, 0.3F, 1.0F});

        std::mt19937 random(std::random_device{}());
        std::uniform_real_distribution<float> x(-340.0F, 340.0F);
        std::uniform_real_distribution<float> y(-250.0F, 250.0F);
        for (int i = 0; i < 10; ++i) {
            spawnCoin({x(random), y(random)});
        }
        m_status = "";
    }

    void addQuad(astra2d::Entity entity, float size, glm::vec4 color)
    {
        m_scene.registry().emplace<astra2d::SpriteRenderer>(
            entity, astra2d::SpriteRenderer{.sprite = {}, .size = glm::vec2(size), .tint = color});
    }

    // A planet circling @p center: an invisible "orbit" child that spins, with the planet as its
    // child at @p radius. Returns the planet.
    astra2d::Entity addPlanet(astra2d::Entity center, const std::string& name, float radius,
                              float speed, float size, glm::vec4 color)
    {
        const astra2d::Entity orbit = m_scene.createEntity(name + " orbit");
        m_scene.registry().emplace<Spin>(orbit, speed);
        m_scene.setParent(orbit, center);
        const astra2d::Entity planet = m_scene.createEntity(name, {radius, 0.0F});
        addQuad(planet, size, color);
        m_scene.setParent(planet, orbit);
        return planet;
    }

    void spawnCoin(glm::vec2 position)
    {
        const astra2d::Entity coin = m_scene.createEntity("coin", position);
        m_scene.registry().emplace<astra2d::SpriteRenderer>(
            coin, astra2d::SpriteRenderer{
                      .sprite = m_sheet.frame(4), .size = glm::vec2(kCoinSize), .layer = 1});
        auto& animator = m_scene.registry().emplace<astra2d::Animator>(
            coin, astra2d::Animator{.sheet = &m_sheet});
        animator.player.play(m_coinSpin);
        m_scene.registry().emplace<Pickup>(coin);
    }

    void spawnBurst(glm::vec2 position)
    {
        const astra2d::Entity burst = m_scene.createEntity({}, position);
        m_scene.registry().emplace<astra2d::SpriteRenderer>(
            burst, astra2d::SpriteRenderer{.sprite = m_sheet.frame(4),
                                           .size = glm::vec2(kCoinSize),
                                           .tint = {1.0F, 0.9F, 0.5F, 1.0F},
                                           .layer = 2});
        m_scene.registry().emplace<Burst>(burst);
        m_scene.registry().emplace<astra2d::Lifetime>(burst,
                                                      astra2d::Lifetime{.duration = kBurstTime});
    }

    void collectAt(glm::vec2 point)
    {
        // Collected later: destroying inside the view is only safe for the visited entity, and
        // the onDestroy handler creates a burst entity.
        m_scene.registry().view<Pickup, astra2d::Transform>().each(
            [&](astra2d::Entity coin, const Pickup&, const astra2d::Transform&) {
            if (glm::distance(m_scene.worldTransform(coin).position, point) < kCoinSize * 0.5F) {
                m_scene.destroyLater(coin);
            }
        });
        m_scene.flushDestroyed();
    }

    // --- Files
    // -------------------------------------------------------------------------------------

    void save()
    {
        try {
            m_serializer.saveFile(m_scene, m_saveFile);
            m_status = "saved";
            ASTRA_INFO("Saved the scene to {}", m_saveFile.string());
        } catch (const std::exception& error) {
            m_status = "save failed";
            ASTRA_ERROR("{}", error.what());
        }
    }

    void load()
    {
        // Load into a scratch scene first, so a broken file leaves the current scene untouched.
        astra2d::Scene check;
        try {
            (void)m_serializer.loadFile(check, m_saveFile);
        } catch (const std::exception& error) {
            m_status = "load failed";
            ASTRA_ERROR("{}", error.what());
            return;
        }
        m_rebuilding = true;
        m_scene.clear();
        m_rebuilding = false;
        (void)m_serializer.loadFile(m_scene, m_saveFile);
        m_status = "loaded";
        ASTRA_INFO("Loaded {} entities from {}", m_scene.registry().alive(), m_saveFile.string());
    }

    astra2d::AssetCache m_assets;
    astra2d::SpriteSheet m_sheet;
    astra2d::Font m_font;
    astra2d::OrthographicCamera m_camera;
    astra2d::Renderer2D m_renderer;
    const astra2d::SpriteAnimation m_coinSpin{.frames = {4, 5, 6, 7}, .frameDuration = 0.1F};
    // Declared after everything its entities refer to (sheet, animation), so it is destroyed first.
    astra2d::Scene m_scene;
    astra2d::SceneSerializer m_serializer;
    astra2d::Connection m_pickupDestroyed;
    std::filesystem::path m_saveFile;
    bool m_rebuilding = false;
    int m_collected = 0;
    std::string m_status;
};

} // namespace

int main()
{
    try {
        SceneDemo app;
        app.run();
    } catch (const std::exception& error) {
        ASTRA_CRITICAL("{}", error.what());
        return 1;
    }
    return 0;
}
