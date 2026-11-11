#include "PhonaLab.hpp"
#include "panel.hpp"

// A knob's value under the knob. Blue when it is not the default, so a changed value is easy to see.
struct LabValue : TransparentWidget {
	PhonaLab* module = nullptr;
	int param = 0;
	void draw(const DrawArgs& args) override {
		const PhonaLab::Spec& s = PhonaLab::spec(param);
		float v = module ? module->params[param].getValue() : s.def;
		std::string text;
		switch (s.unit) {
			case PhonaLab::U_X: text = string::f("x %.2f", v); break;
			case PhonaLab::U_MS: text = string::f("%.0f ms", v); break;
			case PhonaLab::U_CUT: text = v >= 150.f ? "OFF" : string::f("%.0f ms", v); break;
			default: text = string::f("%.2f", v);
		}
		std::shared_ptr<window::Font> font = panelFont();
		if (!font) return;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, box.size.y / 2.f);
		nvgFillColor(args.vg, CARD_HEAD);
		nvgFill(args.vg);
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 6.3f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		bool changed = std::fabs(v - s.def) > 1e-4f * (s.mx - s.mn);
		nvgFillColor(args.vg, changed ? nvgRGB(0x16, 0x5c, 0xa8) : INK);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 0.3f, text.c_str(), NULL);
	}
};

static void copyValues(PhonaLab* module) {
	glfwSetClipboardString(APP->window->win, module->valuesText().c_str());
}

// COPY: all values to the clipboard as one line of text.
struct CopyButton : OpaqueWidget {
	PhonaLab* module = nullptr;
	double copiedAt = -10.0;
	void draw(const DrawArgs& args) override {
		bool done = system::getTime() - copiedAt < 1.2;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, done ? AMBER : INK);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = panelFont();
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 6.6f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, done ? INK : nvgRGB(0xf3, 0xeb, 0xdc));
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 0.3f, done ? "COPIED" : "COPY", NULL);
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			copyValues(module);
			copiedAt = system::getTime();
			e.consume(this);
		}
	}
};

struct PhonaLabWidget : ModuleWidget {
	// Geometry (mm), the same numbers as in tools/panel_lab.py
	static constexpr float ROW0 = 8.6f, ROW_H = 29.9f, X0 = 1.5f, X1 = 79.78f;
	static float cellX(int n, int col) { return X0 + (X1 - X0) / n * (col + 0.5f); }

	PhonaLabWidget(PhonaLab* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PhonaLab.svg")));

		PanelLabel* title = new PanelLabel(mm2px(Vec(40.64, 4.1)), "PHONA LAB", 14.f);
		title->color = nvgRGB(0xf3, 0xeb, 0xdc);
		title->spacing = 3.f;
		addChild(title);
		// lit when a Phona is next to this module
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(76.2, 4.1)), module, PhonaLab::LINK_LIGHT));

		const char* sections[4] = {"LAST P  T  K", "LAST CONSONANT", "FIRST CONSONANT / ALL", "TIMING / LEVEL"};
		for (int r = 0; r < 4; r++) {
			PanelLabel* s = new PanelLabel(mm2px(Vec(4.f, ROW0 + r * ROW_H + 2.25f)), sections[r], 6.6f);
			s->color = nvgRGB(0xf3, 0xeb, 0xdc);
			s->align = NVG_ALIGN_LEFT;
			s->spacing = 0.8f;
			addChild(s);
		}
		for (int r = 0; r < PhonaLab::ROWS; r++)
		for (int c = 0; c < PhonaLab::rowLen(r); c++) {
			int i = PhonaLab::cellParam(r, c);
			const PhonaLab::Spec& s = PhonaLab::spec(i);
			float x = cellX(PhonaLab::rowLen(r), c), y = ROW0 + r * ROW_H;
			if (s.unit == PhonaLab::U_SWITCH) {
				// the last cell: the limiter switch and the COPY button
				addChild(new PanelLabel(mm2px(Vec(x - 3.6f, y + 7.2f)), s.label));
				addParam(createParamCentered<CKSS>(mm2px(Vec(x - 3.6f, y + 13.5f)), module, i));
				CopyButton* b = new CopyButton;
				b->module = module;
				b->box.pos = mm2px(Vec(x - 6.2f, y + 20.2f));
				b->box.size = mm2px(Vec(12.4f, 5.f));
				addChild(b);
				continue;
			}
			addChild(new PanelLabel(mm2px(Vec(x, y + 7.2f)), s.label));
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y + 14.6f)), module, i));
			LabValue* v = new LabValue;
			v->module = module;
			v->param = i;
			v->box.pos = mm2px(Vec(x - 5.5f, y + 21.2f));
			v->box.size = mm2px(Vec(11.f, 2.9f));
			addChild(v);
		}
	}

	void appendContextMenu(Menu* menu) override {
		PhonaLab* module = getModule<PhonaLab>();
		if (!module) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Put this module next to a Phona that has lyrics"));
		menu->addChild(createMenuItem("Copy values", "", [=]() { copyValues(module); }));
		menu->addChild(createMenuItem("Paste values", "", [=]() {
			const char* t = glfwGetClipboardString(APP->window->win);
			if (t) module->fromText(t);
		}));
	}
};

Model* modelPhonaLab = createModel<PhonaLab, PhonaLabWidget>("PhonaLab");
