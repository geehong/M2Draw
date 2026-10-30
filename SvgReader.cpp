#include "pch.h"
#include "SvgReader.h"
#include "HtmlComponents/UiComponents.h"
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"

#include <xmllite.h>
#include <shlwapi.h>
#include <atlbase.h>

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <cwctype>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "shlwapi.lib")

namespace ge {

namespace {

// 공백 및 쉼표 건너뛰기
void SkipSeparators(const wchar_t*& p, const wchar_t* end) {
	while (p < end && (iswspace(*p) || *p == L',')) {
		++p;
	}
}

static std::string WideToUtf8(const std::wstring& w) {
	if (w.empty()) return "";
	int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(size, 0);
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), size, nullptr, nullptr);
	return s;
}

// 실수 1개 파싱
bool ParseNextFloat(const wchar_t*& p, const wchar_t* end, float& val) {
	SkipSeparators(p, end);
	if (p >= end) return false;

	wchar_t* next = nullptr;
	val = std::wcstof(p, &next);
	if (next == p) return false;
	p = next;
	return true;
}

// Base64 디코드 (data URI 용). 공백/줄바꿈은 무시
bool Base64Decode(const std::string& in, std::vector<uint8_t>& out)
{
	out.clear();
	unsigned v = 0; int bits = 0;
	for (char c : in) {
		int d;
		if (c >= 'A' && c <= 'Z') d = c - 'A';
		else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
		else if (c >= '0' && c <= '9') d = c - '0' + 52;
		else if (c == '+' || c == '-') d = 62;
		else if (c == '/' || c == '_') d = 63;
		else if (c == '=') break;
		else continue;
		v = (v << 6) | d; bits += 6;
		if (bits >= 8) { bits -= 8; out.push_back(static_cast<uint8_t>((v >> bits) & 0xFF)); }
	}
	return !out.empty();
}

// "a,b,c" 형태의 실수 목록 (테이블 열 너비/행 높이 등)
static FrameSizingMode ParseSizingMode(const std::wstring& v)
{
	if (v == L"hug") return FrameSizingMode::Hug;
	if (v == L"fill") return FrameSizingMode::Fill;
	return FrameSizingMode::Fixed;
}

std::vector<float> ParseFloatList(const std::wstring& text)
{
	std::vector<float> out;
	const wchar_t* p = text.c_str();
	const wchar_t* e = p + text.size();
	float v = 0.0f;
	while (ParseNextFloat(p, e, v)) out.push_back(v);
	return out;
}

// 색상 이름 (SVG/CSS 자주 쓰는 것)
bool NamedColor(const std::wstring& n, unsigned& rgb) {
	static const std::unordered_map<std::wstring, unsigned> k = {
		{L"black",0x000000},{L"white",0xFFFFFF},{L"red",0xFF0000},{L"green",0x008000},{L"blue",0x0000FF},{L"yellow",0xFFFF00},
		{L"cyan",0x00FFFF},{L"aqua",0x00FFFF},{L"magenta",0xFF00FF},{L"fuchsia",0xFF00FF},{L"gray",0x808080},{L"grey",0x808080},
		{L"silver",0xC0C0C0},{L"maroon",0x800000},{L"olive",0x808000},{L"lime",0x00FF00},{L"teal",0x008080},{L"navy",0x000080},
		{L"purple",0x800080},{L"orange",0xFFA500},{L"pink",0xFFC0CB},{L"brown",0xA52A2A},{L"gold",0xFFD700},{L"indigo",0x4B0082},
		{L"violet",0xEE82EE},{L"crimson",0xDC143C},{L"coral",0xFF7F50},{L"salmon",0xFA8072},{L"khaki",0xF0E68C},{L"tan",0xD2B48C},
		{L"skyblue",0x87CEEB},{L"lightgray",0xD3D3D3},{L"lightgrey",0xD3D3D3},{L"darkgray",0xA9A9A9},{L"darkgrey",0xA9A9A9},
		{L"darkgreen",0x006400},{L"darkblue",0x00008B},{L"darkred",0x8B0000},{L"lightblue",0xADD8E6},{L"lightgreen",0x90EE90},
		{L"orangered",0xFF4500},{L"dodgerblue",0x1E90FF},{L"steelblue",0x4682B4},{L"slategray",0x708090},{L"transparent",0xFFFFFF} };
	std::wstring l = n;
	for (auto& c : l) c = static_cast<wchar_t>(towlower(c));
	auto it = k.find(l);
	if (it == k.end()) return false;
	rgb = it->second;
	return true;
}

// 색상: #rgb, #rrggbb, #rrggbbaa, rgb()/rgba() (정수 또는 %), 색상 이름. (currentColor 는 호출 전에 치환)
Color ParseColorValue(const std::wstring& in) {
	std::wstring str = in;
	while (!str.empty() && iswspace(str.front())) str.erase(str.begin());
	while (!str.empty() && iswspace(str.back())) str.pop_back();
	if (str.empty()) return Color(0.0f, 0.0f, 0.0f, 1.0f);

	if (str[0] == L'#') {
		std::wstring h = str.substr(1);
		if (h.length() == 3 || h.length() == 4) {
			std::wstring e;
			for (wchar_t c : h) { e.push_back(c); e.push_back(c); }
			h = e;
		}
		if (h.length() == 6 || h.length() == 8) {
			unsigned v = static_cast<unsigned>(std::wcstoul(h.c_str(), nullptr, 16));
			if (h.length() == 6)
				return Color(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, 1.0f);
			return Color(((v >> 24) & 0xFF) / 255.0f, ((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f);
		}
		return Color(0.0f, 0.0f, 0.0f, 1.0f);
	}
	if (str.compare(0, 3, L"rgb") == 0) {
		const size_t o = str.find(L'(');
		const size_t c = str.find(L')');
		if (o != std::wstring::npos && c != std::wstring::npos && c > o) {
			std::wstring body = str.substr(o + 1, c - o - 1);
			float v[4] = { 0, 0, 0, 1 };
			const wchar_t* p = body.c_str();
			const wchar_t* e = p + body.length();
			for (int i = 0; i < 4; ++i) {
				SkipSeparators(p, e);
				if (p >= e) break;
				wchar_t* nx = nullptr;
				float x = std::wcstof(p, &nx);
				if (nx == p) break;
				p = nx;
				const bool pct = (p < e && *p == L'%');
				if (pct) ++p;
				if (i < 3) v[i] = pct ? x / 100.0f : x / 255.0f;
				else v[3] = pct ? x / 100.0f : x;
			}
			return Color((std::max)(0.0f, (std::min)(1.0f, v[0])), (std::max)(0.0f, (std::min)(1.0f, v[1])),
				(std::max)(0.0f, (std::min)(1.0f, v[2])), (std::max)(0.0f, (std::min)(1.0f, v[3])));
		}
	}
	unsigned rgb = 0;
	if (NamedColor(str, rgb))
		return Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
	return Color(0.0f, 0.0f, 0.0f, 1.0f);
}

Color ParseColorHex(const std::wstring& hexStr) { return ParseColorValue(hexStr); }

// 길이: 숫자 + 단위(px/pt/pc/mm/cm/in/em). % 는 호출 측이 기준값을 넘김
float ParseLength(const std::wstring& text, float def, float percentBase = 0.0f) {
	const wchar_t* p = text.c_str();
	wchar_t* nx = nullptr;
	const float v = std::wcstof(p, &nx);
	if (nx == p) return def;
	std::wstring unit(nx);
	while (!unit.empty() && iswspace(unit.back())) unit.pop_back();
	if (unit == L"pt") return v * 96.0f / 72.0f;
	if (unit == L"pc") return v * 16.0f;
	if (unit == L"mm") return v * 96.0f / 25.4f;
	if (unit == L"cm") return v * 96.0f / 2.54f;
	if (unit == L"in") return v * 96.0f;
	if (unit == L"em") return v * 16.0f;
	if (unit == L"%") return percentBase * v / 100.0f;
	return v;
}

// transform 리스트: translate / scale / rotate / skewX / skewY / matrix 를 나열 순서대로 합성
// (SVG 의 "A B" = 좌표에 B 를 먼저 적용한 뒤 A. 행벡터 행렬이므로 M = B * A 로 누적)
Matrix2D ParseTransformMatrix(const std::wstring& xfStr) {
	Matrix2D result = Matrix2D::Identity();
	size_t pos = 0;
	while (pos < xfStr.size()) {
		const size_t open = xfStr.find(L'(', pos);
		if (open == std::wstring::npos) break;
		const size_t close = xfStr.find(L')', open);
		if (close == std::wstring::npos) break;
		std::wstring name = xfStr.substr(pos, open - pos);
		while (!name.empty() && (iswspace(name.front()) || name.front() == L',')) name.erase(name.begin());
		while (!name.empty() && iswspace(name.back())) name.pop_back();

		const std::wstring args = xfStr.substr(open + 1, close - open - 1);
		const wchar_t* p = args.c_str();
		const wchar_t* e = p + args.length();
		float v[6] = { 0, 0, 0, 0, 0, 0 };
		int n = 0;
		while (n < 6 && ParseNextFloat(p, e, v[n])) ++n;

		Matrix2D m = Matrix2D::Identity();
		if (name == L"matrix" && n >= 6) m = Matrix2D(v[0], v[1], v[2], v[3], v[4], v[5]);
		else if (name == L"translate") m = Matrix2D::Translation(v[0], n >= 2 ? v[1] : 0.0f);
		else if (name == L"scale") m = Matrix2D::Scale(v[0], n >= 2 ? v[1] : v[0]);
		else if (name == L"rotate") {
			const float a = v[0] * 3.14159265f / 180.0f;
			const float c = std::cos(a), s = std::sin(a);
			Matrix2D r(c, s, -s, c, 0.0f, 0.0f);
			if (n >= 3) m = Matrix2D::Translation(-v[1], -v[2]) * r * Matrix2D::Translation(v[1], v[2]);
			else m = r;
		}
		else if (name == L"skewX") m = Matrix2D(1.0f, 0.0f, std::tan(v[0] * 3.14159265f / 180.0f), 1.0f, 0.0f, 0.0f);
		else if (name == L"skewY") m = Matrix2D(1.0f, std::tan(v[0] * 3.14159265f / 180.0f), 0.0f, 1.0f, 0.0f, 0.0f);

		result = m * result;
		pos = close + 1;
	}
	return result;
}

// 타원 호(SVG A 명령)를 3차 베지에 조각들로 변환. emitCubic(c1, c2, end)
template <typename EmitCubic, typename EmitLine>
void ArcToCubics(Vec2 p0, float rx, float ry, float phiDeg, bool fa, bool fs, Vec2 p1, EmitCubic emitCubic, EmitLine emitLine) {
	if (p0.x == p1.x && p0.y == p1.y) return;
	rx = std::fabs(rx); ry = std::fabs(ry);
	if (rx < 1e-6f || ry < 1e-6f) { emitLine(p1); return; }

	const double phi = phiDeg * 3.14159265358979 / 180.0;
	const double c = std::cos(phi), s = std::sin(phi);
	const double dx2 = (p0.x - p1.x) / 2.0, dy2 = (p0.y - p1.y) / 2.0;
	const double x1p = c * dx2 + s * dy2, y1p = -s * dx2 + c * dy2;
	double RX = rx, RY = ry;
	const double lambda = (x1p * x1p) / (RX * RX) + (y1p * y1p) / (RY * RY);
	if (lambda > 1.0) { const double sl = std::sqrt(lambda); RX *= sl; RY *= sl; }

	const double num = RX * RX * RY * RY - RX * RX * y1p * y1p - RY * RY * x1p * x1p;
	const double den = RX * RX * y1p * y1p + RY * RY * x1p * x1p;
	double coef = den > 0.0 ? std::sqrt((std::max)(0.0, num / den)) : 0.0;
	if (fa == fs) coef = -coef;
	const double cxp = coef * (RX * y1p / RY), cyp = coef * (-RY * x1p / RX);
	const double cx = c * cxp - s * cyp + (p0.x + p1.x) / 2.0;
	const double cy = s * cxp + c * cyp + (p0.y + p1.y) / 2.0;

	const double th1 = std::atan2((y1p - cyp) / RY, (x1p - cxp) / RX);
	const double th2 = std::atan2((-y1p - cyp) / RY, (-x1p - cxp) / RX);
	double dth = th2 - th1;
	const double kTwoPi = 6.283185307179586;
	if (fs && dth < 0.0) dth += kTwoPi;
	else if (!fs && dth > 0.0) dth -= kTwoPi;

	const int segs = (std::max)(1, static_cast<int>(std::ceil(std::fabs(dth) / (3.14159265358979 / 2.0) - 1e-9)));
	const double delta = dth / segs;
	const double tt = 4.0 / 3.0 * std::tan(delta / 4.0);
	auto map = [&](double ux, double uy) {
		return Vec2{ static_cast<float>(cx + c * RX * ux - s * RY * uy), static_cast<float>(cy + s * RX * ux + c * RY * uy) };
	};
	for (int i = 0; i < segs; ++i) {
		const double a1 = th1 + i * delta, a2 = a1 + delta;
		const Vec2 q1 = map(std::cos(a1) - tt * std::sin(a1), std::sin(a1) + tt * std::cos(a1));
		const Vec2 q2 = map(std::cos(a2) + tt * std::sin(a2), std::sin(a2) - tt * std::cos(a2));
		const Vec2 e = (i == segs - 1) ? p1 : map(std::cos(a2), std::sin(a2));
		emitCubic(q1, q2, e);
	}
}

// 패스 d 속성 파싱: M L H V C S Q T A Z (대/소문자=절대/상대) 전체 지원
bool ParsePathD(const std::wstring& dStr, std::vector<SubPath>& outPaths) {
	outPaths.clear();
	const wchar_t* p = dStr.c_str();
	const wchar_t* end = p + dStr.length();

	SubPath cur;
	Vec2 cp{ 0, 0 }, start{ 0, 0 }, lastC2{ 0, 0 }, lastQ{ 0, 0 };
	wchar_t cmd = 0, prevUpper = 0;

	auto flush = [&]() {
		if (!cur.nodes.empty()) { outPaths.push_back(std::move(cur)); cur = SubPath(); }
	};
	auto ensureStart = [&]() {
		if (cur.nodes.empty()) { BezierNode n; n.anchor = cp; cur.nodes.push_back(n); }
	};
	auto lineTo = [&](Vec2 q) {
		ensureStart();
		BezierNode n; n.anchor = q;
		cur.nodes.push_back(n);
		cp = q;
	};
	auto cubicTo = [&](Vec2 c1, Vec2 c2, Vec2 q) {
		ensureStart();
		BezierNode& prev = cur.nodes.back();
		prev.hOut = { c1.x - prev.anchor.x, c1.y - prev.anchor.y };
		BezierNode n; n.anchor = q; n.hIn = { c2.x - q.x, c2.y - q.y };
		cur.nodes.push_back(n);
		cp = q;
		lastC2 = c2;
	};

	while (p < end) {
		SkipSeparators(p, end);
		if (p >= end) break;
		if (iswalpha(*p)) { cmd = *p++; }
		else if (cmd == 0) { ++p; continue; }

		const bool rel = (cmd >= L'a' && cmd <= L'z');
		const wchar_t U = static_cast<wchar_t>(rel ? cmd - 32 : cmd);
		float v[7];
		auto num = [&](int n) { for (int i = 0; i < n; ++i) if (!ParseNextFloat(p, end, v[i])) return false; return true; };
		auto flag = [&](float& out) {   // 호 플래그는 "0"/"1" 한 글자가 숫자 뒤에 붙어 올 수 있음
			SkipSeparators(p, end);
			if (p >= end || (*p != L'0' && *p != L'1')) return false;
			out = (*p == L'1') ? 1.0f : 0.0f; ++p; return true;
		};
		const Vec2 o = rel ? cp : Vec2{ 0, 0 };

		if (U == L'M') {
			if (!num(2)) break;
			flush();
			cp = { o.x + v[0], o.y + v[1] };
			start = cp;
			ensureStart();
			cmd = rel ? L'l' : L'L';   // 이어지는 좌표쌍은 L
		}
		else if (U == L'L') { if (!num(2)) break; lineTo({ o.x + v[0], o.y + v[1] }); }
		else if (U == L'H') { if (!num(1)) break; lineTo({ (rel ? cp.x : 0.0f) + v[0], cp.y }); }
		else if (U == L'V') { if (!num(1)) break; lineTo({ cp.x, (rel ? cp.y : 0.0f) + v[0] }); }
		else if (U == L'C') {
			if (!num(6)) break;
			cubicTo({ o.x + v[0], o.y + v[1] }, { o.x + v[2], o.y + v[3] }, { o.x + v[4], o.y + v[5] });
		}
		else if (U == L'S') {
			if (!num(4)) break;
			const bool prevCubic = (prevUpper == L'C' || prevUpper == L'S');
			const Vec2 c1 = prevCubic ? Vec2{ 2 * cp.x - lastC2.x, 2 * cp.y - lastC2.y } : cp;
			cubicTo(c1, { o.x + v[0], o.y + v[1] }, { o.x + v[2], o.y + v[3] });
		}
		else if (U == L'Q' || U == L'T') {
			Vec2 q1, q;
			if (U == L'Q') { if (!num(4)) break; q1 = { o.x + v[0], o.y + v[1] }; q = { o.x + v[2], o.y + v[3] }; }
			else {
				if (!num(2)) break;
				const bool prevQuad = (prevUpper == L'Q' || prevUpper == L'T');
				q1 = prevQuad ? Vec2{ 2 * cp.x - lastQ.x, 2 * cp.y - lastQ.y } : cp;
				q = { o.x + v[0], o.y + v[1] };
			}
			const Vec2 p0 = cp;
			cubicTo({ p0.x + 2.0f / 3.0f * (q1.x - p0.x), p0.y + 2.0f / 3.0f * (q1.y - p0.y) },
			        { q.x + 2.0f / 3.0f * (q1.x - q.x), q.y + 2.0f / 3.0f * (q1.y - q.y) }, q);
			lastQ = q1;
		}
		else if (U == L'A') {
			float rx = 0, ry = 0, rot = 0, fa = 0, fs = 0, x = 0, y = 0;
			if (!ParseNextFloat(p, end, rx) || !ParseNextFloat(p, end, ry) || !ParseNextFloat(p, end, rot) ||
			    !flag(fa) || !flag(fs) || !ParseNextFloat(p, end, x) || !ParseNextFloat(p, end, y)) break;
			ensureStart();
			const Vec2 tgt{ o.x + x, o.y + y };
			ArcToCubics(cp, rx, ry, rot, fa != 0.0f, fs != 0.0f, tgt,
				[&](Vec2 c1, Vec2 c2, Vec2 e) { cubicTo(c1, c2, e); },
				[&](Vec2 e) { lineTo(e); });
			cp = tgt;
		}
		else if (U == L'Z') {
			if (!cur.nodes.empty()) {
				// 마지막 점이 시작점과 같으면 합치고 핸들만 시작점으로 이월
				if (cur.nodes.size() > 1) {
					const Vec2 f = cur.nodes.front().anchor, l = cur.nodes.back().anchor;
					if ((f.x - l.x) * (f.x - l.x) + (f.y - l.y) * (f.y - l.y) < 1e-6f) {
						cur.nodes.front().hIn = cur.nodes.back().hIn;
						cur.nodes.pop_back();
					}
				}
				cur.closed = true;
				outPaths.push_back(std::move(cur));
				cur = SubPath();
			}
			cp = start;
		}
		else { ++p; continue; }
		prevUpper = U;
	}
	flush();
	return !outPaths.empty();
}

// 노드 타입 메타데이터 복원
void ApplyNodeTypes(PathNode& pathNode, const std::wstring& nodeTypesStr) {
	const wchar_t* p = nodeTypesStr.c_str();
	const wchar_t* end = p + nodeTypesStr.length();

	auto& paths = pathNode.GetPathsRef();
	size_t spIdx = 0;
	size_t nIdx = 0;

	while (p < end) {
		SkipSeparators(p, end);
		if (p >= end) break;

		wchar_t t = *p++;
		if (spIdx >= paths.size()) break;

		if (nIdx >= paths[spIdx].nodes.size()) {
			++spIdx;
			nIdx = 0;
			if (spIdx >= paths.size()) break;
		}

		auto& node = paths[spIdx].nodes[nIdx];
		switch (t) {
		case L'c': node.type = BezierNode::Type::Corner; break;
		case L's': node.type = BezierNode::Type::Smooth; break;
		case L'z': node.type = BezierNode::Type::Symmetric; break;
		}
		++nIdx;
	}
}

using AttrMap = std::unordered_map<std::wstring, std::wstring>;

// SvgWriter 가 출력한 id ("layer_12", "group_3", "path_7") 에서 NodeId 복원. 형식이 다르면(외부 SVG) 0
NodeId ParseNodeId(const AttrMap& attrs) {
	auto it = attrs.find(L"id");
	if (it == attrs.end()) return 0;
	const std::wstring& id = it->second;
	static const wchar_t* kPrefixes[] = { L"layer_", L"group_", L"path_" };
	for (const wchar_t* prefix : kPrefixes) {
		const size_t len = wcslen(prefix);
		if (id.compare(0, len, prefix) == 0 && id.size() > len) {
			wchar_t* end = nullptr;
			unsigned long long v = std::wcstoull(id.c_str() + len, &end, 10);
			if (end && *end == L'\0') return static_cast<NodeId>(v);
		}
	}
	return 0;
}

// "0.5" 또는 "50%" -> 비율 (퍼센트는 100 으로 나눔). 비어 있거나 잘못되면 def
float ParseRatio(const std::wstring* v, float def) {
	if (!v || v->empty()) return def;
	wchar_t* end = nullptr;
	float x = std::wcstof(v->c_str(), &end);
	if (end == v->c_str()) return def;
	while (*end && iswspace(*end)) ++end;
	if (*end == L'%') x /= 100.0f;
	return x;
}

// style="a:b; c:d" -> 속성 맵 (공백 제거, 키는 그대로)
std::unordered_map<std::wstring, std::wstring> ParseCssDeclarations(const std::wstring& css) {
	std::unordered_map<std::wstring, std::wstring> out;
	size_t i = 0;
	while (i < css.size()) {
		size_t semi = css.find(L';', i);
		if (semi == std::wstring::npos) semi = css.size();
		const std::wstring decl = css.substr(i, semi - i);
		const size_t colon = decl.find(L':');
		if (colon != std::wstring::npos) {
			auto trim = [](std::wstring t) {
				size_t a = 0, b = t.size();
				while (a < b && iswspace(t[a])) ++a;
				while (b > a && iswspace(t[b - 1])) --b;
				return t.substr(a, b - a);
			};
			out[trim(decl.substr(0, colon))] = trim(decl.substr(colon + 1));
		}
		i = semi + 1;
	}
	return out;
}

// fill="url(#id)" -> "id" (아니면 빈 문자열)
std::wstring ParseUrlRef(const std::wstring& v) {
	if (v.compare(0, 4, L"url(") != 0) return L"";
	size_t a = v.find(L'#');
	size_t b = v.find(L')', a == std::wstring::npos ? 0 : a);
	if (a == std::wstring::npos || b == std::wstring::npos || b <= a + 1) return L"";
	return v.substr(a + 1, b - a - 1);
}

// 보이기/잠금/혼합 모드 복원 (SvgWriter::BuildCommonState 와 대응)
void ApplyCommonState(Node& n, const AttrMap& attrs) {
	auto get = [&](const wchar_t* key) -> const std::wstring* {
		auto it = attrs.find(key);
		return it == attrs.end() ? nullptr : &it->second;
	};
	if (const auto* v = get(L"m2:visible")) n.SetVisible(*v != L"false");
	else if (const auto* s = get(L"style")) { if (s->find(L"display:none") != std::wstring::npos) n.SetVisible(false); }
	if (const auto* l = get(L"m2:locked")) n.SetLocked(*l == L"true");
	if (const auto* b = get(L"m2:blend")) {
		const long m = std::wcstol(b->c_str(), nullptr, 10);
		if (m >= 0 && m <= static_cast<long>(LayerBlendMode::Luminosity))
			n.SetBlendMode(static_cast<LayerBlendMode>(m));
	}
	if (const auto* t = get(L"m2:htmlTag"))     n.SetHtmlTag(WideToUtf8(*t));
	if (const auto* h = get(L"m2:href"))        n.SetHref(WideToUtf8(*h));
	if (const auto* a = get(L"m2:alt"))         n.SetAlt(WideToUtf8(*a));
	if (const auto* l = get(L"m2:ariaLabel"))   n.SetAriaLabel(WideToUtf8(*l));
	if (const auto* c = get(L"m2:customClass")) n.SetCustomClass(WideToUtf8(*c));
	if (const auto* c = get(L"m2:cx")) { const long v = std::wcstol(c->c_str(), nullptr, 10); if (v >= 0 && v <= 3) n.SetConstraintH(static_cast<ConstraintH>(v)); }
	if (const auto* c = get(L"m2:cy")) { const long v = std::wcstol(c->c_str(), nullptr, 10); if (v >= 0 && v <= 3) n.SetConstraintV(static_cast<ConstraintV>(v)); }

	// 그림자: m2:shadow="x,y,blur,spread,#rrggbb,alpha" 우선, 없으면 표준 style="filter:drop-shadow(x y blur rgba(r,g,b,a))"
	if (const auto* sh = get(L"m2:shadow")) {
		std::vector<std::wstring> tok;
		size_t p0 = 0;
		while (p0 <= sh->size()) {
			size_t e = sh->find(L',', p0);
			if (e == std::wstring::npos) e = sh->size();
			tok.push_back(sh->substr(p0, e - p0));
			p0 = e + 1;
		}
		if (tok.size() >= 6) {
			Shadow sd;
			sd.enabled = true;
			sd.x = std::wcstof(tok[0].c_str(), nullptr); sd.y = std::wcstof(tok[1].c_str(), nullptr);
			sd.blur = std::wcstof(tok[2].c_str(), nullptr); sd.spread = std::wcstof(tok[3].c_str(), nullptr);
			sd.color = ParseColorValue(tok[4]);
			sd.color.a = std::wcstof(tok[5].c_str(), nullptr);
			sd.inset = tok.size() >= 7 && tok[6] == L"1";
			n.SetShadow(sd);
		}
	}
	else if (const auto* s = get(L"style")) {
		const size_t k = s->find(L"drop-shadow(");
		if (k != std::wstring::npos) {
			const wchar_t* q = s->c_str() + k + 12;
			wchar_t* nx = nullptr;
			Shadow sd;
			sd.enabled = true;
			sd.x = std::wcstof(q, &nx); q = nx;
			while (*q == L'p' || *q == L'x' || *q == L' ') ++q;
			sd.y = std::wcstof(q, &nx); q = nx;
			while (*q == L'p' || *q == L'x' || *q == L' ') ++q;
			sd.blur = std::wcstof(q, &nx); q = nx;
			while (*q == L'p' || *q == L'x' || *q == L' ') ++q;
			if (wcsncmp(q, L"rgba(", 5) == 0) {
				q += 5;
				float c[3] = { 0, 0, 0 };
				for (int i = 0; i < 3; ++i) { c[i] = std::wcstof(q, &nx) / 255.0f; q = nx; while (*q == L',' || *q == L' ') ++q; }
				const float a = std::wcstof(q, &nx);
				sd.color = Color(c[0], c[1], c[2], a);
			}
			n.SetShadow(sd);
		}
	}

	// 표준 CSS: style="mix-blend-mode:multiply" (외부 SVG / Clean 출력). m2:blend 가 없어도 복원됨
	if (const auto* s = get(L"style")) {
		const size_t key = s->find(L"mix-blend-mode");
		if (key != std::wstring::npos) {
			size_t p = s->find(L':', key);
			if (p != std::wstring::npos) {
				++p;
				while (p < s->size() && iswspace((*s)[p])) ++p;
				size_t e = p;
				while (e < s->size() && (*s)[e] != L';' && !iswspace((*s)[e])) ++e;
				std::string css;
				for (size_t i = p; i < e; ++i) css.push_back(static_cast<char>((*s)[i]));   // 키워드는 ASCII
				LayerBlendMode mode = LayerBlendMode::Normal;
				if (BlendModeFromCssName(css, mode)) n.SetBlendMode(mode);
			}
		}
	}
}


// ---- <style> CSS: 단순 선택자(태그, .class, #id, tag.class, *, 쉼표 목록) ----
struct CssRule {
	std::vector<std::wstring> selectors;
	std::unordered_map<std::wstring, std::wstring> decls;
};

void ParseStyleSheet(const std::wstring& textIn, std::vector<CssRule>& rules) {
	std::wstring text;
	for (size_t i = 0; i < textIn.size(); ++i) {   // /* 주석 */ 제거
		if (textIn[i] == L'/' && i + 1 < textIn.size() && textIn[i + 1] == L'*') {
			const size_t e = textIn.find(L"*/", i + 2);
			if (e == std::wstring::npos) break;
			i = e + 1;
		} else text.push_back(textIn[i]);
	}
	size_t pos = 0;
	while (pos < text.size()) {
		const size_t open = text.find(L'{', pos);
		if (open == std::wstring::npos) break;
		const size_t close = text.find(L'}', open);
		if (close == std::wstring::npos) break;
		std::wstring sel = text.substr(pos, open - pos);
		CssRule rule;
		rule.decls = ParseCssDeclarations(text.substr(open + 1, close - open - 1));
		size_t sp = 0;
		while (sp <= sel.size()) {
			size_t c = sel.find(L',', sp);
			if (c == std::wstring::npos) c = sel.size();
			std::wstring one = sel.substr(sp, c - sp);
			while (!one.empty() && iswspace(one.front())) one.erase(one.begin());
			while (!one.empty() && iswspace(one.back())) one.pop_back();
			if (!one.empty() && one[0] != L'@') rule.selectors.push_back(one);
			sp = c + 1;
		}
		if (!rule.selectors.empty() && sel.find(L'@') == std::wstring::npos) rules.push_back(std::move(rule));
		pos = close + 1;
	}
}

bool CssSelectorMatches(const std::wstring& sel, const std::wstring& tag, const std::wstring& id, const std::wstring& classes) {
	if (sel == L"*") return true;
	std::wstring want = sel;
	std::wstring selTag, selId;
	std::vector<std::wstring> selClasses;
	size_t i = 0;
	while (i < want.size()) {
		const wchar_t kind = want[i];
		size_t j = i + ((kind == L'.' || kind == L'#') ? 1 : 0);
		size_t k = j;
		while (k < want.size() && want[k] != L'.' && want[k] != L'#') ++k;
		std::wstring part = want.substr(j, k - j);
		if (kind == L'.') selClasses.push_back(part);
		else if (kind == L'#') selId = part;
		else selTag = part;
		i = k;
	}
	if (!selTag.empty() && selTag != tag) return false;
	if (!selId.empty() && selId != id) return false;
	for (const auto& c : selClasses) {
		bool found = false;
		size_t p = 0;
		while (p < classes.size()) {
			while (p < classes.size() && iswspace(classes[p])) ++p;
			size_t e = p;
			while (e < classes.size() && !iswspace(classes[e])) ++e;
			if (classes.compare(p, e - p, c) == 0 && e - p == c.size()) { found = true; break; }
			p = e;
		}
		if (!found) return false;
	}
	return true;
}

// 우선순위: 표현 속성 < <style> 규칙 < style="" 속성
void ApplyStyleSources(const std::wstring& elemName, std::unordered_map<std::wstring, std::wstring>& attrs, const std::vector<CssRule>& rules) {
	if (!rules.empty()) {
		const std::wstring id = attrs.count(L"id") ? attrs[L"id"] : L"";
		const std::wstring cls = attrs.count(L"class") ? attrs[L"class"] : L"";
		for (const auto& r : rules)
			for (const auto& sel : r.selectors)
				if (CssSelectorMatches(sel, elemName, id, cls)) {
					for (const auto& d : r.decls) attrs[d.first] = d.second;
					break;
				}
	}
	auto st = attrs.find(L"style");
	if (st != attrs.end())
		for (const auto& d : ParseCssDeclarations(st->second)) attrs[d.first] = d.second;
}

// 그룹에서 자식으로 상속되는 표현 속성
const wchar_t* const kInheritedProps[] = { L"fill", L"fill-opacity", L"fill-rule", L"stroke", L"stroke-width", L"stroke-opacity",
	L"stroke-linecap", L"stroke-linejoin", L"stroke-miterlimit", L"stroke-dasharray", L"stroke-dashoffset", L"color" };

using StyleMap = std::unordered_map<std::wstring, std::wstring>;

void ApplyInheritance(StyleMap& attrs, const StyleMap& inherited) {
	for (const wchar_t* k : kInheritedProps) {
		if (!attrs.count(k)) {
			auto it = inherited.find(k);
			if (it != inherited.end()) attrs[k] = it->second;
		}
	}
	// currentColor -> color 속성 (없으면 검정)
	const std::wstring cur = attrs.count(L"color") ? attrs[L"color"] : L"#000000";
	for (const wchar_t* k : { L"fill", L"stroke" })
		if (attrs.count(k) && attrs[k] == L"currentColor") attrs[k] = cur;
}

std::wstring Num(float v) {
	std::wostringstream o;
	o << v;
	return o.str();
}

// <rect> <circle> <ellipse> <line> <polyline> <polygon> -> 패스 d 문자열 (성공 시 true)
bool ShapeToPathData(const std::wstring& elem, StyleMap& a, std::wstring& d) {
	auto L = [&](const wchar_t* k, float def) { return a.count(k) ? ParseLength(a[k], def) : def; };
	std::wostringstream o;
	if (elem == L"rect") {
		const float x = L(L"x", 0), y = L(L"y", 0), w = L(L"width", 0), h = L(L"height", 0);
		if (w <= 0 || h <= 0) return false;
		float rx = a.count(L"rx") ? L(L"rx", 0) : -1, ry = a.count(L"ry") ? L(L"ry", 0) : -1;
		if (rx < 0 && ry >= 0) rx = ry;
		if (ry < 0 && rx >= 0) ry = rx;
		rx = (std::max)(0.0f, (std::min)(rx, w / 2)); ry = (std::max)(0.0f, (std::min)(ry, h / 2));
		if (rx <= 0 || ry <= 0) {
			o << L"M " << x << L' ' << y << L" H " << x + w << L" V " << y + h << L" H " << x << L" Z";
		} else {
			o << L"M " << x + rx << L' ' << y << L" H " << x + w - rx << L" A " << rx << L' ' << ry << L" 0 0 1 " << x + w << L' ' << y + ry
			  << L" V " << y + h - ry << L" A " << rx << L' ' << ry << L" 0 0 1 " << x + w - rx << L' ' << y + h
			  << L" H " << x + rx << L" A " << rx << L' ' << ry << L" 0 0 1 " << x << L' ' << y + h - ry
			  << L" V " << y + ry << L" A " << rx << L' ' << ry << L" 0 0 1 " << x + rx << L' ' << y << L" Z";
		}
	} else if (elem == L"circle" || elem == L"ellipse") {
		const float cx = L(L"cx", 0), cy = L(L"cy", 0);
		float rx, ry;
		if (elem == L"circle") rx = ry = L(L"r", 0); else { rx = L(L"rx", 0); ry = L(L"ry", 0); }
		if (rx <= 0 || ry <= 0) return false;
		o << L"M " << cx + rx << L' ' << cy << L" A " << rx << L' ' << ry << L" 0 1 1 " << cx - rx << L' ' << cy
		  << L" A " << rx << L' ' << ry << L" 0 1 1 " << cx + rx << L' ' << cy << L" Z";
	} else if (elem == L"line") {
		o << L"M " << L(L"x1", 0) << L' ' << L(L"y1", 0) << L" L " << L(L"x2", 0) << L' ' << L(L"y2", 0);
	} else if (elem == L"polyline" || elem == L"polygon") {
		if (!a.count(L"points")) return false;
		const std::wstring& pts = a[L"points"];
		const wchar_t* p = pts.c_str();
		const wchar_t* e = p + pts.size();
		float x, y;
		bool first = true;
		while (ParseNextFloat(p, e, x) && ParseNextFloat(p, e, y)) {
			o << (first ? L"M " : L" L ") << x << L' ' << y;
			first = false;
		}
		if (first) return false;
		if (elem == L"polygon") o << L" Z";
	} else return false;
	d = o.str();
	return true;
}

// XML 스트림으로부터 Document 파싱
bool ParseXmlStream(IStream* pStream, Document& outDoc) {
	if (!pStream) return false;

	CComPtr<IXmlReader> pReader;
	HRESULT hr = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&pReader), nullptr);
	if (FAILED(hr) || !pReader) return false;

	hr = pReader->SetInput(pStream);
	if (FAILED(hr)) return false;

	outDoc.Clear(false);

	std::vector<GroupNode*> groupStack;
	XmlNodeType nodeType;
	bool sawSvgRoot = false;
	std::wstring fileVersion;

	// 레이어 밖에 있는 요소(<g inkscape:groupmode="layer"> 가 아닌 최상위 <path>/<g>)는 하나의 대체 레이어에 모음.
	// (요소마다 레이어를 만들면 순서 변경(Z-order)이 레이어 경계에 막혀 동작하지 않음)
	LayerNode* sharedFallbackLayer = nullptr;
	auto getFallbackLayer = [&]() {
		if (!sharedFallbackLayer) sharedFallbackLayer = outDoc.AddLayer(L"Layer 1");
		return sharedFallbackLayer;
	};

	// <defs> 의 그라디언트 (id -> 정의). gradientUnits="userSpaceOnUse" 는 도형 박스로 환산할 수 없어 첫 스톱 단색으로 대체 (Step 2.9)
	std::unordered_map<std::wstring, Gradient> gradients;
	std::unordered_set<std::wstring> userSpaceGradients;
	std::wstring curGradientId;
	Gradient curGradient;
	bool inGradient = false;

	// 외부 SVG 지원: <style> CSS 규칙, 그룹 표현 속성 상속 스택, viewBox 스케일
	TextNode* curText = nullptr;   // 읽는 중인 <text>
	std::wstring curTextRaw;        // m2:text (영역 텍스트의 원본 문자열). 있으면 </text> 에서 자식 텍스트 대신 사용
	bool curTextHasRaw = false;
	bool inStyleElem = false;
	std::wstring styleText;
	std::vector<CssRule> cssRules;
	std::vector<StyleMap> styleStack(1);
	bool hasViewBox = false;
	float vbMinX = 0, vbMinY = 0, vbW = 0, vbH = 0, svgW = 0, svgH = 0;

	// <clipPath id>: 첫 번째 도형을 마스크로 보관했다가, clip-path="url(#id)" 를 가진 그룹이 닫힐 때 그룹의 마지막 자식(= 마스크 노드)으로 추가
	bool inClipPath = false;
	std::wstring curClipId;
	std::unordered_map<std::wstring, std::unique_ptr<PathNode>> clipMasks;
	std::unordered_map<GroupNode*, std::wstring> pendingClipGroups;
	auto finishGradient = [&]() {
		if (!inGradient) return;
		curGradient.NormalizeStops();
		if (!curGradientId.empty()) gradients[curGradientId] = curGradient;
		inGradient = false;
	};

	// Read 는 문서 끝에서 S_FALSE, XML 오류에서는 실패 HRESULT 를 반환 -> 오류 시 부분 문서를 성공으로 취급하지 않음
	while (S_OK == (hr = pReader->Read(&nodeType))) {
		if (nodeType == XmlNodeType_Element) {
			LPCWSTR localName = nullptr;
			UINT nameLen = 0;
			pReader->GetLocalName(&localName, &nameLen);
			if (!localName) continue;
			std::wstring elemName(localName, nameLen);

			bool isEmpty = (pReader->IsEmptyElement() == TRUE);

			// 속성 수집
			std::unordered_map<std::wstring, std::wstring> attrs;
			if (S_OK == pReader->MoveToFirstAttribute()) {
				do {
					LPCWSTR aLocal = nullptr;
					UINT aLocalLen = 0;
					pReader->GetLocalName(&aLocal, &aLocalLen);

					LPCWSTR aPrefix = nullptr;
					UINT aPrefixLen = 0;
					pReader->GetPrefix(&aPrefix, &aPrefixLen);

					LPCWSTR aVal = nullptr;
					UINT aValLen = 0;
					pReader->GetValue(&aVal, &aValLen);

					std::wstring key;
					if (aPrefixLen > 0) {
						key = std::wstring(aPrefix, aPrefixLen) + L":" + std::wstring(aLocal, aLocalLen);
					} else {
						key = std::wstring(aLocal, aLocalLen);
					}
					attrs[key] = std::wstring(aVal, aValLen);
				} while (S_OK == pReader->MoveToNextAttribute());
				pReader->MoveToElement();
			}

			// CSS(<style>, style="") 적용 -> 상속 -> 기본 도형을 path 로 변환
			ApplyStyleSources(elemName, attrs, cssRules);
			const bool isTextElem = (elemName == L"text");
			if (isTextElem) {
				ApplyInheritance(attrs, styleStack.back());
				if (!attrs.count(L"fill")) attrs[L"fill"] = L"#000000";
			}
			const bool isShapeElem = (elemName == L"rect" || elemName == L"circle" || elemName == L"ellipse" ||
				elemName == L"line" || elemName == L"polyline" || elemName == L"polygon" || elemName == L"path");
			if (elemName == L"path" && (attrs.count(L"m2:tableGrid") || attrs.count(L"m2:frameBorder"))) continue;   // 테이블 격자선: 테이블 속성으로 다시 그려지므로 노드로 만들지 않음
			if ((elemName == L"rect" || elemName == L"path") && attrs.count(L"m2:frameBg") && !groupStack.empty()) {
				// 프레임의 배경/테두리 사각형: 프레임 스타일로 복원하고 별도 노드는 만들지 않음
				if (auto* fr = dynamic_cast<FrameNode*>(groupStack.back())) {
					Style st = fr->GetStyle();
					const std::wstring fillV = attrs.count(L"fill") ? attrs[L"fill"] : L"none";
					if (fillV == L"none") st.fill.type = PaintType::None;
					else { st.fill.type = PaintType::Solid; st.fill.color = ParseColorHex(fillV); }
					if (attrs.count(L"fill-opacity")) st.fill.color.a = std::wcstof(attrs[L"fill-opacity"].c_str(), nullptr);
					const std::wstring strokeV = attrs.count(L"stroke") ? attrs[L"stroke"] : L"none";
					if (strokeV == L"none") st.stroke.width = 0.0f;
					else { st.stroke.color = ParseColorHex(strokeV); st.stroke.width = attrs.count(L"stroke-width") ? ParseLength(attrs[L"stroke-width"], 1.0f) : 1.0f; }
if (attrs.count(L"m2:bstroke")) {   // partial borders: stroke style kept in separate attrs
	st.stroke.color = ParseColorHex(attrs[L"m2:bstroke"]);
	st.stroke.width = attrs.count(L"m2:bwidth") ? ParseLength(attrs[L"m2:bwidth"], 1.0f) : 1.0f;
}
					fr->SetStyle(st);
				}
				continue;
			}
			if (isShapeElem) {
				ApplyInheritance(attrs, styleStack.back());
				// SVG 기본값: fill = 검정, stroke = none (자체 출력은 항상 명시하므로 영향 없음)
				if (!attrs.count(L"fill")) attrs[L"fill"] = L"#000000";
				if (!attrs.count(L"stroke")) attrs[L"stroke"] = L"none";
				if (elemName != L"path") {
					std::wstring d;
					if (!ShapeToPathData(elemName, attrs, d)) continue;
					attrs[L"d"] = d;
					elemName = L"path";
				}
			}
			else if (elemName == L"g" || elemName == L"svg") {
				// 그룹/루트의 표현 속성은 자식에게 상속 (복사본을 스택에 쌓고 </g> 에서 pop)
				StyleMap top = styleStack.back();
				for (const wchar_t* k : kInheritedProps)
					if (attrs.count(k)) top[k] = attrs[k];
				ApplyInheritance(top, styleStack.back());
				if (elemName == L"svg") styleStack.back() = top;
				else if (!isEmpty) styleStack.push_back(top);
			}
			else if (elemName == L"style") {
				if (!isEmpty) { inStyleElem = true; styleText.clear(); }
			}
			else if (elemName == L"image") {
				// <image xlink:href="data:image/png;base64,..."> (data URI 만 지원: 외부 파일 참조는 읽지 않음)
				std::wstring href = attrs.count(L"xlink:href") ? attrs[L"xlink:href"] : (attrs.count(L"href") ? attrs[L"href"] : L"");
				const size_t comma = href.find(L',');
				if (href.compare(0, 5, L"data:") == 0 && comma != std::wstring::npos && href.find(L"base64") < comma) {
					auto data = std::make_shared<ImageData>();
					std::string mime;
					for (size_t i = 5; i < comma && href[i] != L';'; ++i) mime.push_back(static_cast<char>(href[i]));
					if (!mime.empty()) data->mime = mime;
					std::string b64;
					for (size_t i = comma + 1; i < href.size(); ++i) b64.push_back(static_cast<char>(href[i] < 128 ? href[i] : '?'));
					if (Base64Decode(b64, data->bytes)) {
						int pw = 0, ph = 0;
						if (PathOp::DecodeImageInfo(data->bytes, pw, ph)) { data->pixelW = pw; data->pixelH = ph; }
						auto im = std::make_unique<ImageNode>();
						outDoc.RestoreNodeId(*im, ParseNodeId(attrs));
						ApplyCommonState(*im, attrs);
						if (attrs.count(L"m2:name")) im->SetName(attrs[L"m2:name"]);
						float w = attrs.count(L"width") ? ParseLength(attrs[L"width"], 0.0f) : 0.0f;
						float h = attrs.count(L"height") ? ParseLength(attrs[L"height"], 0.0f) : 0.0f;
						if (w <= 0.0f) w = data->pixelW > 0 ? static_cast<float>(data->pixelW) : 100.0f;
						if (h <= 0.0f) h = data->pixelH > 0 ? static_cast<float>(data->pixelH) : 100.0f;
						im->SetSize(w, h);
						if (attrs.count(L"preserveAspectRatio")) {
							const std::wstring& par = attrs[L"preserveAspectRatio"];
							if (par.find(L"slice") != std::wstring::npos) im->SetFit(ImageFit::Fill);
							else if (par.find(L"none") != std::wstring::npos) im->SetFit(ImageFit::Stretch);
							else im->SetFit(ImageFit::Fit);   // 속성이 있으면 기본값은 meet
						} else {
							im->SetFit(ImageFit::Fit);   // SVG 기본 preserveAspectRatio = xMidYMid meet
						}
						im->SetData(data);
						if (attrs.count(L"opacity")) im->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
						const float x = attrs.count(L"x") ? ParseLength(attrs[L"x"], 0.0f) : 0.0f;
						const float y = attrs.count(L"y") ? ParseLength(attrs[L"y"], 0.0f) : 0.0f;
						Matrix2D xf = Matrix2D::Translation(x, y);
						if (attrs.count(L"transform")) xf = xf * ParseTransformMatrix(attrs[L"transform"]);
						im->SetTransform(xf);
						if (!groupStack.empty()) groupStack.back()->AddChild(std::move(im));
						else getFallbackLayer()->AddChild(std::move(im));
					}
				}
			}
			else if (isTextElem) {
				auto tn = std::make_unique<TextNode>();
				outDoc.RestoreNodeId(*tn, ParseNodeId(attrs));
				ApplyCommonState(*tn, attrs);
				if (attrs.count(L"m2:name")) tn->SetName(attrs[L"m2:name"]);
				const float size = attrs.count(L"font-size") ? ParseLength(attrs[L"font-size"], 16.0f) : 16.0f;
				tn->SetFontSize(size);
				if (attrs.count(L"font-family")) {
					std::wstring f = attrs[L"font-family"];
					const size_t comma = f.find(L',');
					if (comma != std::wstring::npos) f = f.substr(0, comma);
					std::wstring clean;
					for (wchar_t c : f) if (c != L'\'' && c != L'"') clean.push_back(c);
					while (!clean.empty() && iswspace(clean.front())) clean.erase(clean.begin());
					while (!clean.empty() && iswspace(clean.back())) clean.pop_back();
					if (!clean.empty()) tn->SetFontFamily(clean);
				}
				if (attrs.count(L"font-weight")) {
					const std::wstring& w = attrs[L"font-weight"];
					tn->SetBold(w == L"bold" || w == L"bolder" || std::wcstol(w.c_str(), nullptr, 10) >= 600);
				}
				if (attrs.count(L"font-style")) tn->SetItalic(attrs[L"font-style"] == L"italic" || attrs[L"font-style"] == L"oblique");
				if (attrs.count(L"text-anchor")) {
					const std::wstring& a = attrs[L"text-anchor"];
					tn->SetAlign(a == L"middle" ? TextAlign::Center : a == L"end" ? TextAlign::Right : TextAlign::Left);
				}
				Style st = tn->GetStyle();
				if (attrs.count(L"fill")) {
					if (attrs[L"fill"] == L"none") st.fill.type = PaintType::None;
					else { st.fill.type = PaintType::Solid; st.fill.color = ParseColorHex(attrs[L"fill"]); }
				}
				if (attrs.count(L"fill-opacity")) st.fill.color.a = std::wcstof(attrs[L"fill-opacity"].c_str(), nullptr);
				tn->SetStyle(st);
				if (attrs.count(L"opacity")) tn->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));

				// x/y(기준선) -> 상자 위쪽. 요소의 transform 보다 먼저 적용
				float x = attrs.count(L"x") ? ParseLength(attrs[L"x"], 0.0f) : 0.0f;
				const float y = attrs.count(L"y") ? ParseLength(attrs[L"y"], 0.0f) : 0.0f;
				curTextHasRaw = false;
				if (attrs.count(L"m2:boxW")) {
					// 영역 텍스트: x 는 정렬 기준점이므로 상자 왼쪽 x 로 환산
					const float bw = ParseLength(attrs[L"m2:boxW"], 0.0f);
					tn->SetBoxWidth(bw);
					if (tn->GetAlign() == TextAlign::Center) x -= bw * 0.5f; else if (tn->GetAlign() == TextAlign::Right) x -= bw;
					if (attrs.count(L"m2:text")) { curTextRaw = attrs[L"m2:text"]; curTextHasRaw = true; }
				}
				Matrix2D xf = Matrix2D::Translation(x, y - size * TextNode::kAscentRatio);
				if (attrs.count(L"transform")) xf = xf * ParseTransformMatrix(attrs[L"transform"]);
				tn->SetTransform(xf);
				tn->SetText(L"");

				TextNode* raw = tn.get();
				if (!groupStack.empty()) groupStack.back()->AddChild(std::move(tn));
				else getFallbackLayer()->AddChild(std::move(tn));
				if (!isEmpty) curText = raw;
			}
			else if (elemName == L"tspan" && curText) {
				if (!curText->GetText().empty()) {   // next line: strip trailing spaces of the previous line
					std::wstring prev = curText->GetText();
					while (!prev.empty() && prev.back() == L' ') prev.pop_back();
					curText->SetText(prev + L"\n");
				}
			}

			if (elemName == L"svg") {
				sawSvgRoot = true;
				if (attrs.count(L"width")) svgW = ParseLength(attrs[L"width"], 0.0f);
				if (attrs.count(L"height")) svgH = ParseLength(attrs[L"height"], 0.0f);
				if (attrs.count(L"viewBox")) {
					const wchar_t* p = attrs[L"viewBox"].c_str();
					const wchar_t* end = p + attrs[L"viewBox"].length();
					if (ParseNextFloat(p, end, vbMinX) && ParseNextFloat(p, end, vbMinY) && ParseNextFloat(p, end, vbW) && ParseNextFloat(p, end, vbH) &&
					    vbW > 0 && vbH > 0) {
						hasViewBox = true;
						// 문서 크기 = width/height (없으면 viewBox 크기). viewBox 와 다르면 읽기 끝에서 내용에 스케일 적용
						outDoc.width = svgW > 0 ? svgW : vbW;
						outDoc.height = svgH > 0 ? svgH : vbH;
					}
				} else {
					if (svgW > 0) outDoc.width = svgW;
					if (svgH > 0) outDoc.height = svgH;
				}
				if (attrs.count(L"m2:backgroundColor")) {
					outDoc.backgroundColor = ParseColorHex(attrs[L"m2:backgroundColor"]);
				}
				if (attrs.count(L"m2:backgroundOpacity")) {
					outDoc.backgroundColor.a = std::wcstof(attrs[L"m2:backgroundOpacity"].c_str(), nullptr);
				}
				if (attrs.count(L"m2:canvasColor")) {
					outDoc.canvasColor = ParseColorHex(attrs[L"m2:canvasColor"]);
				}
				if (attrs.count(L"m2:originMode")) {
					if (attrs[L"m2:originMode"] == L"bottomLeft") {
						outDoc.originMode = CanvasOriginMode::BottomLeft;
					} else {
						outDoc.originMode = CanvasOriginMode::TopLeft;
					}
				}
				if (attrs.count(L"m2:version")) {
					fileVersion = attrs[L"m2:version"];
				}
			}
			else if (elemName == L"sodipodi:namedview") {
				if (attrs.count(L"pagecolor")) {
					outDoc.backgroundColor = ParseColorHex(attrs[L"pagecolor"]);
				}
			}
			else if (elemName == L"g") {
				bool isLayer = (attrs.count(L"inkscape:groupmode") && attrs[L"inkscape:groupmode"] == L"layer");
				if (isLayer) {
					std::wstring label = L"Layer";
					if (attrs.count(L"inkscape:label")) label = attrs[L"inkscape:label"];
					else if (attrs.count(L"id")) label = attrs[L"id"];

					LayerNode* layer = outDoc.AddLayer(label);
					outDoc.RestoreNodeId(*layer, ParseNodeId(attrs));
					ApplyCommonState(*layer, attrs);
					if (attrs.count(L"opacity")) {
						layer->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
					}

					if (!isEmpty) {
						groupStack.push_back(layer);
					}
				} else {
					std::unique_ptr<GroupNode> group;
					const bool isFrame = attrs.count(L"m2:frame") && attrs[L"m2:frame"] == L"true";
					const bool isTable = attrs.count(L"m2:component") && attrs[L"m2:component"] == L"table";
					if (isTable) {
						auto tn = std::make_unique<TableNode>();
						tn->SetSizes(ParseFloatList(attrs.count(L"m2:cols") ? attrs[L"m2:cols"] : std::wstring()),
							ParseFloatList(attrs.count(L"m2:rows") ? attrs[L"m2:rows"] : std::wstring()));
						if (attrs.count(L"m2:border")) tn->SetBorderWidth(ParseLength(attrs[L"m2:border"], 1.0f));
						if (attrs.count(L"m2:pad")) tn->SetCellPadding(ParseLength(attrs[L"m2:pad"], 6.0f));
						if (attrs.count(L"m2:bcolor")) tn->SetBorderColor(ParseColorHex(attrs[L"m2:bcolor"]));
							if (attrs.count(L"m2:bstyle")) tn->SetBorderStyle(static_cast<int>(ParseLength(attrs[L"m2:bstyle"], 0.0f)));
							tn->SetHeaderRows(attrs.count(L"m2:header") ? static_cast<int>(ParseLength(attrs[L"m2:header"], 0.0f)) : 0);
							if (attrs.count(L"m2:calign")) tn->SetCellAlign(static_cast<int>(ParseLength(attrs[L"m2:calign"], 0.0f)));
							if (attrs.count(L"m2:merges")) {
								std::vector<TableNode::Span> spans;
								const std::wstring all = attrs[L"m2:merges"];
								size_t pos = 0;
								while (pos < all.size()) {
									size_t e = all.find(L';', pos);
									if (e == std::wstring::npos) e = all.size();
									const std::vector<float> v = ParseFloatList(all.substr(pos, e - pos));
									if (v.size() == 4) {
										TableNode::Span sp;
										sp.r = static_cast<int>(v[0]); sp.c = static_cast<int>(v[1]);
										sp.rs = static_cast<int>(v[2]); sp.cs = static_cast<int>(v[3]);
										spans.push_back(sp);
									}
									pos = e + 1;
								}
								tn->SetMerges(spans);
							}
						group = std::move(tn);
					}
					else if (isFrame) {
						auto fn = std::make_unique<FrameNode>();
						fn->SetFrameSize(attrs.count(L"m2:w") ? ParseLength(attrs[L"m2:w"], 100.0f) : 100.0f, attrs.count(L"m2:h") ? ParseLength(attrs[L"m2:h"], 100.0f) : 100.0f);
						fn->SetClip(!(attrs.count(L"m2:clip") && attrs[L"m2:clip"] == L"false"));
							if (attrs.count(L"m2:ui")) {
								fn->SetUiKind(UiComp::WideToUtf8(attrs[L"m2:ui"]));
								if (attrs.count(L"m2:uiProps")) fn->SetUiProps(UiComp::DecodeProps(UiComp::WideToUtf8(attrs[L"m2:uiProps"])));
							}
						if (attrs.count(L"m2:radius")) fn->SetCornerRadius(ParseLength(attrs[L"m2:radius"], 0.0f));
							if (attrs.count(L"m2:radius4")) {
								const std::vector<float> v = ParseFloatList(attrs[L"m2:radius4"]);
								if (v.size() == 4) fn->SetCornerRadii(CornerRadii{ v[0], v[1], v[2], v[3] });
							}
							if (attrs.count(L"m2:borderSides")) fn->SetBorderSides(static_cast<uint8_t>(ParseLength(attrs[L"m2:borderSides"], 15.0f)));
							if (attrs.count(L"m2:sizingW")) fn->SetSizingW(ParseSizingMode(attrs[L"m2:sizingW"]));
							if (attrs.count(L"m2:sizingH")) fn->SetSizingH(ParseSizingMode(attrs[L"m2:sizingH"]));
						if (attrs.count(L"m2:layout")) {
							fn->SetLayoutMode(attrs[L"m2:layout"] == L"row" ? FrameLayoutMode::Row : attrs[L"m2:layout"] == L"column" ? FrameLayoutMode::Column : FrameLayoutMode::None);
							if (attrs.count(L"m2:gap")) fn->SetGap(ParseLength(attrs[L"m2:gap"], 10.0f));
							if (attrs.count(L"m2:pad")) fn->SetPadding(ParseLength(attrs[L"m2:pad"], 10.0f));
								if (attrs.count(L"m2:padding4")) {
									const std::vector<float> v = ParseFloatList(attrs[L"m2:padding4"]);
									if (v.size() == 4) fn->SetPaddingEdges(PaddingEdges{ v[0], v[1], v[2], v[3] });
								}
							if (attrs.count(L"m2:mainAlign")) {
								const auto& a = attrs[L"m2:mainAlign"];
								if (a == L"center") fn->SetMainAlign(LayoutMainAlign::Center);
								else if (a == L"end") fn->SetMainAlign(LayoutMainAlign::End);
								else if (a == L"space-between") fn->SetMainAlign(LayoutMainAlign::SpaceBetween);
								else fn->SetMainAlign(LayoutMainAlign::Start);
							}
							if (attrs.count(L"m2:crossAlign")) {
								const auto& a = attrs[L"m2:crossAlign"];
								if (a == L"center") fn->SetCrossAlign(LayoutCrossAlign::Center);
								else if (a == L"end") fn->SetCrossAlign(LayoutCrossAlign::End);
								else fn->SetCrossAlign(LayoutCrossAlign::Start);
							}
							if (attrs.count(L"m2:sizingW")) {
								fn->SetSizingW(ParseSizingMode(attrs[L"m2:sizingW"]));
							}
							if (attrs.count(L"m2:sizingH")) {
								fn->SetSizingH(ParseSizingMode(attrs[L"m2:sizingH"]));
							}
						}
						group = std::move(fn);
					} else {
						group = std::make_unique<GroupNode>();
					}
					outDoc.RestoreNodeId(*group, ParseNodeId(attrs));
					ApplyCommonState(*group, attrs);
					if (attrs.count(L"m2:name")) group->SetName(attrs[L"m2:name"]);
					if (attrs.count(L"opacity")) group->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
					if (attrs.count(L"transform")) group->SetTransform(ParseTransformMatrix(attrs[L"transform"]));
					std::wstring clipRefId;
					if (attrs.count(L"clip-path")) {
						const std::wstring& cp = attrs[L"clip-path"];
						const size_t hash = cp.find(L'#');
						const size_t close = cp.find(L')', hash == std::wstring::npos ? 0 : hash);
						if (hash != std::wstring::npos && close != std::wstring::npos && close > hash)
							clipRefId = cp.substr(hash + 1, close - hash - 1);
					}

					GroupNode* rawGroup = group.get();
					if (!groupStack.empty()) {
						groupStack.back()->AddChild(std::move(group));
					} else {
						getFallbackLayer()->AddChild(std::move(group));
					}

					if (!clipRefId.empty() && !isFrame) pendingClipGroups[rawGroup] = clipRefId;   // 프레임의 clip-path 는 프레임 상자 클립이라 마스크 그룹으로 만들지 않음

					if (!isEmpty) {
						groupStack.push_back(rawGroup);
					}
				}
			}
			else if (elemName == L"clipPath") {
				if (!isEmpty && attrs.count(L"id")) {
					inClipPath = true;
					curClipId = attrs[L"id"];
				}
			}
			else if (elemName == L"linearGradient" || elemName == L"radialGradient") {
				finishGradient();   // 중첩/이전 정의가 닫히지 않은 경우 대비
				auto get = [&](const wchar_t* k) -> const std::wstring* { auto it = attrs.find(k); return it == attrs.end() ? nullptr : &it->second; };
				curGradient = Gradient();
				curGradientId = get(L"id") ? *get(L"id") : L"";
				if (elemName == L"linearGradient") {
					curGradient.kind = Gradient::Kind::Linear;
					curGradient.start = { ParseRatio(get(L"x1"), 0.0f), ParseRatio(get(L"y1"), 0.0f) };
					curGradient.end   = { ParseRatio(get(L"x2"), 1.0f), ParseRatio(get(L"y2"), 0.0f) };
				} else {
					curGradient.kind = Gradient::Kind::Radial;
					curGradient.start  = { ParseRatio(get(L"cx"), 0.5f), ParseRatio(get(L"cy"), 0.5f) };
					curGradient.radius = ParseRatio(get(L"r"), 0.5f);
					curGradient.end    = { ParseRatio(get(L"fx"), curGradient.start.x), ParseRatio(get(L"fy"), curGradient.start.y) };
				}
				if (get(L"gradientUnits") && *get(L"gradientUnits") == L"userSpaceOnUse") userSpaceGradients.insert(curGradientId);
				inGradient = true;
				if (isEmpty) finishGradient();
			}
			else if (elemName == L"stop" && inGradient) {
				auto get = [&](const wchar_t* k) -> const std::wstring* { auto it = attrs.find(k); return it == attrs.end() ? nullptr : &it->second; };
				// stop-color / stop-opacity 는 속성 또는 style 선언 둘 다 가능 (Inkscape 는 style 사용)
				std::unordered_map<std::wstring, std::wstring> css;
				if (get(L"style")) css = ParseCssDeclarations(*get(L"style"));
				const std::wstring* colorStr = get(L"stop-color");
				const std::wstring* opacityStr = get(L"stop-opacity");
				if (css.count(L"stop-color")) colorStr = &css[L"stop-color"];
				if (css.count(L"stop-opacity")) opacityStr = &css[L"stop-opacity"];
				GradientStop st;
				st.offset = ParseRatio(get(L"offset"), 0.0f);
				st.color = colorStr ? ParseColorHex(*colorStr) : Color(0.0f, 0.0f, 0.0f, 1.0f);
				st.color.a = (std::max)(0.0f, (std::min)(1.0f, ParseRatio(opacityStr, 1.0f)));
				curGradient.stops.push_back(st);
			}
			else if (elemName == L"path") {
				auto pathNode = std::make_unique<PathNode>();
				outDoc.RestoreNodeId(*pathNode, ParseNodeId(attrs));
				ApplyCommonState(*pathNode, attrs);
				if (attrs.count(L"m2:name")) pathNode->SetName(attrs[L"m2:name"]);

				// d 속성 파싱
				if (attrs.count(L"d")) {
					std::vector<SubPath> paths;
					if (ParsePathD(attrs[L"d"], paths)) {
						pathNode->SetPaths(paths);
					}
				}

				// 스타일
				Style st = pathNode->GetStyle();
				if (attrs.count(L"fill")) {
					const std::wstring gradRef = ParseUrlRef(attrs[L"fill"]);
					if (attrs[L"fill"] == L"none") {
						st.fill.type = PaintType::None;
					} else if (!gradRef.empty()) {
						auto git = gradients.find(gradRef);
						if (git == gradients.end() || git->second.stops.empty()) {
							st.fill.type = PaintType::None;                       // 알 수 없는 참조 (패턴 등은 Step 2.9)
						} else if (userSpaceGradients.count(gradRef) || !git->second.IsRenderable()) {
							st.fill.type = PaintType::Solid;                      // 환산 불가/스톱 1개: 첫 스톱 색으로 대체
							st.fill.color = git->second.FallbackColor();
						} else {
							st.fill.type = PaintType::Gradient;
							st.fill.gradient = git->second;
						}
					} else {
						st.fill.type = PaintType::Solid;
						st.fill.color = ParseColorHex(attrs[L"fill"]);
					}
				}
				if (attrs.count(L"fill-opacity")) {
					st.fill.color.a = std::wcstof(attrs[L"fill-opacity"].c_str(), nullptr);
				}
				if (attrs.count(L"fill-rule")) {
					if (attrs[L"fill-rule"] == L"evenodd") {
						st.fillMode = FillMode::Alternate;
					} else {
						st.fillMode = FillMode::Winding;
					}
				}

				if (attrs.count(L"stroke")) {
					if (attrs[L"stroke"] == L"none") {
						st.stroke.width = 0.0f;
					} else {
						st.stroke.color = ParseColorHex(attrs[L"stroke"]);
					}
				}
				if (attrs.count(L"stroke-width")) {
					st.stroke.width = std::wcstof(attrs[L"stroke-width"].c_str(), nullptr);
				}
				if (attrs.count(L"stroke-opacity")) {
					st.stroke.color.a = std::wcstof(attrs[L"stroke-opacity"].c_str(), nullptr);
				}
				if (attrs.count(L"stroke-linecap")) {
					const std::wstring& v = attrs[L"stroke-linecap"];
					st.stroke.cap = (v == L"round") ? LineCap::Round : (v == L"square") ? LineCap::Square : LineCap::Butt;
				}
				if (attrs.count(L"stroke-linejoin")) {
					const std::wstring& v = attrs[L"stroke-linejoin"];
					st.stroke.join = (v == L"round") ? LineJoin::Round : (v == L"bevel") ? LineJoin::Bevel : LineJoin::Miter;
				}
				if (attrs.count(L"stroke-miterlimit")) {
					const float ml = std::wcstof(attrs[L"stroke-miterlimit"].c_str(), nullptr);
					st.stroke.miterLimit = (ml >= 1.0f) ? ml : 4.0f;   // SVG: 1 미만은 오류 -> 기본값
				}
				if (attrs.count(L"stroke-dasharray")) {
					st.stroke.dashArray = ParseDashArrayText(attrs[L"stroke-dasharray"]);
				}
				if (attrs.count(L"stroke-dashoffset")) {
					st.stroke.dashOffset = std::wcstof(attrs[L"stroke-dashoffset"].c_str(), nullptr);
				}

				pathNode->SetStyle(st);

				if (attrs.count(L"opacity")) {
					pathNode->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
				}
				if (attrs.count(L"transform")) {
					pathNode->SetTransform(ParseTransformMatrix(attrs[L"transform"]));
				}

				// 파라메트릭 정보 복원
				if (attrs.count(L"m2:shape")) {
					const std::wstring& sk = attrs[L"m2:shape"];
					if (sk == L"rect") {
						RectParams p;
						if (attrs.count(L"m2:rx")) p.rx = std::wcstof(attrs[L"m2:rx"].c_str(), nullptr);
						if (attrs.count(L"m2:ry")) p.ry = std::wcstof(attrs[L"m2:ry"].c_str(), nullptr);
						if (attrs.count(L"m2:linkRxRy")) p.linkRxRy = (attrs[L"m2:linkRxRy"] == L"true");
						pathNode->SetShapeKind(ShapeKind::Rect);
						pathNode->SetParams(p);
					} else if (sk == L"triangle") {
						TriangleParams p;
						if (attrs.count(L"m2:equilateral")) p.equilateral = (attrs[L"m2:equilateral"] == L"true");
						pathNode->SetShapeKind(ShapeKind::Triangle);
						pathNode->SetParams(p);
					} else if (sk == L"ellipse") {
						EllipseParams p;
						if (attrs.count(L"m2:linkRxRy")) p.linkRxRy = (attrs[L"m2:linkRxRy"] == L"true");
						pathNode->SetShapeKind(ShapeKind::Ellipse);
						pathNode->SetParams(p);
					} else if (sk == L"polygon") {
						PolygonParams p;
						if (attrs.count(L"m2:sides")) p.sides = std::wcstol(attrs[L"m2:sides"].c_str(), nullptr, 10);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Polygon);
						pathNode->SetParams(p);
					} else if (sk == L"star") {
						StarParams p;
						if (attrs.count(L"m2:points")) p.points = std::wcstol(attrs[L"m2:points"].c_str(), nullptr, 10);
						if (attrs.count(L"m2:innerRatio")) p.innerRatio = std::wcstof(attrs[L"m2:innerRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Star);
						pathNode->SetParams(p);
					} else if (sk == L"gear") {
						GearParams p;
						if (attrs.count(L"m2:teeth")) p.teeth = std::wcstol(attrs[L"m2:teeth"].c_str(), nullptr, 10);
						if (attrs.count(L"m2:depthRatio")) p.depthRatio = std::wcstof(attrs[L"m2:depthRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:boreRatio")) p.boreRatio = std::wcstof(attrs[L"m2:boreRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:toothWidthRatio")) p.toothWidthRatio = std::wcstof(attrs[L"m2:toothWidthRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Gear);
						pathNode->SetParams(p);
					} else if (sk == L"spiral") {
						SpiralParams p;
						if (attrs.count(L"m2:turns")) p.turns = std::wcstof(attrs[L"m2:turns"].c_str(), nullptr);
						if (attrs.count(L"m2:innerRatio")) p.innerRatio = std::wcstof(attrs[L"m2:innerRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Spiral);
						pathNode->SetParams(p);
					} else if (sk == L"heart") {
						pathNode->SetShapeKind(ShapeKind::Heart);
						pathNode->SetParams(HeartParams{});
					}
				}

				// 노드 타입 복원
				if (attrs.count(L"m2:node-types")) {
					ApplyNodeTypes(*pathNode, attrs[L"m2:node-types"]);
				}

				if (inClipPath) {
					// 클립 정의 안의 도형은 화면에 그리지 않는 마스크 후보 (첫 번째만 사용)
					if (!clipMasks.count(curClipId)) clipMasks[curClipId] = std::move(pathNode);
				} else if (!groupStack.empty()) {
					groupStack.back()->AddChild(std::move(pathNode));
				} else {
					getFallbackLayer()->AddChild(std::move(pathNode));
				}
			}
		}
		else if (curText && (nodeType == XmlNodeType_Text || nodeType == XmlNodeType_CDATA || nodeType == XmlNodeType_Whitespace)) {
			LPCWSTR v = nullptr; UINT vl = 0;
			pReader->GetValue(&v, &vl);
			if (v) {
				std::wstring add(v, vl);
				// 들여쓰기/줄바꿈 공백은 하나의 공백으로 (xml:space="preserve" 로 저장된 파일은 그대로 두는 편이 정확하지만, 외부 SVG 는 정규화가 표준)
				std::wstring norm;
				bool lastSpace = curText->GetText().empty() || curText->GetText().back() == L'\n' || curText->GetText().back() == L' ';
				for (wchar_t c : add) {
					if (iswspace(c)) { if (!lastSpace) { norm.push_back(L' '); lastSpace = true; } }
					else { norm.push_back(c); lastSpace = false; }
				}
				if (!norm.empty()) curText->SetText(curText->GetText() + norm);
			}
		}
		else if (inStyleElem && (nodeType == XmlNodeType_Text || nodeType == XmlNodeType_CDATA)) {
			LPCWSTR v = nullptr; UINT vl = 0;
			pReader->GetValue(&v, &vl);
			if (v) styleText.append(v, vl);
		}
		else if (nodeType == XmlNodeType_EndElement) {
			LPCWSTR localName = nullptr;
			UINT nameLen = 0;
			pReader->GetLocalName(&localName, &nameLen);
			if (localName) {
				std::wstring elemName(localName, nameLen);
				if (elemName == L"text" && curText && curTextHasRaw) {
					curText->SetText(curTextRaw);   // 영역 텍스트: 저장된 원본 문자열 복원 (줄바꿈은 다시 계산)
					curTextHasRaw = false;
					curText = nullptr;
				}
				else if (elemName == L"text" && curText) {
					std::wstring tx = curText->GetText();
					while (!tx.empty() && (tx.back() == L' ' || tx.back() == L'\n')) tx.pop_back();
					curText->SetText(tx);
					curText = nullptr;
				}
				if (elemName == L"style" && inStyleElem) {
					inStyleElem = false;
					ParseStyleSheet(styleText, cssRules);
				}
				if (elemName == L"g" && styleStack.size() > 1) styleStack.pop_back();
				if (elemName == L"g") {
					if (!groupStack.empty()) {
						GroupNode* closing = groupStack.back();
						groupStack.pop_back();
						auto pc = pendingClipGroups.find(closing);
						if (pc != pendingClipGroups.end()) {
							auto cm = clipMasks.find(pc->second);
							if (cm != clipMasks.end() && cm->second) {
								// 마스크 노드는 관례상 그룹의 마지막(최상단) 자식. 같은 clipPath 를 여러 그룹이 쓰면 복제본 사용
								closing->AddChild(cm->second->Clone());
								closing->SetClippingMask(true);
							}
							pendingClipGroups.erase(pc);
						}
					}
				}
				else if (elemName == L"clipPath") {
					inClipPath = false;
				}
				else if (elemName == L"linearGradient" || elemName == L"radialGradient") {
					finishGradient();
				}
			}
		}
	}

	// XML 오류(깨진 파일)이거나 SVG 문서가 아니면 실패 (호출 측이 기존 문서를 유지할 수 있도록)
	if (FAILED(hr) || !sawSvgRoot) return false;

	// ID 무결성: 복원된 ID 는 유지하고, 없는/중복된 ID(외부 SVG 등)에만 새 ID 발급
	outDoc.EnsureUniqueIds();

	// 구버전(버전 없는 레거시 파일) 중 bottomLeft 로 저장되었던 도형 좌표 y' = H - y 보정
	if (fileVersion.empty() && outDoc.originMode == CanvasOriginMode::BottomLeft) {
		const float H = outDoc.height;
		std::function<void(Node&)> invertNodeY = [&](Node& n) {
			if (n.Kind() == NodeKind::Path) {
				auto& pNode = static_cast<PathNode&>(n);
				std::vector<SubPath> sps = pNode.GetPaths();
				for (auto& sp : sps) {
					for (auto& pt : sp.nodes) {
						pt.anchor.y = H - pt.anchor.y;
						pt.hIn.y = -pt.hIn.y;
						pt.hOut.y = -pt.hOut.y;
					}
				}
				pNode.SetPaths(sps);
			} else if (n.Kind() == NodeKind::Group || n.Kind() == NodeKind::Layer) {
				auto& gNode = static_cast<GroupNode&>(n);
				for (const auto& child : gNode.GetChildren()) {
					if (child) {
						invertNodeY(*child);
					}
				}
			}
		};
		for (auto& layer : outDoc.GetLayers()) {
			if (layer) invertNodeY(*layer);
		}
	}

	if (outDoc.GetLayers().empty()) {
		outDoc.AddLayer(L"Layer 1");
	}

	// viewBox 좌표계 -> 문서(width x height) 좌표계: 기본 preserveAspectRatio(xMidYMid meet)
	if (hasViewBox && (std::fabs(outDoc.width - vbW) > 1e-3f || std::fabs(outDoc.height - vbH) > 1e-3f || vbMinX != 0.0f || vbMinY != 0.0f)) {
		const float sx = outDoc.width / vbW, sy = outDoc.height / vbH;
		const float sc = (std::min)(sx, sy);
		const float offX = (outDoc.width - vbW * sc) * 0.5f, offY = (outDoc.height - vbH * sc) * 0.5f;
		const Matrix2D vbXf = Matrix2D::Translation(-vbMinX, -vbMinY) * Matrix2D::Scale(sc, sc) * Matrix2D::Translation(offX, offY);
		for (auto& layer : outDoc.GetLayers())
			if (layer) layer->SetTransform(layer->GetTransform() * vbXf);
	}
	outDoc.SetActiveLayer(0);

	return true;
}

} // anonymous namespace

bool SvgReader::ReadFromString(const std::string& svgContent, Document& outDoc) {
	if (svgContent.empty()) return false;

	CComPtr<IStream> pStream;
	pStream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(svgContent.data()), static_cast<UINT>(svgContent.size())));
	if (!pStream) return false;

	return ParseXmlStream(pStream, outDoc);
}

bool SvgReader::ReadFromFile(const std::wstring& filePath, Document& outDoc) {
	if (filePath.empty()) return false;

	CComPtr<IStream> pStream;
	HRESULT hr = SHCreateStreamOnFileEx(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &pStream);
	if (FAILED(hr) || !pStream) return false;

	return ParseXmlStream(pStream, outDoc);
}

} // namespace ge
