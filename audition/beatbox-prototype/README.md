# Beatbox prototype renders

Generated with `cargo run --release --manifest-path rack-plugin/rust/Cargo.toml --example beatbox -- "$PWD/audition/beatbox-prototype"` from the project root.

`male_B.wav`, `male_Pf.wav`, `male_t.wav` and female equivalents are isolated hits.
`male_pattern.wav` and `female_pattern.wav` are 120 BPM eighth-note patterns.
`timing.csv` contains measured latency from a simulated gate edge at 48 kHz, including the module's
reset alignment delay. WAVs use a fixed gain, not loudness normalization: body/sound level differences
are retained. No listening verdict is claimed. All eight files passed non-silence, PCM16 mono 48 kHz,
clipping and final-tail silence checks.

See ../../rack-plugin/docs/beatbox-prototype.md for timing limits and behavior, and
../../rack-plugin/docs/overnight-progress-2026-11-13.md for the installed prototype and test patches.
