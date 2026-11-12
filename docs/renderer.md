# Renderer {#renderer_guide}

[TOC]

The @ref renderer "renderer" module draws the game: coloured and textured quads, sprite sheet
frames and debug lines, seen through a 2D camera. Its centre is @ref astra2d::Renderer2D, a batch
renderer that turns thousands of quads into a handful of draw calls. The @ref assets "assets"
module loads the textures and shaders it uses, each file only once.

<div class="astra-summary"><b>In short</b>
<ul>
<li>Draw between `begin(camera)` and `end()`. Positions are quad centres in a y-up world.</li>
<li>`Renderer2D` batches quads: one draw call per 10,000 quads or 16 textures, instead of one per sprite.</li>
<li>A texture must stay alive until `end()`; keeping textures in an `AssetCache` makes that automatic.</li>
<li>`CameraFollow` eases the camera after a target inside level bounds; `setPixelPerfect()` keeps pixel art crisp.</li>
<li>Tile layers, aligned text and particle effects are drawn through the same batch, so they cost no extra draw calls of their own.</li>
<li>The `AssetCache` loads each file once and reloads changed files while the game runs.</li>
</ul></div>

## Drawing a frame

A renderer and a camera are usually members of the Application subclass. Members are constructed
after the window, so the OpenGL context already exists when the renderer creates its buffers:

```cpp
class Game : public astra2d::Application {
    astra2d::AssetCache m_assets{"assets"};
    astra2d::SpriteSheet m_sheet{m_assets.texture("textures/sprites.png"), {16, 16}, {1, 1}, {2, 2}};
    astra2d::OrthographicCamera m_camera{360.0F};
    astra2d::Renderer2D m_renderer;

    void onResize(int width, int height) override { m_camera.setViewportSize(width, height); }

    void onRender() override
    {
        astra2d::Renderer2D::clear({0.1F, 0.1F, 0.15F, 1.0F});
        m_renderer.begin(m_camera);
        m_renderer.drawQuad({0, 0}, {64, 64}, {0.2F, 0.6F, 1.0F, 1.0F});   // solid colour
        m_renderer.drawSprite({100, 0}, {32, 32}, m_sheet.frame(3));       // sprite sheet frame
        m_renderer.drawCircle({0, 0}, 50, {1, 1, 0, 1});                   // debug outline
        m_renderer.end();
    }
};
```

Everything between `begin()` and `end()` is drawn in submission order: later quads cover earlier
ones, and alpha blending is on. All sizes and positions are in world units, with the position at the
**centre** of the quad and rotation in radians, counter-clockwise.

| Call | Draws |
|---|---|
| @ref astra2d::Renderer2D::drawQuad "drawQuad()" | a solid-colour or textured rectangle, optionally rotated |
| @ref astra2d::Renderer2D::drawSprite "drawSprite()" | a Sprite (a texture region, e.g. a sprite sheet frame), optionally mirrored |
| @ref astra2d::Renderer2D::draw "draw(Quad)" | any quad, with every option as a designated initializer |
| @ref astra2d::Renderer2D::drawLine "drawLine()", @ref astra2d::Renderer2D::drawRect "drawRect()", @ref astra2d::Renderer2D::drawCircle "drawCircle()" | debug outlines of a given thickness |

<div class="astra-figures">
<div class="astra-figure"><img src="shapes.png" alt="<code>examples/shapes</code>: quads, rotation, alpha blending and debug outlines"/><span><code>examples/shapes</code>: quads, rotation, alpha blending and debug outlines</span></div>
<div class="astra-figure"><img src="sprites.png" alt="<code>examples/sprites</code>: sprite sheet frames and a walk animation"/><span><code>examples/sprites</code>: sprite sheet frames and a walk animation</span></div>
</div>

## Camera and coordinates

@ref astra2d::OrthographicCamera defines which part of the world is visible. The world is
**y-up**. The camera shows `viewHeight / zoom` world units vertically, centred on its position;
the visible width follows the window's aspect ratio, so resizing the window shows more or less of
the world to the sides instead of stretching it.

| Space | Origin | y axis | Units | Used by |
|---|---|---|---|---|
| World | anywhere you choose | up | world units | game objects, physics, Renderer2D |
| Screen | top-left of the framebuffer | down | pixels | Input::mousePosition(), onResize() |

`screenToWorld()` and `worldToScreen()` convert between the two, so picking an object with the
mouse is one call:

```cpp
const glm::vec2 cursor = m_camera.screenToWorld(input().mousePosition());
```

When the whole playing field must stay visible, call `fitArea({width, height})` once instead of
choosing a view height. The camera then picks the view height that fits the area at the window's
aspect ratio, again after every resize, and the other direction shows extra world. A wide window
then has room at the sides, a tall or tiled window above and below, and nothing is cut off:

```cpp
m_camera.fitArea({640.0F, 360.0F});
```

**Pixel art** needs a whole number of screen pixels per texel; otherwise some texels are drawn
one pixel wider than others, and they shimmer while the camera moves. With one texel per world
unit, `setPixelPerfect(viewHeight)` picks the view height closest to `viewHeight` that gives whole
pixels per unit, again after every resize. It also draws from a position rounded to a whole
pixel, so the camera itself can move smoothly (Unity's Pixel Perfect Camera does the same):

```cpp
m_camera.setPixelPerfect(224.0F); // 720 px high: 3 px per texel, 240 units visible
m_camera.pixelsPerUnit();         // 3, e.g. to scale the HUD with the world
```

The camera computes everything on the CPU and has no OpenGL state, so its maths is unit-tested
(`tests/renderer/test_camera.cpp`). A view height of 720 with a 720-pixel-high window maps one world unit
to one pixel; a game with 16-pixel tiles might instead use a view height of 15 and size its
sprites 1×1, so that one world unit is one tile.

### Rotation, parallax and screen space

- `setRotation(radians)` turns the camera counter-clockwise, so the world appears to turn the other
  way. `screenToWorld()` takes the rotation into account, and `visibleMin()`/`visibleMax()` return
  the axis-aligned bounds of the rotated view, which suit culling.
- `withParallax(factor)` returns a copy of the camera whose position is multiplied by `factor`.
  Drawing a background layer through it makes it scroll slower than the world, so it appears
  further away (0 keeps it fixed on screen, 1 moves it with the world). Combined with
  `Renderer2D::drawTiled()`, which repeats a texture across the visible area, a layer is endless:

  ```cpp
  const auto layer = m_camera.withParallax({0.3F, 0.2F});
  m_renderer.begin(layer);
  m_renderer.drawTiled(center, {width, tile.y}, hills, tile, white, {0, stripBottom});
  m_renderer.end();
  ```

- `OrthographicCamera::screenSpace(width, height)` is a camera in which one unit is one pixel with
  the origin in the bottom-left corner, for HUD and text that do not move with the world.

### Following a target

@ref astra2d::CameraFollow is a camera position that eases after a target and stays inside the
level, like Godot's Camera2D with smoothing and limits. Each frame it covers a part of the
remaining distance that depends only on the elapsed time, `1 - e^(-speed·dt)`. Two short frames
therefore move it exactly as far as one long frame, and the camera feels the same at any frame
rate. Then it is clamped so the view never shows anything outside the bounds. Where the level is
smaller than the view, the view starts at the level's left or bottom edge.

```cpp
m_follow.setBounds({0, 0}, m_level.size());
m_follow.snap(player, m_camera);                         // when a level starts, after a respawn
m_follow.update(player + lookAhead, deltaTime, m_camera); // every frame
m_shake.apply(m_camera, m_follow.position());            // or m_camera.setPosition(...)
```

The look-ahead (a little in front of where the player faces) is the game's choice, so it is
added to the target rather than built in.

### Screen shake

@ref astra2d::CameraShake implements *trauma*-based shake: events add trauma in [0, 1], which
decays over time, and the displacement grows with trauma squared, so small hits barely move the
camera while big ones clearly do. The motion follows smooth 1D gradient noise instead of random
jumps, which reads as a shake rather than jitter. It is deterministic for a seed and unit-tested.

```cpp
m_shake.addTrauma(0.6F);                       // on an explosion
m_shake.update(deltaTime);
m_shake.apply(m_camera, playerPosition);       // position + offset, rotation + angle
```

## Why batching

Every OpenGL draw call costs CPU time in the driver, independent of how little it draws. Drawing
each sprite with its own call (set a matrix, bind a texture, draw two triangles) therefore makes
the CPU the bottleneck long before the GPU is busy.

@ref astra2d::Renderer2D instead writes the four corners of every quad, already transformed to
world space, into one vertex array on the CPU. The array is uploaded and drawn with a single
`glDrawElements` when one of these happens:

1. the batch is full (`Renderer2DSettings::maxQuadsPerBatch`, 10 000 quads by default);
2. a quad needs a texture, and all texture slots of the batch are taken;
3. `end()` is called.

@dot
digraph batching {
    rankdir=LR;
    nodesep=0.3;
    node [shape=box, style="rounded,filled", fontname="Helvetica,Arial,sans-serif", fontsize=10,
          color="#2a7fae", fillcolor="#eaf4fa"];
    edge [color="#5a7b91", arrowsize=0.7, fontname="Helvetica,Arial,sans-serif", fontsize=9,
          fontcolor="#5a7b91"];

    draw  [label="drawQuad / drawSprite /\ndrawLine ..."];
    batch [label="QuadBatch (CPU)\n4 vertices per quad\ntexture → slot"];
    flush [label="flush\nupload vertices\nbind textures\n1 × glDrawElements", fillcolor="#2a7fae",
           fontcolor="white"];
    draw -> batch [label="corners, colour, uv"];
    batch -> flush [label="full / slots taken / end()"];
}
@enddot

Each vertex carries its position, colour, texture coordinates and a **texture slot**. A batch can
use up to 16 textures at once (the minimum OpenGL 3.3 guarantees); slot 0 always holds a 1×1 white
texture, so solid-colour quads go through the same shader as textured ones and need no extra draw
call. Quads that share a texture, such as all frames of one sprite sheet, share a slot.

@note GLSL 3.30 only allows indexing an array of samplers with a constant expression. The fragment
shader therefore selects the texture with a `switch` over the slot, in which every case uses a
constant index, instead of the common but technically undefined `uTextures[vSlot]`.

The batching rules contain no OpenGL calls; they live in an internal `QuadBatch` class that is
unit-tested directly (`tests/renderer/test_quad_batch.cpp`): slot assignment, a full batch, all slots taken.

### Measuring it: renderer_stress

`examples/renderer_stress` draws a configurable number of moving, rotating, tinted sprites.
@ref astra2d::Renderer2D::setBatching "setBatching(false)" switches the renderer to the naive
path: one unit quad, transformed by a per-quad model matrix, one draw call per quad. Both paths
produce the same image, so the frame times can be compared directly (evaluation E1):

```bash
./build-release/examples/renderer_stress --sprites 10000           # interactive; B toggles batching
./build-release/examples/renderer_stress --sprites 10000 --frames 600 --naive   # prints a CSV line
scripts/bench_renderer.sh                                          # full E1 run → benchmarks/results/
```

The benchmark renders into an off-screen framebuffer of a fixed 1280×720 pixels and copies it to
the window, so the measured work does not depend on the window size. It reports two times:
`render_ms` (from `begin()` to `end()` plus `glFinish()`, the renderer's own cost including the
GPU work) and `frame_ms` (the whole frame, including the buffer swap and the compositor). The
batched/naive comparison uses `render_ms`.

The renderer counts draw calls and quads in @ref astra2d::RenderStats; the sandbox and
renderer_stress show them in the window title.

### Texture lifetime

Batching has one consequence for the caller: a quad's texture is used when the batch is flushed,
not when the draw function returns. Every texture drawn in a scene must therefore stay alive until
`end()`. OpenGL reuses the name of a deleted texture immediately, so a texture destroyed too early
does not cause an error; the quad silently shows whichever texture gets the name next.

The API rejects the easy ways to get this wrong at compile time: `SpriteSheet`,
`AnimationPlayer::play()` and `drawQuad()` do not accept temporaries
(`drawQuad(p, s, Texture2D::fromFile("a.png"))` does not compile). Keeping textures in an
@ref astra2d::AssetCache, which owns them until it is cleared, avoids the problem altogether.

## Sprites, sprite sheets and animation

A @ref astra2d::Sprite is a texture plus a @ref astra2d::TextureRegion, the rectangle of the
texture to show. @ref astra2d::SpriteSheet splits a texture into a grid of equally sized frames,
numbered row by row from the **top-left** of the image, the order in which sheets are drawn:

```cpp
const astra2d::SpriteSheet sheet(texture, {16, 16});           // 16x16-texel frames, no border
m_renderer.drawSprite(position, {32, 32}, sheet.frame(5));     // 6th frame
```

Margins around the grid and spacing between frames are supported. The grid arithmetic is in
@ref astra2d::SpriteSheetLayout, which does not need a texture, so it is unit-tested.

@note Leave a border around each frame. When a sprite is rotated, scaled by a non-integer factor
or filtered with `TextureFilter::Linear`, the GPU can sample just outside the frame's rectangle and
pick up texels of the neighbouring frame ("bleeding"). The example sheet surrounds every frame with
a 1-texel copy of its own edge ("extrusion", see `scripts/generate_assets.py`) and is loaded with
`SpriteSheet(texture, {16, 16}, {1, 1}, {2, 2})`: margin 1, spacing 2.

@ref astra2d::SpriteAnimation lists frame indices and a frame duration;
@ref astra2d::AnimationPlayer advances through them:

```cpp
const astra2d::SpriteAnimation m_run{.frames = {0, 1, 2, 3}, .frameDuration = 0.1F};
astra2d::AnimationPlayer m_animation;

void onUpdate(float dt) override
{
    m_animation.play(m_run);   // keeps its progress if m_run is already playing
    m_animation.update(dt);
}
// in onRender(): sheet.frame(m_animation.frame())
```

Looping animations wrap around; non-looping ones stop on their last frame and report
`finished()`. Animation advances in `onUpdate()`, because it only affects what is drawn, not the
simulation.

## Tile layers

A level's ground is a grid of sprite sheet frames. A @ref astra2d::TileLayer holds one frame per
cell, laid out like the collision @ref astra2d::TileGrid (cell (0, 0) at the bottom left), and
@ref astra2d::Renderer2D::drawTileLayer "drawTileLayer()" draws only the cells that the camera
given to `begin()` sees. A level hundreds of tiles wide costs only what is on screen. A layer
from a Tiled map comes from @ref astra2d::makeTileLayer():

```cpp
const astra2d::TileLayer ground = astra2d::makeTileLayer(map, *map.tileLayer("ground"), 16.0F);
m_renderer.begin(m_camera);
m_renderer.drawTileLayer(ground, m_tiles);               // the tileset as a SpriteSheet
m_renderer.drawTileLayer(decor, m_tiles, {0.8F, 0.8F, 0.9F, 1}); // tinted, e.g. darker
m_renderer.end();
```

## Particles

@ref astra2d::ParticleEmitter is a fixed pool of short-lived quads for bursts, sparks, dust and
smoke, like Godot's CPUParticles2D. @ref astra2d::ParticleSettings describes them: lifetime,
direction and spread, speed, gravity, drag, the area they start in, and size, colour and spin
from start to end, each with a random variation. They are simulated on the CPU and drawn through
the batch, so a thousand particles add only quads, not draw calls:

```cpp
astra2d::ParticleEmitter m_sparks{{.lifetime = 0.6F, .speed = 220.0F, .damping = 3.0F,
                                   .startSize = 6.0F, .endSize = 1.0F}};

m_sparks.emit(coin, 24);                       // a burst
m_smoke.stream(exhaust, 40.0F, deltaTime);     // 40 per second, at any frame rate
m_sparks.update(deltaTime);
m_sparks.draw(m_renderer);                     // between begin() and end()
```

The pool never grows after construction: when it is full, new particles are not emitted. A dead
particle is swapped with the last one, so removal is O(1). Given a seed, the emitter is
deterministic, and it is unit-tested without OpenGL (`tests/renderer/test_particles.cpp`).

## Render targets and post-processing

@ref astra2d::Framebuffer is an off-screen render target: after `bind()`, drawing goes into its
colour texture instead of the window. Uses:

- rendering at a fixed internal resolution and scaling the result to the window
  (`blitToWindow()`, optionally letterboxed), which renderer_stress uses so its measurements do not
  depend on the window size;
- **post-processing**: draw the scene into a framebuffer, then draw that texture to the window
  through an effect shader.

@ref astra2d::PostProcess draws a texture over the whole render target with one triangle and an
effect, a fragment shader that receives `vUv`, `uTexture`, `uResolution` and `uTime`:

```cpp
m_scene.bind();                          // 1. scene into the framebuffer
drawScene();
Framebuffer::bindDefault(width, height); // 2. back to the window
m_postProcess.apply(m_scene.colorTexture(), m_assets.effect("shaders/crt.frag"), m_time);
drawHud();                               // 3. HUD on top, unaffected by the effect
```

The sandbox's CRT effect (`examples/assets/shaders/crt.frag`) combines colour fringes, scanlines and a
vignette; P toggles it.

<div class="astra-figures">
<div class="astra-figure"><img src="post_processing.png" alt="<code>examples/post_processing</code>: the scene drawn into a framebuffer, then through an effect"/><span><code>examples/post_processing</code>: the scene drawn into a framebuffer, then through an effect</span></div>
<div class="astra-figure"><img src="sandbox.png" alt="The Sandbox: parallax layers, a tiled background and the HUD, with the CRT effect"/><span>The Sandbox: parallax layers, a tiled background and the HUD, with the CRT effect</span></div>
</div>


## Text

Text is drawn from a **font atlas**: @ref astra2d::FontAtlas rasterizes the characters of a
TrueType font once, at one pixel size, into a texture with stb_truetype, and keeps each
character's metrics (offset from the pen, size, advance). Drawing text is then just drawing quads
from that texture, so text is batched together with sprites.

```cpp
const astra2d::Font font = astra2d::Font::fromFile("assets/fonts/PressStart2P-Regular.ttf", 8.0F);
m_renderer.begin(astra2d::OrthographicCamera::screenSpace(width, height));
m_renderer.drawText(font, "Score: 120\nLives: 3", {16, height - 16}, 16.0F, white, 1.5F);
// A TextStyle aligns text and adds a drop shadow:
m_renderer.drawText(font, "PAUSED", center, {.size = 32, .anchor = {0.5F, 0.5F}});
m_renderer.drawText(font, time, {width - 16, height - 16},
                    {.size = 16, .anchor = {1, 1}, .shadowOffset = {2, -2}}); // right-aligned
m_renderer.end();
```

- Text is UTF-8. The atlas contains printable ASCII, Latin-1 and Latin Extended-A, so most
  European languages work (for example č, ć, đ, š, ž, as far as the font has them); other
  characters are drawn as '?'.
- By default the position is the top-left of the first line; `\n` starts a new line,
  `lineSpacing` line heights further down. With a @ref astra2d::TextStyle, `anchor` picks which
  point of the text block is at the position: (0, 1) top-left, (0.5, 0.5) the centre, (1, 0)
  bottom-right. Each line is aligned the same way (left, centred, right). `shadowOffset` draws a
  copy in `shadowColor` behind the text. `FontAtlas::measure()` returns the size of a text
  block.
- Pixel fonts stay sharp when baked at their design size, drawn at whole multiples of it and placed
  on whole pixels with `TextureFilter::Nearest`. Smooth fonts look best baked at the size they are
  drawn at, with `Linear`.
- Layout and measurement run on the CPU and are unit-tested (`tests/renderer/test_font.cpp`).

The example font is *Press Start 2P* by CodeMan38, under the SIL Open Font License
(`examples/assets/fonts/OFL.txt`). Kerning is not applied; it matters little for pixel and monospaced fonts.

<div class="astra-figures single">
<div class="astra-figure"><img src="text.png" alt="<code>examples/text</code>: a baked pixel font, Latin Extended characters, a colour per character"/><span><code>examples/text</code>: a baked pixel font, Latin Extended characters, a colour per character</span></div>
</div>

## Textures

@ref astra2d::Texture2D loads PNG, JPEG, BMP and TGA files with
[stb_image](https://github.com/nothings/stb) (vendored in `external/stb/`) or takes raw RGBA
pixels. Images are uploaded with their top row at texture coordinate v = 1, so they appear upright
in the y-up world without any flipping in user code.

`TextureFilter::Nearest`, the default, keeps pixel art sharp at any scale; `Linear` smooths
photos and scaled-down art.

## OpenGL wrappers

@ref astra2d::Shader, @ref astra2d::Buffer, @ref astra2d::VertexArray and
@ref astra2d::Texture2D each own one OpenGL object. They are move-only (RAII): the object is
deleted exactly once, when its owner is destroyed, and a moved-from wrapper owns nothing. Errors
such as a shader that does not compile throw `std::runtime_error` with the driver's message.
Renderer2D is built from these wrappers; games need them only for custom rendering.

Public headers do not include OpenGL headers: object names are exposed as `std::uint32_t`
(checked against `GLuint` at compile time), so a game can include GLFW or GLAD in any order.

## Asset cache

@ref astra2d::AssetCache loads each texture and shader file once and returns references to the
loaded object on every later request:

```cpp
astra2d::AssetCache assets("assets");
const astra2d::Texture2D& a = assets.texture("textures/sprites.png");
const astra2d::Texture2D& b = assets.texture("./textures/../textures/sprites.png");   // same object
```

Paths are resolved against the root directory and normalized, so different spellings of the same
file share one entry. `effect(path)` loads a post-processing effect from a fragment shader file.

### Hot reloading

Every file the cache loads is watched by a @ref astra2d::FileWatcher, which polls modification
times (portable, and cheap for the handful of files edited during development). `reloadChanged()`
reloads the assets whose files changed **in place**: the objects keep their address, so every
reference handed out before now sees the new version. A reload that fails, such as a shader with
a syntax error, is logged and the old version stays, so the game keeps running while the file is
fixed:

```cpp
// in onUpdate(), outside begin()/end()
if ((m_reloadTimer += dt) > 0.5F) { m_reloadTimer = 0.0F; m_assets.reloadChanged(); }
```

In the sandbox, editing `examples/assets/shaders/crt.frag` changes the picture within half a second.

The storage is a generic @ref astra2d::ResourceCache, which keeps each
resource behind a stable pointer (references survive later insertions) and stores nothing if
loading throws. It has no OpenGL dependency and is unit-tested with plain types.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref core_guide "Core"</span>
<span class="astra-pager-next">Next: @ref ecs_guide "ECS and scenes" →</span>
</div>
