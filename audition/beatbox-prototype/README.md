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

## Reference approval and transcript findings (2026-11-13)

User approved VMDmusic's Heavy Kick, Light Kick, Punchy Snare, and Closed Hi-Hat as reference
candidates: https://vmdmusic.bandcamp.com/album/beatbox-sample-pack . Target is human mouth
percussion. No preference between the two kicks was specified. References have not yet been
downloaded or included in the plugin.

The supplied “CHIMERA: A Voice You Can See” transcript describes native command-line renders,
overlapping articulator motion, telemetry export, and separately rendered parts mixed together.
It contains obvious transcription errors, so it is not an exact technical specification and does
not identify the demonstrated revision/settings.

Code inspection prompted by it found two concrete comparison targets:
- The native `render beatbox` path leaves voicing enabled, with F0 100 and pressure 8. Our beat
  prototype explicitly sets fold_scale=0 and uses DEFAULT_SOURCE (F0 110). Whether this changes
  the desired unvoiced hits requires a controlled render; it is not a confirmed cause.
- The browser Beatbox mode enables its close-mic chain by default (store.ts mic=true): low-shelf
  boost, presence filter, compression and makeup in AudioEngine.ts. Our prototype has no such
  chain. This does not establish that the native-rendered video used those effects.

Next diagnostic should compare native beat rendering, wrapper, and optional browser-equivalent
processing before attempting anatomy tuning against the approved human references.
