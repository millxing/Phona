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
struct Phona : Module {
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
		// every CV input has an amount trimmer (V/OCT and GATE are not modulation inputs)
		GLIDE_ATT_PARAM, VIBDEPTH_ATT_PARAM, VIBRATE_ATT_PARAM, LARYNX_ATT_PARAM,
		ROUGH_ATT_PARAM, FRY_ATT_PARAM, EFFORT_ATT_PARAM, SOURCE_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, GLIDE_INPUT, VIBDEPTH_INPUT, VIBRATE_INPUT,
		VOWEL_INPUT, NASAL_INPUT, LARYNX_INPUT,
		TENSION_INPUT, BRIGHT_INPUT, BREATHY_INPUT, ROUGH_INPUT, FRY_INPUT,
		GATE_INPUT, EFFORT_INPUT, EXT_INPUT, SOURCE_INPUT, INPUTS_LEN
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
	std::atomic<float> sourceShown{0.f};     // external share in use (knob + CV), for the panel's grey-out
	std::atomic<float> modShown[PARAMS_LEN] = {}; // knob + CV per param, for the panel's modulation marks
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

	Phona() {
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
		configParam(GLIDE_ATT_PARAM, -1.f, 1.f, 1.f, "Glide CV amount", "%", 0.f, 100.f);
		configParam(VIBDEPTH_ATT_PARAM, -1.f, 1.f, 1.f, "Vibrato depth CV amount", "%", 0.f, 100.f);
		configParam(VIBRATE_ATT_PARAM, -1.f, 1.f, 1.f, "Vibrato rate CV amount", "%", 0.f, 100.f);
		configParam(LARYNX_ATT_PARAM, -1.f, 1.f, 1.f, "Larynx CV amount", "%", 0.f, 100.f);
		configParam(ROUGH_ATT_PARAM, -1.f, 1.f, 1.f, "Roughness CV amount", "%", 0.f, 100.f);
		configParam(FRY_ATT_PARAM, -1.f, 1.f, 1.f, "Fry CV amount", "%", 0.f, 100.f);
		configParam(EFFORT_ATT_PARAM, -1.f, 1.f, 1.f, "Effort CV amount", "%", 0.f, 100.f);
		configParam(SOURCE_ATT_PARAM, -1.f, 1.f, 1.f, "Source CV amount", "%", 0.f, 100.f);
		configParam(EFFORT_PARAM, 0.25f, 2.f, 1.f, "Effort (lung pressure)", "%", 0.f, 100.f);
		configInput(EFFORT_INPUT, "Effort CV (10V = full knob range)");
		configParam(SOURCE_PARAM, 0.f, 1.f, 1.f, "Source (vocal folds .. external input)", "%", 0.f, 100.f);
		configInput(SOURCE_INPUT, "Source CV (10V = full knob range)");
		configInput(EXT_INPUT, "External sound (replaces the vocal folds; no effect when unpatched)");
		configInput(VOCT_INPUT, "1V/octave pitch");
		configInput(GATE_INPUT, "Gate / breath envelope (0.1-10V; unpatched = always on)");
		const char* cvNames[] = {nullptr, "Glide", "Vibrato depth", "Vibrato rate", "Vowel", "Nasal", "Larynx", "Tension", "Brightness", "Breathiness", "Roughness", "Fry"};
		for (int i = GLIDE_INPUT; i <= FRY_INPUT; i++)
			configInput(i, std::string(cvNames[i]) + " CV (10V = full knob range)");
		configOutput(AUDIO_OUTPUT, "Voice");
		worker = std::thread([this] { work(); });
	}

	~Phona() {
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
		v = clamp(v, q->minValue, q->maxValue);
		modShown[param] = v;
		return v;
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
			modShown[PITCH_PARAM] = params[PITCH_PARAM].getValue() + 12.f * inputs[VOCT_INPUT].getVoltage(); // the arc clamps it
			p.f0 = dsp::FREQ_C4 * std::pow(2.f, semis / 12.f + inputs[VOCT_INPUT].getVoltage());
			p.vowel = knobCv(VOWEL_PARAM, VOWEL_INPUT, VOWEL_ATT_PARAM);
			p.pressure = 8.0 * knobCv(EFFORT_PARAM, EFFORT_INPUT, EFFORT_ATT_PARAM); // Rust side clamps and compensates
			p.breathiness = knobCv(BREATHY_PARAM, BREATHY_INPUT, BREATHY_ATT_PARAM);
			p.tension = knobCv(TENSION_PARAM, TENSION_INPUT, TENSION_ATT_PARAM);
			p.brightness = knobCv(BRIGHT_PARAM, BRIGHT_INPUT, BRIGHT_ATT_PARAM);
			p.roughness = knobCv(ROUGH_PARAM, ROUGH_INPUT, ROUGH_ATT_PARAM);
			p.vib_depth = knobCv(VIBDEPTH_PARAM, VIBDEPTH_INPUT, VIBDEPTH_ATT_PARAM);
			p.vib_rate = knobCv(VIBRATE_PARAM, VIBRATE_INPUT, VIBRATE_ATT_PARAM);
			p.gate = gate;
			p.glide = 0.002 * std::pow(500.f, knobCv(GLIDE_PARAM, GLIDE_INPUT, GLIDE_ATT_PARAM));
			p.fry = knobCv(FRY_PARAM, FRY_INPUT, FRY_ATT_PARAM);
			p.velum = -0.25 + 1.65 * knobCv(NASAL_PARAM, NASAL_INPUT, NASAL_ATT_PARAM);
			p.larynx = knobCv(LARYNX_PARAM, LARYNX_INPUT, LARYNX_ATT_PARAM);
			p.ventricular = 0.0; // FALSE FOLDS control removed (subtle effect)
			// SOURCE only acts with a cable in EXT IN: unpatched, the folds always sing
			float source = knobCv(SOURCE_PARAM, SOURCE_INPUT, SOURCE_ATT_PARAM);
			if (!inputs[EXT_INPUT].isConnected()) source = 0.f;
			sourceShown = source;
			chim_set_ext(voice, source);
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

// Panel colours. The same values are in tools/panel.py, which writes res/Phona.svg.
static const NVGcolor INK = nvgRGB(0x24, 0x21, 0x1d);
static const NVGcolor AMBER = nvgRGB(0xd9, 0x8a, 0x1f);
static const NVGcolor MOD_BLUE = nvgRGB(0x1f, 0x7a, 0xd9);
static const NVGcolor CARD_BODY = nvgRGB(0xec, 0xe7, 0xdd);
static const NVGcolor CARD_HEAD = nvgRGB(0xd8, 0xd0, 0xc1);

static std::shared_ptr<window::Font> panelFont() {
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

// What a knob's value display shows.
enum Readout { R_NOTE, R_FINE, R_MS, R_CENTS, R_HZ, R_VOWEL, R_PCT, R_CM, R_NUM };

static std::string readoutText(int kind, float v) {
	switch (kind) {
		case R_NOTE: {
			static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
			int midi = 60 + (int) std::round(v);
			return string::f("%s%d", names[((midi % 12) + 12) % 12], (int) std::floor(midi / 12.f) - 1);
		}
		case R_FINE: {
			int c = (int) std::round(v * 100.f);
			return c == 0 ? "0 ct" : string::f("%+d ct", c);
		}
		case R_MS: {
			float ms = 2.f * std::pow(500.f, v);
			return ms < 999.5f ? string::f("%.0f ms", ms) : string::f("%.1f s", ms / 1000.f);
		}
		case R_CENTS: return string::f("%.0f ct", v);
		case R_HZ: return string::f("%.1f Hz", v);
		case R_VOWEL: return std::string(1, "IEAOU"[clamp((int) std::round(v), 0, 4)]);
		case R_PCT: return string::f("%.0f%%", v * 100.f);
		case R_CM: return std::fabs(v) < 0.05f ? "0.0 cm" : string::f("%+.1f cm", v);
		default: return string::f("%.2f", v);
	}
}

// The knob's setting in a small plate under the knob. With a cable in the knob's CV input it shows
// the value in use (knob + CV) in blue. VOWEL has a dark plate with the vowel letter.
struct ValueDisplay : TransparentWidget {
	Phona* module = nullptr;
	int param = 0, input = -1, kind = R_NUM;
	float def = 0.f; // shown in the module browser, where there is no module
	void draw(const DrawArgs& args) override {
		float v = module ? module->params[param].getValue() : def;
		bool live = module && input >= 0 && module->inputs[input].isConnected();
		if (live) v = module->modShown[param];
		bool dark = kind == R_VOWEL;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, box.size.y / 2.f);
		nvgFillColor(args.vg, dark ? INK : CARD_HEAD);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = panelFont();
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, dark ? 7.5f : 6.3f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		if (live) nvgFillColor(args.vg, dark ? nvgRGB(0x6c, 0xb4, 0xf5) : nvgRGB(0x16, 0x5c, 0xa8));
		else nvgFillColor(args.vg, dark ? nvgRGB(0xf0, 0xa8, 0x3c) : INK);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 0.3f, readoutText(kind, v).c_str(), NULL);
	}
};

// Amber arc around a knob, from the minimum (or from the centre, for a knob with a +/- range) to
// the knob's setting, over a faint track of the full travel. With a cable in the knob's CV input,
// a blue arc goes from the knob's setting to the value in use (knob + CV), so it shows how far
// and in which direction the modulation moves the control at this moment.
struct ValueArc : TransparentWidget {
	Phona* module = nullptr;
	int param = 0, input = -1;
	float mn = 0.f, mx = 1.f, def = 0.f; // used when there is no module
	float radius = 10.f;
	void draw(const DrawArgs& args) override {
		float lo = mn, hi = mx, v = def;
		if (module) {
			ParamQuantity* q = module->paramQuantities[param];
			lo = q->minValue;
			hi = q->maxValue;
			v = module->params[param].getValue();
		}
		// same travel as Rack's RoundKnob; NanoVG angles start at 3 o'clock
		const float A0 = -0.83f * M_PI - 0.5f * M_PI, A1 = 0.83f * M_PI - 0.5f * M_PI;
		float t = clamp((v - lo) / (hi - lo), 0.f, 1.f);
		float from = lo == -hi ? 0.5f : 0.f;
		nvgLineCap(args.vg, NVG_ROUND);
		nvgBeginPath(args.vg);
		nvgArc(args.vg, 0, 0, radius, A0, A1, NVG_CW);
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStrokeColor(args.vg, nvgRGB(0xd0, 0xc8, 0xb8));
		nvgStroke(args.vg);
		if (std::fabs(t - from) > 0.004f) {
			float a = A0 + (A1 - A0) * std::min(t, from), b = A0 + (A1 - A0) * std::max(t, from);
			nvgBeginPath(args.vg);
			nvgArc(args.vg, 0, 0, radius, a, b, NVG_CW);
			nvgStrokeColor(args.vg, AMBER);
			nvgStroke(args.vg);
		}
		if (module && input >= 0 && module->inputs[input].isConnected()) {
			float m = clamp((module->modShown[param] - lo) / (hi - lo), 0.f, 1.f);
			float am = A0 + (A1 - A0) * m;
			if (std::fabs(m - t) > 0.004f) {
				float a = A0 + (A1 - A0) * std::min(t, m), b = A0 + (A1 - A0) * std::max(t, m);
				nvgBeginPath(args.vg);
				nvgArc(args.vg, 0, 0, radius, a, b, NVG_CW);
				nvgStrokeColor(args.vg, MOD_BLUE);
				nvgStroke(args.vg);
			}
			// the end mark stays visible when the modulation is zero
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, radius * std::cos(am), radius * std::sin(am), 1.5f);
			nvgFillColor(args.vg, MOD_BLUE);
			nvgFill(args.vg);
		}
		nvgLineCap(args.vg, NVG_BUTT);
	}
};

// Greys out a control that acts only on the vocal folds, by the share of the sound that comes
// from EXT IN instead (a wash of the panel colour over the label, knob and jack). The controls
// still work: at SOURCE 50% they act on the fold half of the sound.
struct DimOverlay : TransparentWidget {
	Phona* module = nullptr;
	bool whenUnpatched = false; // SOURCE: the opposite case, no effect without a cable in EXT IN
	void draw(const DrawArgs& args) override {
		float amount = 1.f;
		if (module) {
			bool ext = module->inputs[Phona::EXT_INPUT].isConnected();
			if (ext == whenUnpatched) return;
			if (!whenUnpatched) amount = module->sourceShown; // knob + CV
		}
		else if (whenUnpatched || !getenv("CHIMERA_DIM_PREVIEW")) {
			return; // module browser / screenshot: not dimmed (set the variable to check the overlays)
		}
		if (amount <= 0.f) return;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgTransRGBAf(CARD_BODY, 0.72f * amount));
		nvgFill(args.vg);
	}
};

struct PhonaWidget : ModuleWidget {
	// Panel geometry (mm), the same numbers as in tools/panel.py. A dark title bar, then four
	// bands of equal height. Each band holds one card: a header strip with the section name,
	// then the cells.
	static constexpr float ROW0 = 8.6f, ROW_H = 29.9f;
	static constexpr float CARD_X1 = 100.1f, HEAD_Y = 2.15f;
	static constexpr float LABEL_Y = 5.8f, KNOB_Y = 12.6f, VALUE_Y = 19.3f, JACK_Y = 25.1f; // within a band
	static float cellLeft(int row) { (void) row; return 1.5f; /* see LEFT in tools/panel.py */ }
	static float cellW(int row, int n) { return (CARD_X1 - cellLeft(row)) / n; }
	static float cellX(int row, int n, int col) { return cellLeft(row) + cellW(row, n) * (col + 0.5f); }

	// fold-only controls are washed with the card colour when EXT IN takes over
	void addDim(Phona* module, int row, float x0, float x1, bool whenUnpatched = false) {
		DimOverlay* d = new DimOverlay;
		d->module = module;
		d->whenUnpatched = whenUnpatched;
		d->box.pos = mm2px(Vec(x0, ROW0 + row * ROW_H + 4.2f));
		d->box.size = mm2px(Vec(x1 - x0, 25.2f));
		addChild(d);
	}

	void addLabel(float x, float y, const char* text) {
		addChild(new PanelLabel(mm2px(Vec(x, y)), text));
	}

	PhonaWidget(Phona* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Phona.svg")));

		PanelLabel* title = new PanelLabel(mm2px(Vec(50.8, 4.1)), "PHONA", 14.f);
		title->color = nvgRGB(0xf3, 0xeb, 0xdc);
		title->spacing = 3.f;
		addChild(title);
		addChild(createLightCentered<SmallLight<GreenRedLight>>(mm2px(Vec(96.5, 4.1)), module, Phona::STATUS_LIGHT));

		const char* sections[4] = {"PITCH", "VOCAL TRACT", "VOCAL FOLDS", "BREATH / EXTERNAL / OUT"};
		for (int r = 0; r < 4; r++) {
			PanelLabel* s = new PanelLabel(mm2px(Vec(8.f, ROW0 + r * ROW_H + HEAD_Y + 0.1f)), sections[r], 6.6f);
			s->color = nvgRGB(0xf3, 0xeb, 0xdc);
			s->align = NVG_ALIGN_LEFT;
			s->spacing = 0.8f;
			addChild(s);
		}

		// A cell is a column: label, knob with its value arc, value display, then the CV jack
		// (left) with the CV amount trimmer (right). It is the same in every cell, so what
		// belongs together is always stacked under one label. PITCH has a plain V/OCT jack,
		// FINE has no CV. `n` = cells in the band (they share the width evenly), `col` =
		// position. `mn, mx, def` repeat the param's range for the module browser only.
		struct Cell { int row, n, col, param, input, att; const char* name; bool big; int kind; float mn, mx, def; };
		const Cell cells[] = {
			{0, 5, 0, Phona::PITCH_PARAM, Phona::VOCT_INPUT, -1, "PITCH", true, R_NOTE, -36.f, 36.f, -12.f},
			{0, 5, 1, Phona::FINE_PARAM, -1, -1, "FINE", false, R_FINE, -1.f, 1.f, 0.f},
			{0, 5, 2, Phona::GLIDE_PARAM, Phona::GLIDE_INPUT, Phona::GLIDE_ATT_PARAM, "GLIDE", false, R_MS, 0.f, 1.f, 0.547f},
			{0, 5, 3, Phona::VIBDEPTH_PARAM, Phona::VIBDEPTH_INPUT, Phona::VIBDEPTH_ATT_PARAM, "VIB DEPTH", false, R_CENTS, 0.f, 200.f, 0.f},
			{0, 5, 4, Phona::VIBRATE_PARAM, Phona::VIBRATE_INPUT, Phona::VIBRATE_ATT_PARAM, "VIB RATE", false, R_HZ, 0.5f, 12.f, 5.5f},
			{1, 4, 0, Phona::VOWEL_PARAM, Phona::VOWEL_INPUT, Phona::VOWEL_ATT_PARAM, "VOWEL", true, R_VOWEL, 0.f, 4.f, 2.f},
			{1, 4, 1, Phona::NASAL_PARAM, Phona::NASAL_INPUT, Phona::NASAL_ATT_PARAM, "NASAL", false, R_PCT, 0.f, 1.f, 0.06f},
			{1, 4, 2, Phona::LARYNX_PARAM, Phona::LARYNX_INPUT, Phona::LARYNX_ATT_PARAM, "LARYNX", false, R_CM, -1.6f, 1.6f, 0.f},
			{2, 5, 0, Phona::TENSION_PARAM, Phona::TENSION_INPUT, Phona::TENSION_ATT_PARAM, "TENSION", false, R_NUM, -0.5f, 1.5f, 0.5f},
			{2, 5, 1, Phona::BRIGHT_PARAM, Phona::BRIGHT_INPUT, Phona::BRIGHT_ATT_PARAM, "BRIGHT", false, R_NUM, -0.5f, 1.5f, 0.25f},
			{2, 5, 2, Phona::BREATHY_PARAM, Phona::BREATHY_INPUT, Phona::BREATHY_ATT_PARAM, "BREATHY", false, R_NUM, 0.f, 1.5f, 0.15f},
			{2, 5, 3, Phona::ROUGH_PARAM, Phona::ROUGH_INPUT, Phona::ROUGH_ATT_PARAM, "ROUGH", false, R_NUM, 0.f, 2.f, 0.1f},
			{2, 5, 4, Phona::FRY_PARAM, Phona::FRY_INPUT, Phona::FRY_ATT_PARAM, "FRY", false, R_PCT, 0.f, 1.f, 0.f},
			{3, 5, 0, Phona::EFFORT_PARAM, Phona::EFFORT_INPUT, Phona::EFFORT_ATT_PARAM, "EFFORT", false, R_PCT, 0.25f, 2.f, 1.f},
			{3, 5, 3, Phona::SOURCE_PARAM, Phona::SOURCE_INPUT, Phona::SOURCE_ATT_PARAM, "SOURCE", false, R_PCT, 0.f, 1.f, 1.f},
		};
		for (const Cell& c : cells) {
			float x = cellX(c.row, c.n, c.col);
			float y = ROW0 + c.row * ROW_H;
			addLabel(x, y + LABEL_Y, c.name);

			ValueArc* arc = new ValueArc;
			arc->module = module;
			arc->param = c.param;
			arc->input = c.input;
			arc->mn = c.mn;
			arc->mx = c.mx;
			arc->def = c.def;
			arc->radius = mm2px(c.big ? 5.65f : 4.95f);
			arc->box.pos = mm2px(Vec(x, y + KNOB_Y));
			addChild(arc);
			if (c.big)
				addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y + KNOB_Y)), module, c.param));
			else
				addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y + KNOB_Y)), module, c.param));

			ValueDisplay* disp = new ValueDisplay;
			disp->module = module;
			disp->param = c.param;
			disp->input = c.input;
			disp->kind = c.kind;
			disp->def = c.def;
			Vec ds = c.kind == R_VOWEL ? Vec(6.f, 2.9f) : Vec(9.6f, 2.5f);
			disp->box.pos = mm2px(Vec(x - ds.x / 2.f, y + VALUE_Y - ds.y / 2.f));
			disp->box.size = mm2px(ds);
			addChild(disp);

			if (c.input < 0) continue;
			if (c.att >= 0) {
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x - 4.4f, y + JACK_Y)), module, c.input));
				addParam(createParamCentered<Trimpot>(mm2px(Vec(x + 4.8f, y + JACK_Y)), module, c.att));
			}
			else {
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y + JACK_Y)), module, c.input));
			}
		}
		// cells without a knob: the switch or the jack sits mid-height under its label
		float midY = 16.5f;
		float y1 = ROW0 + 1 * ROW_H, y3 = ROW0 + 3 * ROW_H;
		float bx = cellX(1, 4, 3);
		addLabel(bx, y1 + LABEL_Y, "BODY");
		addParam(createParamCentered<CKSS>(mm2px(Vec(bx, y1 + midY)), module, Phona::BODY_PARAM));
		addLabel(bx + 5.f, y1 + midY - 2.6f, "F"); // CKSS: up = 1 = female
		addLabel(bx + 5.f, y1 + midY + 2.6f, "M");
		addLabel(cellX(3, 5, 1), y3 + LABEL_Y, "GATE");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cellX(3, 5, 1), y3 + midY)), module, Phona::GATE_INPUT));
		addLabel(cellX(3, 5, 2), y3 + LABEL_Y, "EXT IN");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cellX(3, 5, 2), y3 + midY)), module, Phona::EXT_INPUT));
		addLabel(cellX(3, 5, 4), y3 + LABEL_Y, "OUT");
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(cellX(3, 5, 4), y3 + midY)), module, Phona::AUDIO_OUTPUT));

		// added last so they draw over the controls: the pitch band, the folds band except
		// BREATHY, and EFFORT
		addDim(module, 0, 1.7f, 99.9f);
		float w = cellW(2, 5);
		for (int col : {0, 1, 3, 4}) {
			float x = cellX(2, 5, col);
			addDim(module, 2, x - w / 2.f + 0.2f, x + w / 2.f - 0.2f);
		}
		addDim(module, 3, 1.7f, 1.5f + cellW(3, 5) - 0.2f);
		// SOURCE is greyed out the other way round: when nothing is patched into EXT IN
		float sx = cellX(3, 5, 3);
		addDim(module, 3, sx - w / 2.f + 0.2f, sx + w / 2.f - 0.2f, true);
	}
};

Model* modelPhona = createModel<Phona, PhonaWidget>("Phona");
