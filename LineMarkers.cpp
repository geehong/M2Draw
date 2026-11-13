#include "pch.h"
#include "LineMarkers.h"
#include "PathTrim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ge {
namespace Markers {

namespace {

const float kEps = 1e-4f;
const float kArrowTrim = 0.85f;   // 채워진 화살촉 길이에 대한 선 끝 단축 비율

struct KindInfo { const char* id; const wchar_t* label; };
const KindInfo kInfo[kKindCount] = {
	{ "none",      L"None" },
	{ "arrow",     L"Arrow" },
	{ "open",      L"Open Arrow" },
	{ "dot",       L"Dot" },
	{ "square",    L"Square" },
	{ "diamond",   L"Diamond" },
	{ "bar",       L"Bar" },
};

Vec2 Sub(Vec2 a, Vec2 b) { return Vec2{ a.x - b.x, a.y - b.y }; }
Vec2 Lerp(Vec2 a, Vec2 b, float t) { return Vec2{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
float Len(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

bool Norm(Vec2 v, Vec2& out)
{
	const float l = Len(v);
	if (l < kEps) return false;
	out = Vec2{ v.x / l, v.y / l };
	return true;
}

// 세그먼트 시작/끝의 진행 방향 (제어점이 끝점과 겹치면 다음 점으로)
Vec2 StartTangent(const Trim::Cubic& c)
{
	Vec2 d;
	if (Norm(Sub(c.p1, c.p0), d) || Norm(Sub(c.p2, c.p0), d) || Norm(Sub(c.p3, c.p0), d)) return d;
	return Vec2{ 1.0f, 0.0f };
}

Vec2 EndTangent(const Trim::Cubic& c)
{
	Vec2 d;
	if (Norm(Sub(c.p3, c.p2), d) || Norm(Sub(c.p3, c.p1), d) || Norm(Sub(c.p3, c.p0), d)) return d;
	return Vec2{ 1.0f, 0.0f };
}

Vec2 Eval(const Trim::Cubic& c, float t)
{
	const float u = 1.0f - t;
	const float a = u * u * u, b = 3.0f * u * u * t, cc = 3.0f * u * t * t, d = t * t * t;
	return Vec2{ a * c.p0.x + b * c.p1.x + cc * c.p2.x + d * c.p3.x, a * c.p0.y + b * c.p1.y + cc * c.p2.y + d * c.p3.y };
}

void Split(const Trim::Cubic& c, float t, Trim::Cubic& l, Trim::Cubic& r)
{
	const Vec2 p01 = Lerp(c.p0, c.p1, t), p12 = Lerp(c.p1, c.p2, t), p23 = Lerp(c.p2, c.p3, t);
	const Vec2 p012 = Lerp(p01, p12, t), p123 = Lerp(p12, p23, t);
	const Vec2 m = Lerp(p012, p123, t);
	l = Trim::Cubic{ c.p0, p01, p012, m };
	r = Trim::Cubic{ m, p123, p23, c.p3 };
}

// 세그먼트의 끝(fromEnd)이나 시작에서 직선거리 length 만큼 잘라낸다. 세그먼트가 너무 짧으면 그 90% 까지만 자른다
bool TrimSegment(Trim::Cubic& c, float length, bool fromEnd)
{
	const float chord = Len(Sub(c.p3, c.p0));
	if (chord < kEps || length <= kEps) return false;
	length = (std::min)(length, chord * 0.9f);
	float lo = 0.0f, hi = 1.0f;
	for (int i = 0; i < 40; ++i) {
		const float mid = (lo + hi) * 0.5f;
		const float dist = fromEnd ? Len(Sub(c.p3, Eval(c, mid))) : Len(Sub(Eval(c, mid), c.p0));
		if (fromEnd) { if (dist > length) lo = mid; else hi = mid; }
		else         { if (dist < length) lo = mid; else hi = mid; }
	}
	const float t = (lo + hi) * 0.5f;
	Trim::Cubic left, right;
	Split(c, t, left, right);
	c = fromEnd ? left : right;
	return true;
}

Vec2 Rotate(Vec2 local, Vec2 dir, Vec2 pos)
{
	return Vec2{ pos.x + dir.x * local.x - dir.y * local.y, pos.y + dir.y * local.x + dir.x * local.y };
}

} // namespace

const char* KindId(MarkerKind k) { return kInfo[static_cast<int>(k)].id; }
const wchar_t* KindLabel(MarkerKind k) { return kInfo[static_cast<int>(k)].label; }

bool KindFromId(const std::string& id, MarkerKind& out)
{
	for (int i = 0; i < kKindCount; ++i) if (id == kInfo[i].id) { out = static_cast<MarkerKind>(i); return true; }
	return false;
}

float MarkerSize(const Stroke& s) { return (std::max)(s.width, 0.5f) * (std::max)(s.markerScale, 0.1f); }

bool HasOpenSubPath(const std::vector<SubPath>& paths)
{
	for (const auto& sp : paths) if (!sp.closed && sp.nodes.size() >= 2) return true;
	return false;
}

std::vector<Placement> Compute(const std::vector<SubPath>& paths, const Stroke& s)
{
	std::vector<Placement> out;
	if (!s.HasMarkers()) return out;
	const float size = MarkerSize(s);
	for (const auto& sp : paths) {
		const std::vector<Trim::Cubic> segs = Trim::ToCubics(sp);
		if (segs.empty()) continue;
		if (!sp.closed) {
			if (s.markerStart != MarkerKind::None) {
				const Vec2 d = StartTangent(segs.front());
				out.push_back(Placement{ s.markerStart, Placement::Start, segs.front().p0, Vec2{ -d.x, -d.y }, size });
			}
			if (s.markerEnd != MarkerKind::None) {
				out.push_back(Placement{ s.markerEnd, Placement::End, segs.back().p3, EndTangent(segs.back()), size });
			}
		}
		if (s.markerMid != MarkerKind::None) {
			const size_t n = segs.size();
			for (size_t i = sp.closed ? 0 : 1; i < n; ++i) {
				const Vec2 din = EndTangent(segs[(i + n - 1) % n]), dout = StartTangent(segs[i]);
				Vec2 d;
				if (!Norm(Vec2{ din.x + dout.x, din.y + dout.y }, d)) d = dout;
				out.push_back(Placement{ s.markerMid, Placement::Mid, segs[i].p0, d, size });
			}
		}
	}
	return out;
}

std::vector<SubPath> TrimEnds(const std::vector<SubPath>& paths, const Stroke& s)
{
	std::vector<SubPath> out = paths;
	const float trim = MarkerSize(s) * kArrowTrim;
	for (auto& sp : out) {
		if (sp.closed) continue;
		std::vector<Trim::Cubic> segs = Trim::ToCubics(sp);
		if (segs.empty()) continue;
		bool changed = false;
		if (s.markerEnd == MarkerKind::Arrow) changed = TrimSegment(segs.back(), trim, true) || changed;
		if (s.markerStart == MarkerKind::Arrow) changed = TrimSegment(segs.front(), trim, false) || changed;
		if (changed) sp = Trim::FromCubics(segs, false);
	}
	return out;
}

Shape MakeShape(const Placement& p)
{
	Shape sh;
	const float S = p.size;
	auto add = [&](float lx, float ly) { sh.pts.push_back(Rotate(Vec2{ lx, ly }, p.dir, p.pos)); };
	switch (p.kind) {
	case MarkerKind::Arrow:      // 채워진 삼각형: 끝점에 꼭짓점
		add(0.0f, 0.0f); add(-S, -S * 0.5f); add(-S, S * 0.5f);
		break;
	case MarkerKind::OpenArrow:  // 선으로 그린 V 자: 끝점에 꼭짓점
		sh.closed = false; sh.filled = false;
		add(-S * 0.8f, -S * 0.45f); add(0.0f, 0.0f); add(-S * 0.8f, S * 0.45f);
		break;
	case MarkerKind::Dot:
		sh.circle = true; sh.radius = S * 0.5f;
		add(0.0f, 0.0f);
		break;
	case MarkerKind::Square:
		add(-S * 0.5f, -S * 0.5f); add(S * 0.5f, -S * 0.5f); add(S * 0.5f, S * 0.5f); add(-S * 0.5f, S * 0.5f);
		break;
	case MarkerKind::Diamond:
		add(S * 0.6f, 0.0f); add(0.0f, S * 0.6f); add(-S * 0.6f, 0.0f); add(0.0f, -S * 0.6f);
		break;
	case MarkerKind::Bar:        // 선에 수직인 막대
		add(-S * 0.1f, -S * 0.6f); add(S * 0.1f, -S * 0.6f); add(S * 0.1f, S * 0.6f); add(-S * 0.1f, S * 0.6f);
		break;
	default:
		break;
	}
	return sh;
}

namespace {
std::string Num(float v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(v));
	return buf;
}
} // namespace

std::string SvgMarkerId(MarkerKind k, const std::string& colorHex, float scale, bool trimmed)
{
	std::string hex = colorHex;
	if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
	return std::string("m2m-") + KindId(k) + "-" + hex + "-" + std::to_string(static_cast<int>(scale * 10.0f + 0.5f)) + ((trimmed && k == MarkerKind::Arrow) ? "-t" : "");
}

std::string SvgMarkerDef(MarkerKind k, const std::string& colorHex, float scale, bool trimmed)
{
	if (k == MarkerKind::None) return std::string();
	scale = (std::max)(scale, 0.1f);
	// 도형은 10x10 (마름모·막대는 -1..11) 좌표 안에 그리고, 마커 상자 크기로 실제 크기를 정한다
	const bool wide = (k == MarkerKind::Diamond || k == MarkerKind::Bar);
	const float box = wide ? 12.0f : 10.0f;
	const float mw = scale * box / 10.0f;
	const std::string fill = colorHex;
	std::string body;
	switch (k) {
	case MarkerKind::Arrow:     body = "<path d=\"M0,0 L10,5 L0,10 z\" fill=\"" + fill + "\" />"; break;
	case MarkerKind::OpenArrow: body = "<path d=\"M2,0.5 L10,5 L2,9.5\" fill=\"none\" stroke=\"" + fill + "\" stroke-width=\"" + Num(10.0f / scale) + "\" />"; break;
	case MarkerKind::Dot:       body = "<circle cx=\"5\" cy=\"5\" r=\"5\" fill=\"" + fill + "\" />"; break;
	case MarkerKind::Square:    body = "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"" + fill + "\" />"; break;
	case MarkerKind::Diamond:   body = "<polygon points=\"5,-1 11,5 5,11 -1,5\" fill=\"" + fill + "\" />"; break;
	case MarkerKind::Bar:       body = "<rect x=\"4\" y=\"-1\" width=\"2\" height=\"12\" fill=\"" + fill + "\" />"; break;
	default: break;
	}
	std::string refX = (k == MarkerKind::Arrow || k == MarkerKind::OpenArrow) ? "10" : "5";
	if (trimmed && k == MarkerKind::Arrow) refX = Num(10.0f - 10.0f * kArrowTrim);   // 선 끝을 짧게 썼으므로 꼭짓점이 단축한 길이만큼 앞에 오도록
	return "<marker id=\"" + SvgMarkerId(k, colorHex, scale, trimmed) + "\" viewBox=\"" + (wide ? "-1 -1 12 12" : "0 0 10 10") +
		"\" refX=\"" + refX + "\" refY=\"5\" markerUnits=\"strokeWidth\" markerWidth=\"" + Num(mw) +
		"\" markerHeight=\"" + Num(mw) + "\" orient=\"auto-start-reverse\" overflow=\"visible\">" + body + "</marker>";
}
} // namespace Markers
} // namespace ge
