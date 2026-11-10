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
  upstream/                 chimera-vocal-synth clone, pinned at 092b16ce (untouched reference, DO NOT edit)
  engine/                   local git clone of upstream/, branch `rack` (base 092b16ce): the engine fork the
                            plugin builds against; changes saved as rack-plugin/engine-fork.patch
  sdk/Rack-SDK/             Rack SDK 2.6.6 mac-arm64
  rack-plugin/              this plugin
    Makefile                RACK_DIR ?= ../sdk/Rack-SDK; builds the Rust lib first (phony `rustlib`)
    plugin.json
    src/Singer.cpp          the module (C++ wrapper, panel, worker thread)
    res/Singer.svg          panel background (20HP)
    engine-fork.patch       the fork's (uncommitted) changes against 092b16ce; see "Engine fork"
    rust/                   Rust staticlib `chimera_rack` exposing a C ABI; depends on ../../engine
      chimera_rack.h        the C API
      src/lib.rs            implementation + tests
      examples/ab.rs        A/B renders through the C API, autocorrelation pitch check
      examples/scan.rs      stability scan at every control extreme
      examples/nasal.rs     vowel x nasal level grid (silence check)
      examples/effort.rs    level against EFFORT (RMS dBFS per vowel/pitch)
      examples/breathsweep.rs, breathy.rs, fry.rs   breathiness and fry measurements
      examples/limab.rs        limiter on/off A/B at panel defaults (uses the `chim_set_limiter` test hook)
      examples/bodycmp.rs      dumps raw f32 audio per body/note/vowel for spectrum analysis
      examples/levelmap.rs     level against pitch per body and vowel (corrected); arg `raw` prints the numbers for LEVEL_DB
      examples/breathlevel.rs  level change with BREATHY per body and note
      examples/extin.rs        external input offline test (saw, chord, noise through the tract); writes WAVs to the dir in arg 1
  spike/rack-bench/         CPU benchmark in Rack-style per-sample calls
  spike/nasal-diag/         tract-geometry dump used to find the velum bug
  spike/audition-tool/      WAV renderer used for listening rounds
  audition/{,round2,round3} listening renders; round3/README.md + metrics.csv rank each control's audibility
```

The project is a git repo rooted at `Chimera/` (branch `main`). Ignored: `upstream/` and `engine/` (their
own clones), `sdk/`, build output, and rendered audio (`*.wav`, `*.f32`; regenerate with
`spike/audition-tool`). On a fresh checkout, restore the dependencies:

```sh
git clone https://github.com/jamesondh/chimera-vocal-synth upstream && git -C upstream checkout 092b16ce
git clone https://github.com/jamesondh/chimera-vocal-synth engine && git -C engine checkout 092b16ce
git -C engine apply ../rack-plugin/engine-fork.patch
# download Rack SDK 2.6.6 for your platform into sdk/Rack-SDK
```

**Engine fork**: `engine/` (branch `rack`) holds the changes uncommitted; `rack-plugin/engine-fork.patch`
is the tracked copy, so regenerate it after any engine edit (`git -C engine diff > ../rack-plugin/...`).
All four changes leave default behaviour bit-identical (the engine's own suite passes with no failures and
no re-bless):
- `HumanVoice::limiter: bool` (default true) switches off the output limiter (0.75 envelope gain + 0.85 soft
  clip).
- `ParametricGlottis::fry_pull: Option<f64>` (default None = follow `fry`) separates fry's pitch pull toward
  48 Hz from fry's pulse shape.
- `HumanVoice::ext_in: f64` (default 0): external excitation, a volume velocity in cm³/s injected at the first
  pharynx node (`supra`, just above the glottis) on every internal step through the network's existing
  `ext_flow`, so the tract filters it as it filters the glottal flow. The host sets it before each render
  call; it is held, not cleared.
- `HumanVoice::fold_scale: f64` (default 1) multiplies the vibrating fold area (`glottis.params.amplitude`);
  0 leaves only the static leak (abduction and chink), so the folds are silent.

The patch is 109 lines. See "External input (talk box)".

## Build, test, install

```sh
cd rack-plugin
make install          # cargo build --release (MACOSX_DEPLOYMENT_TARGET=11.0), then C++ build, installs to
                      # ~/Library/Application Support/Rack2/plugins-mac-arm64/ChimeraVoice
cd rust
cargo test --release -- --test-threads=1     # 5 tests pass, incl. level_gain_evens_out_pitch_and_body.
                                             # --test-threads=1 is REQUIRED: the no-allocation test uses a
                                             # global counting allocator and picks up other test threads
cargo run --release --example scan           # also: ab, nasal, levelmap, limab, extin <dir>
```

`cargo` must be on PATH (`~/.cargo/bin`). The Makefile puts the cargo recipe on the library target itself;
with a phony prerequisite, GNU Make 3.81 did not relink the plugin after a Rust-only change.

Restart Rack after installing. Headless smoke test: `Rack -h -u <dir>` (quits on stdin EOF).
If the module doesn't appear in the browser, check that the Brand filter is cleared.

## Architecture

**Rust side (`rust/src/lib.rs`)**: `ChimVoice` holds a `chimera::Engine`, five precomputed vowel postures
(i e ɑ o u via `planner::vowel_artic`), and a velum-cap table. C API:

| function | notes |
|---|---|
| `chim_new(sample_rate, female, child)` | Allocates. Call **only off the audio thread**. Ends with a 256-sample warm-up render, because the first control tick lazily allocates `HumanAirway.base`. |
| `chim_set(v, &ChimParams)` | No allocation. Clamps every field and replaces NaN with a default (`fin()`). Blends vowel postures, adds the larynx offset, caps the velum, sets the gate. |
| `chim_render(v, buf, n)` | No allocation. Wrapped in `catch_unwind`; on panic or non-finite output it marks the voice dead and outputs zeros. Applies the level gain (smoothed per sample) and `soft_clip`. |
| `chim_set_limiter(v, on)` | Engine output limiter on/off, real-time safe. Test hook used by `examples/limab.rs`; the module no longer calls it (the limiter stays off, as `chim_new` sets). Still declared in the header. |
| `chim_set_leveling(v, on)` | Level correction on/off (default on). Test hook used by the examples only (levelmap, limab); not called by the module and not declared in the header. |
| `chim_set_ext(v, mix)` | External input share, 0 = folds only, 1 = external sound only (clamped, NaN gives 0). Sets `fold_scale = 1 - mix` at the next `chim_set`; the input scaling uses the new mix at once. Real-time safe. |
| `chim_render_ext(v, in, out, n)` | As `chim_render`, but one sample at a time with `in` (1.0 = 5 V, clamped to ±4, non-finite gives 0) sent through the tract. No allocation. A null `in` falls back to `chim_render`. See "External input (talk box)". |
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
- **Gate**: unpatched means always on. Otherwise V/10 sets the breath level (0–1.0; the Rust layer accepts
  up to 1.5). The gate also opens and closes the external input (see below). Below 0.1 V the gate is closed, so a small DC offset cannot hold the voice open. A rising
  edge is held for 128 samples so short triggers sound.
- **Idle bypass**: after 1 s of silence with the gate closed, rendering stops (a silent voice otherwise
  costs full CPU).
- **Output**: `6.25 * sample` volts (+25% over the original 5 V). By default the engine limiter is off and
  the Rust layer guards the output with `soft_clip` (see EFFORT below), so the peak is 10 V. The vowel-mean
  level is held near -15 dBFS across pitch and body (see "Level correction").
- **Context menu / patch data**: none. The "Output limiter (old level behaviour)" item and its `"limiter"`
  JSON key were removed at the user's request (the user heard no difference); the module has no
  `dataToJson`/`dataFromJson`/`appendContextMenu` and never calls `chim_set_limiter`. The engine limiter is
  always off (`chim_new`). With FRY at 0 and notes at C3 or below, limiter on and off give sample-identical
  output; above that the only difference is level (`examples/limab.rs`).
- **CV**: each knob except FINE and SOURCE has a CV input (EFFORT included, no trimmer). 10 V spans the knob's full range; CV is added to the knob
  and clamped to the knob's range. VOWEL, NASAL, TENSION, BRIGHT and BREATHY have a CV amount trimmer
  (attenuverter, −100..100%, default 100%) next to the jack. V/OCT is a standard 1 V/oct.
- **Fault handling**: the status light is green while sounding and red on a fault (dead voice or failed
  build). The audio thread checks `chim_is_dead` at control rate and sets `rebuildRequest`; the worker
  builds a replacement voice, at most once per second.
- **Panel layout**: one row per function group. Pitch: PITCH, FINE, GLIDE, VIB DEPTH, VIB RATE. Tract:
  VOWEL, NASAL, LARYNX, BODY. Source: TENSION, BRIGHT, BREATHY, ROUGH, FRY. PITCH and VOWEL use the
  large knob. The bottom strip (all at y 115 mm) is, left to right: EFFORT knob (x 10.16 mm), EFFORT CV (24),
  GATE (40), EXT IN (58), SOURCE knob (71.5), OUT (91.44). `EFFORT_PARAM`/`EFFORT_INPUT` and then
  `SOURCE_PARAM`/`EXT_INPUT` are appended at the end of the enums, so saved patches keep their ids.
- **External input**: the module always renders through `chim_render_ext` (input = EXT IN volts / 5). Every 16
  samples it calls `chim_set_ext` with the SOURCE value only if a cable is in EXT IN, otherwise 0, so SOURCE
  does nothing unpatched.

**Controls → engine**:

| knob | range | maps to |
|---|---|---|
| PITCH | ±36 semitones around C4, snaps to semitones | f0 = C4·2^((pitch+fine)/12+voct), engine clamps to 30–1500 Hz |
| FINE | ±100 cents | added to PITCH (no CV input) |
| GLIDE | 2 ms .. 1 s (exp) | source glide time constant |
| VOWEL | 0..4 | blend i→e→a→o→u postures |
| NASAL | 0..1 | velum −0.25..1.4, capped per vowel (see Known issues) |
| LARYNX | ±1.6 | offset added to the vowel's larynx height (Rust accepts ±3.2) |
| TENSION | −0.5..1.5 | source tension |
| BRIGHT | −0.5..1.5 | glottal closure sharpness, not a filter |
| BREATHY | 0..1.5 | abduction 0.1·b² + chink; level-compensated (see below) |
| ROUGH | 0..2 | jitter, shimmer, period doubling |
| FRY | 0..1 | vocal fry via `fry_map`: 0–50% exact pitch, pulse shape 0..0.85; 50–100% shape 0.85..1 and pitch falls to 48 Hz (`fry_pull`) |
| EFFORT | 25–200 %, default 100 % | lung pressure 8 × effort (2..16 engine units); 10 V CV = full range |
| VIB DEPTH / RATE | 0–200 cents / 0.5–12 Hz | source vibrato (full range of the Rust layer) |
| SOURCE | 0–100 %, default 100 % | folds .. external input share (`chim_set_ext`); acts only with a cable in EXT IN; no CV |
| BODY switch | M/F | rebuilds the voice on the worker thread |

**Fry** (`fry_map` in lib.rs) returns (pulse shape, pitch pull). The stock engine ties both to one number
and pulls the fold rate toward 48 Hz linearly in Hz (10% fry = -1.5 semitones at C4). The fork's `fry_pull`
separates them. Knob 0-50%: pitch stays exact and the shape runs 0..0.85, so the tuned zone has a clear
creak (before, the shape reached only 0.5 there and was very subtle). Knob 50-100%: shape 0.85..1 and the
pitch falls evenly in pitch to 48 Hz. No raised-f0 trick is sent any more, so the old 770 Hz limit and the
shallower vibrato are gone. Check with `cargo run --release --example fry`.

**EFFORT**: range 25-200 %, default 100 %, maps to lung pressure 8 x effort (2..16). The breathiness
compensation multiplies it and the product is capped at 20. `chim_new` sets `limiter = false` (always off in the
module); `chim_render` guards the output with `soft_clip`: linear to 1.0, soft knee
topping out at 1.6 (= 10 V at 6.25 V/unit).
Measured after the level correction (`cargo run --release --example effort`, vowel ɑ, male C3, RMS dBFS):
25 % silent (below the phonation threshold), 37.5 % -34, 50 % -23.5, 75 % -17, 100 % -13, 125 % -9,
150 % -6, 200 % -3.4 (peak 1.5, in the soft clip). About 30 dB of usable dynamics. The level correction
sits after the engine, so loud high notes at high effort still reach the soft clip. (Before the correction
the male C3 numbers were -38 / -27 / -17 / -7 dB at 37.5 / 50 / 100 / 200 %.)

**Level correction** (`level_gain` and `LEVEL_DB` in lib.rs). The raw engine level rises about 12 dB per
octave from C2 to C4 on the male body, and the female body is 6-9 dB under the male one at the same note
(the engine multiplies its output by 0.55 for the female body, expecting her an octave up). The raw
vowel levels differ too (i and u 4-14 dB under ɑ). The wrapper applies a gain from `LEVEL_DB`, a 3-D table
[body male/female][17 notes, minor thirds C2..C6][5 vowels i e ɑ o u] of raw RMS dBFS at panel defaults,
limiter off, smoothed along pitch (vowel mean over 3 points, each vowel's offset from the mean over 5).
`level_gain(f0, female, vowel)` interpolates in pitch, vowel position and body (M/F axis) and brings the
level to `LEVEL_TARGET_DB` (-15 dBFS) plus `VOWEL_TARGET_DB` = [-1.2, 0.4, 1.8, 0.4, -1.2] dB for i e ɑ o u,
so the closed vowels i and u sit about 3 dB under ɑ. Boost capped at +18 dB. The gain follows the fold rate
after the fry pull (`chim_set`), and is smoothed per sample in `chim_render` with the glide time (minimum
20 ms).
Result (`examples/levelmap.rs`, corrected): vowel mean within about ±1 dB of -15 dBFS from C2 to C6 on both
bodies (before: -28..-4 dBFS male, -35..-9 female); single vowels mostly within 2 dB of their target, with
single notes up to 6 dB over where a harmonic sits on a formant (male ɑ at 370 Hz and 740 Hz, about -9 and
-8 dBFS). Peak at EFFORT 100 % is 0.41-0.69 (2.6-4.3 V). EFFORT dynamics are left alone. To re-measure:
`cargo run --release --example levelmap raw` prints the numbers for `LEVEL_DB`.

**Breathiness compensation** (`breath_comp` in lib.rs), two parts, both neutral at the default 0.15:
- *Pressure*: open folds cost 9 dB at BREATHY 1 and 20+ dB at 1.5, so `chim_set` raises the lung pressure
  with b² (×1.86 at b = 1, engine maximum 20).
- *Aspiration noise*: with the engine's stock glottis noise gain the air noise stayed 10+ dB under the voice
  until b ≈ 1.4 (the knob only sounded like a softer voice). `chim_set` scales
  `engine.voice_mut().airway.noise_gain.glottis` up to about ×15 (fast rise, then level) before the
  corrections below. More than that disturbs the fold oscillation.

The aspiration noise does not rise with pitch, so the noise source gain is divided by the level gain (keeps
the noise-to-voice balance constant across pitch); the total factor is capped at 20. The male body's added
noise (the part above factor 1) is trimmed by 4 dB (factor 0.63, blended to 1.0 toward the female body).

Result (male ɑ, before the level correction): total level within 3 dB over the whole knob, noise above
3 kHz audible from ~0.4 and equal to the voice near 0.7; 1.5 is a whisper as loud as the sung note. Male i
gets ~8 dB louder at the top; female noise rises later and female u drops ~9 dB at 1.5. Not yet tuned by ear.
Measured after the level correction, BREATHY level change against 0.15 (vowel ɑ, `--example breathlevel`):
at 1.0 male about +3 dB at C3 and 0 to -1 dB at C4, female +2.6 dB at C3 and 0 dB at C4; at 1.5 the level
falls 4-8 dB above G3 (pressure cap of 20). Noise peaks at BREATHY 1.0 on the male body reach about 1.4
(soft clip region).
Measure with `cargo run --release --example breathsweep [rate]` (live sweep, three bands) and
`--example breathy [vowel]`.

**External input (talk box)**: EXT IN sends an outside sound through the tract in place of the vocal folds,
so VOWEL, NASAL, LARYNX and BODY shape it. Pieces:
- *Injection*: `chim_render_ext` renders one sample at a time. The input passes a one-pole low-pass at
  `EXT_TILT_HZ` = 150 Hz, is scaled by `EXT_FLOW` = 250 cm³/s per unit (1.0 = 5 V), by the mix, and by the
  breath gate smoothed over 5 ms, and is written to `HumanVoice::ext_in`. A closed gate also silences the
  external sound. The input is clamped to ±4.
- *Mix*: `chim_set_ext(mix)` sets `fold_scale = 1 - mix`, so the folds fade out as the external sound fades in.
  The lung pressure stays on: at SOURCE 100 % the BREATHY noise and a small steady leak flow remain.
- *Why the tilt filter*: the tract radiates the derivative of the flow. A glottal flow falls 12 dB/octave, a
  sawtooth only 6. Without the filter the first offline test was thin and harsh (3.5-5 kHz was the loudest
  band on every vowel, and the raw level was 14 dB over the voice). With it the saw shows the same formant
  bands as the folds (i: 200-400 Hz and 1.8-2.5 kHz; a: 600-900 Hz; u: below 600 Hz and 0.9-1.3 kHz),
  somewhat brighter than the folds.
- *Level*: `gain_target` is a geometric blend of the fold-pitch gain and the gain at a fixed `EXT_REF_HZ`
  (130.81 Hz, C3), weighted by the mix (`g_fold^(1-mix) * g_ext^mix`), because the fold pitch says nothing
  about an external sound. EFFORT does not change the external sound's level.
- *What acts on what*: PITCH, TENSION, BRIGHT, ROUGH, FRY and vibrato act only on the fold share.
- *Offline results* (`cargo run --release --example extin <dir>`; 110 Hz, vowel i-a-u-a-i over 8 s, EFFORT
  default, RMS dBFS): folds alone -14.0 male / -14.5 female; saw at SOURCE 100 % -14.5 (peak 1.00) male /
  -12.4 (peak 1.40) female; saw at 50 % -17.1 / -16.1; gate closed -81 (silence). No dead voices. The example
  writes WAVs per body (folds, saw, chord, noise, saw_half, saw_gate_closed).
- *Not yet done*: the user has not listened to it in Rack. SOURCE has no CV input. The input is held
  (zero-order) across the engine's 2x internal oversampling steps rather than interpolated. The female body
  at SOURCE 100 % is about 2 dB hotter than the male one.

FALSE FOLDS was removed (subtle effect); `ventricular` is sent as 0 and the Rust API still accepts it. Param and input ids changed on
2026-11-09, so patches saved with the older prototype load with wrong knob values.

## Key findings (measured)

- **CPU**: ~12% (44.1k), 14% (48k), 16% (96k) of one M4 Max core per voice. The user sees ~17–18% in Rack
  and considers it too high. `AirwayNetwork::step` is ~84% of the time. Per-sample calls cost the same as
  128-sample blocks and give bit-identical output. FTZ has no effect on arm64.
- **No cheap eco mode**: oversample 1× at 48 kHz is unstable (NaN on female, garbage on male). The solver
  needs ~96 kHz internally.
- **Polyphony**: feasible only up to about 4 voices given the CPU cost (voice-count option recommended).
- **Weak controls**: `ARTIC_SPECS` clamps articulators. (The engine's output limiter used to flatten level
  differences and made pressure a volume control; the fork now switches it off.) Round-3 audibility ranking
  (STALE: measured with the limiter on, needs re-measuring): fry, velum, tension,
  brightness, breathiness (mostly level), larynx strong. Epilarynx, root, dorsum, ttCurl, lateral weak
  or none. Ventricular is subtle.
- **Female body** doesn't sound very feminine (same in the upstream app). Round-2 renders explored
  alternatives; no fix applied. The M/F switch changes the body only, not the pitch. Above 4 kHz the female
  voice has about 7 dB less energy than the male voice at C3; this is a property of the fold model and is
  not corrected.

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
- Don't edit `upstream/` (reference only). Engine changes go in the `engine/` fork (branch `rack`); after
  each change refresh `rack-plugin/engine-fork.patch`. Upstream's `AGENTS.md` rules
  apply there: golden tests re-bless with `CHIMERA_BLESS=1`, never edit `*.generated.json`, and
  transcendental math only through `chimera::math`.
- Run `examples/scan` (stability) and `examples/nasal` (silence) after any change to ranges or mappings.

## Open work (user hasn't picked next)

0. Pending from the level-correction round: the user's listening verdict on the pitch, body and vowel level
   correction (vowel-dependent level is now corrected); the round-3 control ranking was measured with the
   limiter on and is stale.

1. **Engine fork** (started; limiter switch, `fry_pull`, `ext_in` and `fold_scale` done). Still open: proper velum fix, wider
   articulator clamps, optimize `AirwayNetwork::step` (biggest CPU lever).
2. External input (EXT IN + SOURCE, done offline, see "External input (talk box)"). Still open: listening test
   in Rack, a SOURCE CV input, interpolating the input across the oversampling steps, the female level.
   Smaller ideas: "snap to vowel" menu option, fade on body swap, M/F/child switch.
3. Polyphony, up to ~4 voices, with a voice-count option.
4. Beatbox module (the engine has `Engine::beat_hit`).
5. Better female voicing defaults.
6. Real panel design.
7. Cross-platform builds and Library binary submission; reply to Andrew for the submission details.
