# Chimera → VCV Rack: discussion and implementation handoff

Date: November 9, 2026

## User intent and current status

Explore using [jamesondh/chimera-vocal-synth](https://github.com/jamesondh/chimera-vocal-synth) as the basis for an interesting VCV Rack module.

We discussed possible instruments, licensing, and native integration feasibility. No code has been cloned, built, modified, benchmarked, or auditioned locally. No module design has been approved as a final specification. The current workspace was empty before this handoff was created.

The assessment below comes from reading the upstream source and documentation. Upstream performance and quality measurements have not been independently reproduced. Pin a commit and recheck the current source before implementation; this is a young, changing project.

## What Chimera provides

Chimera is a physical-modeling vocal synthesizer. Sound comes from a simulated airway and vocal source rather than recorded voice samples. The model includes articulation, a branched vocal tract, nasal coupling, airflow, pressure, and a parametric glottis. Its acoustic solver uses a branched finite-difference time-domain (FDTD) tube network.

The project supports human speech, singing, beatboxing, and several animal voices. Its browser application combines React, canvas/Three.js visualizations, and a Rust engine compiled to WebAssembly for an AudioWorklet. The same Rust engine runs natively for tests and a command-line renderer.

Important source locations:

- `crates/chimera/`: native DSP, anatomy, gestures, phonetics, and engine library.
- `crates/chimera/src/engine.rs`: instrument control, rendering, body/creature changes, and beat sequencer.
- `crates/chimera/src/voice/human_voice.rs`: human voice rendering and source/articulation controls.
- `crates/chimera-wasm/`: browser-oriented C ABI; useful reference, not a ready-made native Rack interface.
- `crates/chimera-cli/`: offline rendering and measurement tools.
- `src/worklet/processor.ts`: browser audio-thread wrapper.
- `crates/chimera/tests/`: acoustic, behavioral, allocation, golden-output, and block-size tests.

Main references:

- [README](https://github.com/jamesondh/chimera-vocal-synth/blob/main/README.md)
- [Acoustic architecture](https://github.com/jamesondh/chimera-vocal-synth/blob/main/docs/architecture.md)
- [Engine contract and reported performance](https://github.com/jamesondh/chimera-vocal-synth/blob/main/docs/rust-engine.md)
- [Upstream agent instructions](https://github.com/jamesondh/chimera-vocal-synth/blob/main/AGENTS.md)

Some architecture documentation describes future work. Treat implementation and the documentation's shipped-status sections as the stronger evidence.

## Musical directions discussed

### 1. Voltage-controlled vocal anatomy

A monophonic instrument with 1 V/oct pitch, breath, tongue X/Y, jaw, lip rounding, nasal coupling, and selected source controls such as roughness or breathiness. An animated tract could show how the patch moves the mouth.

The appeal is interaction between physical controls: sequence tongue position independently of pitch, open the jaw with an envelope, and vary breath to move between voiced and noisy sounds.

### 2. Clocked articulatory gestures

A bank of vowels, consonants, and beatbox gestures, selected by CV and triggered or advanced by a clock. Controls could include duration, transition speed, accent, and pitch.

Potential results include whispered rhythms, vocal basslines, stuttering syllables, and patterns between speech and percussion. This was considered the strongest musical direction in the discussion, but it remains a recommendation.

Consonants need time-dependent gestures. A plosive requires closure and release; a static mouth preset does not reproduce the full event.

### 3. External excitation through the tract

An audio input would let other Rack oscillators or noise sources excite the modeled airway while CV controls articulation.

This is a later research direction. External excitation has not been verified as an existing engine feature. The source/airway coupling makes this more involved than attaching an input to a conventional filter.

## Suggested first module scope

A recommendation, not a finalized user requirement:

- One human voice, monophonic.
- Pitch and breath inputs.
- Tongue X/Y, jaw, lips, and nasal controls with CV modulation.
- A small bank of approximately 8–16 useful articulations, with select CV and trigger.
- Transition-time control.
- Audio output; potentially a breath/activity CV output if a useful signal can be exposed.
- Body type selected as a preset initially.

Defer text entry, animal modes, polyphony, external excitation, and continuous body morphing until the basic instrument sounds compelling and performs acceptably.

## Licensing findings

### Chimera

The repository and core crate declare the MIT license. Its copyright notice names Jameson Hodge, 2026.

MIT permits use, modification, distribution, sublicensing, and sale, including use in a closed-source module. Retain the copyright and permission notice with copies or substantial portions of the software. Chimera's MIT license does not require publishing modifications or paying its author.

Source: [LICENSE](https://github.com/jamesondh/chimera-vocal-synth/blob/main/LICENSE).

### Third-party material

CMUdict has its own permissive notice, including requirements for source and binary redistribution. Preserve it if distributing the dictionary or relevant derived material. Other dependencies and any reused fonts/assets need their applicable notices too. We did not complete a full transitive dependency or asset license audit.

Source: [CMUdict notice](https://github.com/jamesondh/chimera-vocal-synth/blob/main/public/data/CMUDICT-LICENSE.txt).

Upstream also distinguishes evaluation datasets from redistributable application content. Do not assume reference recordings or generated demo songs are covered by the code's MIT license; check provenance before packaging them.

### VCV Rack

Rack's licensing is separate from Chimera's:

- Rack's non-commercial plugin exception allows free plugins under other terms, including MIT or closed-source freeware.
- A commercially sold plugin under non-GPL terms requires a commercial agreement with VCV.
- GPLv3-compatible distribution is another route; do not assume every paid plugin requires a proprietary commercial agreement.
- Significant copied Rack code and Rack/component artwork can introduce additional requirements.
- VCV Library distribution has branding/panel-copying guidelines. Do not assume MIT permission for the engine also permits copying another product's branding or panel.

Recheck the applicable terms before release: [VCV plugin licensing](https://vcvrack.com/manual/PluginLicensing).

## Native integration approach

Keep the DSP in Rust. Build a standard C++ Rack plugin around a small native C-compatible interface, linking a Rust `staticlib` into the plugin binary. Users would not need Rust installed, a browser, or a WebAssembly runtime.

This avoids a full C++ DSP rewrite. The existing WebAssembly C ABI is a reference for operations and data layout, but a native wrapper must handle native pointers, ownership, platform builds, and error behavior explicitly.

Useful references:

- [Rust native library linkage](https://doc.rust-lang.org/reference/linkage.html)
- [Rack plugin tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial)
- [Rack API guide](https://vcvrack.com/manual/PluginGuide)

Rack calls a module's processing method per sample. Chimera accepts output slices and has a test verifying block-size-independent output, including single-sample rendering. Start by measuring a minimal single-sample interface to preserve responsive pitch, gates, and feedback patching. Only introduce buffering if measurements justify it, with explicit treatment of latency and incoming CV/event timing.

Slow articulation targets can potentially update at a reduced control rate. Audio rendering remains continuous. Do not assume audio-rate FM or audio-rate mouth modulation is already supported: the voice has internal control-rate updates and smoothing that need to be measured and understood.

## Performance evidence and limits

The upstream engine document reports native male-voice rendering at 2× oversampling consuming approximately 19–21% of one core on a Ryzen 7 8745HS. That is a particular upstream benchmark, not a measured Rack CPU figure or a guarantee for another machine.

The relevant benchmark is `crates/chimera/examples/bench.rs`, invoked upstream with:

```sh
cargo run --release -p chimera --example bench
```

It renders a speech fixture at 48 kHz in 128-sample blocks. Its reported averages do not establish worst-case callback timing, single-sample wrapper overhead, performance under continuous CV changes, or multiple-instance behavior in Rack.

Conclusion: a monophonic module appears technically credible. Lightweight polyphony and many simultaneous instances are unproven. The physical solver is likely the principal cost; native integration alone does not make it cheap.

## Real-time and correctness considerations

- Upstream promises allocation-free steady-state rendering and several direct control operations, backed by tests.
- Initial construction, body changes, and creature changes allocate and rebuild state. Prepare replacement state outside audio processing and arrange safe transfer and disposal.
- Text/gesture planners allocate. Precompute gesture banks or plan off the audio thread. The documented live note planner also allocates.
- Continuous body morph CV is not ready as-is: the current body setter rebuilds the airway. This is different from ordinary tongue/jaw/lip modulation.
- Verify sample-rate behavior at relevant Rack rates, including rate changes. Existing tests do not prove every Rack configuration works correctly.
- Upstream documents x86-64 native/WASM identity; aarch64/macOS identity was listed as untested. Test Apple Silicon directly if targeting it.
- The engine's determinism contract assumes no flush-to-zero/denormals-are-zero. Check Rack's actual floating-point environment and assess sound/stability implications; compatibility has not been established.
- Keep panics and invalid pointers from crossing the C boundary. Upstream release settings use `panic = "abort"`, so invalid-input handling matters in a native host.
- Reduce or omit unnecessary telemetry work only after profiling; keep UI drawing and expensive analysis away from audio processing.
- Do not lower oversampling, change numerical precision, or simplify the solver without listening and stability tests. These changes can alter both sound and physical-model behavior.

## Sound-quality expectations

The initial assessment was that Chimera may be more compelling as an expressive synthetic voice and unusual percussion source than as a realistic singer or speech system.

The upstream speech evaluation reports approximately 37% Whisper word error rate on synthesized sentences, with issues involving sibilants, timing, and spectral balance. This is a documented evaluation result, not a current independent listening test or a measure of musical usefulness.

Source: [Speech evaluation](https://github.com/jamesondh/chimera-vocal-synth/blob/main/docs/speech-comparison.md).

## Recommended next investigation

1. Clone and pin an upstream commit; read its current instructions and licenses.
2. Build and run the native tests and benchmark on the target development machine.
3. Audition a few rendered vowels, speech gestures, and beatbox sounds.
4. Build a minimal native Rack prototype with pitch, breath, and tongue controls.
5. Measure actual Rack CPU, worst-case processing time, modulation response, sample-rate handling, and multiple-instance behavior.
6. Decide whether to retain the full model, profile and optimize it, or reduce scope before building a polished panel.

The first useful deliverable is evidence that a small set of patchable controls sounds good and leaves enough CPU headroom. A complete browser-feature port is not needed to answer that question.

## Collaboration preference

The user asks for candid judgment and pushback when appropriate. Avoid unsupported optimism. Exclamation points should be interpreted as frustration with repeated failures and a signal to reconsider the approach rather than repeat it.
