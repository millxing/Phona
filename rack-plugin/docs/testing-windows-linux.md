# Test Phona on Windows or Linux

Thank you for testing. Phona is a singing voice for VCV Rack 2. It is a physical model of the vocal
folds and the vocal tract, not samples. Until now it has only run on Macs. This is a **test build** for
Windows and Linux. Automatic tests pass on both, but nobody has played it on a real PC yet.

It takes about 15 minutes. You need no special knowledge, only VCV Rack 2 and headphones or speakers.

## What you need

- **Windows** 10 or 11 (64-bit), or **Linux** (64-bit x86, Ubuntu 20.04 or newer, or a similar distribution).
- **VCV Rack 2.6** or newer. Rack Free is enough.
- The file `Phona-2.4.1-win-x64.vcvplugin` (Windows) or `Phona-2.4.1-lin-x64.vcvplugin` (Linux).
  Rob gives you the link.

## Install

1. Close Rack.
2. Copy the `.vcvplugin` file into the Rack plugins folder:
   - Windows: `%LOCALAPPDATA%\Rack2\plugins-win-x64\`
     (paste this text into the address bar of File Explorer, then press Enter)
   - Linux: `~/.local/share/Rack2/plugins-lin-x64/`
3. Start Rack. Rack unpacks the file. You do not need to do anything else.

To remove Phona later: close Rack, then delete the `Phona` folder and the `.vcvplugin` file from the
same plugins folder.

## Tests

Do the tests in this order. If a test fails, write down what you saw and continue with the next test.

**1. Phona loads.** Right-click on an empty area of the rack. Type `Phona` in the search field.
You should see 4 modules: Phona, Phona View, Phona Scope and Phona Script.

**2. It sings.** Add **Phona**. Connect its **OUT** to an **Audio** module, and choose your audio
device there. With no cable in **GATE**, Phona sings all the time. Turn **VOWEL** and **PITCH**.
The vowel and the pitch must change smoothly.

**3. Lyrics.** Connect a clock or a sequencer gate (for example from the Fundamental modules **LFO**
square output or **SEQ-3**) to **GATE**. Right-click on Phona and type `hel-lo world` in the
**Lyrics** field (the hyphen splits a word into syllables). Each gate must sing the next syllable:
"hel", "lo", "world".

**4. Four voices.** Right-click on Phona > **Polyphony (max voices)** > **4**. Add a **MIDI-CV** module
with polyphony set to 4 channels. Connect its **V/OCT** to Phona's **V/OCT** and its **GATE** to
Phona's **GATE**. Play a chord on your MIDI keyboard. If you do not have a keyboard, use any
polyphonic source. You must hear one voice for each note.

**5. Displays.** Put **Phona View** and **Phona Scope** directly next to Phona, with no gap. The
link light comes on, and the mouth and the displays move while Phona sings.

**6. CPU use. This is the most important test.**
- Turn on the CPU meters: in the **Engine** menu, choose **Performance meters** (or press F3).
- Use a sample rate of 48 kHz (Audio module or Engine menu).
- Read the number on Phona with **1 voice** singing, then with **4 voices** singing.
- Listen for clicks, crackles or dropouts with 4 voices.
- Phona starts in **Eco** quality. Then right-click on Phona > **Quality** > **Full (more CPU)** and read
  the numbers for 1 and 4 voices again. Full must use much more CPU. The sound is a little darker; that is normal.

**7. Beatbox (optional, experimental).** Right-click on Phona > **Lyrics/Beatbox** > Beatbox. Type
`B t Pf t` in the text field. Each gate plays the next sound (kick, hat, snare, hat). These sounds are
not finished. Report only crashes, silence or loud clicks, not how good the sounds are.

## Send your results to Rob

Copy this list and fill it in:

```
Operating system and version:
Rack version (Help > About):
Processor (CPU) model:
Audio sample rate and block size:

1. Phona loads:           yes / no
2. It sings:              yes / no
3. Lyrics:                yes / no
4. Four voices:           yes / no
5. Displays:              yes / no
6. CPU, 1 voice:          Eco ___ %   Full ___ %
   CPU, 4 voices:         Eco ___ %   Full ___ %
   Clicks or dropouts:    none / some / many
7. Beatbox (optional):    ok / problem / not tested

Anything else (crashes, error messages, sounds that seem wrong):
```

If Rack crashes, also send the log file:
- Windows: `%LOCALAPPDATA%\Rack2\log.txt`
- Linux: `~/.local/share/Rack2/log.txt`
