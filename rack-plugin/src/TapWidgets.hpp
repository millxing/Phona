#pragma once
#include "PhonaTap.hpp"
#include "panel.hpp"

// Voice selector: F (follow the newest note) and 1-4. The voice on display is lit; voices the
// polyphony menu has not built are dim.
struct VoiceSelector : ParamWidget {
	PhonaTap* view() { return module ? dynamic_cast<PhonaTap*>(module) : nullptr; }
	static constexpr int N = 5;
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		int sel = getParamQuantity() ? (int) std::round(getParamQuantity()->getValue()) : 1;
		PhonaTap* v = view();
		int voices = v ? v->uiVoices : 4, shown = v ? v->uiShown : 0;
		std::shared_ptr<window::Font> font = panelFont();
		float w = box.size.x / N, h = box.size.y;
		for (int i = 0; i < N; i++) {
			bool on = i == sel;
			bool avail = i == 0 || i <= voices;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, i * w + 0.8f, 0.f, w - 1.6f, h, 2.5f);
			nvgFillColor(vg, on ? INK : CARD_HEAD);
			nvgFill(vg);
			bool lit = i > 0 && i - 1 == shown;
			if (lit) {
				nvgStrokeColor(vg, AMBER);
				nvgStrokeWidth(vg, 1.2f);
				nvgStroke(vg);
			}
			if (!font) continue;
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			NVGcolor tc = on ? nvgRGB(0xf0, 0xa8, 0x3c) : INK;
			nvgFillColor(vg, avail ? tc : nvgTransRGBAf(tc, 0.35f));
			const char* label[N] = {"F", "1", "2", "3", "4"};
			nvgText(vg, i * w + w / 2.f, h / 2.f + 0.5f, label[i], NULL);
		}
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && getParamQuantity()) {
			int i = clamp((int) (e.pos.x / (box.size.x / N)), 0, N - 1);
			getParamQuantity()->setValue(i);
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}
};
