#pragma once
#include "plugin.hpp"
#include "chimera_rack.h"
#include <atomic>
#include <cstring>

// Phona's display expanders (Phona View: the animated mouth and throat; Phona Scope: the
// instrument display). They have no audio and no cables. Phona finds them beside itself, also
// with other Phona expanders between (Lab, View, Scope in any order, on either side), captures the
// voice each one shows about 40 times a second on the audio thread, and writes the snapshot into
// the expander's triple buffer. The expander's widget reads the newest snapshot on the UI thread.

// Phona's expanders: a chain of them may stand between Phona and the one it looks for.
static inline bool phonaFamily(Module* m) {
	return m && m->model && (m->model == modelPhonaLab || m->model == modelPhonaView || m->model == modelPhonaScope);
}
// The module of model `want` beside `m`, or further along a chain of Phona expanders.
static inline Module* familyNear(Module* m, Model* want) {
	if (!want) return nullptr;
	for (int dir = 0; dir < 2; dir++) {
		Module* cur = dir ? m->rightExpander.module : m->leftExpander.module;
		for (int hop = 0; hop < 4 && cur; hop++) {
			if (cur->model == want) return cur;
			if (!phonaFamily(cur)) break;
			cur = dir ? cur->rightExpander.module : cur->leftExpander.module;
		}
	}
	return nullptr;
}

struct PhonaTap : Module {
	enum ParamId { VOICE_PARAM, PARAMS_LEN }; // 0 = follow the newest note, 1..4 = that voice
	enum LightId { LINK_LIGHT, LIGHTS_LEN };
	static constexpr int AUDIO_N = 4096;

	// What Phona sends: the voice's snapshot plus what only Phona knows.
	struct Frame {
		ChimVis vis;
		float audio[AUDIO_N]; // the voice's last output samples (volts), oldest first
		float rate = 48000.f;
		int voice = 0;       // the voice shown (0-based)
		int voices = 0;      // voices built (the polyphony menu's maximum)
		int sounding = 0;    // voices that sound (the V/OCT or GATE channel count)
		bool lyrics = false; // lyrics mode in use
		bool beat = false;   // beatbox mode: not shown yet
		bool female = false; // BODY switch
		char syllable[32] = {};
		unsigned seq = 0;
	};

	// Triple buffer: the writer (audio thread) fills `back`, then swaps it with `mid` and marks it
	// new; the reader (UI thread) swaps `front` with `mid` when it is new. Neither waits.
	Frame buf[3];
	int back = 0;                // audio thread
	int front = 2;               // UI thread
	std::atomic<int> mid{1};     // index | NEW
	static constexpr int NEW = 4;
	std::atomic<bool> linked{false};
	int uiVoices = 0, uiShown = -1; // UI thread: what the display shows, for the voice selector
	unsigned seq = 0;

	PhonaTap() {
		config(PARAMS_LEN, 0, 0, LIGHTS_LEN);
		configSwitch(VOICE_PARAM, 0.f, 4.f, 1.f, "Voice shown", {"Follow the newest note", "Voice 1", "Voice 2", "Voice 3", "Voice 4"});
		for (Frame& f : buf) {
			std::memset(&f.vis, 0, sizeof(ChimVis));
			std::memset(f.audio, 0, sizeof(f.audio));
		}
	}

	// Audio thread (Phona's process): the slot to fill, then publish().
	Frame& writeSlot() { return buf[back]; }
	void publish() {
		buf[back].seq = ++seq;
		back = mid.exchange(back | NEW) & 3;
	}
	// UI thread: the newest complete snapshot.
	const Frame& read() {
		if (mid.load() & NEW) front = mid.exchange(front) & 3;
		return buf[front];
	}

	void process(const ProcessArgs& args) override {
		bool on = familyNear(this, modelPhona) != nullptr;
		linked = on;
		lights[LINK_LIGHT].setBrightnessSmooth(on ? 1.f : 0.f, args.sampleTime);
	}
};

struct PhonaView : PhonaTap {};
struct PhonaScope : PhonaTap {};
