# Phona: five-minute live instrument check

Open `../tests/patches/singing-triggers.vcv` as a separate patch. Save your current
work first. The fixture contains **no Audio module**, the sequencer is stopped,
and both VCA levels are zero. No device settings are saved. Opening the supplied
file cannot send sound to a hardware output. Do not merge it into a live patch.

Only Phona, Core Notes, and installed VCV Free (Fundamental) modules are used.
There is one Phona voice. The singing fixtures do not require Beatbox or Phona Lab.
A third fixture, `beatbox-triggers.vcv`, starts in Beatbox mode with `B t Pf t B . Pf t`,
four triggers per second, stopped transport, and zero VCA gain. Its pitch and modulation
cables are omitted because those controls do not affect this prototype. The parked LFO
and VCO are unused. See `beatbox-prototype.md` for timing and cancellation limits.

## Patch map

Left to right: SEQ-3, Phona, LFO, VCO, VCA. Core Notes sits below them.

| Cable/control | Purpose |
| --- | --- |
| SEQ-3 CV 1 → V/OCT | C3 D3 E3 F3 G3 A3 B3 C4; 1 step/second |
| TRIG → GATE | Short gates; one lyric syllable per rising edge |
| SEQ-3 RESET output → RESET | Restart lyric position with the sequencer |
| LFO sine → VOWEL and NASAL | Bipolar slow CV, 8-second period; Phona amounts start at zero |
| Phona OUT → VCA channel 1 | Output level control; starts at zero |
| Unconnected VCO SAW | C3 source for the optional talkbox check |

SEQ-3 starts at step 1 but its first clock advances to step 2. After RESET then
RUN, the first trigger is therefore **D3**, followed by E F G A B C4 C3. This is
verified behavior of installed SEQ-3, not a Phona pitch error. Lyrics start with
“doh” on that first trigger. The phrase is `doh ray mee fah soh lah tee doh`.

The second fixture, `singing-held.vcv`, replaces TRIG with CV 2. Steps 2–3 hold
10 V across D→E; steps 5–7 hold 10 V across G→A→B. Steps 1, 4, and 8 are zero.
Thus pitch moves while one syllable stays held. Both fixtures start with GATE low.
Stopping SEQ-3 can retain a high CV 2 value. To close that gate, turn the current
CV 2 step to zero; do not unplug GATE (unpatched means always on).

## Short acceptance checklist

Do the visual checks first. One brief listening session is sufficient; none was
performed for this change. Check a box only after observing the result.

- [ ] **Recall and triggers:** Open the trigger fixture. Confirm male BODY,
  saved lyrics, GLIDE about 60 ms, and both CV amounts at zero. Press RESET,
  then RUN. The displayed syllables advance once per trigger and wrap without
  an extra syllable. Press RESET while running: the next gate starts “doh”.
- [ ] **Held notes and slides:** Open the held fixture. Press RESET then RUN.
  The syllable stays the same across D→E and G→A→B; each low-to-high gate
  advances it once. Increase GLIDE to about 200 ms. Pitch must slide without
  a new lyric onset. Restore about 60 ms.
- [ ] **Body and CV:** Change BODY M→F→M while a note is held. Allow a moment
  for each rebuild. Pass: no red fault light or permanent silence. Clear lyrics
  in the menu. Raise VOWEL CV amount to +50%, then −50%, then zero: the blue
  value/arc moves, reverses, then stops. Try NASAL at a knob value of 50% and
  amount +25%; output should continue across the sweep. Restore the fixture
  by reopening it. VOWEL is intentionally ignored while lyrics are active.
- [ ] **Quiet listening, only when ready:** Keep the system/interface volume
  low. Add Core Audio-2, select the intended device, and connect VCA channel 1
  OUT to both audio inputs. Start with VCA at zero; raise slowly toward 0.05
  (5%). Stop at a comfortable level. Pass: clear retriggers, sustained vowels,
  audible pitch slides, and no severe clicks, bursts, or stuck notes. The VCA
  is attenuation, not a limiter. No device gain is assumed safe.
- [ ] **Optional talkbox:** Clear lyrics. Connect VCO SAW to EXT IN; SOURCE
  is saved at 100%. Use the held fixture. Sweep VOWEL, NASAL, and BODY.
  Pass: tract changes shape the sound, gate-low intervals become quiet, and
  SOURCE at 0% returns to folds. Fold-only controls dim at 100% external.
- [ ] **Save a copy and reopen:** Stop, set VCA to zero, and remove Audio-2
  before saving a test copy. Change BODY, lyrics, and a CV amount first, then
  verify those values return on reopen. Runtime voice state, current lyric
  position, and oscillator phase are not promised to resume. RESET establishes
  a new phrase. Record Rack CPU with sample rate, buffer, and other app load.

## Repeatable offline checks

From the repository root, use the existing SDK and installed modules:

```sh
RACK_SDK=/Users/robschoen/Dropbox/CC/Chimera/sdk/Rack-SDK \
RACK_PLUGINS="$HOME/Library/Application Support/Rack2/plugins-mac-arm64" \
  rack-plugin/tests/patches/check.sh
```

`PHONA_DIR=/path/to/built/rack-plugin` can select a parent's build without
installing it. The directory must contain `plugin.dylib` and `plugin.json`.
The script builds two small C++ executables in a temporary directory and removes
them. It never opens Rack, an audio device, or the user's patch/preferences.
It requires macOS, clang++, Python 3, and an existing Rack SDK. No downloads.

`host_probe.cpp` reads actual model parameters, port names, and default JSON
from the plugin binaries and Core Notes in libRack. `schema.json` is that captured
schema, not inferred numeric IDs. The script fails on schema drift. Inspect the
change before replacing the snapshot; do not blindly rebaseline it.

`generate.py` deterministically writes the three plain-JSON Rack 2 patches.
`generate.py --check` checks exact fixture content, parameter ranges, cable
endpoints, duplicate input connections, stopped transport, and zero VCA gain.

`host_smoke.cpp` loads the fixture modules with Rack's `Module::fromJson`, checks
saved parameters/data, and runs their real `process()` functions at 48 kHz.
It transfers cable values with a one-sample delay. It checks pitch/gate order,
finite output within ±10.01 V, absence of the red fault light, zero VCA output,
one-sample gates with reset offsets 0/2/30/60 samples, body change, slow CV,
external excitation, and gate-close decay. It does not play or write audio.

## Integration checks (2026-11-13)

The parent integrated the beatbox prototype and updated the captured schema for the single
new default JSON field `beatbox: false`. Parameter and input IDs did not change. Both singing
fixtures now explicitly save that field; legacy patches without it are tested by `tests/beatbox.sh`.
The host test also checks the beatbox fixture. At 4 Hz the installed SEQ-3 has a 12001-sample
clock period at 48 kHz, so this fixture runs 17.02 seconds to include the 68th edge. Its clock
period must remain within 0.1% of 12000 samples. This avoids a false failure at an exact window edge.
A beat reset 60 samples late deliberately cancels the preparing hit; the next gate must sound.
Use `PHONA_PATCHES=beatbox-triggers` to run only that fixture's host smoke (all schemas and
fixture files are still checked). Singing regression coverage remains unchanged.

## Original agent evidence and limits (2026-11-13)

**Verified:** SDK 2.6.6, Fundamental 2.6.4, installed Phona 2.1.0. Installed Phona
binary SHA-256:
`56583a29e17eb1c6dec3f9f783d993e28258a0eadc712f2ff1f232a9e886801a`.
Schema matches the current Phona enums and `dataToJson` (lyrics only). Both
fixtures pass module-state recall and signal checks. Trigger run: 17 gate edges
in 17 seconds, peak about 4.62 V. Held run: 5 edges, peak about 4.49 V.
The external closed-gate final-second peak is zero in both runs. Measurements
are offline volts before the zero-level VCA; they are not speaker levels.
Worker scheduling and vocal noise can change exact peak values between runs.

**Not verified:** full Rack GUI patch loading/layout, listening quality, lyric
identity at each reset offset, click-free body swaps, exact slide time, audible
CV strength, live CPU, or other sample rates/platforms. The host smoke is not a
full Rack Engine/GUI integration test. Body/CV checks establish continued finite
output, not perceptual correctness. Reset offset tests establish safe non-silent
retriggers only. Source delays lyric onset by about 1 ms: 0/2/30 samples fall
inside this window at 48 kHz; 60 samples fall outside it. Do not treat the 60-sample
case as a promise of reset/gate alignment. Confirm lyric order manually or with
a future source-level test hook; the installed model ABI does not expose it.

**Assumptions:** the intended parent build retains the captured schema; the user
has the tested Fundamental modules installed. Run the checks against the parent
build after integration. No plugin DSP/UI, engine, upstream, installation,
preferences, or current Rack patch was changed.

Existing `spike/rack-bench` measures engine CPU, `spike/nasal-diag` inspects tract
geometry, and `spike/audition-tool` renders audio. Existing `rack-plugin/tools`
contains panel generators and a word judge. None provides patch-schema or
installed-module recall checks. This fixture harness fills that narrow gap.
