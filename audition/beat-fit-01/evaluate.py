#!/usr/bin/env python3
"""Check a fit: numbers, robustness, a spectrogram image and listening files.

Usage: evaluate.py <sound> <work_dir>   (reads <work_dir>/fit_<sound>.json)
Writes into <work_dir>: <sound>_spectrograms.png (reference | current | fitted, 0-16 kHz,
300 ms), <sound>_ref_then_fitted.wav, <sound>_ref_then_current.wav, eval_<sound>.json.
"""
import json, struct, sys, wave, zlib
from pathlib import Path
import numpy as np
from scipy.signal import stft
import fit as F

FS = F.FS

def bands(x):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / FS)
    return [float(s[(f >= a) & (f < b)].sum() / s.sum()) for a, b in [(0, 500), (500, 2000), (2000, 5000), (5000, 16000)]], float((f * s).sum() / s.sum())

def span90(x):
    c = np.cumsum(x ** 2); c /= c[-1]
    return float((np.argmax(c >= 0.95) - np.argmax(c >= 0.05)) / FS * 1e3)

def png(path, img):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[r].tobytes() for r in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

def spec_img(x, height=256, scale=3):
    _, _, z = stft(x, FS, nperseg=512, noverlap=512 - 48, boundary="zeros", padded=True)
    p = np.abs(z[: int(16000 / (FS / 512)) + 1]) ** 2
    d = 10 * np.log10(np.maximum(p / p.max(), 1e-7)) / 70 + 1  # 0..1 over 70 dB
    d = np.clip(d, 0, 1)[::-1]
    rows = np.linspace(0, d.shape[0] - 1, height).astype(int)
    d = np.repeat(d[rows], scale, axis=1)
    stops = np.array([[0, 0, 4], [60, 15, 110], [180, 55, 120], [250, 140, 80], [252, 250, 190]], float)
    i = d * (len(stops) - 1)
    lo = np.clip(i.astype(int), 0, len(stops) - 2)
    return (stops[lo] + (stops[lo + 1] - stops[lo]) * (i - lo)[..., None]).astype(np.uint8)

def wav(path, x):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(FS)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())

def main():
    sound, work = sys.argv[1], Path(sys.argv[2])
    fit = json.load(open(work / f"fit_{sound}.json"))
    f = F.Fitter(sound, work)
    best = np.array(fit["x"])
    rng = np.random.default_rng(0)
    b = np.array(f.bounds())
    jit = [np.clip(best + rng.normal(0, 0.005, len(best)) * (b[:, 1] - b[:, 0]), b[:, 0], b[:, 1]) for _ in range(16)]
    ys = f.render([f.x0(), best] + jit)
    t = f.targets[0]
    report = {"sound": sound, "reference": F.SOUNDS[sound]["refs"][0]}
    rb, rc = bands(t.x)
    report["ref"] = {"bands": rb, "centroid": rc, "span_ms": span90(t.x)}
    segs = {}
    for name, y in [("current", ys[0]), ("fitted", ys[1])]:
        d = t.loss(y, detail=True)
        seg = d.pop("seg")
        segs[name] = seg
        bb, c = bands(seg)
        report[name] = {**d, "bands": bb, "centroid": c, "span_ms": span90(seg)}
        # Where a waiting mouth may stop: the last moment before the sound's onset at which
        # the level is under -40 dB of the peak (the preparation before it is not heard: the
        # module opens a mouth's output only at the hit). When the sound is over: 15 ms after
        # the level last passes -50 dB. Seconds from the start of the gestures.
        e = F.envelope(y, 1.0)
        on = int(np.argmax(e >= 0.2 * e.max()))
        quiet = np.nonzero(e[:on] < e.max() * 10 ** (-40 / 20))[0]
        report[name]["hold_s"] = float(quiet[-1] / FS) if len(quiet) else 0.0
        report[name]["tail_s"] = float(min(0.6, np.nonzero(e > e.max() * 10 ** (-50 / 20))[0][-1] / FS + 0.015))
        report[name]["onset_after_hold_ms"] = on / FS * 1e3 - report[name]["hold_s"] * 1e3
    jl = [t.loss(y) for y in ys[2:]]
    report["robustness"] = {"what": "16 renders with every parameter moved by a random 0.5 % of its range", "loss_mean": float(np.mean(jl)), "loss_max": float(np.max(jl)), "loss_min": float(np.min(jl))}
    json.dump(report, open(work / f"eval_{sound}.json", "w"), indent=1)
    gap = np.full((256, 12, 3), 255, np.uint8)
    png(work / f"{sound}_spectrograms.png", np.concatenate([spec_img(t.x), gap, spec_img(segs["current"]), gap, spec_img(segs["fitted"])], axis=1))
    # listening files: reference then candidate, twice; one hit per second; equal energy
    for name in ["fitted", "current"]:
        out = np.zeros(FS * 4)
        for k, seg in enumerate([t.x, segs[name], t.x, segs[name]]):
            s = seg.copy()
            s[:48] *= np.linspace(0, 1, 48); s[-240:] *= np.linspace(1, 0, 240)
            out[k * FS + 4800: k * FS + 4800 + len(s)] = s
        wav(work / f"{sound}_ref_then_{name}.wav", out / np.abs(out).max() * 0.7)
    pr = lambda k: f"{k:8s} loss {report[k]['total']:5.2f} (fine {report[k]['spec_fine']:.1f} mid {report[k]['spec_mid']:.1f} coarse {report[k]['spec_coarse']:.1f} env {report[k]['env']:.1f} balance {report[k]['balance']:.1f} texture {report[k]['texture']:.2f} outside {report[k]['outside']*100:.0f}%) peak {report[k]['peak']:.2f} latency {report[k]['latency_ms']:.0f} ms, hold {report[k]['hold_s']*1e3:.0f} ms, tail {report[k]['tail_s']*1e3:.0f} ms, onset {report[k]['onset_after_hold_ms']:.0f} ms after hold"
    print(pr("current")); print(pr("fitted"))
    for k in ["ref", "current", "fitted"]:
        r = report[k]
        print(f"{k:8s} bands <0.5k/0.5-2k/2-5k/>5k " + "/".join(f"{x*100:.0f}%" for x in r["bands"]) + f"  centroid {r['centroid']:.0f} Hz  span {r['span_ms']:.0f} ms")
    print("robustness", report["robustness"])

if __name__ == "__main__":
    main()
