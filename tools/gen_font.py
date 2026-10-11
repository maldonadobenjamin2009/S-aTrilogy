#!/usr/bin/env python3
"""Genera src/font_data.c: atlas SDF (distancia con signo) de DejaVu Sans Bold.
La tipografía DejaVu es de licencia libre (Bitstream Vera / DejaVu). El atlas se escala a cualquier
tamaño en tiempo de ejecución y se incrusta en el ejecutable (sin archivos de fuente externos)."""
import sys, math
import numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw, ImageFont

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
OUT = sys.argv[2] if len(sys.argv) > 2 else "src/font_data.c"
EM = 40          # tamaño base (px) del atlas
SS = 4           # supermuestreo para calcular la distancia
SPREAD = 6       # alcance de la distancia en px del atlas
ATLAS_W = 1024

cps = list(range(32, 127)) + list(range(160, 256)) + [0x2026, 0x2014, 0x2022, 0x2190, 0x2191, 0x2192, 0x2193]
hi = ImageFont.truetype(FONT, EM * SS)
glyphs = []
for cp in cps:
    ch = chr(cp)
    adv = hi.getlength(ch) / SS
    l, t, r, b = hi.getbbox(ch, anchor="ls")
    if r <= l or b <= t:      # espacio u otro glifo vacío
        glyphs.append(dict(cp=cp, adv=adv, w=0, h=0, x0=0.0, y0=0.0, img=None))
        continue
    pad = SPREAD * SS + SS
    wpx = ((r - l + 2 * pad) + SS - 1) // SS * SS
    hpx = ((b - t + 2 * pad) + SS - 1) // SS * SS
    img = Image.new("L", (wpx, hpx), 0)
    ox, oy = pad - l, pad - t
    ImageDraw.Draw(img).text((ox, oy), ch, font=hi, fill=255, anchor="ls")
    a = np.asarray(img) > 127
    din = ndimage.distance_transform_edt(a)
    dout = ndimage.distance_transform_edt(~a)
    sd = (din - dout) / SS                       # + dentro, - fuera (px del atlas)
    # muestreo en el centro de cada celda de SSxSS
    sd = sd.reshape(hpx // SS, SS, wpx // SS, SS).mean(axis=(1, 3))
    v = np.clip(128 + sd * (127.0 / SPREAD), 0, 255).astype(np.uint8)
    glyphs.append(dict(cp=cp, adv=adv, w=wpx // SS, h=hpx // SS, x0=-ox / SS, y0=-oy / SS, img=v))

# empaquetado en estantes
x = y = rowh = 0
for g in glyphs:
    if g["img"] is None:
        g["ax"] = g["ay"] = 0
        continue
    if x + g["w"] + 1 > ATLAS_W:
        x, y, rowh = 0, y + rowh + 1, 0
    g["ax"], g["ay"] = x, y
    x += g["w"] + 1
    rowh = max(rowh, g["h"])
ATLAS_H = y + rowh + 1
atlas = np.zeros((ATLAS_H, ATLAS_W), np.uint8)
for g in glyphs:
    if g["img"] is not None:
        atlas[g["ay"]:g["ay"] + g["h"], g["ax"]:g["ax"] + g["w"]] = g["img"]

asc = hi.getmetrics()[0] / SS
desc = hi.getmetrics()[1] / SS
with open(OUT, "w") as f:
    f.write("/* Generado por tools/gen_font.py (DejaVu Sans Bold, SDF). No editar a mano. */\n")
    f.write('#include "font.h"\n\n')
    f.write("const int font_em = %d;\nconst int font_spread = %d;\nconst int font_atlas_w = %d;\nconst int font_atlas_h = %d;\n" % (EM, SPREAD, ATLAS_W, ATLAS_H))
    f.write("const float font_ascent = %.3ff;\nconst float font_descent = %.3ff;\n" % (asc, desc))
    f.write("const int font_glyph_count = %d;\n" % len(glyphs))
    f.write("const FontGlyph font_glyphs[] = {\n")
    for g in glyphs:
        f.write("  {%d,%d,%d,%d,%d,%.3ff,%.3ff,%.3ff},\n" % (g["cp"], g["ax"], g["ay"], g["w"], g["h"], g["x0"], g["y0"], g["adv"]))
    f.write("};\n\nconst unsigned char font_atlas[] = {")
    flat = atlas.flatten()
    for i in range(0, len(flat), 32):
        f.write("\n" + ",".join(str(int(b)) for b in flat[i:i + 32]) + ",")
    f.write("\n};\n")
print("glifos:", len(glyphs), "atlas:", ATLAS_W, "x", ATLAS_H, "=", ATLAS_W * ATLAS_H // 1024, "KB")
Image.fromarray(atlas).save("/tmp/atlas.png")
