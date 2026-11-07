#!/usr/bin/env python3
"""Generates the assets of examples/platformer: sprite sheets, sounds and the Tiled levels.

Everything is original and drawn or synthesized by this script (CC0, no third-party assets), with
only the Python standard library:

    python3 scripts/generate_platformer_assets.py

Writes into examples/assets/:
    textures/platformer.png   characters and objects, 8x3 frames of 16x16
    textures/tiles.png        the tileset, 8x2 tiles of 16x16
    sounds/*.wav              sound effects and the music loop (16-bit mono, 22050 Hz)
    levels/tiles.tsj          the tileset for Tiled, with collision properties
    levels/level2.tmj         Tiled maps, made from the ASCII art below; they open in Tiled and
    levels/level3.tmj         can be edited there (running this script again overwrites them)

levels/level1.txt is not generated: it is the hand-written text map (format in that file).

Both sheets use the layout of sprites.png: every frame has a 1-texel extruded border, so cells are
18x18 texels (margin 1, spacing 2 between frames), and nothing bleeds in when scaled.

platformer.png frames (row-major from the top-left):
     0  1   player idle            2-5  player run          6 jump      7 fall
     8  9   crawler walk           10   crawler squashed    11-14 coin  15 heart
    16 17   goal flag waving       18 19 checkpoint off/on  20 empty heart  21-23 sparkle
tiles.png tiles:
     0 grass   1 dirt   2 grass left edge   3 grass right edge   4 stone top   5 stone
     6 one-way plank   7 spikes   8 bush   9 flower   10 rock   11 sign   12 crate   13 brick
    14 grass single column   15 lava
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
VISOR = rgb(64, 176, 236)
VISOR_LIGHT = rgb(186, 242, 255)
SUIT = rgb(240, 128, 52)
SUIT_DARK = rgb(188, 82, 38)
BOOT = rgb(70, 64, 92)
PACK = rgb(146, 152, 176)


def draw_player(s, legs, bob=0, arm_up=False):
    """The astronaut, facing right. legs: two (x, top, bottom) columns of 2-texel-wide legs."""
    for x, top, bottom in legs:
        s.rect(x, top, x + 1, bottom - 1, SUIT_DARK)
        s.rect(x, bottom, x + 1, bottom, BOOT)
    s.rect(3, 8 + bob, 4, 11 + bob, PACK)                  # backpack
    s.rect(5, 9 + bob, 10, 12 + bob, SUIT)                 # body
    s.rect(5, 12 + bob, 10, 12 + bob, SUIT_DARK)
    s.ellipse(8.0, 6.0 + bob, 4.5, 4.0, HELMET)            # helmet
    s.rect(4, 7 + bob, 5, 8 + bob, HELMET_SHADE)
    s.rect(9, 4 + bob, 12, 7 + bob, VISOR)                 # visor, facing right
    s.put(10, 4 + bob, VISOR_LIGHT)
    s.put(11, 5 + bob, VISOR_LIGHT)
    if arm_up:
        s.rect(9, 9 + bob, 10, 9 + bob, SUIT_DARK)
    else:
        s.rect(7, 10 + bob, 8, 11 + bob, SUIT_DARK)
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
    s.rect(3, 13, 13, 13, SHELL_DARK)
    s.put(5, 8, SHELL_LIGHT)
    s.put(6, 7, SHELL_LIGHT)
    s.put(7, 7, SHELL_LIGHT)
    for ex in (9, 12):  # eyes looking right
        s.rect(ex, 8, ex + 1, 9, EYE)
        s.put(ex + 1, 9, PUPIL)
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


POLE = rgb(180, 186, 204)


def draw_flag(s, phase, cloth, cloth_dark, height=13, small=False):
    top = FRAME - 1 - height
    s.rect(4, top + 1, 4, 15, POLE)
    s.put(4, top, COIN)
    width = 6 if small else 9
    rows = 4 if small else 6
    for x in range(width):
        wave = round(math.sin(phase + x * 0.8) * (0.6 if small else 1.0))
        for y in range(rows):
            color = cloth if (x + y) % 4 < 2 or small else cloth_dark
            s.put(5 + x, top + 1 + y + wave, color)
    s.outline()


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
    s = Sheet(8, 3)
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
    # Goal flag, checkpoints, empty heart, sparkle.
    draw_flag(s.frame(16), 0.0, rgb(250, 214, 74), rgb(226, 92, 64))
    draw_flag(s.frame(17), 1.6, rgb(250, 214, 74), rgb(226, 92, 64))
    draw_flag(s.frame(18), 0.0, rgb(120, 124, 140), rgb(120, 124, 140), height=10, small=True)
    draw_flag(s.frame(19), 0.0, rgb(92, 220, 236), rgb(92, 220, 236), height=10, small=True)
    draw_heart(s.frame(20), False)
    draw_sparkle(s.frame(21), 1, rgb(255, 250, 200))
    draw_sparkle(s.frame(22), 3, rgb(255, 240, 150))
    draw_sparkle(s.frame(23), 2, rgb(255, 210, 90))
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


def make_tiles():
    s = Sheet(8, 2)
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
    return s


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
        "name": "Crystal Caves",
        "hint": "Hold DOWN and press JUMP to drop through planks",
        "rows": [
            "====================================================================================================",
            "=..................................................................................................=",
            "=..................................................................................................=",
            "=..................................................................................................=",
            "=..................................................................................................=",
            "=....................................................o.o.o.........................................=",
            "=...................................................-------........................................=",
            "=........................................................................---.......................=",
            "=..............................oooo.o.o.o.........o.o.o.o.........C.e................oooooo........=",
            "=...................................-----.........-------....============............------........=",
            "=........oooo..----............====..........................============....o.o^^^................=",
            "=..P..r..................e.....====..^^^...C..........e...e..============...=======.....e......F...=",
            "===============....==========================...=========================...========================",
            "===============LLLL==========================...=========================...========================",
            "=============================================LLL=========================LLL========================",
        ],
    },
    "level3": {
        "name": "Sky Bridge",
        "hint": "Stomp crawlers from above, mind the gaps",
        "rows": [
            "..............................................................................................................",
            "..............................................................................................................",
            "..............................................................................................................",
            "..............................................................................................................",
            "..............................................................................................................",
            "..........................................o.o.o...............................................................",
            "........................oooo..............-----...................oo..o.o.o.o.....ooo.........................",
            ".........o.o.o..........----...C.....................ooo...C..e..----.............---.....e...................",
            "...P.s...-----...f.e.........#####..---.....e........---.========.........e...---.....#######.---....b...F....",
            "#######........########......#####......########.---.....========.....#######.........#######.....############",
            "#######........########......#####......########.........========.....#######.........#######.....############",
            "#######........########......#####......########.........========.....#######.........#######.....############",
            "#######^^^^^^^^########^^^^^^#####^^^^^^########^^^^^^^^^========^^^^^#######^^^^^^^^^#######^^^^^############",
        ],
    },
}

SOLID_GROUND = "#"
SOLID_STONE = "="
PLANK = "-"
DECOR = {"b": 8, "f": 9, "r": 10, "s": 11}
SPAWNS = {"P": "player", "o": "coin", "e": "enemy", "F": "goal", "C": "checkpoint"}


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
    if c == SOLID_STONE:
        return 5 if at(x, y - 1) == SOLID_STONE else 4
    return {"-": 6, "^": 7, "X": 12, "B": 13, "L": 15}.get(c)


def make_tileset():
    solid = [0, 1, 2, 3, 4, 5, 12, 13, 14]
    tiles = []
    for i in range(16):
        props = []
        if i in solid:
            props.append({"name": "collision", "type": "string", "value": "solid"})
        elif i == 6:
            props.append({"name": "collision", "type": "string", "value": "oneway"})
        elif i in (7, 15):
            props.append({"name": "hazard", "type": "bool", "value": True})
        if props:
            tiles.append({"id": i, "properties": props})
    return {
        "type": "tileset", "version": "1.10", "tiledversion": "1.10.2", "name": "tiles",
        "image": "../textures/tiles.png", "imagewidth": CELL * 8, "imageheight": CELL * 2,
        "tilewidth": FRAME, "tileheight": FRAME, "tilecount": 16, "columns": 8,
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
    print(f"Wrote {textures / 'platformer.png'} and {textures / 'tiles.png'}")
    make_sounds()
    make_levels()


if __name__ == "__main__":
    main()
