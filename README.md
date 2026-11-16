# Phona

A physical-model singing voice for VCV Rack 2. Phona simulates the vocal folds and the vocal tract
in real time. It sings vowels, sings words from text, and can filter an external sound through the
tract (talk box).

The voice is the [Chimera vocal synthesis engine](https://github.com/jamesondh/chimera-vocal-synth)
by Jameson Hodge (MIT). Phona puts the engine in a Rack module.

**Status: prototype.** It works, but it is not in the VCV Library. The Mac build is signed and
notarized. The Windows and Linux builds are test builds: they pass the automatic tests, but nobody
has played them on a real PC yet.

## Download and install

1. Download the newest `.vcvplugin` file for your system from [Releases](https://github.com/millxing/Phona/releases).
2. Close Rack. Remove older Phona `.vcvplugin` files from the plugins folder, then put the new file there:
   - Mac (Apple Silicon): `~/Library/Application Support/Rack2/plugins-mac-arm64/`
   - Windows: `%LOCALAPPDATA%\Rack2\plugins-win-x64\`
   - Linux: `~/.local/share/Rack2/plugins-lin-x64/`
3. Start Rack.

Requirements: VCV Rack 2 and one of these systems:
- macOS 11 or later on Apple Silicon. There is no Intel Mac build.
- Windows 10 or 11, 64-bit.
- Linux x64 with glibc 2.30 or newer (for example Ubuntu 20.04+).

Windows and Linux testers: see [the tester guide](rack-plugin/docs/testing-windows-linux.md).

## Modules

| Module | Width | What it does |
|---|---|---|
| **Phona** | 20 HP | The voice. |
| **Phona View** | 14 HP | Animated mouth and throat. |
| **Phona Scope** | 20 HP | Instrument display: spectrum, formants, tract, vocal folds, vowel chart. |
| **Phona Script** | 14 HP | 8 lyric phrases. A trigger or CV chooses the phrase. |
| **Phona Lab** | 16 HP | Tuning tool for the sung consonants. |

<img src="rack-plugin/docs/images/phona.png" width="300" alt="Phona panel">

### Phona

- **Pitch:** PITCH, FINE, GLIDE, vibrato depth and rate. V/OCT input.
- **Vocal tract:** VOWEL, NASAL, LARYNX height, BODY (male or female).
- **Vocal folds:** TENSION, BRIGHT, BREATHY, ROUGH, FRY.
- **Breath:** EFFORT and GATE. With no cable in GATE, the voice sings all the time.
- **EXT IN and SOURCE:** send an external sound into the tract. SOURCE sets the mix between the folds and the external sound.
- Most knobs have a CV input with an attenuator.

**Lyrics.** Right-click the module and type text in the Lyrics field. Each gate sings the next
syllable: the consonants at the start, then the vowel while the gate is high, then the end of the
syllable when the gate closes. RESET goes back to the first syllable. A hyphen splits a word into
syllables. English only (CMU Pronouncing Dictionary).

**Polyphony.** Right-click > Polyphony (max voices): 1 to 4. Polyphonic V/OCT or GATE cables set how
many voices sound. OUT is polyphonic, with one channel for each voice. In lyrics mode, notes that
start together sing the same syllable.

**Quality.** Right-click > Quality: **Full** or **Eco (less CPU)**. At 44.1 and 48 kHz Eco uses about a third of the CPU, with a slightly brighter sound. At 88.2 kHz and above both are the same.

**Beatbox (experimental).** Right-click > Lyrics/Beatbox. The text field then takes the symbols
`B` (kick), `Pf` (snare), `t` (closed hat) and `.` (rest). Each gate plays the next symbol. Beatbox mode is mono
and not finished.

### Phona View and Phona Scope

<img src="rack-plugin/docs/images/phona-view.png" width="280" alt="Phona View"> <img src="rack-plugin/docs/images/phona-scope.png" width="380" alt="Phona Scope">

Put them next to Phona. They have no cables. You can put them on either side, and Phona Lab, View,
Scope and Script can be in a chain in any order. You can use more than one View or Scope, for
example one View for each voice. A chain can have up to 8 expanders on each side of Phona. The link
light comes on when the module finds a Phona.

- **Phona View** shows a side view of the head: the tract shape, the air pressure in the tract,
  airflow, noise at the place where it starts, the nasal passage when it is open, and the vocal folds
  seen from above. The top line shows the voice, the syllable and the phoneme.
- **Phona Scope** shows:
  - pitch, note, lung pressure, level, voicing, open quotient, nasal port and the narrowest point of the tract
  - the output spectrum with the tract's frequency response and formants F1 to F4
  - the tract as a tube from the glottis to the lips
  - the glottal area and airflow
  - an F1/F2 vowel chart with average human vowels (Hillenbrand 1995) for the current body

  The formants come from a simple tube model of the tract shape. They are not measured from the engine's own filter.
- The selector at the bottom chooses the voice: **F** follows the newest note, **1 to 4** show that voice.
- Beatbox mode is not shown yet.

### Phona Script

Put it next to Phona (or in the chain of Phona expanders). It holds 8 phrases. While it is attached,
its phrases replace the text in Phona's Lyrics field.

- **Enter the phrases:** right-click > Phrase 1 to 8. Empty phrases are skipped. Up to 255 characters each.
- Phona sings the current phrase, one syllable for each gate, and **repeats it** until you choose another phrase.
- A new phrase starts at the **next gate**. The syllable that sings now finishes first.
- **NEXT** (trigger): the next phrase. After the last phrase, phrase 1.
- **RESET** (trigger): phrase 1, from its first syllable.
- **SELECT** (CV): 0 to 10 V chooses phrase 1 to 8 (1.25 V for each phrase). A voltage for an empty phrase has no effect.
- **Click** a phrase on the panel to choose it.
- Phona's own RESET starts the current phrase again from its first syllable.
- In Beatbox mode, each phrase is a beat pattern (`B t Pf .`).

The phrase that sings is lit. The phrase that comes at the next gate has an amber frame.

### Phona Lab

A tuning tool for the consonants in lyrics mode. Put it next to Phona, adjust the knobs, and copy the values.

## Build from source

The plugin is C++ for Rack and Rust for the engine. You need the Rack SDK 2.6.6, a Rust toolchain and `zstd`.

```sh
git clone https://github.com/millxing/Phona && cd Phona
git clone https://github.com/jamesondh/chimera-vocal-synth engine && git -C engine checkout 092b16ce
git -C engine apply ../rack-plugin/engine-fork.patch
# download Rack SDK 2.6.6 for your platform into sdk/Rack-SDK
cd rack-plugin
make install   # or: make share  (package without the build machine's user name, in share/)
```

Tests: `cd rack-plugin/rust && cargo test --release -- --test-threads=1`, and the scripts in
`rack-plugin/tests/` (`poly.sh`, `view.sh`).

More detail on the design and the measurements: [rack-plugin/HANDOFF.md](rack-plugin/HANDOFF.md).

## License

MIT. See [rack-plugin/LICENSE](rack-plugin/LICENSE). It includes the MIT notice of the Chimera engine.
The CMU Pronouncing Dictionary has its own license: [rack-plugin/LICENSE-CMUDICT.txt](rack-plugin/LICENSE-CMUDICT.txt).
