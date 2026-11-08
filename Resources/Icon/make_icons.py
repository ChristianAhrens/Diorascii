#!/usr/bin/env python3
"""Generate the Diorascii app-icon art (tvOS layered tile + square Icon Composer icon).

Everything is drawn on a character grid, exactly like the app itself: solid colour cells with
ASCII glyphs on top. Glyphs are converted to vector outlines (fontTools), so the SVGs have no
font dependency.

Usage:   python3 make_icons.py [--out DIR] [--font DejaVuSansMono.ttf] [--bold DejaVuSansMono-Bold.ttf]
Needs:   pip install fonttools      (optional: rsvg-convert on PATH for the PNG exports)
"""
import argparse, json, os, shutil, subprocess, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

# ----------------------------------------------------------------------------- palettes
DAY = dict(
    sky=['#4a8fd4', '#6aa8dc', '#9cc8e8', '#bcdcf0'], sun=True, sun_col='#f2d24a', cloud='#f0f0f0', cloud_glyph='#ffffff',
    rock=['#7a7872', '#8a8882'], rock_glyph='#5e5c58', snow='#e8f0f8', snow_glyph='#b8cddf',
    forest='#2f7a3a', forest_glyph='#4dbb6a', meadow='#3d8f35', meadow_glyph='#58b248',
    eagle='#3a2f26', eagle_far='#5a4a3e', eagle_head='#f6f6f0', stars=False)
NIGHT = dict(
    sky=['#0b1630', '#10214a', '#182a58', '#26366a'], sun=False, sun_col='#eef0f4', cloud='#7f8fcf', cloud_glyph='#aab6e8',
    rock=['#3a4256', '#434c62'], rock_glyph='#252b3a', snow='#aab8d8', snow_glyph='#8494b8',
    forest='#12301f', forest_glyph='#1f6a3c', meadow='#16401f', meadow_glyph='#215a2c',
    eagle='#d9cfba', eagle_far='#a89f90', eagle_head='#ffffff', stars=True)

# eagle sprites (candidate improvement of assets/forest/eagle.sprite: fingered tips + tail row)
EAGLE_UP    = ["\\\\_.-.-v-.-._//", "   `-.---.-'"]
EAGLE_GLIDE = ["_.-'\"-.v.-\"'-._", "'   `-._.-'   '"]

# ----------------------------------------------------------------------------- scene configs
TV = dict(name='tv', W=1280, H=768, cols=64, rows=19,
          sky_rows=(5, 8, 11), sun=(57, 2), clouds=[(3, 7), (51, 7)],
          peaks=[(6, 11.5), (20, 10.0), (36, 8.0), (50, 10.5), (61, 9.5)], snow_below=11,
          base=15, forest=(15, 17), meadow_from=17,
          eagles=[(EAGLE_UP, 7, 2, False), (EAGLE_GLIDE, 33, 5, True)])
SQUARE = dict(name='square', W=1024, H=1024, cols=32, rows=32,
              sky_rows=(8, 14, 20), sun=(26, 4), clouds=[(2, 12)],
              peaks=[(4, 19.5), (15, 15.0), (27, 18.0)], snow_below=19, slope=0.75,
              base=26, forest=(26, 28), meadow_from=28,
              eagles=[(EAGLE_UP, 2, 5, False), (EAGLE_GLIDE, 13, 9, True)])

# ----------------------------------------------------------------------------- vector canvas
class Fonts:
    def __init__(self, reg, bold):
        self.f = {False: TTFont(reg), True: TTFont(bold)}
        self.gs = {k: v.getGlyphSet() for k, v in self.f.items()}
        self.cmap = {k: v.getBestCmap() for k, v in self.f.items()}
        self.upm = self.f[False]['head'].unitsPerEm
        hhea = self.f[False]['hhea']
        self.mid = (hhea.ascent + hhea.descent) / 2 / self.upm   # em above baseline of the vertical middle
        self.adv = self.f[False]['hmtx'][self.cmap[False][ord('M')]][0] / self.upm
    def path(self, ch, bold, cx, cy, fs):
        name = self.cmap[bold].get(ord(ch))
        if not name: return ''
        s = fs / self.upm
        pen = SVGPathPen(self.gs[bold])
        # centre advance box on (cx), put vertical middle on (cy); font y axis is up -> flip
        x0 = cx - self.adv * fs / 2; y0 = cy + self.mid * fs
        self.gs[bold][name].draw(TransformPen(pen, (s, 0, 0, -s, x0, y0)))
        return pen.getCommands()

class Layer:
    def __init__(self, cfg, fonts):
        self.cfg, self.fonts = cfg, fonts
        self.cw = cfg['W'] / cfg['cols']; self.ch = cfg['H'] / cfg['rows']
        self.fs = self.cw / fonts.adv
        self.cells = {}; self.glyphs = []
    def block(self, c, r, col):
        if 0 <= c < self.cfg['cols'] and 0 <= r < self.cfg['rows']: self.cells[(c, r)] = col
    def glyph(self, c, r, ch, col, bold=False):
        if ch != ' ' and 0 <= c < self.cfg['cols'] and 0 <= r < self.cfg['rows']:
            self.glyphs.append((c, r, ch, col, bold))
    def svg_body(self):
        out = ['<g shape-rendering="crispEdges">']
        # merge horizontal runs of equal colour; 0.4 px overlap hides anti-aliasing seams
        for r in range(self.cfg['rows']):
            c = 0
            while c < self.cfg['cols']:
                col = self.cells.get((c, r))
                if col is None: c += 1; continue
                c2 = c
                while self.cells.get((c2 + 1, r)) == col: c2 += 1
                x = c * self.cw; w = (c2 - c + 1) * self.cw
                out.append(f'<rect x="{x:.2f}" y="{r*self.ch:.2f}" width="{w+.4:.2f}" height="{self.ch+.4:.2f}" fill="{col}"/>')
                c = c2 + 1
        out.append('</g>')
        by = {}
        for c, r, ch, col, b in self.glyphs:
            d = self.fonts.path(ch, b, (c + .5) * self.cw, (r + .5) * self.ch, self.fs)
            if d: by.setdefault(col, []).append(d)
        for col, ds in by.items():
            out.append(f'<path fill="{col}" d="{" ".join(ds)}"/>')
        return '\n'.join(out)
    def svg(self, name):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.cfg["W"]}" height="{self.cfg["H"]}" '
                f'viewBox="0 0 {self.cfg["W"]} {self.cfg["H"]}">\n<title>{name}</title>\n{self.svg_body()}\n</svg>\n')

def compose_svg(cfg, layers):
    body = '\n'.join(f'<g id="{n}">\n{l.svg_body()}\n</g>' for n, l in layers)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{cfg["W"]}" height="{cfg["H"]}" '
            f'viewBox="0 0 {cfg["W"]} {cfg["H"]}">\n{body}\n</svg>\n')

# ----------------------------------------------------------------------------- scene
def build(cfg, fonts, t):
    sky, sun, mtn, eag = (Layer(cfg, fonts) for _ in range(4))
    cols, rows = cfg['cols'], cfg['rows']; a, b, c3 = cfg['sky_rows']
    for r in range(rows):
        col = t['sky'][0] if r < a else t['sky'][1] if r < b else t['sky'][2] if r < c3 else t['sky'][3]
        for c in range(cols): sky.block(c, r, col)
    sx, sy = cfg['sun']
    keep_out = [(sx - 3, sy - 2, sx + 3, sy + 2)]
    if t['sun']:   # sun_big: rays around a one-cell core
        for dx, dy, ch in [(-1,-1,'\\'),(0,-1,'|'),(1,-1,'/'),(-2,0,'-'),(-1,0,'('),(1,0,')'),(2,0,'-'),(-1,1,'/'),(0,1,'|'),(1,1,'\\')]:
            sun.glyph(sx + dx, sy + dy, ch, t['sun_col'], True)
        sun.block(sx, sy, t['sun_col'])
    else:          # moon_big: solid pale disc with .-. ( ) `-' outline
        for dx in range(-1, 2):
            sun.block(sx + dx, sy - 1, t['sun_col']); sun.block(sx + dx, sy + 1, t['sun_col'])
        for dx in range(-2, 3): sun.block(sx + dx, sy, t['sun_col'])
        for dx, dy, ch in [(-1,-1,'.'),(0,-1,'-'),(1,-1,'.'),(-2,0,'('),(2,0,')'),(-1,1,'`'),(0,1,'-'),(1,1,"'")]:
            sun.glyph(sx + dx, sy + dy, ch, '#aeb6cc', True)
    for cx, cy in cfg['clouds']:
        keep_out.append((cx - 1, cy - 1, cx + 10, cy + 2))
        for c in range(cx + 2, cx + 7): sun.block(c, cy, t['cloud'])
        for c in range(cx, cx + 9): sun.block(c, cy + 1, t['cloud'])
        sun.glyph(cx - 1, cy + 1, '(', t['cloud_glyph']); sun.glyph(cx + 9, cy + 1, ')', t['cloud_glyph']); sun.glyph(cx + 4, cy - 1, '.', t['cloud_glyph'])
    for lines, c0, r0, _ in cfg['eagles']:
        keep_out.append((c0 - 1, r0 - 1, c0 + max(len(l) for l in lines), r0 + len(lines)))
    if t['stars']:
        for r in range(0, cfg['base'] - 4):
            for c in range(cols):
                h = (c * 37 + r * 91 + (c * r * 7) % 17) % 43
                if h in (0, 1) and not any(x0 <= c <= x1 and y0 <= r <= y1 for x0, y0, x1, y1 in keep_out):
                    sky.glyph(c, r, '*' if h == 0 else '.', '#e8ecf8', True)
    # mountains (stepped slabs with glyph texture), forest band, meadow
    base = cfg['base']
    for c in range(cols):
        h = min(pr + abs(c - pc) * cfg.get('slope', 0.5) for pc, pr in cfg['peaks'])
        for r in range(int(h) + 1, base):
            depth = r - h
            snow = depth < 2.2 + ((c * 5) % 3) * 0.6 and h < cfg['snow_below']
            mtn.block(c, r, t['snow'] if snow else t['rock'][1] if (c // 2 + r) % 2 else t['rock'][0])
            ch = "/" if (c*2+r) % 5 == 0 else "\\" if (c*3+r) % 7 == 0 else ":" if (c+r*2) % 4 == 0 else "'" if (c+r) % 5 == 1 else "." if (c*r) % 6 == 3 else ","
            if depth < 1 and c % 3 == 0: ch = '^'
            mtn.glyph(c, r, ch, t['snow_glyph'] if snow else t['rock_glyph'])
    f0, f1 = cfg['forest']
    for r in range(f0, f1):
        for c in range(cols): mtn.block(c, r, t['forest'])
    for c in range(cols):
        mtn.glyph(c, f0, "^/\\^"[(c*7) % 4], t['forest_glyph']); mtn.glyph(c, f0 + 1, "'^',"[(c*5) % 3], t['forest_glyph'])
    for r in range(cfg['meadow_from'], rows):
        for c in range(cols):
            mtn.block(c, r, t['meadow'])
            if (c*7 + r*13) % 11 == 0: mtn.glyph(c, r, "',.\""[(c+r) % 4], t['meadow_glyph'])
    for lines, c0, r0, far in cfg['eagles']:
        col = t['eagle_far'] if far else t['eagle']
        for j, l in enumerate(lines):
            for k, ch in enumerate(l):
                eag.glyph(c0 + k, r0 + j, ch, t['eagle_head'] if ch in 'v^' else col, True)
    return sky, sun, mtn, eag

# ----------------------------------------------------------------------------- export helpers
def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f: f.write(text)

def png(svg, out, w, h):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if shutil.which('rsvg-convert'):
        subprocess.run(['rsvg-convert', '-w', str(w), '-h', str(h), '-o', out, svg], check=True)
    else:
        print(f'  (rsvg-convert not found - skipped {out})')

def json_dump(path, obj): write(path, json.dumps(obj, indent=2) + '\n')
INFO = {'author': 'xcode', 'version': 1}

def export_tv(out, fonts, t, sub, tag):
    sky, sun, mtn, eag = build(TV, fonts, t)
    back = Layer(TV, fonts); back.cells = dict(sky.cells); back.cells.update(sun.cells); back.glyphs = sky.glyphs + sun.glyphs
    stack = {'Back': back, 'Middle': mtn, 'Front': eag}
    base = os.path.join(out, 'tvOS', sub)
    for n, l in stack.items(): write(f'{base}/layers/{n.lower()}.svg', l.svg(f'Diorascii tvOS {tag} {n}'))
    write(f'{base}/preview/tile.svg', compose_svg(TV, [('back', back), ('middle', mtn), ('front', eag)]))
    png(f'{base}/preview/tile.svg', f'{base}/preview/tile-1280x768.png', 1280, 768)
    # Xcode asset catalog: brand assets with a large (store) and a small (home screen) image stack
    ba = f'{base}/App Icon & Top Shelf Image.brandassets'
    json_dump(f'{ba}/Contents.json', {'assets': [
        {'filename': 'App Icon - Large.imagestack', 'idiom': 'tv', 'role': 'primary-app-icon', 'size': '1280x768'},
        {'filename': 'App Icon - Small.imagestack', 'idiom': 'tv', 'role': 'primary-app-icon', 'size': '400x240'}], 'info': INFO})
    for label, (w, h) in {'Large': (1280, 768), 'Small': (400, 240)}.items():
        st = f'{ba}/App Icon - {label}.imagestack'
        json_dump(f'{st}/Contents.json', {'info': INFO, 'layers': [{'filename': f'{n}.imagestacklayer'} for n in ('Front', 'Middle', 'Back')]})
        for n in stack:
            ly = f'{st}/{n}.imagestacklayer'
            json_dump(f'{ly}/Contents.json', {'info': INFO})
            json_dump(f'{ly}/Content.imageset/Contents.json', {'images': [{'idiom': 'tv', 'filename': f'{n}.png', 'scale': '1x'}], 'info': INFO})
            png(f'{base}/layers/{n.lower()}.svg', f'{ly}/Content.imageset/{n}.png', w, h)

def export_square(out, fonts):
    base = os.path.join(out, 'square')
    res = {}
    for key, t, suf in (('day', DAY, ''), ('night', NIGHT, '-night')):
        sky, sun, mtn, eag = build(SQUARE, fonts, t)
        layers = {'Sky': sky, 'Sun' if t['sun'] else 'Moon': sun, 'Mountains': mtn, 'Eagles': eag}
        for n, l in layers.items(): write(f'{base}/layers/{n.lower()}{suf}.svg', l.svg(f'Diorascii {key} {n}'))
        write(f'{base}/preview/icon{suf}.svg', compose_svg(SQUARE, [(n.lower(), l) for n, l in layers.items()]))
        png(f'{base}/preview/icon{suf}.svg', f'{base}/preview/icon{suf}-1024.png', 1024, 1024)
        res[key] = layers
    # Icon Composer bundle (same structure as Mema.icon / Umsci.icon). Day and night art are separate layers;
    # day layers are hidden in the dark appearance and night layers only show there (hidden-specializations).
    # (image-name-specializations is ignored by ictool, so a per-appearance image swap would not compile.)
    bundle = f'{base}/Diorascii.icon'
    shutil.rmtree(bundle, ignore_errors=True); os.makedirs(f'{bundle}/Assets')
    pairs = [('Eagles', 'eagles', 'eagles-night', 'EaglesNight'), ('Sun', 'sun', 'moon-night', 'Moon'),
             ('Mountains', 'mountains', 'mountains-night', 'MountainsNight'), ('Sky', 'sky', 'sky-night', 'SkyNight')]
    for day, fd, fn, night in pairs:
        shutil.copy(f'{base}/layers/{fd}.svg', f'{bundle}/Assets/{day}.svg')
        shutil.copy(f'{base}/layers/{fn}.svg', f'{bundle}/Assets/{night}.svg')
    def rgb(h): return 'srgb:%.5f,%.5f,%.5f,1.00000' % tuple(int(h[i:i+2], 16) / 255 for i in (1, 3, 5))
    gday = {'linear-gradient': [rgb(DAY['sky'][0]), rgb(DAY['sky'][3])]}
    gnight = {'linear-gradient': [rgb(NIGHT['sky'][0]), rgb(NIGHT['sky'][3])]}
    def group(n, night_only):
        layer = {'glass': False, 'image-name': f'{n}.svg', 'name': n, 'position': {'scale': 1, 'translation-in-points': [0, 0]}}
        if night_only:
            layer['hidden'] = True
            layer['hidden-specializations'] = [{'appearance': 'dark', 'value': False}]
            layer['fill-specializations'] = [{'appearance': 'dark', 'value': 'none'}]   # keep the SVG's own colours
        else:
            layer['hidden'] = False
            layer['hidden-specializations'] = [{'appearance': 'dark', 'value': True}]
        return {'name': n, 'layers': [layer], 'shadow': {'kind': 'none', 'opacity': 0}, 'translucency': {'enabled': False, 'value': 0}}
    groups = []                                  # first group = top-most
    for day, fd, fn, night in pairs:
        groups += [group(night, True), group(day, False)]
    json_dump(f'{bundle}/icon.json', {
        'fill': gday, 'fill-specializations': [{'value': gday}, {'appearance': 'dark', 'value': gnight}],
        'groups': groups, 'supported-platforms': {'squares': 'shared'}})

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=here)
    ap.add_argument('--font', default='/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
    ap.add_argument('--bold', default='/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf')
    a = ap.parse_args()
    fonts = Fonts(a.font, a.bold)
    export_tv(a.out, fonts, DAY, '', 'day'); export_tv(a.out, fonts, NIGHT, 'night', 'night'); export_square(a.out, fonts)
    print('done ->', a.out)
if __name__ == '__main__': main()
