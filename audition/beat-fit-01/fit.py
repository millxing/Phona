#!/usr/bin/env python3
"""Fit one beat sound to a human reference by search (differential evolution).

Usage: fit.py <sound: t|Pf|B> <work_dir> [generations] [seed]
Each candidate is rendered through the wrapper's voice by the test `sensitivity::fit_batch`
(build it first: cargo test --release --manifest-path rack-plugin/rust/Cargo.toml --lib --no-run).
Loss = distance between level-normalised log-mel spectrograms at two time resolutions, plus the
level envelope, plus a penalty for sound outside the reference's time window.
"""
import glob, json, os, subprocess, sys
from pathlib import Path
import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import stft

FS = 48000
N = FS * 6 // 10
ROOT = Path(__file__).resolve().parents[2]
REFDIR = ROOT / "audition" / "beatbox-diagnosis-01"
PRE, POST = 0.04, 0.26  # reference window around its onset (s)
RANGE = 50.0
BALANCE = float(os.environ.get("FIT_BALANCE", "1.0"))  # weight of the band-energy term
TEXTURE = float(os.environ.get("FIT_TEXTURE", "4.0"))  # weight of the noise-or-tone term

# name: (low, high, default, log scale)
NOISE = {
    "ng": (0.1, 30, 1, True), "nc": (0.1, 100, 1, True), "nt": (0.1, 100, 1, True),
    "bgl": (1500, 20000, 3500, True), "bgh": (100, 5000, 300, True),
    "bcl": (1500, 20000, 2600, True), "bch": (200, 8000, 350, True),
    "btl": (2000, 20000, 5000, True), "bth": (300, 10000, 1200, True),
    "hp": (20, 4000, 20, True),  # output high-pass (Hz)
}
# output tone (shelf, peak, low-pass) and airway damping
TONE = {
    "shf": (60, 400, 120, True), "shg": (-12, 18, 0, False),
    "pkf": (300, 6000, 1000, True), "pkg": (-15, 15, 0, False), "pkq": (0.5, 4, 1, True),
    "lp": (1500, 20000, 20000, True), "visc": (0.3, 30, 1, True),
}
SOUNDS = {
    "t": {"refs": ["ref_hat.mp3"], "params": {**NOISE, **TONE,
        "press": (3, 20, 8, False), "pl": (-0.15, 0.4, 0, False), "pt": (-0.3, 0.4, 0, False),
        "g0.ttY": (8.5, 10.5, 9.68, False), "g0.ro": (0.2, 4, 1, True), "g0.spd": (1, 6, 3.5, False), "g0.dur": (0.4, 2, 1, True),
        "g3.t0": (-10, 20, 0, False), "g3.dur": (0.3, 3, 1, True), "g3.ttY": (8.0, 10.0, 9.111, False), "g3.ttX": (5.5, 7.0, 6.3, False),
        "g3.str": (0.1, 2, 1, True), "g3.ro": (0.2, 4, 1, True),
        "g5.abduction": (0.05, 1.0, 0.36, False), "g5.dur": (0.3, 2, 1, True), "g5.t0": (-20, 20, 0, False),
        "g6.breath": (0.3, 3, 1.25, False), "g6.dur": (0.2, 2, 1, True), "g6.t0": (-25, 20, 0, False),
        "g6.ri": (0.2, 3, 1, True), "g6.ro": (0.1, 4, 1, True)}},
    "Pf": {"refs": ["ref_snare.mp3"], "params": {**NOISE, **TONE,
        "press": (3, 20, 8, False), "pl": (-0.15, 0.4, 0, False), "pj": (-0.2, 0.4, 0, False),
        "g0.lipAp": (-0.5, -0.05, -0.25, False), "g0.ro": (0.2, 4, 1, True), "g0.spd": (1, 6, 3.5, False), "g0.dur": (0.4, 2, 1, True),
        "g1.t0": (-15, 25, 0, False), "g1.dur": (0.2, 3, 1, True), "g1.lipAp": (-0.05, 0.2, 0.016, False), "g1.str": (0.05, 2, 1, True), "g1.ro": (0.2, 4, 1, True),
        "g2.abduction": (0.05, 1.0, 0.45, False), "g2.dur": (0.2, 3, 1, True), "g2.t0": (-20, 20, 0, False),
        "g3.breath": (0.3, 3, 1.7, False), "g3.dur": (0.2, 2, 1, True), "g3.t0": (-25, 20, 0, False),
        "g3.ri": (0.2, 3, 1, True), "g3.ro": (0.1, 8, 1, True)}},
    "B": {"refs": ["ref_heavy_kick.mp3"], "params": {**TONE,
        "nc": (0.1, 100, 1, True), "bcl": (1500, 20000, 2600, True), "bch": (200, 8000, 350, True), "hp": (20, 300, 20, True),
        "pl": (-0.24, 0.4, 0, False), "pj": (-0.2, 0.4, 0, False), "pt": (-0.3, 0.4, 0, False),
        # the lips close again after the burst (an open mouth rings at its resonances)
        "x0.lipAp": (-0.2, 0.25, 0.25, False), "x0.t0": (-20, 40, 0, False), "x0.dur": (10, 200, 60, True),
        "x0.ri": (2, 40, 10, True), "x0.ro": (5, 100, 30, True), "x0.str": (0.5, 60, 20, True),
        "g0.lipAp": (-0.5, -0.05, -0.25, False), "g0.ro": (0.2, 6, 1, True), "g0.spd": (1, 6, 3.5, False), "g0.dur": (0.5, 1.6, 1, True), "g0.t0": (-15, 15, 0, False),
        "g1.jaw": (0.1, 0.8, 0.3, False),
        "g2.dur": (0.5, 2, 1, True), "g2.ro": (0.2, 4, 1, True),
        "g3.larynx": (0.8, 1.6, 1.55, False), "g3.dur": (0.5, 2, 1, True), "g3.ri": (0.3, 3, 1, True), "g3.ro": (0.2, 3, 1, True), "g3.t0": (-15, 15, 0, False), "g3.spd": (1, 6, 3, False)}},
}

def mel_fb(nfft, nmel):
    mel = lambda f: 2595 * np.log10(1 + f / 700)
    inv = lambda m: 700 * (10 ** (m / 2595) - 1)
    edges = inv(np.linspace(mel(60), mel(16000), nmel + 2))
    freqs = np.fft.rfftfreq(nfft, 1 / FS)
    fb = np.zeros((nmel, len(freqs)))
    for i in range(nmel):
        lo, c, hi = edges[i:i + 3]
        fb[i] = np.clip(np.minimum((freqs - lo) / (c - lo), (hi - freqs) / (hi - c)), 0, None)
    fb[0, :] += (freqs < edges[1]) * (fb[0] == 0) * 0  # keep shape; lowest band starts at 60 Hz
    return fb

RES = [(256, 48, 20, 3), (1024, 240, 40, 1), (4096, 960, 48, 1)]  # nfft, hop, mel bands, time smoothing
FBS = [mel_fb(n, m) for n, _, m, _ in RES]

def specs(x):
    out = []
    for (nfft, hop, _, sm), fb in zip(RES, FBS):
        _, _, z = stft(x, FS, nperseg=nfft, noverlap=nfft - hop, boundary="zeros", padded=True)
        p = fb @ (np.abs(z) ** 2)
        if sm > 1:
            k = np.ones(sm) / sm
            p = np.apply_along_axis(lambda r: np.convolve(r, k, mode="same"), 1, p)
        out.append(p)
    return out

def envelope(x, ms=2.0):
    n = int(FS * ms / 1000)
    return np.sqrt(np.convolve(x.astype(np.float64) ** 2, np.ones(n) / n, mode="same"))

BAND_EDGES = [0, 150, 300, 600, 1200, 2400, 4800, 9600, 16000]

def band_energy(x):
    """Share of the energy in each band of BAND_EDGES (the whole window)."""
    s = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / FS)
    e = np.array([s[(f >= a) & (f < b)].sum() for a, b in zip(BAND_EDGES[:-1], BAND_EDGES[1:])])
    return e / max(e.sum(), 1e-30)

def flatness(x):
    """Spectral flatness of 300-3000 Hz in three parts of the window (onset, body, tail):
    1 = noise, near 0 = ringing tones. The ear hears a ring there as "metallic"."""
    on = int(PRE * FS)
    out = []
    for a, b in [(0.0, 0.012), (0.012, 0.04), (0.04, 0.12)]:
        seg = x[on + int(a * FS): on + int(b * FS)]
        s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 8192)) ** 2 + 1e-30
        f = np.fft.rfftfreq(8192, 1 / FS)
        m = (f >= 300) & (f < 3000)
        out.append(np.exp(np.mean(np.log(s[m]))) / np.mean(s[m]))
    return np.log10(np.maximum(np.array(out), 1e-4))

def db(p, ref):
    return 10 * np.log10(np.maximum(p, ref * 10 ** (-RANGE / 10)))

class Target:
    def __init__(self, path):
        raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(FS), "-f", "f32le", "-"], capture_output=True, check=True).stdout
        x = np.frombuffer(raw, np.float32).astype(np.float64)
        e = envelope(x)
        on = int(np.argmax(e >= 0.2 * e.max()))
        a, b = on - int(PRE * FS), on + int(POST * FS)
        seg = np.pad(x, (max(0, -a), max(0, b - len(x))))[max(0, a) + 0:][: b - a] if a >= 0 else np.pad(x, (-a, max(0, b - len(x))))[: b - a]
        self.x = seg / np.sqrt(np.sum(seg ** 2))
        self.env = envelope(self.x)
        self.specs = specs(self.x)
        self.refs = [s.max() for s in self.specs]
        self.dbs = [db(s, r) for s, r in zip(self.specs, self.refs)]
        self.bands = band_energy(self.x)
        self.flat = flatness(self.x)
        self.env_db = 20 * np.log10(np.maximum(self.env, self.env.max() * 10 ** (-RANGE / 20)))

    def loss(self, y, detail=False):
        """y: 0.6 s render from the trigger (after the output high-pass)."""
        if not np.all(np.isfinite(y)):
            return 1e3
        peak = np.abs(y).max()
        if peak < 1e-4:
            return 500.0
        L = len(self.x)
        e = envelope(y)
        # best time alignment of the level envelopes
        c = np.correlate(e, self.env, mode="valid")
        lag = int(np.argmax(c))
        seg = y[lag:lag + L]
        tot = np.sum(y ** 2)
        inside = np.sum(seg ** 2)
        outside = 1.0 - inside / tot
        seg = seg / np.sqrt(max(inside, 1e-30))
        parts = [float(np.mean(np.abs(db(s, r) - d))) for s, r, d in zip(specs(seg), self.refs, self.dbs)]
        es = envelope(seg)
        env = float(np.mean(np.abs(20 * np.log10(np.maximum(es, self.env.max() * 10 ** (-RANGE / 20))) - self.env_db)))
        level = 20.0 * max(0.0, np.log10(0.05 / peak)) + 20.0 * max(0.0, peak - 0.9)
        # where the energy is: dB error of each band's share, weighted by the reference's share.
        # (The spectrogram terms count every time-frequency cell alike, loud or quiet; without
        # this the snare fit put its energy at 0.5-2 kHz, where the reference has 8 %.)
        be = band_energy(seg)
        balance = float(np.sum(self.bands * np.abs(10 * np.log10(np.maximum(be, 1e-4) / np.maximum(self.bands, 1e-4)))))
        texture = float(np.mean(np.abs(flatness(seg) - self.flat)))
        total = sum(parts) + 0.5 * env + 40.0 * outside + level + BALANCE * balance + TEXTURE * texture
        if detail:
            return {"total": total, "balance": balance, "texture": texture, "spec_fine": parts[0], "spec_mid": parts[1], "spec_coarse": parts[2], "env": env, "outside": outside,
                    "peak": float(peak), "latency_ms": (lag + PRE * FS) / FS * 1e3, "seg": seg}
        return total

def binary():
    c = [p for p in glob.glob(str(ROOT / "rack-plugin/rust/target/release/deps/chimera_rack-*")) if os.access(p, os.X_OK) and not p.endswith(".d")]
    return max(c, key=os.path.getmtime)

class Fitter:
    def __init__(self, sound, work):
        self.sound, self.work = sound, Path(work)
        self.work.mkdir(parents=True, exist_ok=True)
        cfg = SOUNDS[sound]
        self.names = list(cfg["params"])
        self.spec = [cfg["params"][n] for n in self.names]
        self.targets = [Target(REFDIR / r) for r in cfg["refs"]]
        self.bin = binary()
        self.evals = 0

    def bounds(self):
        return [(np.log(lo), np.log(hi)) if lg else (lo, hi) for lo, hi, _, lg in self.spec]

    def x0(self, start=None):
        """The engine's own values, or the values of an earlier fit (a parameter it lacks = default)."""
        vals = [(start or {}).get(n, d) for n, (_, _, d, _) in zip(self.names, self.spec)]
        x = np.array([np.log(np.clip(v, lo, hi)) if lg else np.clip(v, lo, hi) for v, (lo, hi, _, lg) in zip(vals, self.spec)])
        b = np.array(self.bounds())
        eps = 1e-9 * (b[:, 1] - b[:, 0])
        return np.clip(x, b[:, 0] + eps, b[:, 1] - eps)

    def values(self, x):
        return {n: float(np.exp(v) if s[3] else v) for n, s, v in zip(self.names, self.spec, x)}

    def render(self, xs):
        """xs: list of parameter vectors -> list of 0.6 s renders through the module's hit path."""
        vals = [self.values(x) for x in xs]
        fin, fout = self.work / "in.txt", self.work / "out.f32"
        fin.write_text("\n".join(self.sound + " " + " ".join(f"{k}={v:.9g}" for k, v in d.items()) for d in vals) + "\n")
        subprocess.run([self.bin, "sensitivity::fit_batch", "--ignored", "--exact"], env={**os.environ, "PHONA_FIT_IN": str(fin), "PHONA_FIT_OUT": str(fout)}, check=True, capture_output=True)
        y = np.fromfile(fout, np.float32).reshape(len(xs), N).astype(np.float64)
        return list(y)

    def cost(self, X):
        xs = list(X.T) if X.ndim == 2 else [X]
        ys = self.render(xs)
        self.evals += len(xs)
        return np.array([np.mean([t.loss(y) for t in self.targets]) for y in ys])

def main():
    sound, work = sys.argv[1], sys.argv[2]
    gens = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    f = Fitter(sound, work)
    base = float(f.cost(f.x0())[0])
    prev = Path(__file__).with_name(f"fit_{sound}.json")
    x0 = f.x0(json.load(open(prev))["params"]) if prev.exists() else f.x0()
    print(f"{sound}: {len(f.names)} parameters, engine sound loss {base:.2f}, start loss {float(f.cost(x0)[0]):.2f}", flush=True)
    hist = []
    def cb(intermediate_result):
        hist.append(float(intermediate_result.fun))
        if len(hist) % 5 == 0:
            print(f"  gen {len(hist):3d}  best {hist[-1]:.2f}  evals {f.evals}", flush=True)
    r = differential_evolution(f.cost, f.bounds(), x0=x0, popsize=3, maxiter=gens, tol=0, mutation=(0.4, 0.9), recombination=0.7,
                               vectorized=True, updating="deferred", polish=False, seed=seed, callback=cb, init="sobol")
    best = f.values(r.x)
    json.dump({"sound": sound, "baseline_loss": base, "loss": float(r.fun), "history": hist, "evals": f.evals,
               "params": best, "defaults": f.values(f.x0()), "x": list(map(float, r.x))}, open(f.work / f"fit_{sound}.json", "w"), indent=1)
    print(f"done: loss {base:.2f} -> {r.fun:.2f}")
    for k, v in best.items():
        print(f"  {k:14s} {f.values(f.x0())[k]:10.4g} -> {v:10.4g}")

if __name__ == "__main__":
    main()
