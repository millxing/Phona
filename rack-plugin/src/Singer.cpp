#include "plugin.hpp"
#include "chimera_rack.h"
#include <atomic>
#include <thread>
#include <chrono>

// Prototype singing voice. One Chimera voice per module (monophonic).
//
// Real-time rules: the audio thread only calls chim_set / chim_render. Building a voice (sample
// rate or body change) allocates, so a worker thread builds it and hands it over through an
// atomic pointer; the old voice goes back to the worker to be freed.
struct Singer : Module {
	enum ParamId {
		// pitch group
		PITCH_PARAM, FINE_PARAM, GLIDE_PARAM, VIBDEPTH_PARAM, VIBRATE_PARAM,
		// tract group
		VOWEL_PARAM, NASAL_PARAM, LARYNX_PARAM,
		// source group
		TENSION_PARAM, BRIGHT_PARAM, BREATHY_PARAM, ROUGH_PARAM, FRY_PARAM,
		BODY_PARAM,
		// CV amount trimmers (attenuverters)
		VOWEL_ATT_PARAM, NASAL_ATT_PARAM, TENSION_ATT_PARAM, BRIGHT_ATT_PARAM, BREATHY_ATT_PARAM,
			EFFORT_PARAM, SOURCE_PARAM, // appended last so saved patches keep their param ids
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GLIDE_INPUT, VIBDEPTH_INPUT, VIBRATE_INPUT,
		VOWEL_INPUT, NASAL_INPUT, LARYNX_INPUT,
		TENSION_INPUT, BRIGHT_INPUT, BREATHY_INPUT, ROUGH_INPUT, FRY_INPUT,
		GATE_INPUT, EFFORT_INPUT, EXT_INPUT, INPUTS_LEN
	};
	enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(STATUS_LIGHT, 2), LIGHTS_LEN }; // green = sounding, red = fault

	// A gate input below this voltage is closed (a small DC offset must not hold the voice open).
	static constexpr float GATE_THRESHOLD = 0.1f;

	// A voice plus what it was built for. Immutable once published.
	struct Built {
		ChimVoice* voice;
		int body;
		float rate;
	};

	Built* active = nullptr;               // owned by the audio thread
	std::atomic<Built*> incoming{nullptr}; // published by worker, taken by audio thread
	std::atomic<Built*> outgoing{nullptr}; // handed back by audio thread, freed by worker
	std::atomic<int> desiredBody{0};       // what the audio thread wants now
	std::atomic<float> desiredRate{0.f};
	std::atomic<bool> running{true};
	std::atomic<bool> buildFailed{false};
	std::atomic<bool> rebuildRequest{false}; // audio thread: the active voice died, build a new one
	bool dead = false;          // active voice hit a panic / non-finite output
	bool faultReported = false; // rebuildRequest already sent for this voice
	std::thread worker;

	int ctrl = 0;
	float silentFor = 0.f;  // seconds of silence with gate closed
	bool idle = false;
	float lastGate = 0.f;
	int gateHold = 0;       // samples a gate onset is held open so the engine's control tick sees it
	float heldGate = 0.f;

	Singer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PITCH_PARAM, -36.f, 36.f, -12.f, "Pitch", " Hz", dsp::FREQ_SEMITONE, dsp::FREQ_C4);
		getParamQuantity(PITCH_PARAM)->snapEnabled = true; // semitone steps; FINE covers the gaps
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine tune", " cents", 0.f, 100.f);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.547f, "Glide", " ms", 500.f, 2.f); // 2 ms .. 1 s, default 60 ms
		configParam(VIBDEPTH_PARAM, 0.f, 200.f, 0.f, "Vibrato depth", " cents");
		configParam(VIBRATE_PARAM, 0.5f, 12.f, 5.5f, "Vibrato rate", " Hz");
		configParam(VOWEL_PARAM, 0.f, 4.f, 2.f, "Vowel (i e a o u)");
		configParam(NASAL_PARAM, 0.f, 1.f, 0.06f, "Nasal (velum opening)", "%", 0.f, 100.f);
		configParam(LARYNX_PARAM, -1.6f, 1.6f, 0.f, "Larynx height (low/dark .. high/bright)", " cm");
		configParam(TENSION_PARAM, -0.5f, 1.5f, 0.5f, "Vocal fold tension (lax .. pressed)");
		configParam(BRIGHT_PARAM, -0.5f, 1.5f, 0.25f, "Brightness (glottal closure)");
		configParam(BREATHY_PARAM, 0.f, 1.5f, 0.15f, "Breathiness");
		configParam(ROUGH_PARAM, 0.f, 2.f, 0.1f, "Roughness (jitter, shimmer, period doubling)");
		configParam(FRY_PARAM, 0.f, 1.f, 0.f, "Vocal fry", "%", 0.f, 100.f);
		configSwitch(BODY_PARAM, 0.f, 1.f, 0.f, "Body", {"Male", "Female"});
		configParam(VOWEL_ATT_PARAM, -1.f, 1.f, 1.f, "Vowel CV amount", "%", 0.f, 100.f);
		configParam(NASAL_ATT_PARAM, -1.f, 1.f, 1.f, "Nasal CV amount", "%", 0.f, 100.f);
		configParam(TENSION_ATT_PARAM, -1.f, 1.f, 1.f, "Tension CV amount", "%", 0.f, 100.f);
		configParam(BRIGHT_ATT_PARAM, -1.f, 1.f, 1.f, "Brightness CV amount", "%", 0.f, 100.f);
		configParam(BREATHY_ATT_PARAM, -1.f, 1.f, 1.f, "Breathiness CV amount", "%", 0.f, 100.f);
		configParam(EFFORT_PARAM, 0.25f, 2.f, 1.f, "Effort (lung pressure)", "%", 0.f, 100.f);
		configInput(EFFORT_INPUT, "Effort CV (10V = full knob range)");
		configParam(SOURCE_PARAM, 0.f, 1.f, 1.f, "Source (vocal folds .. external input)", "%", 0.f, 100.f);
		configInput(EXT_INPUT, "External sound (replaces the vocal folds; no effect when unpatched)");
		configInput(VOCT_INPUT, "1V/octave pitch");
		configInput(GATE_INPUT, "Gate / breath envelope (0.1-10V; unpatched = always on)");
		const char* cvNames[] = {nullptr, "Glide", "Vibrato depth", "Vibrato rate", "Vowel", "Nasal", "Larynx", "Tension", "Brightness", "Breathiness", "Roughness", "Fry"};
		for (int i = GLIDE_INPUT; i <= FRY_INPUT; i++)
			configInput(i, std::string(cvNames[i]) + " CV (10V = full knob range)");
		configOutput(AUDIO_OUTPUT, "Voice");
		worker = std::thread([this] { work(); });
	}

	~Singer() {
		running = false;
		worker.join();
		destroy(active);
		destroy(incoming.exchange(nullptr));
		destroy(outgoing.exchange(nullptr));
	}

	static void destroy(Built* b) {
		if (!b) return;
		chim_free(b->voice);
		delete b;
	}

	// Worker: builds whatever the audio thread currently wants, retries failures with backoff.
	void work() {
		int builtBody = -1;
		float builtRate = 0.f;
		int backoff = 0;
		while (running) {
			destroy(outgoing.exchange(nullptr));
			int b = desiredBody.load();
			float r = desiredRate.load();
			// the active voice died: build a replacement, but not more often than once per second
			if (rebuildRequest.exchange(false)) {
				builtBody = -1;
				backoff = 200;
			}
			if (backoff > 0) backoff--;
			else if (r > 0.f && (b != builtBody || r != builtRate)) {
				ChimVoice* v = chim_new(r, b == 1 ? 1.0 : 0.0, 0.0);
				if (v) {
					builtBody = b;
					builtRate = r;
					buildFailed = false;
					destroy(incoming.exchange(new Built{v, b, r})); // a stale unclaimed build is dropped
				}
				else {
					buildFailed = true;
					backoff = 100; // ~0.5 s
				}
			}
			// a voice the audio thread rejected (stale) means it still wants something else
			if (active_rejected.exchange(false)) builtBody = -1;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
	std::atomic<bool> active_rejected{false};

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		desiredRate = e.sampleRate; // the worker rebuilds; process() mutes until it arrives
	}

	// knob + CV. 10V spans the knob's full range; `att` is an optional attenuverter param (-1..1).
	float knobCv(int param, int input, int att = -1) {
		ParamQuantity* q = paramQuantities[param];
		float amount = att >= 0 ? params[att].getValue() : 1.f;
		float v = params[param].getValue() + amount * inputs[input].getVoltage() / 10.f * (q->maxValue - q->minValue);
		return clamp(v, q->minValue, q->maxValue);
	}

	void process(const ProcessArgs& args) override {
		int b = (int) params[BODY_PARAM].getValue();
		desiredBody = b;
		desiredRate = args.sampleRate;

		// take a newly built voice only when the hand-back slot is free (old voice must go to the worker)
		if (outgoing.load() == nullptr) {
			if (Built* n = incoming.exchange(nullptr)) {
				if (n->body == b && n->rate == args.sampleRate) {
					outgoing.store(active);
					active = n;
					ctrl = 0;
					idle = false;
					dead = false;
					faultReported = false;
				}
				else {
					outgoing.store(n); // obsolete build: dispose on the worker
					active_rejected = true;
				}
			}
		}
		// no voice yet, or still the old sample rate (would play at the wrong pitch): mute
		if (!active || active->rate != args.sampleRate) {
			outputs[AUDIO_OUTPUT].setVoltage(0.f);
			lights[STATUS_LIGHT + 0].setBrightness(0.f);
			lights[STATUS_LIGHT + 1].setBrightness(buildFailed ? 1.f : 0.f);
			return;
		}
		ChimVoice* voice = active->voice;

		float gate = 1.f;
		if (inputs[GATE_INPUT].isConnected()) {
			float gv = inputs[GATE_INPUT].getVoltage();
			gate = gv >= GATE_THRESHOLD ? clamp(gv / 10.f, 0.f, 1.f) : 0.f;
		}
		// The engine samples its gate once per 64 output samples, so a short trigger could vanish:
		// hold an onset open for 128 samples (and send it right away).
		if (gate > 0.f && lastGate <= 0.f) {
			gateHold = 128;
			heldGate = 0.f;
			ctrl = 0;
		}
		lastGate = gate;
		if (gateHold > 0) {
			gateHold--;
			heldGate = std::max(heldGate, gate);
			gate = heldGate;
		}

		// controls at 1/16 sample rate (the engine itself updates every 64 samples)
		if (ctrl-- <= 0) {
			ctrl = 15;
			ChimParams p;
			float semis = params[PITCH_PARAM].getValue() + params[FINE_PARAM].getValue();
			p.f0 = dsp::FREQ_C4 * std::pow(2.f, semis / 12.f + inputs[VOCT_INPUT].getVoltage());
			p.vowel = knobCv(VOWEL_PARAM, VOWEL_INPUT, VOWEL_ATT_PARAM);
			p.pressure = 8.0 * knobCv(EFFORT_PARAM, EFFORT_INPUT); // Rust side clamps and compensates
			p.breathiness = knobCv(BREATHY_PARAM, BREATHY_INPUT, BREATHY_ATT_PARAM);
			p.tension = knobCv(TENSION_PARAM, TENSION_INPUT, TENSION_ATT_PARAM);
			p.brightness = knobCv(BRIGHT_PARAM, BRIGHT_INPUT, BRIGHT_ATT_PARAM);
			p.roughness = knobCv(ROUGH_PARAM, ROUGH_INPUT);
			p.vib_depth = knobCv(VIBDEPTH_PARAM, VIBDEPTH_INPUT);
			p.vib_rate = knobCv(VIBRATE_PARAM, VIBRATE_INPUT);
			p.gate = gate;
			p.glide = 0.002 * std::pow(500.f, knobCv(GLIDE_PARAM, GLIDE_INPUT));
			p.fry = knobCv(FRY_PARAM, FRY_INPUT);
			p.velum = -0.25 + 1.65 * knobCv(NASAL_PARAM, NASAL_INPUT, NASAL_ATT_PARAM);
			p.larynx = knobCv(LARYNX_PARAM, LARYNX_INPUT);
			p.ventricular = 0.0; // FALSE FOLDS control removed (subtle effect)
			// SOURCE only acts with a cable in EXT IN: unpatched, the folds always sing
			chim_set_ext(voice, inputs[EXT_INPUT].isConnected() ? params[SOURCE_PARAM].getValue() : 0.0);
			chim_set(voice, &p);
			// a dead voice stays silent: show the fault and ask the worker for a new voice (once)
			dead = chim_is_dead(voice) != 0;
			if (dead && !faultReported) {
				faultReported = true;
				rebuildRequest = true;
			}
		}

		// skip rendering once the gate is closed and the voice has decayed to silence
		if (gate > 0.f) {
			idle = false;
			silentFor = 0.f;
		}
		float out = 0.f;
		if (!idle) {
			float ext = inputs[EXT_INPUT].getVoltage() / 5.f;
			chim_render_ext(voice, &ext, &out, 1);
			if (gate <= 0.f && std::fabs(out) < 1e-5f) {
				silentFor += args.sampleTime;
				if (silentFor > 1.f) idle = true;
			}
			else if (gate <= 0.f) {
				silentFor = 0.f;
			}
		}
		outputs[AUDIO_OUTPUT].setVoltage(6.25f * out);
		bool fault = dead || buildFailed;
		lights[STATUS_LIGHT + 0].setBrightnessSmooth(idle || fault ? 0.f : 1.f, args.sampleTime);
		lights[STATUS_LIGHT + 1].setBrightnessSmooth(fault ? 1.f : 0.f, args.sampleTime);
	}
};

// Prototype panel: plain background + text labels drawn with NanoVG.
struct PanelLabel : TransparentWidget {
	std::string text;
	float size;
	PanelLabel(Vec pos, std::string t, float s = 9.f) : text(t), size(s) { box.pos = pos; box.size = Vec(0, 0); }
	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, size);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0x22, 0x22, 0x22));
		nvgText(args.vg, 0, 0, text.c_str(), NULL);
	}
};

// Greys out a control that acts only on the vocal folds, by the share of the sound that comes
// from EXT IN instead (a wash of the panel colour over the label, knob and jack). The controls
// still work: at SOURCE 50% they act on the fold half of the sound.
struct DimOverlay : TransparentWidget {
	Singer* module = nullptr;
	NVGcolor panel;
	void draw(const DrawArgs& args) override {
		float amount = 1.f;
		if (module) {
			if (!module->inputs[Singer::EXT_INPUT].isConnected()) return;
			amount = module->params[Singer::SOURCE_PARAM].getValue();
		}
		else if (!getenv("CHIMERA_DIM_PREVIEW")) {
			return; // module browser / screenshot: not dimmed (set the variable to check the overlays)
		}
		if (amount <= 0.f) return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgTransRGBAf(panel, 0.72f * amount));
		nvgFill(args.vg);
	}
};

struct SingerWidget : ModuleWidget {
	// fold-only controls: the pitch row, and TENSION, BRIGHT, ROUGH, FRY and EFFORT
	void addDim(Singer* module, Vec centerMm, Vec sizeMm, NVGcolor panel = nvgRGB(0xe8, 0xe4, 0xdc)) {
		DimOverlay* d = new DimOverlay;
		d->module = module;
		d->panel = panel; // must match res/Singer.svg under the overlay
		d->box.pos = mm2px(Vec(centerMm.x - sizeMm.x / 2.f, centerMm.y - sizeMm.y / 2.f));
		d->box.size = mm2px(sizeMm);
		addChild(d);
	}

	SingerWidget(Singer* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Singer.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(new PanelLabel(mm2px(Vec(50.8, 6)), "SINGER", 13.f));
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(93, 6)), module, Singer::STATUS_LIGHT));

		// One row per function group: pitch, tract, source. Each cell: label, knob, CV jack below.
		// A cell with a CV amount trimmer puts the jack on the left and the trimmer on the right.
		struct Cell { int row, col, param, input, att; const char* name; bool big; };
		const Cell cells[] = {
			{0, 0, Singer::PITCH_PARAM, Singer::VOCT_INPUT, -1, "PITCH / V-OCT", true},
			{0, 1, Singer::FINE_PARAM, -1, -1, "FINE", false},
			{0, 2, Singer::GLIDE_PARAM, Singer::GLIDE_INPUT, -1, "GLIDE", false},
			{0, 3, Singer::VIBDEPTH_PARAM, Singer::VIBDEPTH_INPUT, -1, "VIB DEPTH", false},
			{0, 4, Singer::VIBRATE_PARAM, Singer::VIBRATE_INPUT, -1, "VIB RATE", false},
			{1, 0, Singer::VOWEL_PARAM, Singer::VOWEL_INPUT, Singer::VOWEL_ATT_PARAM, "VOWEL", true},
			{1, 1, Singer::NASAL_PARAM, Singer::NASAL_INPUT, Singer::NASAL_ATT_PARAM, "NASAL", false},
			{1, 2, Singer::LARYNX_PARAM, Singer::LARYNX_INPUT, -1, "LARYNX", false},
			{2, 0, Singer::TENSION_PARAM, Singer::TENSION_INPUT, Singer::TENSION_ATT_PARAM, "TENSION", false},
			{2, 1, Singer::BRIGHT_PARAM, Singer::BRIGHT_INPUT, Singer::BRIGHT_ATT_PARAM, "BRIGHT", false},
			{2, 2, Singer::BREATHY_PARAM, Singer::BREATHY_INPUT, Singer::BREATHY_ATT_PARAM, "BREATHY", false},
			{2, 3, Singer::ROUGH_PARAM, Singer::ROUGH_INPUT, -1, "ROUGH", false},
			{2, 4, Singer::FRY_PARAM, Singer::FRY_INPUT, -1, "FRY", false},
		};
		for (const Cell& c : cells) {
			float x = 10.16f + c.col * 20.32f;
			float y = 24.f + c.row * 29.f;
			addChild(new PanelLabel(mm2px(Vec(x, y - 7.5f)), c.name, 7.f));
			if (c.big)
				addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(x, y + 2.f)), module, c.param));
			else
				addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y + 2.f)), module, c.param));
			if (c.input < 0) continue;
			if (c.att >= 0) {
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x - 4.5f, y + 14.f)), module, c.input));
				addParam(createParamCentered<Trimpot>(mm2px(Vec(x + 5.f, y + 14.f)), module, c.att));
			}
			else {
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y + 14.f)), module, c.input));
			}
		}
		// body is a tract property: it sits in the tract row
		addChild(new PanelLabel(mm2px(Vec(91.44f, 45.5f)), "BODY M/F", 7.f));
		addParam(createParamCentered<CKSS>(mm2px(Vec(91.44f, 57.f)), module, Singer::BODY_PARAM));
		addChild(new PanelLabel(mm2px(Vec(10.16f, 106.f)), "EFFORT", 7.f));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10.16f, 115.f)), module, Singer::EFFORT_PARAM));
		addChild(new PanelLabel(mm2px(Vec(24.f, 106.f)), "CV", 7.f));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.f, 115.f)), module, Singer::EFFORT_INPUT));
		addChild(new PanelLabel(mm2px(Vec(40.f, 106.f)), "GATE", 7.f));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.f, 115.f)), module, Singer::GATE_INPUT));
		addChild(new PanelLabel(mm2px(Vec(58.f, 106.f)), "EXT IN", 7.f));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58.f, 115.f)), module, Singer::EXT_INPUT));
		addChild(new PanelLabel(mm2px(Vec(71.5f, 106.f)), "SOURCE", 7.f));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(71.5f, 115.f)), module, Singer::SOURCE_PARAM));
		addChild(new PanelLabel(mm2px(Vec(91.44f, 106.f)), "OUT", 7.f));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(91.44f, 115.f)), module, Singer::AUDIO_OUTPUT));

		// added last so they draw over the controls
		addDim(module, Vec(50.8f, 28.5f), Vec(100.f, 28.f));              // pitch row
		for (int col : {0, 1, 3, 4})                                       // source row except BREATHY
			addDim(module, Vec(10.16f + col * 20.32f, 86.5f), Vec(19.5f, 28.f));
		addDim(module, Vec(17.f, 113.f), Vec(24.f, 18.f), nvgRGB(0xd6, 0xcf, 0xc2)); // EFFORT knob and CV
	}
};

Model* modelSinger = createModel<Singer, SingerWidget>("Singer");
