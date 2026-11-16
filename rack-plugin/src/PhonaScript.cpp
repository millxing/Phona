#include "PhonaScript.hpp"
#include "panel.hpp"

// The 8 phrases. The phrase Phona sings is lit; the phrase that comes at the next gate (when it is
// another one) has an amber frame. Click a phrase to choose it.
struct ScriptList : OpaqueWidget {
	PhonaScript* module = nullptr;
	std::string shown[PhonaScript::SLOTS];
	float rowH() { return box.size.y / PhonaScript::SLOTS; }
	void step() override {
		if (module) for (int i = 0; i < PhonaScript::SLOTS; i++) shown[i] = module->phrase(i);
		else {
			shown[0] = "twinkle twinkle little star";
			shown[1] = "how I won-der what you are";
		}
		OpaqueWidget::step();
	}
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		std::shared_ptr<window::Font> font = panelFont();
		if (!font) return;
		int cur = module ? module->current.load() : 0;
		int want = module ? module->want.load() : 0;
		bool linked = module && module->lights[PhonaScript::LINK_LIGHT].getBrightness() > 0.5f;
		float h = rowH(), w = box.size.x;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 9.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < PhonaScript::SLOTS; i++) {
			float y = i * h;
			bool empty = !PhonaScript::hasWords(shown[i]);
			bool singing = linked && i == cur;
			bool next = i == want && (i != cur || !linked);
			if (singing) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, 1.f, y + 1.f, w - 2.f, h - 2.f, 2.f);
				nvgFillColor(vg, nvgRGB(0x3a, 0x2a, 0x12));
				nvgFill(vg);
			}
			if (next && !empty) {
				nvgBeginPath(vg);
				nvgRoundedRect(vg, 1.5f, y + 1.5f, w - 3.f, h - 3.f, 2.f);
				nvgStrokeColor(vg, AMBER);
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
			NVGcolor num = singing ? nvgRGB(0xf0, 0xa8, 0x3c) : nvgRGB(0x8a, 0x80, 0x70);
			nvgFillColor(vg, num);
			nvgText(vg, 6.f, y + h / 2.f, string::f("%d", i + 1).c_str(), NULL);
			// the text, cut to the row with "..."
			std::string t = empty ? "-" : shown[i];
			float maxW = w - 22.f;
			if (nvgTextBounds(vg, 0, 0, t.c_str(), NULL, NULL) > maxW) {
				while (!t.empty() && nvgTextBounds(vg, 0, 0, (t + "...").c_str(), NULL, NULL) > maxW) t.pop_back();
				t += "...";
			}
			nvgFillColor(vg, empty ? nvgRGB(0x55, 0x50, 0x48) : singing ? nvgRGB(0xff, 0xe2, 0xb0) : nvgRGB(0xd8, 0xd0, 0xc1));
			nvgText(vg, 17.f, y + h / 2.f, t.c_str(), NULL);
		}
	}
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			module->clickReq = clamp((int) (e.pos.y / rowH()), 0, PhonaScript::SLOTS - 1);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
};

// One phrase's text, in the module's menu.
struct PhraseField : ui::TextField {
	PhonaScript* module = nullptr;
	int slot = 0;
	PhraseField() {
		box.size.x = 320.f;
	}
	void onChange(const ChangeEvent& e) override {
		if (module) module->setPhrase(slot, getText());
	}
};

struct PhonaScriptWidget : ModuleWidget {
	// Panel geometry (mm), the same numbers as in tools/panel_view.py
	static constexpr float W = 71.12f, DISP_X0 = 1.6f, DISP_Y0 = 9.4f, DISP_Y1 = 100.4f;
	PhonaScriptWidget(PhonaScript* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PhonaScript.svg")));
		PanelLabel* title = new PanelLabel(mm2px(Vec(W / 2.f, 4.1f)), "PHONA SCRIPT", 11.f);
		title->color = nvgRGB(0xf3, 0xeb, 0xdc);
		title->spacing = 2.f;
		addChild(title);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(W - 4.6f, 4.1f)), module, PhonaScript::LINK_LIGHT));

		ScriptList* list = new ScriptList;
		list->module = module;
		list->box.pos = mm2px(Vec(DISP_X0 + 0.6f, DISP_Y0 + 0.6f));
		list->box.size = mm2px(Vec(W - 2 * DISP_X0 - 1.2f, DISP_Y1 - DISP_Y0 - 1.2f));
		addChild(list);

		const char* labels[3] = {"NEXT", "RESET", "SELECT"};
		for (int i = 0; i < 3; i++) {
			float x = W / 6.f * (2 * i + 1);
			addChild(new PanelLabel(mm2px(Vec(x, 107.6f)), labels[i], 7.f));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, 116.f)), module, i));
		}
	}

	void appendContextMenu(Menu* menu) override {
		PhonaScript* module = getModule<PhonaScript>();
		if (!module) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Put this module next to a Phona (in Lyrics or Beatbox mode)"));
		menu->addChild(createMenuLabel("Its phrases replace Phona's own text. Each phrase repeats"));
		menu->addChild(createMenuLabel("until NEXT, SELECT, RESET or a click changes it (at the next gate)"));
		for (int i = 0; i < PhonaScript::SLOTS; i++) {
			menu->addChild(createMenuLabel(string::f("Phrase %d", i + 1)));
			PhraseField* f = new PhraseField;
			f->module = module;
			f->slot = i;
			f->placeholder = i == 0 ? "Words (a hyphen splits a word into syllables)" : "Empty: skipped";
			f->setText(module->phrase(i));
			menu->addChild(f);
		}
		menu->addChild(createMenuItem("Clear all phrases", "", [=]() {
			for (int i = 0; i < PhonaScript::SLOTS; i++) module->setPhrase(i, "");
		}));
	}
};

Model* modelPhonaScript = createModel<PhonaScript, PhonaScriptWidget>("PhonaScript");
