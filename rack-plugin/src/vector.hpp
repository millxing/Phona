#pragma once
#include "plugin.hpp"
#include <vector>

// The vector-display look shared by Phona View and Phona Scope (the web app's style, see
// engine/src/ui/colors.ts and render/vector.ts): glowing lines on black, drawn additively, so
// crossings get brighter. Blue is structure, amber is sound.
namespace vd {

typedef std::vector<Vec> Pts;

static const NVGcolor BLUE = nvgRGB(0x4f, 0xb6, 0xf2);
static const NVGcolor BLUE_MID = nvgRGB(0x34, 0x87, 0xc4);
static const NVGcolor BLUE_DIM = nvgRGB(0x1f, 0x5f, 0x8c);
static const NVGcolor FRAME = nvgRGB(0x13, 0x39, 0x54);
static const NVGcolor GREEN = nvgRGB(0x5b, 0xe3, 0x81);
static const NVGcolor RED = nvgRGB(0xff, 0x5d, 0x40);
static const NVGcolor PINK = nvgRGB(0xff, 0x73, 0xcf);
static const NVGcolor WHITE = nvgRGB(0xee, 0xf6, 0xf8);
static const NVGcolor AMBER = nvgRGB(0xff, 0xb3, 0x40);
static const NVGcolor SPARK = nvgRGB(0xf4, 0xfb, 0xff);
static const NVGcolor STREAK = nvgRGB(0xff, 0xe2, 0xa8);

static inline NVGcolor mixc(NVGcolor a, NVGcolor b, float u) { return nvgLerpRGBA(a, b, clamp(u, 0.f, 1.f)); }
// the whitish core of a bright beam in this colour
static inline NVGcolor core(NVGcolor c) { return mixc(c, nvgRGB(255, 255, 255), 0.55f); }

// sound level 0..1 → dim amber → amber → pale yellow → white
static inline NVGcolor heat(float v) {
	static const float at[5] = {0.f, 0.35f, 0.6f, 0.82f, 1.f};
	static const NVGcolor c[5] = {nvgRGB(0x3a, 0x24, 0x08), nvgRGB(0xc9, 0x82, 0x2a), nvgRGB(0xff, 0xb3, 0x40), nvgRGB(0xff, 0xe3, 0x9a), nvgRGB(0xff, 0xf8, 0xec)};
	v = clamp(v, 0.f, 1.f);
	for (int i = 1; i < 5; i++)
		if (v <= at[i]) return mixc(c[i - 1], c[i], (v - at[i - 1]) / (at[i] - at[i - 1]));
	return c[4];
}
// acoustic pressure RMS (dyn/cm²) → 0..1 on a log scale
static inline float pressureLevel(float rms) {
	if (!(rms > 0.f)) return 0.f;
	const float lo = std::log10(40.f), hi = std::log10(4000.f);
	return clamp((std::log10(rms) - lo) / (hi - lo), 0.f, 1.f);
}
static inline float smoothstep(float a, float b, float x) {
	float t = clamp((x - a) / (b - a), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

// A glowing beam through screen points: a wide faint halo, the line, and for the brightest lines
// a whitish core. `w` is the line width in px.
static inline void beam(NVGcontext* vg, const Pts& pts, NVGcolor c, float w, float alpha = 1.f, bool hot = false, bool close = false) {
	if (pts.size() < 2 || alpha <= 0.f) return;
	nvgBeginPath(vg);
	for (size_t i = 0; i < pts.size(); i++) {
		if (i) nvgLineTo(vg, pts[i].x, pts[i].y);
		else nvgMoveTo(vg, pts[i].x, pts[i].y);
	}
	if (close) nvgClosePath(vg);
	nvgLineJoin(vg, NVG_ROUND);
	nvgLineCap(vg, NVG_ROUND);
	nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
	nvgStrokeColor(vg, nvgTransRGBAf(c, 0.16f * alpha));
	nvgStrokeWidth(vg, w * 3.4f);
	nvgStroke(vg);
	nvgStrokeColor(vg, nvgTransRGBAf(c, alpha));
	nvgStrokeWidth(vg, w);
	nvgStroke(vg);
	if (hot) {
		nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
		nvgStrokeColor(vg, nvgTransRGBAf(core(c), alpha));
		nvgStrokeWidth(vg, w * 0.42f);
		nvgStroke(vg);
	}
	nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
}
// a thin line without halo (grids)
static inline void hair(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor c, float w, float alpha = 1.f) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, x0, y0);
	nvgLineTo(vg, x1, y1);
	nvgStrokeColor(vg, nvgTransRGBAf(c, alpha));
	nvgStrokeWidth(vg, w);
	nvgStroke(vg);
}
// Display text: ShareTechMono (a terminal face), or DejaVuSans for IPA symbols.
static inline void text(NVGcontext* vg, float x, float y, const char* str, float size, NVGcolor c, int align, bool ipa = false, float alpha = 1.f) {
	if (APP->window) { // (no window in the offline render tests)
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system(ipa ? "res/fonts/DejaVuSans.ttf" : "res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		nvgFontFaceId(vg, font->handle);
	}
	nvgFontSize(vg, size);
	nvgTextLetterSpacing(vg, ipa ? 0.f : size * 0.12f);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, nvgTransRGBAf(c, alpha));
	nvgText(vg, x, y, str, NULL);
	nvgTextLetterSpacing(vg, 0.f);
}

} // namespace vd
