#!/usr/bin/env python3
import csv, hashlib, json, math, pathlib, statistics, subprocess, wave
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[2]
TOOL=ROOT/'spike/audition-tool'; RAW=TOOL/'raw-round3'; DEST=ROOT/'audition/round3'; DEST.mkdir(parents=True,exist_ok=True)
FS=48000; PEAK=10**(-1/20)
if not (RAW/'timings.json').exists():
    with (TOOL/'round3-render.log').open('w') as log:
        subprocess.run(['cargo','run','--release','--locked','--manifest-path',str(TOOL/'Cargo.toml'),'--bin','round3','--',str(RAW)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
manifest=json.loads((RAW/'manifest.json').read_text()); timing=json.loads((RAW/'timings.json').read_text())
data={p.stem:np.fromfile(p,dtype='<f4').astype(float) for p in RAW.glob('*.f32')}
bad={name:np.flatnonzero(~np.isfinite(x)) for name,x in data.items() if not np.isfinite(x).all()}
assert not any(not name.startswith('quality-') for name in bad), 'Invalid Engine control render'
clean={name:np.where(np.isfinite(x),x,0.) for name,x in data.items()}
def db(x):return 20*math.log10(max(float(x),1e-15))
def rms(x):return np.sqrt(np.mean(x*x))
def features(x):
    n=4096; w=np.hanning(n); frames=np.lib.stride_tricks.sliding_window_view(x,n)[::2048]
    p=np.mean(abs(np.fft.rfft(frames*w,axis=1))**2,axis=0);p[1:-1]*=2;f=np.fft.rfftfreq(n,1/FS)
    bands=np.array([p[(f>=a)&(f<b)].sum() for a,b in [(0,1000),(1000,3000),(3000,8000)]])
    edges=100*2**(np.arange(21)/3);edges=np.append(edges[edges<8000],8000)
    shape=np.array([p[(f>=a)&(f<b)].sum() for a,b in zip(edges[:-1],edges[1:])]);shape/=p[f<8000].sum()
    return (f*p).sum()/p.sum(),bands,shape
def score(a,b):return float(np.sqrt(np.mean((10*np.log10(np.maximum(b,1e-6)/np.maximum(a,1e-6)))**2)))
gain=PEAK/max(abs(x).max() for name,x in data.items() if not name.startswith('quality-'))
gains={name:gain if not name.startswith('quality-') else PEAK/abs(clean[name]).max() for name,x in data.items()}
stats=[]
for name,x in sorted(data.items()):
    pcm=np.rint(clean[name]*gains[name]*32767).astype('<i2')
    filename=name+('-FAILED-nonfinite-silenced' if name in bad else '')+'.wav'
    with wave.open(str(DEST/filename),'wb') as w:w.setnchannels(1);w.setsampwidth(2);w.setframerate(FS);w.writeframes(pcm.tobytes())
    blocks=[rms(z) for z in np.array_split(x[24000:],max(1,(len(x)-24000)//4800))]
    stats.append(dict(file=filename,samples=len(x),raw_peak=float(abs(x[np.isfinite(x)]).max()),raw_rms_dbfs=None if name in bad else db(rms(x)),gain=gains[name],wav_rms_dbfs=db(rms(pcm.astype(float)/32768)),wav_peak_dbfs=db(abs(pcm.astype(float)).max()/32768),nonfinite=int((~np.isfinite(x)).sum()),raw_samples_ge_1=int((abs(x)>=1).sum()),max_adjacent_jump=None if name in bad else float(abs(np.diff(x)).max()),first_nonfinite_seconds=float(bad[name][0]/FS) if name in bad else None,steady_min_100ms_rms_dbfs=None if name in bad else db(min(blocks)),steady_max_100ms_rms_dbfs=None if name in bad else db(max(blocks)),sha256=hashlib.sha256((DEST/filename).read_bytes()).hexdigest()))
rows=[]
for s in manifest:
 if s['mode']!='min':continue
 name=s['name'];a=data[name][24000:];b=data[name[:-3]+'max'][24000:];ca,ba,sa=features(a);cb,bb,sb=features(b)
 rows.append(dict(control=s['control'],gender=s['gender'],minimum=s['min'],maximum=s['max'],spectral_change_db=score(sa,sb),rms_delta_db=db(rms(b)/rms(a)),centroid_delta_hz=cb-ca,band_0_1k_db=float(10*np.log10(bb[0]/ba[0])),band_1_3k_db=float(10*np.log10(bb[1]/ba[1])),band_3_8k_db=float(10*np.log10(bb[2]/ba[2])),min_analysis_rms_dbfs=db(rms(a)),max_analysis_rms_dbfs=db(rms(b)),spectral_comparison_valid=bool(db(rms(a))>-80 and db(rms(b))>-80),endpoint_max_abs_difference=float(abs(data[name]-data[name[:-3]+'max']).max())))
rank=sorted({r['control'] for r in rows},key=lambda c:(not all(r['spectral_comparison_valid'] for r in rows if r['control']==c),-statistics.mean(r['spectral_change_db'] for r in rows if r['control']==c)))
rows.sort(key=lambda r:(rank.index(r['control']),r['gender']!='male'))
def pitches(x):
 result={}
 for start in range(0,len(x)-3840,960):
    z=x[start:start+3840];z=(z-z.mean())*np.hanning(len(z));ac=np.fft.irfft(abs(np.fft.rfft(z,8192))**2,8192)[:3840];ac/=max(ac[0],1e-30)
    peaks=np.where((ac[69:799]>ac[68:798])&(ac[69:799]>ac[70:800]))[0]+69
    if not len(peaks):continue
    lag=int(peaks[np.argmax(ac[peaks])])
    if ac[lag]<.6 or db(rms(z)) < -65:continue
    shift=.5*(ac[lag-1]-ac[lag+1])/(ac[lag-1]-2*ac[lag]+ac[lag+1]);result[start]=FS/(lag+shift)
 return result
qrows=[];cpu=[]
for gender in ['female','male']:
 for kind in ['phrase','vowel-i-u']:
    na=f'quality-{gender}-{kind}-os1';nb=f'quality-{gender}-{kind}-os2';a=data[na];b=data[nb]
    for os in [1,2]:
        v=[t['core_percent'] for t in timing if t['name']==f'quality-{gender}-{kind}-os{os}']
        cpu.append(dict(case=f'{gender}-{kind}',os=os,median_core_percent=statistics.median(v),min_core_percent=min(v),max_core_percent=max(v),finite_audio=f'quality-{gender}-{kind}-os{os}' not in bad,anomalous_audio=os==1))
    if na in bad or nb in bad:
        qrows.append(dict(case=f'{gender}-{kind}',spectral_change_db=None,raw_rms_delta_db_os1_minus_os2=None,normalized_rms_delta_db_os1_minus_os2=None,centroid_delta_hz_os1_minus_os2=None,band_shape_delta_db_os1_minus_os2=None,pitch_median_cents_os1_minus_os2=None,pitch_abs_p95_cents=None,paired_pitch_frames=0,pitch_frames_os1=0,pitch_frames_os2=0,pitch_hz_median_os1=None,pitch_hz_median_os2=None))
        continue
    sl=slice(24000,168000 if kind=='phrase' else 192000);ca,ba,sa=features(a[sl]);cb,bb,sb=features(b[sl])
    pa=pitches(a);pb=pitches(b);common=pa.keys()&pb.keys();cents=np.array([1200*np.log2(pa[i]/pb[i]) for i in common])
    qrows.append(dict(case=f'{gender}-{kind}',spectral_change_db=score(sb,sa),raw_rms_delta_db_os1_minus_os2=db(rms(a)/rms(b)),normalized_rms_delta_db_os1_minus_os2=db(rms(a*gains[na])/rms(b*gains[nb])),centroid_delta_hz_os1_minus_os2=ca-cb,band_shape_delta_db_os1_minus_os2=(10*np.log10((ba/ba.sum())/(bb/bb.sum()))).tolist(),pitch_median_cents_os1_minus_os2=float(np.median(cents)),pitch_abs_p95_cents=float(np.percentile(abs(cents),95)),paired_pitch_frames=len(cents),pitch_frames_os1=len(pa),pitch_frames_os2=len(pb),pitch_hz_median_os1=float(np.median(list(pa.values()))),pitch_hz_median_os2=float(np.median(list(pb.values())))))
for r in qrows:
 for os in [1,2]:
    name=f"quality-{r['case']}-os{os}";x=data[name]
    if name in bad:
        r[f'dominant_peak_hz_os{os}']=None;r[f'power_fraction_above_8k_os{os}']=None;r[f'last_100ms_rms_dbfs_os{os}']=None
    else:
        n=8192;w=np.hanning(n);frames=np.lib.stride_tricks.sliding_window_view(x[24000:],n)[::4096];power=np.mean(abs(np.fft.rfft(frames*w,axis=1))**2,axis=0);power[1:-1]*=2;freq=np.fft.rfftfreq(n,1/FS)
        r[f'dominant_peak_hz_os{os}']=float(freq[np.argmax(power)]);r[f'power_fraction_above_8k_os{os}']=float(power[freq>8000].sum()/power.sum());r[f'last_100ms_rms_dbfs_os{os}']=db(rms(x[-4800:]))
def records(name,rs):
 (DEST/(name+'.json')).write_text(json.dumps(rs,indent=2)+'\n')
 with (DEST/(name+'.csv')).open('w') as f:
    w=csv.DictWriter(f,fieldnames=list(rs[0]));w.writeheader();w.writerows(rs)
for name,rs in [('statistics',stats),('metrics',rows),('quality-metrics',qrows),('cpu',cpu)]:records(name,rs)
for name in ['manifest','timings']:(DEST/(name+'.json')).write_bytes((RAW/(name+'.json')).read_bytes())
text=f'''# Chimera physical controls — round 3

108 mono 48 kHz PCM16 WAVs: 96 control files, 2 whispers, 2 default references, 8 oversampling comparisons. Upstream commit 092b16ce12b927d473c884ca2dfae53ce1b995df; no tracked upstream files modified. No musical-quality judgment.

## Reproduction

From /Users/robschoen/Dropbox/CC/Chimera:

    . "$HOME/.cargo/env"
    cargo run --release --locked --manifest-path spike/audition-tool/Cargo.toml --bin round3 -- spike/audition-tool/raw-round3
    python3 spike/audition-tool/render_round3.py

Requires Rust/Cargo and NumPy ({np.__version__} here). Existing tool crate and Cargo.lock reused. Added src/bin/round3.rs and render_round3.py; no existing source changed. Python reuses raw renders if timings.json exists; the explicit Cargo command always regenerates them. Originals: spike/audition-tool/raw-round3/*.f32. [render.log](render.log), [Rust source copy](round3.rs), [Python source copy](render_round3.py). [manifest.json](manifest.json) records every control range, duration and final articulator target/actual.

## Rendering and gain

Engine::new(48000), set_body(MALE/FEMALE), set_source(f0=147/294), set_artic(vowel_artic("ɑ",body),None), set_gate(1), then render in 48-frame (1 ms) blocks. All other defaults retained: pressure=8, breathiness=.15, tension=.5, brightness=.25, roughness=.1, fry=0, glide=.06, vibrato_depth=0, vibrato_rate=6, drift=5, voiced=true. Tip-free remains true. No gestures, pre-roll, snapping, limiter bypass or fades. Startup includes normal articulation settling and pitch glide. Gate held from t=0 through EOF; file cuts may click. Analyses exclude first .5 s.

6 s sweeps use triangular linear target ramps min→max→min updated each ms. Peak target occurs at 3 s to block precision; last block is within 1 ms of min. Static endpoint files last 2 s. Each render starts from a fresh deterministic engine. Whispers only change voiced=false; reference files retain default source at the specified pitch/posture.

**One common gain for all 100 control/reference files: {gain:.9f} ({db(gain):+.4f} dB).** Maximum raw peak {max(s['raw_peak'] for s in stats if not s['file'].startswith('quality-')):.9f}; target peak −1 dBFS. Relative levels preserved across controls AND both bodies. No individual normalization of these files. PCM16 quantization: round(sample × gain × 32767). No resampling, compression or fades. Every file's full-duration written WAV RMS dBFS, original levels, gain, numerical checks and SHA256 are in [statistics.csv](statistics.csv) / [statistics.json](statistics.json), and the file list below.

The eight quality A/B files are a separate requested exception: each member independently scaled to the same −1 dBFS peak within its matched os1/os2 pair. RMS is not matched. Original levels and gains retained.

## Ranked endpoint metrics

Signs are **max minus min**. RMS uses .5–2 s; centroid uses all 0–24 kHz power; band deltas use integrated absolute power in [0,1k), [1k,3k), [3k,8k) Hz. Welch: 4096-sample Hann, 2048 hop, one-sided power. Metrics use unquantized raw samples: a common scalar cancels these deltas.

Shape score = RMS dB change of one-third-octave band energy fractions from 100 Hz to 8 kHz (last bin truncated), each normalized by total 0–8 kHz energy and floored at a −60 dB fraction. A uniform gain change scores zero. Ranking uses mean male/female shape score, excluding level. Pressure is placed last and marked unrankable because its minimum is effectively silent in the analysis window; its numerical spectral score is retained only as a diagnostic. This definition measures spectral change, not audibility or musical utility. Endpoint metrics can miss intermediate-only effects. Default stochastic drift/roughness remain; small changes can include differently driven stochastic evolution.

|Rank|Control (range)|Body|Shape dB|Δ RMS dB|Δ centroid Hz|Δ 0–1k dB|Δ 1–3k dB|Δ 3–8k dB|
|---:|---|---|---:|---:|---:|---:|---:|---:|
'''
for r in rows:text+=f"|{str(rank.index(r['control'])+1) if r['spectral_comparison_valid'] else 'N/A'}|{r['control']} ({r['minimum']:g}…{r['maximum']:g})|{r['gender']}|{r['spectral_change_db']:.2f}|{r['rms_delta_db']:+.2f}|{r['centroid_delta_hz']:+.1f}|{r['band_0_1k_db']:+.2f}|{r['band_1_3k_db']:+.2f}|{r['band_3_8k_db']:+.2f}|\n"
text+='\n[metrics.csv](metrics.csv) / [metrics.json](metrics.json).\n\n## Anomalies and implementation caveats\n\n'
text+=f"{len(stats)-len(bad)} of {len(stats)} raw files are finite; two female os1 files fail as detailed below. Raw samples at or beyond ±1: {sum(s['raw_samples_ge_1'] for s in stats)}. No final PCM clipping. Maximum raw adjacent-sample difference {max(s['max_adjacent_jump'] for s in stats if s['max_adjacent_jump'] is not None):.6f}. Raw means engine output AFTER its internal limiter but BEFORE common export gain: these checks cannot measure internal pre-limiter peaks.\n\n"
for r in rows:
 if r['endpoint_max_abs_difference']==0:text+=f"- **{r['control']} / {r['gender']}: min and max are sample-identical**; no endpoint effect in this ɑ posture.\n"
for s in stats:
 if s['steady_min_100ms_rms_dbfs'] is not None and (s['steady_min_100ms_rms_dbfs'] < -80 or s['steady_max_100ms_rms_dbfs']-s['steady_min_100ms_rms_dbfs']>20):
    text+=f"- Level excursion flag {s['file']}: settled 100 ms raw RMS range {s['steady_min_100ms_rms_dbfs']:.1f}…{s['steady_max_100ms_rms_dbfs']:.1f} dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.\n"
text+='''
- All endpoint targets were accepted. ArticulatorBank clamps to ARTIC_SPECS; physiological smoothing, palate-contact constraints and geometry clamping remain active. Target/actual at EOF recorded in manifest.json.
- **Velum:** geometry clamps velum/1.2 to 0…1. Requested −.25…0 and 1.2…1.4 include saturated regions.
- **Lateral:** only enlarges a passage near the tip when area is below lateral × .35 × scale². An already-open ɑ need not change. Sample-identical behavior here does not imply this control is globally ignored.
- **TtCurl:** changes blade curvature; tip-free follows jaw/tongue-body posture. It does not independently command a retroflex tip position.
- **Ventricular:** changes epilarynx area via vent=.95×(1−.75×ventricular), not a separate false-fold oscillator. Its label does not establish a growl mechanism.
- **Pressure min=2:** both bodies decay to effective silence (below −240 dBFS in the settled interval; PCM becomes zero). The >249 dB RMS and huge band deltas compare signal with a numerical residual, so they are not useful timbre metrics. No meaningful spectral ranking is assigned to this control.\n- **Pressure:** glottal effort clamp((abs(pressure)−3)/10,0,1) saturates below3/above13, although lung pressure continues changing. Limiter active.
- **Brightness/tension:** combine into clamped closure sharpness. Brightness=0 at default tension reaches the lower closure clamp; other glottal parameters still vary.
- All 100 Engine controls/references and both male os modes contain finite output; pressure minima become effectively silent. Female os1 fails. Signal irregularity from roughness/fry is not automatically numerical instability. Sample checks cannot certify absence of audible internal clicks or assess musical quality.

## Oversampling A/B and CPU

HumanVoice::new(48000,body.body(),os), os=1/2, 48-frame blocks; direct voice needed to choose os. Defaults, limiter, gate/score behavior retained. Phrase reuses round2's five ɑ syllables, semitones [0,2,4,7,0], beats [1,1,1,1,2], 110 BPM, t0=.3; score vibrato targets removed. Female A3 B3 C♯4 E4 A3 (MIDI57), male G2 A2 B2 D3 G2 (MIDI43). Round2's 3.972729 s continues rendering to exactly 4 s, rather than appending zeros. Vowel comparison: calibrated i→u articulation interpolation over 4 s, male147/female294 Hz, gate always on, all other defaults.

Apple M4 Max arm64, Cargo release build. One excluded warmup per case/os, five timed fresh renders each, alternating os order. Timing includes render calls and sweep-control updates; excludes construction/planning/allocation/export/analysis. Percent of one core =100×elapsed wall seconds/4 audio seconds, following upstream bench.rs. Wall-time estimate, subject to host scheduling/load; no per-sample telemetry or analysis in timed region. [timings.json](timings.json) retains all measurements; [cpu.csv](cpu.csv).

|Case|os1 anomalous output, median % (min–max)|os2 median % (min–max)|os1/os2|
|---|---:|---:|---:|
'''
for gender in ['female','male']:
 for kind in ['phrase','vowel-i-u']:
    case=f'{gender}-{kind}';a,b=[next(r for r in cpu if r['case']==case and r['os']==os) for os in [1,2]]
    text+=f"|{case}|{a['median_core_percent']:.2f} ({a['min_core_percent']:.2f}–{a['max_core_percent']:.2f})|{b['median_core_percent']:.2f} ({b['min_core_percent']:.2f}–{b['max_core_percent']:.2f})|{a['median_core_percent']/b['median_core_percent']:.3f}|\n"
text+='''
Quality deltas below are **os1 minus os2**. Spectral windows .5–3.5 s phrase / .5–4 s vowel; same shape score as above. Band shape deltas normalize each spectrum to integrated 0–8 kHz energy. Pitch: 80 ms Hann frames, 20 ms hop, normalized autocorrelation, strongest local peak in 60–700 Hz and parabolic lag interpolation; confidence≥.6 and RMS>−65 dBFS. Paired frames valid in both renders. This is an estimator difference, not ground truth; octave errors and changed stochastic timing may contribute. Internal random/control updates change with oversampling, so the signals need not null. These metrics do not isolate aliasing or establish perceptual quality. The male os1 dominant spectra are around 3.35–3.37 kHz, and its phrase fails to decay after release. Its autocorrelation estimates are therefore labeled estimator output, NOT a reliable sung F0 shift.

|Case|Shape dB|Δ centroid Hz|Δ band fractions dB (0–1/1–3/3–8k)|Δ raw RMS dB|Δ matched RMS dB|Estimator median cents|Estimator abs P95 cents|Paired frames|
|---|---:|---:|---|---:|---:|---:|---:|---:|
'''
for r in qrows:
 if r['spectral_change_db'] is None:
    text+=f"|{r['case']}|N/A|N/A|N/A|N/A|N/A|N/A|N/A|0 (failed)|\n"
 else:
    text+=f"|{r['case']}|{r['spectral_change_db']:.2f}|{r['centroid_delta_hz_os1_minus_os2']:+.1f}|{' / '.join(f'{v:+.2f}' for v in r['band_shape_delta_db_os1_minus_os2'])}|{r['raw_rms_delta_db_os1_minus_os2']:+.2f}|{r['normalized_rms_delta_db_os1_minus_os2']:+.2f}|{r['pitch_median_cents_os1_minus_os2']:+.2f}|{r['pitch_abs_p95_cents']:.2f}|{r['paired_pitch_frames']}|\n"
text+='\nFemale os1 produces non-finite samples and invalidates that comparison. The two FAILED-nonfinite-silenced WAVs preserve finite output with invalid samples replaced by zero; they are diagnostic artifacts, not valid 4 s singing A/Bs. They are peak matched using only their finite portion. Raw .f32 originals retain NaNs. No spectral/pitch difference is computed for failed pairs. No final export clipping. Pitch-frame counts/median Hz: [quality-metrics.json](quality-metrics.json); [quality-metrics.csv](quality-metrics.csv).\n\n## Complete file list and RMS\n\nFull-duration written PCM RMS.\n\n|File|Duration s|RMS dBFS|\n|---|---:|---:|\n'
for s in stats:text+=f"|[{s['file']}]({s['file']})|{s['samples']/FS:.3f}|{s['wav_rms_dbfs']:.2f}|\n"
failure_text='\n## Failed oversampling renders\n\n'
for name,indices in bad.items():
    failure_text+=f'- **{name}**: {len(indices)} non-finite samples; first at {indices[0]/FS:.9f} s. Failed sample region replaced by zero in explicitly named diagnostic WAV; raw samples retained unchanged.\n'
for r in qrows:
 if r['case'].startswith('male'):
    failure_text+=f"- **{r['case']} os1 finite but anomalous**: dominant spectral peak {r['dominant_peak_hz_os1']:.1f} Hz vs {r['dominant_peak_hz_os2']:.1f} Hz os2; power above 8 kHz {100*r['power_fraction_above_8k_os1']:.2f}% vs {100*r['power_fraction_above_8k_os2']:.3f}%; last 100 ms RMS {r['last_100ms_rms_dbfs_os1']:.1f} vs {r['last_100ms_rms_dbfs_os2']:.1f} dBFS.\n"
failure_text+='\nFemale os1 CPU figures include processing after failure and are NOT stable-voice cost estimates. Male os1 timings also describe anomalous output, so neither body establishes usable os1 polyphony cost. No engine fixes were attempted. All measured repeat trials and failure timestamps are in timings.json.\n'
(DEST/'README.md').write_text(text+failure_text)
for src in [TOOL/'src/bin/round3.rs',TOOL/'render_round3.py',TOOL/'round3-render.log']:(DEST/({'round3-render.log':'render.log'}.get(src.name,src.name))).write_bytes(src.read_bytes())
print(json.dumps(dict(wavs=len(stats),common_gain=gain,rank=rank,cpu=cpu,quality=qrows),indent=2))
