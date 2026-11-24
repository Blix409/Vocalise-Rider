#!/usr/bin/env python3
"""Builds the guide web page. Run from anywhere:  python3 Packaging/make_guide.py
 - Assets/guide.html            (built into the plugin; the plugin fills in version + logo when you press "i")
 - Packaging/docs/Vocalise-Rider Guide.html and Packaging/windows/docs/... (standalone copies)"""
import base64, os, re
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
src = os.path.join(here, 'guide_src')
tpl = open(os.path.join(src, 'guide.template.html'), encoding='utf-8').read()
shot = 'data:image/png;base64,' + base64.b64encode(open(os.path.join(src, 'screenshot.png'), 'rb').read()).decode()
logo_png = open(os.path.join(root, 'Assets', 'logo.png'), 'rb').read()
cm = open(os.path.join(root, 'CMakeLists.txt')).read()
version = re.search(r'project\(VocalRider VERSION ([0-9.]+)', cm).group(1)

def diagram():
    W, H = 760, 280; x0, x1 = 60, 740
    target, ceil, floor = -30.0, -26.0, -37.0
    y = lambda db: 20 + (-db - 18) / 34 * (H - 50)
    import math, random
    random.seed(4)
    n = 90; xs = [x0 + (x1 - x0) * i / (n - 1) for i in range(n)]
    orig = [-30 + 11 * math.sin(i / 5.0) + 4 * math.sin(i / 1.7 + 1) + random.uniform(-1.5, 1.5) for i in range(n)]
    lev = 0.4
    after = [max(floor, min(ceil, o + lev * (target - o))) for o in orig]
    pts = lambda v: ' '.join(f'{x:.1f},{y(d):.1f}' for x, d in zip(xs, v))
    g = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img" aria-label="Diagram of the Tame Peaks and Lower Levels lines" style="background:#1c1c1c;border-radius:10px;border:1px solid #313131">']
    for d in (-20, -30, -40):
        g.append(f'<line x1="{x0}" x2="{x1}" y1="{y(d):.1f}" y2="{y(d):.1f}" stroke="#313131"/><text x="{x0-8}" y="{y(d)+4:.1f}" fill="#888" font-size="11" text-anchor="end">{d}</text>')
    g.append(f'<polyline points="{pts(orig)}" fill="none" stroke="#8a8a8a" stroke-width="1.5"/>')
    g.append(f'<polyline points="{pts(after)}" fill="none" stroke="#4caf50" stroke-width="2.4"/>')
    for db, col, label in ((ceil, '#ce93d8', 'TAME PEAKS +4 dB: the ceiling'), (target, '#80deea', 'TARGET'), (floor, '#90caf9', 'LOWER LEVELS -7 dB: the floor')):
        g.append(f'<line x1="{x0}" x2="{x1}" y1="{y(db):.1f}" y2="{y(db):.1f}" stroke="{col}" stroke-dasharray="6 5" stroke-width="1.4"/>')
        g.append(f'<text x="{x1-4}" y="{y(db)-5:.1f}" fill="{col}" font-size="12" font-weight="700" text-anchor="end">{label}</text>')
    g.append(f'<line x1="{x0+24}" x2="{x0+24}" y1="{y(ceil):.1f}" y2="{y(target):.1f}" stroke="#ce93d8" stroke-width="2"/><text x="{x0+32}" y="{(y(ceil)+y(target))/2+4:.1f}" fill="#ce93d8" font-size="11">+4 dB</text>')
    g.append(f'<line x1="{x0+24}" x2="{x0+24}" y1="{y(target):.1f}" y2="{y(floor):.1f}" stroke="#90caf9" stroke-width="2"/><text x="{x0+32}" y="{(y(target)+y(floor))/2+4:.1f}" fill="#90caf9" font-size="11">-7 dB</text>')
    g.append(f'<text x="{x0+6}" y="{H-8}" fill="#8a8a8a" font-size="12">grey = original</text><text x="{x0+120}" y="{H-8}" fill="#4caf50" font-size="12">green = after riding (peaks cut to the ceiling, dips lifted toward the floor)</text></svg>')
    return ''.join(g)

page = tpl.replace('{{SCREENSHOT}}', shot).replace('{{DIAGRAM}}', diagram())
open(os.path.join(root, 'Assets', 'guide.html'), 'w', encoding='utf-8').write(page)      # still has {{VERSION}} and {{LOGO}}
standalone = page.replace('{{VERSION}}', version).replace('{{LOGO}}', 'data:image/png;base64,' + base64.b64encode(logo_png).decode())
for d in (os.path.join(here, 'docs'), os.path.join(here, 'windows', 'docs')):
    open(os.path.join(d, 'Vocalise-Rider Guide.html'), 'w', encoding='utf-8').write(standalone)
print('guide built, version', version, len(page) // 1024, 'KB')
