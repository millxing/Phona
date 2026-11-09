# Chimera physical controls — round 3

108 mono 48 kHz PCM16 WAVs: 96 control files, 2 whispers, 2 default references, 8 oversampling comparisons. Upstream commit 092b16ce12b927d473c884ca2dfae53ce1b995df; no tracked upstream files modified. No musical-quality judgment.

## Reproduction

From /Users/robschoen/Dropbox/CC/Chimera:

    . "$HOME/.cargo/env"
    cargo run --release --locked --manifest-path spike/audition-tool/Cargo.toml --bin round3 -- spike/audition-tool/raw-round3
    python3 spike/audition-tool/render_round3.py

Requires Rust/Cargo and NumPy (2.4.1 here). Existing tool crate and Cargo.lock reused. Added src/bin/round3.rs and render_round3.py; no existing source changed. Python reuses raw renders if timings.json exists; the explicit Cargo command always regenerates them. Originals: spike/audition-tool/raw-round3/*.f32. [render.log](render.log), [Rust source copy](round3.rs), [Python source copy](render_round3.py). [manifest.json](manifest.json) records every control range, duration and final articulator target/actual.

## Rendering and gain

Engine::new(48000), set_body(MALE/FEMALE), set_source(f0=147/294), set_artic(vowel_artic("ɑ",body),None), set_gate(1), then render in 48-frame (1 ms) blocks. All other defaults retained: pressure=8, breathiness=.15, tension=.5, brightness=.25, roughness=.1, fry=0, glide=.06, vibrato_depth=0, vibrato_rate=6, drift=5, voiced=true. Tip-free remains true. No gestures, pre-roll, snapping, limiter bypass or fades. Startup includes normal articulation settling and pitch glide. Gate held from t=0 through EOF; file cuts may click. Analyses exclude first .5 s.

6 s sweeps use triangular linear target ramps min→max→min updated each ms. Peak target occurs at 3 s to block precision; last block is within 1 ms of min. Static endpoint files last 2 s. Each render starts from a fresh deterministic engine. Whispers only change voiced=false; reference files retain default source at the specified pitch/posture.

**One common gain for all 100 control/reference files: 1.188212202 (+1.4979 dB).** Maximum raw peak 0.750077248; target peak −1 dBFS. Relative levels preserved across controls AND both bodies. No individual normalization of these files. PCM16 quantization: round(sample × gain × 32767). No resampling, compression or fades. Every file's full-duration written WAV RMS dBFS, original levels, gain, numerical checks and SHA256 are in [statistics.csv](statistics.csv) / [statistics.json](statistics.json), and the file list below.

The eight quality A/B files are a separate requested exception: each member independently scaled to the same −1 dBFS peak within its matched os1/os2 pair. RMS is not matched. Original levels and gains retained.

## Ranked endpoint metrics

Signs are **max minus min**. RMS uses .5–2 s; centroid uses all 0–24 kHz power; band deltas use integrated absolute power in [0,1k), [1k,3k), [3k,8k) Hz. Welch: 4096-sample Hann, 2048 hop, one-sided power. Metrics use unquantized raw samples: a common scalar cancels these deltas.

Shape score = RMS dB change of one-third-octave band energy fractions from 100 Hz to 8 kHz (last bin truncated), each normalized by total 0–8 kHz energy and floored at a −60 dB fraction. A uniform gain change scores zero. Ranking uses mean male/female shape score, excluding level. Pressure is placed last and marked unrankable because its minimum is effectively silent in the analysis window; its numerical spectral score is retained only as a diagnostic. This definition measures spectral change, not audibility or musical utility. Endpoint metrics can miss intermediate-only effects. Default stochastic drift/roughness remain; small changes can include differently driven stochastic evolution.

|Rank|Control (range)|Body|Shape dB|Δ RMS dB|Δ centroid Hz|Δ 0–1k dB|Δ 1–3k dB|Δ 3–8k dB|
|---:|---|---|---:|---:|---:|---:|---:|---:|
|1|fry (0…1)|male|11.51|-8.42|-120.8|-8.38|-10.16|-3.69|
|1|fry (0…1)|female|25.22|-20.93|-383.1|-20.91|-22.25|-16.54|
|2|velum (-0.25…1.4)|male|7.01|-4.74|-146.7|-4.94|-2.01|-8.65|
|2|velum (-0.25…1.4)|female|19.16|-42.37|-509.9|-42.40|-41.84|-64.32|
|3|tension (0…1)|male|11.88|+9.55|+469.1|+8.86|+19.72|+15.09|
|3|tension (0…1)|female|12.72|+1.61|+267.7|+1.04|+6.01|+24.52|
|4|roughness (0…1)|male|8.79|-0.72|+10.0|-0.73|-0.52|-0.13|
|4|roughness (0…1)|female|13.19|-1.78|+6.7|-1.83|-0.18|-1.51|
|5|brightness (0…1)|male|9.40|+3.81|+234.0|+3.23|+10.09|+14.90|
|5|brightness (0…1)|female|9.06|-0.08|+189.6|-0.56|+4.30|+17.94|
|6|breathiness (0…1)|male|8.48|-12.18|+36.3|-12.30|-12.90|+3.88|
|6|breathiness (0…1)|female|7.43|-13.57|+23.1|-14.61|-6.60|-6.72|
|7|larynx (-1.6…1.6)|male|7.12|-1.57|-12.0|-1.43|-4.46|-1.98|
|7|larynx (-1.6…1.6)|female|6.18|+2.70|+34.1|+3.11|-1.07|+1.24|
|8|lipWidth (0…1)|male|3.06|+1.14|+96.5|+1.11|+1.87|+0.78|
|8|lipWidth (0…1)|female|4.78|+5.06|+81.8|+5.53|+0.40|+3.81|
|9|lipProt (-0.2…1.5)|male|2.27|-0.65|-40.2|-0.71|+0.26|+1.08|
|9|lipProt (-0.2…1.5)|female|4.82|-3.91|-35.4|-4.34|+0.44|-1.73|
|10|epilarynx (0…1)|male|2.66|-0.19|+6.7|-0.21|+0.15|-2.78|
|10|epilarynx (0…1)|female|2.62|+0.30|+6.3|+0.30|+0.31|-4.32|
|11|root (-1…1)|male|2.06|+0.13|-11.6|+0.15|-0.26|-0.08|
|11|root (-1…1)|female|1.99|-1.51|-15.0|-1.56|-0.62|+0.04|
|12|ventricular (0…1)|male|1.90|-0.27|+20.2|-0.30|+0.39|-2.06|
|12|ventricular (0…1)|female|1.51|-0.04|+7.8|-0.04|-0.05|-0.95|
|13|dorsum (0…1)|male|1.48|-0.07|+10.7|-0.14|+1.09|-0.53|
|13|dorsum (0…1)|female|1.49|+0.39|+9.5|+0.34|+1.19|-0.88|
|14|ttCurl (0…1)|male|0.83|+0.07|-3.4|+0.08|-0.21|-0.25|
|14|ttCurl (0…1)|female|0.65|-0.50|-6.3|-0.48|-0.70|-0.18|
|15|lateral (0…1)|male|0.00|+0.00|+0.0|+0.00|+0.00|+0.00|
|15|lateral (0…1)|female|0.00|+0.00|+0.0|+0.00|+0.00|+0.00|
|N/A|pressure (2…16)|male|21.11|+249.25|+607.6|+256.60|+285.92|+282.21|
|N/A|pressure (2…16)|female|26.51|+257.05|+880.3|+263.54|+282.14|+291.94|

[metrics.csv](metrics.csv) / [metrics.json](metrics.json).

## Anomalies and implementation caveats

106 of 108 raw files are finite; two female os1 files fail as detailed below. Raw samples at or beyond ±1: 0. No final PCM clipping. Maximum raw adjacent-sample difference 0.755364. Raw means engine output AFTER its internal limiter but BEFORE common export gain: these checks cannot measure internal pre-limiter peaks.

- **lateral / male: min and max are sample-identical**; no endpoint effect in this ɑ posture.
- **lateral / female: min and max are sample-identical**; no endpoint effect in this ɑ posture.
- Level excursion flag female-pressure-min.wav: settled 100 ms raw RMS range -300.0…-254.2 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.
- Level excursion flag female-pressure-sweep.wav: settled 100 ms raw RMS range -46.4…-8.7 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.
- Level excursion flag female-velum-sweep.wav: settled 100 ms raw RMS range -51.5…-9.0 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.
- Level excursion flag male-pressure-min.wav: settled 100 ms raw RMS range -300.0…-247.3 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.
- Level excursion flag male-pressure-sweep.wav: settled 100 ms raw RMS range -52.9…-9.7 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.
- Level excursion flag quality-female-phrase-os2.wav: settled 100 ms raw RMS range -209.1…-9.8 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.
- Level excursion flag quality-male-phrase-os2.wav: settled 100 ms raw RMS range -184.6…-13.0 dBFS. Phrase rests/releases are intentional; level changes alone do not establish numerical instability.

- All endpoint targets were accepted. ArticulatorBank clamps to ARTIC_SPECS; physiological smoothing, palate-contact constraints and geometry clamping remain active. Target/actual at EOF recorded in manifest.json.
- **Velum:** geometry clamps velum/1.2 to 0…1. Requested −.25…0 and 1.2…1.4 include saturated regions.
- **Lateral:** only enlarges a passage near the tip when area is below lateral × .35 × scale². An already-open ɑ need not change. Sample-identical behavior here does not imply this control is globally ignored.
- **TtCurl:** changes blade curvature; tip-free follows jaw/tongue-body posture. It does not independently command a retroflex tip position.
- **Ventricular:** changes epilarynx area via vent=.95×(1−.75×ventricular), not a separate false-fold oscillator. Its label does not establish a growl mechanism.
- **Pressure min=2:** both bodies decay to effective silence (below −240 dBFS in the settled interval; PCM becomes zero). The >249 dB RMS and huge band deltas compare signal with a numerical residual, so they are not useful timbre metrics. No meaningful spectral ranking is assigned to this control.
- **Pressure:** glottal effort clamp((abs(pressure)−3)/10,0,1) saturates below3/above13, although lung pressure continues changing. Limiter active.
- **Brightness/tension:** combine into clamped closure sharpness. Brightness=0 at default tension reaches the lower closure clamp; other glottal parameters still vary.
- All 100 Engine controls/references and both male os modes contain finite output; pressure minima become effectively silent. Female os1 fails. Signal irregularity from roughness/fry is not automatically numerical instability. Sample checks cannot certify absence of audible internal clicks or assess musical quality.

## Oversampling A/B and CPU

HumanVoice::new(48000,body.body(),os), os=1/2, 48-frame blocks; direct voice needed to choose os. Defaults, limiter, gate/score behavior retained. Phrase reuses round2's five ɑ syllables, semitones [0,2,4,7,0], beats [1,1,1,1,2], 110 BPM, t0=.3; score vibrato targets removed. Female A3 B3 C♯4 E4 A3 (MIDI57), male G2 A2 B2 D3 G2 (MIDI43). Round2's 3.972729 s continues rendering to exactly 4 s, rather than appending zeros. Vowel comparison: calibrated i→u articulation interpolation over 4 s, male147/female294 Hz, gate always on, all other defaults.

Apple M4 Max arm64, Cargo release build. One excluded warmup per case/os, five timed fresh renders each, alternating os order. Timing includes render calls and sweep-control updates; excludes construction/planning/allocation/export/analysis. Percent of one core =100×elapsed wall seconds/4 audio seconds, following upstream bench.rs. Wall-time estimate, subject to host scheduling/load; no per-sample telemetry or analysis in timed region. [timings.json](timings.json) retains all measurements; [cpu.csv](cpu.csv).

|Case|os1 anomalous output, median % (min–max)|os2 median % (min–max)|os1/os2|
|---|---:|---:|---:|
|female-phrase|5.30 (5.28–5.34)|12.61 (12.54–12.78)|0.420|
|female-vowel-i-u|5.62 (5.56–5.71)|12.93 (12.87–12.98)|0.434|
|male-phrase|5.75 (5.72–5.78)|13.45 (13.41–13.55)|0.427|
|male-vowel-i-u|6.02 (6.01–6.13)|13.73 (13.69–13.77)|0.439|

Quality deltas below are **os1 minus os2**. Spectral windows .5–3.5 s phrase / .5–4 s vowel; same shape score as above. Band shape deltas normalize each spectrum to integrated 0–8 kHz energy. Pitch: 80 ms Hann frames, 20 ms hop, normalized autocorrelation, strongest local peak in 60–700 Hz and parabolic lag interpolation; confidence≥.6 and RMS>−65 dBFS. Paired frames valid in both renders. This is an estimator difference, not ground truth; octave errors and changed stochastic timing may contribute. Internal random/control updates change with oversampling, so the signals need not null. These metrics do not isolate aliasing or establish perceptual quality. The male os1 dominant spectra are around 3.35–3.37 kHz, and its phrase fails to decay after release. Its autocorrelation estimates are therefore labeled estimator output, NOT a reliable sung F0 shift.

|Case|Shape dB|Δ centroid Hz|Δ band fractions dB (0–1/1–3/3–8k)|Δ raw RMS dB|Δ matched RMS dB|Estimator median cents|Estimator abs P95 cents|Paired frames|
|---|---:|---:|---|---:|---:|---:|---:|---:|
|female-phrase|N/A|N/A|N/A|N/A|N/A|N/A|N/A|0 (failed)|
|female-vowel-i-u|N/A|N/A|N/A|N/A|N/A|N/A|N/A|0 (failed)|
|male-phrase|38.84|+5770.3|-68.54 / -21.55 / +27.51|+9.16|+6.30|+2825.59|3015.78|166|
|male-vowel-i-u|32.06|+5082.4|-65.07 / -14.38 / +28.37|+10.54|+2.33|+2635.65|2653.39|196|

Female os1 produces non-finite samples and invalidates that comparison. The two FAILED-nonfinite-silenced WAVs preserve finite output with invalid samples replaced by zero; they are diagnostic artifacts, not valid 4 s singing A/Bs. They are peak matched using only their finite portion. Raw .f32 originals retain NaNs. No spectral/pitch difference is computed for failed pairs. No final export clipping. Pitch-frame counts/median Hz: [quality-metrics.json](quality-metrics.json); [quality-metrics.csv](quality-metrics.csv).

## Complete file list and RMS

Full-duration written PCM RMS.

|File|Duration s|RMS dBFS|
|---|---:|---:|
|[female-baseline-static.wav](female-baseline-static.wav)|2.000|-7.85|
|[female-breathiness-max.wav](female-breathiness-max.wav)|2.000|-20.77|
|[female-breathiness-min.wav](female-breathiness-min.wav)|2.000|-7.27|
|[female-breathiness-sweep.wav](female-breathiness-sweep.wav)|6.000|-11.23|
|[female-brightness-max.wav](female-brightness-max.wav)|2.000|-8.83|
|[female-brightness-min.wav](female-brightness-min.wav)|2.000|-8.85|
|[female-brightness-sweep.wav](female-brightness-sweep.wav)|6.000|-7.46|
|[female-dorsum-max.wav](female-dorsum-max.wav)|2.000|-7.49|
|[female-dorsum-min.wav](female-dorsum-min.wav)|2.000|-7.85|
|[female-dorsum-sweep.wav](female-dorsum-sweep.wav)|6.000|-7.31|
|[female-epilarynx-max.wav](female-epilarynx-max.wav)|2.000|-7.60|
|[female-epilarynx-min.wav](female-epilarynx-min.wav)|2.000|-7.85|
|[female-epilarynx-sweep.wav](female-epilarynx-sweep.wav)|6.000|-7.45|
|[female-fry-max.wav](female-fry-max.wav)|2.000|-28.59|
|[female-fry-min.wav](female-fry-min.wav)|2.000|-7.85|
|[female-fry-sweep.wav](female-fry-sweep.wav)|6.000|-12.89|
|[female-larynx-max.wav](female-larynx-max.wav)|2.000|-7.84|
|[female-larynx-min.wav](female-larynx-min.wav)|2.000|-10.23|
|[female-larynx-sweep.wav](female-larynx-sweep.wav)|6.000|-8.26|
|[female-lateral-max.wav](female-lateral-max.wav)|2.000|-7.85|
|[female-lateral-min.wav](female-lateral-min.wav)|2.000|-7.85|
|[female-lateral-sweep.wav](female-lateral-sweep.wav)|6.000|-7.65|
|[female-lipProt-max.wav](female-lipProt-max.wav)|2.000|-11.34|
|[female-lipProt-min.wav](female-lipProt-min.wav)|2.000|-7.73|
|[female-lipProt-sweep.wav](female-lipProt-sweep.wav)|6.000|-9.25|
|[female-lipWidth-max.wav](female-lipWidth-max.wav)|2.000|-7.85|
|[female-lipWidth-min.wav](female-lipWidth-min.wav)|2.000|-12.67|
|[female-lipWidth-sweep.wav](female-lipWidth-sweep.wav)|6.000|-9.41|
|[female-pressure-max.wav](female-pressure-max.wav)|2.000|-7.66|
|[female-pressure-min.wav](female-pressure-min.wav)|2.000|-70.71|
|[female-pressure-sweep.wav](female-pressure-sweep.wav)|6.000|-8.76|
|[female-root-max.wav](female-root-max.wav)|2.000|-9.08|
|[female-root-min.wav](female-root-min.wav)|2.000|-7.73|
|[female-root-sweep.wav](female-root-sweep.wav)|6.000|-8.15|
|[female-roughness-max.wav](female-roughness-max.wav)|2.000|-9.57|
|[female-roughness-min.wav](female-roughness-min.wav)|2.000|-7.82|
|[female-roughness-sweep.wav](female-roughness-sweep.wav)|6.000|-8.22|
|[female-tension-max.wav](female-tension-max.wav)|2.000|-9.46|
|[female-tension-min.wav](female-tension-min.wav)|2.000|-11.34|
|[female-tension-sweep.wav](female-tension-sweep.wav)|6.000|-8.25|
|[female-ttCurl-max.wav](female-ttCurl-max.wav)|2.000|-8.29|
|[female-ttCurl-min.wav](female-ttCurl-min.wav)|2.000|-7.85|
|[female-ttCurl-sweep.wav](female-ttCurl-sweep.wav)|6.000|-7.90|
|[female-velum-max.wav](female-velum-max.wav)|2.000|-42.54|
|[female-velum-min.wav](female-velum-min.wav)|2.000|-7.85|
|[female-velum-sweep.wav](female-velum-sweep.wav)|6.000|-10.09|
|[female-ventricular-max.wav](female-ventricular-max.wav)|2.000|-7.91|
|[female-ventricular-min.wav](female-ventricular-min.wav)|2.000|-7.85|
|[female-ventricular-sweep.wav](female-ventricular-sweep.wav)|6.000|-7.55|
|[female-whisper-static.wav](female-whisper-static.wav)|2.000|-44.30|
|[male-baseline-static.wav](male-baseline-static.wav)|2.000|-11.85|
|[male-breathiness-max.wav](male-breathiness-max.wav)|2.000|-22.51|
|[male-breathiness-min.wav](male-breathiness-min.wav)|2.000|-10.52|
|[male-breathiness-sweep.wav](male-breathiness-sweep.wav)|6.000|-14.87|
|[male-brightness-max.wav](male-brightness-max.wav)|2.000|-9.93|
|[male-brightness-min.wav](male-brightness-min.wav)|2.000|-13.72|
|[male-brightness-sweep.wav](male-brightness-sweep.wav)|6.000|-10.35|
|[male-dorsum-max.wav](male-dorsum-max.wav)|2.000|-11.93|
|[male-dorsum-min.wav](male-dorsum-min.wav)|2.000|-11.85|
|[male-dorsum-sweep.wav](male-dorsum-sweep.wav)|6.000|-11.74|
|[male-epilarynx-max.wav](male-epilarynx-max.wav)|2.000|-12.04|
|[male-epilarynx-min.wav](male-epilarynx-min.wav)|2.000|-11.85|
|[male-epilarynx-sweep.wav](male-epilarynx-sweep.wav)|6.000|-11.78|
|[male-fry-max.wav](male-fry-max.wav)|2.000|-20.13|
|[male-fry-min.wav](male-fry-min.wav)|2.000|-11.92|
|[male-fry-sweep.wav](male-fry-sweep.wav)|6.000|-15.23|
|[male-larynx-max.wav](male-larynx-max.wav)|2.000|-13.63|
|[male-larynx-min.wav](male-larynx-min.wav)|2.000|-11.97|
|[male-larynx-sweep.wav](male-larynx-sweep.wav)|6.000|-11.91|
|[male-lateral-max.wav](male-lateral-max.wav)|2.000|-11.85|
|[male-lateral-min.wav](male-lateral-min.wav)|2.000|-11.85|
|[male-lateral-sweep.wav](male-lateral-sweep.wav)|6.000|-11.69|
|[male-lipProt-max.wav](male-lipProt-max.wav)|2.000|-12.54|
|[male-lipProt-min.wav](male-lipProt-min.wav)|2.000|-12.03|
|[male-lipProt-sweep.wav](male-lipProt-sweep.wav)|6.000|-11.80|
|[male-lipWidth-max.wav](male-lipWidth-max.wav)|2.000|-11.85|
|[male-lipWidth-min.wav](male-lipWidth-min.wav)|2.000|-12.91|
|[male-lipWidth-sweep.wav](male-lipWidth-sweep.wav)|6.000|-11.95|
|[male-pressure-max.wav](male-pressure-max.wav)|2.000|-8.64|
|[male-pressure-min.wav](male-pressure-min.wav)|2.000|-58.70|
|[male-pressure-sweep.wav](male-pressure-sweep.wav)|6.000|-10.68|
|[male-root-max.wav](male-root-max.wav)|2.000|-11.75|
|[male-root-min.wav](male-root-min.wav)|2.000|-11.93|
|[male-root-sweep.wav](male-root-sweep.wav)|6.000|-11.66|
|[male-roughness-max.wav](male-roughness-max.wav)|2.000|-12.76|
|[male-roughness-min.wav](male-roughness-min.wav)|2.000|-11.93|
|[male-roughness-sweep.wav](male-roughness-sweep.wav)|6.000|-11.94|
|[male-tension-max.wav](male-tension-max.wav)|2.000|-9.68|
|[male-tension-min.wav](male-tension-min.wav)|2.000|-18.99|
|[male-tension-sweep.wav](male-tension-sweep.wav)|6.000|-11.47|
|[male-ttCurl-max.wav](male-ttCurl-max.wav)|2.000|-11.77|
|[male-ttCurl-min.wav](male-ttCurl-min.wav)|2.000|-11.85|
|[male-ttCurl-sweep.wav](male-ttCurl-sweep.wav)|6.000|-11.65|
|[male-velum-max.wav](male-velum-max.wav)|2.000|-16.43|
|[male-velum-min.wav](male-velum-min.wav)|2.000|-11.85|
|[male-velum-sweep.wav](male-velum-sweep.wav)|6.000|-14.27|
|[male-ventricular-max.wav](male-ventricular-max.wav)|2.000|-12.12|
|[male-ventricular-min.wav](male-ventricular-min.wav)|2.000|-11.85|
|[male-ventricular-sweep.wav](male-ventricular-sweep.wav)|6.000|-11.74|
|[male-whisper-static.wav](male-whisper-static.wav)|2.000|-35.83|
|[quality-female-phrase-os1-FAILED-nonfinite-silenced.wav](quality-female-phrase-os1-FAILED-nonfinite-silenced.wav)|4.000|-24.84|
|[quality-female-phrase-os2.wav](quality-female-phrase-os2.wav)|4.000|-11.97|
|[quality-female-vowel-i-u-os1-FAILED-nonfinite-silenced.wav](quality-female-vowel-i-u-os1-FAILED-nonfinite-silenced.wav)|4.000|-24.84|
|[quality-female-vowel-i-u-os2.wav](quality-female-vowel-i-u-os2.wav)|4.000|-9.85|
|[quality-male-phrase-os1.wav](quality-male-phrase-os1.wav)|4.000|-8.24|
|[quality-male-phrase-os2.wav](quality-male-phrase-os2.wav)|4.000|-14.54|
|[quality-male-vowel-i-u-os1.wav](quality-male-vowel-i-u-os1.wav)|4.000|-6.94|
|[quality-male-vowel-i-u-os2.wav](quality-male-vowel-i-u-os2.wav)|4.000|-9.27|

## Failed oversampling renders

- **quality-female-phrase-os1**: 179819 non-finite samples; first at 0.253770833 s. Failed sample region replaced by zero in explicitly named diagnostic WAV; raw samples retained unchanged.
- **quality-female-vowel-i-u-os1**: 190894 non-finite samples; first at 0.023041667 s. Failed sample region replaced by zero in explicitly named diagnostic WAV; raw samples retained unchanged.
- **male-phrase os1 finite but anomalous**: dominant spectral peak 3345.7 Hz vs 732.4 Hz os2; power above 8 kHz 27.59% vs 0.113%; last 100 ms RMS -8.5 vs -184.6 dBFS.
- **male-vowel-i-u os1 finite but anomalous**: dominant spectral peak 3369.1 Hz vs 293.0 Hz os2; power above 8 kHz 9.82% vs 0.045%; last 100 ms RMS -8.9 vs -20.3 dBFS.

Female os1 CPU figures include processing after failure and are NOT stable-voice cost estimates. Male os1 timings also describe anomalous output, so neither body establishes usable os1 polyphony cost. No engine fixes were attempted. All measured repeat trials and failure timestamps are in timings.json.
