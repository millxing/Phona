#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ChimVoice ChimVoice;
typedef struct ChimParams {
	double f0, pressure, breathiness, tension, brightness, roughness, vib_depth, vib_rate, vowel, gate, glide, fry, velum, larynx, ventricular;
} ChimParams;
// Allocating: never call on the audio thread.
ChimVoice* chim_new(double sample_rate, double female, double child);
void chim_free(ChimVoice* v);
// Beatbox mode and monophonic hits (0/rest or invalid = cancel/mute, 1=B, 2=Pf, 3=t).
void chim_set_beatbox(ChimVoice* v, int on);
void chim_beat_hit(ChimVoice* v, int token);
// Real-time safe.
void chim_set(ChimVoice* v, const ChimParams* p);
void chim_render(ChimVoice* v, float* out, size_t n);
int chim_is_dead(const ChimVoice* v);
/* Engine output limiter on/off (test hook; the module leaves it off). Real-time safe. */
void chim_set_limiter(ChimVoice* v, int on);
/* External input share: 0 = folds only, 1 = external sound only. Applies at the next chim_set. */
void chim_set_ext(ChimVoice* v, double mix);
/* As chim_render, with an external excitation (1.0 = 5 V) sent through the tract. Real-time safe. */
void chim_render_ext(ChimVoice* v, const float* in, float* out, size_t n);
/* Lyrics: text -> syllable plans for one body (female 0..1). English words (CMU dictionary, with
   letter-to-sound rules for the rest); a hyphen splits syllables. Allocating: never call new/free
   on the audio thread. Null for no syllables. */
typedef struct ChimLyrics ChimLyrics;
/* Tunable values (Phona Lab): CHIM_TUNE_N doubles, in the order of T_* in lib.rs; null = defaults.
   chim_lyrics_new uses the ones that shape a syllable's plan, chim_set_tune (real-time safe) the
   ones that act while the voice runs. */
#define CHIM_TUNE_N 18
void chim_tune_default(double* out);
void chim_set_tune(ChimVoice* v, const double* tune);
/* 1 when no syllable is held (the voice then reads nothing from its lyrics). Real-time safe. */
int chim_lyrics_idle(const ChimVoice* v);
ChimLyrics* chim_lyrics_new(const char* text, double female, const double* tune);
void chim_lyrics_free(ChimLyrics* l);
size_t chim_lyrics_len(const ChimLyrics* l);
void chim_lyrics_text(const ChimLyrics* l, size_t i, char* buf, size_t n);
/* Lyrics mode on/off. Off clears the score and drops the voice's pointer to its lyrics: call it
   before the lyrics a voice sang from are freed. Real-time safe. */
void chim_set_lyrics(ChimVoice* v, int on);
/* Gate opened / closed in lyrics mode: sing syllable i, end the syllable. Real-time safe. */
void chim_note_on(ChimVoice* v, const ChimLyrics* l, size_t i);
void chim_note_off(ChimVoice* v);
#ifdef __cplusplus
}
#endif
