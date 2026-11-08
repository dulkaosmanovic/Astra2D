# Example game: Astra Run {#platformer_guide}

[TOC]

`examples/platformer` is the showcase game of Astra2D: a small but complete platformer built only
on the public API in `include/astra2d/`. A stranded astronaut runs and jumps through three levels,
collects coins, stomps crawlers, avoids spikes and lava, and reaches the flag.

<div class="astra-figures">
<div class="astra-figure"><img src="platformer_title.png" alt="The title screen; level 1 runs behind it"/><span>The title screen; level 1 runs behind it</span></div>
<div class="astra-figure"><img src="platformer_level2.png" alt="Level 2, a Tiled map: a jump over lava, coins and a crawler"/><span>Level 2, a Tiled map: a jump over lava, coins and a crawler</span></div>
</div>

```bash
./build/examples/platformer        # from the title screen
./build/examples/platformer 2      # start at level 2, e.g. while editing it
```

| Keyboard | Gamepad | Action |
|---|---|---|
| A / D, arrows | left stick, d-pad | run |
| Space, W, Up, Z | A | jump; hold for a higher jump |
| S or Down + jump | d-pad down + A | drop through a plank |
| Enter | A, Start | start, continue |
| P | Start | pause |
| R | Back | restart the level |
| M | | mute |
| Escape | | quit |
| F3 | | physics debug view (see @ref debug_guide) |
| F2 | | FPS, frame times and render statistics in the log |

<div class="astra-summary"><b>In short</b>
<ul>
<li>A complete game with three levels, enemies, hazards, checkpoints, sound and a HUD, written only against the public API.</li>
<li>1,508 lines of code, 15 % of the engine's size; its level loading and movement rules are unit-tested.</li>
<li>Shows Scene systems, physics signals, collision groups, coyote time and jump buffering, and Tiled maps working together.</li>
</ul></div>

## Game rules

- Three hearts per level. Spikes, lava and touching a crawler from the side cost one, with a
  knock-back and a moment of invulnerability (the astronaut blinks). Falling out of the level costs
  one and returns to the last checkpoint. No hearts left: game over, and the level can be retried.
- Landing on a crawler from above stomps it and bounces the player (higher with jump held).
- Coins are counted per level; the flag ends the level. After the third level the victory screen
  shows all coins and the total time.

## Code structure

The game is split by what each part does, and the parts that do not draw are unit-tested
(`tests/test_platformer.cpp`):

| File | Lines | What |
|---|---|---|
| `level.hpp/.cpp` | 514 | Levels as data: text maps and Tiled maps into tiles, collision grids, hazards and spawn points |
| `controller.hpp/.cpp` | 136 | Movement rules: acceleration, jump, jump cut, coyote time, jump buffering |
| `game.cpp` | 1,098 | The game: entities, systems, physics events, camera, HUD, sound, game states |
| `main.cpp`, `game.hpp` | 77 | Entry point |

About 1,800 lines of game code (1,508 without comments and blank lines, see @ref evaluation) on
about 10,200 lines of engine code: the engine does the
window, input mapping, fixed timestep, rendering, animation, the ECS, physics, collision events,
level file parsing and audio, and the game only says what happens.

### Entities and systems

Every game object is an entity in a @ref astra2d::Scene with the engine's components (Transform,
SpriteRenderer, Animator, RigidBody, Collider, TilemapCollider, Interpolated) and a few of the
game's own, which are plain data: `Player`, `Enemy`, `Coin`, `Goal`, `Checkpoint`, `HazardTile`,
`Squashed`, `Effect`.

The gameplay runs as Scene systems in a fixed order:

| Phase | System | Does |
|---|---|---|
| FixedUpdate | physics | the @ref astra2d::PhysicsSystem step; its events run the handlers below |
| FixedUpdate | player control | input to velocity: run, jump, drop through planks |
| FixedUpdate | enemy patrol | crawlers walk and turn at walls and ledges (two ray casts each) |
| FixedUpdate | dangers | hazard contact, falling out of the level, invulnerability |
| Update | player look | idle / run / jump / fall animation, facing, blinking |
| Update | effects | squashed crawlers and sparkles disappear |
| LateUpdate | animation | the engine's: advances every Animator |

The physics reports what happened through signals, and the game reacts:

- `onCollisionBegin` between the player and a crawler: the contact normal decides between a stomp
  (the player is above) and a hit.
- `onTriggerEnter` / `onTriggerExit` with coins, checkpoints, the goal and hazard tiles. Hazards
  count overlaps, so standing in spikes keeps hurting once the invulnerability runs out.

**Collision groups** decide what touches what: world, planks, player, enemies, pickups, hazards.
The planks are a second @ref astra2d::TilemapCollider in their own group, so the player drops
through them by leaving that group out of its mask for a moment; while invulnerable, the player
leaves out the enemy group and walks through crawlers.

### Movement: coyote time and jump buffering

The player's RigidBody has a fixed rotation and no friction; the controller sets its velocity
directly, which gives exact, tunable movement:

- accelerates towards the run speed, with more grip on the ground than in the air;
- a jump sets the upward speed; releasing the button while rising cuts it (short hops);
- falling uses 1.7 times the gravity, which feels snappier, and is capped at a terminal speed;
- **coyote time** (0.1 s): a jump still works shortly after running off a ledge, so a press one
  frame late is not lost;
- **jump buffering** (0.12 s): a press shortly before landing jumps on landing.

Both grace times are in `JumpController`, which is pure logic and tested step by step. A press is
also kept for the next fixed step if a frame had none (at a high frame rate some frames do not run
the fixed update), so no press is ever dropped.

The jump reaches about four tiles high and clears a three-tile gap; the levels are designed within
that, and a unit test checks it.

### Camera and drawing

The camera follows the player with exponential smoothing, a little ahead in the facing direction,
and stays inside the level. It snaps to whole screen pixels, and the view height is chosen so a
texel is a whole number of pixels (pixel-perfect scaling), so the 16×16 art never shimmers.

Each frame draws parallax mountains and hills, the decoration and ground tiles the camera sees, the
scene's sprites by layer, interpolated between fixed steps, and the HUD in screen space with
the vendored pixel font. Trauma-based camera shake marks stomps and hits.

## Levels

Two formats lead to the same `Level` data:

**Text maps** (`examples/assets/levels/level1.txt`) are written in any editor. One character is one
16×16 tile; the file lists the legend. The start of level 1 (the first 44 columns of 5 of its rows):

```text
name: Green Hills
hint: Run with A/D or the arrows, jump with SPACE
map:
##........................o.o.o.............
##.................o......-----...........oo
##......oooo................................
##..P.b.......s...###....f.........e...r....
#####################...##################^^
...
```

The loader picks the drawn tile from the neighbours (grass on top, dirt below, grass edges where
the ground ends), so the map stays readable. Errors name the line and column.

**Tiled maps** (`level2.tmj`, `level3.tmj`) open in the [Tiled](https://www.mapeditor.org) editor
with the tileset `levels/tiles.tsj`. The engine reads them with @ref astra2d::loadTiledMap() and the
game takes from them:

- tile layer `ground`: tiles whose custom property `collision` is `solid` or `oneway` collide, tiles
  with `hazard` hurt; `decor` is drawn behind;
- object layer `objects`: point objects with the class `player`, `coin`, `enemy`, `checkpoint` or
  `goal`;
- map properties `name` and `hint`.

@ref astra2d::makeTileGrid turns a layer into collision tiles for a TilemapCollider, which merges
them into few boxes without internal edges (see @ref physics_guide).

## Assets

All art and sound is original, made by `scripts/generate_platformer_assets.py` with only the Python
standard library (CC0):

- `textures/platformer.png`: the astronaut (idle, run, jump, fall), the crawler, coins, hearts,
  flags and sparkles; `textures/tiles.png`: 16 tiles. Both use 1-texel extruded borders, so
  neighbouring frames never bleed in.
- `sounds/*.wav`: square, triangle and noise synthesis for the effects, and an 8-bar loop at 120 bpm
  for the music.
- `levels/level2.tmj`, `level3.tmj` and `tiles.tsj`: generated once from ASCII art and editable in
  Tiled afterwards.

The font is Press Start 2P (SIL Open Font License), shared with the other examples.

## Engine features used

Application and fixed timestep with interpolation; ActionMap with keyboard and gamepad; AssetCache;
Renderer2D with sprites, tiled backgrounds and text; SpriteSheet and animation; camera with
parallax and shake; Scene, Registry, views and systems; PhysicsSystem with dynamic and static
bodies, triggers, collision events, collision filtering, one-way tile platforms, ground detection
and ray casts; TilemapCollider; Tiled map loading; AudioEngine.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref debug_guide "Debug tools"</span>
<span class="astra-pager-next">Next: @ref evaluation "Evaluation" →</span>
</div>
