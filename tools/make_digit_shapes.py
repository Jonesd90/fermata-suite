#!/usr/bin/env python3
"""Makes src/core/ScoreDigitShapes.inc: what the ten digits look like in the kinds of type that scores print their bar numbers in.

fermata's score window reads the printed bar numbers (src/core/ScoreDigits.h) by laying each small shape it finds above a staff over
these shapes. Each shape is a digit drawn from a font on this machine, cut to its own edges and brought to a 12 x 18 grid, sixteen
greys a cell - exactly as ScoreDigits.h brings a shape cut from a page to the same grid (keep the two the same: `to_grid` here,
`shapeOf` there). Shapes that are nearly the same as one already kept are left out.

Run it on a machine that has the fonts (any Linux with the TeX Gyre, Liberation and DejaVu fonts): python3 -I tools/make_digit_shapes.py
The fonts are free ones that stand in for what the notation programs use: Schola for Century Schoolbook (MuseScore's Edwin, Dorico's
Academico, LilyPond), Termes for Times (Sibelius, Finale), Pagella for Palatino, Bonum for Bookman, Heros for Helvetica / Arial.
"""
import glob, os, sys
from PIL import Image, ImageDraw, ImageFont
import numpy as np

GW, GH = 12, 18

def to_grid(mask):
    """mask: 2-D array of 0/1, cut to the glyph's own edges. Height to GH; width in proportion, centred - or squeezed to GW if wider."""
    h, w = mask.shape
    out_w = min(GW, max(1.0, w * GH / h))
    x_off = (GW - out_w) / 2.0
    grid = np.zeros((GH, GW)); area = np.zeros((GH, GW))
    for y in range(h):
        gy0, gy1 = y * GH / h, (y + 1) * GH / h
        for x in range(w):
            gx0, gx1 = x_off + x * out_w / w, x_off + (x + 1) * out_w / w
            for cy in range(int(gy0), min(GH, int(np.ceil(gy1)))):
                oy = min(gy1, cy + 1) - max(gy0, cy)
                if oy <= 0: continue
                for cx in range(int(gx0), min(GW, int(np.ceil(gx1)))):
                    ox = min(gx1, cx + 1) - max(gx0, cx)
                    if ox <= 0: continue
                    area[cy, cx] += ox * oy
                    if mask[y, x]: grid[cy, cx] += ox * oy
    return np.clip(np.rint(grid * 15.0), 0, 15).astype(int)          # (a cell is 1 x 1: its inked part, in sixteenths)

def alike(a, b):
    return np.minimum(a, b).sum() / max(1, np.maximum(a, b).sum())

def families():
    want = ['texgyreschola', 'texgyretermes', 'texgyrepagella', 'texgyrebonum', 'LiberationSerif', 'Lora', 'DejaVuSerif', 'Charter', 'bchr',
            'Utopia', 'putr', 'FreeSerif', 'GFSBaskerville', 'lmroman10', 'texgyreheros-', 'LiberationSans', 'DejaVuSans-', 'DejaVuSans.', 'FreeSans', 'Inter']
    files = []
    for pat in ('/usr/share/fonts/**/*.ttf', '/usr/share/fonts/**/*.otf', '/usr/share/texmf/fonts/opentype/**/*.otf', '/usr/share/fonts/**/*.pfb'):
        files += glob.glob(pat, recursive=True)
    out = []
    for w in want:
        for f in sorted(files):
            b = os.path.basename(f)
            if b.startswith(w) and 'Mono' not in b and 'Cond' not in b and 'cn-' not in b and f not in out: out.append(f)
    return out

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    kept = {d: [] for d in '0123456789'}
    used = 0
    for path in families():
        try: font = ImageFont.truetype(path, 120)
        except Exception: continue
        used += 1
        for d in '0123456789':
            im = Image.new('L', (200, 220), 0); ImageDraw.Draw(im).text((40, 30), d, font=font, fill=255)
            a = np.array(im) > 127
            ys, xs = np.nonzero(a)
            if len(ys) == 0: continue
            g = to_grid(a[ys.min():ys.max() + 1, xs.min():xs.max() + 1])
            if all(alike(g, k) < 0.68 for k in kept[d]): kept[d].append(g)
    lines = []
    for d in '0123456789':
        for g in kept[d]: lines.append('    "%s%s",' % (d, ''.join('%x' % v for v in g.flatten())))
    text = ('// Made by tools/make_digit_shapes.py - do not edit by hand. Each line: the digit, then its shape on a %d x %d grid (a hex figure a cell,\n'
            '// row by row from the top; f = all ink). %d shapes from %d fonts.\n' % (GW, GH, len(lines), used)) + '\n'.join(lines) + '\n'
    out = os.path.join(here, '..', 'src', 'core', 'ScoreDigitShapes.inc')
    open(out, 'w').write(text)
    print('wrote', os.path.normpath(out), ':', len(lines), 'shapes from', used, 'fonts;', {d: len(kept[d]) for d in kept})

if __name__ == '__main__':
    main()
