#pragma once
#include "plugin.hpp"
// Drawing helpers shared by the module panels.

// Panel colours. The same values are in tools/panel.py, which writes res/Phona.svg.
static const NVGcolor INK = nvgRGB(0x24, 0x21, 0x1d);
static const NVGcolor AMBER = nvgRGB(0xd9, 0x8a, 0x1f);
static const NVGcolor MOD_BLUE = nvgRGB(0x1f, 0x7a, 0xd9);
static const NVGcolor CARD_BODY = nvgRGB(0xec, 0xe7, 0xdd);
static const NVGcolor CARD_HEAD = nvgRGB(0xd8, 0xd0, 0xc1);

static inline std::shared_ptr<window::Font> panelFont() {
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
}

// Panel text (Rack's SVG reader has no text, so the widget draws it).
struct PanelLabel : TransparentWidget {
	std::string text;
	float size;
	NVGcolor color = INK;
	int align = NVG_ALIGN_CENTER;
	float spacing = 0.3f;
	PanelLabel(Vec pos, std::string t, float s = 7.f) : text(t), size(s) { box.pos = pos; box.size = Vec(0, 0); }
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = panelFont();
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, size);
		nvgTextAlign(args.vg, align | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, color);
		nvgTextLetterSpacing(args.vg, spacing);
		nvgText(args.vg, 0, 0, text.c_str(), NULL);
		nvgTextLetterSpacing(args.vg, 0.f);
	}
};

