// Headless tests of Phona View: Phona finds the View beside it (also through Phona Lab, in both
// orders), sends ~40 snapshots a second of the chosen voice, copies the sung syllable, and the
// View costs little CPU. Then it draws snapshots with the real drawing code into SVG files
// (NanoVG calls are recorded, not rendered), for a look at the result without opening Rack.
#include "../src/Phona.cpp"
#include "../src/PhonaLab.cpp"
#include "../src/PhonaView.cpp"
#include "../src/PhonaScope.cpp"
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
