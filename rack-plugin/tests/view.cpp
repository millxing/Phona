// Headless tests of Phona View: Phona finds the View beside it (also through Phona Lab, in both
// orders), sends ~40 snapshots a second of the chosen voice, copies the sung syllable, and the
// View costs little CPU. Then it draws snapshots with the real drawing code into SVG files
// (NanoVG calls are recorded, not rendered), for a look at the result without opening Rack.
#include "../src/Phona.cpp"
#include "../src/PhonaLab.cpp"
#include "../src/PhonaView.cpp"
#include "../src/PhonaScope.cpp"
#include "../src/PhonaScript.cpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
Plugin* pluginInstance = nullptr;

#include "nvg_record.hpp"

int main(int argc, char** argv) {
	Context context;
	context.engine = new engine::Engine;
	contextSet(&context);
	float rate = argc > 1 ? std::stof(argv[1]) : 48000.f;
	const char* outDir = argc > 2 ? argv[2] : nullptr;
	Module::ProcessArgs args{rate, 1.f / rate, 0};

	Phona m;
	PhonaLab lab;
	PhonaView view;
	PhonaScope scope;
	scope.model = modelPhonaScope;
	m.model = modelPhona;
	lab.model = modelPhonaLab;
	view.model = modelPhonaView;
	auto link = [](Module* a, Module* b) { a->rightExpander.module = b; b->leftExpander.module = a; };
	auto unlinkAll = [&]() {
		for (Module* x : {(Module*) &m, (Module*) &lab, (Module*) &view, (Module*) &scope}) x->leftExpander.module = x->rightExpander.module = nullptr;
	};
	auto tick = [&](int n) { for (int i = 0; i < n; i++) { m.process(args); view.process(args); lab.process(args); scope.process(args); } };
	auto ready = [&]() {
		for (int i = 0; i < 4000; i++) {
			tick(16);
			if (m.active && m.active->n == m.polyMax.load() && m.active->rate == rate
				&& m.activeLyr && m.activeLyr->version == m.lyricsVersion.load() && m.activeLyr->body == m.active->body) return;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		assert(false && "worker timeout");
	};
	Input& voct = m.inputs[Phona::VOCT_INPUT];
	Input& gate = m.inputs[Phona::GATE_INPUT];
	m.outputs[Phona::AUDIO_OUTPUT].channels = 1;
	voct.channels = 3;
	gate.channels = 3;
	m.setPolyMax(4);
	ready();

	// No View: nothing is sent.
	tick((int) rate / 4);
	assert(view.read().seq == 0 && !view.linked);

	// View on the right: about 40 snapshots a second, all valid.
	link(&m, &view);
	for (int c = 0; c < 3; c++) gate.setVoltage(10.f, c);
	unsigned s0 = view.seq;
	tick((int) rate);
	unsigned got = view.seq - s0;
	std::cout << "snapshots in 1 s: " << got << "\n";
	assert(got >= 38 && got <= 42);
	const PhonaView::Frame& f = view.read();
	assert(view.linked && f.vis.valid && f.voices == 4 && f.sounding == 3);
	assert(f.vis.voicing > 0.2f && f.vis.n_grid > 10 && f.vis.line[CHIM_VIS_TONGUE].n > 10);

	// Voice choice: 3 → voice index 2; follow → the voice whose gate opened last.
	view.params[PhonaView::VOICE_PARAM].setValue(3.f);
	tick((int) (rate * 0.05f));
	assert(view.read().voice == 2);
	view.params[PhonaView::VOICE_PARAM].setValue(0.f);
	gate.setVoltage(0.f, 1); tick(64); gate.setVoltage(10.f, 1);
	tick((int) (rate * 0.05f));
	assert(view.read().voice == 1);
	view.params[PhonaView::VOICE_PARAM].setValue(4.f); // voice 4 is built but silent: still shown
	tick((int) (rate * 0.05f));
	assert(view.read().voice == 3 && view.read().vis.valid);

	// Chains: Phona | Lab | View, and Phona | View | Lab (the Lab's knobs still reach Phona).
	unlinkAll();
	link(&m, &lab); link(&lab, &view);
	s0 = view.seq; tick((int) (rate * 0.1f));
	assert(view.seq > s0 && view.linked);
	unlinkAll();
	link(&view, &m); link(&m, &lab); // View | Phona | Lab
	s0 = view.seq; tick((int) (rate * 0.1f));
	assert(view.seq > s0);
	unlinkAll();
	link(&m, &view); link(&view, &lab); // Phona | View | Lab
	lab.params[PhonaLab::LEVEL_PARAM].setValue(2.5f);
	s0 = view.seq; tick((int) (rate * 0.1f));
	assert(view.seq > s0);
	assert(m.tuneCur[PhonaLab::LEVEL_PARAM] == 2.5);
	lab.params[PhonaLab::LEVEL_PARAM].setValue(PhonaLab::spec(PhonaLab::LEVEL_PARAM).def);
	tick(64);

	// CPU: the same second with and without the View.
	auto cpu = [&](bool withView) {
		unlinkAll();
		if (withView) link(&m, &view);
		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < (int) rate; i++) m.process(args);
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	};
	cpu(false);
	double without = cpu(false), with = cpu(true);
	std::cout << "3 voices: " << 100.0 * without << "% of one core; with View " << 100.0 * with << "%\n";
	assert(with < without * 1.05 + 0.01);
	for (int c = 0; c < 3; c++) gate.setVoltage(0.f, c);
	tick((int) (rate * 0.3f));

	// Lyrics: the shown voice's syllable and the speech sound arrive with the snapshot.
	unlinkAll();
	link(&m, &view);
	view.params[PhonaView::VOICE_PARAM].setValue(1.f);
	voct.channels = 1; gate.channels = 1;
	m.setLyrics("sun song"); ready(); tick(64);
	ViewDisplay d;
	d.module = &view;
	d.box.size = mm2px(Vec(71.12f - 3.2f - 1.2f, 105.f - 1.2f));
	struct Shot { const char* name; bool done; };
	Shot shots[4] = {{"vowel", false}, {"hiss", false}, {"nasal", false}, {"breathy", false}};
	bool sawSyl = false, sawPhoneme = false;
	float maxPort = 0.f, maxHiss = 0.f;
	unsigned lastSeq = view.seq;
	for (int note = 0; note < 2; note++) {
		gate.setVoltage(10.f, 0);
		for (int i = 0; i < (int) (rate * 1.1f); i += 64) {
			if (i >= (int) (rate * 0.7f)) gate.setVoltage(0.f, 0); // the last consonant comes after the gate
			tick(64);
			if (view.seq == lastSeq) continue;
			lastSeq = view.seq;
			const PhonaView::Frame& r = view.read();
			if (r.syllable[0]) sawSyl = true;
			if (r.vis.phoneme[0]) sawPhoneme = true;
			maxPort = std::max(maxPort, r.vis.velum_port);
			float oralHiss = 0.f; // loudest source in front of the pharynx
			for (int j = 0; j < r.vis.n_noise; j++) if (r.vis.noise_s[j] > 9.f) oralHiss = std::max(oralHiss, r.vis.noise_level[j]);
			maxHiss = std::max(maxHiss, oralHiss);
			d.drawContent(Widget::DrawArgs()); // keeps the afterglow and particles moving (draws nowhere)
			if (!outDir) continue;
			std::string ph = r.vis.phoneme;
			if (!shots[1].done && oralHiss > 30.f && ph == "s") { writeSvg(d, std::string(outDir) + "/view-hiss.svg"); shots[1].done = true; }
			if (!shots[2].done && r.vis.velum_port > 0.4f && r.vis.voicing > 0.3f) { writeSvg(d, std::string(outDir) + "/view-nasal.svg"); shots[2].done = true; }
			if (!shots[0].done && note == 0 && i > rate * 0.35f) { writeSvg(d, std::string(outDir) + "/view-vowel.svg"); shots[0].done = true; }
		}
	}
	std::cout << "syllable seen " << sawSyl << ", speech sound seen " << sawPhoneme << ", largest velum port " << maxPort << " cm2, loudest hiss in the mouth " << maxHiss << "\n";
	assert(sawSyl && sawPhoneme);
	assert(maxPort > 0.2f && maxHiss > 30.f); // the n / ng lower the soft palate; the s hisses at the teeth

	// No lyrics, very breathy: the folds open at the back.
	m.setLyrics(""); ready();
	m.params[Phona::BREATHY_PARAM].setValue(1.5f);
	gate.setVoltage(10.f, 0);
	tick((int) (rate * 0.5f));
	const PhonaView::Frame& br = view.read();
	std::cout << "breathy abduction " << br.vis.abduction << ", plain abduction ";
	if (outDir) { for (int i = 0; i < 10; i++) { tick(1200); d.drawContent(Widget::DrawArgs()); } writeSvg(d, std::string(outDir) + "/view-breathy.svg"); }
	float breathy = br.vis.abduction;
	m.params[Phona::BREATHY_PARAM].setValue(0.f);
	tick((int) (rate * 0.5f));
	std::cout << view.read().vis.abduction << "\n";
	assert(breathy > view.read().vis.abduction);

	// ---------------------------------------------------------------- Phona Scope
	// Both displays at once, in long chains: View | Phona | Lab | Scope, then Phona | Lab | View | Scope.
	gate.setVoltage(10.f, 0);
	unlinkAll();
	link(&view, &m); link(&m, &lab); link(&lab, &scope);
	unsigned v0 = view.seq, c0 = scope.seq;
	tick((int) (rate * 0.25f));
	assert(view.seq > v0 && scope.seq > c0 && scope.linked && lab.lights[PhonaLab::LINK_LIGHT].getBrightness() > 0.f);
	unlinkAll();
	link(&m, &lab); link(&lab, &view); link(&view, &scope);
	lab.params[PhonaLab::LEVEL_PARAM].setValue(2.f);
	v0 = view.seq; c0 = scope.seq;
	tick((int) (rate * 0.25f));
	assert(view.seq > v0 && scope.seq > c0 && m.tuneCur[PhonaLab::LEVEL_PARAM] == 2.0);
	lab.params[PhonaLab::LEVEL_PARAM].setValue(PhonaLab::spec(PhonaLab::LEVEL_PARAM).def);
	// Both show voice 1: one capture serves both (a second capture would find the sound pressure
	// averages just reset to zero).
	{
		const PhonaTap::Frame &a = view.read(), &b = scope.read();
		float pa = 0.f, pb = 0.f;
		for (int i = 0; i < b.vis.n_nodes; i++) { pa = std::max(pa, a.vis.pressure[i]); pb = std::max(pb, b.vis.pressure[i]); }
		assert(pa > 0.f && pb > 0.f);
		// the voice's output came along: about the level Phona sends out
		double sum = 0;
		for (float x : b.audio) sum += x * x;
		float rms = std::sqrt(sum / PhonaTap::AUDIO_N);
		std::cout << "scope audio RMS " << rms << " V\n";
		assert(rms > 0.1f && rms < 10.f);
	}
	// Formants of the engine's vowels, from the tract model: [i] low F1 high F2, [ɑ] high F1, [u] low F2.
	ScopeDisplay sd;
	sd.module = &scope;
	sd.box.size = mm2px(Vec(101.6f - 3.2f - 1.2f, 105.f - 1.2f));
	m.params[Phona::BREATHY_PARAM].setValue(0.15f);
	const char* vname[3] = {"i", "a", "u"};
	float vpos[3] = {0.f, 2.f, 4.f};
	float F[3][2];
	for (int body = 0; body < 2; body++) {
		m.params[Phona::BODY_PARAM].setValue((float) body);
		for (int i = 0; i < 4000 && !(m.active && m.active->body == body); i++) { tick(16); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
		assert(m.active->body == body);
		ready();
		gate.setVoltage(10.f, 0);
		for (int k = 0; k < 3; k++) {
			m.params[Phona::VOWEL_PARAM].setValue(vpos[k]);
			tick((int) (rate * 0.6f));
			for (int j = 0; j < 4; j++) { tick((int) rate / 40); sd.drawContent(Widget::DrawArgs()); }
			assert(sd.nFormants >= 3);
			F[k][0] = sd.formant[0];
			F[k][1] = sd.formant[1];
			std::cout << (body ? "female " : "male ") << "[" << vname[k] << "] F1 " << sd.formant[0] << " F2 " << sd.formant[1] << " F3 " << sd.formant[2]
				<< "  open quotient " << sd.oq << "\n";
			if (outDir && rate == 48000.f) writeSvg(sd, std::string(outDir) + "/scope-" + (body ? "f-" : "m-") + vname[k] + ".svg");
		}
		assert(F[0][0] < 450.f && F[0][1] > 1800.f);   // i
		assert(F[1][0] > 550.f && F[1][1] < 1800.f);   // a (women: about 1550 Hz)
		assert(F[2][0] < 500.f && F[2][1] < 1200.f);   // u
		assert(sd.oq > 0.2f && sd.oq < 0.95f);
	}
	m.params[Phona::BODY_PARAM].setValue(0.f);
	m.params[Phona::VOWEL_PARAM].setValue(2.f);
	for (int i = 0; i < 4000 && !(m.active && m.active->body == 0); i++) { tick(16); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
	ready();
	// The spectrum's strongest peak sits on a harmonic of the pitch.
	{
		tick((int) (rate * 0.5f));
		for (int j = 0; j < 6; j++) { tick((int) rate / 40); sd.drawContent(Widget::DrawArgs()); }
		int best = 1;
		for (int k = 1; k < (int) sd.spec.size(); k++) if (sd.spec[k] > sd.spec[best]) best = k;
		float fpk = best * sd.specRate / sd.specN, f0 = scope.read().vis.f0;
		float h = fpk / f0;
		std::cout << "spectrum peak " << fpk << " Hz = " << h << " x f0 (" << f0 << " Hz), " << sd.spec[best] << " dB\n";
		assert(std::fabs(h - std::round(h)) < 0.2f && sd.spec[best] > -30.f);
	}
	// Lyrics through the Scope: a picture during [s] of "sun".
	m.setLyrics("sun song"); ready();
	gate.setVoltage(0.f, 0); tick((int) (rate * 0.3f));
	gate.setVoltage(10.f, 0);
	for (int i = 0; i < (int) (rate * 0.6f); i += (int) rate / 40) {
		tick((int) rate / 40);
		sd.drawContent(Widget::DrawArgs());
		const ChimVis& sv = scope.read().vis;
		float hiss = 0.f;
		for (int j = 0; j < sv.n_noise; j++) if (sv.noise_s[j] > 9.f) hiss = std::max(hiss, sv.noise_level[j]);
		if (outDir && rate == 48000.f && std::string(sv.phoneme) == "s" && hiss > 50.f) { writeSvg(sd, std::string(outDir) + "/scope-s.svg"); break; }
	}
	gate.setVoltage(0.f, 0);
	m.setLyrics(""); ready();

	// ---------------------------------------------------------------- Several displays
	// Phona | View x8 | Scope: every View within the chain limit gets its own voice, and the Scope
	// behind them too; a 9th View is out of reach and its link light stays off. Lab on the left.
	{
		unlinkAll();
		PhonaView vs[9];
		for (PhonaView& v : vs) v.model = modelPhonaView;
		voct.channels = 4; gate.channels = 4;
		for (int c = 0; c < 4; c++) gate.setVoltage(10.f, c);
		for (int k = 0; k < 9; k++) vs[k].params[PhonaTap::VOICE_PARAM].setValue((float) (k % 4 + 1));
		link(&lab, &m); link(&m, &vs[0]);
		for (int k = 0; k < 7; k++) link(&vs[k], &vs[k + 1]);
		link(&vs[7], &scope);   // the 9th module on the right: out of reach
		link(&scope, &vs[8]);   // the 10th: also out of reach
		unsigned c0 = scope.seq;
		for (int i = 0; i < (int) (rate * 0.25f); i++) {
			tick(1);
			for (PhonaView& v : vs) v.process(args);
		}
		// FAMILY_HOPS = 8 expanders on each side: the Scope is the 9th, so it must not be reached.
		bool scopeFed = scope.seq > c0;
		std::cout << "views fed:";
		for (int k = 0; k < 9; k++) std::cout << " " << (vs[k].seq > 0) << (vs[k].linked ? "L" : "-");
		std::cout << ", scope " << scopeFed << (scope.linked ? "L" : "-") << "\n";
		for (int k = 0; k < 8; k++) {
			assert(vs[k].seq > 0 && vs[k].linked);
			assert(vs[k].read().voice == k % 4 && vs[k].read().vis.valid);
		}
		// beyond the limit: no data, and no link light (the light means "fed")
		assert(!scopeFed && !scope.linked);
		assert(vs[8].seq == 0 && !vs[8].linked);
		// CPU: 8 Views, each with its own voice, against none (4 voices sound)
		{
			auto run = [&]() {
				auto t0 = std::chrono::steady_clock::now();
				for (int i = 0; i < (int) rate; i++) m.process(args);
				return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			};
			double with8 = run();
			m.rightExpander.module = nullptr;
			double none = run();
			m.rightExpander.module = &vs[0];
			with8 = std::min(with8, run());
			std::cout << "4 voices: " << 100.0 * none << "% of one core; with 8 Views " << 100.0 * with8 << "%\n";
			assert(with8 < none * 1.05 + 0.01);
		}
		for (int c = 0; c < 4; c++) gate.setVoltage(0.f, c);
		m.leftExpander.module = m.rightExpander.module = nullptr;
		lab.rightExpander.module = nullptr;
		scope.leftExpander.module = scope.rightExpander.module = nullptr;
		voct.channels = 1; gate.channels = 1;
		tick((int) (rate * 0.3f));
	}

	// ---------------------------------------------------------------- Phona Script
	{
		unlinkAll();
		PhonaScript sc;
		sc.model = modelPhonaScript;
		link(&m, &sc); link(&sc, &view); // the View sees Phona through the Script
		auto tickS = [&](int n) { for (int i = 0; i < n; i++) { tick(1); sc.process(args); } };
		auto trig = [&](int input, float v = 10.f) {
			sc.inputs[input].setVoltage(v); tickS(32);
			sc.inputs[input].setVoltage(0.f); tickS(32);
		};
		// one note: returns the syllable index Phona picked
		auto sing = [&]() {
			gate.setVoltage(10.f, 0); tickS((int) (rate * 0.15f));
			int s = m.sylShown.load();
			gate.setVoltage(0.f, 0); tickS((int) (rate * 0.25f));
			return s;
		};
		auto scriptReady = [&](bool on) {
			tickS(64);
			ready();
			assert(m.activeLyr && m.activeLyr->script == on);
		};
		m.setLyrics("ignored when a script is on");
		sc.setPhrase(0, "one two");
		sc.setPhrase(2, "red blue green");
		scriptReady(true);
		const Phona::BuiltLyr* L = m.activeLyr;
		std::cout << "script phrases: start " << L->pStart[0] << "/" << L->pStart[2] << ", length " << L->pLen[0] << "/" << L->pLen[1] << "/" << L->pLen[2] << ", total " << L->n << "\n";
		assert(L->pStart[0] == 0 && L->pLen[0] == 2 && L->pLen[1] == 0 && L->pStart[2] == 2 && L->pLen[2] == 3 && L->n == 5);
		assert(sc.lights[PhonaScript::LINK_LIGHT].getBrightness() > 0.f && view.linked);
		// a phrase repeats
		int a = sing(), b = sing(), c = sing();
		assert(a == 0 && b == 1 && c == 0 && sc.current == 0);
		// NEXT in the middle of a phrase: the next gate starts phrase 3 (phrase 2 is empty)
		trig(PhonaScript::NEXT_INPUT);
		assert(sc.want == 2 && sc.current == 0); // not yet: at the next gate
		a = sing(); b = sing(); c = sing(); int d = sing();
		assert(a == 2 && b == 3 && c == 4 && d == 2 && sc.current == 2);
		// the View shows the sung syllable through the Script
		assert(std::string(view.read().syllable) == std::string(L->texts[2].data()));
		// NEXT after the last phrase: back to phrase 1
		trig(PhonaScript::NEXT_INPUT);
		assert(sing() == 0);
		// RESET: phrase 1 from syllable 1, also when phrase 1 is already chosen
		trig(PhonaScript::RESET_INPUT);
		assert(sing() == 0);
		// SELECT: 2.6 V = phrase 3; 1.5 V = phrase 2, which is empty: no change
		sc.inputs[PhonaScript::SELECT_INPUT].channels = 1;
		sc.inputs[PhonaScript::SELECT_INPUT].setVoltage(2.6f); tickS(32);
		assert(sing() == 2);
		sc.inputs[PhonaScript::SELECT_INPUT].setVoltage(1.5f); tickS(32);
		assert(sing() == 3);
		sc.inputs[PhonaScript::SELECT_INPUT].channels = 0;
		// Phona's own RESET: the current phrase from its first syllable
		m.inputs[Phona::RESET_INPUT].setVoltage(10.f); tickS(32); m.inputs[Phona::RESET_INPUT].setVoltage(0.f); tickS(32);
		assert(sing() == 2);
		// a click on the panel
		sc.clickReq = 0; tickS(32);
		assert(sing() == 0);
		// new text: the phrase ranges follow
		sc.setPhrase(0, "one two three");
		scriptReady(true);
		assert(m.activeLyr->pLen[0] == 3 && m.activeLyr->pStart[2] == 3);
		a = sing(); b = sing(); c = sing(); d = sing();
		assert(a == 0 && b == 1 && c == 2 && d == 0);
		// save and load
		json_t* j = sc.dataToJson();
		PhonaScript sc2;
		sc2.dataFromJson(j);
		json_decref(j);
		assert(sc2.phrase(2) == "red blue green" && sc2.phrase(1).empty() && sc2.want == sc.want.load() && sc2.mask == 5u);
		// Beatbox mode: the phrases are beat patterns
		m.setBeatMode(true);
		sc.setPhrase(0, "B t");
		sc.setPhrase(2, "Pf");
		scriptReady(true);
		int beats[5];
		for (int k = 0; k < 3; k++) { sing(); beats[k] = m.beatShown.load(); }
		trig(PhonaScript::NEXT_INPUT);
		for (int k = 3; k < 5; k++) { sing(); beats[k] = m.beatShown.load(); }
		std::cout << "script beats: " << beats[0] << beats[1] << beats[2] << beats[3] << beats[4] << "\n";
		assert(beats[0] == 1 && beats[1] == 3 && beats[2] == 1 && beats[3] == 2 && beats[4] == 2);
		m.setBeatMode(false);
		// Script removed: Phona's own text again
		unlinkAll();
		scriptReady(false);
		assert(m.getLyrics() == "ignored when a script is on" && m.activeLyr->n > 0);
		m.setLyrics(""); ready();
		std::cout << "script test passed\n";
	}

	// Unlinked: the display falls back to the resting head and says where to put the module.
	unlinkAll();
	tick(64);
	assert(!view.linked);
	if (outDir) writeSvg(d, std::string(outDir) + "/view-unlinked.svg");
	assert(!scope.linked);
	if (outDir) writeSvg(sd, std::string(outDir) + "/scope-unlinked.svg");
	std::cout << "view module test passed\n";
	return 0;
}
