# Example game: Astra Run {#platformer_guide}

[TOC]

`examples/platformer` is the showcase game of Astra2D: a small but complete platformer built only
on the public API in `include/astra2d/`. A stranded astronaut makes their way home to Earth across
four planets: green **Verda**, the inside of the icy **Crystal Moon**, the cloud planet **Nimbus**
and the lava planet **Pyros**. On each one they collect coins, stomp crawlers and bats, dodge spiky
hedgehogs, spikes and lava, ride moving platforms and lifts, and reach the rocket that flies them to
the next planet.

Every planet has its own ground, weather and sky:

| Planet | Ground | Weather | In the sky |
|---|---|---|---|
| Verda | teal turf on violet soil, mushroom forests, rock spires | drifting spores | the Crystal Moon |
| Crystal Moon | ice rock with frost, giant crystals | falling ice glitter | (inside the moon) |
| Nimbus | pink moss on sky rock, one-way clouds, floating islands | floating motes | its own rings, Pyros |
| Pyros | basalt with magma veins, lava | rising embers | Earth |

<div class="astra-figures">
<div class="astra-figure"><img src="platformer_title.png" alt="The title screen with the route home; Verda runs behind it"/><span>The title screen with the route home; Verda runs behind it</span></div>
<div class="astra-figure"><img src="platformer_level2.png" alt="The Crystal Moon, a Tiled map: a bat, and a moving platform over the lava"/><span>The Crystal Moon, a Tiled map: a bat, and a moving platform over the lava</span></div>
<div class="astra-figure"><img src="platformer_level3.png" alt="Nimbus: a jump to a one-way cloud, with Pyros, the next stop, in the sky"/><span>Nimbus: a jump to a one-way cloud, with Pyros, the next stop, in the sky</span></div>
<div class="astra-figure"><img src="platformer_level4.png" alt="Pyros: over the spikes and a spiker, with volcanoes behind and Earth in the sky"/><span>Pyros: over the spikes and a spiker, with volcanoes behind and Earth in the sky</span></div>
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
<li>A complete game with four planets, three kinds of enemies, moving platforms, springs, hazards, checkpoints, sound and a HUD, written only against the public API.</li>
<li>2,119 lines of code, 21 % of the engine's size; its level loading and movement rules are unit-tested.</li>
<li>Shows Scene systems, physics signals, collision groups, kinematic bodies, coyote time and jump buffering, and Tiled maps working together.</li>
</ul></div>

## Game rules

- Three hearts per level. Spikes, lava and touching an enemy from the side cost one, with a
  knock-back and a moment of invulnerability (the astronaut blinks). Falling out of the level costs
  one and returns to the last checkpoint. No hearts left: game over, and the level can be retried.
  A heart pickup gives one back.
- Three enemies: **crawlers** walk along their floor and turn at walls and ledges; **bats** fly
  back and forth and bob up and down; **spikers** walk like crawlers, but their spikes hurt from
  above too. Landing on a crawler or a bat stomps it and bounces the player (higher with jump held).
- **Springs** throw the player about nine tiles up. **Moving platforms** go four tiles to the side
  and back, **lifts** four tiles up and back; both carry the player and are one-way, like planks
  and clouds.
- Radio beacons are the checkpoints: touching one lights it up.
- Coins are counted per level. Touching the rocket ends the level: the astronaut climbs in, the
  rocket lifts off, and a panel shows the next planet. After Pyros the rocket reaches Earth, and
  the victory screen shows all coins and the total time.

## Code structure

The game is split by what each part does, and the parts that do not draw are unit-tested
(`tests/examples/test_platformer.cpp`):

| File | Lines | What |
|---|---|---|
| `level.hpp/.cpp` | 586 | Levels as data: text maps and Tiled maps into tiles, collision grids, hazards and spawn points |
| `controller.hpp/.cpp` | 136 | Movement rules: acceleration, jump, jump cut, coyote time, jump buffering |
| `game.cpp` | 1,588 | The game: entities, systems, physics events, camera, HUD, sound, game states |
| `looks.hpp` | 144 | Each planet: weather, sky colours, nebula, stars, rings, the planet in the sky, parallax layers |
| `main.cpp`, `game.hpp` | 79 | Entry point |

About 2,533 lines of game code (2,119 without comments and blank lines, see @ref evaluation) on
about 10,100 lines of engine code: the engine does the
window, input mapping, fixed timestep, rendering, animation, the ECS, physics, collision events,
level file parsing and audio, and the game only says what happens.

### Entities and systems

Every game object is an entity in a @ref astra2d::Scene with the engine's components (Transform,
SpriteRenderer, Animator, RigidBody, Collider, TilemapCollider, Interpolated) and a few of the
game's own, which are plain data: `Player`, `Enemy` (stompable or not), `Walker`, `Flyer`, `Mover`,
`Coin`, `HeartPickup`, `Spring`, `Goal`, `Checkpoint`, `HazardTile`, `Squashed`, `Effect`. An
enemy is a combination: a crawler is `Enemy` + `Walker`, a spiker the same with a non-stompable
`Enemy`, a bat `Enemy` + `Flyer`.

The gameplay runs as Scene systems in a fixed order:

| Phase | System | Does |
|---|---|---|
| FixedUpdate | physics | the @ref astra2d::PhysicsSystem step; its events run the handlers below |
| FixedUpdate | movers | moving platforms and lifts follow their path |
| FixedUpdate | player control | input to velocity: run, jump, drop through planks, ride platforms, dust on landing |
| FixedUpdate | walkers | crawlers and spikers walk and turn at walls and ledges (two ray casts each) |
| FixedUpdate | flyers | bats fly back and forth, turn at walls, and bob on a sine wave |
| FixedUpdate | dangers | hazard contact, falling out of the level, invulnerability |
| Update | player look | idle / run / jump / fall / hurt animation, facing, blinking |
| Update | effects | stomped enemies, sparkles and dust disappear |
| LateUpdate | animation | the engine's: advances every Animator |

The physics reports what happened through signals, and the game reacts:

- `onCollisionBegin` between the player and an enemy: the contact normal decides between a stomp
  (the player is above, and the enemy is not spiky) and a hit.
- `onTriggerEnter` / `onTriggerExit` with coins, hearts, springs, checkpoints, the goal and hazard
  tiles. Hazards count overlaps, so standing in spikes keeps hurting once the invulnerability runs
  out.

**Kinematic bodies** move only by the velocity the game gives them and push dynamic bodies out of
the way. Bats are kinematic, so they keep their flight path when they touch the player. Moving
platforms are kinematic one-way boxes: each fixed step, the `movers` system sets the velocity that
reaches the next point of a smooth back-and-forth path (`0.5 - 0.5 cos(2πt / T)` of the travel).
The player controller finds the platform under the feet with a ray cast and adds its velocity to
the player's own, so the player rides along instead of sliding off.

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
that, and a unit test checks it. A spring's launch is not cut short when jump is released.

### Camera and drawing

The camera follows the player with exponential smoothing, a little ahead in the facing direction,
and stays inside the level. It snaps to whole screen pixels, and the view height is chosen so a
texel is a whole number of pixels (pixel-perfect scaling), so the 16×16 art never shimmers.

Each frame draws the planet's look (`looks.hpp`): a sky gradient in 24 bands, a nebula tinted
for the planet, stars with a shooting star every few seconds, Nimbus's own rings arcing across
its sky, and the next planet of the route hanging in it (the far things drift only slightly as
the camera moves, so they seem far away). Then up to three parallax layers (Verda's rock spires
and mushroom forest, the Crystal Moon's cave wall and giant crystals, Nimbus's clouds and floating
islands, Pyros's smoking volcanoes), the decoration (darkened, so it reads as background and
never looks like something to touch) and the ground tiles the camera sees, the moving
platforms, the scene's sprites by layer, interpolated between fixed steps, and the HUD in screen space with the vendored pixel font:
one bar along the top (hearts, coins, planet, time) and a panel behind the text of the title, pause,
lift-off, game-over and victory screens. The title and victory screens show the whole route as a row
of planets, and the title shows the astronaut and their rocket. Trauma-based camera shake marks stomps and hits, and
dust puffs mark jumps, landings and springs. The planet's weather is a few dozen small squares that
drift with their own speed and sway and are wrapped into the camera's view, so they fill it
wherever the camera goes without being spawned or removed.

## Levels

Two formats lead to the same `Level` data:

**Text maps** (`examples/assets/levels/level1.txt`) are written in any editor. One character is one
16×16 tile; the file lists the legend. The start of level 1 (the first 48 columns of 6 of its rows):

```text
name: Planet Verda
hint: Run with A/D, jump with SPACE, and reach the rocket
map:
##............................o.....o.o.o.......
##......oooo.................o.o....-----.......
##................fa............................
##...............###............................
##.dP.g.b..a.a.s.###.....e.......f......u.....j.
#############################...################
...
```

The loader picks the drawn tile from the neighbours (grass on top, dirt below, grass edges where
the ground ends), so the map stays readable. Errors name the line and column.

**Tiled maps** (`level2.tmj` to `level4.tmj`) open in the [Tiled](https://www.mapeditor.org) editor
with the tileset `levels/tiles.tsj`. The engine reads them with @ref astra2d::loadTiledMap() and the
game takes from them:

- tile layer `ground`: tiles whose custom property `collision` is `solid` or `oneway` collide, tiles
  with `hazard` hurt; `decor` is drawn behind;
- object layer `objects`: point objects with the class `player`, `coin`, `crawler`, `flyer`,
  `spiker`, `spring`, `heart`, `platform`, `lift`, `checkpoint` or `goal`;
- map properties `name` and `hint`.

@ref astra2d::makeTileGrid turns a layer into collision tiles for a TilemapCollider, which merges
them into few boxes without internal edges (see @ref physics_guide).

## Assets

All art and sound is original, made by `scripts/generate_platformer_assets.py` with only the Python
standard library (CC0):

- `textures/astronaut.png`: the astronaut in a white suit with a life-support pack (idle, run,
  jump, fall, hurt) and the rocket with its flickering exhaust, in 24×24 frames whose art stands
  on a tile's bottom edge, so they are drawn centred on a tile like the 16×16 sprites;
  `textures/platformer.png`: crawlers (one-eyed slimes), bats, crystal-backed spikers, coins,
  hearts, beacons, springs, the hover platform, sparkles and dust;
  `textures/tiles.png`: 37 tiles (turf and soil, stone, ice, basalt, moss rock, girders, clouds,
  spikes, lava and decoration for each planet: coral bushes, glowing mushrooms and a dish on
  Verda, crystals and geodes on the Crystal Moon, puffballs, cloud shrubs and bell flowers on
  Nimbus, charred trees, lava vents, magma boulders and fire pits on Pyros). All use 1-texel extruded borders, so neighbouring frames never bleed in.
- `textures/nebula.png`, `stars.png`, `cave.png`: skies that tile in both directions;
  `spires.png`, `flora.png`, `crystals.png`, `clouds.png`, `islands.png` and `volcanoes.png`:
  parallax strips that tile horizontally (the mountains behind Pyros's volcanoes are shared with
  the sandbox); `rings.png`: the arc of Nimbus's rings.
- `textures/planets.png`: the five planets of the route (Verda, the Crystal Moon, Nimbus, Pyros and
  Earth), 32×32 each.
- `sounds/*.wav`: square, triangle and noise synthesis for the effects, and an 8-bar loop at 120 bpm
  for the music.
- `levels/level2.tmj` to `level4.tmj` and `tiles.tsj`: generated once from ASCII art and editable
  in Tiled afterwards.

The font is Press Start 2P (SIL Open Font License), shared with the other examples.

## Engine features used

Application and fixed timestep with interpolation; ActionMap with keyboard and gamepad; AssetCache;
Renderer2D with sprites, tiled backgrounds and text; SpriteSheet and animation; camera with
parallax and shake; Scene, Registry, views and systems; PhysicsSystem with dynamic, kinematic and
static bodies, triggers, collision events, collision filtering, one-way tile and body platforms,
ground detection and ray casts; TilemapCollider; Tiled map loading; AudioEngine.

<div class="astra-pager">
<span class="astra-pager-prev">← @ref debug_guide "Debug tools"</span>
<span class="astra-pager-next">Next: @ref evaluation "Evaluation" →</span>
</div>
