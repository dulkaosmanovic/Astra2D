#!/usr/bin/env python3
"""Generates the assets of examples/platformer: sprite sheets, sounds and the Tiled levels.

Everything is original and drawn or synthesized by this script (CC0, no third-party assets), with
only the Python standard library:

    python3 scripts/generate_platformer_assets.py

Writes into examples/assets/:
    textures/astronaut.png    the astronaut and the rocket, 8x2 frames of 24x24
    textures/platformer.png   creatures and objects, 8x5 frames of 16x16
    textures/tiles.png        the tileset, 8x5 tiles of 16x16
    textures/nebula.png       sky backgrounds that tile in both directions; drawn light so the
    textures/stars.png        game can tint them per planet
    textures/spires.png       parallax strips that tile horizontally: Verda's rock spires and
    textures/flora.png        alien forest, the Crystal Moon's crystals, Nimbus's clouds and
    textures/crystals.png     floating islands, Pyros's volcanoes
    textures/clouds.png
    textures/islands.png
    textures/volcanoes.png
    textures/cave.png         the Crystal Moon's cave wall, tiling in both directions
    textures/rings.png        the rings of Nimbus as seen from its surface (one arc, not tiling)
    textures/planets.png      the five planets of the route, 5 frames of 32x32
    sounds/*.wav              sound effects and the music loop (16-bit mono, 22050 Hz)
    levels/tiles.tsj          the tileset for Tiled, with collision properties
    levels/level2.tmj         Tiled maps, made from the ASCII art below; they open in Tiled and
    levels/level3.tmj         can be edited there (running this script again overwrites them)
    levels/level4.tmj

levels/level1.txt is not generated: it is the hand-written text map (format in that file).

The sheets use the layout of sprites.png: every frame has a 1-texel extruded border (margin 1,
spacing 2 between frames), so nothing bleeds in when scaled.

astronaut.png frames (row-major from the top-left); the art of each stands on row 19, so a frame
centred on a tile has its feet on the tile's bottom edge:
     0  1   idle                   2-5  run                 6 jump      7 fall      8 hurt
     9 10   rocket, lower half (light on/off)   11 rocket, upper half (20 texels higher)
    12 13   rocket exhaust (flickering)
platformer.png frames:
     8  9   crawler (a slime)      10   crawler squashed    11-14 coin  15 heart
    18 19   beacon off/on          20   empty heart         21-23 sparkle
    24 25   bat flying             26   bat stunned         27 28 spiker walk
    30 31   spring, pressed        32-34 dust puff          35-37 hover platform left/middle/right
    (0-7, 16, 17, 29, 38 and 39 are empty)
tiles.png tiles:
     0 turf   1 soil   2 turf left edge   3 turf right edge   4 stone top   5 stone
     6 one-way girder   7 spikes   8 coral bush   9 bulb flower   10 moon rock   11 sign
    12 crate   13 hull panel   14 turf single column   15 lava   16 crystals   17 mushrooms
    18 tendrils   19 fire pit   20 one-way cloud   21 spiral tree   22 ice top   23 ice
    24 basalt top   25 basalt   26 alien plant   27 moss top   28 moss   29 dish   30 geode
    31 puffballs   32 cloud shrub   33 bell flower (Nimbus)   34 charred tree   35 lava vent
    36 magma boulder (Pyros)   37-39 empty
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
    """Frames of width x height texels in a grid, drawn with y down inside each frame."""

    def __init__(self, columns, rows, width=FRAME, height=None):
        self.columns, self.rows = columns, rows
        self.w, self.h = width, height or width
        self.pixels = [[TRANSPARENT] * (columns * self.w) for _ in range(rows * self.h)]
        self.ox = self.oy = 0

    def frame(self, index):
        self.ox = (index % self.columns) * self.w
        self.oy = (index // self.columns) * self.h
        return self

    def put(self, x, y, color):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.pixels[self.oy + y][self.ox + x] = color

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.pixels[self.oy + y][self.ox + x]
        return TRANSPARENT

    def rect(self, x0, y0, x1, y1, color):
        """Inclusive rectangle."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, color)

    def ellipse(self, cx, cy, rx, ry, color):
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.put(x, y, color)

    def points(self, points, color):
        for x, y in points:
            self.put(x, y, color)

    def outline(self, color=OUTLINE):
        """Surrounds the opaque texels of the current frame with a one-texel outline."""
        edge = []
        for y in range(self.h):
            for x in range(self.w):
                if self.get(x, y)[3] == 0 and any(
                        self.get(x + dx, y + dy)[3] > 128 and self.get(x + dx, y + dy) != color
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

    def copy_from(self, other, sy, rows):
        """Copies @p rows rows of @p other, from its row @p sy, to the top of the current frame."""
        for y in range(rows):
            for x in range(self.w):
                self.put(x, y, other.pixels[sy + y][x])

    def extruded(self):
        """Every frame in its own cell with a PADDING-texel border of repeated edge texels."""
        cw, ch = self.w + 2 * PADDING, self.h + 2 * PADDING
        out = [[TRANSPARENT] * (cw * self.columns) for _ in range(ch * self.rows)]
        for row in range(self.rows):
            for column in range(self.columns):
                for dy in range(-PADDING, self.h + PADDING):
                    for dx in range(-PADDING, self.w + PADDING):
                        sx = column * self.w + min(max(dx, 0), self.w - 1)
                        sy = row * self.h + min(max(dy, 0), self.h - 1)
                        out[row * ch + PADDING + dy][column * cw + PADDING + dx] = \
                            self.pixels[sy][sx]
        return out


def shade(color, factor):
    return tuple(max(0, min(255, round(c * factor))) for c in color[:3]) + (color[3],)


def mixed(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3)) + (255,)


# --- The astronaut and the rocket (24x24 frames) ---------------------------------------------
# The art of each 24x24 frame stands on row 19, so a frame drawn centred on a 16x16 tile's centre
# has its feet on the tile's bottom edge; the 4 rows below stay empty.

BIG = 24

SUIT = rgb(238, 241, 248)
SUIT_SHADE = rgb(184, 192, 214)
SUIT_DARK = rgb(128, 136, 166)
ACCENT = rgb(244, 132, 48)
ACCENT_DARK = rgb(196, 84, 36)
GLOVE = rgb(110, 120, 156)
BOOT = rgb(86, 92, 124)
BOOT_LIGHT = rgb(128, 136, 170)
PACK = rgb(200, 206, 224)
PACK_DARK = rgb(138, 146, 176)
VISOR = rgb(40, 56, 112)
VISOR_MID = rgb(70, 118, 196)
VISOR_LIGHT = rgb(150, 220, 255)
VISOR_HURT = rgb(196, 52, 70)
VISOR_HURT_MID = rgb(240, 110, 110)
GLINT = rgb(255, 255, 255)
LAMP_ON = rgb(255, 84, 84)
LAMP_OFF = rgb(130, 60, 70)

ARMS = {  # the near arm: sleeve texels, then glove texels
    "down": ([(12, 11), (13, 11), (12, 12), (13, 12), (12, 13), (13, 13)], [(12, 14), (13, 14)]),
    "front": ([(13, 11), (14, 11), (14, 12), (15, 12), (16, 12)], [(17, 12), (17, 13), (16, 13)]),
    "back": ([(11, 11), (12, 11), (10, 12), (11, 12), (9, 13)], [(8, 13), (8, 14), (9, 14)]),
    "up": ([(13, 11), (14, 11), (15, 10), (16, 10)], [(17, 9), (17, 10), (18, 9)]),
}


def draw_astronaut(s, legs, bob=0, arm="down", lamp=True, hurt=False):
    """The astronaut, facing right, standing on row 19. legs: (x, lift) of the far and the near
    leg, each 2 texels wide; bob moves everything above the legs (negative: up)."""
    b = bob
    for i, (x, lift) in enumerate(legs):  # the far leg is drawn darker, behind
        near = i == 1
        bottom = 19 - lift
        boot = BOOT if near else shade(BOOT, 0.75)
        s.rect(x, 15 + b, x + 1, bottom - 2, SUIT if near else SUIT_SHADE)
        s.put(x, 15 + b, SUIT_SHADE if near else SUIT_DARK)
        s.rect(x, bottom - 1, x + 1, bottom, boot)
        s.put(x + 2, bottom, boot)  # toe
        if near:
            s.put(x, bottom - 1, BOOT_LIGHT)
            s.put(x + 1, bottom - 2, ACCENT)  # knee stripe
    # Life-support pack on the back, with an antenna and its blinking lamp.
    s.rect(4, 6 + b, 8, 14 + b, PACK)
    s.rect(4, 6 + b, 4, 14 + b, PACK_DARK)
    s.rect(4, 14 + b, 8, 14 + b, PACK_DARK)
    s.rect(5, 6 + b, 8, 6 + b, GLINT)
    s.rect(6, 9 + b, 7, 9 + b, PACK_DARK)
    s.rect(6, 11 + b, 7, 11 + b, PACK_DARK)
    s.rect(5, 2 + b, 5, 5 + b, SUIT_DARK)
    s.put(5, 1 + b, LAMP_ON if lamp else LAMP_OFF)
    # Body, with a belt and a chest unit with lights.
    s.rect(9, 10 + b, 15, 14 + b, SUIT)
    s.rect(9, 10 + b, 10, 14 + b, SUIT_SHADE)
    s.rect(9, 14 + b, 15, 14 + b, SUIT_DARK)
    s.rect(14, 12 + b, 15, 13 + b, SUIT_DARK)
    s.put(14, 12 + b, rgb(255, 92, 92))
    s.put(15, 12 + b, rgb(110, 240, 140))
    s.put(14, 13 + b, rgb(120, 210, 255))
    # Helmet: a big round bubble with a dark visor that reflects the stars.
    s.ellipse(12.0, 5.8 + b, 5.4, 4.9, SUIT)
    for x, y in ((7, 4), (7, 5), (7, 6), (7, 7), (8, 8), (8, 9), (9, 9), (10, 10), (8, 3)):
        s.put(x, y + b, SUIT_SHADE)
    s.put(9, 1 + b, GLINT)
    s.put(8, 2 + b, GLINT)
    s.rect(9, 10 + b, 15, 10 + b, ACCENT)  # collar
    s.rect(9, 10 + b, 10, 10 + b, ACCENT_DARK)
    visor, mid = (VISOR_HURT, VISOR_HURT_MID) if hurt else (VISOR, VISOR_MID)
    s.ellipse(14.3, 6.0 + b, 3.4, 2.8, visor)
    for x, y in ((12, 4), (13, 4), (14, 3), (15, 3), (16, 4), (14, 7)):
        if s.get(x, y + b) == visor:
            s.put(x, y + b, mid)  # the sky reflected in the visor
    s.put(12, 4 + b, VISOR_LIGHT)
    s.put(13, 4 + b, GLINT)
    s.put(16, 7 + b, VISOR_LIGHT)
    # The near arm, with an orange band and a dark edge where it crosses the body.
    sleeve, glove = ARMS[arm]
    arm_texels = {(x, y + b) for x, y in sleeve + glove}
    for x, y in arm_texels:
        for nx, ny in ((x - 1, y), (x, y + 1)):
            if (nx, ny) not in arm_texels and s.get(nx, ny) in (SUIT, SUIT_SHADE, ACCENT):
                s.put(nx, ny, SUIT_DARK)
    s.points([(x, y + b) for x, y in sleeve], SUIT)
    s.put(sleeve[2][0], sleeve[2][1] + b, ACCENT)
    s.points([(x, y + b) for x, y in glove], GLOVE)
    s.outline()


HULL = rgb(238, 241, 248)
HULL_SHADE = rgb(176, 186, 210)
HULL_DARK = rgb(120, 130, 162)
HULL_RED = rgb(230, 70, 70)
HULL_RED_DARK = rgb(160, 40, 56)
HULL_RED_LIGHT = rgb(255, 136, 120)
STEEL = rgb(122, 140, 172)
STEEL_LIGHT = rgb(190, 208, 232)
STEEL_DARK = rgb(70, 82, 112)
SIGNAL = rgb(250, 200, 70)


def draw_rocket(s, light_on):
    """The whole rocket on a 24x40 canvas, standing on row 39: nose cone, porthole, fins, nozzle
    and landing legs. Split into two 24x24 frames by make_big()."""
    for y in range(0, 34):  # the hull, round at the nose
        half = 4 if y >= 10 else max(1, round(4.2 * math.sqrt(y / 10.0 + 0.04)))
        x0, x1 = 12 - half, 11 + half
        s.rect(x0, y, x1, y, HULL)
        s.put(x0, y, GLINT)
        if x1 - x0 >= 3:
            s.rect(x1 - 1, y, x1, y, HULL_SHADE)
        if x1 - x0 >= 5:
            s.put(x1 - 2, y, HULL_SHADE if y % 2 else HULL)
    for y in range(0, 9):  # red nose cone
        for x in range(24):
            if s.get(x, y) in (HULL, GLINT):
                s.put(x, y, HULL_RED_LIGHT if s.get(x, y) == GLINT else HULL_RED)
            elif s.get(x, y) == HULL_SHADE:
                s.put(x, y, HULL_RED_DARK)
    s.rect(8, 9, 15, 9, HULL_DARK)  # seam under the cone
    s.ellipse(12.0, 15.5, 3.3, 3.3, STEEL)  # porthole
    s.ellipse(12.0, 15.5, 2.3, 2.3, VISOR)
    s.points(((11, 14), (11, 15), (12, 14)), VISOR_MID)
    s.put(11, 14, VISOR_LIGHT)
    s.rect(8, 22, 15, 23, HULL_RED)  # stripe
    s.rect(14, 22, 15, 23, HULL_RED_DARK)
    s.put(8, 22, HULL_RED_LIGHT)
    s.points(((10, 27), (11, 27)), SIGNAL if light_on else STEEL_DARK)
    s.points(((10, 26), (11, 28)), HULL_SHADE)
    for side in (-1, 1):  # fins
        for i, y in enumerate(range(24, 36)):
            reach = min(5, 1 + i // 2)
            if side < 0:
                s.rect(8 - reach, y, 7, y, HULL_RED if i < 10 else HULL_RED_DARK)
                s.put(8 - reach, y, HULL_RED_LIGHT)
            else:
                s.rect(16, y, 15 + reach, y, HULL_RED_DARK)
    s.rect(9, 34, 14, 34, STEEL_DARK)  # nozzle
    s.rect(8, 35, 15, 37, STEEL)
    s.rect(8, 35, 9, 37, STEEL_LIGHT)
    s.rect(14, 35, 15, 37, STEEL_DARK)
    for x0 in (3, 19):  # landing legs
        s.rect(x0, 34, x0 + 1, 38, STEEL_DARK)
        s.put(x0, 34, STEEL)
        s.rect(x0 - 1, 39, x0 + 2, 39, STEEL_DARK)
    s.outline()


def draw_exhaust(s, step):
    """The flame under the rocket, flickering between two lengths."""
    length = 16 if step == 0 else 13
    for y in range(length):
        t = y / length
        half = 3.8 * (1.0 - t) ** 0.7 + (0.6 if step and y % 3 == 0 else 0)
        for x in range(24):
            d = abs(x + 0.5 - 12.0)
            if d <= half:
                core = d <= half * 0.45 and t < 0.6
                color = (rgb(255, 250, 220) if core and t < 0.3 else rgb(255, 214, 96) if core
                         else rgb(250, 128, 40) if t < 0.7 else rgb(214, 60, 40))
                s.put(x, y, color)
    for x, y in ((6, 4 + step), (17, 6 - step), (8, 11), (16, 12 + step)):  # sparks
        s.put(x, y, rgb(255, 200, 90))


def make_big():
    """astronaut.png: the astronaut and their rocket, 8x2 frames of 24x24 (see the module doc)."""
    s = Sheet(8, 2, BIG)
    draw_astronaut(s.frame(0), [(10, 0), (13, 0)])
    draw_astronaut(s.frame(1), [(10, 0), (13, 0)], bob=1, lamp=False)
    draw_astronaut(s.frame(2), [(9, 1), (15, 0)], arm="back")
    draw_astronaut(s.frame(3), [(11, 0), (13, 2)], bob=-1)
    draw_astronaut(s.frame(4), [(14, 0), (9, 1)], arm="front")
    draw_astronaut(s.frame(5), [(13, 2), (11, 0)], bob=-1)
    draw_astronaut(s.frame(6), [(9, 1), (14, 2)], bob=-1, arm="up")
    draw_astronaut(s.frame(7), [(9, 0), (15, 0)], arm="up", lamp=False)
    draw_astronaut(s.frame(8), [(8, 1), (15, 0)], arm="up", hurt=True)
    rockets = []
    for light in (True, False):
        canvas = Sheet(1, 1, BIG, 40)
        draw_rocket(canvas, light)
        rockets.append(canvas)
    s.frame(9).copy_from(rockets[0], 20, 20)  # lower half, standing on row 19
    s.frame(10).copy_from(rockets[1], 20, 20)
    s.frame(11).copy_from(rockets[0], 0, 20)  # upper half, drawn 20 texels higher
    draw_exhaust(s.frame(12), 0)
    draw_exhaust(s.frame(13), 1)
    return s


# --- Creatures and objects (16x16 frames) -----------------------------------------------------

EYE = rgb(250, 250, 245)
PUPIL = OUTLINE
SLIME = rgb(236, 92, 170)
SLIME_LIGHT = rgb(255, 170, 214)
SLIME_DARK = rgb(168, 48, 124)
GLOW = rgb(255, 236, 120)


def draw_crawler(s, step):
    """A one-eyed alien slime with two feelers, facing right; it squishes as it crawls."""
    rx, ry = (6.8, 4.3) if step == 0 else (6.2, 4.8)
    top = 15.6 - 2 * ry
    s.ellipse(8.0, 15.6 - ry, rx, ry, SLIME)
    s.rect(round(8 - rx) + 1, 15, round(8 + rx) - 1, 15, SLIME_DARK)
    s.ellipse(7.0, 15.6 - ry * 0.55, rx * 0.7, ry * 0.45, SLIME_DARK)
    s.ellipse(8.0, 15.6 - ry, rx * 0.95, ry * 0.75, SLIME)
    for x, tip in ((6, -1), (10, 1)):  # feelers with glowing tips
        s.put(x, top, SLIME_DARK)
        s.put(x + tip, top - 1, SLIME_DARK)
        s.put(x + tip, top - 2, GLOW)
    s.put(4, round(top) + 2, SLIME_LIGHT)
    s.put(5, round(top) + 1, SLIME_LIGHT)
    s.ellipse(10.0, top + 3.6, 2.4, 2.2, EYE)
    s.rect(11, round(top + 3), 11, round(top + 4), PUPIL)
    s.put(10, round(top + 2), GLINT)
    s.outline()


def draw_crawler_squashed(s):
    s.ellipse(8.0, 14.2, 7.2, 1.9, SLIME)
    s.rect(2, 15, 14, 15, SLIME_DARK)
    s.put(4, 13, SLIME_LIGHT)
    for x, y in ((9, 13), (11, 13), (10, 14), (9, 15), (11, 15)):  # a dazed X eye
        s.put(x, y, PUPIL)
    s.outline()


BAT = rgb(110, 76, 170)
BAT_DARK = rgb(70, 44, 120)
BAT_LIGHT = rgb(168, 130, 230)
BAT_EYE = rgb(120, 255, 220)
WING_UP = ["#.....", "##....", "###...", "####..", "#####.", ".#####"]


def draw_bat(s, wings_up, stunned=False):
    """A space bat seen from the front, with one big glowing eye, so it needs no flipping."""
    wing = WING_UP if wings_up else WING_UP[::-1]
    y = 2 if wings_up else 7
    s.mask(wing, 0, y, {"#": BAT_DARK})
    s.mask([row[::-1] for row in wing], 10, y, {"#": BAT_DARK})
    for x0 in (1, 13):  # wing bones
        s.put(x0 + (1 if x0 == 1 else 0), y + (2 if wings_up else 3), BAT_LIGHT)
    s.ellipse(8.0, 8.5, 3.9, 3.6, BAT)
    s.points(((5, 4), (5, 5), (10, 4), (10, 5)), BAT)  # ears
    s.put(6, 6, BAT_LIGHT)
    s.put(5, 7, BAT_LIGHT)
    if stunned:
        s.points(((6, 8), (7, 9), (8, 8), (9, 9), (10, 8)), PUPIL)
    else:
        s.ellipse(8.0, 8.4, 2.1, 2.0, EYE)
        s.points(((7, 8), (8, 8), (7, 9), (8, 9)), BAT_EYE)
        s.put(8, 8, PUPIL)
        s.put(7, 7, GLINT)
        s.points(((6, 11), (10, 11)), EYE)  # fangs
    s.outline()


SPIKER = rgb(214, 124, 64)
SPIKER_DARK = rgb(150, 76, 44)
SPIKER_LIGHT = rgb(244, 186, 126)
CRYSTAL = (rgb(170, 250, 255), rgb(72, 196, 236), rgb(38, 110, 184))
CRYSTAL_PINK = (rgb(255, 186, 244), rgb(220, 96, 214), rgb(130, 50, 160))


def draw_spiker(s, step):
    """A crystal-backed alien hedgehog facing right: its spikes hurt, so it cannot be stomped."""
    for i, x in enumerate((5, 10)):
        dx = step if i == 0 else -step
        s.rect(x + dx, 14, x + dx + 1, 15, OUTLINE)
    for i, (x, top) in enumerate(((2, 6), (4, 3), (6, 4), (8, 2), (10, 5))):  # crystal spikes
        light, mid, dark = CRYSTAL_PINK if i % 2 else CRYSTAL
        s.put(x, top, light)
        s.rect(x, top + 1, x, 9, light)
        s.rect(x + 1, top + 1, x + 1, 9, mid)
        s.put(x + 1, 9, dark)
    s.ellipse(7.5, 11.0, 6.2, 3.8, SPIKER)
    s.rect(3, 13, 11, 14, SPIKER_DARK)
    s.rect(4, 9, 8, 9, SPIKER_LIGHT)
    s.rect(11, 10, 13, 12, SPIKER_LIGHT)  # snout
    s.put(14, 11, rgb(255, 120, 150))
    s.put(11, 9, GLOW)
    s.put(12, 9, PUPIL)
    s.outline()


SPRING_RED = rgb(226, 72, 72)
SPRING_LIGHT = rgb(255, 150, 140)


def draw_spring(s, pressed):
    top = 11 if pressed else 7
    s.rect(3, 14, 12, 15, STEEL_DARK)  # base
    s.rect(3, 14, 12, 14, STEEL)
    for y in range(top + 2, 14):  # coil
        s.rect(5, y, 10, y, STEEL_LIGHT if y % 2 else STEEL)
    s.rect(2, top, 13, top + 1, SPRING_RED)  # pad
    s.rect(2, top, 13, top, SPRING_LIGHT)
    s.put(7, top + 1, SIGNAL)
    s.put(8, top + 1, SIGNAL)
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


THRUSTER = rgb(110, 240, 255)


def draw_platform(s, left, right):
    """One 16 texel piece of a hover platform; the solid part is the top 7 rows, with the glow
    of its thrusters below."""
    x0, x1 = (1 if left else 0), (14 if right else 15)
    s.rect(x0, 0, x1, 5, STEEL)
    s.rect(x0, 0, x1, 0, STEEL_LIGHT)
    s.rect(x0, 4, x1, 5, STEEL_DARK)
    for x in range(x0, x1 + 1):  # a strip of lights along the side
        if x % 4 == 1:
            s.put(x, 2, THRUSTER)
        elif x % 4 == 3:
            s.put(x, 2, STEEL_DARK)
    for x in ((x0 + 1,) if left else ()) + ((x1 - 1,) if right else ()):
        s.put(x, 1, STEEL_LIGHT)  # bolts
    s.rect(6, 6, 9, 6, STEEL_DARK)  # a thruster under each piece
    s.outline()
    s.rect(7, 7, 8, 7, THRUSTER)
    s.points(((7, 8), (8, 8)), (*THRUSTER[:3], 170))
    s.points(((7, 9), (8, 10)), (*THRUSTER[:3], 90))


COIN = rgb(250, 200, 64)
COIN_DARK = rgb(200, 128, 36)
COIN_LIGHT = rgb(255, 242, 170)
STAR = ["..#..", ".###.", "#####", ".###.", ".#.#."]


def draw_coin(s, angle):
    """A gold coin with a star, seen at an angle as it spins."""
    squeeze = abs(math.cos(angle))
    rx = max(squeeze * 5.6, 1.5)
    s.ellipse(8.0, 8.0, rx, 6.2, COIN_DARK)
    if rx > 2.5:
        s.ellipse(8.0 - 0.3, 8.0, rx - 1.0, 5.2, COIN)
        if squeeze > 0.9:
            s.mask(STAR, 6, 6, {"#": COIN_DARK})
            s.points(((8, 6), (7, 7)), COIN_LIGHT)
        else:
            s.rect(7, 6, 8, 10, COIN_DARK)
        s.put(5, 5, COIN_LIGHT)
        s.put(5, 6, COIN_LIGHT)
    else:
        s.rect(8, 3, 8, 12, COIN_LIGHT)
    s.outline()


HEART = [".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."]


def draw_heart(s, full):
    fill = rgb(232, 60, 84) if full else rgb(64, 52, 76)
    s.mask(HEART, 4, 5, {"#": fill})
    if full:
        s.put(5, 6, rgb(255, 170, 180))
        s.put(6, 6, rgb(255, 120, 140))
    s.outline()


def draw_beacon(s, on):
    """A radio beacon: the checkpoint. Its light turns cyan when it is reached."""
    s.rect(7, 7, 8, 13, STEEL_DARK)
    s.rect(7, 7, 7, 13, STEEL)
    s.points(((5, 13), (6, 12), (10, 13), (9, 12)), STEEL_DARK)  # tripod
    s.rect(3, 14, 12, 15, STEEL_DARK)
    s.rect(3, 14, 12, 14, STEEL)
    s.rect(6, 9, 9, 9, STEEL_LIGHT)  # a cross bar with a signal lamp
    s.put(9, 8, SIGNAL)
    light = rgb(92, 230, 246) if on else rgb(120, 124, 140)
    s.ellipse(7.5, 4.5, 2.5, 2.5, light)
    s.put(6, 3, rgb(230, 255, 255) if on else rgb(170, 174, 186))
    s.outline()
    if on:  # glow and radio waves
        for x, y in ((2, 1), (13, 1), (1, 4), (14, 4), (2, 7), (13, 7)):
            s.put(x, y, (150, 240, 255, 190))


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
    # 0-7, 16, 17, 29, 38, 39: unused (the astronaut and the rocket are in astronaut.png).
    draw_crawler(s.frame(8), 0)
    draw_crawler(s.frame(9), 1)
    draw_crawler_squashed(s.frame(10))
    for i in range(4):
        draw_coin(s.frame(11 + i), i * math.pi / 4)
    draw_heart(s.frame(15), True)
    draw_beacon(s.frame(18), False)
    draw_beacon(s.frame(19), True)
    draw_heart(s.frame(20), False)
    draw_sparkle(s.frame(21), 1, rgb(255, 250, 200))
    draw_sparkle(s.frame(22), 3, rgb(255, 240, 150))
    draw_sparkle(s.frame(23), 2, rgb(255, 210, 90))
    draw_bat(s.frame(24), True)
    draw_bat(s.frame(25), False)
    draw_bat(s.frame(26), False, stunned=True)
    draw_spiker(s.frame(27), 0)
    draw_spiker(s.frame(28), 1)
    draw_spring(s.frame(30), False)
    draw_spring(s.frame(31), True)
    for i in range(3):
        draw_dust(s.frame(32 + i), i)
    draw_platform(s.frame(35), True, False)
    draw_platform(s.frame(36), False, False)
    draw_platform(s.frame(37), False, True)
    return s


# --- Tiles -----------------------------------------------------------------------------------

# Verda: teal alien turf on violet soil with glowing mineral specks.
SOIL = rgb(94, 62, 104)
SOIL_DARK = rgb(68, 44, 82)
SOIL_LIGHT = rgb(126, 88, 132)
MINERAL = rgb(120, 240, 230)
TURF = rgb(64, 200, 170)
TURF_LIGHT = rgb(156, 248, 214)
TURF_DARK = rgb(30, 128, 128)
TURF_TIP = rgb(255, 150, 220)
STONE = rgb(110, 116, 142)
STONE_LIGHT = rgb(152, 158, 184)
STONE_DARK = rgb(78, 82, 106)
METAL = rgb(126, 138, 166)
METAL_LIGHT = rgb(184, 196, 222)
METAL_DARK = rgb(78, 88, 118)
STEM = rgb(40, 132, 120)
STEM_DARK = rgb(26, 92, 92)


def soil(s, seed):
    """Violet soil with darker clods, pebbles and a few glowing mineral specks."""
    rnd = random.Random(seed)
    s.rect(0, 0, 15, 15, SOIL)
    for _ in range(9):
        x, y = rnd.randrange(16), rnd.randrange(16)
        s.rect(x, y, min(x + 2, 15), y, SOIL_DARK)
    for _ in range(5):
        x, y = rnd.randrange(15), rnd.randrange(15)
        s.put(x, y, SOIL_LIGHT)
        s.put(x + 1, y + 1, SOIL_DARK)
    for _ in range(2):
        x, y = rnd.randrange(1, 15), rnd.randrange(5, 15)
        s.put(x, y, MINERAL)
        s.put(x + 1, y, rgb(70, 160, 170))


def turf_top(s, left_edge, right_edge):
    soil(s, 1)
    for x in range(16):
        depth = 3 + (1 if x % 3 == 1 else 0) + (1 if x % 7 == 3 else 0)
        s.rect(x, 0, x, depth - 1, TURF)
        s.put(x, depth - 1, TURF_DARK)
        if x % 5 == 2:
            s.put(x, depth, TURF_DARK)  # roots hanging into the soil
    s.rect(0, 0, 15, 0, TURF_LIGHT)
    for x in (3, 9, 13):
        s.put(x, 1, TURF_LIGHT)
    for x in (5, 12):
        s.put(x, 0, TURF_TIP)  # glowing tips
    if left_edge:
        s.rect(0, 0, 0, 6, TURF_DARK)
        s.rect(1, 0, 1, 4, TURF)
    if right_edge:
        s.rect(15, 0, 15, 6, TURF_DARK)
        s.rect(14, 0, 14, 4, TURF)


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


CLOUD = rgb(250, 252, 255)
CLOUD_SHADE = rgb(214, 220, 246)
CLOUD_DARK = rgb(184, 186, 232)


def crystal(s, x, top, colors):
    """A two-texel-wide crystal from row @p top to the bottom, with a pointed tip."""
    light, mid, dark = colors
    s.put(x, top - 1, light)
    s.rect(x, top, x, 15, light)
    s.rect(x + 1, top, x + 1, 15, mid)
    s.put(x + 1, 15, dark)


def glow_dot(s, x, y, color):
    """A glowing texel with a faint halo."""
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        if s.get(x + dx, y + dy)[3] == 0:
            s.put(x + dx, y + dy, (*color[:3], 90))
    s.put(x, y, color)


def make_tiles():
    s = Sheet(8, 5)
    turf_top(s.frame(0), False, False)
    soil(s.frame(1), 2)
    turf_top(s.frame(2), True, False)
    turf_top(s.frame(3), False, True)
    stone(s.frame(4), True)
    stone(s.frame(5), False)
    # 6: one-way plank: a metal girder; only the top part is solid.
    p = s.frame(6)
    p.rect(0, 0, 15, 4, METAL)
    p.rect(0, 0, 15, 0, METAL_LIGHT)
    p.rect(0, 4, 15, 4, METAL_DARK)
    for x in range(0, 16, 4):  # truss holes
        p.rect(x + 1, 2, x + 2, 2, METAL_DARK)
    for x in (2, 13):
        p.rect(x, 5, x, 7, METAL_DARK)  # brackets
    # 7: spikes on the bottom half: three crystal shards, lit from the left.
    sp = s.frame(7)
    spike = ["..#..", "..#..", ".l#d.", ".l#d.", "ll#dd", "ll#dd", "ll#dd"]
    for base in (0, 5, 10):
        sp.mask(spike, base + 1 if base < 10 else base, 8,
                {"l": rgb(214, 220, 240), "#": rgb(250, 252, 255), "d": rgb(130, 136, 170)})
    sp.rect(0, 15, 15, 15, rgb(90, 94, 120))
    sp.outline()
    # 8: an alien coral bush.
    b = s.frame(8)
    for x, top, lean in ((3, 8, -1), (6, 5, 0), (9, 7, 1), (12, 9, 1)):
        for y in range(top, 16):
            b.put(x + (lean if y < top + 3 else 0), y, TURF if x % 2 else TURF_DARK)
        b.put(x + lean, top - 1, TURF_LIGHT)
    b.ellipse(8.0, 14.0, 6.5, 2.4, TURF_DARK)
    b.ellipse(7.5, 13.2, 4.5, 1.8, TURF)
    b.outline()
    for x, y in ((2, 7), (6, 4), (13, 8)):
        glow_dot(b, x, y, TURF_TIP)
    # 9: a glow-bulb flower on a curling stem.
    f = s.frame(9)
    for x, y in ((7, 15), (7, 14), (7, 13), (6, 12), (6, 11), (7, 10), (8, 9), (8, 8)):
        f.put(x, y, STEM)
    f.points(((5, 13), (4, 12), (9, 12), (10, 11)), TURF)  # leaves
    f.ellipse(8.5, 6.0, 2.4, 2.4, rgb(255, 228, 120))
    f.put(8, 5, rgb(255, 255, 230))
    f.outline()
    glow_dot(f, 11, 4, rgb(255, 240, 160))
    glow_dot(f, 5, 5, rgb(255, 240, 160))
    # 10: a moon rock with craters.
    r = s.frame(10)
    r.ellipse(8.0, 12.5, 6.0, 3.6, STONE)
    r.ellipse(6.5, 11.5, 3.0, 1.6, STONE_LIGHT)
    r.ellipse(10.0, 13.0, 1.4, 1.0, STONE_DARK)
    r.ellipse(5.5, 13.5, 1.0, 0.8, STONE_DARK)
    r.rect(3, 15, 12, 15, STONE_DARK)
    r.outline()
    # 11: a sign post with a glowing arrow screen.
    g = s.frame(11)
    g.rect(7, 9, 8, 15, METAL_DARK)
    g.rect(7, 9, 7, 15, METAL)
    g.rect(2, 3, 13, 9, METAL_DARK)
    g.rect(3, 4, 12, 8, rgb(24, 40, 70))
    g.mask(["....#..", "....##.", "#######", "....##.", "....#.."], 4, 4, {"#": MINERAL})
    g.outline()
    # 12: a supply crate with hazard stripes.
    c = s.frame(12)
    c.rect(0, 0, 15, 15, METAL)
    c.rect(0, 0, 15, 0, METAL_LIGHT)
    c.rect(0, 0, 0, 15, METAL_LIGHT)
    c.rect(15, 0, 15, 15, METAL_DARK)
    c.rect(0, 15, 15, 15, METAL_DARK)
    for x in range(1, 15):
        c.rect(x, 2, x, 3, SIGNAL if (x // 2) % 2 == 0 else OUTLINE)
        c.rect(x, 12, x, 13, SIGNAL if (x // 2) % 2 == 0 else OUTLINE)
    c.mask(STAR, 6, 6, {"#": METAL_LIGHT})
    # 13: a hull panel with rivets.
    k = s.frame(13)
    k.rect(0, 0, 15, 15, METAL)
    k.rect(0, 0, 15, 0, METAL_LIGHT)
    k.rect(0, 7, 15, 7, METAL_DARK)
    k.rect(0, 15, 15, 15, METAL_DARK)
    for x, y in ((2, 2), (13, 2), (2, 10), (13, 10)):
        k.put(x, y, METAL_LIGHT)
        k.put(x + 1, y + 1, METAL_DARK)
    turf_top(s.frame(14), True, True)
    # 15: lava with a bright, wavy surface.
    lv = s.frame(15)
    lv.rect(0, 4, 15, 15, rgb(226, 82, 34))
    lv.rect(0, 11, 15, 15, rgb(190, 54, 30))
    for x in range(16):
        top = 3 + round(math.sin(x * 0.9))
        lv.rect(x, top, x, 5, rgb(255, 170, 60))
        lv.put(x, top, rgb(255, 230, 140))
    for x, y in ((3, 8), (10, 10), (13, 7), (6, 13)):
        lv.put(x, y, rgb(255, 200, 90))
    # 16: crystals.
    cr = s.frame(16)
    crystal(cr, 3, 10, CRYSTAL_PINK)
    crystal(cr, 7, 4, CRYSTAL)
    crystal(cr, 11, 8, CRYSTAL)
    cr.outline()
    cr.put(7, 5, GLINT)
    # 17: two glowing alien mushrooms.
    m = s.frame(17)
    m.rect(9, 9, 10, 15, rgb(226, 230, 240))
    m.rect(9, 9, 9, 15, rgb(170, 176, 200))
    m.ellipse(9.5, 8.0, 5.0, 3.0, rgb(80, 200, 255))
    m.rect(5, 9, 14, 9, rgb(40, 120, 200))
    m.rect(4, 12, 4, 15, rgb(226, 230, 240))
    m.ellipse(4.0, 11.5, 2.6, 1.6, rgb(220, 110, 255))
    m.outline()
    for x, y in ((7, 6), (11, 7), (9, 5), (3, 11)):
        m.put(x, y, rgb(230, 255, 255))
    # 18: curling alien tendrils.
    t = s.frame(18)
    for x, top, curl in ((3, 11, 1), (6, 7, -1), (9, 9, 1), (12, 11, -1)):
        t.rect(x, top, x, 15, TURF_DARK if x % 2 else TURF)
        t.put(x + curl, top - 1, TURF)
        t.put(x + curl, top - 2, TURF_LIGHT)
    t.outline()
    # 19: a fire pit of basalt with flames (Pyros).
    tc = s.frame(19)
    tc.ellipse(7.5, 6.5, 3.6, 5.4, rgb(240, 100, 36))
    tc.ellipse(7.5, 8.0, 2.0, 3.6, rgb(255, 200, 90))
    tc.ellipse(7.5, 9.5, 1.0, 1.8, rgb(255, 250, 220))
    tc.ellipse(7.5, 13.5, 5.5, 2.5, rgb(58, 44, 50))
    tc.rect(2, 12, 13, 12, rgb(98, 74, 74))
    tc.outline()
    # 20: a cloud to stand on (one-way, like a plank); its edges match so clouds join up.
    cl = s.frame(20)
    cl.rect(0, 2, 15, 7, CLOUD)
    cl.ellipse(4.0, 2.5, 3.6, 2.5, CLOUD)
    cl.ellipse(12.0, 2.0, 3.8, 2.6, CLOUD)
    cl.rect(0, 6, 15, 8, CLOUD_SHADE)
    cl.ellipse(8.0, 8.5, 5.0, 1.5, CLOUD_SHADE)
    cl.rect(3, 9, 12, 9, CLOUD_DARK)
    cl.points(((3, 1), (11, 0), (12, 0), (2, 2)), GLINT)
    # 21: a spiral alien tree.
    p = s.frame(21)
    for y in range(6, 16):
        x = 7 + round(math.sin(y * 0.8))
        p.rect(x, y, x + 1, y, STEM if y % 3 else STEM_DARK)
    p.ellipse(8.0, 4.0, 4.5, 3.4, rgb(150, 90, 220))
    p.ellipse(7.0, 3.2, 2.5, 1.6, rgb(206, 150, 255))
    p.outline()
    for x, y in ((4, 6), (12, 6), (8, 7)):
        glow_dot(p, x, y, rgb(255, 210, 120))
    # 22-25: the ground of the Crystal Moon (ice) and of Pyros (basalt), each with a top.
    ice(s.frame(22), True)
    ice(s.frame(23), False)
    basalt(s.frame(24), True)
    basalt(s.frame(25), False)
    # 26: an alien plant with glowing bulbs.
    a = s.frame(26)
    for x, top, bend in ((5, 6, -1), (8, 3, 0), (11, 7, 1)):
        for y in range(top + 2, 16):
            a.put(x + (bend if y < top + 5 else 0), y, STEM)
        a.ellipse(x + bend + 0.5, top + 1.5, 1.8, 1.8, rgb(120, 250, 220) if x != 8 else
                  rgb(250, 140, 240))
        a.put(x + bend, top, rgb(240, 255, 250))
    a.rect(4, 15, 12, 15, STEM_DARK)
    a.outline()
    # 27-28: the ground of Nimbus: pink moss on layered sky rock.
    moss(s.frame(27), True)
    moss(s.frame(28), False)
    # 29: a satellite dish.
    d = s.frame(29)
    d.rect(7, 10, 8, 15, METAL_DARK)
    d.rect(4, 14, 11, 15, METAL_DARK)
    d.rect(4, 14, 11, 14, METAL)
    for i, (x0, x1) in enumerate(((10, 12), (7, 12), (5, 11), (4, 9), (4, 7), (5, 6))):
        d.rect(x0, 3 + i, x1, 3 + i, METAL_LIGHT if i < 3 else METAL)
    d.rect(8, 5, 9, 6, METAL_DARK)  # the feed
    d.put(10, 4, SIGNAL)
    d.outline()
    # 30: a glowing crystal geode.
    q = s.frame(30)
    q.ellipse(8.0, 12.5, 6.0, 3.6, rgb(84, 70, 110))
    q.ellipse(8.0, 12.0, 3.8, 2.2, rgb(40, 30, 64))
    for x, top in ((6, 9), (8, 8), (10, 10)):
        q.rect(x, top, x, 13, CRYSTAL_PINK[0] if x == 8 else CRYSTAL[0])
        q.rect(x + 1, top + 1, x + 1, 13, CRYSTAL_PINK[1] if x == 8 else CRYSTAL[1])
    q.outline()
    nimbus_decor(s)
    pyros_decor(s)
    return s


PUFF = rgb(255, 228, 244)
PUFF_SHADE = rgb(236, 176, 222)
BELL = rgb(200, 150, 255)
BELL_LIGHT = rgb(236, 210, 255)
CHAR = rgb(46, 32, 36)
CHAR_LIGHT = rgb(78, 56, 58)
SMOKE = (120, 100, 110, 150)


def nimbus_decor(s):
    """31-33: the plants of the cloud planet, in its pinks and lilacs."""
    p = s.frame(31)  # puffballs: dandelion heads on thin stems
    for x, top, r in ((5, 5, 3.0), (11, 8, 2.4)):
        for y in range(round(top + r), 16):
            p.put(x + (1 if y > 12 else 0), y, SKYROCK_DARK)
        p.ellipse(x + 0.5, top + 0.5, r, r, PUFF)
        p.ellipse(x + 0.9, top + 1.0, r * 0.6, r * 0.6, PUFF_SHADE)
        p.put(x - 1, top - 1, GLINT)
    p.points(((2, 13), (14, 12)), MOSS)  # leaves
    p.outline()
    for x, y in ((9, 2), (13, 4), (2, 3)):  # seeds drifting off
        p.put(x, y, (*PUFF[:3], 170))
    w = s.frame(32)  # a cloud shrub: puffs of cloud on a short stem
    w.rect(7, 12, 8, 15, SKYROCK_DARK)
    for cx, cy, r in ((5.0, 10.0, 3.0), (10.5, 9.5, 3.4), (8.0, 7.0, 3.2), (12.5, 12.0, 2.2)):
        w.ellipse(cx, cy, r, r * 0.85, CLOUD)
    for cx, cy, r in ((5.5, 11.0, 2.0), (11.0, 11.0, 2.2)):
        w.ellipse(cx, cy, r, r * 0.6, CLOUD_SHADE)
    w.outline(CLOUD_DARK)
    w.points(((7, 5), (6, 6)), GLINT)
    b = s.frame(33)  # a bell flower: an arching stem with hanging bells
    b.rect(5, 6, 5, 15, SKYROCK_DARK)
    b.points(((6, 5), (7, 4), (8, 3), (9, 3), (10, 3), (11, 4), (12, 5), (4, 9), (3, 9)),
             SKYROCK_DARK)
    for x0, y0 in ((7, 5), (11, 6), (2, 10)):
        b.mask([".#.", "###", "###"], x0, y0, {"#": BELL})
        b.points(((x0, y0 + 1), (x0 + 1, y0)), BELL_LIGHT)
    b.points(((6, 14), (4, 13)), MOSS)
    b.outline()
    for x0, y0 in ((7, 5), (11, 6), (2, 10)):
        b.put(x0 + 1, y0 + 3, GLOW)  # glowing clappers


def pyros_decor(s):
    """34-36: the lava planet: burnt trees, smoking vents and glowing boulders."""
    t = s.frame(34)  # a charred tree with embers on its branches
    t.rect(7, 7, 8, 15, CHAR)
    t.rect(7, 7, 7, 15, CHAR_LIGHT)
    for x, y in ((6, 6), (5, 5), (4, 4), (9, 5), (10, 4), (11, 3), (12, 3), (8, 6), (8, 3),
                 (8, 4), (8, 5), (3, 4), (6, 9), (5, 8), (9, 10), (10, 9)):
        t.put(x, y, CHAR)
    t.rect(5, 15, 10, 15, CHAR)
    t.outline()
    for x, y in ((3, 4), (12, 3), (8, 3), (5, 8)):
        t.put(x, y, MAGMA)
    t.put(7, 12, MAGMA)
    v = s.frame(35)  # a lava vent: a little cone with a glowing mouth
    for i, y in enumerate(range(10, 16)):
        v.rect(6 - i, y, 9 + i, y, BASALT if i > 1 else BASALT_LIGHT)
    v.rect(6, 10, 9, 10, MAGMA)
    v.rect(7, 10, 8, 10, MAGMA_LIGHT)
    v.rect(7, 11, 8, 12, MAGMA)
    v.outline()
    for cx, cy, r in ((7.5, 7.5, 1.6), (8.5, 4.5, 2.0), (7.0, 1.8, 1.6)):  # smoke
        v.ellipse(cx, cy, r, r, SMOKE)
    m = s.frame(36)  # a basalt boulder with glowing cracks
    m.ellipse(8.0, 11.5, 6.5, 4.6, BASALT)
    m.ellipse(6.5, 9.5, 3.0, 1.6, BASALT_LIGHT)
    m.rect(2, 15, 13, 15, BASALT_DARK)
    for x, y in ((5, 12), (6, 13), (7, 12), (8, 11), (9, 12), (10, 13), (11, 12), (8, 10)):
        m.put(x, y, MAGMA)
    m.put(8, 11, MAGMA_LIGHT)
    m.outline()


ICE = rgb(150, 200, 236)
ICE_LIGHT = rgb(214, 238, 255)
ICE_DARK = rgb(96, 146, 198)
FROST = rgb(244, 250, 255)
BASALT = rgb(58, 44, 50)
BASALT_LIGHT = rgb(98, 74, 74)
BASALT_DARK = rgb(36, 26, 32)
MAGMA = rgb(255, 122, 40)
MAGMA_LIGHT = rgb(255, 210, 110)
MOSS = rgb(250, 156, 206)
MOSS_LIGHT = rgb(255, 210, 236)
MOSS_DARK = rgb(204, 100, 168)
SKYROCK = rgb(120, 96, 166)
SKYROCK_LIGHT = rgb(154, 128, 198)
SKYROCK_DARK = rgb(90, 68, 132)


def ice(s, top):
    """Ice rock: blocks with light edges, cracks and a frozen crystal; the top one has a cap of
    frost."""
    s.rect(0, 0, 15, 15, ICE)
    for y in (0, 8):
        offset = 0 if y == 0 else 5
        s.rect(0, y + 7, 15, y + 7, ICE_DARK)
        s.rect((offset + 9) % 16, y, (offset + 9) % 16, y + 6, ICE_DARK)
        s.rect(0, y, 15, y, ICE_LIGHT)
    for x, y in ((3, 3), (4, 4), (5, 5), (12, 11), (13, 12), (6, 12)):
        s.put(x, y, ICE_LIGHT)
    s.points(((12, 3), (12, 4), (13, 4)), rgb(190, 250, 255))  # a crystal frozen inside
    s.put(13, 5, CRYSTAL[1])
    if top:
        for x in range(16):
            depth = 2 + (x * 7 % 5 == 0) + (x % 6 == 2)
            s.rect(x, 0, x, depth, FROST)
        s.put(4, 4, FROST)
        s.put(11, 5, FROST)
        s.points(((2, 0), (9, 0)), GLINT)


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


def moss(s, top):
    """Lilac sky rock in soft layers; the top one has a cushion of pink moss."""
    s.rect(0, 0, 15, 15, SKYROCK)
    for y, offset in ((4, 0), (9, 5), (14, 2)):
        for x in range(16):
            s.put(x, y + (1 if (x + offset) % 7 < 2 else 0), SKYROCK_DARK)
    for x, y in ((2, 2), (11, 6), (5, 11), (14, 12)):
        s.put(x, y, SKYROCK_LIGHT)
        s.put(x + 1, y, SKYROCK_LIGHT)
    if top:
        for x in range(16):
            depth = 3 + (x % 4 == 1) + (x % 5 == 3)
            s.rect(x, 0, x, depth - 1, MOSS)
            s.put(x, depth - 1, MOSS_DARK)
        s.rect(0, 0, 15, 0, MOSS_LIGHT)
        s.points(((4, 1), (10, 1), (14, 1)), MOSS_LIGHT)


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


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def dither(x, y):
    """Ordered dithering threshold for texel (x, y), in [-0.5, 0.5)."""
    return BAYER[y % 4][x % 4] / 16.0 - 0.5


def periodic_noise(width, height, cells, seed):
    """Smooth value noise that tiles in both directions: a grid of random values, cells across
    the width, interpolated with smoothstep."""
    rnd = random.Random(seed)
    rows = max(1, round(cells * height / width))
    grid = [[rnd.random() for _ in range(cells)] for _ in range(rows)]
    out = []
    for y in range(height):
        gy = y * rows / height
        y0, fy = int(gy), gy - int(gy)
        fy = fy * fy * (3 - 2 * fy)
        row = []
        for x in range(width):
            gx = x * cells / width
            x0, fx = int(gx), gx - int(gx)
            fx = fx * fx * (3 - 2 * fx)
            a, b = grid[y0 % rows][x0 % cells], grid[y0 % rows][(x0 + 1) % cells]
            c, d = grid[(y0 + 1) % rows][x0 % cells], grid[(y0 + 1) % rows][(x0 + 1) % cells]
            top, bottom = a + (b - a) * fx, c + (d - c) * fx
            row.append(top + (bottom - top) * fy)
        out.append(row)
    return out


def fbm(width, height, seed, octaves=4, cells=4):
    """Several octaves of periodic_noise, in [0, 1]."""
    layers = [periodic_noise(width, height, cells * 2 ** o, seed + o) for o in range(octaves)]
    weights = [0.5 ** o for o in range(octaves)]
    total = sum(weights)
    return [[sum(w * layer[y][x] for w, layer in zip(weights, layers)) / total
             for x in range(width)] for y in range(height)]


def make_nebula():
    """Soft clouds of gas in pink and blue, 128x128, tiling in both directions; dithered into a
    few levels of opacity so they suit the pixel art. The game tints them per planet."""
    size = 128
    density = fbm(size, size, 21, octaves=4, cells=3)
    hue = fbm(size, size, 41, octaves=2, cells=2)
    levels = [0, 30, 58, 88, 120]
    image = [[TRANSPARENT] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            d = max(0.0, min(1.0, (density[y][x] - 0.40) / 0.30))
            q = max(0, min(4, int(d * 4 + 0.5 + dither(x, y))))
            if q:
                color = mixed((130, 160, 255), (255, 140, 210), max(0.0, min(1.0, hue[y][x] * 2 - 0.5)))
                color = mixed(color, (255, 255, 255), 0.12 * q)
                image[y][x] = (*color[:3], levels[q])
    return image


def make_stars():
    """Stars on a transparent 128x128 tile that repeats in both directions: faint and bright
    ones, a few with a cross and a few big four-pointed sparkles."""
    size = 128
    rnd = random.Random(11)
    image = [[TRANSPARENT] * size for _ in range(size)]
    tints = ((255, 255, 255), (200, 220, 255), (255, 236, 200), (255, 210, 240))
    for i in range(170):
        x, y = rnd.randrange(size), rnd.randrange(size)
        tint = rnd.choice(tints)
        image[y][x] = (*tint, rnd.choice((70, 110, 160, 220, 255)))
        if i % 14 == 0:  # a few bright ones with a cross
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                image[(y + dy) % size][(x + dx) % size] = (*tint, 120)
    for _ in range(4):  # big sparkles
        x, y = rnd.randrange(size), rnd.randrange(size)
        tint = rnd.choice(tints)
        image[y][x] = (255, 255, 255, 255)
        for r, alpha in ((1, 220), (2, 140), (3, 70)):
            for dx, dy in ((r, 0), (-r, 0), (0, r), (0, -r)):
                image[(y + dy) % size][(x + dx) % size] = (*tint, alpha)
    return image


def silhouette(solid, base, rim, dark, light_side=None):
    """Colours a mask of solid texels: a rim along the top edges, a lit left side and a dark
    right side."""
    height, width = len(solid), len(solid[0])
    image = [[TRANSPARENT] * width for _ in range(height)]
    for y in range(height):
        for x in range(width):
            if not solid[y][x]:
                continue
            above = y > 0 and solid[y - 1][x]
            left, right = solid[y][(x - 1) % width], solid[y][(x + 1) % width]
            color = base
            if not right:
                color = dark
            if not left and light_side is not None:
                color = light_side
            if not above:
                color = rim
            image[y][x] = color
    return image


def make_spires():
    """Planet Verda's far hills: violet rock spires, mesas and an arch, 256x80, tiling
    horizontally, with crystals glinting in the rock."""
    width, height = 256, 80
    solid = [[False] * width for _ in range(height)]
    tops = [round(64 + 4 * math.sin(2 * math.pi * x / width) + 2 * math.sin(6 * math.pi * x / width))
            for x in range(width)]
    for cx, half, rise in ((20, 13, 30), (92, 9, 22), (168, 16, 26), (232, 6, 18)):  # mesas
        for dx in range(-half - 8, half + 9):
            slope = max(0, abs(dx) - half) * 2
            x = (cx + dx) % width
            tops[x] = min(tops[x], height - rise + slope - (1 if abs(dx) % 5 == 0 else 0))
    for x in range(width):
        for y in range(tops[x], height):
            solid[y][x] = True
    for cx, top, half, cap in ((46, 14, 2, 5), (58, 26, 1, 4), (122, 8, 2, 6), (4, 32, 1, 3),
                               (146, 22, 1, 4), (200, 12, 2, 5), (214, 30, 1, 3)):
        for y in range(top, height):  # hoodoos: a thin column with a cap of harder rock
            w = half + (1 if y > top + 20 else 0) + (1 if y > top + 32 else 0)
            for dx in range(-w, w + 1):
                solid[y][(cx + dx) % width] = True
        for y in range(top - 3, top + 2):
            for dx in range(-cap, cap + 1):
                if (dx / (cap + 0.5)) ** 2 + ((y - top + 0.5) / 2.6) ** 2 <= 1.0:
                    solid[y][(cx + dx) % width] = True
    # An arch: a band of rock over a hole.
    for y in range(28, height):
        for x in range(66, 88):
            dx, dy = (x + 0.5 - 77) / 11.0, (y + 0.5 - 52) / 22.0
            r = dx * dx + dy * dy
            solid[y][x] = solid[y][x] or (0.55 <= r <= 1.0 and y < 52) or \
                (y >= 52 and 0.6 < abs(dx) <= 1.0)
    image = silhouette(solid, rgb(64, 66, 124), rgb(124, 132, 206), rgb(46, 46, 96),
                       rgb(84, 88, 156))
    for y in range(height):  # strata
        for x in range(width):
            if image[y][x] == rgb(64, 66, 124) and (y + round(2 * math.sin(16 * math.pi * x / width))) % 9 == 0:
                image[y][x] = rgb(56, 56, 112)
    rnd = random.Random(31)
    for _ in range(36):  # glinting crystals
        x, y = rnd.randrange(width), rnd.randrange(30, height)
        if image[y][x][3] and image[y - 1][x][3]:
            image[y][x] = rgb(120, 240, 230) if rnd.random() < 0.6 else rgb(255, 150, 220)
    return image


def make_flora():
    """Planet Verda's near forest: giant mushroom trees and bulb stalks in dark teal, with glowing
    spots, 256x64, tiling horizontally."""
    width, height = 256, 64
    solid = [[False] * width for _ in range(height)]
    glow = []
    for x in range(width):  # the ground, with bumps
        top = round(54 + 1.5 * math.sin(18 * math.pi * x / width) +
                    math.sin(46 * math.pi * x / width))
        for y in range(top, height):
            solid[y][x] = True
    rnd = random.Random(17)
    for cx, stem_top, cap_w in ((14, 22, 11), (52, 14, 13), (90, 28, 9), (124, 20, 10),
                                (160, 16, 12), (196, 26, 8), (230, 12, 14)):
        for y in range(stem_top, 58):  # a leaning stem, wider at the foot
            lean = round(2 * math.sin((y - stem_top) * 0.09))
            w = 1 + (1 if y > 46 else 0)
            for dx in range(-w, w + 1):
                solid[y][(cx + lean + dx) % width] = True
        for y in range(stem_top - 7, stem_top + 2):  # the cap
            for dx in range(-cap_w - 1, cap_w + 2):
                ex, ey = dx / (cap_w + 0.5), (y - stem_top + 0.5) / 7.0
                if ex * ex + ey * ey <= 1.0 and ey <= 0.25:
                    solid[y][(cx + dx) % width] = True
        for _ in range(cap_w // 2):
            glow.append(((cx + rnd.randint(-cap_w + 2, cap_w - 2)) % width,
                         stem_top - rnd.randint(1, 5)))
        glow.append(((cx - cap_w + 1) % width, stem_top + 1))
        glow.append(((cx + cap_w - 1) % width, stem_top + 1))
    for cx in (30, 36, 70, 76, 106, 142, 178, 184, 212, 250):  # bulb stalks
        top = rnd.randint(30, 44)
        for y in range(top, 58):
            solid[y][(cx + round(math.sin(y * 0.3))) % width] = True
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                solid[top + dy][(cx + dx) % width] = True
        glow.append((cx, top))
    image = silhouette(solid, rgb(22, 58, 74), rgb(44, 104, 112), rgb(16, 44, 60))
    for x, y in glow:
        color = rnd.choice((rgb(120, 250, 230), rgb(255, 150, 220), rgb(255, 230, 130)))
        image[y][x] = color
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = (x + dx) % width, y + dy
            if image[ny][nx][3]:
                image[ny][nx] = mixed(image[ny][nx], color, 0.35)
    return image


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
    """The inside of the Crystal Moon: a dark wall of round stones with glowing crystal veins,
    64x64, tiling in both directions."""
    rnd = random.Random(5)
    image = [[(26, 28, 52, 255)] * 64 for _ in range(64)]
    for _ in range(26):
        cx, cy, r = rnd.uniform(0, 64), rnd.uniform(0, 64), rnd.uniform(5, 10)
        tone = rnd.randrange(36, 52)
        wrapped_ellipse(image, cx, cy, r * 1.6, r, (20, 20, 40, 255), wrap_y=True)
        wrapped_ellipse(image, cx - 0.5, cy - 0.5, r * 1.6 - 1.2, r - 1.2,
                        (tone, tone + 4, tone + 30, 255), wrap_y=True)
        wrapped_ellipse(image, cx - r * 0.5, cy - r * 0.5, r * 0.5, r * 0.2,
                        (tone + 14, tone + 20, tone + 48, 255), wrap_y=True)
    for _ in range(6):  # glowing crystal veins
        x, y = rnd.randrange(64), rnd.randrange(64)
        color = (120, 230, 255) if rnd.random() < 0.7 else (240, 140, 255)
        for i in range(rnd.randint(4, 7)):
            px, py = (x + i) % 64, (y - i // 2) % 64
            for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                qx, qy = (px + dx) % 64, (py + dy) % 64
                image[qy][qx] = mixed(image[qy][qx], color, 0.35)
        for i in range(rnd.randint(4, 7)):
            image[(y - i // 2) % 64][(x + i) % 64] = (*color, 255)
    return image


def make_crystals():
    """Giant crystals growing in the Crystal Moon's cave, 256x72, tiling horizontally: see-through
    prisms on a dark rock floor."""
    width, height = 256, 72
    image = [[TRANSPARENT] * width for _ in range(height)]
    rnd = random.Random(23)
    floor = [round(62 + 2 * math.sin(12 * math.pi * x / width) + math.sin(34 * math.pi * x / width))
             for x in range(width)]
    clusters = [(14, 0), (54, 1), (100, 0), (148, 1), (196, 0), (234, 1)]
    for cx, pink in clusters:
        for _ in range(rnd.randint(3, 5)):
            bx = cx + rnd.randint(-7, 7)
            w = rnd.randint(3, 6)
            h = rnd.randint(14, 52)
            lean = rnd.uniform(-0.35, 0.35)
            light, mid, dark = CRYSTAL_PINK if pink and rnd.random() < 0.7 else CRYSTAL
            base = floor[bx % width] + 2
            for i in range(h):
                y = base - i
                half = w if i < h - w else (h - i)
                center = bx + lean * i
                for xx in range(int(center - half), int(center + half) + 1):
                    if y < 0:
                        continue
                    left = xx < center
                    edge = xx == int(center - half) or xx == int(center + half)
                    color = (light if edge and left else dark if edge else
                             mixed(light, mid, 0.4) if left else mid)
                    image[y][xx % width] = (*color[:3], 255 if edge else 200)
    for x in range(width):  # rock floor
        for y in range(floor[x], height):
            image[y][x] = rgb(30, 30, 58) if y > floor[x] else rgb(60, 66, 110)
    return image


def make_rings():
    """The rings of Nimbus, seen from its surface: an arc across the sky, 440x120 (not tiling)."""
    width, height = 440, 120
    cx, cy, radius = 220.0, 334.0, 314.0
    bands = [(0, 3, (250, 230, 200), 90), (3, 10, (255, 240, 214), 210), (10, 12, None, 0),
             (12, 19, (240, 200, 196), 160), (19, 25, (220, 170, 200), 90)]
    image = [[TRANSPARENT] * width for _ in range(height)]
    rnd = random.Random(8)
    for y in range(height):
        for x in range(width):
            depth = radius - math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            for inner, outer, color, alpha in bands:
                if color and inner <= depth < outer:
                    a = alpha + (25 if rnd.random() < 0.15 else 0)
                    image[y][x] = (*color, min(255, a))
    return image


def make_islands():
    """Floating islands of Nimbus: pink moss on lilac rock that tapers into hanging roots and
    crystals, 256x64, tiling horizontally."""
    width, height = 256, 64
    image = [[TRANSPARENT] * width for _ in range(height)]
    rnd = random.Random(13)
    for cx, top, half, depth in ((26, 20, 16, 30), (110, 36, 10, 20), (160, 12, 5, 9),
                                 (214, 28, 13, 24)):
        for dx in range(-half, half + 1):
            t = abs(dx) / half
            bottom = top + 2 + round(depth * (1.0 - t ** 1.6)) + rnd.randint(0, 1)
            surface = top + (1 if t > 0.8 else 0)
            x = (cx + dx) % width
            for y in range(surface, bottom):
                color = SKYROCK if dx < half * 0.4 else SKYROCK_DARK
                if dx < -half * 0.5:
                    color = SKYROCK_LIGHT
                if (y - top) % 6 == 5:
                    color = SKYROCK_DARK
                image[y][x] = color
            for y in range(surface, surface + 2):  # moss on top
                image[y][x] = MOSS_LIGHT if y == surface else MOSS
            if dx % 5 == 0 and abs(dx) < half - 2:  # hanging roots
                for y in range(bottom, bottom + rnd.randint(2, 6)):
                    image[y][x] = MOSS_DARK
        for _ in range(2):  # crystals under the island
            x = (cx + rnd.randint(-half // 2, half // 2)) % width
            y = top + depth - 2
            for i in range(4):
                if y + i < height:
                    image[y + i][x] = CRYSTAL[0] if i < 2 else CRYSTAL[1]
    return image


def make_volcanoes():
    """Volcanoes with glowing lava and smoke on a dark ridge, 256x96, tiling horizontally."""
    width, height = 256, 96
    rock, rim, lava = (52, 30, 40, 255), (92, 52, 60, 255), (255, 128, 48, 255)
    warm = (110, 46, 46, 255)
    image = [[TRANSPARENT] * width for _ in range(height)]
    peaks = ((24, 40, 26), (70, 28, 20), (112, 48, 30), (170, 34, 22), (220, 52, 30))
    for cx, rise, _ in peaks:  # smoke rising and drifting to the right
        for i in range(7):
            alpha = 120 - i * 14
            wrapped_ellipse(image, cx + i * 3.0, height - rise - 4 - i * 5.5, 3 + i * 1.3,
                            2 + i * 0.8, (96, 70, 80, alpha))
    tops = [round(84 + 3 * math.sin(2 * math.pi * x / width) + 2 * math.sin(6 * math.pi * x / width))
            for x in range(width)]
    for cx, rise, spread in peaks:
        for dx in range(-spread, spread + 1):
            x = (cx + dx) % width
            top = height - 1 - round(rise * min(1.0, 1.25 * (1.0 - abs(dx) / spread)))
            tops[x] = min(tops[x], top)
    for x in range(width):
        for y in range(tops[x], height):
            image[y][x] = rim if y == tops[x] else rock
    for cx, rise, _ in peaks:  # lava running down, with a warm glow around it
        for i, dy in enumerate(range(4, rise // 2, 3)):
            for side in (-1, 1):
                x = (cx + side * (dy // 3 + i % 2)) % width
                y = height - 1 - rise + dy
                for gx, gy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if image[y + gy][(x + gx) % width] == rock:
                        image[y + gy][(x + gx) % width] = warm
                image[y][x] = lava
        for dx in range(-3, 4):  # glowing crater
            image[height - 1 - rise][(cx + dx) % width] = (255, 214, 110, 255) if abs(dx) < 2 \
                else lava
    return image


PLANET = 32


def make_planets():
    """The five planets of the route, 32x32 each with a 1-texel border like the sprite sheets:
    Verda, the Crystal Moon, Nimbus, Pyros and Earth. Lit from the top left, with a dithered
    terminator and a thin atmosphere."""
    rnd = random.Random(3)
    cell = PLANET + 2 * PADDING
    image = [[TRANSPARENT] * (cell * 5) for _ in range(cell)]

    def blobs(count, radius):
        return [(rnd.uniform(4, 28), rnd.uniform(4, 28), rnd.uniform(*radius)) for _ in range(count)]

    looks = [  # base colour, features, atmosphere
        ((64, 184, 160), [((120, 70, 140), blobs(4, (3, 6))), ((200, 255, 230), blobs(3, (1.5, 2.5)))],
         (150, 255, 230)),
        ((176, 214, 240), [((128, 166, 214), blobs(6, (1.5, 3.5))), ((90, 120, 180), blobs(4, (1, 1.8)))],
         None),
        ((206, 124, 196), "bands", (255, 200, 240)),
        ((122, 34, 36), "cracks", (255, 120, 60)),
        ((56, 112, 212), [((84, 172, 92), blobs(4, (4, 6))), ((245, 250, 255), blobs(4, (1.5, 3)))],
         (150, 210, 255)),
    ]
    for i, (base, features, air) in enumerate(looks):
        ox = i * cell + PADDING
        for y in range(PLANET):
            for x in range(PLANET):
                dx, dy = (x + 0.5 - 16) / 13.0, (y + 0.5 - 16) / 13.0
                d2 = dx * dx + dy * dy
                if d2 > 1.0:
                    continue
                color = base
                if features == "bands":
                    band = math.sin(y * 0.9 + math.sin(x * 0.3))
                    color = shade(rgb(*base), 1.15 if band > 0.6 else 0.82 if band < -0.5 else 1.0)
                elif features == "cracks":  # glowing lava along wavy lines
                    crack = math.sin(x * 0.55 + 2.2 * math.sin(y * 0.4)) + math.sin(y * 0.7 - x * 0.2)
                    color = (255, 132, 44) if abs(crack) < 0.18 else base
                else:
                    for feature, spots in features:
                        if any((x - bx) ** 2 + (y - by) ** 2 <= r * r for bx, by, r in spots):
                            color = feature
                light = -0.62 * dx - 0.62 * dy + 0.12 * dither(x, y)
                factor = 1.2 if light > 0.62 else 1.0 if light > -0.2 else 0.72 if light > -0.55 \
                    else 0.48
                pixel = shade(rgb(*color[:3]), factor)
                if air and d2 > 0.8 and light > -0.3:  # a thin, glowing atmosphere
                    pixel = mixed(pixel, air, 0.45)
                image[PADDING + y][ox + x] = pixel
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
        "hint": "Spikers cannot be stomped; ride the platforms over the lava",
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
            "II.P..c..u...c........k....II...................C.........k.......u....c.....V..IIIIII......c..k................C...c.....k.....u.............c.u.F.II",
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
            "..................................................................ooo.......................o.o.op..............................................................",
            "..................................................................~~~......................NNNNNNNN..o.o....o.o.................................ooo.............",
            "............ooo.........................o.o.o.........v......ooo.......ooo.................NNNNNNNN..~~~~...~~~~................o.o.o...........~~~.............",
            "............~~~..............................................~~~.......~~~.................NNNNNNNN.............................................................",
            "..............................p..j..w...........C...p...l..................................NNNNNNNN.................C...e..w.............l...k....p..l...F...w..",
            "...P..s.l..........p.e..w.....NNNNNNN..M......NNNNNNNNNNNNN...................l.k..p.......NNNNNNNN...............NNNNNNNNNNN..M......NNNNNNNNNNNNNNNNNNNNNNNNNN",
            "NNNNNNNNNN.......NNNNNNNNNN...NNNNNNN.........NNNNNNNNNNNNN.................NNNNNNNNN......NNNNNNNN...............NNNNNNNNNNN.........NNNNNNNNNNNNNNNNNNNNNNNNNN",
            "NNNNNNNNNN.......NNNNNNNNNN...NNNNNNN.........NNNNNNNNNNNNN.................NNNNNNNNN...V..NNNNNNNN...............NNNNNNNNNNN.........NNNNNNNNNNNNNNNNNNNNNNNNNN",
            "NNNNNNNNNN.......NNNNNNNNNN...NNNNNNN.........NNNNNNNNNNNNN.................NNNNNNNNN......NNNNNNNN...............NNNNNNNNNNN.........NNNNNNNNNNNNNNNNNNNNNNNNNN",
            "NNNNNNNNNN.......NNNNNNNNNN...NNNNNNN.........NNNNNNNNNNNNN.................NNNNNNNNN......NNNNNNNN...............NNNNNNNNNNN.........NNNNNNNNNNNNNNNNNNNNNNNNNN",
            "NNNNNNNNNN.......NNNNNNNNNN...NNNNNNN.........NNNNNNNNNNNNN.................NNNNNNNNN......NNNNNNNN...............NNNNNNNNNNN.........NNNNNNNNNNNNNNNNNNNNNNNNNN",
        ],
    },
    "level4": {
        "name": "Pyros",
        "hint": "Mind the lava and the falling embers",
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
            "RR..............................RRRRRRR......m....o.o.o...............RRRRRRR..........................................................o.o.o..................RR",
            "RR...........................................RR.......................RRRRRRR......................ooo...o..H.................................................RR",
            "RR.P..t......t....^^^....k..t..m.....t...n...RR..........t..C...n..V..RRRRRRRx..m.....k......n...^^....^^......n..C...j...n..x...x..............e..x..t...F..tRR",
            "RRRRRRRRRR...RRRRRRRRRRRRRRRRRRRRRRRRRRRRR...RR..M......RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR..M.......RRRRRRRRRRRRRRRRRR",
            "RRRRRRRRRRLLLRRRRRRRRRRRRRRRRRRRRRRRRRRRRRLLLRRLLLLLLLLLRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRLLLLLLLLLLRRRRRRRRRRRRRRRRRR",
            "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
            "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR",
        ],
    },
}

SOLID_GROUND = "#"
PLANK = "-"
DECOR = {"b": 8, "f": 9, "r": 10, "s": 11, "c": 16, "u": 17, "g": 18, "t": 19, "y": 21, "a": 26,
         "d": 29, "q": 30, "p": 31, "w": 32, "l": 33, "x": 34, "n": 35, "m": 36}
TOPPED = {"=": (4, 5), "I": (22, 23), "R": (24, 25), "N": (27, 28)}  # (top, inside): stone, ice,
# basalt, moss
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
    solid = [0, 1, 2, 3, 4, 5, 12, 13, 14, 22, 23, 24, 25, 27, 28]
    tiles = []
    for i in range(40):
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
        "image": "../textures/tiles.png", "imagewidth": CELL * 8, "imageheight": CELL * 5,
        "tilewidth": FRAME, "tileheight": FRAME, "tilecount": 40, "columns": 8,
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
    write_png(textures / "astronaut.png", make_big().extruded())
    write_png(textures / "platformer.png", make_characters().extruded())
    write_png(textures / "tiles.png", make_tiles().extruded())
    for name, make in (("nebula", make_nebula), ("stars", make_stars), ("spires", make_spires),
                       ("flora", make_flora), ("crystals", make_crystals), ("clouds", make_clouds),
                       ("islands", make_islands), ("volcanoes", make_volcanoes),
                       ("cave", make_cave), ("rings", make_rings), ("planets", make_planets)):
        write_png(textures / f"{name}.png", make())
    print(f"Wrote the sprite sheets, tiles and backgrounds to {textures}")
    make_sounds()
    make_levels()


if __name__ == "__main__":
    main()
