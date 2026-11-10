#!/usr/bin/env python3
"""Writes res/Singer.svg. Run from rack-plugin/:  python3 tools/panel.py

The geometry constants are the same as in SingerWidget (src/Singer.cpp). Rack's SVG reader has no
text, blur or shadow, so the panel is flat shapes only; all text is drawn by the widget.
"""
W, H = 101.6, 128.5
ROW0, ROW_H = 8.6, 29.9          # title bar height, band height
CARD_X0, CARD_X1 = 1.5, 100.1    # card left / right
CARD_Y0, CARD_Y1 = 0.3, 29.6     # card top / bottom within a band
HEAD_H = 3.7                     # header strip height
DRAWINGS = False                 # shelved: head (band 1) and vocal folds (band 2) drawings at the left
LEFT = [1.5, 14.0, 12.5, 1.5] if DRAWINGS else [1.5] * 4  # where the cells start (also cellLeft in Singer.cpp)
CELLS = [5, 4, 5, 5]
TAB_END = [17.0, 26.5, 26.5, 41.0]
TAB_COL = ["#4a4034", "#3c4a46", "#6e4038", "#3f5268"]

BG, CARD_HEAD, CARD_BODY, BORDER, SEP = "#cfc7b8", "#d8d0c1", "#ece7dd", "#a89e8b", "#d3cbbc"
INK, AMBER, ICON = "#24211d", "#d98a1f", "#f3ebdc"

o = []
a = o.append
a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">')
a(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

# title bar, with a quiet wave on each side of the name
a(f'<rect width="{W}" height="{ROW0 - 0.3:.2f}" fill="{INK}"/>')
a(f'<rect y="{ROW0 - 0.55:.2f}" width="{W}" height="0.25" fill="#b97c2a"/>')
for side in (0, 1):
    for k, amp in enumerate((1.5, 1.0, 0.5)):
        x0, x1 = (9.0, 34.0) if side == 0 else (92.6, 67.6)
        seg = (x1 - x0) / 4.0
        y = 4.15
        d = f"M {x0:.2f} {y:.2f}"
        for i in range(4):
            s = -1 if i % 2 == 0 else 1
            # the wave grows toward the name
            g = amp * (i + 1) / 4.0
            xa, xb = x0 + seg * i, x0 + seg * (i + 1)
            d += f" C {xa + seg * 0.4:.2f} {y + s * g:.2f}, {xb - seg * 0.4:.2f} {y + s * g:.2f}, {xb:.2f} {y:.2f}"
        a(f'<path d="{d}" fill="none" stroke="#8c6a3c" stroke-width="0.15" opacity="{0.9 - 0.25 * k:.2f}"/>')

def icon(row, y):
    """Small line icon in the header tab, centred on (4.6, y)."""
    st = f'fill="none" stroke="{ICON}" stroke-width="0.3" stroke-linecap="round"'
    if row == 0:    # bars
        for i, h in enumerate((0.5, 1.0, 1.4, 1.0, 0.5)):
            x = 3.4 + 0.6 * i
            a(f'<path d="M {x:.2f} {y - h:.2f} L {x:.2f} {y + h:.2f}" {st}/>')
    elif row == 1:  # wave
        a(f'<path d="M 3.0 {y:.2f} C 3.5 {y - 1.7:.2f}, 4.1 {y - 1.7:.2f}, 4.6 {y:.2f} C 5.1 {y + 1.7:.2f}, 5.7 {y + 1.7:.2f}, 6.2 {y:.2f}" {st}/>')
    elif row == 2:  # vibrating folds
        for i in range(3):
            x = 3.8 + 0.8 * i
            a(f'<path d="M {x:.2f} {y - 1.3:.2f} C {x - 0.5:.2f} {y - 0.6:.2f}, {x + 0.5:.2f} {y - 0.3:.2f}, {x:.2f} {y:.2f} C {x - 0.5:.2f} {y + 0.3:.2f}, {x + 0.5:.2f} {y + 0.6:.2f}, {x:.2f} {y + 1.3:.2f}" {st}/>')
    else:           # air
        for i, (x0, x1) in enumerate(((3.0, 6.0), (3.6, 6.4), (3.0, 5.4))):
            yy = y - 0.9 + 0.9 * i
            a(f'<path d="M {x0:.2f} {yy:.2f} L {x1:.2f} {yy:.2f}" {st}/>')

for r in range(4):
    y0 = ROW0 + r * ROW_H + CARD_Y0
    y1 = ROW0 + r * ROW_H + CARD_Y1
    cw = CARD_X1 - CARD_X0
    a(f'<rect x="{CARD_X0}" y="{y0:.2f}" width="{cw:.2f}" height="{y1 - y0:.2f}" rx="1.3" fill="{CARD_HEAD}" stroke="{BORDER}" stroke-width="0.2"/>')
    # header tab: rounded top-left corner, pointed right end
    te, yb, ym = TAB_END[r], y0 + HEAD_H, y0 + HEAD_H / 2
    a(f'<path d="M {CARD_X0 + 1.3:.2f} {y0:.2f} L {te:.2f} {y0:.2f} L {te + 1.8:.2f} {ym:.2f} L {te:.2f} {yb:.2f} L {CARD_X0:.2f} {yb:.2f} L {CARD_X0:.2f} {y0 + 1.3:.2f} Q {CARD_X0:.2f} {y0:.2f} {CARD_X0 + 1.3:.2f} {y0:.2f} Z" fill="{TAB_COL[r]}"/>')
    icon(r, ym)
    a(f'<rect x="{CARD_X0 + 0.1:.2f}" y="{yb:.2f}" width="{cw - 0.2:.2f}" height="{y1 - yb - 0.1:.2f}" rx="1.2" fill="{CARD_BODY}"/>')
    n, left = CELLS[r], LEFT[r]
    for i in range(1, n):
        x = left + (CARD_X1 - left) / n * i
        a(f'<rect x="{x - 0.1:.2f}" y="{y0 + 6.5:.2f}" width="0.2" height="20.5" fill="{SEP}"/>')

if DRAWINGS:
    # vocal tract band: head in profile, with the airway from the larynx to the lips and the nose
    by = ROW0 + 1 * ROW_H
    def P(pts):
        return " ".join(f"{x + 1.9:.2f} {y + by + 5.2:.2f}" for x, y in pts)
    skin = "#b3a792"
    a(f'<path d="M {P([(2.6, 3.6)])} C {P([(4.6, 0.2), (9.4, 1.2), (9.5, 6.0)])} L {P([(9.3, 7.4)])} L {P([(11.0, 10.3)])} L {P([(9.9, 10.9)])} L {P([(10.3, 11.9)])} L {P([(9.8, 12.5)])} L {P([(10.2, 13.2)])} L {P([(9.7, 14.0)])} L {P([(9.9, 15.4)])} C {P([(9.5, 16.8), (8.0, 17.0), (6.9, 16.6)])} L {P([(6.5, 19.5)])} L {P([(6.8, 23.8)])}" fill="none" stroke="{skin}" stroke-width="0.35" stroke-linecap="round" stroke-linejoin="round"/>')
    a(f'<path d="M {P([(2.6, 3.6)])} C {P([(0.6, 6.5), (0.8, 11.0), (2.2, 14.5)])} L {P([(2.0, 23.8)])}" fill="none" stroke="{skin}" stroke-width="0.35" stroke-linecap="round" opacity="0.6"/>')
    for wdt, col, op in ((1.9, AMBER, 0.28), (0.9, AMBER, 0.75)):
        a(f'<path d="M {P([(4.9, 23.8)])} L {P([(4.6, 16.5)])} C {P([(4.3, 13.0), (5.6, 12.6), (9.4, 12.6)])}" fill="none" stroke="{col}" stroke-width="{wdt}" stroke-linecap="round" opacity="{op}"/>')
        a(f'<path d="M {P([(4.6, 14.2)])} C {P([(4.9, 10.4), (7.4, 10.2), (9.9, 10.5)])}" fill="none" stroke="{col}" stroke-width="{wdt * 0.6:.2f}" stroke-linecap="round" opacity="{op}"/>')

    # vocal folds band: the larynx seen from above, folds open
    by = ROW0 + 2 * ROW_H
    cx, cy = 7.0, by + 16.6
    a(f'<ellipse cx="{cx}" cy="{cy:.2f}" rx="4.5" ry="6.4" fill="#e2c8b6" stroke="#c4a591" stroke-width="0.25"/>')
    a(f'<ellipse cx="{cx}" cy="{cy:.2f}" rx="3.3" ry="5.2" fill="#d6b09d"/>')
    a(f'<path d="M {cx} {cy - 4.6:.2f} L {cx - 1.5:.2f} {cy + 3.9:.2f} Q {cx} {cy + 4.9:.2f} {cx + 1.5:.2f} {cy + 3.9:.2f} Z" fill="#5d3b34"/>')
    for s in (-1, 1):
        a(f'<path d="M {cx + s * 0.25:.2f} {cy - 4.5:.2f} L {cx + s * 1.75:.2f} {cy + 4.0:.2f}" fill="none" stroke="#f6eee2" stroke-width="0.55" stroke-linecap="round"/>')

a('</svg>')
open("res/Singer.svg", "w").write("\n".join(o) + "\n")
