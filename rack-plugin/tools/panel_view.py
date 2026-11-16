#!/usr/bin/env python3
"""Writes res/PhonaView.svg (14 HP, the animated mouth and throat), res/PhonaScope.svg (20 HP,
the instrument display) and res/PhonaScript.svg (14 HP, the phrase list and its three jacks). Run from rack-plugin/:  python3 tools/panel_view.py
Same style as tools/panel.py; the geometry constants are the same as in PhonaViewWidget and
PhonaScopeWidget."""
H = 128.5
ROW0 = 8.6
DISP_X0, DISP_Y0, DISP_Y1 = 1.6, 9.4, 114.4
BG, CARD_HEAD, BORDER, INK = "#cfc7b8", "#d8d0c1", "#a89e8b", "#24211d"

def panel(path, W, disp_y1=DISP_Y1, script=False):
    o = []
    a = o.append
    a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">')
    a(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
    a(f'<rect width="{W}" height="{ROW0 - 0.3:.2f}" fill="{INK}"/>')
    a(f'<rect y="{ROW0 - 0.55:.2f}" width="{W}" height="0.25" fill="#b97c2a"/>')
    # the screen: a dark bezel and a black display
    a(f'<rect x="{DISP_X0}" y="{DISP_Y0}" width="{W - 2 * DISP_X0:.2f}" height="{disp_y1 - DISP_Y0:.2f}" rx="1.6" fill="#15130f" stroke="{BORDER}" stroke-width="0.2"/>')
    a(f'<rect x="{DISP_X0 + 0.6:.2f}" y="{DISP_Y0 + 0.6:.2f}" width="{W - 2 * DISP_X0 - 1.2:.2f}" height="{disp_y1 - DISP_Y0 - 1.2:.2f}" rx="1.1" fill="#000000"/>')
    if script:
        # the jacks' card (NEXT, RESET, SELECT): the same numbers as in PhonaScriptWidget
        a(f'<rect x="{DISP_X0}" y="103.0" width="{W - 2 * DISP_X0:.2f}" height="21.6" rx="1.3" fill="{CARD_HEAD}" stroke="{BORDER}" stroke-width="0.2"/>')
        a('</svg>')
        open(path, "w").write("\n".join(o) + "\n")
        return
    # the voice selector's strip
    a(f'<rect x="{DISP_X0}" y="116.6" width="{W - 2 * DISP_X0:.2f}" height="9.6" rx="1.3" fill="{CARD_HEAD}" stroke="{BORDER}" stroke-width="0.2"/>')
    a('</svg>')
    open(path, "w").write("\n".join(o) + "\n")

panel("res/PhonaView.svg", 71.12)
panel("res/PhonaScope.svg", 101.6)
panel("res/PhonaScript.svg", 71.12, disp_y1=100.4, script=True)
