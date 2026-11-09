# Chimera Voice — VCV Rack plugin: handoff (2026-11-09)

Status of the working prototype, how it is built, the design decisions behind it, and what is left.
The original project brief is `../CHIMERA-RACK-HANDOFF.md`; this file supersedes it for current state.

## What exists

One module, **Singer** (plugin slug `ChimeraVoice`, brand "Chimera Voice"), wraps
[jamesondh/chimera-vocal-synth](https://github.com/jamesondh/chimera-vocal-synth), a Rust physical-modeling
(FDTD tube network) vocal synth, as a monophonic VCV Rack 2 module. The user has tested it live in
VCV Rack Pro 2.6.6 on macOS arm64. Real-time modulation works, and that was the go/no-go condition.

Scope the user wants: **male singing, female singing, beatboxing**. Vibrato is required.
Speech, creatures and consonant sequencing are deprioritized.

## Layout

```
Chimera/
  CHIMERA-RACK-HANDOFF.md   original brief
  upstream/                 chimera-vocal-synth clone, pinned at 092b16ce (DO NOT edit; see "Fork" below)
  sdk/Rack-SDK/             Rack SDK 2.6.6 mac-arm64
  rack-plugin/              this plugin
    Makefile                RACK_DIR ?= ../sdk/Rack-SDK; builds the Rust lib first (phony `rustlib`)
    plugin.json
    src/Singer.cpp          the module (C++ wrapper, panel, worker thread)
    res/Singer.svg          panel background (20HP)
    rust/                   Rust staticlib `chimera_rack` exposing a C ABI
      chimera_rack.h        the C API
      src/lib.rs            implementation + tests
      examples/ab.rs        A/B renders through the C API, autocorrelation pitch check
      examples/scan.rs      stability scan at every control extreme
      examples/nasal.rs     vowel x nasal level grid (silence check)
  spike/rack-bench/         CPU benchmark in Rack-style per-sample calls
  spike/nasal-diag/         tract-geometry dump used to find the velum bug
  spike/audition-tool/      WAV renderer used for listening rounds
  audition/{,round2,round3} listening renders; round3/README.md + metrics.csv rank each control's audibility
```

The project is a git repo rooted at `Chimera/` (branch `main`). Ignored: `upstream/` (its own clone),
`sdk/`, build output, and rendered audio (`*.wav`, `*.f32`; regenerate with `spike/audition-tool`).
On a fresh checkout, restore the dependencies:

```sh
git clone https://github.com/jamesondh/chimera-vocal-synth upstream && git -C upstream checkout 092b16ce
# download Rack SDK 2.6.6 for your platform into sdk/Rack-SDK
```

## Build, test, install

```sh
cd rack-plugin
make install          # cargo build --release (MACOSX_DEPLOYMENT_TARGET=11.0), then C++ build, installs to
                      # ~/Library/Application Support/Rack2/plugins-mac-arm64/ChimeraVoice
cd rust
cargo test --release -- --test-threads=1     # --test-threads=1 is REQUIRED: the no-allocation test uses a
                                             # global counting allocator and picks up other test threads
cargo run --release --example scan           # also: ab, nasal
```

Restart Rack after installing. Headless smoke test: `Rack -h -u <dir>` (quits on stdin EOF).
If the module doesn't appear in the browser, check that the Brand filter is cleared.

## Architecture

**Rust side (`rust/src/lib.rs`)**: `ChimVoice` holds a `chimera::Engine`, five precomputed vowel postures
(i e ɑ o u via `planner::vowel_artic`), and a velum-cap table. C API:

| function | notes |
|---|---|
| `chim_new(sample_rate, female, child)` | Allocates. Call **only off the audio thread**. Ends with a 256-sample warm-up render, because the first control tick lazily allocates `HumanAirway.base`. |
| `chim_set(v, &ChimParams)` | No allocation. Clamps every field and replaces NaN with a default (`fin()`). Blends vowel postures, adds the larynx offset, caps the velum, sets the gate. |
| `chim_render(v, buf, n)` | No allocation. Wrapped in `catch_unwind`; on panic or non-finite output it marks the voice dead and outputs zeros. |
| `chim_is_dead`, `chim_free` | `chim_free` allocates/frees, so never call it on the audio thread. |
| `chim_velum_cap(v, vowel)` | Exported but not yet declared in the header (debug use). |

Cargo profile: `lto`, `codegen-units=1`, `panic="unwind"` (required for `catch_unwind`).

**C++ side (`src/Singer.cpp`)**:
- **Threading**: a worker thread builds voices (`chim_new`) when the desired body (M/F) or sample rate
  differs from the built one, and hands them to the audio thread through atomics (`incoming`/`outgoing`).
  The audio thread takes a new voice only if it matches the current body and rate and `outgoing` is empty,
  so it never frees or allocates. A failed build retries with backoff. If the active voice's rate doesn't
  match the engine rate, the output is muted until a rebuilt voice arrives.
- **Control rate**: params go to `chim_set` every 16 samples. The engine itself updates every 64 output
  samples, so there is no audio-rate FM.
- **Gate**: unpatched means always on. Otherwise V/10 sets the breath level (0–1.5). A rising edge is held
  for 128 samples so short triggers sound.
- **Idle bypass**: after 1 s of silence with the gate closed, rendering stops (a silent voice otherwise
  costs full CPU).
- **Output**: `6.25 * sample` volts (+25% over the original 5 V).
- **CV**: each knob has a CV input. 10 V spans the knob's full range; CV is added to the knob and clamped to the knob's range.
  There are no attenuverters yet. V/OCT is a standard 1 V/oct.

**Controls → engine**:

| knob | range | maps to |
|---|---|---|
| PITCH | −3..3 oct around C4 | f0 = C4·2^(knob+voct), engine clamps to 30–1500 Hz |
| GLIDE | 2 ms .. 1 s (exp) | source glide time constant |
| VOWEL | 0..4 | blend i→e→a→o→u postures |
| NASAL | 0..1 | velum −0.25..1.4, capped per vowel (see Known issues) |
| LARYNX | ±1.6 | offset added to the vowel's larynx height (Rust accepts ±3.2) |
| TENSION | −0.5..1.5 | source tension |
| BRIGHT | −0.5..1.5 | glottal closure sharpness, not a filter |
| BREATHY | 0..1.5 | abduction 0.1·b² + noise |
| ROUGH | 0..2 | jitter, shimmer, period doubling |
| FRY | 0..1 | vocal fry |
| FALSE FOLDS | 0..1 | ventricular constriction |
| VIB DEPTH / RATE | 0–100 cents / 3–8 Hz | source vibrato (Rust accepts 0–200, 0.5–12) |
| BODY switch | M/F | rebuilds the voice on the worker thread |

Pressure is fixed at 8.0. The user found it was just a volume control because of the engine limiter.

## Key findings (measured)

- **CPU**: ~12% (44.1k), 14% (48k), 16% (96k) of one M4 Max core per voice. The user sees ~17–18% in Rack
  and considers it too high. `AirwayNetwork::step` is ~84% of the time. Per-sample calls cost the same as
  128-sample blocks and give bit-identical output. FTZ has no effect on arm64.
- **No cheap eco mode**: oversample 1× at 48 kHz is unstable (NaN on female, garbage on male). The solver
  needs ~96 kHz internally.
- **Polyphony**: feasible only up to about 4 voices given the CPU cost (voice-count option recommended).
- **Weak controls**: the engine's output limiter (envelope >0.75 → gain down, soft clip at 0.85) flattens
  level differences, and `ARTIC_SPECS` clamps articulators. Round-3 audibility ranking: fry, velum, tension,
  brightness, breathiness (mostly level), larynx strong. Epilarynx, root, dorsum, ttCurl, lateral weak
  or none. Ventricular is subtle.
- **Female body** doesn't sound very feminine (same in the upstream app). Round-2 renders explored
  alternatives; no fix applied.

## Known issues

1. **Velum seals back vowels (engine bug)**. On o/u, a lowered velum closes grid areas to 0 at indices
   ~14–21, below the nasal port, which silences the voice. The workaround (`velum_table` in lib.rs)
   probes static geometry at 33 vowel positions when a voice is built and caps the velum to stay below
   the seal, minus a 0.1 margin. NASAL is weaker on o/u as a result. The sweep shows no silence (worst
   case −22.6 dB, including larynx extremes). A very fast vowel sweep with high NASAL might still dip
   briefly, because the cap uses the target vowel while articulators lag ~10–12 Hz. The proper fix is
   in the engine: the velum should seal the oral side only, allowing an "ng" hum. Worth reporting upstream.
2. **Panic path isn't RT-safe**: `catch_unwind` handles a panic, but unwinding may allocate. Acceptable for
   a prototype.
3. **Panel** is functional only: NanoVG text labels over a plain SVG. Needs a real design.
4. **macOS arm64 only** so far.

## Distribution (VCV's answer)

Andrew Belt (VCV): the Library build toolchain has no Rust, so Rust plugins must be submitted as **prebuilt
binary packages** for each platform, plus identity verification by email (he offered to send details;
not yet requested). Rack 3 will have a C API, with a Rust API hoped for after. So: keep the C++ wrapper
thin for a later Rack 3 port, and build all four platforms (mac arm64/x64, Windows, Linux) ourselves,
likely in CI. `plugin.json` has an empty `authorEmail`, which must be filled in before submission.

## Rules for working on this

- No allocation or freeing on the audio thread. Keep `audio_path_does_not_allocate` passing.
- Don't edit `upstream/` in place. Engine changes go in a fork (below). Upstream's `AGENTS.md` rules
  apply there: golden tests re-bless with `CHIMERA_BLESS=1`, never edit `*.generated.json`, and
  transcendental math only through `chimera::math`.
- Run `examples/scan` (stability) and `examples/nasal` (silence) after any change to ranges or mappings.

## Open work (none started; user hasn't picked yet)

1. **Engine fork**: proper velum fix, wider articulator clamps, replace the 0.75 output limiter,
   optimize `AirwayNetwork::step` (biggest CPU lever).
2. Attenuverters under each CV input.
3. Polyphony, up to ~4 voices, with a voice-count option.
4. Beatbox module (the engine has `Engine::beat_hit`).
5. Better female voicing defaults.
6. Real panel design.
7. Cross-platform builds and Library binary submission; reply to Andrew for the submission details.
