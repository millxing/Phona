# Beatbox prototype

Status: implemented and checked offline on macOS arm64. Listening and live Rack checks remain with the parent. No engine source or `engine-fork.patch` changes.

## Use and behavior

Select **Beatbox mode** in Phona's context menu. The existing text field accepts case-sensitive, whitespace-separated tokens: `B` kick, `Pf` snare, `t` closed hat, `.` rest. Example: `B t Pf t B . Pf t`. Each rising GATE edge advances one token and wraps. Gate threshold is the existing 0.1 V. Gate height does not change accent. An unpatched GATE does not play.

Unknown tokens each occupy one silent step, shown as `?`; punctuation is not stripped and aliases are not accepted. Empty text is silent. The vowel plate shows the current token, or `--` before a hit / while waiting for a pattern. Only BODY, GATE, RESET and OUT apply; the other cells are dimmed, with the token plate left clear. Lyrics dimming is preserved.

A hit uses `Engine::beat_hit`, not pronunciation. Its full gesture runs even with a one-sample trigger. Each new step cancels the preceding score: a new hit replaces it; a rest/invalid step cancels and mutes it. Fast retriggers can prevent a prepared hit from reaching its release. There is no overlap, clock prediction, sample playback, or hidden sequencer.

Every edge waits the existing 1 ms reset window (47 samples after the edge sample at 48 kHz). A fixed 1024-slot delay line retains separate edges inside that window, covering supported rates through 768 kHz. RESET cancels/mutes the current hit and selects token one for the next pending/new edge. It leaves pending delayed edges intact: RESET 0, 2 or 30 samples after a gate selects token one. A reset outside the window cannot undo sound already emitted.

Mode changes cancel all pending edges and scores, drop lyric pointers, and reset the position. Entry to Beatbox with GATE already high requires a low level and then a new rising edge. Text changes cancel/mute on the next audio sample and restart at token one after the worker publishes the new pattern. Edges while a pattern is unavailable are silent, not replayed later. Body/sample-rate voice replacement also requires a fresh edge. A Lab-only replan retains the position.

Patch JSON keeps the existing `lyrics` string and adds boolean `beatbox`. Missing/false means the previous Lyrics/plain-vowel behavior. Parameter/input IDs did not change. Initialize selects Lyrics with empty text and Rack's default knobs.

## Audio ownership and real-time safety

Beat entry uses the engine's body-specific `beat_ready` posture and fixed default source. Gate stays zero: only beat gestures supply breath. Singing control updates return immediately in Rust, so they cannot overwrite abduction, glottal closure, effort or tract targets between engine control ticks. EXT IN is ignored. `fold_scale = 0` removes periodic fold excitation while retaining static leakage and ejective closure. This is a deliberately unvoiced three-sound scope; adding hum/bass needs a different policy. Exit restores fold scaling and the next singing control update.

Beat output uses unity gain, the existing final soft clip, and no lyric high-pass, lyric makeup, or pitch/vowel compensation. This preserves the kick's low pressure pulse. Levels differ between bodies. Empty/cancelled beats are output-muted while the physical airway continues to settle. Cancellation is a hard mute, so reset/rest/edit during a loud hit can click. Mouth state is not reconstructed on a retrigger; preceding hits can affect its sound and timing.

Parsing, vectors and destruction stay on the worker. The audio thread reads an immutable token vector and uses the existing atomic hand-back slots. Delay storage is fixed. Engine hit scratch and score storage are preallocated. Normal beat entry, hits, cancellation, controls, rendering and mode exit pass the allocation **and free** counter. Existing panic recovery remains a prototype limit: unwinding is not guaranteed allocation-free.

Beat mode keeps rendering even when silent so gesture time and mouth settling cannot stall. This costs idle CPU; no CPU optimization was attempted.

## Measured timing (verified)

`rust/examples/beatbox.rs`, 48 kHz, one-sample rendering, controls each 16 samples, both bodies. The voice settles for 0.5 s before an isolated trigger. Times below are from the simulated Rack edge and include its 47-sample reset delay. Onset is the first absolute sample at the stated fraction of that hit's peak (1% = -40 dB relative, 10% = -20 dB, 50% ≈ -6 dB). These are measured signals, not engine metadata or perceptual judgments.

| Body | Token | 1% onset ms | 10% onset ms | 50% onset ms | Peak ms | Peak engine output |
|---|---|---:|---:|---:|---:|---:|
| Male | B | 3.312 | 140.312 | 141.583 | 141.688 | 0.176526 |
| Male | Pf | 3.479 | 120.271 | 120.375 | 120.729 | 0.245001 |
| Male | t | 3.042 | 3.354 | 107.062 | 108.500 | 0.099058 |
| Female | B | 3.312 | 140.333 | 140.354 | 141.646 | 0.077210 |
| Female | Pf | 3.521 | 120.271 | 120.375 | 120.771 | 0.109732 |
| Female | t | 3.042 | 3.333 | 107.146 | 108.792 | 0.038975 |

Mouth-preparation transients make the low-threshold onset early; they do **not** mean a 3 ms kick. The main bursts are much later and differ by sound. Rack adds audio-device/buffer latency. Arbitrary gate phase, first hit after entry, fast patterns and other sample rates can differ; those timings have not been characterized. No beat alignment claim is made. The example also renders a 120 BPM eighth-note pattern for each body. WAV samples are engine output divided by 1.6 (no normalization); Rack output is engine output times 6.25 V.

## Verification and integration

Verified:

- `cargo test --release --lib -- --test-threads=1`: six tests passed. Includes existing singing/lyrics checks, beat allocation/free checks on both bodies, exact preservation of gesture-sensitive controls, zero fold amplitude, empty silence with held gate/external signal, dense score replacement, invalid cancellation, and mode restoration.
- `tests/beatbox.sh`: real C++ module plus worker against Rack SDK; token order/loop, invalid/rest, one-sample and held gates, multiple edges inside 1 ms, delayed RESET, body replacement, immediate clear silence, JSON recall/legacy JSON, mode entry with held gate, Initialize passed.
- `make -j2`: linked `plugin.dylib`. SDK emits its existing old macOS target warning. Test harness uses Rack's internal Engine constructor to support Initialize and emits a deprecation warning.
- Offline isolated and pattern renders completed without a dead voice. Last 250 ms of each isolated PCM16 WAV was zero.
- `git diff --check` passed.

Not run: live Rack UI/audio, listening evaluation, engine suite (engine unchanged), other platforms, multi-rate beat timing/stress, new CPU benchmark. Existing vowel/velum mappings were not changed, so no new nasal/range scans were required.

Parent integration, from the project root with its existing `engine/` fork and `sdk/Rack-SDK`:

```sh
# Cherry-pick the implementation commit if integrating into another checkout.
cd rack-plugin
make -j2
./tests/beatbox.sh
cd rust
cargo test --release --lib -- --test-threads=1
cargo run --release --example beatbox -- /tmp/phona-beats
```

No dependency installation is needed. This worktree used temporary symlinks to the parent's engine and SDK. They are not part of the commit. Parent owns any live Rack installation. Listen to the six isolated WAVs and two pattern WAVs, then check gate/reset routing and panel dimming in Rack before treating this as ready for use.
