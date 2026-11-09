# Chimera singing audition — round 2

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

- [A_female-a3-e4-default.wav](A_female-a3-e4-default.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"f0": 220.0}; larynx delta 0.0 cm.
- [B_female-a3-e4-breathiness-0p35.wav](B_female-a3-e4-breathiness-0p35.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"breathiness": 0.35, "f0": 220.0}; larynx delta 0.0 cm.
- [B_female-a3-e4-tension-0p70.wav](B_female-a3-e4-tension-0p70.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"f0": 220.0, "tension": 0.7}; larynx delta 0.0 cm.
- [B_female-a3-e4-tension-0p30.wav](B_female-a3-e4-tension-0p30.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"f0": 220.0, "tension": 0.3}; larynx delta 0.0 cm.
- [B_female-a3-e4-brightness-0p55.wav](B_female-a3-e4-brightness-0p55.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"brightness": 0.55, "f0": 220.0}; larynx delta 0.0 cm.
- [B_female-a3-e4-pressure-9.wav](B_female-a3-e4-pressure-9.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"f0": 220.0, "pressure": 9.0}; larynx delta 0.0 cm.
- [B_female-a3-e4-child-axis-0p30.wav](B_female-a3-e4-child-axis-0p30.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.3); source overrides {"f0": 220.0}; larynx delta 0.0 cm.
- [B_female-a3-e4-larynx-raised-0p5cm.wav](B_female-a3-e4-larynx-raised-0p5cm.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.0); source overrides {"f0": 220.0}; larynx delta 0.5 cm.
- [B_female-a3-e4-combined-best-guess.wav](B_female-a3-e4-combined-best-guess.wav) (3.972729 s): phrase base MIDI 57; BodyMix(female=1.0, child=0.3); source overrides {"breathiness": 0.35, "brightness": 0.55, "f0": 220.0, "pressure": 9.0, "tension": 0.55}; larynx delta 0.5 cm.
- [A_female-a4-e5-default.wav](A_female-a4-e5-default.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"f0": 440.0}; larynx delta 0.0 cm.
- [B_female-a4-e5-breathiness-0p35.wav](B_female-a4-e5-breathiness-0p35.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"breathiness": 0.35, "f0": 440.0}; larynx delta 0.0 cm.
- [B_female-a4-e5-tension-0p70.wav](B_female-a4-e5-tension-0p70.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"f0": 440.0, "tension": 0.7}; larynx delta 0.0 cm.
- [B_female-a4-e5-tension-0p30.wav](B_female-a4-e5-tension-0p30.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"f0": 440.0, "tension": 0.3}; larynx delta 0.0 cm.
- [B_female-a4-e5-brightness-0p55.wav](B_female-a4-e5-brightness-0p55.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"brightness": 0.55, "f0": 440.0}; larynx delta 0.0 cm.
- [B_female-a4-e5-pressure-9.wav](B_female-a4-e5-pressure-9.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"f0": 440.0, "pressure": 9.0}; larynx delta 0.0 cm.
- [B_female-a4-e5-child-axis-0p30.wav](B_female-a4-e5-child-axis-0p30.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.3); source overrides {"f0": 440.0}; larynx delta 0.0 cm.
- [B_female-a4-e5-larynx-raised-0p5cm.wav](B_female-a4-e5-larynx-raised-0p5cm.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.0); source overrides {"f0": 440.0}; larynx delta 0.5 cm.
- [B_female-a4-e5-combined-best-guess.wav](B_female-a4-e5-combined-best-guess.wav) (3.972729 s): phrase base MIDI 69; BodyMix(female=1.0, child=0.3); source overrides {"breathiness": 0.35, "brightness": 0.55, "f0": 440.0, "pressure": 9.0, "tension": 0.55}; larynx delta 0.5 cm.
- [C_male-baritone-g2-d3-default.wav](C_male-baritone-g2-d3-default.wav) (3.972729 s): phrase base MIDI 43; BodyMix(female=0.0, child=0.0); source overrides {"f0": 97.99885899543733}; larynx delta 0.0 cm.
- [C_male-tenor-c4-g4-default.wav](C_male-tenor-c4-g4-default.wav) (3.972729 s): phrase base MIDI 60; BodyMix(female=0.0, child=0.0); source overrides {"f0": 261.6255653005986}; larynx delta 0.0 cm.
- [C_male-tenor-c4-g4-belt-brightness-0p55-pressure-10.wav](C_male-tenor-c4-g4-belt-brightness-0p55-pressure-10.wav) (3.972729 s): phrase base MIDI 60; BodyMix(female=0.0, child=0.0); source overrides {"brightness": 0.55, "f0": 261.6255653005986, "pressure": 10.0}; larynx delta 0.0 cm.
- [D_male-147hz-depth-0c-rate-5p5hz.wav](D_male-147hz-depth-0c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-15c-rate-5p5hz.wav](D_male-147hz-depth-15c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoDepth": 15.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-30c-rate-5p5hz.wav](D_male-147hz-depth-30c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoDepth": 30.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-50c-rate-5p5hz.wav](D_male-147hz-depth-50c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoDepth": 50.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-80c-rate-5p5hz.wav](D_male-147hz-depth-80c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoDepth": 80.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-30c-rate-4p5hz.wav](D_male-147hz-depth-30c-rate-4p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoDepth": 30.0, "vibratoRate": 4.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-30c-rate-6p5hz.wav](D_male-147hz-depth-30c-rate-6p5hz.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoDepth": 30.0, "vibratoRate": 6.5}; larynx delta 0.0 cm.
- [D_male-147hz-depth-30c-rate-5p5hz-delayed-600ms.wav](D_male-147hz-depth-30c-rate-5p5hz-delayed-600ms.wav) (3.500000 s): sustain; BodyMix(female=0.0, child=0.0); source overrides {"f0": 147.0, "vibratoRate": 5.5}; larynx delta 0.0 cm; set depth=30 cents at sample 33600.
- [D_female-294hz-depth-0c-rate-5p5hz.wav](D_female-294hz-depth-0c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-15c-rate-5p5hz.wav](D_female-294hz-depth-15c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoDepth": 15.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-30c-rate-5p5hz.wav](D_female-294hz-depth-30c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoDepth": 30.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-50c-rate-5p5hz.wav](D_female-294hz-depth-50c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoDepth": 50.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-80c-rate-5p5hz.wav](D_female-294hz-depth-80c-rate-5p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoDepth": 80.0, "vibratoRate": 5.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-30c-rate-4p5hz.wav](D_female-294hz-depth-30c-rate-4p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoDepth": 30.0, "vibratoRate": 4.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-30c-rate-6p5hz.wav](D_female-294hz-depth-30c-rate-6p5hz.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoDepth": 30.0, "vibratoRate": 6.5}; larynx delta 0.0 cm.
- [D_female-294hz-depth-30c-rate-5p5hz-delayed-600ms.wav](D_female-294hz-depth-30c-rate-5p5hz-delayed-600ms.wav) (3.500000 s): sustain; BodyMix(female=1.0, child=0.0); source overrides {"f0": 294.0, "vibratoRate": 5.5}; larynx delta 0.0 cm; set depth=30 cents at sample 33600.

## Sanity checks

37 WAVs re-opened and checked: correct mono/48 kHz/PCM16 format; no silent files, nonfinite source samples, final clipped samples, or nonzero file-boundary samples. Peak range -1.000152 to -1.000152 dBFS. Maximum absolute DC mean 0.00005504; maximum adjacent-sample jump 0.411407. Raw peak maximum 0.750069; 0 raw samples at/above ±1.

No adjacent-sample jumps above 0.5 full scale.

No unsupported requested controls or render failures. Sample checks do not certify musical quality, perceived gender, or absence of audible internal clicks.

Additional pitch spot-check (normalized autocorrelation with parabolic lag interpolation, 0.430–0.530 s window, search restricted to ±15% of the intended first-note Hz): female defaults 220.14 / 440.41 Hz; baritone 97.97 Hz; tenor 261.72 Hz; depth-zero holds male 146.68 / female 294.05 Hz. These confirm registers, not every note’s intonation or every vibrato trajectory. All 37 final WAV hashes are distinct.
