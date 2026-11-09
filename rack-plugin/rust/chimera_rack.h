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
#ifdef __cplusplus
}
#endif
