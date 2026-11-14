#include "plugin.hpp"
#include "chimera_rack.h"
#include "panel.hpp"
#include "PhonaLab.hpp"
#include "PhonaTap.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <array>

// Prototype singing voice. Up to MAX_VOICES Chimera voices per module (polyphony). The menu sets
// the maximum; the channel count of the V/OCT or GATE cable sets how many voices sound.
//
// Real-time rules: the audio thread only calls chim_set / chim_render. Building voices (sample
// rate, body or voice-count change) allocates, so a worker thread builds them and hands them over
// through an atomic pointer; the old voices go back to the worker to be freed.
struct Phona : Module {
	static constexpr int MAX_VOICES = 4;
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
		GATE_INPUT, EFFORT_INPUT, EXT_INPUT, SOURCE_INPUT, RESET_INPUT, INPUTS_LEN
	};
	enum OutputId { AUDIO_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(STATUS_LIGHT, 2), LIGHTS_LEN }; // green = sounding, red = fault

	// A gate input below this voltage is closed (a small DC offset must not hold the voice open).
	static constexpr float GATE_THRESHOLD = 0.1f;

	// The voices plus what they were built for. Immutable once published.
	struct Built {
		ChimVoice* voice[MAX_VOICES];
		int n; // voices built (the menu's maximum when it was built)
		int body;
		float rate;
	};
	// Audio-thread state of one voice
	struct Lane {
		bool lyr = false;      // lyrics mode as last sent to this voice
		bool lyrGate = false;  // gate state at the last sample (lyrics and beat edges)
		int lyrDelay = 0;      // samples until a gate that opened starts its syllable
		float lastGate = 0.f;
		int gateHold = 0;      // samples a gate onset is held open so the engine's control tick sees it
		float heldGate = 0.f;
		float silentFor = 0.f; // seconds of silence with gate closed
		bool idle = false;
		bool dead = false;     // this voice hit a panic / non-finite output
		char syllable[32] = {}; // the syllable this voice sings, for Phona View
	};

	// Lyrics turned into syllable plans, for one body. Built by the worker (it allocates), handed
	// over like a voice. `l` is null for an empty text.
	struct BuiltLyr {
		ChimLyrics* l;
		std::vector<int> beats; // worker-owned storage; read only on audio thread
		bool beat;
		int version;
		int body;
		int n;
		bool keep; // same text and body as the build before (only the Lab's values changed): the song goes on
		std::vector<std::array<char, 32>> texts; // each syllable's spelling, for Phona View (audio thread reads)
	};
	std::mutex lyricsMutex;                // guards `lyrics` and `sylTexts` (UI and worker threads only)
	std::string lyrics;
	std::vector<std::string> sylTexts;     // spelling of each syllable, for the panel
	std::atomic<int> lyricsVersion{0};
	std::atomic<bool> beatMode{false};
	std::atomic<int> beatShown{-1}; // -1 waiting, 0 rest, 1 B, 2 Pf, 3 t, 4 invalid
	bool voiceBeat = false;
	// Delay each edge, including multiple edges inside 1ms. Fixed storage covers the
	// engine's supported sample rates (up to 768kHz); no queue allocations.
	std::array<bool, 1024> beatEdges{};
	unsigned beatTick = 0;
	int beatVersion = -1;
	BuiltLyr* activeLyr = nullptr;         // owned by the audio thread
	BuiltLyr* lyrPending = nullptr;        // taken from the worker, waits for the held syllable to end
	// Tunable values (Phona Lab, an expander next to this module; defaults without it)
	double tuneCur[CHIM_TUNE_N];           // audio thread
	std::atomic<double> tuneShared[CHIM_TUNE_N]; // for the worker, which plans the syllables
	std::atomic<int> tuneVersion{0};
	std::atomic<BuiltLyr*> lyrIncoming{nullptr};
	std::atomic<BuiltLyr*> lyrOutgoing{nullptr};
	std::atomic<bool> lyricsShown{false};  // lyrics mode in use, for the panel
	std::atomic<int> sylShown{0};          // syllable being sung
	int syl = 0;                           // next syllable (one counter for all voices)
	// Gates that open together (a chord) sing the same syllable: the first voice starts a chord
	// window; another voice that starts inside it, and is not yet in the chord, joins it.
	static constexpr float CHORD_WINDOW = 0.02f; // seconds
	int chordLeft = 0;                     // samples left in the chord window
	int chordSyl = 0;
	unsigned chordMask = 0;                // voices already in the chord
	dsp::SchmittTrigger resetTrigger;
	std::atomic<int> polyMax{1};           // menu: maximum voices (1..MAX_VOICES)
	Lane lanes[MAX_VOICES];
	// Display expanders (Phona View, Phona Scope): the voice whose note started last, samples until
	// the next snapshots, each voice's recent output, and this round's captures
	int newestVoice = 0;
	int viewCtr = 0;
	static constexpr int TAP_N = PhonaTap::AUDIO_N; // a power of two
	float tapRing[MAX_VOICES][TAP_N] = {};
	unsigned tapPos = 0;
	bool tapsOn = false; // a display expander was found at the last round: record the output
	ChimVis tapVis[MAX_VOICES];

	Built* active = nullptr;               // owned by the audio thread
	std::atomic<Built*> incoming{nullptr}; // published by worker, taken by audio thread
	std::atomic<Built*> outgoing{nullptr}; // handed back by audio thread, freed by worker
	std::atomic<int> desiredBody{0};       // what the audio thread wants now
	std::atomic<float> desiredRate{0.f};
	std::atomic<bool> running{true};
	std::atomic<bool> buildFailed{false};
	std::atomic<float> sourceShown{0.f};     // external share in use (knob + CV), for the panel's grey-out
	std::atomic<float> modShown[PARAMS_LEN] = {}; // knob + CV per param, for the panel's modulation marks
	std::atomic<bool> rebuildRequest{false}; // audio thread: an active voice died, build new ones
	bool faultReported = false; // rebuildRequest already sent for these voices
	std::thread worker;

	int ctrl = 0;

	Phona() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		chim_tune_default(tuneCur);
		for (int i = 0; i < CHIM_TUNE_N; i++) tuneShared[i] = tuneCur[i];
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
		configInput(VOCT_INPUT, "1V/octave pitch (polyphonic: one channel per voice)");
		configInput(GATE_INPUT, "Gate / breath envelope (0.1-10V; unpatched = always on; polyphonic)");
		const char* cvNames[] = {nullptr, "Glide", "Vibrato depth", "Vibrato rate", "Vowel", "Nasal", "Larynx", "Tension", "Brightness", "Breathiness", "Roughness", "Fry"};
		for (int i = GLIDE_INPUT; i <= FRY_INPUT; i++)
			configInput(i, std::string(cvNames[i]) + " CV (10V = full knob range)");
		configInput(RESET_INPUT, "Reset (lyrics start again at the first syllable)");
		configOutput(AUDIO_OUTPUT, "Voice (polyphonic: one channel per voice)");
		worker = std::thread([this] { work(); });
	}

	~Phona() {
		running = false;
		worker.join();
		destroy(active);
		destroy(incoming.exchange(nullptr));
		destroy(outgoing.exchange(nullptr));
		destroyLyr(activeLyr);
		destroyLyr(lyrPending);
		destroyLyr(lyrIncoming.exchange(nullptr));
		destroyLyr(lyrOutgoing.exchange(nullptr));
	}

	static void destroyLyr(BuiltLyr* b) {
		if (!b) return;
		chim_lyrics_free(b->l);
		delete b;
	}

	void setBeatMode(bool on) {
		if (beatMode.exchange(on) != on) lyricsVersion++;
	}

	// UI thread: new lyrics text. The worker turns it into syllables.
	void setLyrics(const std::string& text) {
		{
			std::lock_guard<std::mutex> lock(lyricsMutex);
			if (text == lyrics) return;
			lyrics = text;
		}
		lyricsVersion++;
	}
	std::string getLyrics() {
		std::lock_guard<std::mutex> lock(lyricsMutex);
		return lyrics;
	}
	// UI thread: spelling of the syllable being sung ("" when there is none)
	std::string shownSyllable() {
		if (beatMode.load()) {
			static const char* names[] = {".", "B", "Pf", "t", "?"};
			int i = beatShown.load();
			return i >= 0 && i < 5 ? names[i] : "--";
		}
		std::lock_guard<std::mutex> lock(lyricsMutex);
		int i = sylShown;
		return i >= 0 && i < (int) sylTexts.size() ? sylTexts[i] : "";
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "beatbox", json_boolean(beatMode.load()));
		json_object_set_new(root, "voices", json_integer(polyMax.load()));
		json_object_set_new(root, "lyrics", json_string(getLyrics().c_str()));
		return root;
	}
	void dataFromJson(json_t* root) override {
		setBeatMode(json_is_true(json_object_get(root, "beatbox")));
		json_t* v = json_object_get(root, "voices");
		setPolyMax(v && json_is_integer(v) ? (int) json_integer_value(v) : 1); // older patches: mono
		json_t* l = json_object_get(root, "lyrics");
		if (l && json_is_string(l)) setLyrics(json_string_value(l));
	}
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		setBeatMode(false);
		setLyrics("");
		setPolyMax(1);
	}

	// UI thread: maximum voices. The worker builds that many; the old voices sound until they arrive.
	void setPolyMax(int n) {
		polyMax = clamp(n, 1, MAX_VOICES);
	}

	static void destroy(Built* b) {
		if (!b) return;
		for (int i = 0; i < b->n; i++) chim_free(b->voice[i]);
		delete b;
	}

	// Worker: builds whatever the audio thread currently wants, retries failures with backoff.
	void work() {
		int builtBody = -1, builtN = 0;
		float builtRate = 0.f;
		int backoff = 0;
		int builtLyrVersion = -1, builtLyrBody = -1, builtTune = 0;
		while (running) {
			destroy(outgoing.exchange(nullptr));
			destroyLyr(lyrOutgoing.exchange(nullptr));
			int b = desiredBody.load();
			// lyrics: the syllable plans depend on the text and on the body
			int lv = lyricsVersion.load();
			int tv = tuneVersion.load();
			bool beat = beatMode.load();
			if (lv != builtLyrVersion || b != builtLyrBody || tv != builtTune) {
				std::string text;
				{
					std::lock_guard<std::mutex> lock(lyricsMutex);
					text = lyrics;
				}
				double tune[CHIM_TUNE_N];
				for (int i = 0; i < CHIM_TUNE_N; i++) tune[i] = tuneShared[i].load();
				ChimLyrics* l = beat || text.empty() ? nullptr : chim_lyrics_new(text.c_str(), b == 1 ? 1.0 : 0.0, tune);
				int n = (int) chim_lyrics_len(l);
				std::vector<std::string> texts;
				for (int i = 0; i < n; i++) {
					char buf[32];
					chim_lyrics_text(l, i, buf, sizeof(buf));
					texts.push_back(buf);
				}
				{
					std::lock_guard<std::mutex> lock(lyricsMutex);
					sylTexts = texts;
				}
				bool keep = lv == builtLyrVersion && b == builtLyrBody;
				BuiltLyr* fresh = new BuiltLyr{l, {}, beat, lv, b, n, keep, {}};
				for (const std::string& t : texts) {
					std::array<char, 32> a{};
					std::strncpy(a.data(), t.c_str(), a.size() - 1);
					fresh->texts.push_back(a);
				}
				if (beat) {
					std::istringstream in(text);
					std::string token;
					while (in >> token) {
						fresh->beats.push_back(token == "B" ? 1 : token == "Pf" ? 2 : token == "t" ? 3 : token == "." ? 0 : 4);
					}
				}
				// a stale unclaimed build is dropped (if it had new text, this one stands in for it)
				if (BuiltLyr* stale = lyrIncoming.exchange(nullptr)) {
					fresh->keep = keep && stale->keep;
					destroyLyr(stale);
				}
				destroyLyr(lyrIncoming.exchange(fresh));
				builtTune = tv;
				builtLyrVersion = lv;
				builtLyrBody = b;
			}
			float r = desiredRate.load();
			int nv = polyMax.load();
			// the active voice died: build a replacement, but not more often than once per second
			if (rebuildRequest.exchange(false)) {
				builtBody = -1;
				backoff = 200;
			}
			if (backoff > 0) backoff--;
			else if (r > 0.f && (b != builtBody || r != builtRate || nv != builtN)) {
				Built* fresh = new Built{{}, 0, b, r};
				bool ok = true;
				for (int i = 0; i < nv && ok; i++) {
					fresh->voice[i] = chim_new(r, b == 1 ? 1.0 : 0.0, 0.0);
					ok = fresh->voice[i] != nullptr;
					if (ok) fresh->n++;
				}
				if (ok) {
					builtBody = b;
					builtRate = r;
					builtN = nv;
					buildFailed = false;
					destroy(incoming.exchange(fresh)); // a stale unclaimed build is dropped
				}
				else {
					destroy(fresh);
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

	// knob + CV for voice `c`. 10V spans the knob's full range; `att` is an optional attenuverter
	// param (-1..1). The panel shows voice 1.
	float knobCv(int param, int input, int att, int c) {
		ParamQuantity* q = paramQuantities[param];
		float amount = att >= 0 ? params[att].getValue() : 1.f;
		float v = params[param].getValue() + amount * inputs[input].getPolyVoltage(c) / 10.f * (q->maxValue - q->minValue);
		v = clamp(v, q->minValue, q->maxValue);
		if (c == 0) modShown[param] = v;
		return v;
	}

	// No voice other than `except` holds a syllable (so none of them reads the old lyrics).
	bool lyricsIdle(int except = -1) {
		for (int c = 0; c < active->n; c++)
			if (c != except && !chim_lyrics_idle(active->voice[c])) return false;
		return true;
	}

	// Next syllable for voice `c` (of `n`): the chord's syllable if the voice joins a chord,
	// else the next one in the lyrics (it starts a new chord).
	int pickSyllable(int c, int n, float sampleRate) {
		if (chordLeft > 0 && !(chordMask & (1u << c)) && chordSyl < n) {
			chordMask |= 1u << c;
			return chordSyl;
		}
		if (syl >= n) syl = 0;
		chordSyl = syl;
		chordMask = 1u << c;
		chordLeft = std::max(1, (int) (sampleRate * CHORD_WINDOW));
		syl = (syl + 1) % n;
		return chordSyl;
	}

	// The display expanders beside this module (also behind other Phona expanders): about 40
	// times a second, a snapshot of the voice each one shows goes into its triple buffer. A voice
	// is captured once per round, also when both show it (a capture resets the engine's telemetry
	// averages). Allocation-free.
	void captureTaps(float sampleRate, int nOut, bool lyr, bool beat) {
		if (--viewCtr > 0) return;
		viewCtr = std::max(1, (int) (sampleRate / 40.f));
		PhonaTap* taps[2] = {(PhonaTap*) familyNear(this, modelPhonaView), (PhonaTap*) familyNear(this, modelPhonaScope)};
		tapsOn = taps[0] || taps[1];
		if (!tapsOn || !active) return;
		unsigned got = 0;
		for (PhonaTap* t : taps) {
			if (!t) continue;
			int want = (int) t->params[PhonaTap::VOICE_PARAM].getValue();
			int c = clamp(want <= 0 ? newestVoice : want - 1, 0, active->n - 1);
			if (!(got & (1u << c))) {
				chim_vis_capture(active->voice[c], &tapVis[c]);
				got |= 1u << c;
			}
			PhonaTap::Frame& f = t->writeSlot();
			std::memcpy(&f.vis, &tapVis[c], sizeof(ChimVis));
			unsigned p = tapPos & (TAP_N - 1); // oldest sample first
			std::memcpy(f.audio, &tapRing[c][p], (TAP_N - p) * sizeof(float));
			std::memcpy(f.audio + (TAP_N - p), tapRing[c], p * sizeof(float));
			f.rate = sampleRate;
			f.voice = c;
			f.voices = active->n;
			f.sounding = beat ? 1 : nOut;
			f.lyrics = lyr;
			f.beat = beat;
			f.female = active->body == 1;
			if (lyr) std::memcpy(f.syllable, lanes[c].syllable, sizeof(f.syllable));
			else f.syllable[0] = 0;
			t->publish();
		}
	}

	void process(const ProcessArgs& args) override {
		int b = (int) params[BODY_PARAM].getValue();
		desiredBody = b;
		desiredRate = args.sampleRate;
		int wantN = polyMax.load();

		// take newly built voices only when the hand-back slot is free (old voices must go to the worker)
		if (outgoing.load() == nullptr) {
			if (Built* n = incoming.exchange(nullptr)) {
				if (n->body == b && n->rate == args.sampleRate && n->n == wantN) {
					outgoing.store(active);
					active = n;
					for (Lane& L : lanes) L = Lane(); // new voices start in plain mode
					voiceBeat = false;
					ctrl = 0;
					faultReported = false;
				}
				else {
					outgoing.store(n); // obsolete build: dispose on the worker
					active_rejected = true;
				}
			}
		}
		// no voices yet, or still the old sample rate (would play at the wrong pitch): mute
		if (!active || active->rate != args.sampleRate) {
			outputs[AUDIO_OUTPUT].setChannels(1);
			outputs[AUDIO_OUTPUT].setVoltage(0.f);
			lights[STATUS_LIGHT + 0].setBrightness(0.f);
			lights[STATUS_LIGHT + 1].setBrightness(buildFailed ? 1.f : 0.f);
			return;
		}
		const int nv = active->n;
		ChimVoice* const* voice = active->voice;

		// New lyrics. With new text the voices must drop their pointers to the old ones before the
		// worker frees them. With the same text (a Lab value changed) the song goes on: the new
		// plans wait until no syllable is held, because only then does a voice read nothing
		// from the old ones.
		if (lyrOutgoing.load() == nullptr) {
			if (!lyrPending) {
				lyrPending = lyrIncoming.exchange(nullptr);
			}
			else if (BuiltLyr* n = lyrIncoming.exchange(nullptr)) {
				n->keep = n->keep && lyrPending->keep;
				lyrOutgoing.store(lyrPending); // a newer build replaces the one that waited
				lyrPending = n;
			}
		}
		if (lyrPending && lyrOutgoing.load() == nullptr && (!lyrPending->keep || lyricsIdle())) {
			if (!lyrPending->keep) {
				for (int c = 0; c < nv; c++) {
					chim_set_lyrics(voice[c], 0);
					lanes[c].lyr = false;
				}
				if (voiceBeat) { chim_beat_hit(voice[0], 0); beatShown = -1; beatEdges.fill(false); }
				syl = 0;
				chordLeft = 0;
			}
			lyrOutgoing.store(activeLyr);
			activeLyr = lyrPending;
			lyrPending = nullptr;
		}
		// Lyrics mode needs syllables planned for these voices' body (after a BODY change the two
		// arrive separately; the plain voices sound in between). Beatbox uses voice 1 only.
		bool wantBeat = beatMode.load();
		bool wantLyr = !wantBeat && activeLyr && activeLyr->l && activeLyr->n > 0 && activeLyr->body == active->body;
		if (wantBeat != voiceBeat) {
			chim_set_beatbox(voice[0], wantBeat ? 1 : 0); // also ends lyrics mode on voice 1
			for (int c = 1; c < nv; c++) chim_set_lyrics(voice[c], 0);
			voiceBeat = wantBeat;
			beatEdges.fill(false);
			for (Lane& L : lanes) {
				L.lyr = false;
				L.lyrGate = false;
				L.lyrDelay = 0;
				L.idle = false;
			}
			lanes[0].lyrGate = inputs[GATE_INPUT].getVoltage() >= GATE_THRESHOLD; // require a fresh edge
			syl = 0;
			chordLeft = 0;
			beatShown = -1;
			ctrl = 0;
		}
		for (int c = 0; c < nv; c++) {
			if (wantLyr == lanes[c].lyr) continue;
			chim_set_lyrics(voice[c], wantLyr ? 1 : 0);
			lanes[c].lyr = wantLyr;
			lanes[c].lyrGate = false;
			lanes[c].lyrDelay = 0;
		}
		lyricsShown = wantLyr || wantBeat;
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			syl = 0;
			chordLeft = 0;
			if (wantBeat) { chim_beat_hit(voice[0], 0); beatShown = -1; }
		}

		// Voices that sound: the channel count of V/OCT or GATE, up to the voices built. A voice
		// above that count gets a closed gate; it still renders until silent, but is not output.
		int channels = std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels());
		int nOut = clamp(channels, 1, nv);
		bool gatePatched = inputs[GATE_INPUT].isConnected();
		float gates[MAX_VOICES];
		for (int c = 0; c < nv; c++) {
			gates[c] = 0.f;
			if (c >= nOut) continue;
			if (!gatePatched) { gates[c] = 1.f; continue; }
			float gv = inputs[GATE_INPUT].getPolyVoltage(c);
			gates[c] = gv >= GATE_THRESHOLD ? clamp(gv / 10.f, 0.f, 1.f) : 0.f;
		}
		if (wantBeat) {
			int version = lyricsVersion.load();
			if (version != beatVersion) {
				beatVersion = version;
				chim_beat_hit(voice[0], 0);
				beatShown = -1;
				beatEdges.fill(false);
				syl = 0;
			}
			Lane& L = lanes[0];
			bool open = gatePatched && gates[0] > 0.f;
			unsigned slot = beatTick++ % beatEdges.size();
			if (open && !L.lyrGate) {
				unsigned delay = (unsigned) clamp(args.sampleRate * 0.001f, 1.f, 1024.f);
				beatEdges[(slot + delay - 1) % beatEdges.size()] = true;
			}
			if (beatEdges[slot]) {
				beatEdges[slot] = false;
				bool ready = activeLyr && activeLyr->beat && activeLyr->version == lyricsVersion.load()
					&& activeLyr->body == active->body && !activeLyr->beats.empty();
				int token = 0;
				if (ready) {
					if (syl >= (int) activeLyr->beats.size()) syl = 0;
					token = activeLyr->beats[syl];
					syl = (syl + 1) % activeLyr->beats.size();
				}
				beatShown = ready ? token : -1;
				chim_beat_hit(voice[0], token);
			}
			L.lyrGate = open;
			// beat gestures own breath; an unpatched gate never sings; other voices are silent
			for (int c = 0; c < nv; c++) gates[c] = 0.f;
			L.gateHold = 0;
		}
		// Lyrics: each time a voice's gate opens, it sings the next syllable (or its chord's);
		// when the gate closes, the syllable ends (the Rust side keeps a trigger-length gate open
		// until the vowel has sounded).
		if (wantLyr) {
			if (chordLeft > 0) chordLeft--;
			for (int c = 0; c < nv; c++) {
				Lane& L = lanes[c];
				bool open = gates[c] > 0.f;
				// The syllable starts 1 ms after the gate opens. A reset that comes with the gate can
				// arrive some samples late (each cable adds one); without the wait, the gate would sing
				// the old position, then the reset would make the next gate sing syllable 1 again.
				if (open && !L.lyrGate) L.lyrDelay = std::max(1, (int) (args.sampleRate * 0.001f));
				if (L.lyrDelay > 0 && --L.lyrDelay == 0) {
					// Plans with new Lab values that still wait (with long consonants a syllable can
					// be held until the next one starts, so a voice is never idle): a note-on ends
					// this voice's held syllable from the old plans first, so when no other voice
					// holds one, this is the other safe moment.
					bool swap = lyrPending && lyrPending->keep && lyrPending->l && lyrPending->n > 0
						&& lyrPending->body == active->body && lyrOutgoing.load() == nullptr && lyricsIdle(c);
					BuiltLyr* from = swap ? lyrPending : activeLyr;
					int s = pickSyllable(c, from->n, args.sampleRate);
					chim_note_on(voice[c], from->l, s); // with a swap: reads the old plans for the last time
					if (swap) {
						lyrOutgoing.store(activeLyr);
						activeLyr = lyrPending;
						lyrPending = nullptr;
					}
					sylShown = s;
					if (s < (int) from->texts.size()) std::memcpy(L.syllable, from->texts[s].data(), sizeof(L.syllable));
					if (!open) chim_note_off(voice[c]);
				}
				else if (!open && L.lyrGate && L.lyrDelay == 0) {
					chim_note_off(voice[c]);
				}
				L.lyrGate = open;
			}
		}
		// The engine samples its gate once per 64 output samples, so a short trigger could vanish:
		// hold an onset open for 128 samples (and send it right away).
		for (int c = 0; c < nv; c++) {
			Lane& L = lanes[c];
			float& gate = gates[c];
			if (gate > 0.f && L.lastGate <= 0.f) {
				newestVoice = c;
				L.gateHold = 128;
				L.heldGate = 0.f;
				ctrl = 0;
			}
			L.lastGate = gate;
			if (L.gateHold > 0) {
				L.gateHold--;
				L.heldGate = std::max(L.heldGate, gate);
				gate = L.heldGate;
			}
		}

		// controls at 1/16 sample rate (the engine itself updates every 64 samples)
		if (ctrl-- <= 0) {
			ctrl = 15;
			// Phona Lab next to this module: its knobs are the tunable values. The ones that shape
			// a syllable's plan go to the worker, the others to the voices.
			// (directly, or with other Phona expanders between them)
			Module* lab = familyNear(this, modelPhonaLab);
			bool replan = false;
			for (int i = 0; i < CHIM_TUNE_N; i++) {
				double x = lab ? (double) lab->params[i].getValue() : PhonaLab::spec(i).def;
				if (x == tuneCur[i]) continue;
				tuneCur[i] = x;
				tuneShared[i] = x;
				if (PhonaLab::spec(i).plan) replan = true;
			}
			if (replan) tuneVersion++;
			float semis = params[PITCH_PARAM].getValue() + params[FINE_PARAM].getValue();
			modShown[PITCH_PARAM] = params[PITCH_PARAM].getValue() + 12.f * inputs[VOCT_INPUT].getVoltage(); // the arc clamps it
			bool extPatched = inputs[EXT_INPUT].isConnected();
			bool anyDead = false;
			for (int c = 0; c < nv; c++) {
				ChimParams p;
				p.f0 = dsp::FREQ_C4 * std::pow(2.f, semis / 12.f + inputs[VOCT_INPUT].getPolyVoltage(c));
				p.vowel = knobCv(VOWEL_PARAM, VOWEL_INPUT, VOWEL_ATT_PARAM, c);
				p.pressure = 8.0 * knobCv(EFFORT_PARAM, EFFORT_INPUT, EFFORT_ATT_PARAM, c); // Rust side clamps and compensates
				p.breathiness = knobCv(BREATHY_PARAM, BREATHY_INPUT, BREATHY_ATT_PARAM, c);
				p.tension = knobCv(TENSION_PARAM, TENSION_INPUT, TENSION_ATT_PARAM, c);
				p.brightness = knobCv(BRIGHT_PARAM, BRIGHT_INPUT, BRIGHT_ATT_PARAM, c);
				p.roughness = knobCv(ROUGH_PARAM, ROUGH_INPUT, ROUGH_ATT_PARAM, c);
				p.vib_depth = knobCv(VIBDEPTH_PARAM, VIBDEPTH_INPUT, VIBDEPTH_ATT_PARAM, c);
				p.vib_rate = knobCv(VIBRATE_PARAM, VIBRATE_INPUT, VIBRATE_ATT_PARAM, c);
				p.gate = gates[c];
				p.glide = 0.002 * std::pow(500.f, knobCv(GLIDE_PARAM, GLIDE_INPUT, GLIDE_ATT_PARAM, c));
				p.fry = knobCv(FRY_PARAM, FRY_INPUT, FRY_ATT_PARAM, c);
				p.velum = -0.25 + 1.65 * knobCv(NASAL_PARAM, NASAL_INPUT, NASAL_ATT_PARAM, c);
				p.larynx = knobCv(LARYNX_PARAM, LARYNX_INPUT, LARYNX_ATT_PARAM, c);
				p.ventricular = 0.0; // FALSE FOLDS control removed (subtle effect)
				// SOURCE only acts with a cable in EXT IN: unpatched, the folds always sing
				float source = knobCv(SOURCE_PARAM, SOURCE_INPUT, SOURCE_ATT_PARAM, c);
				if (!extPatched) source = 0.f;
				if (c == 0) sourceShown = source;
				chim_set_ext(voice[c], source);
				chim_set_tune(voice[c], tuneCur);
				chim_set(voice[c], &p);
				lanes[c].dead = chim_is_dead(voice[c]) != 0;
				anyDead = anyDead || lanes[c].dead;
			}
			// a dead voice stays silent: show the fault and ask the worker for new voices (once)
			if (anyDead && !faultReported) {
				faultReported = true;
				rebuildRequest = true;
			}
		}

		// skip rendering a voice once its gate is closed and it has decayed to silence
		outputs[AUDIO_OUTPUT].setChannels(wantBeat ? 1 : nOut);
		bool anySounding = false, anyDead = false;
		for (int c = 0; c < nv; c++) {
			Lane& L = lanes[c];
			bool beatVoice = wantBeat && c == 0;
			if (gates[c] > 0.f || beatVoice) {
				L.idle = false;
				L.silentFor = 0.f;
			}
			float out = 0.f;
			if (!L.idle) {
				float ext = inputs[EXT_INPUT].getPolyVoltage(c) / 5.f;
				chim_render_ext(voice[c], &ext, &out, 1);
				if (gates[c] <= 0.f && std::fabs(out) < 1e-5f) {
					L.silentFor += args.sampleTime;
					if (L.silentFor > 1.f && !beatVoice) L.idle = true;
				}
				else if (gates[c] <= 0.f) {
					L.silentFor = 0.f;
				}
			}
			if (c < (wantBeat ? 1 : nOut)) outputs[AUDIO_OUTPUT].setVoltage(6.25f * out, c);
			if (tapsOn) tapRing[c][tapPos & (TAP_N - 1)] = 6.25f * out;
			anySounding = anySounding || !L.idle;
			anyDead = anyDead || L.dead;
		}
		tapPos++;
		captureTaps(args.sampleRate, nOut, wantLyr, wantBeat);
		bool fault = anyDead || buildFailed;
		lights[STATUS_LIGHT + 0].setBrightnessSmooth(!anySounding || fault ? 0.f : 1.f, args.sampleTime);
		lights[STATUS_LIGHT + 1].setBrightnessSmooth(fault ? 1.f : 0.f, args.sampleTime);
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
		std::string text = readoutText(kind, v);
		// with lyrics, the vowel plate shows the syllable being sung (the VOWEL knob has no effect)
		bool sung = dark && module && module->lyricsShown;
		if (sung) {
			text = module->shownSyllable();
			live = false;
		}
		std::shared_ptr<window::Font> font = panelFont();
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, dark ? 7.5f : 6.3f);
		// the plate grows with a long syllable
		float w = box.size.x;
		if (sung) w = std::max(w, nvgTextBounds(args.vg, 0, 0, text.c_str(), NULL, NULL) + 9.f);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, (box.size.x - w) / 2.f, 0, w, box.size.y, box.size.y / 2.f);
		nvgFillColor(args.vg, dark ? INK : CARD_HEAD);
		nvgFill(args.vg);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		if (live) nvgFillColor(args.vg, dark ? nvgRGB(0x6c, 0xb4, 0xf5) : nvgRGB(0x16, 0x5c, 0xa8));
		else nvgFillColor(args.vg, dark ? nvgRGB(0xf0, 0xa8, 0x3c) : INK);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 0.3f, text.c_str(), NULL);
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
	bool onlyBeat = false;
	bool whenLyrics = false; // VOWEL: lyrics control the vowel instead of the knob/CV
	bool whenUnpatched = false; // SOURCE: the opposite case, no effect without a cable in EXT IN
	void draw(const DrawArgs& args) override {
		float amount = 1.f;
		if (onlyBeat) {
			if (!module || !module->beatMode.load()) return;
		}
		else if (whenLyrics) {
			if (!module || !module->lyricsShown.load()) return;
		}
		else if (module && module->beatMode.load()) {
			amount = 1.f;
		}
		else if (module) {
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

	void appendContextMenu(Menu* menu) override;

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
		// GATE and RESET (lyrics) share a cell
		float gx = cellX(3, 5, 1);
		addLabel(gx - 4.7f, y3 + midY - 6.6f, "GATE");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(gx - 4.7f, y3 + midY)), module, Phona::GATE_INPUT));
		addLabel(gx + 4.7f, y3 + midY - 6.6f, "RESET");
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(gx + 4.7f, y3 + midY)), module, Phona::RESET_INPUT));
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
		// Lyrics replace the vowel knob and CV. Keep the active syllable plate readable.
		for (const auto& span : {Vec(4.2f, VALUE_Y - 1.7f), Vec(VALUE_Y + 1.7f, 29.4f)}) {
			DimOverlay* d = new DimOverlay;
			d->module = module;
			d->whenLyrics = true;
			d->box.pos = mm2px(Vec(cellLeft(1) + 0.2f, y1 + span.x));
			d->box.size = mm2px(Vec(cellW(1, 4) - 0.4f, span.y - span.x));
			addChild(d);
		}
		// Additional beat-only ineffective cells (other overlays already cover pitch/folds/effort).
		for (auto cell : {Vec(1, 1), Vec(1, 2), Vec(2, 2), Vec(3, 2)}) {
			int row = (int) cell.x, col = (int) cell.y, n = row == 1 ? 4 : 5;
			DimOverlay* d = new DimOverlay;
			d->module = module;
			d->onlyBeat = true;
			d->box.pos = mm2px(Vec(cellLeft(row) + col * cellW(row, n) + 0.2f, ROW0 + row * ROW_H + 4.2f));
			d->box.size = mm2px(Vec(cellW(row, n) - 0.4f, 25.2f));
			addChild(d);
		}
		// SOURCE is greyed out the other way round: when nothing is patched into EXT IN
		float sx = cellX(3, 5, 3);
		addDim(module, 3, sx - w / 2.f + 0.2f, sx + w / 2.f - 0.2f, true);
	}
};

// The lyrics text, in the module's menu. Each change goes to the module, which plans the syllables
// on its worker thread.
struct LyricsField : ui::TextField {
	Phona* module = nullptr;
	LyricsField() {
		box.size.x = 320.f;
		placeholder = "Words, or B t Pf t . in Beatbox mode";
	}
	void onChange(const ChangeEvent& e) override {
		if (module) module->setLyrics(getText());
	}
};

void PhonaWidget::appendContextMenu(Menu* menu) {
	Phona* module = getModule<Phona>();
	if (!module) return;
	menu->addChild(new MenuSeparator);
	menu->addChild(createIndexSubmenuItem("Polyphony (max voices)", {"1", "2", "3", "4"},
		[=]() { return (size_t) (module->polyMax.load() - 1); },
		[=](size_t i) { module->setPolyMax((int) i + 1); }));
	menu->addChild(createMenuLabel("Voices = V/OCT or GATE channels, up to the max; beatbox is mono"));
	menu->addChild(new MenuSeparator);
	menu->addChild(createCheckMenuItem("Lyrics mode", "", [=]() { return !module->beatMode.load(); }, [=]() { module->setBeatMode(false); }));
	menu->addChild(createCheckMenuItem("Beatbox mode", "", [=]() { return module->beatMode.load(); }, [=]() { module->setBeatMode(true); }));
	menu->addChild(createMenuLabel("Lyrics: next syllable per gate; empty = vowel"));
	menu->addChild(createMenuLabel("Beatbox: B kick, Pf snare, t hat, . rest"));
	menu->addChild(createMenuLabel("Space-separated; invalid = silent step (?)"));
	LyricsField* field = new LyricsField;
	field->module = module;
	field->setText(module->getLyrics());
	menu->addChild(field);
	menu->addChild(createMenuItem("Clear text", "", [=]() { module->setLyrics(""); }));
}

Model* modelPhona = createModel<Phona, PhonaWidget>("Phona");
