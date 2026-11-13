# Phona — VCV Rack plugin: handoff

## Current snapshot (2026-11-13)

The sections below are historical implementation notes. Their “not committed” and outstanding
listening labels are not a reliable current task list. Lyrics, panel work, and the voice audition
experiments are committed. The user prefers to develop a live instrument rather than reproduce
the preplanned duet demo; pause female-tone tuning for now.

A dedicated beatbox prototype now accepts `B`, `Pf`, `t`, and `.` in the shared text field via
Lyrics/Beatbox menu selection. Every gate advances one token; reset restarts the sequence.
The model needs substantial preparation time, so this is experimental, not a tight drum-machine
replacement. See [beatbox details](docs/beatbox-prototype.md) and
[live test patches/checklist](docs/live-instrument-test.md). The engine fork is unchanged.
VOWEL dims during lyrics; ineffective controls dim during beatbox. Save data adds `beatbox`
without changing parameter/input IDs; old patches load in Lyrics mode.

See [overnight report](docs/overnight-progress-2026-11-13.md) for integration, installed-build
status, verification, remaining risks, and next steps. No new listening verdict is claimed.

---

# Historical handoff (2026-11-09 onward)

Status of the working prototype, how it is built, the design decisions behind it, and what is left.
The original project brief is `../CHIMERA-RACK-HANDOFF.md`; this file supersedes it for current state.

## What exists

One module, **Phona** (plugin slug `Phona`, brand "Phona"), wraps
[jamesondh/chimera-vocal-synth](https://github.com/jamesondh/chimera-vocal-synth), a Rust physical-modeling
(FDTD tube network) vocal synth, as a monophonic VCV Rack 2 module. The user has tested it live in
VCV Rack Pro 2.6.6 on macOS arm64. Real-time modulation works, and that was the go/no-go condition.

Scope the user wants: **male singing, female singing, beatboxing**. Vibrato is required.
Speech and creatures are deprioritized. Words (lyrics mode, see "Lyrics mode") were added on 2026-11-10.

## Names (changed 2026-11-10)

The plugin and the module are both called **Phona** (plugin slug `Phona`, module slug `Phona`, version 2.1.0).
Before that they were `ChimeraVoice` / `Singer` (2.0.0); older notes below were renamed by search and replace.
Rack 2 only loads plugins whose version starts with `2.`, so the user's "version 1.0" is 2.1.0. Patches saved
with the old slugs do not find the new module. The engine and the Rust wrapper keep the name Chimera.

## Sharing a build

`make share` writes `share/Phona-<version>-mac-arm64.vcvplugin`. Give people this file, not the one in
`dist/`: the archive headers of the `dist/` file hold the build machine's user name. The Makefile also remaps
source paths in the Rust build (`--remap-path-prefix`) for the same reason. Check with
`zstd -dc share/*.vcvplugin | strings | grep -i <user name>` (must find nothing). The public author name is the
user's forum handle, Spunoza (`plugin.json`, `LICENSE`); `LICENSE` also carries the engine's MIT notice
(Jameson Hodge), which must ship with every build; so must `LICENSE-CMUDICT.txt` (the licence of the CMU
Pronouncing Dictionary, embedded since lyrics mode; the Makefile's `LICENSE*` wildcard packs it). Only mac-arm64
exists so far.

## Lyrics mode (branch `lyrics`, 2026-11-10, not committed)

Why: the user compared Phona with the engine's web app (https://chimera-vocal-synth.vercel.app/), which sings
words. The app's word layer (phonetics: dictionary, consonants, live note-on/off planners in the engine's
`phonetics/sing.rs`) was already linked but unused. No engine change was needed.

What it does: the module's right-click menu has a Lyrics text field (and "Clear lyrics"). With text in it, each
rising edge of GATE sings the next syllable: the onset consonants, then the vowel held while the gate is high.
The falling edge sings the diphthong glide and the coda. The syllables wrap around. The RESET input (in the GATE
cell, `RESET_INPUT`, appended last in `InputId`) goes back to the first syllable. A hyphen in the text splits
syllables. Pitch stays with V/OCT, GLIDE and the vibrato knobs: the F0 and vibrato channels are removed from the
planned gestures (`strip_pitch`). The VOWEL knob has no effect while lyrics are on; the vowel plate shows the
syllable being sung. Empty text = the plain voice. The text is saved in the patch (`dataToJson` key `"lyrics"`);
module Initialize clears it.

Rust side: `chim_lyrics_new(text, female)` (allocates; worker thread) plans every syllable ahead into
`SylPlan { on, off, vowel_t0, off_len, text }` using `lyrics_to_syllables`, `plan_note_on` and `plan_note_off`.
Melisma tokens are dropped (a pitch change with the gate held does the same). `chim_set_lyrics(v, on)`,
`chim_note_on(v, l, i)` and `chim_note_off(v)` are real-time safe (the engine's score has fixed capacity; the
alloc test in lib.rs covers the lyrics path). A gate shorter than the onset plus `LYR_MIN_VOWEL` (70 ms) is held
by the wrapper until the vowel has sounded (`lyr_pending_off`). A new onset waits for the previous coda
(`lyr_tail_end`).

Dictionary: the CMU Pronouncing Dictionary from the engine (`engine/public/data/cmudict.txt.gz`) is embedded with
`include_bytes!` and unpacked with `flate2` on first use (worker thread). The plugin binary grew from about 2.4 MB
to 3.6 MB. English only.

Levels: consonant bursts are 3 to 4 times a vowel's level in the raw engine (`examples/sing.rs` with env `RAW=1`).
In lyrics mode the engine's own limiter is turned on (as in the app; `chim_set_lyrics` does it), and
`LYR_MAKEUP` = 1.6 gives back part of the level, because sung words came out about 8 dB under the plain voice.
These values were set from measurements only; nobody has listened yet.

Threading in `Phona.cpp`: `BuiltLyr` is handed over like a voice (atomic `lyrIncoming` / `lyrOutgoing`). Before the
audio thread takes new lyrics it calls `chim_set_lyrics(voice, 0)`, so the voice drops its pointer to the old ones.
Lyrics are planned per body, so after a BODY change the plain voice sounds until the matching lyrics arrive.
`lyricsMutex` guards the text and the syllable spellings; only the UI and worker threads lock it.

Timing limit: the vowel starts after the onset consonants, so it is late by the onset's length. The app's Sing
page knows the song ahead and puts consonants before the beat; a Rack module cannot.

Tests: `cargo test --release -- --test-threads=1` passes (5 tests). `cargo run --release --example sing -- <dir>`
writes test WAVs (male and female, four texts; env `RAW=1` = no level correction, `LIM=1` = limiter). A harness
outside the repo ran the real module for 30 s with gates, a reset, a body change and two lyric changes under
load: no fault, about 8.5% CPU of one core (continuous, not Rack's meter).

Not done: listening check by the user; no on-panel text editor (menu only); the VOWEL cell is not greyed out in
lyrics mode; no CV selection of syllable.

Observed, not changed: in the plain voice with default settings and gated notes at C3, the harness measured peaks
at the 10 V soft-clip ceiling.


### Faults found in the first listening test (2026-11-10), all in `rust/src/lib.rs`

The user typed "doh ray mee fah soh lah tee doh" and heard "note", "gvay", "like", "neat". `examples/lyrab.rs`
renders the same words through the engine as its app drives it (A) and through the wrapper (B); the faults
were found by comparing the two.

1. **Voiceless consonants were voiced.** `chim_set` runs every 16 samples and calls `set_source`, which puts
   the glottal abduction back to the knobs' value. The score opens the glottis for [s], [f], [t] only on the
   engine's control tick (every 64 samples). Fix: with lyrics, the abduction is saved and restored around
   `set_source`. Low-band level during [f s t] fell by 10 to 12 dB, to the app path's level.
2. **A click at every note start, also in the plain voice.** The same clash on the glottis `effort` value:
   the engine derives it from the pressure in use (zero while the breath starts), `set_source` from the full
   pressure, so it jumped 750 times a second during attacks and releases. Fix: `effort` is always saved and
   restored. This also changes the plain voice: gated notes at C3 peaked at the 10 V clip ceiling, now 3 to 4 V.
   The GitHub release v2.1.0 still has the click.
3. **Stops became nasals with NASAL raised** ([t] to [n]). The engine leaves the velum to the performer except
   for nasals. Fix: `strip_pitch` adds a closed velum to every non-nasal consonant's main gesture.
4. **"fah", "soh", "lah" were mispronounced** by the spelling rules. `sung_syllable` gives fixed phonemes for
   common singing syllables (doh ray mee fah sol lah tee, ah, ooh, da, ba, na, ...).
5. With lyrics, the breath noise is no longer divided by the level gain (the engine's own balance, as in the app).

`chim_debug` is a test hook for `examples/lyrab.rs`. `examples/phones.rs` prints phonemes (and gestures with
`GEST=1`); `examples/syls.rs` prints the syllable split. Not yet heard by the user after these fixes.

### Reset that arrives after the gate (2026-11-10, `src/Phona.cpp`)

The user's sequencer sent RESET and GATE together and heard "doh" twice. Each cable adds one sample, so the
reset can arrive some samples after the gate: the gate sang the old position, then the reset set the next
gate to syllable 1 again. Fix: the syllable starts 1 ms after the gate opens (`lyrDelay`), so a reset inside
that time counts. Checked with a test that compiles `Phona.cpp` into a small program and prints
`shownSyllable()`: reset 0, 2 and 30 samples late gives the correct order; 60 samples late (outside the
window) shows the old fault.

### Extra sound after a last consonant (2026-11-10, `rust/src/lib.rs`)

The user heard a short blip after each syllable that ends in a consonant. Cause: the engine's planner opens
the last consonant about 50 ms before the breath has gone (as in speech), and the open mouth sounds as a
short vowel ("dum" = "dum-uh"; the level went up 4 to 6 dB at the end of [m]). The engine's app has it too,
a little weaker. Tried and rejected: to hold the consonant closed for longer. The air caught behind the
closure makes a pop of -21 to -35 dB (relative to the vowel at about -10) when it opens, even 0.5 s later
and with a slow release. Fix: `coda_end` gives the time when the last consonant starts to open
(`SylPlan::mute_at`); `chim_render` fades the output out there (4 ms) and back in when the next syllable
starts (`lyr_mute_at`, `lyr_unmute_at`, `lyr_fade`). Voiceless stops (p, t, k, tʃ) keep their release.
`examples/phones.rs` with `GEST=1` now prints the note-off gestures too.

### Final voiceless stops too strong (2026-11-10, `rust/src/lib.rs`)

The user: "pop comes out fine, but then there is this higher pitch blip afterward". Measured on the final
[p]: a pressure pulse 0.55 of the vowel peak (0 to peak in less than 1 ms, 0.46 in the engine's app path),
then about 25 ms of hiss near 6 kHz. Wider fold opening (abduction 0.4 to 1.0) changed nothing, so it is not
voicing. `soften_release` sets the breath target of the note-off plan to `CODA_STOP_BREATH` (0.5) for
syllables that end in p, t, k, tʃ. Not confirmed by ear that the hiss is the blip the user means.

### Phona Lab: tuning module (2026-11-10)

The user could still hear the blip after a last [p] and asked for a module with many parameters, to find the
best values by ear and report them. **Phona Lab** (`src/PhonaLab.hpp`, `src/PhonaLab.cpp`, panel from
`tools/panel_lab.py`, slug `PhonaLab`, 16 HP) is that tool. It has no audio and no cables. A Phona reads the
knobs of a Lab directly beside it (left or right; `rightExpander.module->model == modelPhonaLab`), and uses
the defaults when there is none. COPY (button and menu) puts all values on the clipboard as one line
(`PhonaLab stopBreath=0.5 ...`); "Paste values" in the menu reads such a line. It is not meant for the public
release: remove it from `plugin.json` and `plugin.cpp` (or keep it hidden) before a release, after the values
found are made the defaults.

- The 16 tunable values are `T_*` / `TUNE_DEFAULT` in `rust/src/lib.rs`; Lab param ids are the same indices,
  and `PhonaLab::spec(i).def` must equal `TUNE_DEFAULT[i]`. Values that shape a syllable's plan (`tune_on`,
  `tune_off`; `spec.plan`) go to `chim_lyrics_new(text, female, tune)`; the others (fade time, min vowel,
  next wait, level, limiter) go to the voice with `chim_set_tune`. Null = defaults.
- A change of a plan value makes the worker plan the lyrics again (`tuneVersion`). Such a build has
  `keep = true`: the audio thread holds it in `lyrPending` and swaps it in without a reset of the syllable
  position, when no syllable is held (`chim_lyrics_idle`) or at the next note-on (the note-on ends the held
  syllable from the old plans first). A build with new text or body resets as before.
- With default values the output is bit-identical to the build before (checked with `examples/lyrab.rs`;
  env `TUNE="0=0.3,5=10"` sets values by index there).
- `src/panel.hpp` now holds the panel colours, `panelFont` and `PanelLabel` for both widgets.
- The limiter switch made no difference in the `lyrab` render (the limiter does not act at those levels).

### "church" (2026-11-10, `rust/src/lib.rs`)

The user: Phona "really struggled with church"; and a very low CUT on the Lab reduced the pop after a last
[p] (the default is still OFF, until the user sends values). Two faults, found with `lyrab` on
"church church":
- A last [tʃ] was treated as a stop, so STOP BREATH halved the breath of its fricative part: 10 to 13 dB
  less hiss than the app path. Now only p, t, k are stops there; [tʃ] and [dʒ] keep the full breath and the
  output fades 20 ms after the fricative part ends (plus FADE START).
- A syllable that waits for the coda of the one before (`lead` in `chim_note_on`) had its gate open from the
  note-on, so the folds sang the base vowel until its gestures started: an "uh" before the word, 12 dB under
  the vowel for up to 0.2 s. Now the breath waits for `lyr_start`.

### The pop of a first p/t/k; Lab has 18 values (2026-11-10)

The user: CUT at about 20 ms removes the pop after a last [p], but "something like a switch sound" stays,
and nothing in the top row removes it. Measured on "pop pop" (`coda/gap.py` in the scratchpad: 5 ms peak
levels between two syllables): with CUT at 20 ms or less the end of the word is silent. What stays is the
**first [p] of the next word**: the same low pressure pulse, 51% of the vowel peak, about 75 ms after the gate.
The Lab had no control for it. Two were added (param ids 16 and 17, at the end, so the order of the others
stays; the panel order is `PhonaLab::cellParam`):
- **BREATH** (third row, `T_ONSET_BREATH`, default 1): breath while a first p/t/k is closed. The engine
  takes the higher of the gate and the score's breath (`gate_goal = max(gate_target, score_breath)` in
  `human_voice.rs`), so a breath gesture alone does nothing during a held note: the wrapper also lowers the
  gate for that time (`SylPlan::dip`, `lyr_dip`). 0.5: 39%; 0: 27%.
- **THUMP CUT** (fourth row, `T_THUMP`, default 0 = off): 2-pole high-pass on the output at this share of
  the note's pitch, lyrics mode only. 0.7: 29%. With BREATH 0.3: 20%.
Also found: a small tick 5% of the vowel peak, mostly above 3 kHz, 66 ms after the gate-off of a last [p]
(the first leak of the closure). CUT at 10 ms or less removes it; at 20 ms it stays.

### Consonants judged by a speech recognizer; the sung plan rebuilt (2026-11-10, `rust/src/lib.rs`)

The user cannot tune consonants by ear ("I can just tell when it doesn't sound like the word"),
so a recognizer is the judge: `tools/word_judge.py` gives P(right word) among about 150 similar
words (Whisper small.en, forced scoring). The macOS `say` voice scores 94 %. 20 words with
stops and 10 others, at C3 and G3.

| | stop words | other words |
|---|---|---|
| before | 14 %, 7 of 40 right | 64 %, 16 of 20 |
| engine's speech path (`chimera render text`) | about 60 % | |
| same words as one plan, flat pitch, 0.45 s vowel (`examples/sp.rs`) | 68 % | |
| now | 66 %, 34 of 40 | 82 %, 19 of 20 |

Lab knobs alone reached 40 % (ONSET LEN 1.5, STOP HISS 0.3, CODA LEN 2) and no further. The
causes were in the plan, found by moving from the one-piece plan to the split one step by step:

1. **The live planner puts gestures 45 ms in the past** (`LIVE_T0`, to save latency). The first
   consonant starts half formed with the breath on: [p t k] were heard as [h]. Now `plan_on`
   with `ON_T0 = 0.02` and `plan_off` with `OFF_T0 = -0.02` (41 % -> 67 % in the bare engine).
   Cost: the vowel is about 65 ms later after the gate than before.
2. **The gate must not open before the plan's breath** (`lyr_start = at + breath_t0`). With the
   gate open 50 ms before the gestures the score was 2 %.
3. **Consonants at their spoken length**, not 0.7 of it. Affricates keep 0.7 of the table
   length (the duration rules halve them: "church" was "it").
4. **Base mouth position is neutral with lyrics**, not the VOWEL control's vowel (49 % -> 70 %;
   "tea" was "see", "do" was "to"). LARYNX, NASAL and FALSE FOLDS still apply.
5. **Last [b d g] are handled as stops** (no output fade): the fade removed their release.

ONSET LEN and CODA LEN in Lab now mean 1 = spoken length. Their default 1.0 is right.

Still wrong: "pie" is "bye", "bed" is "bet", "cat" is weak, [s] and [sh] are close to each
other. The engine's own speech path has the same faults, so they are in the engine's
consonant targets, not in the wrapper. Not tested: the female body, two-consonant onsets,
the user's ear.

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
    src/Phona.cpp          the module (C++ wrapper, panel, worker thread)
    res/Phona.svg          panel background (20HP); generated by tools/panel.py, do not edit by hand
    LICENSE-CMUDICT.txt     licence of the embedded CMU dictionary (shipped in the package)
    engine-fork.patch       the fork's changes against 092b16ce; see "Engine fork"
    rust/                   Rust staticlib `chimera_rack` exposing a C ABI; depends on ../../engine
      chimera_rack.h        the C API
      src/lib.rs            implementation + tests
      examples/ab.rs           A/B renders through the C API, autocorrelation pitch check
      examples/scan.rs      stability scan at every control extreme
      examples/nasal.rs     vowel x nasal level grid (silence check)
      examples/effort.rs    level against EFFORT (RMS dBFS per vowel/pitch)
      examples/breathsweep.rs, breathy.rs, fry.rs   breathiness and fry measurements
      examples/limab.rs        limiter on/off A/B at panel defaults (uses the `chim_set_limiter` test hook)
      examples/bodycmp.rs      dumps raw f32 audio per body/note/vowel for spectrum analysis
      examples/levelmap.rs     level against pitch per body and vowel (corrected); arg `raw` prints the numbers for LEVEL_DB
      examples/breathlevel.rs  level change with BREATHY per body and note
      examples/extin.rs        external input offline test (saw, chord, noise through the tract); writes WAVs to the dir in arg 1
      examples/velumgeo.rs     static geometry against velum per vowel (narrowest point, below or above the nasal junction);
                               arg 1 = body 0/1, arg 2 = a vowel prints the rays near the junction; env STOCK=1 = the engine's stock behaviour
      examples/nasalscan.rs    level (RMS dBFS, peak) against NASAL per vowel position and body, with the velum cap; flags dead voices
      examples/nasalsweep.rs   live sweep NASAL 0 to 1 to 0 at 9 vowel positions on both bodies: dead voices, largest sample jump (clicks), peaks
      examples/sing.rs         lyrics mode offline: gates and pitches sung to a text, WAVs to the dir in arg 1; see "Lyrics mode"
      examples/bench.rs        CPU benchmark through the C API, per-sample calls as the module makes them; see "CPU optimisation"
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

**Engine fork**: `engine/` (branch `rack`) holds the changes; `rack-plugin/engine-fork.patch` is the tracked
copy against 092b16ce, so regenerate it after any engine edit (`git -C engine diff 092b16ce > ../rack-plugin/...`).
Git state (lyrics mode on branch `lyrics` not committed: `lib.rs`, `chimera_rack.h`, `Phona.cpp`, `Cargo.*`,
`examples/sing.rs`, `LICENSE-CMUDICT.txt`; panel redesign of 2026-11-10 not committed: `Phona.cpp`, `Phona.svg`, `tools/`, this file): everything up to and including the external input is committed (outer repo branch `singer-controls`,
commit ec704b8; engine branch `rack`, commit 51608fe). The nasal fix (`velum_junction`), the grey-out of
fold-only controls and the CPU changes (see "CPU optimisation") are committed. The five host changes
below leave default behaviour bit-identical (the engine's own suite passes, 83 tests, no failures, no re-bless):
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
- `HumanVocalTract::velum_junction: bool` (default false, so the engine and its tests are unchanged; the wrapper
  turns it on in `chim_new` and in `velum_table`): the nasal fix, see "Nasal fix" below.

The CPU changes are also in the fork, all in the engine: `AirwayNetwork::step` (slices per section, cached
reciprocals), `project_on` (skips `hypot`), `HumanAirway::commit` (skips the geometry when `frame_key` is
unchanged) and `ArticulatorBank::settle` (default false, so the engine and its tests are unchanged; the wrapper
sets it in `chim_new`). The patch is 641 lines, regenerated. See "External input (talk box)", "Nasal fix" and
"CPU optimisation".

## Build, test, install

```sh
cd rack-plugin
make install          # cargo build --release (MACOSX_DEPLOYMENT_TARGET=11.0), then C++ build, installs to
                      # ~/Library/Application Support/Rack2/plugins-mac-arm64/Phona
cd rust
cargo test --release -- --test-threads=1     # 5 tests pass, incl. level_gain_evens_out_pitch_and_body.
                                             # --test-threads=1 is REQUIRED: the no-allocation test uses a
                                             # global counting allocator and picks up other test threads
cargo run --release --example scan           # also: sing <dir> (lyrics), ab, nasal, levelmap, limab, extin <dir>, bench (see "CPU optimisation")
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
| `chim_lyrics_new(text, female)`, `chim_lyrics_free(l)`, `chim_lyrics_len(l)`, `chim_lyrics_text(l, i, buf, n)` | Lyrics plans for one body (see "Lyrics mode"). New and free allocate: worker thread only. `chim_lyrics_new` returns null for no syllables. |
| `chim_set_lyrics(v, on)` | Lyrics mode on/off; also switches the engine limiter with it. Off drops the voice's pointer to its lyrics. Real-time safe. |
| `chim_note_on(v, l, i)`, `chim_note_off(v)` | Gate opened / closed in lyrics mode: sing syllable i, end it. Real-time safe. |
| `chim_is_dead`, `chim_free` | `chim_free` allocates/frees, so never call it on the audio thread. |
| `chim_velum_cap(v, vowel)` | Exported but not yet declared in the header (debug use). With the nasal fix it returns 1.40 at every vowel. |

Cargo profile: `lto`, `codegen-units=1`, `panic="unwind"` (required for `catch_unwind`).

**C++ side (`src/Phona.cpp`)**:
- **Threading**: a worker thread builds voices (`chim_new`) when the desired body (M/F) or sample rate
  differs from the built one, and hands them to the audio thread through atomics (`incoming`/`outgoing`).
  The audio thread takes a new voice only if it matches the current body and rate and `outgoing` is empty,
  so it never frees or allocates. A failed build retries with backoff. If the active voice's rate doesn't
  match the engine rate, the output is muted until a rebuilt voice arrives.
- **Control rate**: params go to `chim_set` every 16 samples. The engine itself updates every 64 output
  samples, so there is no audio-rate FM.
- **Gate**: unpatched means always on (in lyrics mode every gate edge sings a syllable, see "Lyrics mode"). Otherwise V/10 sets the breath level (0–1.0; the Rust layer accepts
  up to 1.5). The gate also opens and closes the external input (see below). Below 0.1 V the gate is closed, so a small DC offset cannot hold the voice open. A rising
  edge is held for 128 samples so short triggers sound.
- **Idle bypass**: after 1 s of silence with the gate closed, rendering stops (a silent voice otherwise
  costs full CPU).
- **Output**: `6.25 * sample` volts (+25% over the original 5 V). By default the engine limiter is off and
  the Rust layer guards the output with `soft_clip` (see EFFORT below), so the peak is 10 V. The vowel-mean
  level is held near -15 dBFS across pitch and body (see "Level correction").
- **Context menu / patch data**: the menu has the Lyrics text field and "Clear lyrics"; `dataToJson` saves only
  `"lyrics"` (see "Lyrics mode"). The "Output limiter (old level behaviour)" item and its `"limiter"`
  JSON key were removed at the user's request (the user heard no difference); the module never calls
  `chim_set_limiter`. The engine limiter is off (`chim_new`) except in lyrics mode (`chim_set_lyrics`). With FRY at 0 and notes at C3 or below, limiter on and off give sample-identical
  output; above that the only difference is level (`examples/limab.rs`).
- **CV**: each knob except FINE has a CV input (SOURCE since 2026-11-09). 10 V spans the knob's full range; CV is
  added to the knob and clamped to the knob's range. **Rule: every CV input has a CV amount trimmer**
  (attenuverter, −100..100%, default 100%) at the right of its jack. V/OCT, GATE and RESET are not modulation inputs and
  have none; FINE has no CV at all. The trimmers VOWEL, NASAL, TENSION, BRIGHT, BREATHY (`*_ATT_PARAM`) are the
  original five; GLIDE_ATT, VIBDEPTH_ATT, VIBRATE_ATT, LARYNX_ATT, ROUGH_ATT, FRY_ATT, EFFORT_ATT and SOURCE_ATT
  were appended after `SOURCE_PARAM` on 2026-11-09 (before `PARAMS_LEN`), so saved patches keep their ids and
  behave as before (new trimmers load at 100%). V/OCT is a standard 1 V/oct.
- **Fault handling**: the status light is green while sounding and red on a fault (dead voice or failed
  build). The audio thread checks `chim_is_dead` at control rate and sets `rebuildRequest`; the worker
  builds a replacement voice, at most once per second.
- **Panel layout** (redesigned 2026-11-10 to follow a mockup image from the user; the first four-band layout of
  2026-11-09 is replaced). `res/Phona.svg` is generated: run `python3 tools/panel.py` from `rack-plugin/`. Do not
  edit the SVG by hand. The geometry constants (`ROW0` 8.6, `ROW_H` 29.9, cell left edge 1.5 mm in all cards (drawings shelved, see below),
  card right edge 100.1) and the colours are duplicated in `tools/panel.py` and in `PhonaWidget` (`src/Phona.cpp`);
  change both together. Panel size 101.6 x 128.5 mm.
  - Look: a dark title bar with "PHONA"; four cards (PITCH, VOCAL TRACT, VOCAL FOLDS, BREATH / EXTERNAL / OUT), each
    with a header strip and a coloured tab with a line icon. The SVG is flat vector only (Rack's SVG reader has no
    text, blur or shadow); all text is drawn by the widget (`PanelLabel`, Nunito-Bold from Rack's system fonts).
    The painted shadows and glows and the small subtitles of the mockup were left out on purpose (the subtitles are
    unreadable at 100% zoom).
  - The head drawing (vocal tract card) and the vocal folds drawing are shelved (user request, 2026-11-10).
    `tools/panel.py` has `DRAWINGS = False`; the drawing code is kept under that flag. With it off, all cells start
    at 1.5 mm (`cellLeft` in `Phona.cpp` returns 1.5). To bring the drawings back, set `DRAWINGS = True`, run
    `python3 tools/panel.py`, and make `cellLeft` return 1.5 / 14.0 / 12.5 / 1.5 (by row) again.
  - Each knob cell is: label, knob with a `ValueArc` (amber arc from the minimum, or from the centre for a +/- range
    knob, to the knob setting), a `ValueDisplay` (short text of the knob's setting, or of the value in use (knob + CV, blue text) when the CV input has a cable; PITCH shows
    a note name, VOWEL shows the nearest vowel letter on a dark plate), then the CV jack (left) and the trimmer
    (right). Knobs are `RoundSmallBlackKnob` (`RoundBlackKnob` for PITCH and VOWEL) to make room for the display.
    A jack without a trimmer (V/OCT under PITCH) is centred. The panel screws were removed (user request).
  - Cells: PITCH card (5): PITCH, FINE (knob only), GLIDE, VIB DEPTH, VIB RATE. Vocal tract card (4): VOWEL, NASAL,
    LARYNX, BODY switch (labels F up, M down). Vocal folds card (5): TENSION, BRIGHT, BREATHY, ROUGH, FRY. Bottom
    card (5): EFFORT, GATE and RESET jacks (one cell), EXT IN jack, SOURCE (knob, CV, trimmer), OUT jack.
  - The `Cell` table in `PhonaWidget` repeats each param's min, max and default; these are used only when there is
    no module (module browser, screenshots). If a param range changes in the `Phona` constructor, change the `Cell`
    table too.
  - Modulation marks: `Phona::modShown[PARAMS_LEN]` (atomic floats) holds knob + CV for each param. `knobCv`
    writes it at control rate; PITCH is written in `process` as knob + 12 x V/OCT. `ValueArc` draws, only when the
    knob's CV input has a cable, a blue arc (`MOD_BLUE` #1f7ad9) from the knob setting to the value in use, with a
    dot at the live end. The value display follows the same value (`modShown`) and turns blue. Not visible in screenshots (needs a
    module); not yet checked in Rack.
  - `EFFORT_PARAM`/`EFFORT_INPUT`, then `SOURCE_PARAM`/`EXT_INPUT`, `SOURCE_INPUT` and the eight new trimmer params are
    appended at the end of their enums, so saved patches keep their ids.
  - Status: checked in screenshots only, not operated in Rack. Not committed (together with the SOURCE CV input and
    the all-trimmers layout).
- **External input**: the module always renders through `chim_render_ext` (input = EXT IN volts / 5). Every 16
  samples it calls `chim_set_ext` with the SOURCE value only if a cable is in EXT IN, otherwise 0, so SOURCE
  does nothing unpatched.
- **Grey-out of fold-only controls** (`DimOverlay`, added with `addDim`, added last so they draw over the
  controls): with a cable in EXT IN, a wash of the card body colour (#ece7dd, alpha 0.72 x grey-out amount) covers
  the controls that act only on the vocal folds: the whole pitch card (PITCH, FINE, GLIDE, VIB DEPTH, VIB RATE, with
  their CV jacks and trimmers), TENSION, BRIGHT, ROUGH, FRY, and the EFFORT cell (knob, CV, trimmer). BREATHY, the
  tract card (VOWEL, NASAL, LARYNX, BODY), GATE, EXT IN, SOURCE and OUT stay clear. The grey-out amount is
  `sourceShown` (SOURCE knob + SOURCE CV). The controls still work (at SOURCE 50% they act on the fold half).
  The SOURCE cell is greyed out the other way round (`whenUnpatched`): at full strength when no cable is in EXT IN.
  `addDim(module, row, x0, x1)` places a rectangle from x0 to x1, starting 4.2 mm under the card top, 25.2 mm high.
  Setting the environment variable `CHIMERA_DIM_PREVIEW=1` shows the overlays at full strength in the module
  browser and screenshot mode, to check them. Not yet seen by the user in Rack.

**Controls → engine**:

| knob | range | maps to |
|---|---|---|
| PITCH | ±36 semitones around C4, snaps to semitones | f0 = C4·2^((pitch+fine)/12+voct), engine clamps to 30–1500 Hz |
| FINE | ±100 cents | added to PITCH (no CV input) |
| GLIDE | 2 ms .. 1 s (exp) | source glide time constant |
| VOWEL | 0..4 | blend i→e→a→o→u postures |
| NASAL | 0..1 | velum −0.25..1.4; the per-vowel cap exists but is 1.40 everywhere (see "Nasal fix") |
| LARYNX | ±1.6 | offset added to the vowel's larynx height (Rust accepts ±3.2) |
| TENSION | −0.5..1.5 | source tension |
| BRIGHT | −0.5..1.5 | glottal closure sharpness, not a filter |
| BREATHY | 0..1.5 | abduction 0.1·b² + chink; level-compensated (see below) |
| ROUGH | 0..2 | jitter, shimmer, period doubling |
| FRY | 0..1 | vocal fry via `fry_map`: 0–50% exact pitch, pulse shape 0..0.85; 50–100% shape 0.85..1 and pitch falls to 48 Hz (`fry_pull`) |
| EFFORT | 25–200 %, default 100 % | lung pressure 8 × effort (2..16 engine units); 10 V CV = full range |
| VIB DEPTH / RATE | 0–200 cents / 0.5–12 Hz | source vibrato (full range of the Rust layer) |
| SOURCE | 0–100 %, default 100 % | folds .. external input share (`chim_set_ext`); acts only with a cable in EXT IN; CV input, 10 V = full range; the grey-out follows knob + CV (`sourceShown`) |
| BODY switch | M/F | rebuilds the voice on the worker thread |

**Fry** (`fry_map` in lib.rs) returns (pulse shape, pitch pull). The stock engine ties both to one number
and pulls the fold rate toward 48 Hz linearly in Hz (10% fry = -1.5 semitones at C4). The fork's `fry_pull`
separates them. Knob 0-50%: pitch stays exact and the shape runs 0..0.85, so the tuned zone has a clear
creak (before, the shape reached only 0.5 there and was very subtle). Knob 50-100%: shape 0.85..1 and the
pitch falls evenly in pitch to 48 Hz. No raised-f0 trick is sent any more, so the old 770 Hz limit and the
shallower vibrato are gone. Check with `cargo run --release --example fry`.

**EFFORT**: range 25-200 %, default 100 %, maps to lung pressure 8 x effort (2..16). The breathiness
compensation multiplies it and the product is capped at 20. `chim_new` sets `limiter = false` (off in plain mode;
lyrics mode turns it on); `chim_render` guards the output with `soft_clip`: linear to 1.0, soft knee
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
- *Not yet done*: the user has not listened to it in Rack. The input is held
  (zero-order) across the engine's 2x internal oversampling steps rather than interpolated. The female body
  at SOURCE 100 % is about 2 dB hotter than the male one.

**Nasal fix** (engine fork, `anatomy/human.rs`, flag `HumanVocalTract::velum_junction`; found with
`examples/velumgeo.rs`):
- *Cause*: the tract splits pharynx and mouth at a fixed fan ray (150 degrees), above the uvula. A lowered velum
  hangs below that ray, so pharynx rays above the uvula tip measure the gap between the velum's oral surface and
  the tongue. On back vowels the tongue meets the velum there, and the "pharynx" tube sealed 1-1.5 cm BELOW the
  nasal junction (vowel o at velum 0.9: areas 0 from s 7.5 to 9.4 cm with the junction at s 8.8). That cut off
  both mouth and nose: silence.
- *Fix*: for grid samples up to and including the junction sample whose ray passes above the foot of the port
  window (`lid0`, where the outer contour leaves the posterior wall), the area is floored at the port area
  (velum x k squared), fading in with the velum opening (smoothstep 0..0.25 of v_open) and over the first 2 mm
  above the window foot. The mouth still seals where the tongue meets the velum, so a fully nasal back vowel
  becomes an [ng]-like hum through the nose.
- *Tried first and rejected, do not retry*: moving the junction itself down to the uvula tip. It shortens the
  pharynx tube by up to 2 cm with a fixed number of sections, which breaks the network's stability limit:
  outputs blew up to full-scale clipping or fell silent, mostly on the female body.
- *Wrapper*: `chim_new` and `velum_table` set `velum_junction = true`. `velum_table` now counts only a seal
  below the junction as unsafe (`g.s >= f.pharynx_length` is allowed). With the fix the cap is 1.40 (no cap) at
  every vowel position on both bodies; the table stays as a safety net.
- *Results* (`examples/nasalscan.rs`, static; male C3 / female C4): no silence and no dead voice at any vowel
  position and NASAL setting. Front and open vowels change by under 3 dB across the NASAL range. Back vowels
  (vowel knob 2.5-4) at NASAL 75-100% are 3-9 dB quieter than at 0 (male -17 to -20.5 dBFS, female -19 to
  -22.4 dBFS, against about -13 to -16): the mouth is closed and the sound leaves through the nose. Live sweep
  (`examples/nasalsweep.rs`, NASAL 0 to 1 to 0, 9 vowel positions, both bodies): no dead voice, no clicks
  (largest sample-to-sample jump 0.11, normal waveform slope), peaks at most 0.60.
- *Tests*: engine suite 83 passed, 0 failed, no re-bless; wrapper 5 pass. `engine-fork.patch` regenerated.
- *Known limits*: the velum flap stops moving at velum 1.2 while the port area goes on to 1.4; the level
  correction does not include NASAL, so the hum on back vowels is quieter.
- *Not yet done*: the user has not listened to it in Rack.

FALSE FOLDS was removed (subtle effect); `ventricular` is sent as 0 and the Rust API still accepts it. Param and input ids changed on
2026-11-09, so patches saved with the older prototype load with wrong knob values.

## Key findings (measured)

- **CPU**: before the optimisation, ~12% (44.1k), 14% (48k), 16% (96k) of one M4 Max core per voice, and the
  user saw ~17–18% in Rack and considered it too high. Now about 9% (moving vowel) or 6% (held vowel) at 48 kHz
  in the bench; the user has not yet checked the Rack CPU meter. See "CPU optimisation". Per-sample calls cost
  the same as 128-sample blocks and give bit-identical output. FTZ has no effect on arm64.
- **No cheap eco mode**: oversample 1× at 48 kHz is unstable (NaN on female, garbage on male). The solver
  needs ~96 kHz internally.
- **Polyphony**: was about 4 voices at 12-16% each; at 6-9% per voice about 8 voices should fit (voice-count
  option recommended). Not tried in Rack.
- **Weak controls**: `ARTIC_SPECS` clamps articulators. (The engine's output limiter used to flatten level
  differences and made pressure a volume control; the fork now switches it off.) Round-3 audibility ranking
  (STALE: measured with the limiter on, needs re-measuring): fry, velum, tension,
  brightness, breathiness (mostly level), larynx strong. Epilarynx, root, dorsum, ttCurl, lateral weak
  or none. Ventricular is subtle.
- **Female body** doesn't sound very feminine (same in the upstream app). Round-2 renders explored
  alternatives; no fix applied. The M/F switch changes the body only, not the pitch. Above 4 kHz the female
  voice has about 7 dB less energy than the male voice at C3; this is a property of the fold model and is
  not corrected.

## CPU optimisation

**Rack's CPU meter against these numbers (2026-11-09).** After the optimised build the user saw 28.5% on
the module's meter and reported it as worse. Timing `Phona::process()` of the real `plugin.dylib` outside
Rack (a small C++ harness that loads the plugin against `libRack.dylib`, the user's patch settings, thread
CPU time) gave: old build 13.0%, new build 8.4% on a performance core; old 70%, new 44% on an efficiency
core (`taskpolicy -b`). The engine is about 5x slower on an efficiency core, and the meter measures elapsed
time, so its reading depends on which core macOS gives the audio thread (the machine was busy: a browser at
550% CPU, Spotlight indexing, Ableton Live). Compare old and new builds only under the same load. Flush-to-
zero (which Rack sets) made no difference.

The meter still read 28% with the other programs closed. Reproduced outside Rack by pacing the harness as
real-time audio (one 256-sample block every 5.33 ms, then sleep): old build about 36%, new build about 27%
(against 13.0% / 8.4% in a continuous loop). A light, bursty load runs about 3x slower per sample on this
Mac (macOS keeps the core slow or uses an efficiency core), and Rack's meter shows elapsed time. So: the
meter overstates the cost at light load, percentages do not add linearly as a patch grows, and in Rack
conditions the optimised build saves about 25%, not the 35-52% of the continuous benchmark.

Measured with `rust/examples/bench.rs`: per-sample calls as the module makes them (`chim_set` every 16 samples,
`chim_render_ext` one sample at a time), 48 kHz, male C3 and female C4. It reports process CPU time
(`getrusage`), not wall-clock time, because other programs on the machine made wall-clock results jump by 20%;
take the best of several runs. Args: seconds, sample rate, and `held` (vowel and NASAL stand still; the default
moves both all the time, which is the costly case). It also prints a hash of the output, to check that a change
is bit-identical.

Results (M4 Max, best of 8 runs of 6 s, machine under load from other programs, so compare the columns, not the
absolute numbers), % of one core, male / female:

| | moving vowel | held vowel |
|---|---|---|
| before | 14.3 / 13.9 | 13.5 / 12.7 |
| after | 8.7-9.5 / 8.7-9.5 | 6.5 / 6.1 |

About 35% less with a moving vowel and about 52% less with a held vowel. What was done, all in the engine fork:
- *a. `AirwayNetwork::step`* (airway/network.rs; was 78% of the time): the arrays are borrowed once per section
  as slices of known length, in sections 1 (geometry ramps), 4 (walls) and 5 (nodes), as section 3 (branches)
  already did. Indexing `self.x[k]` made the compiler reload each Vec's pointer and length and re-check bounds
  on every access. This alone took a voice from 14.0% to 9.3%. Bit-identical.
- *b. Cached node geometry in `step`*: a node whose (area, minimum, dx) did not change keeps its reciprocals and
  square root (`geo_a`, `geo_am`, `geo_d`). About 0.2 points. Bit-identical.
- *c. `project_on`* (anatomy/human.rs): `hypot` is skipped for segments whose squared distance is clearly beyond
  the best so far. About 0.3 points. Bit-identical.
- *d. `HumanAirway::commit`* (voice/human_airway.rs): the tract geometry (`tract.compute`, about a fifth of the
  CPU) is skipped when the articulators and the tract settings (`frame_key`) are the same as last time.
  Bit-identical on its own, but it never hit: the articulator springs creep toward their targets in the last
  bits for ever.
- *e. `ArticulatorBank::settle`* (control/articulators.rs; default off, the wrapper turns it on in `chim_new`):
  an articulator within 1e-7 of its target and slower than 1e-6 comes to rest exactly on it. This makes (d) hit
  after the vowel has been still for about a second. It changes the output in the last bits only (the hash of
  a held note differs), hence a switch; the engine's own tests do not see it.

Tried and did NOT help (each kept the output bit-identical; do not retry): (i) a branch-free form of the branch
flow solve u = 2c/(bq + sqrt(bq^2 + 4k|c|)) in place of the four-way if/else: no change in time; (ii) gathering
the divisions and square roots of each section into plain array loops so they run as two-lane vector
operations: slower (8.8% to 9.1%), tried once before and once after (a); (iii) caching T/C per node. After (a)
the divider is not the limit; the per-branch arithmetic is what is left. How the cause was found: a line-level
profile taken by stopping the process under lldb several hundred times (the `sample` tool only gave function
totals); half of `step`'s samples were on Vec indexing.

Verification: with the default moving-vowel bench the output hash is the same before and after a-d
(a9457588a8c5b9a3 male, d57efc79ca52fdcd female). Engine suite 83 passed, 0 failed, no re-bless. Wrapper tests
5 pass. nasalsweep: no dead voice in 18 cases; nasalscan: no silence or dead voice; scan: no blow-up; levelmap
numbers unchanged.

Where the time goes now (profile after a): `step` about 71%, tract geometry about 24% (moving vowel), the rest
glottis and render. Remaining ideas, not done: the engine still needs 2x internal oversampling at 48 kHz (1x is
unstable); non-bit-identical arithmetic changes (reciprocal multiplies) would need a re-bless of the golden
tests; the geometry runs every 64 output samples and could run less often while articulators move slowly.

Not yet done: the user has not checked the CPU meter in Rack.

## Known issues

1. **Velum on back vowels (fixed in the fork, not upstream)**. The stock engine seals the tract below the
   nasal port on o/u when the velum is lowered (see "Nasal fix"). The fork's `velum_junction` flag fixes it,
   so NASAL works on every vowel (a back vowel at high NASAL is a quieter [ng]-like hum). `velum_table` is
   kept as a safety net and its cap is 1.40 everywhere. Worth reporting upstream.
2. **Panic path isn't RT-safe**: `catch_unwind` handles a panic, but unwinding may allocate. Acceptable for
   a prototype.
3. **Panel**: redesigned on 2026-11-10 into four cards with value displays and arcs (see "Panel layout"); checked in screenshots only, not operated in Rack by the user.
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
- Run `examples/scan` (stability) and `examples/nasal` (silence) after any change to ranges or mappings;
  `examples/nasalscan` and `examples/nasalsweep` after any change to the velum or NASAL mapping.

## Open work (user hasn't picked next)

00. Not yet heard or seen by the user: the nasal fix (back vowels at high NASAL), the grey-out of fold-only
   controls with a cable in EXT IN, the redesigned panel (cards, value displays, trimmers on every CV input), and the CPU meter in
   Rack after the optimisation. The panel change is uncommitted (outer repo).

000. Lyrics mode (2026-11-10): the user has not listened to it. Open: the `LYR_MAKEUP` and limiter levels (set
   from measurements only), an on-panel text editor, greying out VOWEL in lyrics mode, CV selection of the
   syllable, other languages, the late vowel (see "Lyrics mode"). Not committed.

0. Pending from the level-correction round: the user's listening verdict on the pitch, body and vowel level
   correction (vowel-dependent level is now corrected); the round-3 control ranking was measured with the
   limiter on and is stale.

1. **Engine fork** (started; limiter switch, `fry_pull`, `ext_in`, `fold_scale` and the velum fix done). Still open:
   wider articulator clamps, further CPU work (`step` done once, see "CPU optimisation"), velum flap range (stops at 1.2,
   port goes to 1.4), NASAL in the level correction.
2. External input (EXT IN + SOURCE, done offline, see "External input (talk box)"). Still open: listening test
   in Rack, interpolating the input across the oversampling steps, the female level.
   Smaller ideas: "snap to vowel" menu option, fade on body swap, M/F/child switch.
3. Polyphony, probably up to ~8 voices now (was ~4), with a voice-count option; check the Rack CPU meter first.
4. Beatbox module (the engine has `Engine::beat_hit`).
5. Better female voicing defaults.
6. Panel: the 2026-11-10 redesign is in; open: check it in Rack, artwork polish.
7. Cross-platform builds and Library binary submission; reply to Andrew for the submission details.
