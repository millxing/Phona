// Records NanoVG calls into an SVG file: a look at a display widget without opening Rack.
#pragma once
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------- NanoVG → SVG recorder
// These definitions replace libRack's for the calls this program makes.
namespace rec {
struct State { NVGcolor stroke = nvgRGB(255, 255, 255), fill = nvgRGB(255, 255, 255); float width = 1.f; bool lighter = false; float fontSize = 10.f; int align = 0; };
std::vector<State> stack(1);
std::string path;
std::ostringstream svg;
float lastX = 0, lastY = 0;
std::string rgba(NVGcolor c) {
	char b[96];
	std::snprintf(b, sizeof(b), "rgba(%d,%d,%d,%.3f)", (int) (c.r * 255), (int) (c.g * 255), (int) (c.b * 255), c.a);
	return b;
}
std::string blend() { return stack.back().lighter ? " style=\"mix-blend-mode:plus-lighter\"" : ""; }
}
extern "C" {
void nvgSave(NVGcontext*) { rec::stack.push_back(rec::stack.back()); }
void nvgRestore(NVGcontext*) { if (rec::stack.size() > 1) rec::stack.pop_back(); }
void nvgScissor(NVGcontext*, float, float, float, float) {}
void nvgBeginPath(NVGcontext*) { rec::path.clear(); }
void nvgMoveTo(NVGcontext*, float x, float y) { char b[64]; std::snprintf(b, sizeof(b), "M%.2f %.2f ", x, y); rec::path += b; }
void nvgLineTo(NVGcontext*, float x, float y) { char b[64]; std::snprintf(b, sizeof(b), "L%.2f %.2f ", x, y); rec::path += b; }
void nvgClosePath(NVGcontext*) { rec::path += "Z "; }
void nvgLineJoin(NVGcontext*, int) {}
void nvgLineCap(NVGcontext*, int) {}
void nvgGlobalCompositeOperation(NVGcontext*, int op) { rec::stack.back().lighter = op == NVG_LIGHTER; }
void nvgStrokeColor(NVGcontext*, NVGcolor c) { rec::stack.back().stroke = c; }
void nvgStrokeWidth(NVGcontext*, float w) { rec::stack.back().width = w; }
void nvgFillColor(NVGcontext*, NVGcolor c) { rec::stack.back().fill = c; }
NVGpaint nvgRadialGradient(NVGcontext*, float, float, float, float, NVGcolor icol, NVGcolor) { NVGpaint p; std::memset(&p, 0, sizeof(p)); p.innerColor = icol; return p; }
void nvgFillPaint(NVGcontext*, NVGpaint p) { rec::stack.back().fill = p.innerColor; }
void nvgCircle(NVGcontext*, float cx, float cy, float r) {
	char b[160];
	std::snprintf(b, sizeof(b), "M%.2f %.2f a%.2f %.2f 0 1 0 %.2f 0 a%.2f %.2f 0 1 0 %.2f 0 Z ", cx - r, cy, r, r, 2 * r, r, r, -2 * r);
	rec::path += b;
}
void nvgRect(NVGcontext* vg, float x, float y, float w, float h) {
	nvgMoveTo(vg, x, y); nvgLineTo(vg, x + w, y); nvgLineTo(vg, x + w, y + h); nvgLineTo(vg, x, y + h); nvgClosePath(vg);
}
void nvgRoundedRect(NVGcontext* vg, float x, float y, float w, float h, float) {
	nvgMoveTo(vg, x, y); nvgLineTo(vg, x + w, y); nvgLineTo(vg, x + w, y + h); nvgLineTo(vg, x, y + h); nvgClosePath(vg);
}
void nvgStroke(NVGcontext*) {
	auto& s = rec::stack.back();
	rec::svg << "<path d=\"" << rec::path << "\" fill=\"none\" stroke=\"" << rec::rgba(s.stroke) << "\" stroke-width=\"" << s.width
		<< "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"" << rec::blend() << "/>\n";
}
void nvgFill(NVGcontext*) {
	auto& s = rec::stack.back();
	rec::svg << "<path d=\"" << rec::path << "\" fill=\"" << rec::rgba(s.fill) << "\"" << rec::blend() << "/>\n";
}
void nvgFontFaceId(NVGcontext*, int) {}
void nvgFontSize(NVGcontext*, float size) { rec::stack.back().fontSize = size; }
void nvgTextLetterSpacing(NVGcontext*, float) {}
void nvgTextAlign(NVGcontext*, int align) { rec::stack.back().align = align; }
float nvgText(NVGcontext*, float x, float y, const char* str, const char*) {
	auto& s = rec::stack.back();
	const char* anchor = s.align & NVG_ALIGN_RIGHT ? "end" : s.align & NVG_ALIGN_CENTER ? "middle" : "start";
	const char* base = s.align & NVG_ALIGN_MIDDLE ? "middle" : s.align & NVG_ALIGN_TOP ? "hanging" : "auto";
	rec::svg << "<text x=\"" << x << "\" y=\"" << y << "\" font-family=\"Menlo, DejaVu Sans\" font-size=\"" << s.fontSize << "\" fill=\"" << rec::rgba(s.fill)
		<< "\" text-anchor=\"" << anchor << "\" dominant-baseline=\"" << base << "\">" << str << "</text>\n";
	return x;
}
}

template <class D>
static void writeSvg(D& d, const std::string& file) {
	rec::svg.str("");
	rec::stack.assign(1, rec::State());
	Widget::DrawArgs args;
	args.vg = (NVGcontext*) 1;
	d.drawContent(args);
	std::ofstream o(file);
	// square (Quick Look thumbnails crop to a square), the display centred
	float side = std::max(d.box.size.x, d.box.size.y), x0 = -(side - d.box.size.x) / 2.f;
	o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << side * 3 << "\" height=\"" << side * 3 << "\" viewBox=\"" << x0 << " 0 "
	  << side << " " << side << "\"><rect x=\"" << x0 << "\" width=\"" << side << "\" height=\"" << side << "\" fill=\"#222\"/><rect width=\"" << d.box.size.x << "\" height=\"" << d.box.size.y
	  << "\" fill=\"black\"/>\n" << rec::svg.str() << "</svg>\n";
}

