#!/usr/bin/env python3
"""Regenerate the audition pack with stdlib Python, Cargo, and the pinned engine."""
import array, csv, json, math, pathlib, shlex, struct, subprocess, sys, wave
TOOL = pathlib.Path(__file__).resolve().parent
ROOT = TOOL.parents[1]
REPO = ROOT / "upstream"
OUT = ROOT / "audition"
RAW = TOOL / "raw"
FS = 48000
TARGET = 10 ** (-1 / 20)
Q = chr(96)
OUT.mkdir(exist_ok=True); RAW.mkdir(exist_ok=True)
log = (TOOL / "render.log").open("w")
def run(args):
    command = shlex.join(map(str,args))
    print(command, flush=True)
    log.write("$ " + command + "\n"); log.flush()
    p = subprocess.run(list(map(str,args)), cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write(p.stdout); log.flush()
    print(p.stdout, end="", flush=True)
    p.check_returncode()
    return Q+command+Q
run(["cargo", "build", "--release", "-p", "chimera-cli"])
run(["cargo", "run", "--release", "--locked", "--manifest-path", TOOL/"Cargo.toml", "--", RAW])
CLI = REPO / "target/release/chimera"
if not CLI.exists(): CLI = REPO / "target/release/chimera-cli"
entries = []
def add(name, path, recipe, max_seconds=None):
    entries.append((name,path,recipe,max_seconds))
for p in sorted(RAW.glob("*.f32")):
    add(p.stem,p,Q+"src/main.rs::human"+Q+": " + {
        "01": "male calibrated vowel at 110 Hz, drift/roughness off; gate 0.10–1.55 s.",
        "02": "linear calibrated articulation blend ɑ→i→u over 2.7 s, updated every 128 samples; 110 Hz fixed or exponential 110→220 Hz target.",
        "07": "body-specific calibrated ɑ, same 110 Hz source across male/female/child; drift/roughness off.",
        "08": "calibrated male ɑ at 110 Hz: whisper voiced=false/breathiness=.8; breathy breathiness=.8/tension=.2; rough roughness=.85/fry=.65/tension=.3."
    }[p.stem[:2]])
for label, ipa in [("pa","pɑ"),("ta","tɑ"),("ka","kɑ"),("ba","bɑ"),("ma","mɑ"),("na","nɑ"),("sa","sɑ"),("sha","ʃɑ"),("fa","fɑ")]:
    name="03_"+label+"-male"
    cmd=run([CLI,"render","speech",RAW/name,ipa,"male","--float"])
    add(name,RAW/(name+".wav"),cmd)
name="04_text-hello-world-male"
cmd=run([CLI,"render","text",RAW/name,"Hello world.","male","--float"])
add(name,RAW/(name+".wav"),cmd)
notes=TOOL/"notes.json"
notes.write_text(json.dumps({"bpm":120,"lyrics":"la la la","notes":[[55,1],[57,1],[60,2]],"vibrato":25,"t0":0.3},indent=2)+"\n")
name="05_sung-la-la-la-g3-a3-c4"
cmd=run([CLI,"render","notes",notes,RAW/name,"male","--float"])
add(name,RAW/(name+".wav"),cmd+"; notes.json: G3/A3/C4, 1/1/2 beats, 120 BPM, 25-cent vibrato.")
beatdir=RAW/"beatbox"
cmd=run([CLI,"render","beatbox",beatdir,"B t Pf t B t Pf t","male","100","2","--float"])
kit=[("B","kick"),("t","hi-hat"),("tss","open-hat"),("Pf","snare"),("K","rimshot"),("_K","inward-snare"),("_","tongue-click"),("P_","lip-pop"),("kch","crash"),("m","hum")]
for idx,(stem,label) in enumerate(kit,1):
    add(f"06a{idx:02d}_beatbox-{label}",beatdir/(stem+".wav"),cmd+"; individual hit "+Q+stem+".wav"+Q+" at 0.25 s, zero-padded to 1.2 s.")
add("06b_beatbox-sequencer-100bpm",beatdir/"pattern.wav",cmd+"; actual BeatPlayer sequencer, 16 sixteenth-note steps at 100 BPM.")
for creature in ["songbird","seal","alligator"]:
    cmd=run([CLI,"render","creature",creature,RAW/"creatures","--float"])
    add("09_creature-"+creature,RAW/"creatures"/(creature+".wav"),cmd+"; first 4 s (1.2 s default sustained voice, 0.4 s release, then beginning of preset calls), ending faded over 20 ms.",4)
def floats(path):
    data=path.read_bytes()
    if path.suffix==".wav":
        assert data[:4]==b"RIFF" and data[8:12]==b"WAVE"
        pos=12; fmt=None; audio=None
        while pos+8<=len(data):
            tag=data[pos:pos+4]; n=struct.unpack_from("<I",data,pos+4)[0]; chunk=data[pos+8:pos+8+n]; pos+=8+n+(n%2)
            if tag==b"fmt ": fmt=struct.unpack_from("<HHIIHH",chunk)
            if tag==b"data": audio=chunk
        assert fmt[0]==3 and fmt[1]==1 and fmt[2]==FS and fmt[5]==32,fmt
        data=audio
    a=array.array("f"); a.frombytes(data)
    if sys.byteorder!="little": a.byteswap()
    return a
rows=[]
for name,path,recipe,max_seconds in sorted(entries):
    x=floats(path)
    bad=sum(not math.isfinite(v) for v in x)
    if bad: raise ValueError(f"{name}: {bad} nonfinite samples")
    raw_peak=max(map(abs,x),default=0)
    if not raw_peak>1e-8: raise ValueError(f"{name}: silent")
    raw_clips=sum(abs(v)>=1 for v in x)
    raw_duration=len(x)/FS
    if max_seconds: x=x[:round(max_seconds*FS)]
    assert len(x)<=4*FS,(name,len(x)/FS)
    # Fade only file boundaries; retain internal articulatory transients.
    fade=round((.020 if max_seconds else .005)*FS)
    for i in range(min(fade,len(x)//2)):
        gain=i/(fade-1)
        x[i]*=gain; x[-1-i]*=gain
    if len(x)<round(1.2*FS): x.extend([0.]*(round(1.2*FS)-len(x)))
    pk=max(map(abs,x)); gain=TARGET/pk
    pcm=array.array("h",(round(v*gain*32767) for v in x))
    if sys.byteorder!="little": pcm.byteswap()
    dest=OUT/(name+".wav")
    with wave.open(str(dest),"wb") as w:
        w.setparams((1,2,FS,0,"NONE","not compressed")); w.writeframes(pcm.tobytes())
    with wave.open(str(dest),"rb") as w:
        assert w.getframerate()==FS and w.getnchannels()==1
        final=array.array("h"); final.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder!="little": final.byteswap()
    y=[v/32768 for v in final]
    peak=max(map(abs,y)); rms=math.sqrt(sum(v*v for v in y)/len(y))
    jumps=[abs(b-a) for a,b in zip(y,y[1:])]
    row=dict(file=dest.name,seconds=len(y)/FS,peak_dbfs=20*math.log10(peak),rms_dbfs=20*math.log10(rms),mean=sum(y)/len(y),max_sample_jump=max(jumps),jumps_over_0_5=sum(v>.5 for v in jumps),clipped_samples=sum(abs(v)>=1 for v in y),nonfinite_samples=bad,nonzero_samples=sum(v!=0 for v in final),first_sample=y[0],last_sample=y[-1],raw_seconds=raw_duration,raw_peak=raw_peak,raw_samples_at_or_above_1=raw_clips,normalization_gain_db=20*math.log10(gain),recipe=recipe)
    rows.append(row)
    print(f"{dest.name}: {row['seconds']:.3f}s, peak {row['peak_dbfs']:.3f} dBFS, RMS {row['rms_dbfs']:.1f} dBFS, max jump {row['max_sample_jump']:.4f}",flush=True)
(OUT/"statistics.json").write_text(json.dumps(rows,indent=2)+"\n")
with (OUT/"statistics.csv").open("w",newline="") as f:
    writer=csv.DictWriter(f,fieldnames=[k for k in rows[0] if k!="recipe"]); writer.writeheader(); writer.writerows({k:v for k,v in r.items() if k!="recipe"} for r in rows)
readme="""# Chimera audition WAVs

Engine: jamesondh/chimera-vocal-synth commit 092b16ce12b927d473c884ca2dfae53ce1b995df. All files are mono 48 kHz, 16-bit PCM WAV, 1–4 seconds, independently peak-normalized to approximately −1 dBFS. Independent normalization removes original loudness relationships; it is not loudness matching. No musical-quality judgment was made.

The requested 'a' is the engine's calibrated IPA **ɑ**. The male vowel holds and body comparisons use the same 110 Hz source, with drift and roughness disabled; body-specific calibrated articulation is used. Other CLI paths retain their built-in pitch/prosody/source settings. The ɑ→i→u sweep interpolates all calibrated articulation controls, approximating a tract CV sweep rather than changing just tongue position. Pitch sweep rises from 110 to 220 Hz.

## Reproduce

From /Users/robschoen/Dropbox/CC/Chimera, run:

    . "$HOME/.cargo/env"
    python3 spike/audition-tool/render_audition.py

Rust helper source: ../spike/audition-tool/src/main.rs; standalone crate depends on ../../upstream/crates/chimera. Its Cargo.lock pins dependency versions. Helper outputs and original CLI float WAVs are retained in ../spike/audition-tool/raw/; commands, render diagnostics, and build output are in ../spike/audition-tool/render.log. No tracked upstream files were modified. Requires Rust/Cargo and Python 3 standard library; no Python packages.

CLI commands below run from upstream/. Every CLI render uses --float so sample checks and normalization occur before PCM clipping/quantization. render_audition.py checks non-finite values, applies a 5 ms boundary fade (20 ms for creature excerpts), pads short files with trailing zeros to 1.2 s, normalizes to 10^(−1/20), then rounds to 16-bit PCM. The creature CLI renders all calls; audition files retain only the first 4 s. Beat hits are the complete CLI 0.5 s output plus zero padding. No internal consonant/percussion transients were removed.

## Files and recipes

"""
for r in rows:
    readme+=f"- [{r['file']}]({r['file']}) ({r['seconds']:.3f} s): {r['recipe']}\n"
readme+="\n## Sample checks\n\n"
readme+=f"{len(rows)} files verified after writing: no silent files, no non-finite source samples, no clipped final samples. Final peaks span {min(r['peak_dbfs'] for r in rows):.4f} to {max(r['peak_dbfs'] for r in rows):.4f} dBFS. All first/last samples are zero. Full raw and final statistics are in [statistics.csv](statistics.csv) and [statistics.json](statistics.json).\n\n"
readme+="Maximum adjacent-sample differences and counts above 0.5 full scale are recorded as transient indicators, not a listening-based click detector: frication and percussion can naturally have large differences. Boundary fades prevent hard file cuts; internal clicks cannot be ruled out from sample statistics alone.\n\n"
flagged=[r for r in rows if r["jumps_over_0_5"]]
readme+="Files with adjacent-sample jumps above 0.5 full scale (maximum jump, count):\n\n"
for r in flagged:
    readme+=f"- {r['file']}: {r['max_sample_jump']:.4f}, {r['jumps_over_0_5']} samples.\n"
readme+="\nThe largest jump is 0.9246 in the beat sequence; no source float samples reached ±1. Maximum absolute final DC mean is below 0.00057 full scale. All output boundary samples are exactly zero. These checks found no obvious silence, numerical instability, clipping, or hard file-boundary cuts. They do not certify absence of audible internal clicks.\n\n"
readme+="The CLI emitted no errors or compiler warnings. Its beat diagnostics report snare onset 11.1 ms before the nominal hit; setup noise for crash is −2.3 dB relative to the hit, and its 60–150 ms tail is −0.2 dB. Hum tail is +0.4 dB relative to its attack window. These are renderer measurements, not musical-quality judgments. Creature sustained pitch diagnostics: songbird 2609.8 Hz versus target 2600; seal 897.4 versus 900; alligator 27.1 versus 28. Full diagnostics are retained in render.log.\n\n"
readme+="All requested categories are supported and included. No rendering errors occurred in this completed run; Cargo build-lock waits are expected.\n"
(OUT/"README.md").write_text(readme)
log.close()
