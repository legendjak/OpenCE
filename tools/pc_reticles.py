#!/usr/bin/env python3
"""Draws the high-res textures of the two reticles Halo PC added to the HUD's
reticle sheet (ui\\hud\\bitmaps\\combined\\hud_reticles, bitmaps 12 and 13:
the fuel rod gun's and the rocket warthog's), which the Xbox's sheet and the
hand-drawn redraws do not have (custom-387: Halo PC's items, and its maps):

    python tools/pc_reticles.py

Each is redrawn in vectors on the stock 64x64 bitmap's texels, its strokes
where the stock's are (measured from Halo Custom Edition's and Halo PC's
bitmaps, which are the same), into port/assets/hud/ce_hud_reticles__12.png
and __13.png at 8x: white coverage, premultiplied (each channel the
coverage), as the other reticles' textures are. ce_layout.json lists them
(generated_by: this), custom_edition.json the stock pixels' CRC the game
checks before drawing them in the stock bitmap's place (hud_hires.c).
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "port/assets/hud"
# the stock bitmap's texels, the texture's scale, and the drawing's
# supersampling (each texture pixel the mean of SUPER x SUPER)
TEXELS = 64
SCALE = 8
SUPER = 4
UNIT = SCALE * SUPER


def canvas():
    image = Image.new("L", (TEXELS * UNIT, TEXELS * UNIT), 0)
    return image, ImageDraw.Draw(image)


def box(draw, x0, y0, x1, y1, fill=255):
    """the texels from (x0, y0) to (x1, y1), texel edges"""
    draw.rectangle([x0 * UNIT, y0 * UNIT, x1 * UNIT - 1, y1 * UNIT - 1], fill=fill)


def ring(draw, cx, cy, radius, width):
    """a circle's stroke, its middle `radius` from (cx, cy)"""
    outer, inner = radius + width / 2, radius - width / 2
    draw.ellipse([(cx - outer) * UNIT, (cy - outer) * UNIT, (cx + outer) * UNIT, (cy + outer) * UNIT], fill=255)
    draw.ellipse([(cx - inner) * UNIT, (cy - inner) * UNIT, (cx + inner) * UNIT, (cy + inner) * UNIT], fill=0)


def save(image, name):
    small = image.resize((TEXELS * SCALE, TEXELS * SCALE), Image.Resampling.BOX)
    Image.merge("RGBA", (small, small, small, small)).save(ASSETS / name, optimize=True)
    print(name)


def arc(chord_x, top, bottom, edge_x, steps=64):
    """the points of a circle's arc through (chord_x, top), (edge_x, middle)
    and (chord_x, bottom), from top to bottom"""
    half = (bottom - top) / 2
    middle = (top + bottom) / 2
    sagitta = abs(chord_x - edge_x)
    radius = (sagitta * sagitta + half * half) / (2 * sagitta)
    centre = edge_x + radius if edge_x < chord_x else edge_x - radius
    side = -1 if edge_x < chord_x else 1
    points = []
    for step in range(steps + 1):
        y = top + (bottom - top) * step / steps
        points.append((centre + side * math.sqrt(max(radius * radius - (y - middle) ** 2, 0.0)), y))
    return points


def fuel_rod():
    """bitmap 12: two crescents, ( and ), and seven bars between them, the
    middle three the longest"""
    image, draw = canvas()
    for mirror in (False, True):
        # (the crescents' tips at x 14, rows 9 to 54; outer edge x 2, inner 7)
        outer = arc(14, 9, 55, 2)
        inner = arc(14, 9, 55, 7)
        points = outer + inner[::-1]
        if mirror:
            points = [(TEXELS - x, y) for x, y in points]
        draw.polygon([(x * UNIT, y * UNIT) for x, y in points], fill=255)
    for row, left, right in ((13, 29, 35), (19, 25.6, 38.4), (25, 24, 41), (31, 24, 41), (37, 24, 41),
                             (43, 25.6, 38.4), (49, 29, 35)):
        box(draw, left, row, right, row + 1)
    save(image, "ce_hud_reticles__12.png")


# the rocket warthog's labels, in the stock's texels: "50" and "HE" (each
# string a letter's rows, its texels set)
LABEL_ROWS = 1
LETTERS = {
    16: ["###", "#..", "###", "..#", "###"],
    20: ["###", "#.#", "#.#", "#.#", "###"],
    40: ["#..#", "#..#", "####", "#..#", "#..#"],
    45: ["###", "#..", "##.", "#..", "###"],
}


def rocket_warthog():
    """bitmap 13: a ring with a cross through it, a smaller ring inside, three
    dashes, and the labels 50 and HE in the ring's top, which it leaves out
    there"""
    image, draw = canvas()
    ring(draw, 31.5, 32.5, 30.8, 1.05)
    # (the ring left out behind the labels: above row 5.5, more than 6.5 from
    # the middle)
    box(draw, 0, 0, 25, 5.5, fill=0)
    box(draw, 38, 0, TEXELS, 5.5, fill=0)
    inner, inner_draw = canvas()
    ring(inner_draw, 31.5, 32.5, 20.1, 1.05)
    # (the inner ring left out at its sides just above the cross's line)
    box(inner_draw, 0, 29, 16, 32, fill=0)
    box(inner_draw, 48, 29, TEXELS, 32, fill=0)
    image.paste(255, (0, 0), inner)
    # the cross: up and down from ring to ring, across with the middle dash
    box(draw, 31, 1, 32, 12)
    box(draw, 31, 52, 32, 64)
    box(draw, 0, 32, 18, 33)
    box(draw, 26, 32, 37, 33)
    box(draw, 45, 32, 63, 33)
    # the dashes above and below the middle
    box(draw, 26, 22, 37, 23)
    box(draw, 26, 42, 37, 43)
    for column, rows in LETTERS.items():
        for y, row in enumerate(rows):
            for x, texel in enumerate(row):
                if texel == "#":
                    box(draw, column + x, LABEL_ROWS + y, column + x + 1, LABEL_ROWS + y + 1)
    save(image, "ce_hud_reticles__13.png")


if __name__ == "__main__":
    fuel_rod()
    rocket_warthog()
