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
/* Phona View: a snapshot of one voice's anatomy (midsagittal outlines, body cm, x forward, y up,
   glottis near the origin) and sound. Layout must match `ChimVis` in vis.rs. */
#define CHIM_VIS_PTS 96
#define CHIM_VIS_GRID 64
#define CHIM_VIS_NODES 64
#define CHIM_VIS_SCOPE 128
#define CHIM_VIS_NOISE 6
enum { CHIM_VIS_OUTER, CHIM_VIS_INNER, CHIM_VIS_TONGUE, CHIM_VIS_VELUM, CHIM_VIS_UPPER_LIP, CHIM_VIS_LOWER_LIP,
	CHIM_VIS_UPPER_TEETH, CHIM_VIS_LOWER_TEETH, CHIM_VIS_JAW_BONE, CHIM_VIS_EPIGLOTTIS, CHIM_VIS_NASAL, CHIM_VIS_LINES };
typedef struct ChimVisLine { int n; float x[CHIM_VIS_PTS]; float y[CHIM_VIS_PTS]; } ChimVisLine;
typedef struct ChimVis {
	int valid;
	float scale, pharynx, jaw, larynx;  /* body size, pharynx scale; jaw and larynx articulators */
	float glottis[2], tongue_center[2], tongue_radius, tip[2];
	float lip_s, velum_port, lip_area;  /* cm from glottis, cm2, cm2 */
	ChimVisLine line[CHIM_VIS_LINES];
	int n_grid;                         /* cross-section lines of the airway */
	float g_in_x[CHIM_VIS_GRID], g_in_y[CHIM_VIS_GRID], g_out_x[CHIM_VIS_GRID], g_out_y[CHIM_VIS_GRID], g_s[CHIM_VIS_GRID], g_area[CHIM_VIS_GRID];
	int n_nodes;                        /* tube sections: position, area, pressure RMS (dyn/cm2), flow (cm3/s) */
	float node_s[CHIM_VIS_NODES], node_area[CHIM_VIS_NODES], pressure[CHIM_VIS_NODES], flow[CHIM_VIS_NODES];
	int n_noise;                        /* turbulence sources: amplitude, cm from glottis */
	float noise_level[CHIM_VIS_NOISE], noise_s[CHIM_VIS_NOISE];
	float f0, voicing, p_lung, out_rms, gate, abduction;
	int n_scope;                        /* glottal area over the last two cycles */
	float glottal_area[CHIM_VIS_SCOPE], glottal_flow[CHIM_VIS_SCOPE]; /* cm2, cm3/s */
	int n_ph;                           /* the first n_ph nodes are the pharynx, the rest the mouth */
	float ph_len, or_len;               /* cm */
	char phoneme[24];                   /* UTF-8 (IPA) speech sound being made, "" = none */
} ChimVis;
/* Real-time safe. Returns 1 and sets out->valid when there is a frame to show. */
int chim_vis_capture(ChimVoice* v, ChimVis* out);
/* A resting voice (no sound data). Allocates: never on the audio thread. */
int chim_vis_rest(double female, ChimVis* out);
#ifdef __cplusplus
}
#endif
