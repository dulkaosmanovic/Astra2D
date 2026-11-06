# Astra2D {#mainpage}

<div class="astra-hero">
<img src="logo2.png" alt="Astra2D logo"/>
<div class="astra-tagline">
A modular 2D game engine written in C++20 and OpenGL&nbsp;3.3, with a custom entity component system
and a custom impulse-based physics engine.
</div>
<div class="astra-buttons">
<a class="astra-button primary" href="getting-started.html">Get started</a>
<a class="astra-button" href="architecture.html">Architecture</a>
<a class="astra-button" href="topics.html">API reference</a>
</div>
</div>

Astra2D is an engine *library*: games and tools link against it and derive from
@ref astra2d::Application, so the engine never contains a `main()`. It is developed as a
university diploma project, with the focus on the architecture of a real-time engine and on
systems that are built from scratch and measured, not borrowed.

<h2>A minimal application</h2>

```cpp
#include <astra2d/core/application.hpp>
#include <astra2d/core/log.hpp>

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
<div class="astra-card-text">Batched sprite and text renderer, sprite sheets and animation, camera with rotation, parallax and shake, post-processing, asset cache with hot reloading. @ref renderer_guide "Guide" · @ref renderer "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">ECS <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Sparse-set entity component system with generational entity handles, views and component signals. @ref ecs_guide "Guide" · @ref ecs "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Physics <span class="astra-status">planned</span></span>
<div class="astra-card-text">AABB and circle colliders, broad phase, impulse resolution with friction and restitution, triggers.</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Scene <span class="astra-status done">ready</span></span>
<div class="astra-card-text">Scenes with systems in a fixed order, built-in components, parent/child hierarchy and JSON scene files. @ref ecs_guide "Guide" · @ref scene "API"</div>
</div>
<div class="astra-card">
<span class="astra-card-title">Debug tools <span class="astra-status">planned</span></span>
<div class="astra-card-text">Debug drawing of colliders, contacts and velocities; frame time and render statistics.</div>
</div>
</div>

<h2>Design goals</h2>

- **Layered and testable.** Lower layers never include higher ones. The ECS, physics and math do not
  touch OpenGL, so they are unit-tested without a GPU.
- **Built from scratch where it matters.** The ECS and the physics engine are the engine's own
  work; GLFW, GLAD, GLM and spdlog are used for the platform plumbing.
- **Measured.** Batching, ECS layout and the broad phase are each compared against a naive
  baseline in reproducible benchmarks.
- **Checked.** Tests run under AddressSanitizer, UndefinedBehaviorSanitizer and Valgrind.

<h2>Where to go next</h2>

| If you want to… | Read |
|---|---|
| build the engine and run the examples | @ref getting-started |
| understand how the engine is structured | @ref architecture |
| learn how the main loop, timestep and input work | @ref core_guide |
| draw sprites, use the camera and understand batching | @ref renderer_guide |
| organize a game as entities, components and systems | @ref ecs_guide |
| look up a class or function | [Modules](topics.html) or [Classes](annotated.html) |
