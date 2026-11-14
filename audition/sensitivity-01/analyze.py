#!/usr/bin/env python3
"""Sensitivity audit: how far each changed value moves the sound from its baseline.

Usage: analyze.py <PHONA_SENS_DIR> [reference_dir]
Distance = RMS difference (dB) between smoothed log-mel spectrograms. "floor" = the distance
between renders of the same sound at other trigger offsets (other noise samples); a change
at or under the floor is not a real change of the sound.
"""
import json, subprocess, sys
from pathlib import Path
import numpy as np
from scipy.signal import stft

FS = 48000
NFFT, HOP, NMEL, SMOOTH = 1024, 240, 32, 4
RANGE_DB = 60.0

def mel_fb():
    mel = lambda f: 2595 * np.log10(1 + f / 700)
    inv = lambda m: 700 * (10 ** (m / 2595) - 1)
    edges = inv(np.linspace(mel(100), mel(16000), NMEL + 2))
    freqs = np.fft.rfftfreq(NFFT, 1 / FS)
    fb = np.zeros((NMEL, len(freqs)))
    for i in range(NMEL):
        lo, c, hi = edges[i:i + 3]
        fb[i] = np.clip(np.minimum((freqs - lo) / (c - lo), (hi - freqs) / (hi - c)), 0, None)
    return fb
FB = mel_fb()

def melspec(x):
    _, _, z = stft(x, FS, nperseg=NFFT, noverlap=NFFT - HOP, boundary=None, padded=False)
    p = FB @ (np.abs(z) ** 2)
    k = np.ones(SMOOTH) / SMOOTH
    return np.apply_along_axis(lambda r: np.convolve(r, k, mode="valid"), 1, p)

def dist(pa, pb, norm=False):
    n = min(pa.shape[1], pb.shape[1])
    pa, pb = pa[:, :n], pb[:, :n]
    if norm:
        pb = pb * (pa.sum() / max(pb.sum(), 1e-30))
    floor = max(pa.max(), pb.max()) * 10 ** (-RANGE_DB / 10)
    a, b = 10 * np.log10(np.maximum(pa, floor)), 10 * np.log10(np.maximum(pb, floor))
    m = (pa > floor) | (pb > floor)
    return float(np.sqrt(np.mean((a[m] - b[m]) ** 2))) if m.any() else 0.0

def local(pa, pb, win=20):
    """Largest distance in any 100 ms window: a short change is not averaged away."""
    n = min(pa.shape[1], pb.shape[1])
    pa, pb = pa[:, :n], pb[:, :n]
    floor = max(pa.max(), pb.max()) * 10 ** (-RANGE_DB / 10)
    a, b = 10 * np.log10(np.maximum(pa, floor)), 10 * np.log10(np.maximum(pb, floor))
    ms = np.mean((a - b) ** 2, axis=0)
    if len(ms) < win:
        return float(np.sqrt(ms.mean()))
    return float(np.sqrt(np.convolve(ms, np.ones(win) / win, mode="valid").max()))

def env(x, ms=5):
    n = int(FS * ms / 1000)
    return np.sqrt(np.convolve(x.astype(np.float64) ** 2, np.ones(n) / n, mode="same"))

def onset(x):
    e = env(x)
    return int(np.argmax(e >= 0.2 * e.max()))

def shape_stats(x):
    """Onset (ms from the trigger), 90 % energy span (ms) and centroid (Hz) of a hit."""
    o = onset(x)
    w = x[max(0, o - 960):o + int(0.38 * FS)].astype(np.float64)
    c = np.cumsum(w ** 2); c /= max(c[-1], 1e-30)
    span = (np.argmax(c >= 0.95) - np.argmax(c >= 0.05)) / FS * 1e3
    s = np.abs(np.fft.rfft(w * np.hanning(len(w)))) ** 2
    f = np.fft.rfftfreq(len(w), 1 / FS)
    bands = [(0, 500), (500, 2000), (2000, 5000), (5000, 16000)]
    share = [float(s[(f >= a) & (f < b)].sum() / s.sum()) for a, b in bands]
    return o / FS * 1e3, float(span), float((f * s).sum() / s.sum()), share

def load_ref(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(FS), "-f", "f32le", "-"], capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32)

def main():
    d = Path(sys.argv[1])
    refdir = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    rows = []
    for m in sorted(d.glob("manifest_*.tsv")):
        rows += [l.split("\t") for l in m.read_text().splitlines()]
    sets = {}
    for file, s, group, name in rows:
        sets.setdefault(s, []).append((file, group, name))
    refs = {"beat_t": ["ref_hat.mp3"], "beat_Pf": ["ref_snare.mp3"], "beat_B": ["ref_heavy_kick.mp3", "ref_light_kick.mp3"]}
    report = {}
    for s, items in sets.items():
        load = lambda f: np.fromfile(d / f, np.float32)
        base = load(next(f for f, g, _ in items if g == "baseline"))
        pb = melspec(base)
        rms = lambda x: float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))
        out = []
        for file, group, name in items:
            if group == "baseline":
                continue
            x = load(file)
            n = min(len(x), len(base))
            px = melspec(x)
            r = {"group": group, "name": name, "identical": bool(len(x) == len(base) and np.array_equal(x, base)),
                 "dist": dist(pb, px), "local": local(pb, px), "shape": dist(pb, px, True),
                 "level_db": 20 * np.log10(max(rms(x), 1e-12) / rms(base)), "peak": float(np.abs(x).max())}
            if s.startswith("beat_") and r["peak"] > 1e-4:
                r["onset_ms"], r["span_ms"], r["centroid"], r["bands"] = shape_stats(x)
            out.append(r)
        floor = max((r["dist"] for r in out if r["group"] == "floor"), default=0.0)
        lfloor = max((r["local"] for r in out if r["group"] == "floor"), default=0.0)
        info = {"floor": floor, "local_floor": lfloor, "base_peak": float(np.abs(base).max()), "rows": out}
        if s.startswith("beat_"):
            o, span, cen, bands = shape_stats(base)
            info["base"] = {"onset_ms": o, "span_ms": span, "centroid": cen, "bands": bands}
            info["refs"] = {}
            for rf in refs.get(s, []) if refdir else []:
                x = load_ref(refdir / rf)
                ro, bo = onset(x), onset(base)
                seg = x[max(0, ro - bo):]
                seg = np.concatenate([np.zeros(max(0, bo - ro), np.float32), seg])
                seg = np.pad(seg, (0, max(0, len(base) - len(seg))))[:len(base)]
                _, rs, rc, rb = shape_stats(seg)
                info["refs"][rf] = {"shape": dist(pb, melspec(seg), True), "span_ms": rs, "centroid": rc, "bands": rb}
        report[s] = info
    (d / "report.json").write_text(json.dumps(report, indent=1))
    for s, info in report.items():
        rows = [r for r in info["rows"] if r["group"] != "floor"]
        fl = info["floor"]
        dead = [r for r in rows if r["identical"]]
        under = [r for r in rows if not r["identical"] and r["dist"] <= 1.5 * fl]
        print(f"\n=== {s}: {len(rows)} changes | floor {fl:.2f} dB | local floor {info['local_floor']:.2f} dB | baseline peak {info['base_peak']:.3f} | identical {len(dead)} | at/under 1.5x floor {len(under)}")
        if "base" in info:
            b = info["base"]
            print(f"  baseline: onset {b['onset_ms']:.0f} ms, span {b['span_ms']:.0f} ms, centroid {b['centroid']:.0f} Hz, bands <0.5k/0.5-2k/2-5k/>5k = " + "/".join(f"{x*100:.0f}%" for x in b["bands"]))
            for k, r in info["refs"].items():
                print(f"  {k}: shape distance {r['shape']:.1f} dB, span {r['span_ms']:.0f} ms, centroid {r['centroid']:.0f} Hz, bands = " + "/".join(f"{x*100:.0f}%" for x in r["bands"]))
        for r in sorted(rows, key=lambda r: -r["shape"])[:int(sys.argv[3]) if len(sys.argv) > 3 else 14]:
            extra = f" span {r['span_ms']:.0f} cen {r['centroid']:.0f}" if "span_ms" in r else ""
            print(f"  local {r['local']:5.1f}  shape {r['shape']:5.1f}  dist {r['dist']:5.1f}  level {r['level_db']:+6.1f} dB  peak {r['peak']:.2f}{extra}  [{r['group']}] {r['name']}")
        if dead:
            print("  IDENTICAL:", "; ".join(r["name"] for r in dead))

if __name__ == "__main__":
    main()
