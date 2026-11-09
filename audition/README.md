# Chimera audition WAVs

Engine: jamesondh/chimera-vocal-synth commit 092b16ce12b927d473c884ca2dfae53ce1b995df. All files are mono 48 kHz, 16-bit PCM WAV, 1–4 seconds, independently peak-normalized to approximately −1 dBFS. Independent normalization removes original loudness relationships; it is not loudness matching. No musical-quality judgment was made.

The requested 'a' is the engine's calibrated IPA **ɑ**. The male vowel holds and body comparisons use the same 110 Hz source, with drift and roughness disabled; body-specific calibrated articulation is used. Other CLI paths retain their built-in pitch/prosody/source settings. The ɑ→i→u sweep interpolates all calibrated articulation controls, approximating a tract CV sweep rather than changing just tongue position. Pitch sweep rises from 110 to 220 Hz.

## Reproduce

From /Users/robschoen/Dropbox/CC/Chimera, run:

    . "$HOME/.cargo/env"
    python3 spike/audition-tool/render_audition.py

Rust helper source: ../spike/audition-tool/src/main.rs; standalone crate depends on ../../upstream/crates/chimera. Its Cargo.lock pins dependency versions. Helper outputs and original CLI float WAVs are retained in ../spike/audition-tool/raw/; commands, render diagnostics, and build output are in ../spike/audition-tool/render.log. No tracked upstream files were modified. Requires Rust/Cargo and Python 3 standard library; no Python packages.

CLI commands below run from upstream/. Every CLI render uses --float so sample checks and normalization occur before PCM clipping/quantization. render_audition.py checks non-finite values, applies a 5 ms boundary fade (20 ms for creature excerpts), pads short files with trailing zeros to 1.2 s, normalizes to 10^(−1/20), then rounds to 16-bit PCM. The creature CLI renders all calls; audition files retain only the first 4 s. Beat hits are the complete CLI 0.5 s output plus zero padding. No internal consonant/percussion transients were removed.

## Files and recipes

- [01a_vowel-a-male-110hz.wav](01a_vowel-a-male-110hz.wav) (1.800 s): `src/main.rs::human`: male calibrated vowel at 110 Hz, drift/roughness off; gate 0.10–1.55 s.
- [01b_vowel-i-male-110hz.wav](01b_vowel-i-male-110hz.wav) (1.800 s): `src/main.rs::human`: male calibrated vowel at 110 Hz, drift/roughness off; gate 0.10–1.55 s.
- [01c_vowel-u-male-110hz.wav](01c_vowel-u-male-110hz.wav) (1.800 s): `src/main.rs::human`: male calibrated vowel at 110 Hz, drift/roughness off; gate 0.10–1.55 s.
- [01d_vowel-e-male-110hz.wav](01d_vowel-e-male-110hz.wav) (1.800 s): `src/main.rs::human`: male calibrated vowel at 110 Hz, drift/roughness off; gate 0.10–1.55 s.
- [01e_vowel-o-male-110hz.wav](01e_vowel-o-male-110hz.wav) (1.800 s): `src/main.rs::human`: male calibrated vowel at 110 Hz, drift/roughness off; gate 0.10–1.55 s.
- [02a_vowel-glide-a-i-u-110hz.wav](02a_vowel-glide-a-i-u-110hz.wav) (3.600 s): `src/main.rs::human`: linear calibrated articulation blend ɑ→i→u over 2.7 s, updated every 128 samples; 110 Hz fixed or exponential 110→220 Hz target.
- [02b_vowel-and-pitch-glide-110-to-220hz.wav](02b_vowel-and-pitch-glide-110-to-220hz.wav) (3.600 s): `src/main.rs::human`: linear calibrated articulation blend ɑ→i→u over 2.7 s, updated every 128 samples; 110 Hz fixed or exponential 110→220 Hz target.
- [03_ba-male.wav](03_ba-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_ba-male 'bɑ' male --float`
- [03_fa-male.wav](03_fa-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_fa-male 'fɑ' male --float`
- [03_ka-male.wav](03_ka-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_ka-male 'kɑ' male --float`
- [03_ma-male.wav](03_ma-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_ma-male 'mɑ' male --float`
- [03_na-male.wav](03_na-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_na-male 'nɑ' male --float`
- [03_pa-male.wav](03_pa-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_pa-male 'pɑ' male --float`
- [03_sa-male.wav](03_sa-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_sa-male 'sɑ' male --float`
- [03_sha-male.wav](03_sha-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_sha-male 'ʃɑ' male --float`
- [03_ta-male.wav](03_ta-male.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render speech /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/03_ta-male 'tɑ' male --float`
- [04_text-hello-world-male.wav](04_text-hello-world-male.wav) (1.205 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render text /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/04_text-hello-world-male 'Hello world.' male --float`
- [05_sung-la-la-la-g3-a3-c4.wav](05_sung-la-la-la-g3-a3-c4.wav) (2.700 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render notes /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/notes.json /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/05_sung-la-la-la-g3-a3-c4 male --float`; notes.json: G3/A3/C4, 1/1/2 beats, 120 BPM, 25-cent vibrato.
- [06a01_beatbox-kick.wav](06a01_beatbox-kick.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `B.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a02_beatbox-hi-hat.wav](06a02_beatbox-hi-hat.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `t.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a03_beatbox-open-hat.wav](06a03_beatbox-open-hat.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `tss.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a04_beatbox-snare.wav](06a04_beatbox-snare.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `Pf.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a05_beatbox-rimshot.wav](06a05_beatbox-rimshot.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `K.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a06_beatbox-inward-snare.wav](06a06_beatbox-inward-snare.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `_K.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a07_beatbox-tongue-click.wav](06a07_beatbox-tongue-click.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `_.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a08_beatbox-lip-pop.wav](06a08_beatbox-lip-pop.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `P_.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a09_beatbox-crash.wav](06a09_beatbox-crash.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `kch.wav` at 0.25 s, zero-padded to 1.2 s.
- [06a10_beatbox-hum.wav](06a10_beatbox-hum.wav) (1.200 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; individual hit `m.wav` at 0.25 s, zero-padded to 1.2 s.
- [06b_beatbox-sequencer-100bpm.wav](06b_beatbox-sequencer-100bpm.wav) (2.900 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render beatbox /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/beatbox 'B t Pf t B t Pf t' male 100 2 --float`; actual BeatPlayer sequencer, 16 sixteenth-note steps at 100 BPM.
- [07_vowel-a-child-110hz.wav](07_vowel-a-child-110hz.wav) (1.800 s): `src/main.rs::human`: body-specific calibrated ɑ, same 110 Hz source across male/female/child; drift/roughness off.
- [07_vowel-a-female-110hz.wav](07_vowel-a-female-110hz.wav) (1.800 s): `src/main.rs::human`: body-specific calibrated ɑ, same 110 Hz source across male/female/child; drift/roughness off.
- [07_vowel-a-male-110hz.wav](07_vowel-a-male-110hz.wav) (1.800 s): `src/main.rs::human`: body-specific calibrated ɑ, same 110 Hz source across male/female/child; drift/roughness off.
- [08_vowel-a-breathy.wav](08_vowel-a-breathy.wav) (1.800 s): `src/main.rs::human`: calibrated male ɑ at 110 Hz: whisper voiced=false/breathiness=.8; breathy breathiness=.8/tension=.2; rough roughness=.85/fry=.65/tension=.3.
- [08_vowel-a-rough.wav](08_vowel-a-rough.wav) (1.800 s): `src/main.rs::human`: calibrated male ɑ at 110 Hz: whisper voiced=false/breathiness=.8; breathy breathiness=.8/tension=.2; rough roughness=.85/fry=.65/tension=.3.
- [08_vowel-a-whisper.wav](08_vowel-a-whisper.wav) (1.800 s): `src/main.rs::human`: calibrated male ɑ at 110 Hz: whisper voiced=false/breathiness=.8; breathy breathiness=.8/tension=.2; rough roughness=.85/fry=.65/tension=.3.
- [09_creature-alligator.wav](09_creature-alligator.wav) (4.000 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render creature alligator /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/creatures --float`; first 4 s (1.2 s default sustained voice, 0.4 s release, then beginning of preset calls), ending faded over 20 ms.
- [09_creature-seal.wav](09_creature-seal.wav) (4.000 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render creature seal /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/creatures --float`; first 4 s (1.2 s default sustained voice, 0.4 s release, then beginning of preset calls), ending faded over 20 ms.
- [09_creature-songbird.wav](09_creature-songbird.wav) (4.000 s): `/Users/robschoen/Dropbox/CC/Chimera/upstream/target/release/chimera render creature songbird /Users/robschoen/Dropbox/CC/Chimera/spike/audition-tool/raw/creatures --float`; first 4 s (1.2 s default sustained voice, 0.4 s release, then beginning of preset calls), ending faded over 20 ms.

## Sample checks

38 files verified after writing: no silent files, no non-finite source samples, no clipped final samples. Final peaks span -1.0002 to -1.0002 dBFS. All first/last samples are zero. Full raw and final statistics are in [statistics.csv](statistics.csv) and [statistics.json](statistics.json).

Maximum adjacent-sample differences and counts above 0.5 full scale are recorded as transient indicators, not a listening-based click detector: frication and percussion can naturally have large differences. Boundary fades prevent hard file cuts; internal clicks cannot be ruled out from sample statistics alone.

Files with adjacent-sample jumps above 0.5 full scale (maximum jump, count):

- 03_ta-male.wav: 0.7374, 7 samples.
- 06a05_beatbox-rimshot.wav: 0.8383, 4 samples.
- 06a06_beatbox-inward-snare.wav: 0.7414, 2 samples.
- 06a08_beatbox-lip-pop.wav: 0.8020, 3 samples.
- 06a09_beatbox-crash.wav: 0.7963, 96 samples.
- 06b_beatbox-sequencer-100bpm.wav: 0.9246, 39 samples.
- 08_vowel-a-whisper.wav: 0.8639, 962 samples.
- 09_creature-seal.wav: 0.6319, 63 samples.
- 09_creature-songbird.wav: 0.7465, 455 samples.

The largest jump is 0.9246 in the beat sequence; no source float samples reached ±1. Maximum absolute final DC mean is below 0.00057 full scale. All output boundary samples are exactly zero. These checks found no obvious silence, numerical instability, clipping, or hard file-boundary cuts. They do not certify absence of audible internal clicks.

The CLI emitted no errors or compiler warnings. Its beat diagnostics report snare onset 11.1 ms before the nominal hit; setup noise for crash is −2.3 dB relative to the hit, and its 60–150 ms tail is −0.2 dB. Hum tail is +0.4 dB relative to its attack window. These are renderer measurements, not musical-quality judgments. Creature sustained pitch diagnostics: songbird 2609.8 Hz versus target 2600; seal 897.4 versus 900; alligator 27.1 versus 28. Full diagnostics are retained in render.log.

All requested categories are supported and included. No rendering errors occurred in this completed run; Cargo build-lock waits are expected.
