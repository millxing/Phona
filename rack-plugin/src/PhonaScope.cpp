#include "PhonaTap.hpp"
#include "panel.hpp"
#include "vector.hpp"
#include "TapWidgets.hpp"
#include <complex>
#include <deque>

// Phona Scope: the instrument display. From Phona's snapshots of one voice (see PhonaTap.hpp):
// - readouts: pitch, lung pressure, level, voicing, open quotient, nasal port, narrowest place
// - SPECTRUM: the voice's output (FFT of its last samples) with the vocal tract's frequency
//   response drawn over it, and the formants F1-F4 (the response's peaks)
// - TRACT: the area function, glottis → lips, as a tube; its cross-sections lit by sound pressure
// - FOLDS: the glottal opening (white) and airflow (amber) over the last two cycles
// - VOWELS: F1 against F2, with a fading trail and reference vowels
// The frequency response comes from a lossy chain-matrix tube model of the snapshot's sections,
// with the nose as a side branch behind the velum port. It is a model of the shape the engine
// has at this moment, not a measurement of the engine's own filter.

namespace {

using vd::Pts;
typedef std::complex<double> cd;

const float F_LO = 50.f, F_HI = 8000.f; // spectrum axis (Hz, log)
const float DB_TOP = 0.f, DB_BOT = -72.f; // 0 dB = a 5 V sine
const int CURVE_N = 220;                  // points on the response curve

// The vocal tract's response U_lips / U_glottis at frequency f, from the snapshot's sections.
struct TractModel {
	std::vector<double> area, len; // glottis → lips
	int junction = 0;              // section index where the nose branches off (pharynx end)
	double port = 0.0;             // velum port area (cm²)
	bool ok = false;

	void build(const ChimVis& v) {
		area.clear();
		len.clear();
		int n = v.n_nodes, nph = clamp(v.n_ph, 0, n);
		ok = n > 4 && nph > 0 && nph < n && v.ph_len > 0.f && v.or_len > 0.f;
		if (!ok) return;
		for (int i = 0; i < n; i++) {
			area.push_back(std::max(0.02, (double) v.node_area[i]));
			len.push_back(i < nph ? v.ph_len / nph : v.or_len / (n - nph));
		}
		junction = nph;
		port = std::max(0.0, (double) v.velum_port);
	}

	static constexpr double C = 35000.0, RHO = 0.00114;
	// radiation impedance of an opening of area a (a piston in a baffle, low-frequency terms)
	static cd zrad(double k, double a) {
		double r = std::sqrt(a / M_PI);
		return RHO * C / a * cd(0.5 * k * k * r * r, 0.8 * k * r);
	}
	// chain matrix of a lossy uniform tube [[A, B], [C, D]]
	static void tube(cd k, double l, double a, cd m[4]) {
		cd kl = k * l, c = std::cos(kl), s = std::sin(kl), z = RHO * C / a;
		m[0] = c;
		m[1] = cd(0, 1) * z * s;
		m[2] = cd(0, 1) * s / z;
		m[3] = c;
	}
	static void mul(cd a[4], const cd b[4]) {
		cd r[4] = {a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3]};
		for (int i = 0; i < 4; i++) a[i] = r[i];
	}
	double gain(double f) const {
		if (!ok) return 0.0;
		double w = 2.0 * M_PI * f;
		// wall and viscous losses as one damping: formant bandwidths of about 60-90 Hz
		cd k(w / C, -(0.0055 + 0.0012 * std::sqrt(f / 1000.0)));
		cd K[4] = {1.0, 0.0, 0.0, 1.0};
		for (size_t i = 0; i < area.size(); i++) {
			if ((int) i == junction && port > 0.01) {
				// the nose: the port (1 cm), the nasal cavity (11 cm, 2.5 cm²), the nostrils
				cd N[4], P[4];
				tube(k, 11.0, 2.5, N);
				tube(k, 1.0, port, P);
				mul(P, N);
				cd zr = zrad(w / C, 1.5);
				cd zin = (P[0] * zr + P[1]) / (P[2] * zr + P[3]);
				cd Y[4] = {1.0, 0.0, 1.0 / zin, 1.0};
				mul(K, Y);
			}
			cd M[4];
			tube(k, len[i], area[i], M);
			mul(K, M);
		}
		cd h = 1.0 / (K[2] * zrad(w / C, area.back()) + K[3]);
		return std::abs(h);
	}
};

// Reference vowels: mean F1, F2 of American English speakers (Hillenbrand et al. 1995), men and women.
struct RefVowel { const char* ipa; float f1[2], f2[2]; };
const RefVowel REF[] = {
	{"i", {342, 437}, {2322, 2761}}, {"ɪ", {427, 483}, {2034, 2365}}, {"e", {476, 536}, {2089, 2530}},
	{"ɛ", {580, 731}, {1799, 2058}}, {"æ", {588, 669}, {1952, 2349}}, {"ɑ", {768, 936}, {1333, 1551}},
	{"ɔ", {652, 781}, {997, 1136}}, {"o", {497, 555}, {910, 1035}}, {"ʊ", {469, 519}, {1122, 1225}},
	{"u", {378, 459}, {997, 1105}}, {"ʌ", {623, 753}, {1200, 1426}},
};

std::string noteName(float f) {
	if (!(f > 10.f)) return "--";
	static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	float midi = 69.f + 12.f * std::log2(f / 440.f);
	int m = (int) std::round(midi);
	int cents = (int) std::round((midi - m) * 100.f);
	return string::f("%s%d %+d ct", names[((m % 12) + 12) % 12], m / 12 - 1, cents);
}

} // namespace

struct ScopeDisplay : TransparentWidget {
	PhonaTap* module = nullptr;
	unsigned lastSeq = ~0u;
	TractModel tract;
	float curve[CURVE_N] = {};           // tract response (dB), log frequency
	float formant[4] = {};
	int nFormants = 0;
	std::vector<float> spec;             // smoothed output power spectrum (dB) per FFT bin
	float specRate = 48000.f;
	int specN = 0;
	dsp::RealFFT* fft = nullptr;
	int fftN = 0;
	float* fftIn = nullptr;
	float* fftOut = nullptr;
	struct TrailPt { float f1, f2, a; }; // f1 = 0: a break (the voice was silent)
	std::deque<TrailPt> trail;
	float oq = 0.f;

	~ScopeDisplay() {
		delete fft;
		pffft_aligned_free(fftIn);
		pffft_aligned_free(fftOut);
	}

	// ------------------------------------------------------------ analysis (UI thread)
	void analyse(const PhonaTap::Frame& fr) {
		const ChimVis& v = fr.vis;
		// tract response and its peaks
		tract.build(v);
		float top = -INFINITY;
		for (int i = 0; i < CURVE_N; i++) {
			float f = F_LO * std::pow(F_HI / F_LO, (float) i / (CURVE_N - 1));
			curve[i] = 20.f * std::log10(std::max(1e-9, tract.gain(f)));
			top = std::max(top, curve[i]);
		}
		nFormants = 0;
		if (tract.ok) {
			double prev2 = 0, prev = 0;
			for (double f = 100.0; f <= 5500.0 && nFormants < 4; f += 10.0) {
				double g = tract.gain(f);
				if (f > 120.0 && prev > prev2 && prev >= g) {
					// parabolic refinement of the peak at f - 10
					double den = prev2 - 2 * prev + g;
					double off = den != 0 ? 0.5 * (prev2 - g) / den : 0.0;
					formant[nFormants++] = (float) (f - 10.0 + 10.0 * off);
				}
				prev2 = prev;
				prev = g;
			}
		}
		// output spectrum: Hann window over the last samples (every other one above 60 kHz)
		int step = fr.rate > 60000.f ? (fr.rate > 120000.f ? 4 : 2) : 1;
		int n = PhonaTap::AUDIO_N / step;
		if (n != fftN) {
			delete fft;
			pffft_aligned_free(fftIn);
			pffft_aligned_free(fftOut);
			fft = new dsp::RealFFT(n);
			fftIn = (float*) pffft_aligned_malloc(n * sizeof(float));
			fftOut = (float*) pffft_aligned_malloc(n * sizeof(float));
			fftN = n;
			spec.assign(n / 2, DB_BOT);
		}
		specRate = fr.rate / step;
		specN = n;
		for (int i = 0; i < n; i++) {
			float w = 0.5f - 0.5f * std::cos(2.f * M_PI * i / (n - 1));
			fftIn[i] = fr.audio[PhonaTap::AUDIO_N - n * step + i * step] * w;
		}
		fft->rfft(fftIn, fftOut);
		for (int k = 1; k < n / 2; k++) {
			float re = fftOut[2 * k], im = fftOut[2 * k + 1];
			float amp = std::sqrt(re * re + im * im) * 4.f / n; // Hann gain 0.5, one-sided
			float db = 20.f * std::log10(std::max(1e-7f, amp / 5.f));
			spec[k] = std::max(db, spec[k] * 0.55f + db * 0.45f); // fast attack, slow fall
		}
		// open quotient: the share of the cycle in which the folds are apart
		oq = 0.f;
		if (v.n_scope > 0 && v.voicing > 0.2f) {
			float mx = 0.f;
			for (int i = 0; i < v.n_scope; i++) mx = std::max(mx, v.glottal_area[i]);
			int open = 0;
			for (int i = 0; i < v.n_scope; i++) open += v.glottal_area[i] > 0.1f * mx;
			oq = mx > 0.f ? (float) open / v.n_scope : 0.f;
		}
		// vowel chart trail: while the voice sounds and the mouth is open
		float lvl = outLevelDb(v);
		if (nFormants >= 2 && v.voicing > 0.3f && lvl > -40.f) trail.push_front({formant[0], formant[1], 1.f});
		else if (!trail.empty() && trail.front().f1 > 0.f) trail.push_front({0.f, 0.f, 1.f});
		for (TrailPt& t : trail) t.a *= 0.93f;
		while (!trail.empty() && trail.back().a < 0.05f) trail.pop_back();
		while (trail.size() > 60) trail.pop_back();
	}

	static float outLevelDb(const ChimVis& v) {
		return 20.f * std::log10(std::max(1e-6f, v.out_rms * 6.25f / 5.f));
	}

	// ------------------------------------------------------------ drawing
	struct R { float x, y, w, h; };
	void frameBox(NVGcontext* vg, R r, const char* title, const char* right = nullptr) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, r.x, r.y, r.w, r.h, 2.f);
		nvgStrokeColor(vg, vd::FRAME);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		vd::text(vg, r.x + 3.f, r.y + 3.f, title, mm2px(2.1f), vd::BLUE_MID, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		if (right) vd::text(vg, r.x + r.w - 3.f, r.y + 3.f, right, mm2px(2.1f), vd::BLUE_MID, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}
	void readout(NVGcontext* vg, float x, float y, const char* label, const std::string& value, NVGcolor c = vd::BLUE) {
		vd::text(vg, x, y, label, mm2px(1.8f), vd::BLUE_MID, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		vd::text(vg, x, y + mm2px(2.4f), value.c_str(), mm2px(2.9f), c, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	}

	void drawReadouts(NVGcontext* vg, R r, const PhonaTap::Frame& fr) {
		const ChimVis& v = fr.vis;
		float cw = r.w / 4.f, y1 = r.y, y2 = r.y + mm2px(6.6f);
		readout(vg, r.x, y1, "PITCH", v.voicing > 0.1f ? string::f("%.1f Hz", v.f0) : "--");
		readout(vg, r.x + cw, y1, "NOTE", v.voicing > 0.1f ? noteName(v.f0) : "--");
		readout(vg, r.x + 2 * cw, y1, "LUNG", string::f("%.1f cmH2O", v.p_lung));
		float lvl = outLevelDb(v);
		readout(vg, r.x + 3 * cw, y1, "LEVEL", lvl > -90.f ? string::f("%.0f dB", lvl) : "--", lvl > -1.f ? vd::RED : vd::BLUE);
		readout(vg, r.x, y2, "VOICING", string::f("%.0f%%", 100.f * clamp(v.voicing, 0.f, 1.f)));
		readout(vg, r.x + cw, y2, "OPEN Q", oq > 0.f ? string::f("%.2f", oq) : "--");
		readout(vg, r.x + 2 * cw, y2, "NASAL PORT", string::f("%.2f cm2", v.velum_port), v.velum_port > 0.05f ? vd::AMBER : vd::BLUE);
		// the narrowest place in the mouth and throat
		int mi = -1;
		float ma = INFINITY;
		for (int i = 2; i + 1 < v.n_grid; i++)
			if (v.g_area[i] < ma) { ma = v.g_area[i]; mi = i; }
		std::string narrow = mi < 0 ? "--" : ma <= 0.0005f ? "CLOSED" : string::f("%.2f cm2", ma);
		readout(vg, r.x + 3 * cw, y2, "NARROWEST", narrow, ma < 0.3f ? vd::WHITE : vd::BLUE);
		// syllable and speech sound, top right corner of the panel title row is busy: put them in the spectrum box
	}

	float fx(R r, float f) { return r.x + r.w * std::log(f / F_LO) / std::log(F_HI / F_LO); }
	float dby(R r, float db) { return r.y + r.h * (DB_TOP - clamp(db, DB_BOT, DB_TOP)) / (DB_TOP - DB_BOT); }

	void drawSpectrum(NVGcontext* vg, R box, const PhonaTap::Frame& fr) {
		frameBox(vg, box, "SPECTRUM", "TRACT RESPONSE");
		R r{box.x + 4.f, box.y + mm2px(4.2f), box.w - 8.f, box.h - mm2px(4.2f) - mm2px(3.4f)};
		// grid: frequencies and every 12 dB
		const float grid[] = {100, 200, 500, 1000, 2000, 5000};
		const char* gl[] = {"100", "200", "500", "1k", "2k", "5k"};
		for (int i = 0; i < 6; i++) {
			float x = fx(r, grid[i]);
			vd::hair(vg, x, r.y, x, r.y + r.h, vd::FRAME, 0.8f);
			vd::text(vg, x, r.y + r.h + 1.f, gl[i], mm2px(1.7f), vd::BLUE_DIM, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
		for (float db = DB_TOP; db >= DB_BOT; db -= 12.f) vd::hair(vg, r.x, dby(r, db), r.x + r.w, dby(r, db), vd::FRAME, 0.6f);
		// output spectrum: amber, filled faintly
		if (specN > 0) {
			Pts p;
			float binHz = specRate / specN;
			int k0 = std::max(1, (int) (F_LO / binHz)), k1 = std::min(specN / 2 - 1, (int) (F_HI / binHz) + 1);
			// at most ~2 points per px: take the maximum of the bins in each column
			float lastX = -1e9f, colMax = DB_BOT;
			for (int k = k0; k <= k1; k++) {
				float x = fx(r, std::max(F_LO, k * binHz));
				colMax = std::max(colMax, spec[k]);
				if (x - lastX >= 0.7f || k == k1) {
					p.push_back(Vec(x, dby(r, colMax)));
					lastX = x;
					colMax = DB_BOT;
				}
			}
			if (p.size() > 1) {
				nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
				nvgBeginPath(vg);
				nvgMoveTo(vg, p.front().x, r.y + r.h);
				for (Vec q : p) nvgLineTo(vg, q.x, q.y);
				nvgLineTo(vg, p.back().x, r.y + r.h);
				nvgClosePath(vg);
				nvgFillColor(vg, nvgTransRGBAf(vd::AMBER, 0.13f));
				nvgFill(vg);
				nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
				vd::beam(vg, p, vd::AMBER, 0.9f, 0.9f);
			}
		}
		// the tract's response, placed so its peak sits on the spectrum's loudest harmonic
		if (tract.ok) {
			float specTop = DB_BOT, curveTop = -INFINITY;
			for (float d : spec) specTop = std::max(specTop, d);
			for (float c : curve) curveTop = std::max(curveTop, c);
			float off = (specTop > DB_BOT + 6.f ? specTop + 3.f : -12.f) - curveTop;
			Pts p;
			for (int i = 0; i < CURVE_N; i++) {
				float f = F_LO * std::pow(F_HI / F_LO, (float) i / (CURVE_N - 1));
				p.push_back(Vec(fx(r, f), dby(r, curve[i] + off)));
			}
			vd::beam(vg, p, vd::BLUE, 1.2f, 0.95f, true);
			// formant marks
			for (int i = 0; i < nFormants; i++) {
				float x = fx(r, formant[i]);
				for (float y = r.y + mm2px(2.4f); y < r.y + r.h; y += 5.f) vd::hair(vg, x, y, x, std::min(y + 2.5f, r.y + r.h), vd::WHITE, 0.8f, 0.55f);
				vd::text(vg, x, r.y + 1.f, string::f("F%d", i + 1).c_str(), mm2px(1.9f), vd::WHITE, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			}
			std::string fs;
			for (int i = 0; i < nFormants; i++) fs += string::f("%sF%d %.0f", i ? "  " : "", i + 1, formant[i]);
			vd::text(vg, box.x + box.w / 2.f, box.y + 3.f, fs.c_str(), mm2px(2.1f), vd::WHITE, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
	}

	void drawTract(NVGcontext* vg, R box, const PhonaTap::Frame& fr) {
		const ChimVis& v = fr.vis;
		float len = v.ph_len + v.or_len;
		frameBox(vg, box, len > 0.f ? string::f("TRACT  %.1f cm", len).c_str() : "TRACT");
		if (v.n_nodes < 2 || len <= 0.f) return;
		R r{box.x + mm2px(9.f), box.y + mm2px(4.f), box.w - mm2px(16.f), box.h - mm2px(7.4f)};
		float mid = r.y + r.h * 0.5f;
		// radius scale: 3 cm diameter fills the height
		float rs = r.h * 0.5f / 1.6f;
		auto X = [&](float s) { return r.x + r.w * s / len; };
		auto rad = [&](float a) { return rs * std::sqrt(std::max(0.f, a) / (float) M_PI); };
		// sections lit by sound pressure (amber), as in the View
		int n = v.n_nodes;
		for (int i = 0; i < n; i++) {
			float s0 = i == 0 ? 0.f : 0.5f * (v.node_s[i - 1] + v.node_s[i]);
			float s1 = i == n - 1 ? len : 0.5f * (v.node_s[i] + v.node_s[i + 1]);
			float h = rad(v.node_area[i]);
			float lvl = std::pow(vd::pressureLevel(v.pressure[i]), 0.85f);
			nvgBeginPath(vg);
			nvgRect(vg, X(s0), mid - h, X(s1) - X(s0) + 0.3f, 2.f * h);
			nvgFillColor(vg, nvgTransRGBAf(vd::heat(lvl), 0.25f + 0.55f * lvl));
			nvgFill(vg);
		}
		// walls (stepped, as the engine has them)
		Pts up, dn;
		for (int i = 0; i < n; i++) {
			float s0 = i == 0 ? 0.f : 0.5f * (v.node_s[i - 1] + v.node_s[i]);
			float s1 = i == n - 1 ? len : 0.5f * (v.node_s[i] + v.node_s[i + 1]);
			float h = rad(v.node_area[i]);
			up.push_back(Vec(X(s0), mid - h));
			up.push_back(Vec(X(s1), mid - h));
			dn.push_back(Vec(X(s0), mid + h));
			dn.push_back(Vec(X(s1), mid + h));
		}
		vd::beam(vg, up, vd::BLUE, 1.2f, 1.f, true);
		vd::beam(vg, dn, vd::BLUE, 1.2f, 1.f, true);
		// the nose's branch at the velum port: its width is the port
		float jx = X(v.ph_len);
		float jy = mid - rad(v.node_area[clamp(v.n_ph, 0, n - 1)]);
		float pw = std::max(1.f, 2.f * rad(v.velum_port));
		NVGcolor pc = v.velum_port > 0.05f ? vd::AMBER : vd::BLUE_DIM;
		float ny = box.y + mm2px(3.3f);
		vd::beam(vg, {Vec(jx - pw / 2.f, jy), Vec(jx - pw / 2.f, ny)}, pc, 1.f, 0.9f);
		vd::beam(vg, {Vec(jx + pw / 2.f, jy), Vec(jx + pw / 2.f, ny)}, pc, 1.f, 0.9f);
		vd::text(vg, jx, ny - 1.f, "NOSE", mm2px(1.7f), pc, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
		// turbulence sources
		for (int j = 0; j < v.n_noise && j < CHIM_VIS_NOISE; j++) {
			if (!(v.noise_level[j] > 1.f) || v.noise_s[j] < 0.f) continue;
			float in = std::min(1.f, std::log10(v.noise_level[j]) / 3.2f);
			if (in <= 0.05f) continue;
			float x = X(v.noise_s[j]);
			for (int k = 0; k < 12; k++) {
				float dx = (float) ((k * 7919) % 13) / 13.f * mm2px(3.f), dy = ((float) ((k * 104729) % 17) / 17.f - 0.5f) * mm2px(2.f);
				nvgBeginPath(vg);
				nvgCircle(vg, x + dx, mid + dy, 0.7f + 0.5f * (k % 3));
				nvgFillColor(vg, nvgTransRGBAf(vd::SPARK, 0.2f + 0.6f * in));
				nvgFill(vg);
			}
		}
		// end labels and a cm scale
		vd::text(vg, r.x - 3.f, mid, "GLOTTIS", mm2px(1.6f), vd::BLUE_MID, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		vd::text(vg, r.x + r.w + 3.f, mid, "LIPS", mm2px(1.6f), vd::BLUE_MID, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int cm = 0; cm <= (int) len; cm += 2) {
			float x = X((float) cm);
			vd::hair(vg, x, r.y + r.h + 0.5f, x, r.y + r.h + 2.5f, vd::BLUE_DIM, 0.8f);
			if (cm % 4 == 0) vd::text(vg, x, r.y + r.h + 2.8f, string::f("%d", cm).c_str(), mm2px(1.5f), vd::BLUE_DIM, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		}
	}

	void drawFolds(NVGcontext* vg, R box, const PhonaTap::Frame& fr) {
		const ChimVis& v = fr.vis;
		frameBox(vg, box, "FOLDS", oq > 0.f ? string::f("OQ %.2f", oq).c_str() : nullptr);
		R r{box.x + 4.f, box.y + mm2px(5.f), box.w - 8.f, box.h - mm2px(9.f)};
		vd::hair(vg, r.x, r.y + r.h, r.x + r.w, r.y + r.h, vd::FRAME, 0.8f);
		int n = v.n_scope;
		if (n < 2) return;
		// the scales grow with the signal but never below a typical sung cycle, so a nearly
		// closed glottis does not fill the box
		float amax = 0.f, fmax = 0.f, fmin = 0.f;
		for (int i = 0; i < n; i++) {
			amax = std::max(amax, v.glottal_area[i]);
			fmax = std::max(fmax, v.glottal_flow[i]);
			fmin = std::min(fmin, v.glottal_flow[i]);
		}
		float aScale = std::max(amax, 0.2f), fScale = std::max(fmax - fmin, 400.f);
		Pts a, f;
		for (int i = 0; i < n; i++) {
			float x = r.x + r.w * i / (n - 1);
			a.push_back(Vec(x, r.y + r.h - r.h * 0.9f * v.glottal_area[i] / aScale));
			f.push_back(Vec(x, r.y + r.h - r.h * 0.9f * (v.glottal_flow[i] - fmin) / fScale));
		}
		float on = v.voicing > 0.1f ? 1.f : 0.5f;
		vd::beam(vg, f, vd::AMBER, 1.f, 0.85f * on);
		vd::beam(vg, a, vd::WHITE, 1.2f, 0.95f * on, true);
		vd::text(vg, r.x, r.y + r.h + 2.f, string::f("AREA %.2f cm2", amax).c_str(), mm2px(1.7f), vd::WHITE, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		vd::text(vg, r.x + r.w, r.y + r.h + 2.f, string::f("FLOW %.0f cm3/s", fmax).c_str(), mm2px(1.7f), vd::AMBER, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
	}

	void drawVowels(NVGcontext* vg, R box, const PhonaTap::Frame& fr) {
		frameBox(vg, box, "VOWELS", "F2 / F1");
		R r{box.x + 4.f, box.y + mm2px(4.6f), box.w - 8.f, box.h - mm2px(6.2f)};
		// F2 right to left (front vowels left, as on the IPA chart), F1 top to bottom, both log
		const float f2a = 3200.f, f2b = 700.f, f1a = 230.f, f1b = 1100.f;
		auto X = [&](float f2) { return r.x + r.w * std::log(f2a / clamp(f2, f2b, f2a)) / std::log(f2a / f2b); };
		auto Y = [&](float f1) { return r.y + r.h * std::log(clamp(f1, f1a, f1b) / f1a) / std::log(f1b / f1a); };
		int b = fr.female ? 1 : 0;
		for (const RefVowel& rv : REF)
			vd::text(vg, X(rv.f2[b]), Y(rv.f1[b]), rv.ipa, mm2px(2.6f), vd::BLUE_MID, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true, 0.9f);
		// trail, oldest first
		for (size_t i = trail.size(); i-- > 1;) {
			const TrailPt &p = trail[i], &q = trail[i - 1];
			if (p.f1 <= 0.f || q.f1 <= 0.f) continue;
			vd::beam(vg, {Vec(X(p.f2), Y(p.f1)), Vec(X(q.f2), Y(q.f1))}, vd::GREEN, 1.f, 0.7f * q.a);
		}
		if (!trail.empty() && trail.front().a > 0.9f && trail.front().f1 > 0.f) {
			float x = X(trail.front().f2), y = Y(trail.front().f1);
			nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
			nvgBeginPath(vg);
			nvgCircle(vg, x, y, 4.f);
			nvgFillColor(vg, nvgTransRGBAf(vd::GREEN, 0.3f));
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgCircle(vg, x, y, 1.8f);
			nvgFillColor(vg, vd::core(vd::GREEN));
			nvgFill(vg);
			nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
		}
	}

	void drawContent(const DrawArgs& args) {
		NVGcontext* vg = args.vg;
		nvgSave(vg);
		nvgScissor(vg, 0, 0, box.size.x, box.size.y);
		bool linked = module && module->linked.load();
		const PhonaTap::Frame* fr = nullptr;
		if (linked) {
			const PhonaTap::Frame& f = module->read();
			if (f.vis.valid) fr = &f;
		}
		if (module) {
			module->uiVoices = fr ? fr->voices : 0;
			module->uiShown = fr ? fr->voice : -1;
		}
		float pad = mm2px(1.2f), w = box.size.x - 2 * pad;
		R rRead{pad + 2.f, pad, w - 4.f, mm2px(12.4f)};
		R rSpec{pad, mm2px(14.4f), w, mm2px(33.f)};
		R rTract{pad, mm2px(49.f), w, mm2px(22.f)};
		float half = (w - mm2px(1.6f)) / 2.f;
		R rFolds{pad, mm2px(73.f), half, box.size.y - mm2px(73.f) - pad};
		R rVow{pad + half + mm2px(1.6f), mm2px(73.f), half, box.size.y - mm2px(73.f) - pad};
		if (!fr) {
			const char* msg = !module ? "PHONA SCOPE" : !linked ? "PLACE NEXT TO PHONA" : "WAITING FOR PHONA";
			for (R r : {rSpec, rTract, rFolds, rVow}) frameBox(vg, r, "");
			vd::text(vg, box.size.x / 2.f, mm2px(6.f), msg, mm2px(3.f), vd::BLUE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgRestore(vg);
			return;
		}
		if (fr->seq != lastSeq) {
			lastSeq = fr->seq;
			analyse(*fr);
		}
		if (fr->beat) {
			drawReadouts(vg, rRead, *fr);
			vd::text(vg, box.size.x / 2.f, mm2px(30.f), "BEATBOX: NOT SHOWN", mm2px(3.f), vd::BLUE_MID, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgRestore(vg);
			return;
		}
		drawReadouts(vg, rRead, *fr);
		drawSpectrum(vg, rSpec, *fr);
		drawTract(vg, rTract, *fr);
		drawFolds(vg, rFolds, *fr);
		drawVowels(vg, rVow, *fr);
		// voice, syllable and speech sound, in the tract box's title row
		std::string id = string::f("V%d", fr->voice + 1);
		if (fr->lyrics && fr->syllable[0]) {
			std::string syl = fr->syllable;
			for (char& ch : syl) ch = std::toupper((unsigned char) ch);
			id += "  " + syl;
		}
		float idx = rTract.x + rTract.w - 3.f;
		if (fr->vis.phoneme[0]) {
			vd::text(vg, idx, rTract.y + 2.f, string::f("/%s/", fr->vis.phoneme).c_str(), mm2px(2.6f), vd::WHITE, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, true);
			idx -= mm2px(9.f);
		}
		vd::text(vg, idx, rTract.y + 3.f, id.c_str(), mm2px(2.1f), vd::AMBER, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		nvgRestore(vg);
	}

	void draw(const DrawArgs& args) override {
		if (!module) drawContent(args); // module browser
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) drawContent(args); // the display glows in a dark room
		TransparentWidget::drawLayer(args, layer);
	}
};

struct PhonaScopeWidget : ModuleWidget {
	// Panel geometry (mm), the same numbers as in tools/panel_view.py (scope variant).
	static constexpr float W = 101.6f, DISP_X0 = 1.6f, DISP_Y0 = 9.4f, DISP_Y1 = 114.4f;
	PhonaScopeWidget(PhonaScope* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PhonaScope.svg")));
		PanelLabel* title = new PanelLabel(mm2px(Vec(W / 2.f, 4.1f)), "PHONA SCOPE", 12.f);
		title->color = nvgRGB(0xf3, 0xeb, 0xdc);
		title->spacing = 2.5f;
		addChild(title);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(W - 4.6f, 4.1f)), module, PhonaTap::LINK_LIGHT));
		ScopeDisplay* d = new ScopeDisplay;
		d->module = module;
		d->box.pos = mm2px(Vec(DISP_X0 + 0.6f, DISP_Y0 + 0.6f));
		d->box.size = mm2px(Vec(W - 2 * DISP_X0 - 1.2f, DISP_Y1 - DISP_Y0 - 1.2f));
		addChild(d);
		addChild(new PanelLabel(mm2px(Vec(9.f, 121.4f)), "VOICE", 7.f));
		VoiceSelector* sel = createParam<VoiceSelector>(mm2px(Vec(17.5f, 118.2f)), module, PhonaTap::VOICE_PARAM);
		sel->box.size = mm2px(Vec(48.f, 6.4f));
		addParam(sel);
	}
};

Model* modelPhonaScope = createModel<PhonaScope, PhonaScopeWidget>("PhonaScope");
