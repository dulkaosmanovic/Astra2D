#!/usr/bin/env python3
"""Generates the assets of examples/platformer: sprite sheets, sounds and the Tiled levels.

Everything is original and drawn or synthesized by this script (CC0, no third-party assets), with
only the Python standard library:

    python3 scripts/generate_platformer_assets.py

Writes into examples/assets/:
    textures/platformer.png   characters and objects, 8x5 frames of 16x16
    textures/tiles.png        the tileset, 8x4 tiles of 16x16
    textures/clouds.png       parallax backgrounds that tile horizontally (cave.png and
    textures/cave.png         stars.png also vertically); drawn light so the game can tint them
    textures/volcanoes.png    per level
    textures/stars.png
    textures/planets.png      the five planets of the route, 5 frames of 32x32
    sounds/*.wav              sound effects and the music loop (16-bit mono, 22050 Hz)
    levels/tiles.tsj          the tileset for Tiled, with collision properties
    levels/level2.tmj         Tiled maps, made from the ASCII art below; they open in Tiled and
    levels/level3.tmj         can be edited there (running this script again overwrites them)
    levels/level4.tmj

levels/level1.txt is not generated: it is the hand-written text map (format in that file).

Both sheets use the layout of sprites.png: every frame has a 1-texel extruded border, so cells are
18x18 texels (margin 1, spacing 2 between frames), and nothing bleeds in when scaled.

platformer.png frames (row-major from the top-left):
     0  1   player idle            2-5  player run          6 jump      7 fall
     8  9   crawler walk           10   crawler squashed    11-14 coin  15 heart
    16 17   rocket, lower half     18 19 beacon off/on      20 empty heart  21-23 sparkle
    24 25   bat flying             26   bat stunned         27 28 spiker walk   29 player hurt
    30 31   spring, pressed        32-34 dust puff          35-37 moving platform left/middle/right
    38      rocket, upper half     39   rocket exhaust
tiles.png tiles:
     0 grass   1 dirt   2 grass left edge   3 grass right edge   4 stone top   5 stone
     6 one-way plank   7 spikes   8 bush   9 flower   10 rock   11 sign   12 crate   13 brick
    14 grass single column   15 lava   16 crystals   17 mushroom   18 grass tuft   19 torch
    20 one-way cloud   21 pine   22 ice top   23 ice   24 basalt top   25 basalt   26 alien plant
"""

import json
import math
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_assets import write_png  # noqa: E402  (same directory)

FRAME = 16
PADDING = 1
CELL = FRAME + 2 * PADDING
ASSETS = Path(__file__).resolve().parent.parent / "examples" / "assets"

TRANSPARENT = (0, 0, 0, 0)
OUTLINE = (24, 20, 37, 255)


def rgb(r, g, b):
    return (r, g, b, 255)


# ---------------------------------------------------------------------------------------------
# Drawing


class Sheet:
    """Frames of 16x16 texels in a grid, drawn with y down inside each frame."""

    def __init__(self, columns, rows):
        self.columns, self.rows = columns, rows
        self.pixels = [[TRANSPARENT] * (columns * FRAME) for _ in range(rows * FRAME)]
        self.ox = self.oy = 0

    def frame(self, index):
        self.ox = (index % self.columns) * FRAME
        self.oy = (index // self.columns) * FRAME
        return self

    def put(self, x, y, color):
        x, y = int(x), int(y)
        if 0 <= x < FRAME and 0 <= y < FRAME:
            self.pixels[self.oy + y][self.ox + x] = color

    def get(self, x, y):
        if 0 <= x < FRAME and 0 <= y < FRAME:
            return self.pixels[self.oy + y][self.ox + x]
        return TRANSPARENT

    def rect(self, x0, y0, x1, y1, color):
        """Inclusive rectangle."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, color)

    def ellipse(self, cx, cy, rx, ry, color):
        for y in range(FRAME):
            for x in range(FRAME):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.put(x, y, color)

    def outline(self, color=OUTLINE):
        """Surrounds the opaque texels of the current frame with a one-texel outline."""
        edge = []
        for y in range(FRAME):
            for x in range(FRAME):
                if self.get(x, y)[3] == 0 and any(
                        self.get(x + dx, y + dy)[3] != 0 and self.get(x + dx, y + dy) != color
                        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    edge.append((x, y))
        for x, y in edge:
            self.put(x, y, color)

    def mask(self, rows, x0, y0, palette):
        """Draws a small picture given as strings; each character is a palette key, ' ' or '.'
        transparent."""
        for dy, row in enumerate(rows):
            for dx, c in enumerate(row):
                if c in palette:
                    self.put(x0 + dx, y0 + dy, palette[c])

    def extruded(self):
        """Every frame in its own cell with a PADDING-texel border of repeated edge texels."""
        out = [[TRANSPARENT] * (CELL * self.columns) for _ in range(CELL * self.rows)]
        for row in range(self.rows):
            for column in range(self.columns):
                for dy in range(-PADDING, FRAME + PADDING):
                    for dx in range(-PADDING, FRAME + PADDING):
                        sx = column * FRAME + min(max(dx, 0), FRAME - 1)
                        sy = row * FRAME + min(max(dy, 0), FRAME - 1)
                        out[row * CELL + PADDING + dy][column * CELL + PADDING + dx] = \
                            self.pixels[sy][sx]
        return out


# --- Characters and objects ------------------------------------------------------------------

HELMET = rgb(232, 236, 244)
HELMET_SHADE = rgb(168, 178, 202)
HELMET_LIGHT = rgb(255, 255, 255)
VISOR = rgb(64, 176, 236)
VISOR_DARK = rgb(36, 108, 176)
VISOR_LIGHT = rgb(186, 242, 255)
VISOR_HURT = rgb(236, 84, 92)
SUIT = rgb(240, 128, 52)
SUIT_LIGHT = rgb(255, 174, 96)
SUIT_DARK = rgb(188, 82, 38)
BOOT = rgb(70, 64, 92)
PACK = rgb(146, 152, 176)
PACK_DARK = rgb(98, 104, 132)
LAMP = rgb(120, 236, 140)


def draw_player(s, legs, bob=0, arm_up=False, visor=VISOR):
    """The astronaut, facing right. legs: two (x, top, bottom) columns of 2-texel-wide legs."""
    for x, top, bottom in legs:
        s.rect(x, top, x + 1, bottom - 1, SUIT_DARK)
        s.rect(x, bottom, x + 1, bottom, BOOT)
    s.rect(2, 8 + bob, 4, 12 + bob, PACK)                  # backpack
    s.rect(2, 8 + bob, 2, 12 + bob, PACK_DARK)
    s.rect(3, 12 + bob, 4, 12 + bob, PACK_DARK)
    s.rect(5, 9 + bob, 10, 12 + bob, SUIT)                 # body
    s.rect(5, 9 + bob, 10, 9 + bob, SUIT_LIGHT)
    s.rect(5, 12 + bob, 10, 12 + bob, SUIT_DARK)
    s.put(10, 10 + bob, LAMP)                              # chest light
    s.ellipse(8.0, 6.0 + bob, 4.5, 4.0, HELMET)            # helmet
    s.rect(4, 7 + bob, 5, 8 + bob, HELMET_SHADE)
    s.put(6, 3 + bob, HELMET_LIGHT)
    s.put(5, 4 + bob, HELMET_LIGHT)
    s.rect(9, 4 + bob, 12, 7 + bob, visor)                 # visor, facing right
    s.rect(9, 7 + bob, 12, 7 + bob, VISOR_DARK if visor == VISOR else SUIT_DARK)
    s.put(10, 4 + bob, VISOR_LIGHT)
    s.put(11, 5 + bob, VISOR_LIGHT)
    if arm_up:
        s.rect(9, 9 + bob, 11, 9 + bob, SUIT_DARK)
        s.put(12, 8 + bob, HELMET)                         # glove
    else:
        s.rect(7, 10 + bob, 8, 11 + bob, SUIT_DARK)
        s.put(8, 12 + bob, HELMET)
    s.outline()


SHELL = rgb(148, 78, 200)
SHELL_LIGHT = rgb(204, 146, 240)
SHELL_DARK = rgb(98, 50, 140)
EYE = rgb(250, 250, 245)
PUPIL = OUTLINE


def draw_crawler(s, step):
    for i, x in enumerate((4, 8, 11)):  # little legs, alternating
        s.rect(x + (step if i % 2 == 0 else -step), 14, x + (step if i % 2 == 0 else -step), 15,
               OUTLINE)
    s.ellipse(8.0, 10.5, 6.6, 4.6, SHELL)
    for x in (4, 7):  # shell segments
        s.rect(x, 8, x, 12, SHELL_DARK)
    s.rect(3, 13, 13, 13, SHELL_DARK)
    s.put(5, 8, SHELL_LIGHT)
    s.put(6, 7, SHELL_LIGHT)
    s.put(5, 7, SHELL_LIGHT)
    s.put(12, 5, SHELL_DARK)  # feeler
    s.put(13, 4, SHELL_LIGHT)
    for ex in (9, 12):  # eyes looking right
        s.rect(ex, 8, ex + 1, 9, EYE)
        s.put(ex + 1, 9, PUPIL)
    s.outline()


BAT = rgb(96, 84, 150)
BAT_DARK = rgb(62, 50, 104)
BAT_LIGHT = rgb(150, 136, 204)
BAT_EYE = rgb(255, 214, 80)
WING_UP = ["#....", "##...", "###..", "####.", ".####"]


def draw_bat(s, wings_up, stunned=False):
    """A bat seen from the front, so it needs no flipping."""
    wing = WING_UP if wings_up else WING_UP[::-1]
    y = 3 if wings_up else 8
    s.mask(wing, 1, y, {"#": BAT_DARK})
    s.mask([row[::-1] for row in wing], 10, y, {"#": BAT_DARK})
    s.ellipse(8.0, 8.5, 3.5, 3.2, BAT)
    s.put(6, 5, BAT)  # ears
    s.put(10, 5, BAT)
    s.put(7, 6, BAT_LIGHT)
    if stunned:
        s.put(6, 8, PUPIL)
        s.put(10, 8, PUPIL)
    else:
        s.put(6, 8, BAT_EYE)
        s.put(10, 8, BAT_EYE)
        s.put(7, 10, EYE)  # fangs
        s.put(9, 10, EYE)
    s.outline()


SPIKER = rgb(204, 120, 64)
SPIKER_DARK = rgb(146, 78, 44)
SPIKER_LIGHT = rgb(240, 186, 128)
SPIKE = rgb(236, 232, 220)
SPIKE_DARK = rgb(160, 150, 146)


def draw_spiker(s, step):
    """A hedgehog with a back full of spikes, facing right: it cannot be stomped."""
    for i, x in enumerate((5, 10)):
        dx = step if i == 0 else -step
        s.rect(x + dx, 14, x + dx + 1, 15, OUTLINE)
    for i, x in enumerate(range(2, 12, 2)):  # spikes, alternating heights
        top = 3 if i % 2 else 4
        s.rect(x, top + 1, x, 8, SPIKE_DARK)
        s.put(x, top, SPIKE)
        s.rect(x + 1, top + 2, x + 1, 8, SPIKE_DARK)
    s.ellipse(7.5, 10.5, 6.0, 4.0, SPIKER)
    s.rect(3, 13, 11, 13, SPIKER_DARK)
    s.rect(11, 10, 13, 12, SPIKER_LIGHT)  # snout
    s.put(14, 11, OUTLINE)
    s.put(11, 9, EYE)
    s.put(12, 9, PUPIL)
    s.outline()


STEEL = rgb(122, 140, 172)
STEEL_LIGHT = rgb(190, 208, 232)
STEEL_DARK = rgb(70, 82, 112)
SIGNAL = rgb(250, 200, 70)
SPRING_RED = rgb(226, 72, 72)
SPRING_LIGHT = rgb(255, 150, 140)


def draw_spring(s, pressed):
    top = 11 if pressed else 7
    s.rect(3, 14, 12, 15, STEEL_DARK)  # base
    for y in range(top + 2, 14):  # coil
        s.rect(5, y, 10, y, STEEL_LIGHT if y % 2 else STEEL)
    s.rect(2, top, 13, top + 1, SPRING_RED)  # pad
    s.rect(2, top, 13, top, SPRING_LIGHT)
    s.outline()


DUST = (232, 226, 210, 230)
DUST_FADED = (232, 226, 210, 130)


def draw_dust(s, step):
    if step == 0:
        s.ellipse(5.0, 13.0, 1.8, 1.5, DUST)
        s.ellipse(11.0, 13.0, 1.8, 1.5, DUST)
    elif step == 1:
        s.ellipse(3.5, 12.0, 2.6, 2.2, DUST)
        s.ellipse(12.5, 12.0, 2.6, 2.2, DUST)
        s.ellipse(8.0, 13.0, 2.0, 1.6, DUST)
    else:
        for x, y in ((1, 10), (3, 9), (13, 9), (15, 10), (6, 11), (10, 11), (2, 12), (14, 12)):
            s.put(x, y, DUST_FADED)


def draw_platform(s, left, right):
    """One 16 texel piece of a moving platform; the art is the top 7 rows, like a plank."""
    x0, x1 = (1 if left else 0), (14 if right else 15)
    s.rect(x0, 0, x1, 6, STEEL)
    s.rect(x0, 0, x1, 0, STEEL_LIGHT)
    s.rect(x0, 5, x1, 6, STEEL_DARK)
    for x in range(x0, x1 + 1):  # warning stripes along the side
        if (x // 2) % 2 == 0:
            s.put(x, 3, SIGNAL)
    for x in ((x0 + 1,) if left else ()) + ((x1 - 1,) if right else ()):
        s.put(x, 2, STEEL_LIGHT)  # bolts
        s.put(x, 4, STEEL_DARK)
    s.outline()


def draw_crawler_squashed(s):
    s.ellipse(8.0, 14.0, 7.0, 2.0, SHELL)
    s.rect(2, 15, 14, 15, SHELL_DARK)
    for ex in (8, 11):
        s.put(ex, 13, PUPIL)
        s.put(ex + 1, 13, PUPIL)
    s.outline()


COIN = rgb(247, 196, 64)
COIN_DARK = rgb(196, 128, 36)
COIN_LIGHT = rgb(255, 240, 170)


def draw_coin(s, angle):
    rx = max(abs(math.cos(angle)) * 5.5, 1.5)
    s.ellipse(8.0, 8.0, rx, 6.0, COIN_DARK)
    if rx > 2.5:
        s.ellipse(8.0, 8.0, rx - 1.0, 5.0, COIN)
        s.put(7, 5, COIN_LIGHT)
        s.put(7, 6, COIN_LIGHT)
    s.outline()


HEART = [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."]


def draw_heart(s, full):
    fill = rgb(232, 60, 84) if full else rgb(64, 52, 76)
    s.mask(HEART, 4, 5, {"#": fill})
    if full:
        s.put(5, 6, rgb(255, 170, 180))
    s.outline()


HULL = rgb(232, 236, 244)
HULL_SHADE = rgb(168, 178, 202)
HULL_RED = rgb(226, 72, 72)
HULL_RED_DARK = rgb(160, 44, 56)


def draw_rocket_bottom(s, light_on):
    """The lower half of the parked rocket: body, fins, nozzle and landing legs."""
    for side in (0, 1):  # fins and legs
        x0 = 1 if side == 0 else 12
        for y in range(6, 13):
            reach = (y - 6) // 2
            if side == 0:
                s.rect(max(x0, 3 - reach), y, 3, y, HULL_RED)
            else:
                s.rect(12, y, min(14, 12 + reach), y, HULL_RED_DARK)
        s.rect(2 if side == 0 else 13, 13, 2 if side == 0 else 13, 15, STEEL_DARK)
    s.rect(4, 0, 11, 11, HULL)
    s.rect(4, 0, 4, 11, rgb(255, 255, 255))
    s.rect(10, 0, 11, 11, HULL_SHADE)
    s.rect(4, 3, 11, 4, HULL_RED)
    s.rect(10, 3, 11, 4, HULL_RED_DARK)
    s.put(7, 8, SIGNAL if light_on else STEEL_DARK)
    s.put(8, 8, SIGNAL if light_on else STEEL_DARK)
    s.rect(5, 12, 10, 14, STEEL_DARK)
    s.rect(6, 12, 9, 12, STEEL)
    s.outline()


def draw_rocket_top(s):
    """The upper half: nose cone and window. Drawn one frame above the bottom half."""
    for y in range(1, 16):
        half = min(4, 1 + (y - 1) // 2)
        s.rect(8 - half, y, 7 + half, y, HULL)
        s.put(8 - half, y, rgb(255, 255, 255))
        s.rect(max(8 - half, 6 + half), y, 7 + half, y, HULL_SHADE)
    s.rect(6, 1, 9, 4, HULL_RED)
    s.rect(8, 1, 9, 4, HULL_RED_DARK)
    s.ellipse(8.0, 10.5, 2.8, 2.8, STEEL)
    s.ellipse(8.0, 10.5, 1.9, 1.9, VISOR)
    s.put(7, 9, VISOR_LIGHT)
    s.outline()


def draw_exhaust(s):
    s.ellipse(8.0, 5.0, 3.2, 5.5, rgb(240, 110, 40))
    s.ellipse(8.0, 3.5, 1.6, 3.2, rgb(255, 222, 110))
    s.rect(6, 0, 9, 0, rgb(255, 250, 220))


def draw_beacon(s, on):
    """A radio beacon: the checkpoint. Its light turns cyan when it is reached."""
    s.rect(7, 6, 8, 13, STEEL_DARK)
    s.rect(7, 6, 7, 13, STEEL)
    s.rect(4, 14, 11, 15, STEEL_DARK)
    s.rect(4, 14, 11, 14, STEEL)
    light = rgb(92, 220, 236) if on else rgb(120, 124, 140)
    s.ellipse(8.0, 4.0, 2.6, 2.6, light)
    s.put(7, 3, rgb(220, 255, 255) if on else rgb(170, 174, 186))
    s.outline()
    if on:  # glow
        for x, y in ((3, 1), (12, 1), (2, 5), (13, 5)):
            s.put(x, y, (150, 240, 255, 180))


def draw_sparkle(s, size, color):
    c = 7.5
    for i in range(-size, size + 1):
        s.put(c + i, c, color)
        s.put(c, c + i, color)
    if size >= 2:
        for d in (-1, 1):
            s.put(c + d, c + d, color)
            s.put(c + d, c - d, color)


def make_characters():
    s = Sheet(8, 5)
    # Player: idle (with a breathing bob), run cycle, jump, fall.
    draw_player(s.frame(0), [(5, 13, 15), (9, 13, 15)])
    draw_player(s.frame(1), [(5, 13, 15), (9, 13, 15)], bob=1)
    draw_player(s.frame(2), [(4, 13, 14), (10, 13, 15)])
    draw_player(s.frame(3), [(6, 12, 14), (8, 12, 15)], bob=-1)
    draw_player(s.frame(4), [(5, 13, 15), (9, 13, 14)])
    draw_player(s.frame(5), [(7, 12, 15), (8, 12, 14)], bob=-1)
    draw_player(s.frame(6), [(5, 12, 13), (9, 12, 14)], bob=-1, arm_up=True)
    draw_player(s.frame(7), [(4, 13, 15), (10, 13, 15)], arm_up=True)
    # Crawler, coin, heart.
    draw_crawler(s.frame(8), 0)
    draw_crawler(s.frame(9), 1)
    draw_crawler_squashed(s.frame(10))
    for i in range(4):
        draw_coin(s.frame(11 + i), i * math.pi / 4)
    draw_heart(s.frame(15), True)
    # Rocket (the goal), beacons (checkpoints), empty heart, sparkle.
    draw_rocket_bottom(s.frame(16), True)
    draw_rocket_bottom(s.frame(17), False)
    draw_beacon(s.frame(18), False)
    draw_beacon(s.frame(19), True)
    draw_heart(s.frame(20), False)
    draw_sparkle(s.frame(21), 1, rgb(255, 250, 200))
    draw_sparkle(s.frame(22), 3, rgb(255, 240, 150))
    draw_sparkle(s.frame(23), 2, rgb(255, 210, 90))
    # Bat, spiker, hurt player, spring, dust, moving platform.
    draw_bat(s.frame(24), True)
    draw_bat(s.frame(25), False)
    draw_bat(s.frame(26), False, stunned=True)
    draw_spiker(s.frame(27), 0)
    draw_spiker(s.frame(28), 1)
    draw_player(s.frame(29), [(4, 12, 14), (10, 12, 14)], arm_up=True, visor=VISOR_HURT)
    draw_spring(s.frame(30), False)
    draw_spring(s.frame(31), True)
    for i in range(3):
        draw_dust(s.frame(32 + i), i)
    draw_platform(s.frame(35), True, False)
    draw_platform(s.frame(36), False, False)
    draw_platform(s.frame(37), False, True)
    draw_rocket_top(s.frame(38))
    draw_exhaust(s.frame(39))
    return s


# --- Tiles -----------------------------------------------------------------------------------

DIRT = rgb(126, 84, 58)
DIRT_DARK = rgb(98, 64, 46)
DIRT_LIGHT = rgb(152, 106, 72)
GRASS = rgb(92, 188, 92)
GRASS_LIGHT = rgb(156, 226, 122)
GRASS_DARK = rgb(52, 134, 72)
STONE = rgb(110, 116, 142)
STONE_LIGHT = rgb(152, 158, 184)
STONE_DARK = rgb(78, 82, 106)
WOOD = rgb(178, 120, 70)
WOOD_DARK = rgb(122, 78, 46)
WOOD_LIGHT = rgb(214, 160, 100)


def dirt(s, seed):
    rnd = random.Random(seed)
    s.rect(0, 0, 15, 15, DIRT)
    for _ in range(10):
        x, y = rnd.randrange(16), rnd.randrange(16)
        s.put(x, y, DIRT_DARK)
        s.put(x + 1, y, DIRT_DARK)
    for _ in range(6):
        s.put(rnd.randrange(16), rnd.randrange(16), DIRT_LIGHT)


def grass_top(s, left_edge, right_edge):
    dirt(s, 1)
    for x in range(16):
        depth = 3 + (1 if x % 3 == 1 else 0) + (1 if x % 7 == 3 else 0)
        s.rect(x, 0, x, depth - 1, GRASS)
        s.put(x, depth - 1, GRASS_DARK)
    s.rect(0, 0, 15, 0, GRASS_LIGHT)
    for x in (2, 6, 11, 14):
        s.put(x, 1, GRASS_LIGHT)
    if left_edge:
        s.rect(0, 0, 0, 6, GRASS_DARK)
        s.rect(1, 0, 1, 4, GRASS)
    if right_edge:
        s.rect(15, 0, 15, 6, GRASS_DARK)
        s.rect(14, 0, 14, 4, GRASS)


def stone(s, top):
    s.rect(0, 0, 15, 15, STONE)
    for y in (0, 8):  # brick courses
        s.rect(0, y + 7, 15, y + 7, STONE_DARK)
        offset = 0 if y == 0 else 4
        for x in (offset + 3, offset + 11):
            s.rect(x % 16, y, x % 16, y + 6, STONE_DARK)
        s.rect(0, y, 15, y, STONE_LIGHT if top or y == 8 else STONE)
    if not top:
        s.rect(0, 0, 15, 0, STONE)
        s.rect(0, 7, 15, 7, STONE_DARK)


CRYSTAL = (rgb(150, 246, 255), rgb(72, 186, 236), rgb(38, 110, 184))
CLOUD = rgb(250, 252, 255)
CLOUD_SHADE = rgb(208, 220, 242)
PINE = rgb(46, 132, 88)
PINE_DARK = rgb(28, 94, 70)


def crystal(s, x, top, colors):
    """A two-texel-wide crystal from row @p top to the bottom, with a pointed tip."""
    light, mid, dark = colors
    s.put(x, top - 1, light)
    s.rect(x, top, x, 15, light)
    s.rect(x + 1, top, x + 1, 15, mid)
    s.put(x + 1, 15, dark)


def make_tiles():
    s = Sheet(8, 4)
    grass_top(s.frame(0), False, False)
    dirt(s.frame(1), 2)
    grass_top(s.frame(2), True, False)
    grass_top(s.frame(3), False, True)
    stone(s.frame(4), True)
    stone(s.frame(5), False)
    # 6: one-way plank: only the top part is solid.
    p = s.frame(6)
    p.rect(0, 0, 15, 4, WOOD)
    p.rect(0, 0, 15, 0, WOOD_LIGHT)
    p.rect(0, 4, 15, 4, WOOD_DARK)
    for x in (2, 13):
        p.put(x, 2, WOOD_DARK)
        p.rect(x, 5, x, 7, WOOD_DARK)  # brackets
    for x in (5, 9):
        p.rect(x, 1, x, 3, WOOD_DARK)
    # 7: spikes on the bottom half: three triangles, lit from the left.
    sp = s.frame(7)
    spike = ["..#..", "..#..", ".l#d.", ".l#d.", "ll#dd", "ll#dd", "ll#dd"]
    for base in (0, 5, 10):
        sp.mask(spike, base + 1 if base < 10 else base, 8,
                {"l": rgb(214, 220, 234), "#": rgb(246, 248, 252), "d": rgb(126, 132, 152)})
    sp.rect(0, 15, 15, 15, rgb(90, 94, 112))
    sp.outline()
    # 8-11: decorations (no collision).
    b = s.frame(8)
    b.ellipse(8.0, 12.5, 7.0, 4.0, GRASS_DARK)
    b.ellipse(6.0, 11.5, 4.0, 3.5, GRASS)
    b.ellipse(11.0, 12.0, 3.5, 3.0, GRASS)
    b.put(5, 10, GRASS_LIGHT)
    b.put(10, 10, GRASS_LIGHT)
    b.rect(1, 15, 14, 15, GRASS_DARK)
    b.outline()
    f = s.frame(9)
    f.rect(7, 9, 7, 15, GRASS_DARK)
    f.put(6, 12, GRASS)
    f.put(8, 11, GRASS)
    f.mask([".p.", "pyp", ".p."], 6, 6, {"p": rgb(240, 110, 170), "y": rgb(255, 230, 90)})
    f.outline()
    r = s.frame(10)
    r.ellipse(8.0, 13.0, 5.5, 3.2, STONE)
    r.put(6, 11, STONE_LIGHT)
    r.put(7, 11, STONE_LIGHT)
    r.rect(3, 15, 12, 15, STONE_DARK)
    r.outline()
    g = s.frame(11)
    g.rect(7, 9, 8, 15, WOOD_DARK)
    g.rect(2, 3, 13, 9, WOOD)
    g.rect(2, 3, 13, 3, WOOD_LIGHT)
    g.mask(["....#..", "....##.", "#######", "....##.", "....#.."], 4, 4,
           {"#": rgb(250, 246, 230)})
    g.outline()
    # 12: crate, 13: brick, 14: grass single column, 15: lava.
    c = s.frame(12)
    c.rect(0, 0, 15, 15, WOOD)
    c.rect(0, 0, 15, 1, WOOD_DARK)
    c.rect(0, 14, 15, 15, WOOD_DARK)
    c.rect(0, 0, 1, 15, WOOD_DARK)
    c.rect(14, 0, 15, 15, WOOD_DARK)
    for i in range(2, 14):
        c.put(i, i, WOOD_DARK)
        c.put(15 - i, i, WOOD_DARK)
    c.rect(2, 2, 13, 2, WOOD_LIGHT)
    k = s.frame(13)
    k.rect(0, 0, 15, 15, rgb(176, 72, 60))
    for y in (3, 7, 11, 15):
        k.rect(0, y, 15, y, rgb(120, 46, 44))
    for row, y in enumerate((0, 4, 8, 12)):
        for x in ((5, 13) if row % 2 == 0 else (1, 9)):
            k.rect(x, y, x, y + 2, rgb(120, 46, 44))
        k.rect(0, y, 15, y, rgb(214, 110, 90))
    grass_top(s.frame(14), True, True)
    lv = s.frame(15)
    lv.rect(0, 4, 15, 15, rgb(230, 92, 36))
    for x in range(16):
        top = 3 + round(math.sin(x * 0.9))
        lv.rect(x, top, x, 4, rgb(255, 170, 60))
    for x, y in ((3, 8), (10, 11), (13, 7), (6, 13)):
        lv.put(x, y, rgb(255, 214, 110))
    # 16-19, 21: more decorations.
    cr = s.frame(16)
    crystal(cr, 3, 10, (rgb(255, 170, 240), rgb(206, 92, 214), rgb(130, 50, 160)))
    crystal(cr, 7, 4, CRYSTAL)
    crystal(cr, 11, 8, CRYSTAL)
    cr.outline()
    m = s.frame(17)
    m.rect(6, 10, 9, 15, rgb(236, 226, 206))
    m.rect(6, 10, 6, 15, rgb(196, 180, 160))
    m.ellipse(8.0, 9.0, 6.0, 3.6, rgb(220, 64, 70))
    m.rect(3, 10, 13, 10, rgb(160, 40, 56))
    for x, y in ((5, 8), (9, 7), (11, 9)):
        m.put(x, y, EYE)
    m.outline()
    t = s.frame(18)
    for x, top in ((3, 12), (5, 9), (7, 11), (9, 8), (11, 10), (13, 12)):
        t.rect(x, top, x, 15, GRASS if x % 4 == 1 else GRASS_DARK)
        t.put(x, top, GRASS_LIGHT)
    t.outline()
    tc = s.frame(19)
    tc.rect(7, 8, 8, 15, WOOD_DARK)
    tc.rect(6, 8, 9, 8, STONE)
    tc.ellipse(7.5, 5.0, 2.6, 3.6, rgb(240, 110, 40))
    tc.ellipse(7.5, 5.8, 1.3, 2.2, rgb(255, 222, 110))
    tc.outline()
    # 20: a cloud to stand on (one-way, like a plank); its edges match so clouds join up.
    cl = s.frame(20)
    cl.rect(0, 2, 15, 7, CLOUD)
    cl.ellipse(4.0, 2.5, 3.6, 2.5, CLOUD)
    cl.ellipse(12.0, 2.0, 3.8, 2.6, CLOUD)
    cl.rect(0, 7, 15, 8, CLOUD_SHADE)
    cl.ellipse(8.0, 8.5, 5.0, 1.5, CLOUD_SHADE)
    cl.put(3, 1, rgb(255, 255, 255))
    cl.put(11, 0, rgb(255, 255, 255))
    p = s.frame(21)
    p.rect(7, 13, 8, 15, WOOD_DARK)
    for top, bottom in ((1, 6), (4, 9), (7, 12)):
        for y in range(top, bottom + 1):
            half = 1 + (y - top)
            p.rect(8 - half, y, 7, y, PINE)
            p.rect(8, y, 7 + half, y, PINE_DARK)
    p.outline()
    # 22-25: the ground of the Crystal Moon (ice) and of Pyros (basalt), each with a top.
    ice(s.frame(22), True)
    ice(s.frame(23), False)
    basalt(s.frame(24), True)
    basalt(s.frame(25), False)
    # 26: an alien plant with glowing bulbs.
    a = s.frame(26)
    for x, top, bend in ((5, 6, -1), (8, 3, 0), (11, 7, 1)):
        for y in range(top + 2, 16):
            a.put(x + (bend if y < top + 5 else 0), y, rgb(52, 140, 120))
        a.ellipse(x + bend + 0.5, top + 1.5, 1.8, 1.8, rgb(120, 250, 220) if x != 8 else
                  rgb(250, 140, 240))
        a.put(x + bend, top, rgb(240, 255, 250))
    a.rect(4, 15, 12, 15, rgb(36, 100, 92))
    a.outline()
    return s


ICE = rgb(150, 200, 236)
ICE_LIGHT = rgb(214, 238, 255)
ICE_DARK = rgb(96, 146, 198)
FROST = rgb(244, 250, 255)
BASALT = rgb(58, 44, 50)
BASALT_LIGHT = rgb(98, 74, 74)
BASALT_DARK = rgb(36, 26, 32)
MAGMA = rgb(255, 122, 40)
MAGMA_LIGHT = rgb(255, 210, 110)


def ice(s, top):
    """Ice rock: blocks with light edges and cracks; the top one has a cap of frost."""
    s.rect(0, 0, 15, 15, ICE)
    for y in (0, 8):
        offset = 0 if y == 0 else 5
        s.rect(0, y + 7, 15, y + 7, ICE_DARK)
        s.rect((offset + 9) % 16, y, (offset + 9) % 16, y + 6, ICE_DARK)
        s.rect(0, y, 15, y, ICE_LIGHT)
    for x, y in ((3, 3), (4, 4), (12, 11), (13, 12), (6, 12)):
        s.put(x, y, ICE_LIGHT)
    if top:
        for x in range(16):
            depth = 2 + (x * 7 % 5 == 0) + (x % 6 == 2)
            s.rect(x, 0, x, depth, FROST)
        s.put(4, 4, FROST)
        s.put(11, 5, FROST)


def basalt(s, top):
    """Dark volcanic rock with glowing magma veins; the top one has a lighter crust."""
    rnd = random.Random(9 if top else 10)
    s.rect(0, 0, 15, 15, BASALT)
    for _ in range(8):
        x, y = rnd.randrange(16), rnd.randrange(16)
        s.rect(x, y, min(x + 2, 15), y, BASALT_DARK)
    x = rnd.randrange(3, 13)
    for y in range(3 if top else 0, 16):  # a crack of magma
        x = max(1, min(14, x + rnd.choice((-1, 0, 0, 1))))
        s.put(x, y, MAGMA)
        if y % 5 == 0:
            s.put(x, y, MAGMA_LIGHT)
    if top:
        s.rect(0, 0, 15, 1, BASALT_LIGHT)
        for x in range(0, 16, 3):
            s.put(x, 2, BASALT_LIGHT)


# --- Backgrounds -----------------------------------------------------------------------------


def wrapped_ellipse(image, cx, cy, rx, ry, color, wrap_y=False):
    """An ellipse that wraps around the image's left/right (and top/bottom) edges, so the image
    tiles without seams."""
    height, width = len(image), len(image[0])
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        if not wrap_y and not 0 <= y < height:
            continue
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                image[y % height][x % width] = color


def make_clouds():
    """Puffy clouds on a transparent strip, 128x48."""
    image = [[TRANSPARENT] * 128 for _ in range(48)]
    shade, light = (214, 224, 240, 255), (255, 255, 255, 255)
    for cx, cy, scale in ((20, 24, 1.0), (70, 14, 0.7), (104, 30, 1.2)):
        for ox, oy, r in ((-9, 3, 6), (0, 0, 9), (10, 2, 7), (18, 5, 4), (-15, 6, 3)):
            wrapped_ellipse(image, cx + ox * scale, cy + oy * scale, r * scale * 1.3, r * scale,
                            light)
        bottom = int(cy + 6 * scale)
        for x in range(int(cx - 24 * scale), int(cx + 26 * scale)):
            for y in range(bottom, 48):  # a flat bottom
                image[y][x % 128] = TRANSPARENT
            for y in (bottom - 2, bottom - 1):  # with a shaded underside
                if image[y][x % 128] == light:
                    image[y][x % 128] = shade
    return image


def make_cave():
    """A rock wall of round stones with a few crystals, 64x64, tiling in both directions."""
    rnd = random.Random(5)
    image = [[(70, 72, 92, 255)] * 64 for _ in range(64)]
    for _ in range(26):
        cx, cy, r = rnd.uniform(0, 64), rnd.uniform(0, 64), rnd.uniform(5, 10)
        shade = rnd.randrange(84, 112)
        wrapped_ellipse(image, cx, cy, r * 1.6, r, (52, 52, 70, 255), wrap_y=True)
        wrapped_ellipse(image, cx - 0.5, cy - 0.5, r * 1.6 - 1.2, r - 1.2,
                        (shade, shade + 2, shade + 22, 255), wrap_y=True)
        wrapped_ellipse(image, cx - r * 0.5, cy - r * 0.5, r * 0.5, r * 0.2,
                        (shade + 20, shade + 22, shade + 40, 255), wrap_y=True)
    for _ in range(5):  # crystals
        x, y = rnd.randrange(64), rnd.randrange(64)
        for i in range(4):
            image[(y + i) % 64][x] = (210, 240, 255, 255)
            image[(y + i + 1) % 64][(x + 1) % 64] = (150, 196, 240, 255)
    return image


def make_volcanoes():
    """Volcanoes with glowing lava on a dark ridge, 128x64, tiling horizontally."""
    rock, rim, lava = (52, 30, 40, 255), (92, 52, 60, 255), (255, 128, 48, 255)
    image = [[TRANSPARENT] * 128 for _ in range(64)]
    tops = [round(52 + 3 * math.sin(2 * math.pi * x / 128) + 2 * math.sin(6 * math.pi * x / 128))
            for x in range(128)]
    for cx, height, width in ((24, 40, 26), (70, 28, 20), (104, 48, 30)):
        for dx in range(-width, width + 1):
            x = (cx + dx) % 128
            top = 63 - round(height * min(1.0, 1.25 * (1.0 - abs(dx) / width)))  # flat crater
            tops[x] = min(tops[x], top)
        for i, dy in enumerate(range(4, height // 2, 3)):  # lava running down
            for side in (-1, 1):
                x = (cx + side * (dy // 3 + i % 2)) % 128
                image[63 - height + dy][x] = lava
    for x in range(128):
        for y in range(tops[x], 64):
            if image[y][x] != lava:
                image[y][x] = rim if y == tops[x] else rock
    for cx, height, _ in ((24, 40, 26), (70, 28, 20), (104, 48, 30)):  # glowing crater
        for dx in range(-3, 4):
            image[63 - height][(cx + dx) % 128] = lava
    return image


def make_stars():
    """Stars on a transparent 64x64 tile that repeats in both directions."""
    rnd = random.Random(11)
    image = [[TRANSPARENT] * 64 for _ in range(64)]
    for i in range(46):
        x, y = rnd.randrange(64), rnd.randrange(64)
        alpha = rnd.choice((90, 140, 200, 255))
        tint = rnd.choice(((255, 255, 255), (200, 220, 255), (255, 236, 200)))
        image[y][x] = (*tint, alpha)
        if i % 12 == 0:  # a few bright ones with a cross
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                image[(y + dy) % 64][(x + dx) % 64] = (*tint, 120)
    return image


PLANET = 32


def make_planets():
    """The five planets of the route, 32x32 each with a 1-texel border like the sprite sheets:
    Verda, the Crystal Moon, Nimbus, Pyros and Earth."""
    rnd = random.Random(3)
    cell = PLANET + 2 * PADDING
    image = [[TRANSPARENT] * (cell * 5) for _ in range(cell)]

    def shade(color, factor):
        return tuple(max(0, min(255, round(c * factor))) for c in color[:3]) + (255,)

    def blobs(count, radius):
        return [(rnd.uniform(4, 28), rnd.uniform(4, 28), rnd.uniform(*radius)) for _ in range(count)]

    looks = [
        ((72, 168, 96), [((52, 112, 196), blobs(4, (4, 7))), ((240, 248, 255), blobs(3, (1.5, 2.5)))]),
        ((176, 214, 240), [((128, 166, 214), blobs(6, (1.5, 3.5)))]),
        ((206, 124, 196), "bands"),
        ((122, 34, 36), "cracks"),
        ((56, 112, 212), [((84, 172, 92), blobs(4, (4, 6))), ((245, 250, 255), blobs(4, (1.5, 3)))]),
    ]
    for i, (base, features) in enumerate(looks):
        ox = i * cell + PADDING
        for y in range(PLANET):
            for x in range(PLANET):
                dx, dy = (x + 0.5 - 16) / 13.0, (y + 0.5 - 16) / 13.0
                if dx * dx + dy * dy > 1.0:
                    continue
                color = base
                if features == "bands":
                    band = math.sin(y * 0.9 + math.sin(x * 0.3))
                    color = shade(base, 1.15 if band > 0.6 else 0.82 if band < -0.5 else 1.0)
                elif features == "cracks":  # glowing lava along wavy lines
                    crack = math.sin(x * 0.55 + 2.2 * math.sin(y * 0.4)) + math.sin(y * 0.7 - x * 0.2)
                    color = (255, 132, 44) if abs(crack) < 0.18 else base
                else:
                    for feature, spots in features:
                        if any((x - bx) ** 2 + (y - by) ** 2 <= r * r for bx, by, r in spots):
                            color = feature
                light = -0.65 * dx - 0.65 * dy  # lit from the top left
                factor = 1.2 if light > 0.75 else 0.55 if light < -0.45 else 1.0
                image[PADDING + y][ox + x] = shade(color, factor)
        if features == "bands":  # a ring in front
            for x in range(PLANET):
                for y in range(PLANET):
                    dx, dy = (x + 0.5 - 16) / 15.8, (y + 0.5 - 17) / 5.0
                    d = dx * dx + dy * dy
                    if 0.55 <= d <= 1.0 and (y >= 17 or abs(x + 0.5 - 16) > 12.5):
                        image[PADDING + y][ox + x] = (250, 228, 180, 255) if d < 0.8 else \
                            (196, 160, 120, 255)
    return image


# ---------------------------------------------------------------------------------------------
# Sound

RATE = 22050


def write_wav(path, samples):
    """16-bit mono PCM; samples are floats in [-1, 1]."""
    data = b"".join(struct.pack("<h", max(-32767, min(32767, round(s * 32767)))) for s in samples)
    header = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
              struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16) + b"data" +
              struct.pack("<I", len(data)))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + data)


def square(phase, duty=0.5):
    return 1.0 if (phase % 1.0) < duty else -1.0


def triangle(phase):
    p = phase % 1.0
    return 4.0 * p - 1.0 if p < 0.5 else 3.0 - 4.0 * p


def tone(duration, freq, wave=square, volume=0.5, attack=0.005, release=0.05, duty=0.5,
         slide=None, vibrato=0.0):
    """One note; freq may slide linearly to `slide` Hz."""
    n = int(duration * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        f = freq if slide is None else freq + (slide - freq) * (i / n)
        f *= 1.0 + vibrato * math.sin(2 * math.pi * 18 * t)
        phase += f / RATE
        env = min(1.0, t / attack) * min(1.0, (duration - t) / release)
        out.append(volume * env * (wave(phase, duty) if wave is square else wave(phase)))
    return out


def noise(duration, volume=0.5, decay=20.0, seed=7):
    rnd = random.Random(seed)
    n = int(duration * RATE)
    return [volume * math.exp(-decay * i / RATE) * (rnd.random() * 2 - 1) for i in range(n)]


def mix(*tracks):
    length = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(length)]


def sequence(*parts):
    out = []
    for p in parts:
        out += p
    return out


def decay(samples, rate=8.0):
    return [s * math.exp(-rate * i / RATE) for i, s in enumerate(samples)]


NOTES = {"C": -9, "D": -7, "E": -5, "F": -4, "G": -2, "A": 0, "B": 2}


def hz(note):
    """'A4' -> 440 Hz; sharps as 'F#4'."""
    name, octave = note[:-1], int(note[-1])
    semitone = NOTES[name[0]] + (1 if "#" in name else 0) + 12 * (octave - 4)
    return 440.0 * 2 ** (semitone / 12)


def make_sounds():
    sounds = {
        "jump": decay(tone(0.16, 280, duty=0.25, slide=720, volume=0.35), 6),
        "coin": sequence(tone(0.06, hz("B5"), volume=0.3, release=0.01),
                         decay(tone(0.28, hz("E6"), volume=0.3), 9)),
        "stomp": mix(decay(tone(0.16, 220, wave=triangle, slide=50, volume=0.8), 10),
                     noise(0.1, 0.35, 40)),
        "hurt": mix(tone(0.32, 520, duty=0.5, slide=140, volume=0.35, vibrato=0.06),
                    noise(0.12, 0.3, 30)),
        "checkpoint": sequence(*(tone(0.07, hz(n), wave=triangle, volume=0.6, release=0.02)
                                 for n in ("C6", "E6", "G6")),
                               decay(tone(0.25, hz("C7"), wave=triangle, volume=0.5), 8)),
        "win": sequence(*(tone(0.11, hz(n), duty=0.25, volume=0.3, release=0.02)
                          for n in ("C5", "E5", "G5", "C6", "E6", "G6")),
                        mix(decay(tone(0.7, hz("C6"), duty=0.25, volume=0.25), 3),
                            decay(tone(0.7, hz("E5"), wave=triangle, volume=0.4), 3))),
        "gameover": sequence(*(tone(0.22, hz(n), duty=0.5, volume=0.3, release=0.04,
                                    vibrato=0.01) for n in ("G4", "F#4", "F4")),
                             decay(tone(0.8, hz("E4"), duty=0.5, volume=0.3, vibrato=0.015),
                                   3)),
    }
    for name, samples in sounds.items():
        write_wav(ASSETS / "sounds" / f"{name}.wav", samples)
    write_wav(ASSETS / "sounds" / "music.wav", make_music())
    print(f"Wrote {len(sounds) + 1} sounds to {ASSETS / 'sounds'}")


def make_music():
    """An original 8-bar loop at 120 bpm: square lead, triangle bass, noise hi-hat, kick."""
    eighth = 0.25  # seconds
    bar = int(8 * eighth * RATE)
    total = 8 * bar
    out = [0.0] * total

    def add(start, samples):
        for i, s in enumerate(samples):
            if start + i < total:
                out[start + i] += s

    # Lead: one string per bar, eight eighth notes; '-' holds the note before, '.' is a rest.
    melody = [
        "A4 - C5 E5 - D5 C5 -", "A4 - C5 F5 - E5 D5 C5",
        "G4 - C5 E5 - G5 E5 -", "D5 - B4 G4 - A4 B4 -",
        "A4 - C5 E5 - A5 G5 E5", "F5 - E5 C5 - A4 C5 -",
        "E5 - D5 C5 - G4 C5 E5", "D5 - - - B4 - . .",
    ]
    for b, line in enumerate(melody):
        notes = line.split()
        i = 0
        while i < 8:
            note = notes[i]
            length = 1
            while i + length < 8 and notes[i + length] == "-":
                length += 1
            if note not in ("-", "."):
                add(b * bar + int(i * eighth * RATE),
                    tone(length * eighth - 0.02, hz(note), duty=0.25, volume=0.16,
                         release=0.04, vibrato=0.004))
            i += length
    # Bass: chord roots in eighths with an octave jump; chords Am F C G.
    roots = ["A2", "F2", "C3", "G2"] * 2
    for b, root in enumerate(roots):
        for i in range(8):
            f = hz(root) * (2 if i % 2 else 1)
            add(b * bar + int(i * eighth * RATE),
                tone(eighth - 0.03, f, wave=triangle, volume=0.32, release=0.03))
    # Drums: kick on beats 1 and 3, hi-hat on every eighth, snare-ish noise on 2 and 4.
    for b in range(8):
        for i in range(8):
            start = b * bar + int(i * eighth * RATE)
            add(start, noise(0.03, 0.05, 120, seed=b * 8 + i))
            if i in (0, 4):
                add(start, decay(tone(0.12, 120, wave=triangle, slide=45, volume=0.5), 18))
            if i in (2, 6):
                add(start, noise(0.12, 0.12, 25, seed=99 + b))
    peak = max(abs(s) for s in out)
    return [0.7 * s / peak for s in out]


# ---------------------------------------------------------------------------------------------
# Levels

# Same characters as level1.txt (see that file). Tile frames are chosen like the game does for
# text maps (choose_tile below), then saved as an editable Tiled map.
LEVELS = {
    "level2": {
        "name": "Crystal Moon",
        "hint": "Low gravity: you jump higher. Spikers cannot be stomped",
        "rows": [
            "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
            "II..................IIII............................IIIIIIIIIIIIIIIIIII..............................IIII...................IIIII...................II",
            "II..................................................IIIIIIIIIIIIIIIIIII...........ooH...............................................................II",
            "II..................................................IIIIIIIIIIIIIIIIIII..........c..c...............................................................II",
            "II..................................................IIIIIIIIIIIIIIIIIII.........IIIIII..............................................................II",
            "II..................................................IIIIIIIIIIIIIIIIIII.........IIIIII..............................................................II",
            "II.........................oo.......................IIIIIIIIIIIIIIIIIII.........IIIIII..............................................................II",
            "II..............................................................................IIIIII....................ooo.................v.....................II",
            "II.....oooo...o.o.o........c.....v..............................................IIIIII....v...........ooo.---........o.o.o.o.o......................II",
            "II............-----........II..........o.o.o....................................IIIIII................---............................o.o.o..........II",
            "II.........................II.........................o.o.o.o.o.o.o.............IIIIII..............................................................II",
            "II.P..c..u...c........k....II...................C.........k.......u..........V..IIIIII......c..k................C...c.....k...................c.u.F.II",
            "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII..M.......IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII.........IIIIIIIIIIIIIIIIIIIII..M.......IIIIIIIIII",
            "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIILLLLLLLLLLIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIILLLLLLLLLIIIIIIIIIIIIIIIIIIIIILLLLLLLLLLIIIIIIIIII",
            "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
            "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII",
        ],
    },
    "level3": {
        "name": "Nimbus",
        "hint": "Clouds hold you from above; hold DOWN and JUMP to drop",
        "rows": [
            "................................................................................................................................................................",
            "............................o.o.o.H.............................................................................................................................",
            "...........................~~~~~~~~.............................................................................................................................",
            "....................................................................v...........................................................................................",
            ".........................................................................................................v......................................................",
            "..................................................................ooo.......................o.o.oa..............................................................",
            "..................................................................~~~......................########..o.o....o.o.................................ooo.............",
            "............ooo.........................o.o.o.........v......ooo.......ooo.................########..~~~~...~~~~................o.o.o...........~~~.............",
            "............~~~..............................................~~~.......~~~.................########.............................................................",
            ".................................j..............C...a...g..................................########.................C...e..b.............g...k....a..g...F...b..",
            "...P..s.g..........f.e..b.....#######..M......#############...................g.k..f.......########...............###########..M......##########################",
            "##########.......##########...#######.........#############.................#########......########...............###########.........##########################",
            "##########.......##########...#######.........#############.................#########...V..########...............###########.........##########################",
            "##########.......##########...#######.........#############.................#########......########...............###########.........##########################",
            "##########.......##########...#######.........#############.................#########......########...............###########.........##########################",
            "##########.......##########...#######.........#############.................#########......########...............###########.........##########################",
        ],
    },
    "level4": {
        "name": "Pyros",
        "hint": "Heavy gravity: jumps are shorter. Mind the lava",
        "rows": [
            "RR............................................................................................................................................................RR",
            "RR............................................................................................RRRRRRRRRRRRRRRRR...............................................RR",
            "RR............................................................................................RRRRRRRRRRRRRRRRR...............................................RR",
            "RR.....................................................................t.o.ot.................RRRRRRRRRRRRRRRRR..........o.o.o.k..............................RR",
            "RR....................................................................RRRRRRR.................RRRRRRRRRRRRRRRRR.........---------.............................RR",
            "RR....................................................................RRRRRRR.................RRRRRRRRRRRRRRRRR...............................................RR",
            "RR....................................................................RRRRRRR.................RRRRRRRRRRRRRRRRR...............................................RR",
            "RR................ooo.................................................RRRRRRR.................RRRRRRRRRRRRRRRRR...............................................RR",
            "RR........ooo....................o.o.o................................RRRRRRR.....v.......v...................................................................RR",
            "RR..............................RRRRRRR...........o.o.o...............RRRRRRR..........................................................o.o.o..................RR",
            "RR...........................................RR.......................RRRRRRR......................ooo...o..H.................................................RR",
            "RR.P..t...........^^^....k..t................RR..........t..C......V..RRRRRRR.........k..........^^....^^.........C...j.........................e.....t...F..tRR",
            "RRRRRRRRRR...RRRRRRRRRRRRRRRRRRRRRRRRRRRRR...RR..M......RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR..M.......RRRRRRRRRRRRRRRRRR",
            "RRRRRRRRRRLLLRRRRRRRRRRRRRRRRRRRRRRRRRRRRRLLLRRLLLLLLLLLRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRLLLLLLLLLLRRRRRRRRRRRRRRRRRR",
            "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
            "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
        ],
    },
}

SOLID_GROUND = "#"
PLANK = "-"
DECOR = {"b": 8, "f": 9, "r": 10, "s": 11, "c": 16, "u": 17, "g": 18, "t": 19, "y": 21, "a": 26}
TOPPED = {"=": (4, 5), "I": (22, 23), "R": (24, 25)}  # (top, inside): stone, ice, basalt
SPAWNS = {"P": "player", "o": "coin", "e": "crawler", "v": "flyer", "k": "spiker", "j": "spring",
          "H": "heart", "M": "platform", "V": "lift", "F": "goal", "C": "checkpoint"}


def choose_tile(rows, x, y):
    """Visual tile for the character at (x, y), or None. Must match ground tiles in the game."""
    def at(cx, cy):
        if 0 <= cy < len(rows) and 0 <= cx < len(rows[cy]):
            return rows[cy][cx]
        return "#"  # outside counts as ground: no grass edges at the map border

    c = at(x, y)
    if c == SOLID_GROUND:
        if at(x, y - 1) == SOLID_GROUND:
            return 1
        left, right = at(x - 1, y) == SOLID_GROUND, at(x + 1, y) == SOLID_GROUND
        return 0 if left and right else 3 if left else 2 if right else 14
    if c in TOPPED:
        top, inside = TOPPED[c]
        return inside if at(x, y - 1) == c else top
    return {"-": 6, "^": 7, "X": 12, "B": 13, "L": 15, "~": 20}.get(c)


def make_tileset():
    solid = [0, 1, 2, 3, 4, 5, 12, 13, 14, 22, 23, 24, 25]
    tiles = []
    for i in range(32):
        props = []
        if i in solid:
            props.append({"name": "collision", "type": "string", "value": "solid"})
        elif i in (6, 20):
            props.append({"name": "collision", "type": "string", "value": "oneway"})
        elif i in (7, 15):
            props.append({"name": "hazard", "type": "bool", "value": True})
        if props:
            tiles.append({"id": i, "properties": props})
    return {
        "type": "tileset", "version": "1.10", "tiledversion": "1.10.2", "name": "tiles",
        "image": "../textures/tiles.png", "imagewidth": CELL * 8, "imageheight": CELL * 4,
        "tilewidth": FRAME, "tileheight": FRAME, "tilecount": 32, "columns": 8,
        "margin": PADDING, "spacing": 2 * PADDING, "tiles": tiles,
    }


def make_map(level):
    rows = level["rows"]
    width, height = max(len(r) for r in rows), len(rows)
    rows = [r.ljust(width, ".") for r in rows]
    ground, decor, objects = [], [], []
    for y in range(height):
        for x in range(width):
            tile = choose_tile(rows, x, y)
            ground.append(0 if tile is None else tile + 1)  # gid = firstgid (1) + local id
            c = rows[y][x]
            decor.append(DECOR[c] + 1 if c in DECOR else 0)
            if c in SPAWNS:
                objects.append({
                    "id": len(objects) + 1, "name": "", "type": SPAWNS[c], "point": True,
                    "x": (x + 0.5) * FRAME, "y": (y + 0.5) * FRAME, "width": 0, "height": 0,
                    "rotation": 0, "visible": True,
                })

    def layer(layer_id, name, data):
        return {"id": layer_id, "name": name, "type": "tilelayer", "width": width,
                "height": height, "x": 0, "y": 0, "opacity": 1, "visible": True, "data": data}

    return {
        "type": "map", "version": "1.10", "tiledversion": "1.10.2", "orientation": "orthogonal",
        "renderorder": "right-down", "infinite": False, "width": width, "height": height,
        "tilewidth": FRAME, "tileheight": FRAME, "nextlayerid": 4,
        "nextobjectid": len(objects) + 1, "compressionlevel": -1,
        "properties": [{"name": "name", "type": "string", "value": level["name"]},
                       {"name": "hint", "type": "string", "value": level["hint"]}],
        "tilesets": [{"firstgid": 1, "source": "tiles.tsj"}],
        "layers": [layer(1, "decor", decor), layer(2, "ground", ground),
                   {"id": 3, "name": "objects", "type": "objectgroup", "draworder": "topdown",
                    "opacity": 1, "visible": True, "x": 0, "y": 0, "objects": objects}],
    }


def make_levels():
    levels = ASSETS / "levels"
    levels.mkdir(parents=True, exist_ok=True)
    (levels / "tiles.tsj").write_text(json.dumps(make_tileset(), indent=1) + "\n")
    for name, level in LEVELS.items():
        (levels / f"{name}.tmj").write_text(json.dumps(make_map(level), indent=1) + "\n")
    print(f"Wrote {levels / 'tiles.tsj'} and {len(LEVELS)} Tiled maps")


def main():
    textures = ASSETS / "textures"
    write_png(textures / "platformer.png", make_characters().extruded())
    write_png(textures / "tiles.png", make_tiles().extruded())
    write_png(textures / "clouds.png", make_clouds())
    write_png(textures / "cave.png", make_cave())
    write_png(textures / "volcanoes.png", make_volcanoes())
    write_png(textures / "stars.png", make_stars())
    write_png(textures / "planets.png", make_planets())
    print(f"Wrote the sprite sheets, tiles and backgrounds to {textures}")
    make_sounds()
    make_levels()


if __name__ == "__main__":
    main()
