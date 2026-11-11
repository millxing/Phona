#pragma once
#include "plugin.hpp"
#include "chimera_rack.h"

// Phona Lab: a tuning tool, not an instrument. Put it next to a Phona and its knobs set how that
// Phona makes the consonants of its lyrics (the tunable values, T_* in rust/src/lib.rs). The
// values found by ear become Phona's defaults. It has no audio and no cables: Phona reads the
// knobs of the module beside it.
struct PhonaLab : Module {
	// Param ids = indices of the tunable values.
	enum ParamId {
		STOP_BREATH_PARAM, STOP_HISS_PARAM, STOP_CUT_PARAM, RELEASE_PARAM,
		FADE_START_PARAM, FADE_TIME_PARAM, EXHALE_PARAM, CODA_LEN_PARAM,
		ONSET_LEN_PARAM, ONSET_HISS_PARAM, VOICELESS_PARAM, STRENGTH_PARAM,
		MIN_VOWEL_PARAM, NEXT_WAIT_PARAM, LEVEL_PARAM, LIMITER_PARAM,
		ONSET_BREATH_PARAM, THUMP_PARAM,
		PARAMS_LEN
	};
	enum LightId { LINK_LIGHT, LIGHTS_LEN };
	enum Unit { U_NUM, U_X, U_MS, U_CUT, U_SWITCH };

	struct Spec {
		const char* key;   // name in the copied text
		const char* label; // on the panel
		const char* tip;
		float mn, mx, def;
		int unit;
		bool plan;         // shapes a syllable's plan (Phona plans the lyrics again when it changes)
	};
	// Where each control is on the panel: four rows, param ids in panel order.
	static constexpr int ROWS = 4;
	static int rowLen(int r) { return r < 2 ? 4 : 5; }
	static int cellParam(int r, int c) {
		static const int cells[ROWS][5] = {
			{STOP_BREATH_PARAM, STOP_HISS_PARAM, STOP_CUT_PARAM, RELEASE_PARAM, -1},
			{FADE_START_PARAM, FADE_TIME_PARAM, EXHALE_PARAM, CODA_LEN_PARAM, -1},
			{ONSET_BREATH_PARAM, ONSET_HISS_PARAM, ONSET_LEN_PARAM, VOICELESS_PARAM, STRENGTH_PARAM},
			{MIN_VOWEL_PARAM, NEXT_WAIT_PARAM, LEVEL_PARAM, THUMP_PARAM, LIMITER_PARAM},
		};
		return cells[r][c];
	}
	static const Spec& spec(int i) {
		// `def` = TUNE_DEFAULT in rust/src/lib.rs
		static const Spec specs[PARAMS_LEN] = {
			{"stopBreath", "BREATH", "Last p / t / k: breath while the mouth is closed (less = weaker pop and hiss)", 0.f, 1.f, 0.5f, U_NUM, true},
			{"stopHiss", "HISS", "Last p / t / k: how far the folds open after the release (hiss)", 0.f, 3.f, 1.f, U_X, true},
			{"stopCut", "CUT", "Last p / t / k: the output fades out this long after the mouth starts to open (full right = no fade)", 0.f, 155.f, 155.f, U_CUT, true},
			{"release", "RELEASE", "Last consonant: how slowly the mouth opens again", 0.25f, 4.f, 1.f, U_X, true},
			{"fadeStart", "FADE START", "Other last consonants (m, n, l, s, v, ...): when the output fades out, from the moment the mouth starts to open", -30.f, 60.f, 0.f, U_MS, true},
			{"fadeTime", "FADE TIME", "How fast that fade is", 1.f, 50.f, 4.f, U_MS, false},
			{"exhale", "EXHALE", "The breath out after a last consonant (0 = none)", 0.f, 2.f, 1.f, U_X, true},
			{"codaLen", "LENGTH", "Length of the consonants after the vowel", 0.5f, 2.f, 1.f, U_X, true},
			{"onsetLen", "LENGTH", "Length of the consonants before the vowel", 0.5f, 2.f, 1.f, U_X, true},
			{"onsetHiss", "HISS", "First p / t / k: how far the folds open after the release (hiss)", 0.f, 2.f, 1.f, U_X, true},
			{"voiceless", "S F SH", "How far the folds open for s, f, sh, th, h (less = more voiced, more = more air)", 0.f, 2.f, 1.f, U_X, true},
			{"strength", "STRENGTH", "How firmly all consonants close the mouth", 0.25f, 2.f, 1.f, U_X, true},
			{"minVowel", "MIN VOWEL", "A vowel sounds at least this long, also with a very short gate", 0.f, 200.f, 70.f, U_MS, false},
			{"nextWait", "NEXT WAIT", "How long the next syllable waits for the last consonant of the one before (0 = no wait)", 0.f, 1.5f, 1.f, U_X, false},
			{"level", "LEVEL", "Level of sung words", 0.5f, 3.f, 1.6f, U_X, false},
			{"limiter", "LIMIT", "Engine limiter for sung words", 0.f, 1.f, 1.f, U_SWITCH, false},
			{"onsetBreath", "BREATH", "First p / t / k: breath while the mouth is closed (less = weaker pop)", 0.f, 1.f, 1.f, U_NUM, true},
			{"thump", "THUMP CUT", "Removes sound below the note's pitch (the low pop of p / t / k). 0 = off; 0.7 = below 0.7 x the pitch", 0.f, 1.5f, 0.f, U_NUM, false},
		};
		return specs[i];
	}

	PhonaLab() {
		config(PARAMS_LEN, 0, 0, LIGHTS_LEN);
		for (int i = 0; i < PARAMS_LEN; i++) {
			const Spec& s = spec(i);
			if (s.unit == U_SWITCH) configSwitch(i, 0.f, 1.f, s.def, s.tip, {"Off", "On"});
			else configParam(i, s.mn, s.mx, s.def, s.tip, s.unit == U_MS || s.unit == U_CUT ? " ms" : "");
		}
	}

	static bool isPhona(Module* m) { return m && m->model == modelPhona; }

	void process(const ProcessArgs& args) override {
		bool linked = isPhona(leftExpander.module) || isPhona(rightExpander.module);
		lights[LINK_LIGHT].setBrightnessSmooth(linked ? 1.f : 0.f, args.sampleTime);
	}

	// All values as one line of text, to send to the developer.
	std::string valuesText() {
		std::string out = "PhonaLab";
		for (int i = 0; i < PARAMS_LEN; i++)
			out += string::f(" %s=%.3g", spec(i).key, params[i].getValue());
		return out;
	}
	// Sets the values named in such a line; the others stay. Returns how many it set.
	int fromText(const std::string& text) {
		int n = 0;
		size_t pos = 0;
		while (pos < text.size()) {
			size_t end = text.find_first_of(" \t\r\n,", pos);
			if (end == std::string::npos) end = text.size();
			std::string tok = text.substr(pos, end - pos);
			pos = end + 1;
			size_t eq = tok.find('=');
			if (eq == std::string::npos) continue;
			std::string key = tok.substr(0, eq);
			for (int i = 0; i < PARAMS_LEN; i++) {
				if (key != spec(i).key) continue;
				char* e = nullptr;
				float v = std::strtof(tok.c_str() + eq + 1, &e);
				if (e != tok.c_str() + eq + 1 && std::isfinite(v)) {
					params[i].setValue(clamp(v, spec(i).mn, spec(i).mx));
					n++;
				}
			}
		}
		return n;
	}
};
