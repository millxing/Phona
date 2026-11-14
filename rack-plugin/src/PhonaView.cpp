#include "PhonaTap.hpp"
#include "panel.hpp"
#include "vector.hpp"
#include "TapWidgets.hpp"
#include <vector>
#include <deque>
#include <random>

// Phona View's display: the web app's vector-display side view (engine/src/ui/render/anatomy.ts
// and profile.ts), drawn with NanoVG from Phona's snapshots. Glowing outlines on black: blue for
// the body; tongue green, jaw red, lips pink, soft palate white. Sound is amber: the airway's
// cross-sections light with sound pressure, streaks move with the airflow, the nasal cavity
// glows when air goes through the nose, and white sparks show turbulence (s, f, sh, bursts).
// An inset shows the vocal folds from above: how far apart they are and how much they vibrate.
// All coordinates are body cm (x forward, y up, glottis near the origin) until drawn.

namespace {

// the visible part of the head (profile.ts VIEW)
const float VIEW_X0 = -7.5f, VIEW_X1 = 10.25f, VIEW_Y0 = -4.55f, VIEW_Y1 = 19.9f;

using vd::Pts;
using vd::heat;
using vd::pressureLevel;
using vd::smoothstep;
const NVGcolor V_BLUE = vd::BLUE, V_BLUE_MID = vd::BLUE_MID, V_BLUE_DIM = vd::BLUE_DIM, V_GREEN = vd::GREEN, V_RED = vd::RED,
	V_PINK = vd::PINK, V_WHITE = vd::WHITE, V_AMBER = vd::AMBER, V_SPARK = vd::SPARK, V_STREAK = vd::STREAK;

Vec rotateAbout(Vec p, Vec c, float ang) {
	float s = std::sin(ang), co = std::cos(ang);
	float dx = p.x - c.x, dy = p.y - c.y;
	return Vec(c.x + dx * co - dy * s, c.y + dx * s + dy * co);
}
Pts catmullRom(const Pts& p, int per) {
	if (p.size() < 3) return p;
	Pts o;
	for (size_t i = 0; i + 1 < p.size(); i++) {
		Vec p0 = p[i ? i - 1 : 0], p1 = p[i], p2 = p[i + 1], p3 = p[std::min(p.size() - 1, i + 2)];
		for (int k = 0; k < per; k++) {
			float t = (float) k / per, t2 = t * t, t3 = t2 * t;
			o.push_back(Vec(
				0.5f * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
				0.5f * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3)));
		}
	}
	o.push_back(p.back());
	return o;
}
// corner cutting; closes the curve (vector.ts chaikin)
Pts chaikin(const Pts& in, int it) {
	Pts q = in;
	if (q.size() > 1 && q.front().x == q.back().x && q.front().y == q.back().y) q.pop_back();
	for (int k = 0; k < it; k++) {
		Pts o;
		for (size_t i = 0; i < q.size(); i++) {
			Vec a = q[i], b = q[(i + 1) % q.size()];
			o.push_back(Vec(a.x * 0.75f + b.x * 0.25f, a.y * 0.75f + b.y * 0.25f));
			o.push_back(Vec(a.x * 0.25f + b.x * 0.75f, a.y * 0.25f + b.y * 0.75f));
		}
		q = o;
	}
	if (!q.empty()) q.push_back(q.front());
	return q;
}
Pts ellipse(Vec c, float rx, float ry, float rot = 0.f, int n = 40) {
	float cr = std::cos(-rot), sr = std::sin(-rot);
	Pts o;
	for (int i = 0; i <= n; i++) {
		float an = (float) i / n * 2.f * M_PI;
		float x = std::cos(an) * rx, y = std::sin(an) * ry;
		o.push_back(Vec(c.x + x * cr - y * sr, c.y + x * sr + y * cr));
	}
	return o;
}
Pts roundRect(Vec p0, Vec p1, float r) {
	float x0 = std::min(p0.x, p1.x), x1 = std::max(p0.x, p1.x), y0 = std::min(p0.y, p1.y), y1 = std::max(p0.y, p1.y);
	float rr = std::min(r, std::min((x1 - x0) / 2, (y1 - y0) / 2));
	Pts o;
	auto corner = [&](float cx, float cy, float a0) {
		for (int i = 0; i <= 6; i++) {
			float an = a0 + i / 6.f * (M_PI / 2);
			o.push_back(Vec(cx + std::cos(an) * rr, cy + std::sin(an) * rr));
		}
	};
	corner(x1 - rr, y1 - rr, 0);
	corner(x0 + rr, y1 - rr, M_PI / 2);
	corner(x0 + rr, y0 + rr, M_PI);
	corner(x1 - rr, y0 + rr, 3 * M_PI / 2);
	o.push_back(o[0]);
	return o;
}

Pts linePts(const ChimVisLine& l) {
	Pts o;
	for (int i = 0; i < l.n && i < CHIM_VIS_PTS; i++) o.push_back(Vec(l.x[i], l.y[i]));
	return o;
}

// The head around the vocal tract for one snapshot (profile.ts buildProfile): the outlines the
// engine does not draw itself (skin, skull, brain, vertebrae, palate, larynx), placed from the
// body's size and the jaw and larynx positions.
struct Profile {
	Pts skinLower, skinUpper, inset, skull, brain, sulcus, eye, nasal, palOral, palNasal, jawLower, jawUpper,
		hyoid, thyroid, outerWall, glottisWall, lowerTeeth;
	Vec eyeC;
	std::vector<Pts> vertebrae, cords, turbinates, trachea, rings, fibres, folds;
	// the vocal tract itself, from the engine
	Pts outer, inner, tongue, velum, upperLip, lowerLip, upperTeeth, epiglottis;
	float k = 1.f;

	void build(const ChimVis& f) {
		k = f.scale > 0.f ? f.scale : 1.f;
		const float ph = f.pharynx > 0.f ? f.pharynx : 1.f;
		auto P = [&](float x, float y) {
			float xx = x > 0.5f ? 0.5f + (x - 0.5f) * 0.86f : x;
			float yy = y < 7.f ? 7.f - (7.f - y) * ph : y;
			return Vec(xx * k, yy * k);
		};
		auto B = [&](float x, float y) { return Vec(x * k, (y < 7.f ? 7.f - (7.f - y) * ph : y) * k); };
		Vec condyle = P(-2.5f, 10.5f);
		float phi = -clamp(f.jaw, -0.2f, 3.f) * k / (P(6.95f, 7.75f) - condyle).norm();
		auto J = [&](float x, float y) { return rotateAbout(P(x, y), condyle, phi); };
		const float lar = f.larynx * k;
		auto wallX = [](float y) { return -1.2f - 0.025f * y; };

		outer = linePts(f.line[CHIM_VIS_OUTER]);
		inner = linePts(f.line[CHIM_VIS_INNER]);
		tongue = linePts(f.line[CHIM_VIS_TONGUE]);
		velum = linePts(f.line[CHIM_VIS_VELUM]);
		upperLip = linePts(f.line[CHIM_VIS_UPPER_LIP]);
		lowerLip = linePts(f.line[CHIM_VIS_LOWER_LIP]);
		upperTeeth = linePts(f.line[CHIM_VIS_UPPER_TEETH]);
		epiglottis = linePts(f.line[CHIM_VIS_EPIGLOTTIS]);
		if (epiglottis.size() > 3) epiglottis.resize(3);
		Pts jawBone = linePts(f.line[CHIM_VIS_JAW_BONE]);
		Pts lowerTeethF = linePts(f.line[CHIM_VIS_LOWER_TEETH]);

		Pts ulOut(upperLip.size() > 7 ? upperLip.begin() + 7 : upperLip.end(), upperLip.end()); // edge → nose
		Pts llOut(lowerLip.size() > 7 ? lowerLip.begin() + 7 : lowerLip.end(), lowerLip.end()); // edge → chin
		Pts neckFront = catmullRom({B(2.3f, -4.6f), B(2.38f, -1.5f), B(2.52f, 0.4f), B(3.0f, 1.7f), J(5.3f, 3.0f), J(7.25f, 3.35f), J(7.95f, 4.6f), J(7.55f, 5.7f)}, 4);
		Vec faceStart = ulOut.empty() ? B(7.f, 9.5f) : ulOut.back();
		Pts face = catmullRom({faceStart, B(7.05f, 10.6f), B(7.75f, 10.78f), B(8.75f, 11.05f), B(9.25f, 11.65f), B(8.95f, 12.6f), B(8.35f, 13.9f), B(7.7f, 15.0f), B(7.95f, 16.1f), B(7.9f, 18.1f), B(6.9f, 20.5f)}, 4);
		Pts cranium = catmullRom({B(6.9f, 20.5f), B(3.0f, 22.9f), B(-2.2f, 22.7f), B(-6.9f, 19.6f), B(-8.7f, 15.4f), B(-8.3f, 11.9f), B(-7.4f, 8.9f), B(-6.8f, 4.8f), B(-6.6f, 0.f), B(-6.6f, -4.6f)}, 4);
		skinLower = neckFront;
		skinLower.insert(skinLower.end(), llOut.rbegin(), llOut.rend());
		skinUpper = ulOut;
		skinUpper.insert(skinUpper.end(), face.begin() + 1, face.end());
		skinUpper.insert(skinUpper.end(), cranium.begin() + 1, cranium.end());
		inset = catmullRom({B(1.85f, -4.6f), B(1.9f, -0.8f), B(2.3f, 1.6f), J(5.2f, 3.6f), J(6.9f, 3.9f), J(7.4f, 4.9f), J(7.2f, 5.9f), B(6.6f, 9.9f), B(7.6f, 10.9f), B(8.55f, 11.5f), B(8.1f, 13.4f), B(7.25f, 15.2f), B(7.45f, 18.0f), B(3.0f, 22.2f), B(-2.0f, 22.0f), B(-6.3f, 19.2f), B(-7.9f, 15.3f), B(-7.6f, 12.0f), B(-6.7f, 8.7f), B(-6.1f, 4.6f), B(-6.0f, -4.6f)}, 3);
		skull = catmullRom({B(7.0f, 16.2f), B(7.3f, 18.5f), B(6.0f, 20.7f), B(3.0f, 22.1f), B(-2.0f, 22.0f), B(-6.3f, 19.2f), B(-7.9f, 15.3f), B(-7.4f, 12.6f), B(-4.6f, 12.2f), B(-2.2f, 13.3f), B(1.0f, 13.7f), B(4.1f, 14.1f), B(6.2f, 14.9f)}, 4);
		brain = catmullRom({B(6.55f, 16.3f), B(6.75f, 18.4f), B(5.5f, 20.1f), B(2.8f, 21.4f), B(-1.9f, 21.3f), B(-5.8f, 18.8f), B(-7.1f, 15.4f), B(-6.7f, 13.1f), B(-4.4f, 12.8f), B(-2.1f, 13.9f), B(1.0f, 14.3f), B(4.0f, 14.7f), B(5.9f, 15.4f)}, 4);
		sulcus = catmullRom({B(5.4f, 19.4f), B(2.4f, 20.5f), B(-1.4f, 20.4f), B(-4.8f, 18.4f)}, 4);
		eyeC = B(6.5f, 15.4f);
		eye = ellipse(eyeC, 1.1f * k, 0.95f * k);
		vertebrae.clear();
		for (int i = 0; i < 9; i++) {
			float y0 = -4.4f + i * 1.75f;
			if (y0 > 10.5f) break;
			float x0 = wallX(y0) - 2.25f;
			vertebrae.push_back(roundRect(P(x0, y0 + 1.4f), P(x0 + 1.6f, y0), 0.32f * k));
		}
		cords = {{P(-5.3f, -4.6f), P(-4.5f, -4.6f), P(-4.5f, 11.8f), P(-5.1f, 12.4f), P(-5.6f, 11.6f)}, {P(-5.1f, -4.6f), P(-4.7f, -4.6f), P(-4.72f, 11.4f), P(-5.15f, 11.6f)}};
		nasal = catmullRom({B(0.3f, 11.2f), B(1.1f, 10.25f), B(2.7f, 10.75f), B(4.6f, 10.85f), B(6.4f, 10.65f), B(7.6f, 10.85f), B(8.45f, 11.3f), B(8.0f, 12.2f), B(6.7f, 13.4f), B(4.4f, 13.9f), B(1.8f, 13.6f), B(0.2f, 12.6f)}, 4);
		turbinates.clear();
		const float tb[3][4] = {{3.0f, 11.45f, 2.1f, 0.32f}, {3.5f, 12.45f, 1.9f, 0.3f}, {4.0f, 13.3f, 1.45f, 0.22f}};
		for (auto& t : tb) turbinates.push_back(ellipse(B(t[0], t[1]), t[2] * k, t[3] * k, -0.04f));
		const float palRaw[10][2] = {{1.0f, 9.6f}, {1.8f, 9.92f}, {2.7f, 10.18f}, {3.6f, 10.3f}, {4.4f, 10.25f}, {5.1f, 10.05f}, {5.7f, 9.72f}, {6.15f, 9.36f}, {6.55f, 9.15f}, {7.0f, 9.6f}};
		palOral.clear();
		palNasal.clear();
		for (auto& p : palRaw) {
			palOral.push_back(P(p[0], p[1]));
			palNasal.push_back(P(p[0], p[1]).plus(Vec(0.f, 0.55f * k)));
		}
		// the mandible's body only: its ramus is lateral, not in the midsagittal plane
		jawLower.clear();
		jawUpper.clear();
		for (Vec p : jawBone)
			if (p.x > 0.8f * k) {
				jawLower.push_back(p);
				jawUpper.push_back(p.plus(Vec(0.f, 0.75f * k)));
			}
		Vec g(f.glottis[0], f.glottis[1]);
		Vec hy(P(1.6f, 0.f).x, P(0.f, 2.9f).y + 0.6f * lar);
		hyoid = ellipse(hy, 0.5f * k, 0.28f * k, 0.3f, 28);
		thyroid = {g.plus(Vec(0.75f * k, -1.6f * k)), g.plus(Vec(1.75f * k, -0.6f * k)), g.plus(Vec(1.95f * k, 1.2f * k)), g.plus(Vec(1.45f * k, 1.9f * k)),
			g.plus(Vec(1.15f * k, 1.1f * k)), g.plus(Vec(1.3f * k, -0.4f * k)), g.plus(Vec(0.75f * k, -1.6f * k))};
		trachea = {{g.plus(Vec(-0.65f * k, 0.f)), Vec(g.x - 0.75f * k, -4.8f)}, {g.plus(Vec(0.75f * k, 0.f)), Vec(g.x + 0.95f * k, -4.8f)}};
		rings.clear();
		for (int i = 0; i < 4; i++) {
			float y = g.y - (0.9f + i * 1.0f) * k;
			if (y < -4.4f) break;
			rings.push_back({Vec(g.x + 0.8f * k, y), Vec(g.x + 1.05f * k, y - 0.05f), Vec(g.x + 1.05f * k, y - 0.5f * k), Vec(g.x + 0.82f * k, y - 0.45f * k), Vec(g.x + 0.8f * k, y)});
		}
		// tongue muscle fibres, fanning from the floor of the mouth (kept inside the tongue: NanoVG
		// cannot clip to its outline as the web view does)
		Vec origin = J(6.0f, 5.45f);
		Vec tc(f.tongue_center[0], f.tongue_center[1]);
		fibres.clear();
		for (int i = 0; i <= 16; i++) {
			float ang = M_PI * (0.18f + 0.9f * i / 16.f);
			Vec end(tc.x + std::cos(ang) * f.tongue_radius * 0.92f, tc.y + std::sin(ang) * f.tongue_radius * 0.92f);
			Vec ctrl(tc.x, (tc.y + origin.y) / 2.f);
			Pts c;
			for (int j = 0; j <= 12; j++) {
				float u = j / 12.f;
				c.push_back(Vec((1 - u) * (1 - u) * origin.x + 2 * (1 - u) * u * ctrl.x + u * u * end.x,
					(1 - u) * (1 - u) * origin.y + 2 * (1 - u) * u * ctrl.y + u * u * end.y));
			}
			fibres.push_back(c);
		}
		outerWall.clear();
		for (Vec p : outer)
			if (p.y > g.y || p.x > 0.f) outerWall.push_back(p);
		Vec wallAtGlottis(wallX(g.y / k) * k, g.y);
		Vec firstAbove = wallAtGlottis;
		for (Vec p : outer)
			if (p.y > g.y) { firstAbove = p; break; }
		glottisWall = {wallAtGlottis, firstAbove};
		folds = {{Vec(g.x - 0.62f * k, g.y + 0.05f), Vec(g.x - 0.1f, g.y + 0.12f * k), Vec(g.x - 0.62f * k, g.y + 0.32f * k), Vec(g.x - 0.62f * k, g.y + 0.05f)},
			{Vec(g.x + 0.72f * k, g.y + 0.05f), Vec(g.x + 0.12f, g.y + 0.12f * k), Vec(g.x + 0.72f * k, g.y + 0.32f * k), Vec(g.x + 0.72f * k, g.y + 0.05f)}};
		lowerTeeth = lowerTeethF;
		if (!lowerTeethF.empty()) {
			lowerTeeth.push_back(J(6.8f, 6.0f));
			lowerTeeth.push_back(J(6.4f, 6.2f));
			lowerTeeth.push_back(lowerTeethF[0]);
		}
	}
};

// A resting voice, for the module browser and for a View with no Phona beside it.
const ChimVis* restVis(bool female) {
	static ChimVis* cache[2] = {nullptr, nullptr};
	static bool tried[2] = {false, false};
	int i = female ? 1 : 0;
	if (!tried[i]) {
		tried[i] = true;
		ChimVis* v = new ChimVis();
		if (chim_vis_rest(female ? 1.0 : 0.0, v)) cache[i] = v;
		else delete v;
	}
	return cache[i];
}

} // namespace

// The display: the head, drawn into its own box.
struct ViewDisplay : TransparentWidget {
	PhonaView* module = nullptr;
	Profile geo;
	unsigned geoSeq = ~0u;
	bool geoRest = false;
	// screen mapping
	float s = 10.f, ox = 0.f, oy = 0.f, lw = 1.f;
	// afterglow: the tongue, jaw and lips of the last few snapshots
	struct Echo { Pts tongue, jaw, upperLip, lowerLip; };
	std::deque<Echo> echoes;
	// airflow streaks
	struct Particle { float s, u, life, px, py; };
	std::vector<Particle> particles;
	std::minstd_rand rng{12345};
	double lastTime = -1.0;
	// the eye's wandering gaze (profile.ts Gaze)
	float gazePitch = 0.f, gazeYaw = 0.f, toPitch = 0.f, toYaw = 0.f, nextSaccade = 2.f, clock = 0.f;
	// fold shimmer phase
	float shimmer = 0.f;

	float rnd() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }

	ViewDisplay() {
		for (int i = 0; i < 110; i++) particles.push_back({rnd() * 17.f, rnd() * 2.f - 1.f, rnd(), NAN, NAN});
	}

	float X(Vec p) const { return ox + p.x * s; }
	float Y(Vec p) const { return oy - p.y * s; }
	Vec S(Vec p) const { return Vec(X(p), Y(p)); }

	// a glowing beam through body points (vector.ts beam): a wide faint halo, the line, and for
	// the brightest lines a whitish core. Additive, so crossings get brighter.
	void beam(NVGcontext* vg, const Pts& pts, NVGcolor c, float w, float alpha = 1.f, bool hot = false, bool close = false) {
		if (pts.size() < 2 || alpha <= 0.f) return;
		Pts q;
		q.reserve(pts.size());
		for (Vec p : pts) q.push_back(S(p));
		vd::beam(vg, q, c, w * lw, alpha, hot, close);
	}
	void seg(NVGcontext* vg, Vec a, Vec b, NVGcolor c, float w, float alpha = 1.f) { beam(vg, {a, b}, c, w, alpha); }
	void dot(NVGcontext* vg, Vec p, float r, NVGcolor c, float alpha) {
		nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
		nvgBeginPath(vg);
		nvgCircle(vg, X(p), Y(p), r * lw);
		nvgFillColor(vg, nvgTransRGBAf(c, alpha));
		nvgFill(vg);
		nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
	}
	void text(NVGcontext* vg, float x, float y, const char* str, float size, NVGcolor c, int align, bool ipa = false, float alpha = 1.f) {
		vd::text(vg, x, y, str, size, c, align, ipa, alpha);
	}

	// a point in the airway at distance `sp` from the glottis, `u` across it (−1..1)
	bool gridPoint(const ChimVis& f, float sp, float u, Vec& out) {
		int n = f.n_grid;
		if (n < 2) return false;
		if (sp <= f.g_s[0]) {
			float t = std::max(0.f, sp / std::max(0.01f, f.g_s[0]));
			Vec base(f.glottis[0], f.glottis[1]);
			Vec mid((f.g_in_x[0] + f.g_out_x[0]) / 2.f, (f.g_in_y[0] + f.g_out_y[0]) / 2.f);
			out = Vec(base.x + (mid.x - base.x) * t + u * 0.35f * 0.5f, base.y + (mid.y - base.y) * t);
			return true;
		}
		if (sp >= f.g_s[n - 1]) return false;
		int lo = 0, hi = n - 1;
		while (hi - lo > 1) {
			int m = (lo + hi) / 2;
			if (f.g_s[m] <= sp) lo = m;
			else hi = m;
		}
		float t = (sp - f.g_s[lo]) / std::max(1e-6f, f.g_s[hi] - f.g_s[lo]);
		float ix = f.g_in_x[lo] + (f.g_in_x[hi] - f.g_in_x[lo]) * t, iy = f.g_in_y[lo] + (f.g_in_y[hi] - f.g_in_y[lo]) * t;
		float oxx = f.g_out_x[lo] + (f.g_out_x[hi] - f.g_out_x[lo]) * t, oyy = f.g_out_y[lo] + (f.g_out_y[hi] - f.g_out_y[lo]) * t;
		float w = 0.5f + 0.5f * u * 0.82f;
		out = Vec(ix + (oxx - ix) * w, iy + (oyy - iy) * w);
		return true;
	}
	int nearestNode(const ChimVis& f, float sp) {
		int best = 0;
		float bd = INFINITY;
		for (int i = 0; i < f.n_nodes; i++) {
			float d = std::fabs(f.node_s[i] - sp);
			if (d < bd) { bd = d; best = i; }
		}
		return best;
	}

	void layout() {
		// the head fills the box below a band for the syllable and phoneme
		const float padX = 3.f, padT = mm2px(9.5f), padB = 2.f;
		s = std::max(1.f, std::min((box.size.x - 2 * padX) / (VIEW_X1 - VIEW_X0), (box.size.y - padT - padB) / (VIEW_Y1 - VIEW_Y0)));
		ox = (box.size.x - (VIEW_X1 - VIEW_X0) * s) / 2.f - VIEW_X0 * s;
		oy = padT + VIEW_Y1 * s;
		lw = s / 16.f;
	}

	void drawContent(const DrawArgs& args) {
		NVGcontext* vg = args.vg;
		layout();
		double now = system::getTime();
		float dt = lastTime < 0 ? 1.f / 60.f : clamp((float) (now - lastTime), 0.f, 0.1f);
		lastTime = now;
		clock += dt;

		// the snapshot to show
		const PhonaView::Frame* fr = nullptr;
		bool linked = module && module->linked.load();
		if (linked) {
			const PhonaView::Frame& r = module->read();
			if (r.vis.valid) fr = &r;
		}
		const ChimVis* vis = fr ? &fr->vis : restVis(false);
		if (!vis) return;
		const ChimVis& f = *vis;
		bool live = fr != nullptr;
		if (live) {
			module->uiVoices = fr->voices;
			module->uiShown = fr->voice;
		}
		else if (module) {
			module->uiVoices = 0;
			module->uiShown = -1;
		}
		bool fresh = live ? fr->seq != geoSeq || geoRest : !geoRest || geoSeq != ~0u;
		if (fresh) {
			geo.build(f);
			geoSeq = live ? fr->seq : ~0u;
			geoRest = !live;
			if (live) {
				echoes.push_front({geo.tongue, geo.jawLower, chaikin(geo.upperLip, 2), chaikin(geo.lowerLip, 2)});
				while (echoes.size() > 4) echoes.pop_back();
			}
			else echoes.clear();
		}
		const Profile& G = geo;
		const float k = G.k;

		nvgSave(vg);
		nvgScissor(vg, 0, 0, box.size.x, box.size.y);
		float dim = live ? 1.f : 0.45f;

		// deep structures, dim blue
		beam(vg, G.inset, V_BLUE_DIM, 0.9f, 0.9f * dim);
		beam(vg, G.skull, V_BLUE_DIM, 0.9f, dim, false, true);
		beam(vg, G.brain, V_BLUE_MID, 1.1f, dim, false, true);
		beam(vg, G.sulcus, V_BLUE_DIM, 0.9f, dim);
		beam(vg, G.eye, V_BLUE_MID, 1.1f, dim);
		drawEye(vg, G.eyeC, k, dt, dim);
		for (const Pts& v : G.vertebrae) beam(vg, v, V_BLUE_DIM, 1.f, dim);
		if (G.cords.size() == 2) {
			beam(vg, G.cords[0], V_BLUE_DIM, 0.8f, 0.8f * dim);
			beam(vg, G.cords[1], V_BLUE_DIM, 0.8f, 0.6f * dim);
		}
		// the nasal cavity, lit when air goes through the nose
		drawNasalGlow(vg, f, live);
		beam(vg, G.nasal, V_BLUE_MID, 1.f, dim, false, true);
		for (const Pts& t : G.turbinates) beam(vg, t, V_BLUE_DIM, 0.9f, dim);
		beam(vg, G.palOral, V_BLUE_MID, 1.1f, dim);
		beam(vg, G.palNasal, V_BLUE_MID, 1.1f, dim);
		beam(vg, G.hyoid, V_BLUE_DIM, 1.f, dim);
		beam(vg, G.thyroid, V_BLUE_DIM, 1.f, dim);
		for (const Pts& t : G.trachea) beam(vg, t, V_BLUE_MID, 1.f, dim);
		for (const Pts& r : G.rings) beam(vg, r, V_BLUE_DIM, 0.9f, dim);

		// the airway's cross-section lines, lit by sound pressure
		if (live && f.n_nodes > 0) {
			for (int i = 0; i < f.n_grid; i++) {
				float p = f.pressure[nearestNode(f, f.g_s[i])];
				float lvl = std::pow(pressureLevel(p), 0.85f);
				seg(vg, Vec(f.g_in_x[i], f.g_in_y[i]), Vec(f.g_out_x[i], f.g_out_y[i]), heat(lvl), 1.15f, 0.35f + 0.65f * lvl);
			}
			drawParticles(vg, f, dt);
		}
		else {
			for (int i = 0; i < f.n_grid; i++)
				seg(vg, Vec(f.g_in_x[i], f.g_in_y[i]), Vec(f.g_out_x[i], f.g_out_y[i]), V_BLUE_DIM, 0.9f, 0.5f * dim);
		}

		// afterglow: where the tongue, jaw and lips were a moment ago
		for (size_t i = 1; i < echoes.size(); i++) {
			float o = 0.1f + 0.07f * (echoes.size() - 1 - i);
			const Echo& e = echoes[i];
			beam(vg, e.tongue, V_GREEN, 1.f, o);
			beam(vg, e.jaw, V_RED, 1.f, o * 0.9f);
			beam(vg, e.upperLip, V_PINK, 1.f, o * 0.9f);
			beam(vg, e.lowerLip, V_PINK, 1.f, o * 0.9f);
		}

		// skin, airway walls, teeth, folds
		beam(vg, G.skinLower, V_BLUE, 1.7f, dim, live);
		beam(vg, G.skinUpper, V_BLUE, 1.7f, dim, live);
		beam(vg, G.outerWall, V_BLUE, 1.4f, dim, live);
		beam(vg, G.inner, V_BLUE, 1.3f, 0.9f * dim);
		beam(vg, G.glottisWall, V_BLUE, 1.2f, 0.8f * dim);
		beam(vg, G.epiglottis, V_BLUE, 2.2f, 0.85f * dim);
		beam(vg, G.upperTeeth, V_BLUE_MID, 1.1f, dim, false, true);
		beam(vg, G.lowerTeeth, V_BLUE_MID, 1.1f, dim);
		for (const Pts& fo : G.folds) beam(vg, fo, V_BLUE, 1.f, dim);

		// the articulators in their colours
		for (const Pts& fb : G.fibres) beam(vg, fb, V_GREEN, 0.8f, 0.32f * dim);
		beam(vg, G.tongue, V_GREEN, 1.6f, dim, live);
		beam(vg, G.jawLower, V_RED, 1.5f, dim, live);
		beam(vg, G.jawUpper, V_RED, 1.f, 0.45f * dim);
		beam(vg, chaikin(G.upperLip, 2), V_PINK, 1.5f, dim, live);
		beam(vg, chaikin(G.lowerLip, 2), V_PINK, 1.5f, dim, live);
		beam(vg, chaikin(G.velum, 3), V_WHITE, 1.25f, 0.92f * dim);

		if (live) drawTurbulence(vg, f);
		drawFolds(vg, f, live, dt);
		drawReadout(vg, fr, live, linked);
		nvgRestore(vg);
	}

	// the lens of the eye, which holds still, then darts somewhere new (anatomy.ts lens), and the
	// lids and brow at rest
	void drawEye(NVGcontext* vg, Vec c, float k, float dt, float dim) {
		if (clock >= nextSaccade) {
			bool centre = rnd() < 0.35f;
			toPitch = centre ? 0.f : (rnd() * 2.f - 1.f) * 0.55f;
			toYaw = centre ? 0.f : (rnd() * 2.f - 1.f) * 0.9f;
			nextSaccade = clock + 1.2f + rnd() * 3.5f;
		}
		float ease = 1.f - std::exp(-dt * 28.f);
		gazePitch += (toPitch - gazePitch) * ease;
		gazeYaw += (toYaw - gazeYaw) * ease;
		float off = 0.85f * k * std::cos(gazeYaw);
		float cp = std::cos(gazePitch), sp = std::sin(gazePitch);
		Vec lc(c.x + off * cp, c.y + off * sp);
		float rx = (0.18f + 0.2f * std::fabs(std::sin(gazeYaw))) * k, ry = 0.42f * k;
		Pts lens;
		for (int i = 0; i <= 20; i++) {
			float an = i / 20.f * 2.f * M_PI;
			float x = std::cos(an) * rx, y = std::sin(an) * ry;
			lens.push_back(Vec(lc.x + x * cp - y * sp, lc.y + x * sp + y * cp));
		}
		beam(vg, lens, V_BLUE_DIM, 0.9f, dim);
		const float rxe = 1.22f * k, rye = 1.08f * k, deg = M_PI / 180.f;
		auto arc = [&](float a0, float a1) {
			Pts o;
			for (int i = 0; i <= 14; i++) {
				float an = a0 + (a1 - a0) * i / 14.f;
				o.push_back(Vec(c.x + rxe * std::cos(an), c.y + rye * std::sin(an)));
			}
			return o;
		};
		Pts brow;
		for (int i = 0; i <= 12; i++) {
			float u = -1.f + i / 6.f;
			brow.push_back(Vec(c.x + 0.1f * k + 0.9f * k * u, c.y + 1.4f * k + 0.2f * k * (1 - u * u)));
		}
		beam(vg, arc(100 * deg, 30 * deg), V_BLUE, 1.15f, 0.9f * dim);
		beam(vg, arc(-78 * deg, -32 * deg), V_BLUE, 1.f, 0.75f * dim);
		beam(vg, brow, V_BLUE, 1.4f, 0.85f * dim);
	}

	// sound level 0..1 from the voice's output RMS
	static float outLevel(const ChimVis& f) {
		if (!(f.out_rms > 0.f)) return 0.f;
		return clamp((std::log10(f.out_rms) + 2.5f) / 2.2f, 0.f, 1.f); // 0.003 .. 0.5
	}

	// the nasal cavity fills with amber when the soft palate opens the way to the nose: by how far
	// it is open, and brighter while the voice sounds
	void drawNasalGlow(NVGcontext* vg, const ChimVis& f, bool live) {
		if (!live) return;
		float open = smoothstep(0.03f, 0.8f, f.velum_port);
		if (open <= 0.01f) return;
		float amt = open * (0.25f + 0.75f * outLevel(f));
		const Pts& n = geo.nasal;
		if (n.size() < 3) return;
		nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
		nvgBeginPath(vg);
		for (size_t i = 0; i < n.size(); i++) {
			if (i) nvgLineTo(vg, X(n[i]), Y(n[i]));
			else nvgMoveTo(vg, X(n[i]), Y(n[i]));
		}
		nvgClosePath(vg);
		Vec c0 = S(Vec((n.front().x + n[n.size() / 2].x) / 2.f, (n.front().y + n[n.size() / 2].y) / 2.f));
		NVGpaint paint = nvgRadialGradient(vg, c0.x, c0.y, 0.f, 4.5f * s, nvgTransRGBAf(V_AMBER, 0.55f * amt), nvgTransRGBAf(V_AMBER, 0.08f * amt));
		nvgFillPaint(vg, paint);
		nvgFill(vg);
		nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
		beam(vg, n, V_AMBER, 1.2f, 0.8f * amt, true, true);
		// the open port between the soft palate and the back wall of the throat
		const Pts& v = geo.velum;
		if (!v.empty() && !geo.outer.empty()) {
			Vec tipV = v[v.size() / 2];
			Vec wall = geo.outer[0];
			float bd = INFINITY;
			for (Vec p : geo.outer) {
				float d = (p - tipV).norm();
				if (p.x < tipV.x && d < bd) { bd = d; wall = p; }
			}
			for (int i = 0; i < 6; i++) {
				float u = (i + 0.5f) / 6.f;
				Vec p = tipV + (wall - tipV) * u;
				dot(vg, p.plus(Vec(0.f, 0.25f * k_())), 0.8f + 0.6f * rnd(), V_AMBER, 0.25f + 0.5f * amt * rnd());
			}
		}
	}
	float k_() const { return geo.k; }

	// airflow streaks moving at the local particle velocity u = U/A (anatomy.ts drawParticles)
	void drawParticles(NVGcontext* vg, const ChimVis& f, float dt) {
		float L = f.lip_s;
		NVGcolor col = V_STREAK;
		std::vector<std::pair<Vec, Vec>> band[3];
		for (Particle& p : particles) {
			int ni = nearestNode(f, p.s);
			float U = f.flow[ni];
			float A = std::max(0.05f, f.node_area[ni]);
			float u = U / A; // cm/s
			float vis = (u > 0 ? 1.f : -1.f) * 2.2f * std::log1p(std::fabs(u) / 30.f);
			p.s += vis * dt;
			p.life -= dt * 0.25f;
			if (p.s > L || p.s < 0.f || p.life <= 0.f || std::fabs(u) < 3.f) {
				if (!(std::fabs(u) < 3.f && p.life > 0.f)) {
					p.s = rnd() * 0.8f;
					p.u = rnd() * 2.f - 1.f;
					p.life = 0.6f + rnd() * 0.8f;
					p.px = NAN;
					continue;
				}
			}
			Vec pos;
			if (!gridPoint(f, p.s, p.u, pos)) continue;
			Vec q = S(pos);
			if (std::isfinite(p.px)) {
				int b = std::min(2, (int) (std::min(1.f, std::fabs(vis) / 6.f) * std::min(1.f, p.life) * 3.f));
				band[b].push_back({Vec(p.px, p.py), q});
			}
			p.px = q.x;
			p.py = q.y;
		}
		nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
		nvgLineCap(vg, NVG_ROUND);
		for (int b = 0; b < 3; b++) {
			if (band[b].empty()) continue;
			nvgBeginPath(vg);
			for (auto& sgm : band[b]) {
				nvgMoveTo(vg, sgm.first.x, sgm.first.y);
				nvgLineTo(vg, sgm.second.x, sgm.second.y);
			}
			nvgStrokeColor(vg, nvgTransRGBAf(col, 0.16f + 0.2f * b));
			nvgStrokeWidth(vg, 1.1f * lw);
			nvgStroke(vg);
		}
		nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
	}

	// white sparks where the air is turbulent (anatomy.ts drawTurbulence), at every source
	void drawTurbulence(NVGcontext* vg, const ChimVis& f) {
		for (int j = 0; j < f.n_noise && j < CHIM_VIS_NOISE; j++) {
			if (!(f.noise_level[j] > 1.f) || f.noise_s[j] < 0.f) continue;
			float intensity = std::min(1.f, std::log10(f.noise_level[j]) / 3.2f);
			if (intensity <= 0.05f) continue;
			for (int i = 0; i < 30; i++) {
				Vec pos;
				if (!gridPoint(f, f.noise_s[j] + 0.2f + rnd() * 1.2f, rnd() * 2.f - 1.f, pos)) continue;
				dot(vg, pos, 0.6f + rnd() * 1.4f, V_SPARK, 0.15f + 0.7f * intensity * rnd());
			}
		}
	}

	// The vocal folds seen from above, in a round inset under the chin, joined to the larynx: the
	// front of the folds points forward as in the side view. The gap at the back opens with the
	// folds' abduction (breathy voice, h, s, f); while they vibrate, their edges blur between
	// closed and fully open.
	void drawFolds(NVGcontext* vg, const ChimVis& f, bool live, float dt) {
		const float k = geo.k;
		Vec c(6.75f, -1.75f);
		float r = 1.85f;
		Vec g(f.glottis[0], f.glottis[1]);
		float dim = live ? 1.f : 0.45f;
		// leader from the larynx to the inset
		Vec from = g.plus(Vec(1.2f * k, 0.f)), to = c.plus(Vec(-r, 0.4f));
		const int dashes = 7;
		for (int i = 0; i < dashes; i++) {
			float a = (float) i / dashes, b = a + 0.5f / dashes;
			seg(vg, from + (to - from) * a, from + (to - from) * b, V_BLUE_DIM, 0.8f, 0.8f * dim);
		}
		Pts ringP = ellipse(c, r, r, 0.f, 48);
		nvgBeginPath(vg); // black disc under the inset
		nvgCircle(vg, X(c), Y(c), r * s);
		nvgFillColor(vg, nvgRGB(0, 0, 0));
		nvgFill(vg);
		beam(vg, ringP, V_BLUE_MID, 1.f, dim);
		text(vg, X(c), Y(c.plus(Vec(0.f, -r - 0.5f))), "FOLDS", 0.62f * s, V_BLUE_MID, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, false, dim);

		// inset units: front at +x
		const float u = 0.62f * r;
		float ab = live ? clamp(f.abduction / 0.4f, 0.f, 1.f) : 0.f;
		float back = 0.04f + 0.5f * ab;   // half-gap at the back (arytenoids)
		float vib = 0.f;                  // bow of the edges at full vibration
		if (live && f.n_scope > 0 && f.voicing > 0.05f) {
			float mx = 0.f, mn = INFINITY;
			for (int i = 0; i < f.n_scope; i++) { mx = std::max(mx, f.glottal_area[i]); mn = std::min(mn, f.glottal_area[i]); }
			vib = 0.55f * clamp(std::sqrt(std::max(0.f, mx - mn) / (0.18f * k * k)), 0.f, 1.f) * clamp(f.voicing, 0.f, 1.f);
		}
		shimmer += dt * 37.f;
		auto edge = [&](float side, float bow) {
			Pts o;
			for (int i = 0; i <= 16; i++) {
				float t = i / 16.f;                 // 0 front .. 1 back
				float x = u * (1.f - 2.f * t);
				float y = side * (back * t + bow * std::sin(M_PI * t) * (1.f - 0.35f * t));
				o.push_back(c.plus(Vec(x, y)));
			}
			return o;
		};
		// the folds' outer sides and the arytenoids
		for (float side : {-1.f, 1.f}) {
			Pts outerSide = {c.plus(Vec(u, 0.f)), c.plus(Vec(0.f, side * 0.62f * u)), c.plus(Vec(-u, side * (back + 0.55f * u)))};
			beam(vg, catmullRom(outerSide, 6), V_BLUE_MID, 1.f, 0.8f * dim);
			Pts ary = ellipse(c.plus(Vec(-u - 0.12f, side * (back + 0.22f))), 0.2f, 0.16f, 0.f, 16);
			beam(vg, ary, V_BLUE_MID, 1.f, 0.8f * dim);
		}
		// glottal airflow: amber in the opening, by lung pressure and how open it is
		float air = live ? clamp(f.p_lung / 8.f, 0.f, 1.f) * clamp((back + 0.5f * vib) / 0.35f, 0.f, 1.f) : 0.f;
		if (air > 0.02f) {
			Pts l = edge(-1.f, 0.5f * vib), rr = edge(1.f, 0.5f * vib);
			nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
			nvgBeginPath(vg);
			for (size_t i = 0; i < l.size(); i++) {
				if (i) nvgLineTo(vg, X(l[i]), Y(l[i]));
				else nvgMoveTo(vg, X(l[i]), Y(l[i]));
			}
			for (size_t i = rr.size(); i-- > 0;) nvgLineTo(vg, X(rr[i]), Y(rr[i]));
			nvgClosePath(vg);
			nvgFillColor(vg, nvgTransRGBAf(V_AMBER, 0.35f * air));
			nvgFill(vg);
			nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
		}
		for (float side : {-1.f, 1.f}) {
			if (vib > 0.01f) {
				beam(vg, edge(side, vib), V_WHITE, 0.9f, 0.3f * dim);          // fully open
				float ph = 0.5f + 0.5f * std::sin(shimmer + side * 0.3f) * (0.6f + 0.4f * rnd());
				beam(vg, edge(side, vib * ph), V_WHITE, 1.2f, 0.6f * dim);    // somewhere in the cycle
			}
			beam(vg, edge(side, 0.f), V_WHITE, 1.3f, 0.95f * dim, live);       // closed
		}
	}

	// the band at the top: the syllable sung and the speech sound made now, and the voice shown
	void drawReadout(NVGcontext* vg, const PhonaView::Frame* fr, bool live, bool linked) {
		float y = mm2px(4.2f);
		float big = mm2px(4.3f);
		if (!module) {
			text(vg, box.size.x / 2.f, y, "PHONA VIEW", mm2px(3.2f), V_BLUE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		if (!linked) {
			text(vg, box.size.x / 2.f, y, "PLACE NEXT TO PHONA", mm2px(2.7f), V_BLUE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		if (!live) {
			text(vg, box.size.x / 2.f, y, "WAITING FOR PHONA", mm2px(2.7f), V_BLUE_MID, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		std::string v = string::f("V%d", fr->voice + 1);
		text(vg, mm2px(2.f), y, v.c_str(), mm2px(2.6f), fr->voice < fr->sounding ? V_BLUE : V_BLUE_DIM, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		if (fr->beat) {
			text(vg, box.size.x / 2.f, y, "BEATBOX: NOT SHOWN", mm2px(2.5f), V_BLUE_MID, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			return;
		}
		if (fr->lyrics && fr->syllable[0]) {
			std::string syl = fr->syllable;
			for (char& ch : syl) ch = std::toupper((unsigned char) ch);
			text(vg, box.size.x / 2.f, y, syl.c_str(), big, V_AMBER, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		}
		if (fr->vis.phoneme[0]) {
			char ph[40];
			std::snprintf(ph, sizeof(ph), "/%s/", fr->vis.phoneme);
			text(vg, box.size.x - mm2px(2.f), y, ph, mm2px(3.6f), V_WHITE, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, true);
		}
	}

	void draw(const DrawArgs& args) override {
		// module browser: there are no lights layers, so draw the resting head here
		if (!module) drawContent(args);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		// the display glows in a dark room
		if (layer == 1 && module) drawContent(args);
		TransparentWidget::drawLayer(args, layer);
	}
};

struct PhonaViewWidget : ModuleWidget {
	// Panel geometry (mm), the same numbers as in tools/panel_view.py.
	static constexpr float W = 71.12f, DISP_X0 = 1.6f, DISP_Y0 = 9.4f, DISP_Y1 = 114.4f;
	PhonaViewWidget(PhonaView* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PhonaView.svg")));
		PanelLabel* title = new PanelLabel(mm2px(Vec(W / 2.f, 4.1f)), "PHONA VIEW", 12.f);
		title->color = nvgRGB(0xf3, 0xeb, 0xdc);
		title->spacing = 2.5f;
		addChild(title);
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(W - 4.6f, 4.1f)), module, PhonaView::LINK_LIGHT));

		ViewDisplay* d = new ViewDisplay;
		d->module = module;
		d->box.pos = mm2px(Vec(DISP_X0 + 0.6f, DISP_Y0 + 0.6f));
		d->box.size = mm2px(Vec(W - 2 * DISP_X0 - 1.2f, DISP_Y1 - DISP_Y0 - 1.2f));
		addChild(d);

		addChild(new PanelLabel(mm2px(Vec(9.f, 121.4f)), "VOICE", 7.f));
		VoiceSelector* sel = createParam<VoiceSelector>(mm2px(Vec(17.5f, 118.2f)), module, PhonaView::VOICE_PARAM);
		sel->box.size = mm2px(Vec(W - 17.5f - 3.f, 6.4f));
		addParam(sel);
	}
};

Model* modelPhonaView = createModel<PhonaView, PhonaViewWidget>("PhonaView");
