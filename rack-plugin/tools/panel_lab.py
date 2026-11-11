#!/usr/bin/env python3
"""Writes res/PhonaLab.svg (the tuning tool's panel). Run from rack-plugin/:  python3 tools/panel_lab.py
Same style as tools/panel.py; the geometry constants are the same as in PhonaLabWidget."""
W, H = 81.28, 128.5
ROW0, ROW_H = 8.6, 29.9
X0, X1 = 1.5, 79.78
BG, CARD_HEAD, CARD_BODY, BORDER, SEP, INK = "#cfc7b8", "#d8d0c1", "#ece7dd", "#a89e8b", "#d3cbbc", "#24211d"
TAB_END = [24.0, 30.0, 40.0, 28.0]
TAB_COL = ["#6e4038", "#6e4038", "#3c4a46", "#3f5268"]
o = []
a = o.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">')
a(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
a(f'<rect width="{W}" height="{ROW0 - 0.3:.2f}" fill="{INK}"/>')
a(f'<rect y="{ROW0 - 0.55:.2f}" width="{W}" height="0.25" fill="#b97c2a"/>')
for r in range(4):
    y0, y1 = ROW0 + r * ROW_H + 0.3, ROW0 + r * ROW_H + 29.6
    cw = X1 - X0
    a(f'<rect x="{X0}" y="{y0:.2f}" width="{cw:.2f}" height="{y1 - y0:.2f}" rx="1.3" fill="{CARD_HEAD}" stroke="{BORDER}" stroke-width="0.2"/>')
    te, yb, ym = TAB_END[r], y0 + 3.7, y0 + 1.85
    a(f'<path d="M {X0 + 1.3:.2f} {y0:.2f} L {te:.2f} {y0:.2f} L {te + 1.8:.2f} {ym:.2f} L {te:.2f} {yb:.2f} L {X0:.2f} {yb:.2f} L {X0:.2f} {y0 + 1.3:.2f} Q {X0:.2f} {y0:.2f} {X0 + 1.3:.2f} {y0:.2f} Z" fill="{TAB_COL[r]}"/>')
    a(f'<rect x="{X0 + 0.1:.2f}" y="{yb:.2f}" width="{cw - 0.2:.2f}" height="{y1 - yb - 0.1:.2f}" rx="1.2" fill="{CARD_BODY}"/>')
    n = 4 if r < 2 else 5
    for i in range(1, n):
        x = X0 + cw / n * i
        a(f'<rect x="{x - 0.1:.2f}" y="{y0 + 6.5:.2f}" width="0.2" height="20.5" fill="{SEP}"/>')
a('</svg>')
open("res/PhonaLab.svg", "w").write("\n".join(o) + "\n")
