# Word judge for sung lyrics: P(right word | audio) among about 150 similar words, by forced
# scoring in Whisper (small.en). The real voice of the macOS "say" tool scores 94 %.
#
#   python3 -m venv ~/.cache/phona-judge && ~/.cache/phona-judge/bin/pip install faster-whisper scipy
#   (cd rust && cargo build --release --example lyrab)
#   ~/.cache/phona-judge/bin/python tools/word_judge.py render <name> [TUNE]   30 words at C3 and G3
#   ~/.cache/phona-judge/bin/python tools/word_judge.py files pop=a.wav top=b.wav
# env: WORDS="pop top"  PITCHES="48 55"  BRIEF=1; other env goes to lyrab (TUNE by index, HELD, NASAL).
# One run of 60 files takes about 5 minutes. Scores of one word move by 10 points from small
# changes: read the mean over the list, not single words.
import sys, os, subprocess, numpy as np
from concurrent.futures import ThreadPoolExecutor
from scipy.io import wavfile
from scipy.signal import resample_poly
from faster_whisper import WhisperModel
from faster_whisper.tokenizer import Tokenizer
import ctranslate2
OUT = os.environ.get("JUDGE_OUT", "/tmp/phona-judge")
BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "rust", "target", "release", "examples", "lyrab")
STOPS = "pop top cat tea do cup tap key pie two back bit dog big kick hot get put bed tip".split()
CTRL = "moon bee love fish church ma no see sun day".split()
EXTRA = "pub pup bob pa huh uh hop tab chop tub head had hat cap cab cut cod he she hey bye buy pi to coo you bid bet beg bag bat bad bug buck bake dock duck doc dot ha hut hop hog good god got gut food foot but bud bub dip chip hip keep cheap heat eat it at up a ah oh eh oo me knee be sea so sew son some sum say they may lay die tie guy why who goo too due new mom ma'am mama no know low love loaf laugh fit fist fish this miss wish search birch chirp hurt perch".split()
CAND = sorted(set(STOPS + CTRL + EXTRA))
m = WhisperModel("small.en", device="cpu", compute_type="int8")
tk = Tokenizer(m.hf_tokenizer, False, task="transcribe", language="en")
PROMPT = list(tk.sot_sequence) + [tk.no_timestamps]
CT = [[tk.encode(" " + v) for v in (w, w.capitalize(), w.upper())] for w in CAND]
def logsm(v):
    v = np.asarray(v, dtype=np.float64); v = v - v.max(); return v - np.log(np.exp(v).sum())
ENDS = [tk.eot, tk.encode(".")[0], tk.encode("!")[0], tk.encode("?")[0]]
def score(x):
    feats = m.feature_extractor(x)[:, :3000]
    if feats.shape[1] < 3000: feats = np.pad(feats, ((0, 0), (0, 3000 - feats.shape[1])))
    enc = np.array(m.encode(feats))
    need = set()
    for variants in CT:
        for toks in variants:
            for i in range(len(toks)): need.add(tuple(toks[:i]))
    cache = {}
    for n in sorted(set(len(p) for p in need)):
        ps = [p for p in need if len(p) == n]
        for k in range(0, len(ps), 64):
            chunk = ps[k:k + 64]
            e = ctranslate2.StorageView.from_array(np.ascontiguousarray(np.repeat(enc, len(chunk), axis=0)))
            r = m.model.generate(e, [PROMPT + list(p) for p in chunk], max_length=len(PROMPT) + n + 1, return_logits_vocab=True, suppress_blank=False, suppress_tokens=[], beam_size=1)
            for p, rr in zip(chunk, r): cache[p] = logsm(np.array(rr.logits[0][0]).reshape(-1))
    out = []
    for variants in CT:
        lps = []
        for toks in variants:
            lp = sum(cache[tuple(toks[:i])][t] for i, t in enumerate(toks))
            lps.append(lp)
        out.append(np.logaddexp.reduce(lps))
    return np.array(out)
def load(f):
    sr, x = wavfile.read(f); x = x.astype(np.float32) / 32768
    if x.ndim > 1: x = x.mean(1)
    return resample_poly(x, 16000, sr).astype(np.float32)
HOMO = [["two", "to", "too"], ["bee", "be"], ["sun", "son"], ["pie", "pi"], ["see", "sea"], ["no", "know"], ["bye", "buy"], ["so", "sew"], ["some", "sum"], ["dock", "doc"], ["ma", "ma'am"], ["do", "due"], ["new", "knee"][:1]]
def report(name, items):   # items: (group, word, column, wav)
    rows = {}; tot = {}
    for grp, w, col, f in items:
        lp = score(load(f)); p = np.exp(lp - lp.max()); p /= p.sum()
        same = [CAND.index(h) for g in HOMO if w in g for h in g] or [CAND.index(w)]
        pt = sum(p[i] for i in same); best = int(np.argmax(lp))
        rows.setdefault((grp, w), []).append("%3.0f%% %-6s" % (100 * pt, CAND[best] if best not in same else "ok"))
        tot.setdefault(grp, []).append((pt, best in same))
    for grp in dict.fromkeys(g for g, _ in rows):
        t = tot[grp]
        print("%s %s: mean P %.1f%%  top-1 %d/%d" % (name, grp, 100 * np.mean([a for a, _ in t]), sum(b for _, b in t), len(t)))
        if not os.environ.get("BRIEF"):
            for (g, w), cols in rows.items():
                if g == grp: print("   %-7s" % w + "  ".join(cols))
if sys.argv[1] == "files":
    report("files", [("given", a.split("=")[0], 0, a.split("=")[1]) for a in sys.argv[2:]])
else:
    name = sys.argv[2]; tune = sys.argv[3] if len(sys.argv) > 3 else ""
    words = os.environ.get("WORDS", "").split() or STOPS + CTRL
    pitches = [int(x) for x in os.environ.get("PITCHES", "48 55").split()]
    jobs = []
    for w in words:
        for midi in pitches:
            d = f"{OUT}/{name}/{midi}/{w}"; os.makedirs(d, exist_ok=True)
            env = dict(os.environ, MIDI=str(midi))
            if tune: env["TUNE"] = tune
            jobs.append((("stops" if w in STOPS else "control"), w, midi, d, env))
    with ThreadPoolExecutor(6) as ex: list(ex.map(lambda j: subprocess.run([BIN, j[3], j[1]], env=j[4], capture_output=True), jobs))
    report(name, [(g, w, midi, d + "/B.wav") for g, w, midi, d, _ in jobs])
