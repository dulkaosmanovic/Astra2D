#!/usr/bin/env python3
"""Generates the example textures in examples/assets/textures/: the sprite sheet sprites.png and the
parallax background layers mountains.png and hills.png.

The art is original and procedurally drawn by this script (CC0, no third-party assets).
Only the Python standard library is used, so the image can be regenerated anywhere:

    python3 scripts/generate_assets.py

Layout: 16x16 frames, 4 columns x 2 rows, each frame surrounded by a 1-texel border that repeats
its edge texels ("extrusion"), so 18x18 texels per cell and 72x36 in total. Load it with
    SpriteSheet(texture, {16, 16}, /*margin*/ {1, 1}, /*spacing*/ {2, 2})
The border stops neighbouring frames from bleeding in when a sprite is rotated, scaled by a
non-integer factor or filtered linearly: a sample just outside a frame finds the frame's own edge.
    row 0: blob squishing (idle / walk cycle)
    row 1: coin spinning
"""

import math
import struct
import zlib
from pathlib import Path

FRAME = 16
PADDING = 1  # extruded texels on each side of a frame
COLUMNS, ROWS = 4, 2
WIDTH, HEIGHT = FRAME * COLUMNS, FRAME * ROWS  # frames packed without borders, while drawing
CELL = FRAME + 2 * PADDING

TRANSPARENT = (0, 0, 0, 0)
OUTLINE = (24, 20, 37, 255)
BLOB = (92, 196, 120, 255)
BLOB_LIGHT = (160, 232, 150, 255)
EYE = (250, 250, 245, 255)
COIN = (247, 196, 64, 255)
COIN_DARK = (196, 128, 36, 255)
COIN_LIGHT = (255, 240, 170, 255)


def blank():
    return [[TRANSPARENT] * WIDTH for _ in range(HEIGHT)]


def put(image, x, y, color):
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        image[y][x] = color


def ellipse(image, ox, oy, cx, cy, rx, ry, fill, outline):
    """Filled ellipse with a one-texel outline, centred on (cx, cy) inside the frame at (ox, oy)."""
    for y in range(FRAME):
        for x in range(FRAME):
            dx = (x + 0.5 - cx) / rx
            dy = (y + 0.5 - cy) / ry
            d = dx * dx + dy * dy
            if d <= 1.0:
                inner_dx = (x + 0.5 - cx) / max(rx - 1.0, 0.5)
                inner_dy = (y + 0.5 - cy) / max(ry - 1.0, 0.5)
                inside = inner_dx * inner_dx + inner_dy * inner_dy <= 1.0
                put(image, ox + x, oy + y, fill if inside else outline)


def draw_blob(image, column, squish):
    """A round blob resting on the bottom of the frame; squish > 0 flattens it."""
    ox, oy = column * FRAME, 0
    rx = 6.0 + squish
    ry = 5.5 - squish
    cy = FRAME - 1.0 - ry
    ellipse(image, ox, oy, 8.0, cy, rx, ry, BLOB, OUTLINE)
    # Highlight and eyes.
    put(image, ox + 5, oy + round(cy - ry + 2), BLOB_LIGHT)
    put(image, ox + 6, oy + round(cy - ry + 2), BLOB_LIGHT)
    for eye_x in (6, 10):
        put(image, ox + eye_x, oy + round(cy), EYE)
        put(image, ox + eye_x, oy + round(cy) + 1, OUTLINE)


def draw_coin(image, column, angle):
    """A coin seen from the side, rotated by angle around the vertical axis."""
    ox, oy = column * FRAME, FRAME
    rx = max(abs(math.cos(angle)) * 6.0, 1.5)
    ellipse(image, ox, oy, 8.0, 8.0, rx, 6.5, COIN, COIN_DARK)
    if rx > 3.0:  # shine on the face
        put(image, ox + 7, oy + 4, COIN_LIGHT)
        put(image, ox + 7, oy + 5, COIN_LIGHT)


def extrude(image):
    """Copies every frame into its own cell with a PADDING-texel border of repeated edge texels."""
    padded = [[TRANSPARENT] * (CELL * COLUMNS) for _ in range(CELL * ROWS)]
    for row in range(ROWS):
        for column in range(COLUMNS):
            for dy in range(-PADDING, FRAME + PADDING):
                for dx in range(-PADDING, FRAME + PADDING):
                    sx = column * FRAME + min(max(dx, 0), FRAME - 1)
                    sy = row * FRAME + min(max(dy, 0), FRAME - 1)
                    padded[row * CELL + PADDING + dy][column * CELL + PADDING + dx] = image[sy][sx]
    return padded


def write_png(path, image):
    width, height = len(image[0]), len(image)
    raw = b"".join(b"\x00" + bytes(channel for pixel in row for channel in pixel) for row in image)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def background_layer(width, height, waves, base, color, rim):
    """A strip of terrain that tiles horizontally: transparent sky above a silhouette whose height
    is a sum of sines with whole numbers of periods over the width (so both ends match), and a
    lighter one-texel rim along the top edge."""
    image = [[TRANSPARENT] * width for _ in range(height)]
    for x in range(width):
        t = x / width
        top = base + sum(amplitude * math.sin(2 * math.pi * (periods * t + phase))
                         for periods, amplitude, phase in waves)
        top = max(0, min(height - 1, round(top)))
        for y in range(top, height):
            image[y][x] = rim if y == top else color
    return image


def main():
    textures = Path(__file__).resolve().parent.parent / "examples" / "assets" / "textures"
    mountains = background_layer(128, 64, [(1, 9.0, 0.1), (3, 5.0, 0.6), (7, 2.0, 0.3)], 28,
                                 (58, 66, 110, 255), (92, 102, 150, 255))
    write_png(textures / "mountains.png", mountains)
    hills = background_layer(128, 48, [(2, 5.0, 0.0), (5, 2.5, 0.4)], 24,
                             (34, 74, 62, 255), (66, 120, 86, 255))
    write_png(textures / "hills.png", hills)
    print(f"Wrote {textures / 'mountains.png'} and {textures / 'hills.png'}")

    image = blank()
    for column, squish in enumerate((0.0, 0.8, 1.4, 0.8)):
        draw_blob(image, column, squish)
    for column in range(COLUMNS):
        draw_coin(image, column, column * math.pi / COLUMNS)
    padded = extrude(image)
    output = Path(__file__).resolve().parent.parent / "examples" / "assets" / "textures" / "sprites.png"
    write_png(output, padded)
    print(f"Wrote {output} ({len(padded[0])}x{len(padded)})")


if __name__ == "__main__":
    main()
