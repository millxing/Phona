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
		PITCH_PARAM, GLIDE_PARAM, VOWEL_PARAM, NASAL_PARAM, LARYNX_PARAM,
		TENSION_PARAM, BRIGHT_PARAM, BREATHY_PARAM, ROUGH_PARAM, FRY_PARAM,
		FOLDS_PARAM, VIBDEPTH_PARAM, VIBRATE_PARAM, BODY_PARAM, PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GLIDE_INPUT, VOWEL_INPUT, NASAL_INPUT, LARYNX_INPUT,
		TENSION_INPUT, BRIGHT_INPUT, BREATHY_INPUT, ROUGH_INPUT, FRY_INPUT,
		FOLDS_INPUT, VIBDEPTH_INPUT, VIBRATE_INPUT, GATE_INPUT, INPUTS_LEN
	};
	enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
	enum LightId { ACTIVE_LIGHT, LIGHTS_LEN };

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
	std::thread worker;

	int ctrl = 0;
	float silentFor = 0.f;  // seconds of silence with gate closed
	bool idle = false;
	float lastGate = 0.f;
	int gateHold = 0;       // samples a gate onset is held open so the engine's control tick sees it
	float heldGate = 0.f;

	Singer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PITCH_PARAM, -3.f, 3.f, -1.f, "Pitch", " Hz", 2.f, dsp::FREQ_C4);
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.547f, "Glide", " ms", 500.f, 2.f); // 2 ms .. 1 s, default 60 ms
		configParam(VOWEL_PARAM, 0.f, 4.f, 2.f, "Vowel (i e a o u)");
		configParam(NASAL_PARAM, 0.f, 1.f, 0.06f, "Nasal (velum opening)", "%", 0.f, 100.f);
		configParam(LARYNX_PARAM, -1.6f, 1.6f, 0.f, "Larynx height (low/dark .. high/bright)", " cm");
		configParam(TENSION_PARAM, -0.5f, 1.5f, 0.5f, "Vocal fold tension (lax .. pressed)");
		configParam(BRIGHT_PARAM, -0.5f, 1.5f, 0.25f, "Brightness (glottal closure)");
		configParam(BREATHY_PARAM, 0.f, 1.5f, 0.15f, "Breathiness");
		configParam(ROUGH_PARAM, 0.f, 2.f, 0.1f, "Roughness (jitter, shimmer, period doubling)");
		configParam(FRY_PARAM, 0.f, 1.f, 0.f, "Vocal fry", "%", 0.f, 100.f);
		configParam(FOLDS_PARAM, 0.f, 1.f, 0.f, "False folds (ventricular constriction)", "%", 0.f, 100.f);
		configParam(VIBDEPTH_PARAM, 0.f, 100.f, 0.f, "Vibrato depth", " cents");
		configParam(VIBRATE_PARAM, 3.f, 8.f, 5.5f, "Vibrato rate", " Hz");
		configSwitch(BODY_PARAM, 0.f, 1.f, 0.f, "Body", {"Male", "Female"});
		configInput(VOCT_INPUT, "1V/octave pitch");
		configInput(GATE_INPUT, "Gate / breath envelope (0-10V; unpatched = always on)");
		const char* cvNames[] = {nullptr, "Glide", "Vowel", "Nasal", "Larynx", "Tension", "Brightness", "Breathiness", "Roughness", "Fry", "False folds", "Vibrato depth", "Vibrato rate"};
		for (int i = GLIDE_INPUT; i <= VIBRATE_INPUT; i++)
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

	// knob + CV, CV of 10V spans the knob's full range
	float knobCv(int param, int input) {
		ParamQuantity* q = paramQuantities[param];
		float v = params[param].getValue() + inputs[input].getVoltage() / 10.f * (q->maxValue - q->minValue);
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
			lights[ACTIVE_LIGHT].setBrightness(0.f);
			return;
		}
		ChimVoice* voice = active->voice;

		float gate = inputs[GATE_INPUT].isConnected() ? clamp(inputs[GATE_INPUT].getVoltage() / 10.f, 0.f, 1.f) : 1.f;
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
			float pitch = params[PITCH_PARAM].getValue() + inputs[VOCT_INPUT].getVoltage();
			p.f0 = dsp::FREQ_C4 * std::pow(2.f, pitch);
			p.vowel = knobCv(VOWEL_PARAM, VOWEL_INPUT);
			p.pressure = 8.0;
			p.breathiness = knobCv(BREATHY_PARAM, BREATHY_INPUT);
			p.tension = knobCv(TENSION_PARAM, TENSION_INPUT);
			p.brightness = knobCv(BRIGHT_PARAM, BRIGHT_INPUT);
			p.roughness = knobCv(ROUGH_PARAM, ROUGH_INPUT);
			p.vib_depth = knobCv(VIBDEPTH_PARAM, VIBDEPTH_INPUT);
			p.vib_rate = knobCv(VIBRATE_PARAM, VIBRATE_INPUT);
			p.gate = gate;
			p.glide = 0.002 * std::pow(500.f, knobCv(GLIDE_PARAM, GLIDE_INPUT));
			p.fry = knobCv(FRY_PARAM, FRY_INPUT);
			p.velum = -0.25 + 1.65 * knobCv(NASAL_PARAM, NASAL_INPUT);
			p.larynx = knobCv(LARYNX_PARAM, LARYNX_INPUT);
			p.ventricular = knobCv(FOLDS_PARAM, FOLDS_INPUT);
			chim_set(voice, &p);
		}

		// skip rendering once the gate is closed and the voice has decayed to silence
		if (gate > 0.f) {
			idle = false;
			silentFor = 0.f;
		}
		float out = 0.f;
		if (!idle) {
			chim_render(voice, &out, 1);
			if (gate <= 0.f && std::fabs(out) < 1e-5f) {
				silentFor += args.sampleTime;
				if (silentFor > 1.f) idle = true;
			}
			else if (gate <= 0.f) {
				silentFor = 0.f;
			}
		}
		outputs[AUDIO_OUTPUT].setVoltage(6.25f * out);
		lights[ACTIVE_LIGHT].setBrightnessSmooth(idle ? 0.f : 1.f, args.sampleTime);
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

struct SingerWidget : ModuleWidget {
	SingerWidget(Singer* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Singer.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addChild(new PanelLabel(mm2px(Vec(50.8, 6)), "SINGER", 13.f));
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(93, 6)), module, Singer::ACTIVE_LIGHT));

		// 3 columns x 3 rows of knob + CV jack
		struct K { int p, i; const char* name; };
		const char* names[13] = {"PITCH / V-OCT", "GLIDE", "VOWEL", "NASAL", "LARYNX", "TENSION", "BRIGHT", "BREATHY", "ROUGH", "FRY", "FALSE FOLDS", "VIB DEPTH", "VIB RATE"};
		for (int n = 0; n < 13; n++) {
			float x = 10.16f + (n % 5) * 20.32f;
			float y = 24.f + (n / 5) * 29.f;
			addChild(new PanelLabel(mm2px(Vec(x, y - 7.5f)), names[n], 7.f));
			addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y + 2.f)), module, n));      // param n
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y + 14.f)), module, n));        // its CV input
		}
		addChild(new PanelLabel(mm2px(Vec(10.16f, 106.f)), "BODY M/F", 7.f));
		addParam(createParamCentered<CKSS>(mm2px(Vec(10.16f, 115.f)), module, Singer::BODY_PARAM));
		addChild(new PanelLabel(mm2px(Vec(50.8f, 106.f)), "GATE", 7.f));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.8f, 115.f)), module, Singer::GATE_INPUT));
		addChild(new PanelLabel(mm2px(Vec(91.44f, 106.f)), "OUT", 7.f));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(91.44f, 115.f)), module, Singer::AUDIO_OUTPUT));
	}
};

Model* modelSinger = createModel<Singer, SingerWidget>("Singer");
