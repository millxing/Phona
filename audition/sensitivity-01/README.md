# Sensitivity audit 01 (2026-11-13)

Question: which values change the sound, and by how much?

Method: change one value at a time, render, and measure the distance to the baseline render
(RMS difference of smoothed log-mel spectrograms, dB). The "floor" is the distance between
renders of the same sound at other trigger offsets. A change at or under the floor is not a
real change. `report.json` holds all results.

Sets: `beat_t`, `beat_Pf`, `beat_B` (each gesture field, engine noise gains, lung pressure,
ready posture, accent, body), `lab_slow` / `lab_fast` (18 Phona Lab values at their range
limits, phrase "cat stop fish sing bed top"), `knobs` (plain voice controls, held vowel).

Reproduce (renders are about 95 MB; use a temporary directory):

```sh
PHONA_SENS_DIR=/tmp/sens cargo test --release --manifest-path rack-plugin/rust/Cargo.toml --lib sensitivity:: -- --ignored
python3 audition/sensitivity-01/analyze.py /tmp/sens audition/beatbox-diagnosis-01
```

Limits: spectral measure, not a listening test. One value at a time, so no interactions.
Male body. References are 128 kbps MP3 previews. The measure does not see pitch modulation
(vibrato). The 90 % energy span is not stable for the kick and the hat; do not rely on it.
