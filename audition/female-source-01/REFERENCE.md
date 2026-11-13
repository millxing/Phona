# Reference identified: Jameson Hodge's Chimera duet

User supplied https://x.com/chrisjdimarco/status/2121048407171047852 and an MP3.
DiMarco's post quotes https://x.com/jamesonhodge/status/2119085825509954042.
Retrieved post metadata through https://api.fxtwitter.com/chrisjdimarco/status/2121048407171047852
because direct X fetching returned HTTP 403.

Verified: the uploaded MP3 matches the audio of the quoted Hodge video, not DiMarco's swarm
video. Both decoded to 144.683625 seconds at 8 kHz mono; cross-correlation found zero offset
and Pearson waveform correlation 0.9964479464. Different lossy encoding accounts plausibly
for small differences. DiMarco's own video metadata reports 114.371 seconds.
The Hodge thumbnail shows CHIMERA with Lower and Upper heads, consistent with DuetPage.

Local code evidence:
- engine/src/ui/components/DuetPage.tsx describes playback of a mixed prerecorded track and
  per-voice telemetry, generated with `chimera render notes`. It does not synthesize live.
- engine/crates/chimera-cli/src/render.rs render_notes plans complete phrases with plan_song.
  Defaults: pressure 9, tension 0.55, vibrato 35 cents (can be overridden by note JSON).
- Our audition uses pressure 8, tension 0.5 except candidate B, and zero vibrato.
- The wrapper strips pitch and vibrato gestures and handles gate-driven syllables separately.

Unknown: exact demo engine revision, score files, settings, and mix effects. The local duet
source does not establish the exact settings used in the video. No claim of listening to the
recording was made; identification is based on waveform comparison and image/code inspection.

Next: make a controlled comparison of full-phrase rendering and wrapper rendering using the
same short score and source settings, first one voice dry. Use the identified duet as the quality
reference. Do not infer that reverb, harmony, or scheduling alone explains the quality gap.
