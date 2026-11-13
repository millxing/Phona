# Female voice: source comparison 01

Listen to A.wav, B.wav, C.wav, and D.wav at the same playback volume. Each lasts about 21 seconds.
One is the current source setting; three change one source control each. Labels are shuffled.

All use the female body and identical notes, gates, articulation, and timing. No vibrato, EQ,
reverb, compression, or changes to anatomy. This tests existing control settings, not a new fold model.

Listening order within each file:
- 0.25 s: ah at A3, D4, G4.
- 3.80 s: ee at A3, D4, G4.
- 7.35 s: oo at A3, D4, G4.
- 10.90 s: “we sing in the warm sun” (C4 D4 E4 G4 E4 C4).
- 15.90 s: “pop tea church blue skies shine” (C4 D4 E4 F4 G4 C4).

First compare the voice without trying to understand the words. Then compare clarity and unwanted
sounds. Report a preferred letter (or none), which sounds most like the female voice you want,
and any word or sound that is worse. This is an informal blind preference check, not a statistical test.

Verification: the renderer rejects dead voices, nonfinite samples, and silent segments. The preparation
script checks equal loudness within 0.15 LU of the shared target, true peaks below -1.9 dBFS,
PCM16 mono 48 kHz output, no sample clipping, and silent file boundaries. Loudness matching applies
one constant gain per file; it does not change the envelope. These checks do not certify sound quality.

The installed plugin and engine source are unchanged by this experiment. The prior VOWEL dimming
change remains in the working tree. No candidate has been selected for release.

Reproduce from the Chimera repository root:

```sh
cargo run --release --manifest-path rack-plugin/rust/Cargo.toml --example female_audition -- "$PWD/audition/female-source-01"
python3 audition/female-source-01/prepare.py
```

`blind-key.json` records the mapping, matching gains, and measured output. Do not open it before listening.
The existing key preserves the label order on subsequent runs. WAV and raw files are ignored by git.

## Listening result (2026-11-12)

User: “they all sound like robots. but a & B sound better”.
A = brightness 0.55; B = tension 0.30; C = breathiness 0.35; D = baseline.
Each candidate changed only that control from baseline (tension 0.50, brightness 0.25,
breathiness 0.15). No preference between A and B was stated. Neither is accepted as a natural voice.
The result supports testing A/B further, but does not establish the cause of the robotic quality.
