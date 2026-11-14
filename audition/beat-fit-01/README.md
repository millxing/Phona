# Beat fit 01 (2026-11-13)

Fits the kick (B), snare (Pf) and hat (t) to the approved human references by search.

- `fit.py <sound> <work_dir> [generations] [seed]`: differential evolution. Each candidate is
  rendered through the module's own hit path (`chim_beat_hit` with a `BeatFit`) by the ignored
  test `sensitivity::fit_batch`. It starts from `fit_<sound>.json` here when that file exists.
- `evaluate.py <sound> <work_dir>`: numbers, robustness, spectrogram image, listening files.
- `table.py`: writes `rack-plugin/rust/src/beat_fit_table.rs` from `fit_*.json` and `eval_*.json`.
- `out/` (ignored, contains reference audio): `*_ref_then_fitted.wav` = reference, candidate,
  reference, candidate, one hit per second. `_v1` = the first fit the user heard.

Loss: L1 distance of level-normalised log-mel spectrograms at three time resolutions, the level
envelope, the band-energy balance (added for the snare), sound outside the reference window,
and a level guard. The time alignment is free, so latency is not part of the loss.

Loss terms added later: band-energy balance (for the snare) and noise-or-tone texture of
300-3000 Hz (for the kick's metallic ring). Loss numbers from before and after are not comparable.

Results in the module's hit path (loss with all terms):

| Sound | Fit | Note |
| --- | ---: | --- |
| t | 8.6 | 67 % of energy above 5 kHz (engine 8 %, reference 84 %); user: good |
| B | 8.3 | lips close again 24 ms after the burst, more airway damping: the 1.5 kHz ring of the open mouth is gone by measure (texture 0.07); not yet heard by the user |
| Pf | 15.4 | band energy near the reference (72 % below 500 Hz against 78 %); too long (135 ms against 68 ms); user: much better |

How the module plays them (rack-plugin/rust/src/lib.rs, `BeatVoice`): each sound has its own
mouths (kick 2, snare 2, hat 3). A mouth prepares its sound ahead and stops the model just
before the burst (`hold` in the table, written by `table.py` from `eval_*.json`); a hit lets it
go on. Nothing is stored: the sound is synthesized at the hit. Measured with
`sensitivity::table_pattern` and `bench_beat`:

- onset 1.2 ms after the trigger (the engine's own path: 80 to 136 ms);
- hits 125 ms apart, also two kicks, all sound in full: no cut, largest sample step 0.31;
- a sound after other sounds has the loss of the sound alone;
- CPU: 0 % while waiting, 16 % of real time with a hit every 125 ms (singing voice: 7 %);
- `held_sound_is_the_same_sound`: stopping the model and going on gives the same samples.

Open: the table was fitted on the male body (the female one is about 8 dB quieter with the
same timing); a hit that finds no prepared mouth of its sound sounds late; no accent yet;
not tested in Rack itself.

Reproduce:

```sh
cargo test --release --manifest-path rack-plugin/rust/Cargo.toml --lib --no-run
python3 audition/beat-fit-01/fit.py t /tmp/fit/t 220
python3 audition/beat-fit-01/evaluate.py t /tmp/fit/t
```
