# Beatbox diagnosis 01

Objective: improve Phona mouth percussion against the four user-approved VMDmusic references, with controlled renders and waveform/spectrogram comparisons. Status: first diagnosis and first hat tuning experiment complete; no production DSP change or installation.

## Verified findings

- Restoring fold excitation, enabling the limiter, and removing cancellation each leave isolated B/Pf/t samples bit-identical to the current wrapper, for both bodies. These are not fixes for isolated-hit quality in this configuration.
- At 125 ms trigger spacing, folds and cancellation affect output. No perceptual improvement is established.
- Male 90% energy spans in onset-aligned 400 ms windows: hat reference 24.9 ms, current 164.5 ms, scheduled native 156.6 ms. Snare reference 68.1 ms, current 106.8 ms. Kick heavy/light references 43.1/26.4 ms, current 29.0 ms; similar duration does not imply similar timbre.
- Approximate browser mic EQ/compression gives a hat span of 181.5 ms. This processing does not tighten its energy envelope.
- First hat intervention changes only the BREATH gesture's duration and release ramp. Half pulse: 129.0 ms; quarter pulse: 179.0 ms. Tongue and abduction gestures stay fixed. Custom scheduling at scale 1 is bit-identical to the wrapper baseline. The non-monotonic result needs investigation; it is not proof that the half pulse sounds better.
- Six normal Rust tests pass; both ignored diagnostic tests pass when explicitly run. Render outputs are finite, non-silent, and do not kill the wrapper voice. Exported PCM has headroom and common boundary fades.

## Method and limits

48 kHz, existing pinned engine fork. Wrapper tests use the current trigger path. Native-live uses Engine::beat_hit; native-planned uses the CLI's source/posture recipe and future-scheduled gestures. Native-planned uses 180 ms step length for comparability (CLI example uses 150 ms; these three builders do not use that value). This is not verification of the exact engine revision or effects used in the video.

Alignment: first 5 ms RMS-envelope crossing at 20% of peak; include 20 ms before it and 380 ms after it. Charts compare shape, NOT trigger latency. Early preparation noise can shift alignment. Waveform uses min/max bins, not sample decimation. Spectrogram uses 512-point Hann STFT, 96-sample hop, 0–12 kHz, common -80 to 0 dB scale after peak normalization. Metrics use the unnormalized window. Duration means interval between cumulative 5% and 95% energy, not silence-to-silence length.

Audition hits use a common active-RMS target (not perceptually verified equal loudness), with 1 ms entrance and 5 ms exit fades only on exports. Each kit plays kick, snare, hat, one per second. Hat comparison plays reference, current, half pulse, quarter pulse. Files contain short excerpts, not complete source recordings.

Mic-style is FFmpeg's approximation of browser low shelf +9 dB at 120 Hz, peak +3 dB at 3.5 kHz, and compression. FFmpeg/WebAudio filter and compressor behavior differ. It is not an exact browser render.

References are the approved heavy kick, light kick, punchy snare and closed hi-hat from https://vmdmusic.bandcamp.com/album/beatbox-sample-pack . These are public lossy MP3 previews, about 82 KB total, with provenance/checksums in references.json. Do not optimize exact high-frequency spectra against MP3 artifacts. Reference audio remains local and ignored by git; it is not part of the plugin. No listening judgment is claimed.

## Reproduce from repository root

```sh
python3 audition/beatbox-diagnosis-01/fetch_references.py
PHONA_COMPARE_DIR="$PWD/audition/beatbox-diagnosis-01" cargo test --release --manifest-path rack-plugin/rust/Cargo.toml --lib beat_compare:: -- --ignored --test-threads=1
python3 audition/beatbox-diagnosis-01/hat_pulse.py
cargo test --release --manifest-path rack-plugin/rust/Cargo.toml --lib -- --test-threads=1
```

Uses existing numpy, scipy and ffmpeg; no installation. analyze.py generates comparison.html, metrics.json and four kits. hat_pulse.py also generates hat-pulse-metrics.json and the four-hit comparison. Generated audio and HTML are ignored; scripts and metrics are retained.

## Next work

Inspect the hat's pressure release and tongue/abduction overlap to explain why the quarter pulse grows a tail. Change one control at a time, retain the baseline, and evaluate both energy envelope and spectral distribution. Validate promising candidates by listening and rapid retriggers before changing the live plugin. Do not use waveform similarity alone as the quality criterion. Kick and snare need separate gesture work; do not assume the hat result transfers.

Open risks: long live-trigger preparation delay, cancellation clicks, fast-pattern interactions, lossy references, and no perceptual validation. Female and rapid-pattern renders exist for the baseline interventions; the new pulse experiment is male isolated hat only.
