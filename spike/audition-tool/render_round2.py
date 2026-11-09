#!/usr/bin/env python3
"""Round 2: stdlib postprocessing, isolated helper, pinned upstream read-only."""
import array,csv,hashlib,json,math,pathlib,subprocess,sys,wave
TOOL=pathlib.Path(__file__).resolve().parent
ROOT=TOOL.parents[1]; OUT=ROOT/"audition/round2"; RAW=TOOL/"raw-round2"
OUT.mkdir(parents=True,exist_ok=True); RAW.mkdir(exist_ok=True)
FS=48000; TARGET=10**(-1/20)
assert subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT/"upstream",text=True).strip()=="092b16ce12b927d473c884ca2dfae53ce1b995df"
before=subprocess.check_output(["git","status","--porcelain"],cwd=ROOT/"upstream")
defaults=dict(f0=110.,glide=.06,pressure=8.,breathiness=.15,tension=.5,fry=0.,roughness=.1,vibratoDepth=0.,vibratoRate=6.,drift=5.,voiced=True,brightness=.25)
specs=[]
def add(name,kind,female,child=0.,base=None,patch=None,lar=0.,delayed=False):
    source=defaults|dict(patch or {})
    if base is not None: source["f0"]=440*2**((base-69)/12)
    specs.append(dict(name=name,kind=kind,female=female,child=child,base_midi=base,source=source,larynx_delta_cm=lar,delayed=delayed))
variants=[("breathiness-0p35",dict(breathiness=.35),0.,0.),("tension-0p70",dict(tension=.7),0.,0.),("tension-0p30",dict(tension=.3),0.,0.),("brightness-0p55",dict(brightness=.55),0.,0.),("pressure-9",dict(pressure=9.),0.,0.),("child-axis-0p30",{},.3,0.),("larynx-raised-0p5cm",{},0.,.5),("combined-best-guess",dict(breathiness=.35,tension=.55,brightness=.55,pressure=9.),.3,.5)]
for label,base in [("a3-e4",57),("a4-e5",69)]:
    add("A_female-"+label+"-default","phrase",1.,base=base)
    for label2,patch,child,lar in variants:
        add("B_female-"+label+"-"+label2,"phrase",1.,child,base,patch,lar)
add("C_male-baritone-g2-d3-default","phrase",0.,base=43)
add("C_male-tenor-c4-g4-default","phrase",0.,base=60)
add("C_male-tenor-c4-g4-belt-brightness-0p55-pressure-10","phrase",0.,base=60,patch=dict(brightness=.55,pressure=10.))
for label,female,f0 in [("male",0.,147.),("female",1.,294.)]:
    for depth,rate,delay in [(d,5.5,False) for d in [0,15,30,50,80]]+[(30,4.5,False),(30,6.5,False),(30,5.5,True)]:
        name=f"D_{label}-{int(f0)}hz-depth-{depth}c-rate-{str(rate).replace('.','p')}hz"+("-delayed-600ms" if delay else "")
        add(name,"sustain",female,patch=dict(f0=f0,vibratoDepth=0. if delay else float(depth),vibratoRate=rate),delayed=delay)
manifest=OUT/"parameters.json"; manifest.write_text(json.dumps(specs,indent=2)+"\n")
cmd=["cargo","run","--release","--locked","--manifest-path",str(TOOL/"Cargo.toml"),"--bin","round2","--",str(manifest),str(RAW)]
with (OUT/"render.log").open("w") as log:
    p=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT); p.check_returncode()
rows=[]
for s in specs:
    name=s["name"]; data=(RAW/(name+".f32")).read_bytes(); x=array.array("f"); x.frombytes(data)
    if sys.byteorder!="little": x.byteswap()
    bad=sum(not math.isfinite(v) for v in x); assert bad==0
    raw_peak=max(map(abs,x)); assert raw_peak>1e-8
    raw_clips=sum(abs(v)>=1 for v in x)
    for i in range(240):
        g=i/239; x[i]*=g; x[-1-i]*=g
    gain=TARGET/max(map(abs,x)); pcm=array.array("h",(round(v*gain*32767) for v in x))
    if sys.byteorder!="little": pcm.byteswap()
    dest=OUT/(name+".wav")
    with wave.open(str(dest),"wb") as w:
        w.setparams((1,2,FS,0,"NONE","not compressed")); w.writeframes(pcm.tobytes())
    with wave.open(str(dest),"rb") as w:
        assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,FS)
        final=array.array("h"); final.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder!="little": final.byteswap()
    y=[v/32768 for v in final]; jumps=[abs(b-a) for a,b in zip(y,y[1:])]
    row=dict(file=dest.name,seconds=len(y)/FS,peak_dbfs=20*math.log10(max(map(abs,y))),rms_dbfs=20*math.log10(math.sqrt(sum(v*v for v in y)/len(y))),mean=sum(y)/len(y),max_sample_jump=max(jumps),jumps_over_0_5=sum(v>.5 for v in jumps),clipped_samples=sum(abs(v)>=1 for v in y),nonfinite_samples=bad,nonzero_samples=sum(v!=0 for v in final),first_sample=y[0],last_sample=y[-1],raw_seconds=len(x)/FS,raw_peak=raw_peak,raw_samples_at_or_above_1=raw_clips,normalization_gain_db=20*math.log10(gain),sha256=hashlib.sha256(dest.read_bytes()).hexdigest())
    assert row["clipped_samples"]==0 and y[0]==y[-1]==0
    rows.append(row)
(OUT/"statistics.json").write_text(json.dumps(rows,indent=2)+"\n")
with (OUT/"statistics.csv").open("w",newline="") as f:
    w=csv.DictWriter(f,fieldnames=rows[0]); w.writeheader(); w.writerows(rows)
assert subprocess.check_output(["git","status","--porcelain"],cwd=ROOT/"upstream")==before
readme="""# Chimera singing audition — round 2

37 mono 48 kHz, 16-bit PCM WAVs; each independently peak-normalized to −1 dBFS (quantized peak approximately −1.0002). Normalization removes original level relationships; these are not loudness matched. No musical-quality assessment was made. Upstream commit: `092b16ce12b927d473c884ca2dfae53ce1b995df`; tracked upstream files unchanged.

## Reproduce

From `/Users/robschoen/Dropbox/CC/Chimera`:

```sh
. "$HOME/.cargo/env"
python3 spike/audition-tool/render_round2.py
```

The script invokes `cargo run --release --locked --manifest-path spike/audition-tool/Cargo.toml --bin round2 -- audition/round2/parameters.json spike/audition-tool/raw-round2` (absolute paths internally). The helper extends the existing audition crate with `src/bin/round2.rs`; original round-one helper and notes.json remain available. Rust uses upstream as a path dependency. Raw little-endian float32 samples are retained in `spike/audition-tool/raw-round2/`. Build/render diagnostics are in [render.log](render.log). The supplied helper Cargo.lock pins dependencies; the script uses `--locked`.

Each parameters.json entry reproduces exactly one file: pass a JSON array containing only that entry to the helper, then apply this script’s postprocessing. Every WAV is the entire render, with 5 ms linear file-boundary fades, peak scaling to `10^(-1/20)`, and rounding with `round(sample * gain * 32767)` into signed PCM16. No resampling, trimming, compression, or internal fades. Full source parameters per file are in [parameters.json](parameters.json); raw/final statistics and SHA256 hashes are in [statistics.csv](statistics.csv) and [statistics.json](statistics.json).

## Shared phrase and controls

A/B/C all use five repeated “ah” syllables, explicit IPA `ɑ` (no G2P), offsets `[0, 2, 4, 7, 0]` semitones, beats `[1, 1, 1, 1, 2]`, 110 BPM. First vowel beat 0.300 s; score ends 3.572727 s; file duration 3.972729 s including 0.4 s release. This is the same phrase transposed: female A3 B3 C♯4 E4 A3 or A4 B4 C♯5 E5 A4; baritone G2 A2 B2 D3 G2; tenor C4 D4 E4 G4 C4 (within the requested C3–G4 range). Female first pitch is 220 or 440 Hz, never 110 Hz.

The engine’s `plan_song` and body-calibrated vowel gestures supply breath, articulation, note transitions and release. No gate is manually applied to melodic files. All planner vibrato targets are removed so source depth 0 remains 0. Score F0 retains upstream’s base-weight blending and its 0.03 s singing glide (source glide remains .06 s): listed MIDI/Hz are targets, not a claim of acoustically exact measured F0. Source f0 is initialized to the first note. Default drift and roughness are retained in every file, including depth-zero vibrato controls.

Default source (except phrase/sustain pitch): f0=110 Hz, glide=0.06 s, pressure=8, breathiness=0.15, tension=0.5, fry=0, roughness=0.1, vibrato_depth=0 cents, vibrato_rate=6 Hz, drift=5 cents, voiced=true, brightness=0.25. Pressure uses cm H₂O in the human-voice pressure conversion; breathiness/tension/brightness/roughness/fry are dimensionless controls. SourcePatch JSON uses camelCase (`vibratoDepth`, `vibratoRate`).

A uses these defaults. Each B single variation changes only its named control relative to the A file at the same pitch; both female registers receive every variation. The combined-best-guess file changes breathiness=.35, tension=.55, brightness=.55, pressure=9, child=.30 and larynx offset=+.5 cm; its name is a hypothesis for human audition, not a quality verdict. C belt changes brightness=.55 and pressure=10 relative to the tenor default.

BodyMix female=1, child=0 is female; female=0, child=0 is male. Female=1, child=.30 gives 70% female + 30% child, 0% male; weights are `(1-c)*(1-f), (1-c)*f, c`. The blend changes anatomy and its calibrated vowel articulation. For raised larynx, `ArticKey::Larynx` is supported (positive cm raises it). The helper adds +.5 cm to each planned calibrated larynx target and to the base vowel posture, preserving all other targets; this avoids gestures overriding a base-only larynx setting. Geometry multiplies the larynx offset by body scale; articulator dynamics and score/base blending remain active.

## Vibrato semantics and timing

Code references are relative to upstream: `crates/chimera/src/voice/human_voice.rs` (`DEFAULT_SOURCE`, `apply_source`, `control`) and `crates/chimera/src/sources/glottis.rs` (`Glottis::next`). Source depth passes directly to the glottis. The implementation computes:

```
vib_phase += vibrato_rate * 32 / internal_sample_rate * (1 + 0.04 * drift_value)
cents = vibrato_depth * sin(2*pi*vib_phase) + drift * drift_value
pitch_factor = 2^(cents/1200)
```

Thus depth is **peak cents**, not semitones, fraction, or peak-to-peak. 30 cents means nominal ±30 cents (60 cents peak-to-peak), plus the separate drift/jitter. Rate is **nominal cycles/second (Hz)**; its instantaneous value is modulated by `1 + .04*drift_value`, even when source drift depth is zero. Evaluation is decimated every 32 internal samples (96 kHz internal rate with 2× oversampling). Cycle jitter/shimmer are also retained, so acoustic measurements need not equal the nominal LFO values exactly.

D files use body-calibrated sustained `ɑ`, male 147 Hz / female 294 Hz, gate on exactly at 0.100 s and off at 3.100 s (3 s gate, 3.5 s total). Source depth is controlled directly, without score gestures. Delayed variants start at depth 0, call `set_source(vibrato_depth=30)` at 0.700 s (600 ms after gate onset), and retain rate 5.5 Hz. This is a depth step, not a depth ramp; the vibrato oscillator free-runs from construction and is not phase-reset at onset. Rendering in 48-sample blocks places events at exact output sample indices.

Upstream CSD settings: `docs/csd-comparison.md` says these match `render song`; `crates/chimera-cli/src/compare.rs::render_notes` and `render.rs::songs/notes` set pressure=9, tension=.55, correct score pitches and 35-cent planned vibrato, with other source defaults (rate=6 Hz). The singing planner starts its vibrato gesture 0.22 s into a vowel with a 0.3 s activation ramp and blends against the base depth with weight .05. Those tuned singing overrides are deliberately not substituted for the requested default-source baselines. No CSD recording or derived audio was used.

All requested controls, including partial child blend, raised larynx and mid-note delayed vibrato, are supported through the helper. The stock notes CLI cannot set the full source patch/body mix or a mid-note source change, so it alone cannot produce these controlled comparisons. Beatbox files are omitted as requested.

## File recipes

Every file below uses the shared timing and defaults above, with exactly these overrides. The JSON manifest records all values explicitly.

"""
for s,r in zip(specs,rows):
    changes={k:v for k,v in s["source"].items() if v!=defaults[k]}
    if s["kind"]=="phrase":
        recipe=f"phrase base MIDI {s['base_midi']}; "
    else: recipe="sustain; "
    recipe+=f"BodyMix(female={s['female']}, child={s['child']}); source overrides {json.dumps(changes,sort_keys=True)}; larynx delta {s['larynx_delta_cm']} cm"
    if s["delayed"]: recipe+="; set depth=30 cents at sample 33600"
    readme+=f"- [{r['file']}]({r['file']}) ({r['seconds']:.6f} s): {recipe}.\n"
readme+="\n## Sanity checks\n\n"
readme+=f"{len(rows)} WAVs re-opened and checked: correct mono/48 kHz/PCM16 format; no silent files, nonfinite source samples, final clipped samples, or nonzero file-boundary samples. Peak range {min(r['peak_dbfs'] for r in rows):.6f} to {max(r['peak_dbfs'] for r in rows):.6f} dBFS. Maximum absolute DC mean {max(abs(r['mean']) for r in rows):.8f}; maximum adjacent-sample jump {max(r['max_sample_jump'] for r in rows):.6f}. Raw peak maximum {max(r['raw_peak'] for r in rows):.6f}; {sum(r['raw_samples_at_or_above_1'] for r in rows)} raw samples at/above ±1.\n\n"
flagged=[r for r in rows if r["jumps_over_0_5"]]
readme+="Adjacent-sample differences above 0.5 full scale (transient indicators, not a listening-based click detector):\n\n" if flagged else "No adjacent-sample jumps above 0.5 full scale.\n"
for r in flagged: readme+=f"- {r['file']}: maximum {r['max_sample_jump']:.6f}, count {r['jumps_over_0_5']}.\n"
readme+="\nNo unsupported requested controls or render failures. Sample checks do not certify musical quality, perceived gender, or absence of audible internal clicks.\n"
(OUT/"README.md").write_text(readme)
print(json.dumps(dict(files=len(rows),max_jump=max(r["max_sample_jump"] for r in rows),flagged=[r["file"] for r in flagged],max_dc=max(abs(r["mean"]) for r in rows),max_raw_peak=max(r["raw_peak"] for r in rows)),indent=2))
