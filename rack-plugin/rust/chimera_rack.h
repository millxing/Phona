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
#ifdef __cplusplus
}
#endif
