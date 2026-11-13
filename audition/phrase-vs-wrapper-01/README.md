# Full-phrase planner versus Phona wrapper

Purpose: determine whether the upstream-style phrase route sounds better than our current
real-time syllable route. This is a comparison of complete paths, not a single-variable causal test.
It uses the local engine fork in both cases, not a reproduction of the exact social-media demo.

Listen first to `wrapper_0.wav` and `phrase_0.wav` (no vibrato), then `wrapper_35.wav` and
`phrase_35.wav` (35-cent vibrato depth). Files are dry, female, mono, 48 kHz PCM16, about 10.2 s.
No reverb, backing music, harmony, or extra EQ is added.

Shared score: “we sing in the warm sun pop tea church blue skies shine”.
MIDI notes: 60 62 64 67 64 60 60 62 64 65 67 60.
Beats: 1 1 0.5 0.5 1 2 1 1 1 1 1 2. Tempo 96 BPM; first written beat at 0.6 s.

Shared nominal source settings: pressure 9, tension 0.55, brightness 0.25, breathiness 0.15,
roughness 0.1, glide 60 ms, vibrato rate 5.5 Hz, fry 0. The female tract and dictionary are
shared. No user-selected candidate from the first audition was substituted.

Path differences retained deliberately:
- Phrase: `HumanVoice` + `plan_song`, as `render notes` uses. Consonants precede the written
  vowel beat; breath and transitions are planned across the whole phrase. Vibrato gestures
  start after vowel onset and grow. Default engine limiter remains on.
- Wrapper: `chim_*` API, with controls sent every 16 samples. Gates begin on the same written
  beats and last 80% of each note; consonants follow the trigger and codas follow gate-off.
  Vowels therefore do NOT begin at identical times. At 35 cents, vibrato is the wrapper's
  constant panel depth, not the phrase planner's growing envelope. Existing level correction,
  breath compensation, lyric make-up, coda processing and fork flags remain in place.
  A 0.5 s discarded gate-closed preroll clears construction transients before recording.
- No C++ Rack process is run in this test. Its control cadence is represented, but its 1 ms
  gate/reset delay, worker swaps and Rack host behavior are not tested.

Each file receives a single constant gain to match integrated loudness. No external limiter
or compressor is added. `verification.json` records the levels and gains. Passing signal
checks establish file validity, not perceived quality or exact matching of pitch trajectories.

Verification: renders are finite and non-silent; wrapper voices remain alive. Preparation checks
mono 48 kHz PCM16, unclipped samples, silent file boundaries, loudness within 0.15 LU of target,
and true peaks below -1.9 dBFS. All five wrapper tests passed, including audio_path_does_not_allocate.

Reproduce from repository root:

```sh
cargo run --release --manifest-path rack-plugin/rust/Cargo.toml --example phrase_compare -- "$PWD/audition/phrase-vs-wrapper-01"
python3 audition/phrase-vs-wrapper-01/prepare.py
```

Installed plugin, engine code, and wrapper DSP are unchanged. The previous VOWEL dimming edit
remains separate in the working tree. Awaiting user preference: phrase better, wrapper better,
or neither; and whether the same result holds with and without vibrato. If phrase wins, isolate
planning from wrapper output processing next. If neither approaches the reference, obtain its
score/settings or investigate the remaining engine-version and production differences.
