#pragma once
#include "plugin.hpp"
#include "PhonaTap.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

// Phona Script: 8 lyric phrases for the Phona beside it (also with other Phona expanders between).
// Phona sings one phrase at a time and repeats it. NEXT, RESET, SELECT and a click on a phrase choose
// the phrase; the change takes effect at the next gate (the syllable that sings now finishes).
// While a Script is attached, its phrases replace Phona's own lyrics text.
//
// Threads: the phrase texts belong to the UI thread (menu, JSON). Each change is copied into `pub`
// under a sequence lock, and Phona's audio thread copies it from there (no locks, no allocation).
// The phrase choice (`want`) is made here in process(); Phona reads it when a gate opens and
// writes back the phrase it sings (`current`), for the display.
struct PhonaScript : Module {
	static constexpr int SLOTS = 8;
	static constexpr int LEN = 256; // bytes per phrase, with the closing 0
	enum ParamId { PARAMS_LEN };
	enum InputId { NEXT_INPUT, RESET_INPUT, SELECT_INPUT, INPUTS_LEN };
	enum LightId { LINK_LIGHT, LIGHTS_LEN };

	std::mutex textMutex;        // guards `text` (UI thread; JSON may come from another thread)
	std::string text[SLOTS];
	char pub[SLOTS][LEN] = {};   // the published copy, for Phona's audio thread
	std::atomic<unsigned> pubSeq{0}; // odd while `pub` is written
	std::atomic<unsigned> mask{0};   // bit i: phrase i has text

	std::atomic<int> want{0};        // the phrase the next new syllable comes from
	std::atomic<unsigned> resetSeq{0}; // +1 for each RESET: Phona starts the phrase again from syllable 1
	std::atomic<int> current{-1};    // written by Phona: the phrase it sings (-1 = none yet)
	std::atomic<int> clickReq{-1};   // UI thread: a phrase clicked on the panel
	dsp::SchmittTrigger nextTrigger, resetTrigger;
	int lastSelect = -1;

	PhonaScript() {
		config(PARAMS_LEN, INPUTS_LEN, 0, LIGHTS_LEN);
		configInput(NEXT_INPUT, "Next phrase (trigger; empty phrases are skipped)");
		configInput(RESET_INPUT, "Reset (trigger): phrase 1, syllable 1");
		configInput(SELECT_INPUT, "Select phrase (0 to 10 V = phrase 1 to 8, 1.25 V each)");
	}

	// UI thread
	void setPhrase(int i, const std::string& s) {
		if (i < 0 || i >= SLOTS) return;
		std::lock_guard<std::mutex> lock(textMutex);
		if (text[i] == s) return;
		text[i] = s;
		publishLocked();
	}
	std::string phrase(int i) {
		std::lock_guard<std::mutex> lock(textMutex);
		return text[i];
	}
	void publishLocked() {
		unsigned m = 0;
		pubSeq.fetch_add(1, std::memory_order_acq_rel); // odd: being written
		std::atomic_thread_fence(std::memory_order_release);
		for (int i = 0; i < SLOTS; i++) {
			std::memset(pub[i], 0, LEN);
			std::strncpy(pub[i], text[i].c_str(), LEN - 1);
			if (hasWords(text[i])) m |= 1u << i;
		}
		std::atomic_thread_fence(std::memory_order_release);
		pubSeq.fetch_add(1, std::memory_order_release); // even: stable
		mask = m;
	}
	static bool hasWords(const std::string& s) { return s.find_first_not_of(" \t\r\n") != std::string::npos; }

	// The next phrase with text after `i` (wraps); `i` itself when no other has text.
	static int nextFilled(int i, unsigned m) {
		for (int k = 1; k <= SLOTS; k++) {
			int j = (i + k) % SLOTS;
			if (m & (1u << j)) return j;
		}
		return i;
	}
	static int firstFilled(unsigned m) {
		for (int j = 0; j < SLOTS; j++) if (m & (1u << j)) return j;
		return 0;
	}

	void process(const ProcessArgs& args) override {
		bool on = familyNear(this, modelPhona) != nullptr;
		lights[LINK_LIGHT].setBrightnessSmooth(on ? 1.f : 0.f, args.sampleTime);
		unsigned m = mask.load();
		int w = want.load();
		int click = clickReq.exchange(-1);
		if (click >= 0 && (m & (1u << click))) w = click;
		if (inputs[SELECT_INPUT].isConnected()) {
			int sel = clamp((int) std::floor(inputs[SELECT_INPUT].getVoltage() / 1.25f), 0, SLOTS - 1);
			if (sel != lastSelect && (m & (1u << sel))) {
				lastSelect = sel;
				w = sel;
			}
		}
		else lastSelect = -1;
		if (nextTrigger.process(inputs[NEXT_INPUT].getVoltage(), 0.1f, 1.f)) w = nextFilled(w, m);
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			w = firstFilled(m);
			resetSeq++;
		}
		want = w;
	}

	void onReset() override {
		std::lock_guard<std::mutex> lock(textMutex);
		for (std::string& t : text) t.clear();
		publishLocked();
		want = 0;
		resetSeq++;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* arr = json_array();
		{
			std::lock_guard<std::mutex> lock(textMutex);
			for (const std::string& t : text) json_array_append_new(arr, json_string(t.c_str()));
		}
		json_object_set_new(root, "phrases", arr);
		json_object_set_new(root, "phrase", json_integer(want.load()));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* arr = json_object_get(root, "phrases");
		{
			std::lock_guard<std::mutex> lock(textMutex);
			for (int i = 0; i < SLOTS; i++) {
				json_t* t = arr ? json_array_get(arr, i) : nullptr;
				text[i] = t && json_is_string(t) ? json_string_value(t) : "";
			}
			publishLocked();
		}
		json_t* p = json_object_get(root, "phrase");
		if (p && json_is_integer(p)) want = clamp((int) json_integer_value(p), 0, SLOTS - 1);
	}
};
