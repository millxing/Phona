# Phona overnight progress — 2026-11-13

## Outcome

Both Astra tasks were completed, reviewed, and integrated on `lyrics`. A local beatbox prototype
is packaged and placed in Rack's plugin directory for the next restart. Rack was not restarted,
no audio device was opened, and no sound was played. The existing patch and preferences were not changed.
No dependencies were installed and nothing was pushed or published.

The objective was a live instrument prototype and repeatable Rack checks, not a release-quality
beatbox engine. The beat mode is functional, but its mouth-preparation delay is a material limit.

## Start here in the morning

1. Save any current Rack work, then restart Rack to load the pending plugin package.
2. Open `../tests/patches/beatbox-triggers.vcv` as a separate patch. It starts stopped, with no Audio
   module and zero VCA levels. Press RESET then RUN to inspect token advance without sound.
3. Phona's menu selects Beatbox mode. Text is `B t Pf t B . Pf t`: kick, closed hat, snare, hat,
   kick, rest, snare, hat. Every rising gate advances one token; RESET returns to the start.
4. The singing fixtures are `singing-triggers.vcv` and `singing-held.vcv` in the same folder.
   Follow [the short checklist](live-instrument-test.md) when ready to listen.

Only BODY, GATE, RESET and OUT affect this first beatbox prototype. Other controls are dimmed.
Unknown tokens consume one silent step and show `?`; empty text is silent. There is no internal
clock, sample playback, tempo prediction, or preparation-delay compensation. Old patches load
in Lyrics mode because the new `beatbox` field defaults to false.

## Verified

- Plugin built successfully for macOS ARM. Existing SDK deployment-target warnings remain.
- Six Rust tests passed, including zero allocation/free checks and preservation of beat gestures
  under singing-control updates. No engine source changes were required.
- Real-module C++ beatbox regression passed at 44.1, 48 and 96 kHz: token order, looping,
  one-sample and held gates, multiple edges in the reset window, reset offsets, body replacement,
  text clearing, mode switching, patch recall, legacy JSON, and Initialize.
- All three fixture schemas and generated patch files passed validation against the combined build.
- Offline host checks use the real Phona and Fundamental module binaries. They cover saved state,
  pitch/gate sequences, finite/bounded audio, fault lights, muted VCA output, reset/retrigger cases,
  body change, CV, external excitation, and gate-close silence. The final packaged binary passed
  the same checks, not just the unstripped development binary.
- The beat fixture produced all 68 expected edges over its test interval. The test includes a small
  end margin for SEQ-3's floating-point clock rounding. A late reset cancels the preparing hit;
  the test verifies that the next gate sounds instead of demanding audio from the cancelled hit.
- Eight beatbox WAVs rendered: B/Pf/t and a pattern for each body. They are non-silent, unclipped
  mono 48 kHz PCM16, with silent final 250 ms. No auditory judgment is claimed.
- Package contains both required licenses, has anonymous archive headers, and passes the private
  build-path scan. Its binary matches the package placed in Rack's plugin directory.
- `git diff --check` passed. The engine fork remains clean.

## Files and build identity

- Runtime: `src/Phona.cpp`, `rust/src/lib.rs`, `rust/chimera_rack.h`.
- Regression and fixtures: `tests/beatbox.cpp`, `tests/beatbox.sh`, `tests/patches/`.
- Render tool: `rust/examples/beatbox.rs`.
- Measurements and WAVs: `../../audition/beatbox-prototype/` (WAVs ignored by git).
- Technical behavior and measured latency: [beatbox-prototype.md](beatbox-prototype.md).
- Package: `../share/Phona-2.1.0-mac-arm64.vcvplugin` (local prototype, not a new public release).
- Packaged binary SHA-256: `138ec7fa4418d5235cb52c803a5e24f2af0adf3b87b009c1726342de9f294436`.
- Rollback: `../share/rollback/Phona-before-beatbox-mac-arm64.vcvplugin` contains the previously
  installed plugin. To restore, quit Rack, copy this archive to its `plugins-mac-arm64` directory,
  remove any still-pending new Phona archive there, and restart Rack.

Integrated agent commits: `8c68f44` (test fixtures), `de7a377` (beat mode). The parent also added
the combined beatbox fixture, multi-rate regression, final-package checks and current handoff notes.

## Limits and decisions

The main hit is delayed by mouth preparation; delay differs by drum sound. The measured earliest
preparation noise is not a useful estimate of the kick's musical onset. See the timing document.
This may be unsuitable for direct finger drumming or tight rhythmic layers without further work.
It should not be described as a finished drum machine.

Reset/rest/edit cancels and mutes the previous hit abruptly, which can click. Very fast retriggers
can cancel a hit before its main burst. Sound and timing depend on the previous mouth state.
Male/female levels differ, and beat mode currently renders while idle. We did not change these
behaviors without a listening target or add hidden timing prediction.

Not verified: Rack GUI layout/operation, auditory quality, absence of cancellation clicks, live Rack
CPU, exact acoustic latency outside 48 kHz, or platforms other than macOS ARM. Offline harnesses
are not a full Rack Engine/GUI test. No new female-tone settings were adopted.

## Next steps

The remaining useful step is a short Rack check with the prepared fixtures. First establish whether
these three beat sounds are musically useful despite their preparation delay. If useful, improve
cancellation transitions and consider a clearly defined timing policy before adding more sounds.
Keep the singing fixtures as a regression check for any later beat work.
