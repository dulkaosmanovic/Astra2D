# Astra2D {#mainpage}

<div class="astra-hero">
<img src="logo2.png" alt="Astra2D logo"/>
<div class="astra-tagline">
A modular 2D game engine written in C++20 and OpenGL&nbsp;3.3, with its own entity component
system and its own impulse-based physics engine.
</div>
<div class="astra-buttons">
<a class="astra-button primary" href="getting-started.html">Get started</a>
<a class="astra-button" href="architecture.html">Architecture</a>
<a class="astra-button" href="evaluation.html">Results</a>
<a class="astra-button" href="topics.html">API reference</a>
</div>
</div>

Astra2D is an engine *library*: a game links against it, derives from @ref astra2d::Application
and overrides a few hooks, so the engine never contains a `main()`. It is a university diploma
project about the architecture of a real-time engine. The systems that matter most are built from
scratch and measured, not borrowed.

<div class="astra-shots">
<img src="platformer_level1.png" alt="Astra Run on Planet Verda: a jump to the coins"/>
<img src="platformer_debug.png" alt="The same moment with the physics debug view"/>
</div>

*Astra Run*, the example game (@ref platformer_guide), and the same moment with the physics debug
view (F3).

<h2>Key results</h2>

<div class="astra-stats">
<div class="astra-stat"><span class="astra-stat-value">10</span><span class="astra-stat-label">draw calls for 100,000 sprites instead of 100,000; render 2.8× faster (E1)</span></div>
<div class="astra-stat"><span class="astra-stat-value">3.4×</span><span class="astra-stat-label">faster updates with the ECS than with OOP game objects at 1M objects, while its pools are in the same order (E2)</span></div>
<div class="astra-stat"><span class="astra-stat-value">67×</span><span class="astra-stat-label">faster physics step at 5,000 bodies with the uniform grid broad phase (E3)</span></div>
<div class="astra-stat"><span class="astra-stat-value">20</span><span class="astra-stat-label">boxes in an uneven tower that stands with 20 solver iterations; 10 with the default 8 (E4)</span></div>
<div class="astra-stat"><span class="astra-stat-value">75 %</span><span class="astra-stat-label">of engine lines covered by 400+ unit tests; 90 %+ in the modules without OpenGL</span></div>
</div>

How these were measured, and what they mean, is in @ref evaluation.

<h2>A minimal application</h2>

```cpp
#include <astra2d/core/application.hpp>

class MyGame : public astra2d::Application {
public:
    MyGame() : Application({ .window = { .title = "My Game", .width = 1280, .height = 720 } }) {}

protected:
    void onFixedUpdate(float dt) override { /* physics and simulation, 60 times per second */ }

    void onUpdate(float dt) override
    {
        if (input().isPressed(astra2d::Key::Escape)) {
            quit();
        }
    }

    void onRender() override { /* draw the frame */ }
};

int main()
{
    MyGame game;
    game.run();
}
```

<h2>Modules</h2>

<div class="astra-cards">
<div class="astra-card">
<span class="astra-card-title">Core <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Application and main loop with a fixed timestep, window, keyboard, mouse and gamepad input, action mapping, events, logging and assertions. @ref core_guide "Guide" · @ref core "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Renderer <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Batched sprite and text renderer, sprite sheets and animation, camera with rotation, parallax and shake, render targets and post-processing. @ref renderer_guide "Guide" · @ref renderer "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Assets <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Loads each texture, shader, effect and font once, and reloads them while the game runs when their files change. @ref renderer_guide "Guide" · @ref assets "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">ECS <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Sparse-set entity component system with generational entity handles, views and component signals. @ref ecs_guide "Guide" · @ref ecs "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Physics <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Circle, box and polygon colliders, three broad phases, impulse solver with friction, restitution and stable stacking, triggers, one-way platforms, sleeping, bullets, tile maps. @ref physics_guide "Guide" · @ref physics "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Scene <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Scenes with systems in a fixed order, built-in components, parent/child hierarchy, JSON scene files and Tiled maps. @ref ecs_guide "Guide" · @ref scene "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Audio <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Sound effects and music: overlapping voices with volume, pitch and pan, effect and music buses; runs without a sound device. @ref audio_guide "Guide" · @ref audio "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Debug tools <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Physics debug view of shapes, body states, contacts and velocities; frame time and render statistics. @ref debug_guide "Guide" · @ref debug "API"</div>
</div>
</div>

<h2>Design goals</h2>

- **Layered and testable.** Lower layers never include higher ones. The ECS, physics and audio do
  not touch OpenGL, so they are unit-tested without a GPU.
- **Built from scratch where it matters.** The ECS and the physics engine are the engine's own
  work; GLFW, GLAD, GLM, spdlog and miniaudio do the platform plumbing.
- **Measured.** Batching, the ECS layout and the broad phase are each compared against a naive
  baseline, and the solver's stability is measured, all in reproducible benchmarks.
- **Checked.** Tests run under AddressSanitizer, UndefinedBehaviorSanitizer and Valgrind, in CI on
  every push.

<h2>Reading path</h2>

New to the engine? Read the pages in this order. Each one ends with a link to the next.

<div class="astra-path">
<div class="astra-step"><span>@ref getting-started "Getting started": build the engine, run the examples, write a first application.</span></div>
<div class="astra-step"><span>@ref architecture "Architecture": the modules, their layers and how data moves through one frame.</span></div>
<div class="astra-step"><span>@ref core_guide "Core": the main loop, the fixed timestep, input, actions and events.</span></div>
<div class="astra-step"><span>@ref renderer_guide "Renderer": the camera, batching, sprites, text and the asset cache.</span></div>
<div class="astra-step"><span>@ref ecs_guide "ECS and scenes": entities, components, systems, the scene and scene files.</span></div>
<div class="astra-step"><span>@ref physics_guide "Physics": bodies, collision detection, the solver and the platformer features.</span></div>
<div class="astra-step"><span>@ref audio_guide "Audio" and @ref debug_guide "Debug tools": sound, the physics debug view and frame statistics.</span></div>
<div class="astra-step"><span>@ref platformer_guide "Example game": a complete game, showing how the pieces fit together.</span></div>
<div class="astra-step"><span>@ref evaluation "Evaluation" and @ref comparison "Comparison": what was measured, and how the API compares with raylib and Godot.</span></div>
</div>

To look up a class or function, use [Modules](topics.html), [Classes](annotated.html) or the search
box.
