#pragma once
// ============================================================================
// D2DGraphicEngine.h  -  M2Draw 2D 그래픽 엔진
//
//  [계층 구조]
//   1. Math      : Vec2 / Rect2 / Matrix2D
//   2. Model     : Node -> PathNode / GroupNode -> LayerNode, Document (Scene Graph)
//   3. Command   : Undo / Redo
//   4. D2D 백엔드: PathOp, D2DRenderer   (Direct2D 의존은 여기에만)
//
//  [D2D 비의존 모델]
//   - 1~3 은 Direct2D 없이 컴파일된다. GE_NO_D2D 를 정의하고 포함하면 D2D 상호 변환
//     편의 멤버(D2D1_* 변환 생성자/연산자/오버로드)와 4번 백엔드가 제외된다.
//     (GeModelPureCheck.cpp 가 이를 빌드 시 검증. SVG/HTML Exporter 는 이 순수 모델만 사용)
//   - 변환 편의 멤버는 데이터 멤버를 추가하지 않으므로 두 모드의 클래스 레이아웃은 동일하다.
//
//  - 순서: 배열 인덱스 0 = 최하단, 마지막 = 최상단.
// ============================================================================

#ifndef GE_NO_D2D
#include <d2d1_1.h>
#include <wrl/client.h>
#endif
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <optional>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace ge {

#ifndef GE_NO_D2D
using Microsoft::WRL::ComPtr;
#endif

// ============================================================================
// 1. Math
// ============================================================================
struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	Vec2() = default;
	Vec2(float _x, float _y) : x(_x), y(_y) {}

	Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
	Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
	Vec2 operator*(float s) const { return { x * s, y * s }; }
	float Length() const { return std::sqrt(x * x + y * y); }

#ifndef GE_NO_D2D
	Vec2(const D2D1_POINT_2F& p) : x(p.x), y(p.y) {}
	operator D2D1_POINT_2F() const { return D2D1::Point2F(x, y); }
	Vec2& operator=(const D2D1_POINT_2F& p) { x = p.x; y = p.y; return *this; }
	D2D1_POINT_2F ToD2D() const { return D2D1::Point2F(x, y); }
	static Vec2 FromD2D(D2D1_POINT_2F p) { return { p.x, p.y }; }
#endif
};

struct Rect2 {
	float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
	float Width()  const { return right - left; }
	float Height() const { return bottom - top; }
	float MidX()   const { return (left + right) * 0.5f; }
	float MidY()   const { return (top + bottom) * 0.5f; }
	bool  IsEmpty() const { return right <= left || bottom <= top; }
	void  Union(const Rect2& o);                 // 두 박스 합집합

	Rect2() = default;
	Rect2(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}

#ifndef GE_NO_D2D
	Rect2(const D2D1_RECT_F& r) : left(r.left), top(r.top), right(r.right), bottom(r.bottom) {}
	operator D2D1_RECT_F() const { return D2D1::RectF(left, top, right, bottom); }
	Rect2& operator=(const D2D1_RECT_F& r) { left = r.left; top = r.top; right = r.right; bottom = r.bottom; return *this; }
	D2D1_RECT_F ToD2D() const { return D2D1::RectF(left, top, right, bottom); }
	static Rect2 FromD2D(D2D1_RECT_F r) { return { r.left, r.top, r.right, r.bottom }; }
#endif
};

// D2D1::Matrix3x2F 와 동일한 레이아웃/곱셈 순서: (P * A) * B
struct Matrix2D {
	union {
		struct { float m11, m12, m21, m22, dx, dy; };
		struct { float _11, _12, _21, _22, _31, _32; };
	};

	Matrix2D() : m11(1), m12(0), m21(0), m22(1), dx(0), dy(0) {}
	Matrix2D(float f11, float f12, float f21, float f22, float f31, float f32)
		: m11(f11), m12(f12), m21(f21), m22(f22), dx(f31), dy(f32) {}

	static Matrix2D Identity() { return {}; }
	bool IsIdentity() const {
		return m11 == 1.0f && m12 == 0.0f && m21 == 0.0f && m22 == 1.0f && dx == 0.0f && dy == 0.0f;
	}
	static Matrix2D Translation(float x, float y);
	static Matrix2D Scale(float sx, float sy, Vec2 center = {});
	static Matrix2D Rotation(float degree, Vec2 center = {});

	Matrix2D operator*(const Matrix2D& rhs) const;
	bool     Invert();                           // 실패 시 false
	Vec2     TransformPoint(Vec2 p) const;
	Rect2    TransformRect(const Rect2& r) const; // AABB 반환

#ifndef GE_NO_D2D
	Matrix2D(const D2D1_MATRIX_3X2_F& m)
		: m11(m._11), m12(m._12), m21(m._21), m22(m._22), dx(m._31), dy(m._32) {}
	operator D2D1_MATRIX_3X2_F() const { return ToD2D(); }
	operator D2D1::Matrix3x2F() const { return D2D1::Matrix3x2F(m11, m12, m21, m22, dx, dy); }
	Matrix2D& operator=(const D2D1_MATRIX_3X2_F& m) {
		m11 = m._11; m12 = m._12; m21 = m._21; m22 = m._22; dx = m._31; dy = m._32;
		return *this;
	}
	Matrix2D operator*(const D2D1_MATRIX_3X2_F& rhs) const {
		return (*this) * Matrix2D::FromD2D(rhs);
	}
	D2D1_POINT_2F TransformPoint(D2D1_POINT_2F p) const {
		Vec2 r = TransformPoint(Vec2(p.x, p.y));
		return D2D1::Point2F(r.x, r.y);
	}
	D2D1_MATRIX_3X2_F ToD2D() const {
		D2D1_MATRIX_3X2_F m;
		m._11 = m11; m._12 = m12;
		m._21 = m21; m._22 = m22;
		m._31 = dx;  m._32 = dy;
		return m;
	}
	static Matrix2D FromD2D(const D2D1_MATRIX_3X2_F& m) {
		return { m._11, m._12, m._21, m._22, m._31, m._32 };
	}
#endif
};

#ifndef GE_NO_D2D
inline Matrix2D operator*(const D2D1_MATRIX_3X2_F& lhs, const Matrix2D& rhs) {
	return Matrix2D::FromD2D(lhs) * rhs;
}
#endif

// 뷰포트 (기존 ViewportTransform 대응)
struct Viewport {
	float zoom = 1.0f;
	Vec2  pan{};
	Matrix2D ToMatrix() const;
	Vec2 ScreenToWorld(Vec2 screenPt) const;
	Vec2 WorldToScreen(Vec2 worldPt) const;

#ifndef GE_NO_D2D
	D2D1_POINT_2F ScreenToWorld(D2D1_POINT_2F screenPt) const {
		Vec2 res = ScreenToWorld(Vec2{ screenPt.x, screenPt.y });
		return res.ToD2D();
	}
	D2D1_POINT_2F WorldToScreen(D2D1_POINT_2F worldPt) const {
		Vec2 res = WorldToScreen(Vec2{ worldPt.x, worldPt.y });
		return res.ToD2D();
	}
#endif
};

// ============================================================================
// 2. Model (Scene Graph) - 렌더링 API에 의존하지 않는 순수 데이터
// ============================================================================
struct Color {
	float r = 0, g = 0, b = 0, a = 1;

	Color() = default;
	Color(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
	static Color FromD2D(const Color& c) { return c; }

#ifndef GE_NO_D2D
	Color(const D2D1_COLOR_F& c) : r(c.r), g(c.g), b(c.b), a(c.a) {}
	operator D2D1_COLOR_F() const { return D2D1::ColorF(r, g, b, a); }
	Color& operator=(const D2D1_COLOR_F& c) { r = c.r; g = c.g; b = c.b; a = c.a; return *this; }
	D2D1_COLOR_F ToD2D() const { return D2D1::ColorF(r, g, b, a); }
	static Color FromD2D(const D2D1_COLOR_F& c) { return { c.r, c.g, c.b, c.a }; }
#endif

	bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const Color& o) const { return !(*this == o); }
};

// ---- 스타일 모델 (Paint / Stroke) -------------------------------------------
// 채우기 종류. Pattern 은 지원하지 않음 (렌더링된 적 없는 자리표시자였음)
enum class PaintType : uint8_t { None = 0, Solid = 1, Gradient = 2 };
enum class FillMode  : uint8_t { Alternate = 0, Winding };
enum class LineCap   : uint8_t { Butt = 0, Round, Square };
enum class LineJoin  : uint8_t { Miter = 0, Round, Bevel };

// 대시 프리셋: UI 버튼/도구 설정용 "변환 헬퍼의 입력". 모델은 프리셋이 아니라 Stroke::dashArray(절대값)를 저장
enum class DashStyle : uint8_t { Solid = 0, Dash, Dot, DashDot, DashDotDot, Custom };

struct GradientStop {
	float offset = 0.0f;   // 0 ~ 1
	Color color;
};

// 그라디언트 모델. 좌표는 도형 로컬 바운딩 박스 기준 비율(0~1, SVG gradientUnits="objectBoundingBox")
//  - Linear: start -> end 방향
//  - Radial: start = 중심(cx,cy), end = 초점(fx,fy), radius = 반지름(박스 너비/높이에 각각 비례하는 타원)
struct Gradient {
	enum class Kind : uint8_t { Linear = 0, Radial };
	Kind kind = Kind::Linear;
	Vec2 start{ 0.0f, 0.0f };
	Vec2 end{ 1.0f, 0.0f };
	float radius = 0.5f;                  // Radial 전용
	std::vector<GradientStop> stops;

	// 그라디언트로 렌더링 가능한 최소 조건: 스톱 2개 이상
	bool IsRenderable() const { return stops.size() >= 2; }

	// 렌더링할 수 없을 때의 대체 색 (첫 스톱, 없으면 투명)
	Color FallbackColor() const { return stops.empty() ? Color(0.0f, 0.0f, 0.0f, 0.0f) : stops.front().color; }

	// 종류별 기본 기하 (선형: 왼쪽->오른쪽, 방사형: 중심에서 바깥으로 초점=중심)
	void ResetGeometry() {
		if (kind == Kind::Linear) { start = { 0.0f, 0.0f }; end = { 1.0f, 0.0f }; radius = 0.5f; }
		else                      { start = { 0.5f, 0.5f }; end = { 0.5f, 0.5f }; radius = 0.5f; }
	}

	// 종류 전환: 스톱은 유지하고 기하만 해당 종류의 기본값으로
	void SetKind(Kind k) { if (kind != k) { kind = k; ResetGeometry(); } }

	static Gradient Make(Kind k, const Color& a, const Color& b) {
		Gradient g;
		g.kind = k;
		g.ResetGeometry();
		g.stops = { { 0.0f, a }, { 1.0f, b } };
		return g;
	}

	// offset 오름차순 정렬(같은 offset 은 기존 순서 유지) 및 0~1 범위로 제한
	void NormalizeStops() {
		for (auto& s : stops) s.offset = (std::max)(0.0f, (std::min)(1.0f, s.offset));
		std::stable_sort(stops.begin(), stops.end(), [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
	}

	// 그라디언트 방향 반전: 모든 스톱의 offset 을 1 - offset 으로
	void Reverse() {
		for (auto& s : stops) s.offset = 1.0f - s.offset;
		NormalizeStops();
	}

	// offset 위치의 색 (스톱 사이 선형 보간, 범위 밖은 양 끝 색). 새 스톱 추가 시 기본 색으로 사용
	Color SampleColor(float t) const {
		if (stops.empty()) return Color(0.0f, 0.0f, 0.0f, 0.0f);
		if (t <= stops.front().offset) return stops.front().color;
		if (t >= stops.back().offset)  return stops.back().color;
		for (size_t i = 1; i < stops.size(); ++i) {
			if (t <= stops[i].offset) {
				const GradientStop& a = stops[i - 1];
				const GradientStop& b = stops[i];
				const float span = b.offset - a.offset;
				const float u = span > 1e-6f ? (t - a.offset) / span : 0.0f;
				return Color(a.color.r + (b.color.r - a.color.r) * u, a.color.g + (b.color.g - a.color.g) * u,
				             a.color.b + (b.color.b - a.color.b) * u, a.color.a + (b.color.a - a.color.a) * u);
			}
		}
		return stops.back().color;
	}
};

struct Paint {
	PaintType type  = PaintType::None;
	Color     color = { 0.2f, 0.6f, 1.0f, 0.25f };   // Solid
	Gradient  gradient;                              // Gradient
};

struct Stroke {
	Color    color      = { 0.12f, 0.56f, 1.0f, 1.0f };
	float    width      = 2.0f;
	LineCap  cap        = LineCap::Butt;
	LineJoin join       = LineJoin::Miter;
	float    miterLimit = 4.0f;                      // SVG 기본값
	// 대시 패턴: 문서 단위(px) 절대값. SVG stroke-dasharray 와 동일 의미이며 화면(D2D)과 출력이 같은 값을 쓴다.
	// 비어 있으면 실선. (D2D 는 선 두께 배수를 요구하므로 렌더러가 width 로 나눠서 넘김)
	std::vector<float> dashArray;
	float    dashOffset = 0.0f;

	bool HasDash() const { return !dashArray.empty(); }
};

struct Style {
	Paint    fill;
	Stroke   stroke;
	FillMode fillMode = FillMode::Alternate;
};

// ---- 대시 변환 헬퍼 (프리셋 <-> dashArray) ------------------------------------
// 프리셋은 선 두께의 배수. 기본 두께(2px)에서 예전 SVG 출력("8 4", "2 2" 등)과 같은 값이 나오도록 정함
inline std::vector<float> DashPresetRatios(DashStyle preset) {
	switch (preset) {
	case DashStyle::Dash:       return { 4.0f, 2.0f };
	case DashStyle::Dot:        return { 1.0f, 1.0f };
	case DashStyle::DashDot:    return { 4.0f, 2.0f, 1.0f, 2.0f };
	case DashStyle::DashDotDot: return { 4.0f, 2.0f, 1.0f, 2.0f, 1.0f, 2.0f };
	default:                    return {};
	}
}

// 프리셋 -> 절대값 dashArray (Solid/Custom 은 빈 배열)
inline std::vector<float> MakeDashArray(DashStyle preset, float strokeWidth) {
	std::vector<float> a = DashPresetRatios(preset);
	for (float& v : a) v *= (std::max)(strokeWidth, 0.0f);
	return a;
}

// dashArray 유효성: 요소 모두 >= 0, 합 > 0, 최대 16개 (D2D/SVG 가 거부하는 값을 모델에 넣지 않기 위함)
inline bool IsValidDashArray(const std::vector<float>& a) {
	if (a.empty() || a.size() > 16) return false;
	float sum = 0.0f;
	for (float v : a) {
		if (!(v >= 0.0f) || !std::isfinite(v)) return false;
		sum += v;
	}
	return sum > 0.0f;
}

// dashArray -> 프리셋 (빈 배열 = Solid, 어떤 프리셋과도 같지 않으면 Custom)
inline DashStyle ClassifyDashArray(const std::vector<float>& a, float strokeWidth) {
	if (a.empty()) return DashStyle::Solid;
	for (DashStyle p : { DashStyle::Dash, DashStyle::Dot, DashStyle::DashDot, DashStyle::DashDotDot }) {
		const std::vector<float> ref = MakeDashArray(p, strokeWidth);
		if (ref.size() != a.size()) continue;
		bool same = true;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::abs(ref[i] - a[i]) > 0.001f + 0.001f * std::abs(ref[i])) { same = false; break; }
		if (same) return p;
	}
	return DashStyle::Custom;
}

// "8 4 2 4" / "8,4" 형태의 문자열 -> dashArray. SVG stroke-dasharray 와 속성창 입력이 같은 규칙을 쓴다.
// 빈 문자열/"none"/숫자가 아닌 토큰/음수/합이 0/17개 이상이면 실선(빈 배열).
// SVG 규격: 개수가 홀수이면 목록을 두 번 반복해 짝수로 만듦
inline std::vector<float> ParseDashArrayText(const std::wstring& text) {
	std::vector<float> values;
	const wchar_t* p = text.c_str();
	const wchar_t* end = p + text.length();
	auto skipSep = [&] { while (p < end && (std::iswspace(*p) || *p == L',')) ++p; };
	for (;;) {
		skipSep();
		if (p >= end) break;
		wchar_t* next = nullptr;
		const float v = std::wcstof(p, &next);
		if (next == p) return {};   // 숫자가 아닌 토큰 ("none" 포함)
		p = next;
		values.push_back(v);
		if (values.size() > 16) return {};
	}
	if (values.size() % 2 == 1) {
		const std::vector<float> once = values;
		values.insert(values.end(), once.begin(), once.end());
	}
	return IsValidDashArray(values) ? values : std::vector<float>{};
}

// dashArray -> 표시용 문자열 ("8 4 2 4"), 실선이면 빈 문자열
inline std::wstring FormatDashArrayText(const std::vector<float>& a) {
	std::wstring s;
	for (size_t i = 0; i < a.size(); ++i) {
		wchar_t buf[32];
		swprintf_s(buf, L"%g", a[i]);
		if (i) s += L" ";
		s += buf;
	}
	return s;
}

struct BezierNode {
	enum class Type : uint8_t { Corner, Smooth, Symmetric };
	Vec2 anchor{};
	Vec2 hIn{};     // 앵커 기준 상대 오프셋
	Vec2 hOut{};
	Type type = Type::Corner;

	Vec2 AbsIn()  const { return { anchor.x + hIn.x,  anchor.y + hIn.y  }; }
	Vec2 AbsOut() const { return { anchor.x + hOut.x, anchor.y + hOut.y }; }
	bool HasIn()  const { return (hIn.x * hIn.x + hIn.y * hIn.y)   > 0.0001f; }
	bool HasOut() const { return (hOut.x * hOut.x + hOut.y * hOut.y) > 0.0001f; }

		// 핸들 이동 시 Smooth / Symmetric 규칙에 따라 반대편 핸들 자동 보정
		void SetHandleOut(Vec2 offset) {
			hOut = offset;
			if (type == Type::Corner) return;
			float lenOut = std::sqrt(offset.x * offset.x + offset.y * offset.y);
			if (lenOut < 0.0001f) return;
			Vec2 opp = { -offset.x / lenOut, -offset.y / lenOut };
			if (type == Type::Symmetric) {
				hIn = { opp.x * lenOut, opp.y * lenOut };
			} else {
				float lenIn = std::sqrt(hIn.x * hIn.x + hIn.y * hIn.y);
				if (lenIn < 0.0001f) lenIn = lenOut;
				hIn = { opp.x * lenIn, opp.y * lenIn };
			}
		}
		void SetHandleIn(Vec2 offset) {
			hIn = offset;
			if (type == Type::Corner) return;
			float lenIn = std::sqrt(offset.x * offset.x + offset.y * offset.y);
			if (lenIn < 0.0001f) return;
			Vec2 opp = { -offset.x / lenIn, -offset.y / lenIn };
			if (type == Type::Symmetric) {
				hOut = { opp.x * lenIn, opp.y * lenIn };
			} else {
				float lenOut = std::sqrt(hOut.x * hOut.x + hOut.y * hOut.y);
				if (lenOut < 0.0001f) lenOut = lenIn;
				hOut = { opp.x * lenOut, opp.y * lenOut };
			}
		}
};

struct SubPath {
	std::vector<BezierNode> nodes;
	bool closed = false;
};

// 자유 배치(LayoutMode::None) 프레임의 크기를 바꿀 때 자식이 따르는 제약 (Step 4.5.11)
enum class ConstraintH : uint8_t { Left = 0, Right = 1, LeftRight = 2, Center = 3 };
enum class ConstraintV : uint8_t { Top = 0, Bottom = 1, TopBottom = 2, Center = 3 };

enum class NodeKind : uint8_t { Path, Group, Layer, Text, Image };

// 혼합 모드 (SVG/CSS mix-blend-mode 와 1:1 대응). 모든 노드에 지정 가능, 렌더링 적용은 Phase 2
enum class LayerBlendMode : uint8_t {
	Normal = 0,    // 정상
	Darken,        // 어둡게
	Multiply,      // 곱하기
	ColorBurn,     // 컬러 번
	Lighten,       // 밝게
	Screen,        // 화면(스크린)
	ColorDodge,    // 컬러 닷지
	Overlay,       // 오버레이
	SoftLight,     // 부드러운 빛
	HardLight,     // 강렬한 빛
	Difference,    // 디퍼런스(다름)
	Exclusion,     // 익스클루전(제외)
	Hue,           // 색조
	Saturation,    // 채도
	Color,         // 색상
	Luminosity     // 광도
};

inline constexpr int kBlendModeCount = 16;

// 혼합 모드 표시 이름 (레이어 패널/속성창 공용, 영문 UI 명칭). 값이 범위를 벗어나면 "Normal"
inline const wchar_t* BlendModeName(LayerBlendMode m) {
	static const wchar_t* const names[kBlendModeCount] = {
		L"Normal", L"Darken", L"Multiply", L"Color Burn", L"Lighten", L"Screen", L"Color Dodge", L"Overlay",
		L"Soft Light", L"Hard Light", L"Difference", L"Exclusion", L"Hue", L"Saturation", L"Color", L"Luminosity"
	};
	const int i = static_cast<int>(m);
	return (i >= 0 && i < kBlendModeCount) ? names[i] : names[0];
}

// CSS/SVG mix-blend-mode 키워드 (표준). 저장·출력 용도 (화면 합성은 렌더 타깃 전환 2.2.6 이후)
inline const char* BlendModeCssName(LayerBlendMode m) {
	static const char* const names[kBlendModeCount] = {
		"normal", "darken", "multiply", "color-burn", "lighten", "screen", "color-dodge", "overlay",
		"soft-light", "hard-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity"
	};
	const int i = static_cast<int>(m);
	return (i >= 0 && i < kBlendModeCount) ? names[i] : names[0];
}

// CSS 키워드 -> 혼합 모드. 알 수 없는 값이면 false (out 은 변경하지 않음)
inline bool BlendModeFromCssName(const std::string& css, LayerBlendMode& out) {
	for (int i = 0; i < kBlendModeCount; ++i) {
		if (css == BlendModeCssName(static_cast<LayerBlendMode>(i))) { out = static_cast<LayerBlendMode>(i); return true; }
	}
	return false;
}

// 불린 연산 모드 (합치기 / 빼기 / 교차 / 제외)
enum class BooleanMode {
	Union = 0,     // 합집합 (D2D1_COMBINE_MODE_UNION)
	Intersect,     // 교집합 (D2D1_COMBINE_MODE_INTERSECT)
	Subtract,      // 차집합 / 빼기 (D2D1_COMBINE_MODE_EXCLUDE: A에서 B 빼기)
	Exclude        // 배타적 논리합 / 겹친 곳 제외 (D2D1_COMBINE_MODE_XOR)
};

// 파라메트릭 도형 종류 (구엔진과 1:1 호환 및 확장 지원)
enum class ShapeKind {
	Path = 0,
	Rect,
	Triangle,
	Ellipse,
	Polygon,
	Star,
	Gear,
	Spiral,
	Heart,
	Arc,        // Step 6.13: 원호 / 부채꼴(Pie) / 활꼴(Chord) / 도넛 호(Donut)
	BlockArrow, // Step 6.13: 블록 화살표
	Callout     // Step 6.13: 말풍선 (콜아웃)
};

enum class ArcMode : uint8_t {
	Open  = 0,  // 열린 원호 (Open Arc)
	Pie   = 1,  // 부채꼴 (Pie / Sector)
	Chord = 2,  // 활꼴 (Chord)
	Donut = 3   // 도넛 / 링 부채꼴 (Donut / Ring Arc)
};

struct RectParams       { float rx = 0.0f; float ry = 0.0f; bool linkRxRy = true; };
struct TriangleParams   { bool equilateral = false; };
struct EllipseParams    { bool linkRxRy = true; };
struct PolygonParams    { int sides = 5; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct StarParams       { int points = 5; float innerRatio = 0.5f; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct GearParams       { int teeth = 12; float depthRatio = 0.2f; float boreRatio = 0.3f; float toothWidthRatio = 0.5f; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct SpiralParams     { float turns = 3.0f; float innerRatio = 0.1f; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct HeartParams      { };
struct ArcParams        { float startAngle = 0.0f; float sweepAngle = 270.0f; float innerRatio = 0.5f; ArcMode mode = ArcMode::Pie; float cx = 0.0f; float cy = 0.0f; float rx = 0.0f; float ry = 0.0f; };
struct BlockArrowParams { float headLengthRatio = 0.35f; float shaftWidthRatio = 0.5f; };
struct CalloutParams    { float tailPosRatio = 0.3f; float tailWidthRatio = 0.16f; float tailHeightRatio = 0.25f; float cornerRadius = 8.0f; };

using ShapeParams = std::variant<std::monostate, RectParams, TriangleParams, EllipseParams,
                                 PolygonParams, StarParams, GearParams, SpiralParams, HeartParams,
                                 ArcParams, BlockArrowParams, CalloutParams>;

using NodeId = uint64_t;

// 히트테스트/노드 편집에서 공용으로 쓰는 부위 종류
enum class HitPart { None, Fill, Stroke, Anchor, HandleIn, HandleOut };

// 그림자 (Step 4.5.17): 모든 노드가 공유하는 외형 속성. 단위는 문서 px
struct Shadow {
	bool  enabled = false;
	float x = 0.0f, y = 4.0f;
	float blur = 8.0f, spread = 0.0f;
	Color color = Color(0.0f, 0.0f, 0.0f, 0.25f);
	bool  inset = false;       // 안쪽 그림자 (CSS box-shadow: inset). 글자/벡터 filter 에서는 무시
	bool operator==(const Shadow& o) const {
		return enabled == o.enabled && inset == o.inset && x == o.x && y == o.y && blur == o.blur && spread == o.spread &&
			color.r == o.color.r && color.g == o.color.g && color.b == o.color.b && color.a == o.color.a;
	}
	bool operator!=(const Shadow& o) const { return !(*this == o); }
};

class Node;

// ============================================================================
// 4. HitResult
struct HitResult {
	using Part = HitPart;
	Part  part    = Part::None;
	Node* node    = nullptr;
	int   subPath = -1;
	int   nodeIdx = -1;

	HitResult() = default;
	HitResult(Part p, int sp = -1, int ni = -1, Node* n = nullptr)
		: part(p), node(n), subPath(sp), nodeIdx(ni) {}
	HitResult(const HitResult& o)
		: part(o.part), node(o.node), subPath(o.subPath), nodeIdx(o.nodeIdx) {}
	HitResult& operator=(const HitResult& o) {
		part = o.part; node = o.node; subPath = o.subPath; nodeIdx = o.nodeIdx;
		return *this;
	}
};

class Document;
class INodeVisitor;

// 모델 변경 카운터: 직렬화 결과에 영향을 주는 모든 수정(setter, 비const 접근자, 계층/순서 변경)이 올린다.
// UndoManager::CommitChange 는 카운터가 기준 시점과 같으면 문서 직렬화를 통째로 건너뜀 (큰 문서/이미지 대비).
// 문서 구분 없는 프로세스 전역 값이므로 다른 문서의 편집으로 올라가도 "불필요한 직렬화"만 생길 뿐 안전함.
// 새 수정 함수를 추가할 때는 반드시 Bump() 를 호출할 것 (누락하면 Undo 가 그 변경을 놓침).
struct ChangeTracker {
	static uint64_t& Counter() { static uint64_t c = 0; return c; }
	static void Bump()         { ++Counter(); }
	static uint64_t Value()    { return Counter(); }
};

// ---- 모든 씬 노드의 기반 클래스 ---------------------------------------------
class GroupNode;

class Node {
public:
	virtual ~Node() = default;
	virtual NodeKind Kind() const = 0;
	virtual std::unique_ptr<Node> Clone() const = 0;
	virtual void Accept(INodeVisitor& visitor) = 0;

	// 로컬 바운딩 박스 (자신의 m_xf 적용 전)
	virtual bool GetLocalBounds(Rect2& out) = 0;
#ifndef GE_NO_D2D
	// 비가상: D2D 전용 편의 오버로드가 가상 함수 테이블 모양을 바꾸면 GE_NO_D2D 로 컴파일한 TU 와 앱 본체의 ABI 가 어긋남
	bool GetLocalBounds(ID2D1Factory* /*f*/, D2D1_RECT_F& outBounds) {
		Rect2 r;
		if (GetLocalBounds(r)) {
			outBounds = r.ToD2D();
			return true;
		}
		return false;
	}
#endif

	// 계층 구조
	NodeId            GetId() const          { return m_id; }
	GroupNode*        GetParent() const      { return m_parent; }

	// getter / setter
	const std::wstring& GetName() const      { return m_name; }
	void SetName(const std::wstring& v)      { m_name = v; ChangeTracker::Bump(); }

	const Matrix2D& GetTransform() const     { return m_xf; }
	// 변환은 로컬 지오메트리와 무관하므로 지오메트리 캐시를 무효화하지 않음 (변경 카운터는 올림)
	void SetTransform(const Matrix2D& v)     { m_xf = v; ChangeTracker::Bump(); }

	const Style& GetStyle() const            { return m_style; }
	// 수정용 접근자: 호출 시 지오메트리 캐시 무효화 (fill.type/fillMode 가 지오메트리에 영향). 읽기는 GetStyle() 사용
	Style&       GetStyleRef()               { Invalidate(); return m_style; }
	void SetStyle(const Style& v)            { m_style = v; Invalidate(); }

	bool IsVisible() const                   { return m_visible; }
	void SetVisible(bool v)                  { m_visible = v; ChangeTracker::Bump(); }
	bool IsLocked() const                    { return m_locked; }
	void SetLocked(bool v)                   { m_locked = v; ChangeTracker::Bump(); }
	float GetOpacity() const                 { return m_opacity; }   // 0.0 ~ 1.0
	void SetOpacity(float v)                 { m_opacity = (std::max)(0.0f, (std::min)(1.0f, v)); ChangeTracker::Bump(); }
	LayerBlendMode GetBlendMode() const      { return m_blendMode; }
	void SetBlendMode(LayerBlendMode m)      { m_blendMode = m; ChangeTracker::Bump(); }

	// 자신 + 조상(그룹/레이어) 체인까지 고려한 최종 가시성/잠금
	// - 가시성(IsEffectivelyVisible): 부모가 숨김이면 자식도 화면에서 숨김
	// - 잠금(IsEffectivelyLocked): 잠금은 해당 객체 자체의 변형/선택만 고정함.
	//   단, 버튼/UI 컴포넌트(IsAtomicComponent)와 일반 그룹(Ctrl+G)은 하나의 덩어리로 동작하므로 조상 중 해당 단위가 잠겨 있으면 함께 잠김.
	//   반면 레이어(LayerNode)나 일반 프레임(FrameNode)이 잠겨 있어도 잠기지 않은 내부 자식은 자유롭게 선택/편집 가능.
	bool IsEffectivelyVisible() const;
	bool IsEffectivelyLocked() const;

	void Invalidate()                        { m_dirty = true; m_version = NextVersion(); ChangeTracker::Bump(); }
	bool IsDirty() const                     { return m_dirty; }

	// 부모 체인을 곱한 월드 변환 (자신 포함)
	Matrix2D GetWorldTransform() const;

	// 부모 불투명도까지 곱한 최종 불투명도
	float GetEffectiveOpacity() const;

	// 변경 횟수 (렌더러 지오메트리 캐시 무효화 판단용)
	uint32_t GetVersion() const { return m_version; }

	// 웹/HTML 시맨틱 속성 (Step 4.5)
	const std::string& GetHtmlTag() const        { return m_htmlTag; }
	void SetHtmlTag(const std::string& tag)      { m_htmlTag = tag; Invalidate(); }
	const std::string& GetHref() const           { return m_href; }
	void SetHref(const std::string& h)           { m_href = h; Invalidate(); }
	const std::string& GetAlt() const            { return m_alt; }
	void SetAlt(const std::string& a)            { m_alt = a; Invalidate(); }
	const std::string& GetAriaLabel() const      { return m_ariaLabel; }
	void SetAriaLabel(const std::string& l)      { m_ariaLabel = l; Invalidate(); }
	const std::string& GetCustomClass() const    { return m_customClass; }
	void SetCustomClass(const std::string& c)    { m_customClass = c; Invalidate(); }

	// 부모가 자유 배치 프레임일 때 부모 크기 변화에 따르는 방식 (4.5.11)
	ConstraintH GetConstraintH() const           { return m_constraintH; }
	void SetConstraintH(ConstraintH c)           { m_constraintH = c; ChangeTracker::Bump(); }
	ConstraintV GetConstraintV() const           { return m_constraintV; }
	void SetConstraintV(ConstraintV c)           { m_constraintV = c; ChangeTracker::Bump(); }

	// 공통 외형: 그림자 (4.5.17)
	const Shadow& GetShadow() const              { return m_shadow; }
	void SetShadow(const Shadow& s) {
		m_shadow = s;
		m_shadow.blur = (std::max)(0.0f, s.blur);   // spread 는 음수 허용(그림자 축소)
		ChangeTracker::Bump();
	}

	virtual const wchar_t* GetShapeTypeName() const { return L"쉐이프"; }

protected:
	// Clone 규약: 복제본은 문서 식별 정보(id/parent/version)를 갖지 않음 -> 문서 삽입 시 AssignIds 로 새 ID 발급
	void CopyNodePropsFrom(const Node& o) {
		m_name = o.m_name; m_xf = o.m_xf; m_style = o.m_style;
		m_visible = o.m_visible; m_locked = o.m_locked; m_opacity = o.m_opacity;
		m_blendMode = o.m_blendMode;
		m_htmlTag = o.m_htmlTag; m_href = o.m_href; m_alt = o.m_alt;
		m_ariaLabel = o.m_ariaLabel; m_customClass = o.m_customClass;
		m_constraintH = o.m_constraintH; m_constraintV = o.m_constraintV;
		m_shadow = o.m_shadow;
	}
	void ResetIdentity() { m_id = 0; m_parent = nullptr; m_version = NextVersion(); m_dirty = true; }

	// 버전은 프로세스 전역에서 겹치지 않는 값 (같은 NodeId 를 가진 다른 노드 - 예: Undo 복원으로 재생성된 노드 -
	// 가 렌더러 지오메트리 캐시의 오래된 항목과 우연히 일치하는 것을 방지)
	static uint32_t NextVersion() { static uint32_t s_counter = 0; return ++s_counter; }

	uint32_t     m_version = NextVersion();
	friend class GroupNode;
	friend class Document;
	NodeId       m_id = 0;                 // Document 가 발급
	GroupNode*   m_parent = nullptr;       // 비소유 포인터
	std::wstring m_name;
	Matrix2D     m_xf;                     // 로컬 변환 (이동/회전/스케일)
	Style        m_style;
	bool         m_visible = true;
	bool         m_locked  = false;
	float        m_opacity = 1.0f;
	LayerBlendMode m_blendMode = LayerBlendMode::Normal;
	std::string  m_htmlTag;                // e.g. "header", "nav", "section", "button", "h1", "p"
	std::string  m_href;                   // 링크 주소
	std::string  m_alt;                    // 대체 텍스트
	std::string  m_ariaLabel;              // 접근성 라벨
	std::string  m_customClass;            // 사용자 정의 CSS 클래스
	ConstraintH  m_constraintH = ConstraintH::Left;
	ConstraintV  m_constraintV = ConstraintV::Top;
	Shadow       m_shadow;
	bool         m_dirty   = true;
};

// ---- 텍스트 (Step 3.1) -------------------------------------------------------
// 로컬 원점 = 텍스트 상자의 (정렬 기준점 x, 위쪽 y). 글자는 원점에서 아래로 배치된다 (Left: 원점이 왼쪽 끝, Center: 가운데, Right: 오른쪽 끝).
// 채우기 색은 Style.fill 을 사용한다. 크기(폭/높이)는 렌더러가 DirectWrite 로 재서 캐시하고, 그 전에는 추정치를 쓴다.
enum class TextAlign : uint8_t { Left, Center, Right };

class TextNode : public Node {
public:
	// SVG <text> 의 y 는 기준선: 위쪽 y 와의 차이를 글꼴 크기 비율로 근사 (읽기/쓰기가 같은 값을 써서 왕복 시 위치 보존)
	static constexpr float kAscentRatio = 0.8f;
	static constexpr float kLineHeightRatio = 1.25f;

	TextNode() { m_style.fill.type = PaintType::Solid; m_style.fill.color = Color(0.0f, 0.0f, 0.0f, 1.0f); m_style.stroke.width = 0.0f; }

	NodeKind Kind() const override { return NodeKind::Text; }
	std::unique_ptr<Node> Clone() const override;
	void Accept(INodeVisitor& visitor) override;
	bool GetLocalBounds(Rect2& out) override;
	using Node::GetLocalBounds;
	const wchar_t* GetShapeTypeName() const override { return L"텍스트"; }

	const std::wstring& GetText() const       { return m_text; }
	void SetText(const std::wstring& v)       { m_text = v; m_mw = m_mh = 0.0f; Invalidate(); }
	const std::wstring& GetFontFamily() const { return m_family; }
	void SetFontFamily(const std::wstring& v) { m_family = v; m_mw = m_mh = 0.0f; Invalidate(); }
	float GetFontSize() const                 { return m_size; }
	void SetFontSize(float v)                 { m_size = (std::max)(1.0f, v); m_mw = m_mh = 0.0f; Invalidate(); }
	bool IsBold() const                       { return m_bold; }
	void SetBold(bool v)                      { m_bold = v; m_mw = m_mh = 0.0f; Invalidate(); }
	bool IsItalic() const                     { return m_italic; }
	void SetItalic(bool v)                    { m_italic = v; m_mw = m_mh = 0.0f; Invalidate(); }
	TextAlign GetAlign() const                { return m_align; }
	void SetAlign(TextAlign v)                { m_align = v; Invalidate(); }
	// 영역 텍스트(Area Text): 폭이 정해진 상자 안에서 자동 줄바꿈. 0 이면 점 텍스트(Point Text). 영역 텍스트의 로컬 원점은 상자의 왼쪽 위,
	// 정렬(왼쪽/가운데/오른쪽)은 상자 폭 안에서 적용된다.
	float GetBoxWidth() const                 { return m_boxW; }
	bool  IsAreaText() const                  { return m_boxW > 0.0f; }
	void  SetBoxWidth(float v)                { m_boxW = v > 1.0f ? v : 0.0f; m_mw = m_mh = 0.0f; Invalidate(); }

	// 렌더러가 잰 크기 (변경 카운터를 올리지 않음: 화면 갱신의 부산물)
	void SetMeasured(float w, float h)        { m_mw = w; m_mh = h; }
	float MeasuredWidth() const;
	float MeasuredHeight() const;
	int   LineCount() const;

private:
	std::wstring m_text = L"텍스트";
	std::wstring m_family = L"Segoe UI";
	float        m_size = 24.0f;
	bool         m_bold = false;
	bool         m_italic = false;
	TextAlign    m_align = TextAlign::Left;
	float        m_boxW = 0.0f;
	float        m_mw = 0.0f, m_mh = 0.0f;   // 측정값 (0 = 아직 안 잼)
};

// ---- 이미지 (Step 3.4) ---------------------------------------------------------
// 원본 파일 바이트(PNG/JPEG/GIF/BMP)를 그대로 보관한다. 같은 ImageData 를 가리키는 노드(복제본)는 바이트를 공유한다.
// 계획서의 "문서 리소스 테이블" 대신 노드가 shared_ptr 로 직접 보관하는 단순한 구조 (SVG 에는 data URI 로 임베드되므로 문서 단위 테이블이 필요 없음)
struct ImageData {
	std::vector<uint8_t> bytes;
	std::string          mime = "image/png";
	int                  pixelW = 0, pixelH = 0;   // 원본 픽셀 크기 (모르면 0)
};

// 맞춤 방식 (CSS object-fit / SVG preserveAspectRatio): Stretch = none, Fit = xMidYMid meet (전체가 보이게), Fill = xMidYMid slice (상자를 가득 채우고 넘치는 부분은 잘림)
enum class ImageFit : uint8_t { Stretch, Fit, Fill };

// 로컬 좌표 (0,0)~(W,H) 상자에 이미지를 맞춰 그림
class ImageNode : public Node {
public:
	ImageNode() { m_style.fill.type = PaintType::None; m_style.stroke.width = 0.0f; }

	NodeKind Kind() const override { return NodeKind::Image; }
	std::unique_ptr<Node> Clone() const override;
	void Accept(INodeVisitor& visitor) override;
	bool GetLocalBounds(Rect2& out) override;
	using Node::GetLocalBounds;
	const wchar_t* GetShapeTypeName() const override { return L"이미지"; }

	const std::shared_ptr<const ImageData>& GetData() const { return m_data; }
	void SetData(std::shared_ptr<const ImageData> d) { m_data = std::move(d); Invalidate(); }
	float GetWidth() const  { return m_w; }
	float GetHeight() const { return m_h; }
	void  SetSize(float w, float h) { m_w = (std::max)(1.0f, w); m_h = (std::max)(1.0f, h); Invalidate(); }
	ImageFit GetFit() const { return m_fit; }
	void  SetFit(ImageFit f) { m_fit = f; Invalidate(); }

private:
	ImageFit m_fit = ImageFit::Stretch;
	std::shared_ptr<const ImageData> m_data;
	float m_w = 100.0f, m_h = 100.0f;
};

// ---- 패스 도형 --------------------------------------------------------------
class PathNode : public Node {
public:
	NodeKind Kind() const override { return NodeKind::Path; }
	std::unique_ptr<Node> Clone() const override;
	void Accept(INodeVisitor& visitor) override;
	bool GetLocalBounds(Rect2& out) override;
	using Node::GetLocalBounds;

	const std::vector<SubPath>& GetPaths() const { return m_paths; }
	// 수정용 접근자: 호출 시 지오메트리 캐시 무효화. 읽기는 GetPaths() 사용
	std::vector<SubPath>&       GetPathsRef()    { Invalidate(); return m_paths; }
	void SetPaths(const std::vector<SubPath>& v) { m_paths = v; Invalidate(); }
	void AddAnchor(Vec2 localPt, bool forceNewSubPath = false);
#ifndef GE_NO_D2D
	void AddAnchor(D2D1_POINT_2F localPt, bool forceNewSubPath = false) {
		AddAnchor(Vec2{ localPt.x, localPt.y }, forceNewSubPath);
	}
#endif

	// 파라메트릭 도형 종류 및 설정
	ShapeKind GetShapeKind() const { return m_kind; }
	ShapeKind GetKind() const      { return m_kind; } // 하위 호환용
	void SetKind(ShapeKind k)      { m_kind = k; ChangeTracker::Bump(); }
	void SetShapeKind(ShapeKind k) { m_kind = k; ChangeTracker::Bump(); }

	const ShapeParams& GetParams() const { return m_params; }
	// 수정용 접근자: 호출 시 변경 카운터를 올림 (수정 여부를 알 수 없으므로 보수적으로). 읽기는 GetParams()/GetParam() 사용
	ShapeParams&       GetParamsRef()    { ChangeTracker::Bump(); return m_params; }
	void SetParams(const ShapeParams& p) { m_params = p; ChangeTracker::Bump(); }

	template<typename T>
	const T* GetParam() const { return std::get_if<T>(&m_params); }

	template<typename T>
	T* GetParamRef() { ChangeTracker::Bump(); return std::get_if<T>(&m_params); }

	// ---- 노드 편집 API ----
	// 앵커/핸들 이동 (worldPt 는 월드 좌표)
	void MoveNodeOrHandle(HitPart part, int subPathIdx, int nodeIdx, Vec2 worldPt, bool breakTangent = false);
#ifndef GE_NO_D2D
	void MoveNodeOrHandle(HitPart part, int subPathIdx, int nodeIdx, D2D1_POINT_2F worldPt, bool breakTangent = false) {
		MoveNodeOrHandle(part, subPathIdx, nodeIdx, Vec2(worldPt.x, worldPt.y), breakTangent);
	}

	HitResult HitTestScreenSpace(D2D1_POINT_2F screenPt, float tolPx, const Viewport& view, ID2D1Factory* f);
#endif
	// 세그먼트를 t(0~1) 위치에서 분할(De Casteljau)해 노드 삽입. 새 노드 인덱스 반환, 실패 시 -1
	int  InsertNodeOnSegment(int subPathIdx, int segIdx, float t);
	// 노드 삭제 (서브패스가 비면 서브패스도 삭제)
	bool RemoveNode(int subPathIdx, int nodeIdx);
	// 로컬 좌표에서 가장 가까운 세그먼트 검색 (노드 추가 툴용)
	bool FindClosestSegment(Vec2 localPt, int& spIdx, int& segIdx, float& t, float& distSq) const;
	// 세그먼트 위의 t 지점 좌표
	static Vec2 EvalSegment(const BezierNode& a, const BezierNode& b, float t);

	// Step 6.13: 파라메트릭 2D 벡터 도형 서브패스 빌더 (순수 CPU 기하학)
	static void BuildArcSubPaths(const Rect2& box, const ArcParams& p, std::vector<SubPath>& out);
	static void BuildBlockArrowSubPaths(const Rect2& box, const BlockArrowParams& p, std::vector<SubPath>& out);
	static void BuildCalloutSubPaths(const Rect2& box, const CalloutParams& p, std::vector<SubPath>& out);

private:
	std::vector<SubPath> m_paths;
	ShapeKind            m_kind = ShapeKind::Path;
	ShapeParams          m_params;
	// Step 6.8: 로컬 타이트 바운딩 박스 캐시 (m_version 과 동기화, ABI 호환을 위해 항상 포함)
	mutable Rect2        m_cachedBounds{};
	mutable uint32_t     m_boundsVersion = 0;
	mutable bool         m_boundsValid = false;
};

// ---- 그룹 (컴포지트) --------------------------------------------------------
class GroupNode : public Node {
public:
	NodeKind Kind() const override { return NodeKind::Group; }
	std::unique_ptr<Node> Clone() const override;
	void Accept(INodeVisitor& visitor) override;
	bool GetLocalBounds(Rect2& out) override;

	const std::vector<std::unique_ptr<Node>>& GetChildren() const { return m_children; }

	Node* AddChild(std::unique_ptr<Node> child, int index = -1);   // -1 = 최상단
	std::unique_ptr<Node> RemoveChild(Node* child);               // 소유권 반환
	int   IndexOf(const Node* child) const;

	// Z-Order (자식 목록 내부)
	void BringToFront(Node* child);
	void SendToBack(Node* child);
	void BringForward(Node* child);
	void SendBackward(Node* child);

	// 클리핑 마스크 (SVG <clipPath> / D2D geometricMask)
	bool  IsClippingMask() const noexcept { return m_bClippingMask; }
	void  SetClippingMask(bool bClip) noexcept { m_bClippingMask = bClip; }
	Node* GetMaskNode() const noexcept {
		// 마스크 노드는 관례상 자식 중 최상단(맨 뒤 인덱스) 도형
		return (m_bClippingMask && !m_children.empty()) ? m_children.back().get() : nullptr;
	}

protected:
	std::vector<std::unique_ptr<Node>> m_children;   // 0 = 최하단
	bool m_bClippingMask = false;
};

// ---- 프레임 / 아트보드 (Step 4.1~4.2) ---------------------------------------------
// 그룹의 한 종류: 로컬 (0,0)~(W,H) 상자에 배경(Style.fill)/테두리(Style.stroke)/모서리 반경을 가지고 자식을 그 상자로 클리핑한다.
// Kind() 는 Group 이라서 기존의 그룹 처리(선택, 순서, 패널, Undo)가 그대로 동작한다. 프레임 여부는 dynamic_cast<FrameNode*> 로 구분.
// (자동 레이아웃(Row/Column)은 4.3)
enum class FrameLayoutMode : uint8_t {
	None = 0,
	Row = 1,
	Column = 2
};

enum class LayoutMainAlign : uint8_t {
	Start = 0,
	Center = 1,
	End = 2,
	SpaceBetween = 3
};

enum class LayoutCrossAlign : uint8_t {
	Start = 0,
	Center = 1,
	End = 2
};

enum class FrameSizingMode : uint8_t {
	Fixed = 0,
	Hug = 1,
	Fill = 2    // 부모 자동 배치 프레임의 가용 공간을 채움 (Step 4.5.9)
};

// 4방향 값 (Step 4.5.10)
struct PaddingEdges {
	float top = 10.0f, right = 10.0f, bottom = 10.0f, left = 10.0f;
	bool IsUniform() const { return top == right && right == bottom && bottom == left; }
};
struct CornerRadii {
	float tl = 0.0f, tr = 0.0f, br = 0.0f, bl = 0.0f;
	bool IsUniform() const { return tl == tr && tr == br && br == bl; }
	bool IsZero() const { return tl == 0.0f && tr == 0.0f && br == 0.0f && bl == 0.0f; }
};
// 테두리를 그릴 변 (비트 플래그)
enum BorderSide : uint8_t { BorderTop = 1, BorderRight = 2, BorderBottom = 4, BorderLeft = 8, BorderAll = 15 };

class FrameNode : public GroupNode {
public:
	FrameNode() { m_style.fill.type = PaintType::Solid; m_style.fill.color = Color(1.0f, 1.0f, 1.0f, 1.0f); m_style.stroke.width = 0.0f; }

	std::unique_ptr<Node> Clone() const override;
	bool GetLocalBounds(Rect2& out) override { out = Rect2{ 0.0f, 0.0f, m_w, m_h }; return true; }
	using Node::GetLocalBounds;
	const wchar_t* GetShapeTypeName() const override {
		if (GetHtmlTag() == "button") return L"버튼";
		return L"프레임";
	}

	float GetWidth() const  { return m_w; }
	float GetHeight() const { return m_h; }
	void  SetFrameSize(float w, float h) { m_w = (std::max)(1.0f, w); m_h = (std::max)(1.0f, h); Invalidate(); }
	bool  IsClip() const    { return m_clip; }
	void  SetClip(bool v)   { m_clip = v; Invalidate(); }
	// 모서리: 4개가 같으면 단일 값. GetCornerRadius()는 대표값(좌상), SetCornerRadius()는 4개 모두 설정 (기존 호환)
	float GetCornerRadius() const { return m_radii.tl; }
	void  SetCornerRadius(float v) { v = (std::max)(0.0f, v); m_radii = CornerRadii{ v, v, v, v }; Invalidate(); }
	const CornerRadii& GetCornerRadii() const { return m_radii; }
	void  SetCornerRadii(const CornerRadii& r) {
		m_radii = CornerRadii{ (std::max)(0.0f, r.tl), (std::max)(0.0f, r.tr), (std::max)(0.0f, r.br), (std::max)(0.0f, r.bl) };
		Invalidate();
	}
	// 변 길이에 맞게 줄인 반경 (이웃한 두 모서리 합이 변 길이를 넘지 않도록)
	CornerRadii ClampedRadii() const;
	uint8_t GetBorderSides() const { return m_borderSides; }
	void  SetBorderSides(uint8_t s) { m_borderSides = static_cast<uint8_t>(s & BorderAll); Invalidate(); }

	FrameLayoutMode GetLayoutMode() const { return m_layoutMode; }
	void SetLayoutMode(FrameLayoutMode m) { m_layoutMode = m; Invalidate(); }
	float GetGap() const { return m_gap; }
	void SetGap(float g) { m_gap = (std::max)(0.0f, g); Invalidate(); }
	// 여백: GetPadding()은 대표값(위), SetPadding()은 4방향 모두 설정 (기존 호환)
	float GetPadding() const { return m_pad.top; }
	void SetPadding(float p) { p = (std::max)(0.0f, p); m_pad = PaddingEdges{ p, p, p, p }; Invalidate(); }
	const PaddingEdges& GetPaddingEdges() const { return m_pad; }
	void SetPaddingEdges(const PaddingEdges& e) {
		m_pad = PaddingEdges{ (std::max)(0.0f, e.top), (std::max)(0.0f, e.right), (std::max)(0.0f, e.bottom), (std::max)(0.0f, e.left) };
		Invalidate();
	}

	LayoutMainAlign GetMainAlign() const { return m_mainAlign; }
	void SetMainAlign(LayoutMainAlign a) { m_mainAlign = a; Invalidate(); }
	LayoutCrossAlign GetCrossAlign() const { return m_crossAlign; }
	void SetCrossAlign(LayoutCrossAlign a) { m_crossAlign = a; Invalidate(); }
	FrameSizingMode GetSizingW() const { return m_sizingW; }
	void SetSizingW(FrameSizingMode s) { m_sizingW = s; Invalidate(); }
	FrameSizingMode GetSizingH() const { return m_sizingH; }
	void SetSizingH(FrameSizingMode s) { m_sizingH = s; Invalidate(); }

	// 자동 레이아웃(Row/Column): 자식을 z-순서대로 안쪽 여백(padding) 안에서 gap 간격과 정렬 규칙에 따라 배치.
	// SizingMode 가 Hug 인 축은 자식 총합에 맞춰 프레임 크기를 자동 조정. 모드가 None 이면 아무것도 하지 않음. 변경이 있었으면 true
	bool ApplyLayout();

	// 프레임 크기를 바꾼다. 자유 배치(None)면 자식이 자신의 제약(ConstraintH/V)에 따라 위치/크기가 보정되고,
	// 자동 배치(Row/Column)면 다시 배치한다 (4.5.11)
	void ResizeFrame(float w, float h);

	// ---- 표준 HTML UI 컴포넌트 (4.5.17) ----
	// 컴포넌트 프레임 = 종류(uiKind) + 속성(uiProps) + 속성으로 만든 자식 도형들. 자식은 Rebuild 가 속성에서 다시 만든다.
	// 속성은 SVG(m2:ui / m2:uiProps)로 저장되고, HTML 내보내기는 이 속성으로 의미 있는 태그를 만든다.
	using UiProps = std::vector<std::pair<std::string, std::string>>;
	bool IsUiComponent() const { return !m_uiKind.empty(); }
	const std::string& GetUiKind() const { return m_uiKind; }
	void SetUiKind(const std::string& k) { m_uiKind = k; }
	const UiProps& GetUiProps() const { return m_uiProps; }
	void SetUiProps(const UiProps& p) { m_uiProps = p; }
	std::string GetUiProp(const std::string& key, const std::string& def = std::string()) const {
		for (const auto& kv : m_uiProps) if (kv.first == key) return kv.second;
		return def;
	}
	void SetUiPropRaw(const std::string& key, const std::string& value) {
		for (auto& kv : m_uiProps) if (kv.first == key) { kv.second = value; return; }
		m_uiProps.emplace_back(key, value);
	}
	// 크기가 바뀐 컴포넌트 프레임의 자식을 다시 만드는 함수 (UiComponents.cpp 가 등록)
	static void (*s_uiRebuildHook)(FrameNode&);

	// 자식이 부모 프레임 범위(0,0,w,h)를 벗어나지 못하게 한다 (자유 배치 프레임만; 자동 배치는 레이아웃이 결정).
	// shrink=false: 위치를 안으로 옮기고 부모보다 큰 자식은 부모 크기로 줄임 (이동/생성/부모 축소)
	// shrink=true : 위치는 두고 넘친 쪽만 잘라 크기를 줄임 (크기 조절 드래그: 반대쪽 모서리 고정)
	// 글자/표는 줄이지 않고 위치만 보정. 변경이 있었으면 true
	bool ClampChild(Node* child, bool shrink = false);
	void ClampChildren();
	// child 의 부모가 프레임이면 ClampChild 호출 (아니면 false)
	static bool ClampNode(Node* child, bool shrink = false);

private:
	float m_w = 100.0f, m_h = 100.0f;
	bool  m_clip = true;
	CornerRadii m_radii;
	uint8_t m_borderSides = BorderAll;
	FrameLayoutMode m_layoutMode = FrameLayoutMode::None;
	float m_gap = 10.0f;
	PaddingEdges m_pad;
	LayoutMainAlign m_mainAlign = LayoutMainAlign::Start;
	LayoutCrossAlign m_crossAlign = LayoutCrossAlign::Start;
	FrameSizingMode m_sizingW = FrameSizingMode::Fixed;
	FrameSizingMode m_sizingH = FrameSizingMode::Fixed;
	std::string m_uiKind;
	UiProps m_uiProps;
};


// ---- HTML 컴포넌트: 테이블 (Step 4.5.7.1) ----------------------------------------
// GroupNode 상속 (FrameNode 가 아님: 프레임은 자동 배치 컨테이너라 표의 행/열 의미와 맞지 않음).
// 자식 노드 = 칸 내용. 자식의 로컬 원점은 표 기준 좌표이며, 칸 안에 두면 그 칸의 내용이 된다.
// 이 클래스는 격자 모델만 가진다. 그리기는 D2DTableRender.cpp, HTML 출력은 HtmlExporter 가 담당.
// 칸 병합/헤더는 4.5.7.5에서 추가한다.
class TableNode : public GroupNode {
public:
	TableNode() { m_style.fill.type = PaintType::None; m_style.stroke.width = 0.0f; }

	std::unique_ptr<Node> Clone() const override;
	bool GetLocalBounds(Rect2& out) override;
	using Node::GetLocalBounds;
	const wchar_t* GetShapeTypeName() const override { return L"테이블"; }

	// 격자 전체를 같은 크기 칸으로 다시 만든다 (행/열은 1~50 으로 제한)
	void SetGrid(int rows, int cols, float cellW, float cellH);
	// 열 너비/행 높이 목록을 한 번에 설정 (저장 파일 복원용). 비어 있으면 무시
	void SetSizes(const std::vector<float>& colW, const std::vector<float>& rowH);

	int  Rows() const { return static_cast<int>(m_rowH.size()); }
	int  Cols() const { return static_cast<int>(m_colW.size()); }
	const std::vector<float>& ColWidths() const { return m_colW; }
	const std::vector<float>& RowHeights() const { return m_rowH; }
	void SetColWidth(int c, float w);
	void SetRowHeight(int r, float h);

	// 테두리 종류: 0=실선 1=파선 2=점선 (4.5.18.4)
	int   GetBorderStyle() const { return m_borderStyle; }
	void  SetBorderStyle(int s) { m_borderStyle = (std::max)(0, (std::min)(2, s)); Invalidate(); }
	float GetBorderWidth() const { return m_border; }
	void  SetBorderWidth(float v) { m_border = (std::max)(0.0f, v); Invalidate(); }
	Color GetBorderColor() const { return m_borderColor; }
	void  SetBorderColor(const Color& c) { m_borderColor = c; Invalidate(); }
	float GetCellPadding() const { return m_pad; }
	void  SetCellPadding(float v) { m_pad = (std::max)(0.0f, v); Invalidate(); }

	// 칸의 로컬 사각형 (표 기준). 인덱스가 범위를 벗어나면 빈 사각형
	Rect2 CellRect(int r, int c) const;
	// 로컬 점이 속한 칸. 병합된 칸이면 병합 영역의 왼쪽 위 칸(앵커)을 돌려준다. 표 밖이면 false
	bool  CellAt(Vec2 local, int& r, int& c) const;

	// ---- 병합 / 머리글 / 정렬 (4.5.7.2) ----
	struct Span { int r = 0, c = 0, rs = 1, cs = 1; };
	const std::vector<Span>& Merges() const { return m_merges; }
	void SetMerges(const std::vector<Span>& v);
	// (r0,c0)~(r1,c1) 범위를 한 칸으로 병합. 범위 밖/1칸/기존 병합과 겹치면 false
	bool MergeRange(int r0, int c0, int r1, int c1);
	// (r,c) 를 포함하는 병합을 해제. 병합이 없으면 false
	bool SplitAt(int r, int c);
	// (r,c) 를 포함하는 병합 정보 (없으면 자기 자신 1x1)
	Span SpanOf(int r, int c) const;
	// 병합된 칸이 차지하는 전체 로컬 사각형 (앵커 기준)
	Rect2 SpanRect(int r, int c) const;
	// 안쪽 격자선 조각 (병합 칸 내부 선 제외). 바깥 테두리는 포함하지 않는다
	struct Seg { float x0, y0, x1, y1; };
	void GridSegments(std::vector<Seg>& out) const;

	// 행/열 구조 편집 (병합 정보를 함께 갱신. 자식 노드 이동은 TableComponent 가 담당)
	bool InsertRowAt(int at, float h);
	bool RemoveRowAt(int at);
	bool InsertColAt(int at, float w);
	bool RemoveColAt(int at);

	int   GetHeaderRows() const { return m_headerRows; }
	void  SetHeaderRows(int n) { m_headerRows = (std::max)(0, (std::min)(n, Rows())); Invalidate(); }
	int   GetCellAlign() const { return m_cellAlign; }          // 0=왼쪽 1=가운데 2=오른쪽 (내보내기 text-align)
	void  SetCellAlign(int a) { m_cellAlign = (std::max)(0, (std::min)(2, a)); Invalidate(); }

	// 편집 중인 칸 (저장하지 않는 임시 상태. -1 = 없음)
	void  SetActiveCell(int r, int c) { m_actR = r; m_actC = c; }
	bool  GetActiveCell(int& r, int& c) const { r = m_actR; c = m_actC; return m_actR >= 0 && m_actR < Rows() && m_actC >= 0 && m_actC < Cols(); }

private:
	std::vector<float> m_colW, m_rowH;
	std::vector<Span>  m_merges;
	int   m_headerRows = 0;
	int   m_cellAlign = 0;
	int   m_borderStyle = 0;
	int   m_actR = -1, m_actC = -1;
	float m_border = 1.0f;
	float m_pad = 6.0f;
	Color m_borderColor = Color(0.35f, 0.38f, 0.45f, 1.0f);
};

// ---- 컴포넌트 판별 정책 (Step 6.2) ---------------------------------------------
// 컴포넌트의 정체성은 이름(사용자가 바꿀 수 있음)이 아니라 역할 태그/종류 속성으로 정한다.
// 선택·히트·스마트 가이드·레이어 패널·속성창은 이 쿼리만 사용한다 (이름/태그 문자열 비교를 흩뿌리지 않음).
enum class ComponentKind : uint8_t { None, Frame, Button, Table, UiComponent };

inline ComponentKind GetComponentKind(const Node* n)
{
	if (!n) return ComponentKind::None;
	if (dynamic_cast<const TableNode*>(n)) return ComponentKind::Table;
	if (const auto* f = dynamic_cast<const FrameNode*>(n)) {
		if (f->IsUiComponent()) return ComponentKind::UiComponent;
		if (f->GetHtmlTag() == "button") return ComponentKind::Button;
		return ComponentKind::Frame;
	}
	return ComponentKind::None;
}

inline bool IsButtonComponent(const Node* n) { return GetComponentKind(n) == ComponentKind::Button; }

// 하나의 오브젝트로 선택/이동/크기 조절되는 컴포넌트인가 (버튼, 표준 UI 컴포넌트). 자식은 따로 선택되지 않는다.
inline bool IsAtomicComponent(const Node* n)
{
	const ComponentKind k = GetComponentKind(n);
	return k == ComponentKind::Button || k == ComponentKind::UiComponent;
}

// 자식을 개별 선택할 수 있는 컨테이너인가 (일반 프레임). 표는 칸 편집용이라 제외
inline bool AllowsChildSelection(const Node* n) { return GetComponentKind(n) == ComponentKind::Frame; }

} // namespace ge

#include "D2DLayer.h"

namespace ge {

enum class CanvasOriginMode : uint8_t {
	TopLeft = 0,    // 웹/화면: 좌상단 (0,0), Y축 아래로 증가 (화면 표준)
	BottomLeft = 1  // 문서/인쇄: 좌하단 (0,0), Y축 위로 증가 (CAD/수학/인쇄 표준)
};

// ---- 문서 (Scene 루트) ------------------------------------------------------
class Document {
public:
	Document();
	void Clear(bool addDefaultLayer = false);

	// 레이어
	const std::vector<std::unique_ptr<LayerNode>>& GetLayers() const { return m_layers; }
	LayerNode* AddLayer(const std::wstring& name, int index = -1);
	bool       RemoveLayer(LayerNode* layer);
	void       MoveLayer(LayerNode* layer, int newIndex);
	// 노드를 다른 부모(레이어/그룹) 또는 같은 부모의 다른 위치로 이동 (레이어 패널 드래그용).
	// index = 이동 전 newParent 자식 목록 기준 삽입 위치 (-1 = 최상단). 화면상 위치가 변하지 않게 로컬 변환을 보정.
	// 레이어/자기 자신/자기 후손 안으로의 이동은 거부 (false)
	bool       MoveNodeTo(Node* node, GroupNode* newParent, int index);
	LayerNode* GetActiveLayer() const;
	void       SetActiveLayer(int index);
	int        GetActiveLayerIndex() const { return m_activeLayer; }

	// Artboard: 레이어 직속 최상위 FrameNode 목록 조회
	std::vector<FrameNode*> GetArtboards() const;

	// 도형 추가 : 항상 활성 레이어의 최상단 (기존 push_back 대체 지점)
	Node* AddShape(std::unique_ptr<Node> node);
	// 프레임 자동 부모 (4.5.12): 월드 점 아래의 가장 안쪽 프레임을 찾는다 (exclude 와 그 후손은 제외). 없으면 nullptr
	FrameNode* FindFrameAt(Vec2 worldPt, const Node* exclude = nullptr) const;
	// 월드 좌표에 놓인 새 노드를 worldPt 아래의 프레임의 자식으로 추가 (없으면 활성 레이어). 변환은 부모 로컬로 보정됨
	Node* AddShapeAt(std::unique_ptr<Node> node, Vec2 worldPt);
	// 노드의 월드 경계 중심 기준으로 AddShapeAt (시작점을 모르는 도구용)
	Node* AddShapeAuto(std::unique_ptr<Node> node);
	// 노드를 월드 점 아래의 프레임(없으면 소속 레이어)으로 옮긴다 (4.5.13). 부모가 바뀌면 true
	bool ReparentAt(Node* node, Vec2 worldPt);
	// 자동 배치 프레임 체인을 다시 배치 (자식 프레임 -> 조상 순)
	static void RelayoutFrameChain(GroupNode* from);

	// 선택 (인덱스가 아닌 포인터/ID 기반). 원자 컴포넌트(버튼/UI 컴포넌트) 내부 자식은 항상 루트 컴포넌트로 승격하여 자식 재구성 시 댕글링 포인터 원천 차단
	const std::vector<Node*>& GetSelection() const { return m_selection; }
	void SetSelection(const std::vector<Node*>& nodes) {
		m_selection.clear();
		for (Node* n : nodes) {
			if (!n) continue;
			Node* cur = n;
			for (GroupNode* p = n->GetParent(); p; p = p->GetParent()) {
				if (IsAtomicComponent(p)) cur = p;
			}
			if (std::find(m_selection.begin(), m_selection.end(), cur) == m_selection.end())
				m_selection.push_back(cur);
		}
	}
	void ClearSelection()                               { m_selection.clear(); }

	// ---- 그룹 격리 편집 (Isolation Mode, Step 5.3) ----
	// 격리 대상은 NodeId 로 보관한다 (Undo/문서 교체로 노드가 다시 만들어져도 id 로 찾고, 사라졌으면 자동 해제)
	static bool CanIsolate(const Node* n);                 // 일반 그룹만 (프레임/표/클리핑 마스크 그룹/레이어 제외)
	GroupNode*  GetIsolationGroup();                       // 현재 격리 대상 (없거나 사라졌으면 nullptr)
	bool        EnterIsolation(GroupNode* g);              // 선택을 비우고 g 내부 편집으로 진입
	void        ExitIsolation();                           // 한 단계 위로 (부모가 격리 가능한 그룹이면 그 그룹, 아니면 해제)
	void        ClearIsolation()                           { m_isolationId = 0; }
	std::vector<GroupNode*> IsolationPath();               // 레이어 -> ... -> 현재 격리 그룹 (브레드크럼 표시용)

	// 그룹화 & 프레임 감싸기
	GroupNode* GroupSelection();
	void       UngroupSelection();
	FrameNode* WrapSelectionInFrame();        // 선택된 노드들을 감싸는 새 FrameNode 생성 (Figma: Frame Selection)
	bool       CanGroupSelection() const;     // 그룹화 가능한 선택(2개 이상)인지
	bool       CanWrapInFrame() const;        // 프레임으로 감쌀 수 있는 선택(1개 이상)인지
	bool       CanUngroupSelection() const;   // 선택에 그룹이 있는지
	bool       CanArrangeSelection() const;   // 순서(Z-order)를 바꿀 수 있는 선택(레이어 제외, 부모가 있는 노드)이 있는지
	bool       CanBooleanOperation() const;   // 불리언 연산 가능한 선택 (2개 이상의 PathNode 또는 변환 가능한 도형)
#ifndef GE_NO_D2D
	PathNode*  BooleanOperation(BooleanMode mode, ID2D1Factory* factory);
#endif
	// 클리핑 마스크 (Ctrl+7 만들기, Ctrl+Alt+7 해제)
	bool       CanMakeClipMask() const;
	bool       CanReleaseClipMask() const;
	GroupNode* MakeClipMask();
	void       ReleaseClipMask();

	// 패스 연산 (Step 2.6: Outline, Simplify, Reverse, Join)
	bool       CanOutlineStroke() const;
	bool       CanSimplify() const;
	bool       CanReversePath() const;
	bool       CanJoinPaths() const;
#ifndef GE_NO_D2D
	std::vector<PathNode*> OutlineStrokeSelection(ID2D1Factory* factory, float widthOverride = -1.0f);   // widthOverride > 0: 선 두께를 이 값으로 확장
	std::vector<PathNode*> SmoothSelection(float tolerance);
	bool CanConvertTextToPath() const;
	std::vector<PathNode*> TextToPathSelection(ID2D1Factory* factory);   // 선택된 텍스트를 같은 위치의 PathNode 로 교체
	std::vector<PathNode*> SimplifySelection(float tolerance = 2.0f);
	std::vector<PathNode*> ReversePathSelection();
	PathNode*              JoinSelection();
#endif

	// 복제 (Ctrl+D): 선택 항목을 같은 부모의 원본 바로 위에 복제, 새 ID/이름 발급 후 복제본을 선택
	std::vector<Node*> DuplicateSelection();

	// 다른 Document(파일에서 읽은 임시 문서, 클립보드 SVG 등)의 노드를 이 문서로 가져오기 (SVG 가져오기/붙여넣기 공용 경로).
	// 소스는 변경하지 않고 Clone() 한 뒤 이 문서의 ID 발급기로 모든 노드(하위 포함)에 새 NodeId 를 부여하므로,
	// 소스의 ID 가 이 문서와 겹쳐도 충돌하지 않는다. 선택은 바꾸지 않음 (호출 측이 반환값으로 결정).
	//  - target == nullptr : 활성 레이어에 삽입. 소스가 레이어이면 새 레이어로 추가됨 (index = 레이어 위치)
	//  - target 지정       : 해당 그룹/레이어의 index 위치(-1 = 최상단)에 삽입. 소스가 레이어이면 그 자식들을 펼쳐서 삽입
	//  - target 이 이 문서 소속이 아니면 아무것도 하지 않고 빈 목록 반환
	// 반환: 이 문서에 새로 생성된 최상위 노드들 (소스 순서)
	std::vector<Node*> ImportNodes(const std::vector<const Node*>& sources, GroupNode* target = nullptr, int index = -1);

		// 노드 제거 (소유권 반환, 선택에서도 제외)
		std::unique_ptr<Node> RemoveNode(Node* node);
		Node* FindById(NodeId id) const;

		// 자동 명명 (Inkscape 스타일: rect1, ellipse1, path1 등)
		std::wstring GenerateNodeName(ShapeKind kind) const;
		std::wstring GenerateLayerName() const;
		std::wstring GenerateGroupName() const;                              // group1, group2 ...
		std::wstring GenerateFrameName() const;                              // Frame 1, Frame 2 ...
		std::wstring GenerateNameWithPrefix(const std::wstring& prefix) const;

		// 선택 항목 Z-Order 변경 (각자의 부모 안에서)
		enum class ZOrder { Front, Back, Forward, Backward };
		void ArrangeSelection(ZOrder order);

		// 월드 좌표 바운딩 박스 (Step 6.8: 기하학 경계 vs 스트로크 두께가 반영된 시각적 경계 분리)
		static bool GetWorldBounds(Node& node, Rect2& out);
		static bool GetVisualWorldBounds(Node& node, Rect2& out);
		bool GetSelectionBounds(Rect2& out) const;

	// 정렬 / 분배 (선택 기준)
	enum class AlignMode { Left, CenterH, Right, Top, CenterV, Bottom };
	void AlignSelection(AlignMode mode, bool toCanvas);
	void DistributeSelection(bool horizontal);

	// 캔버스/도큐멘트 크기 및 배경색, 원점 모드
	float width  = 1920.0f;
	float height = 1080.0f;
	Color backgroundColor = Color(1.0f, 1.0f, 1.0f, 1.0f); // 도큐멘트(용지) 배경색 (투명도 포함)
	Color canvasColor     = Color(0.84f, 0.85f, 0.88f, 1.0f); // 캔버스(전체 월드 작업대) 배경색
	CanvasOriginMode originMode = CanvasOriginMode::TopLeft;

	NodeId IssueId() { return ++m_nextId; }
	NodeId PeekMaxId() const { return m_nextId; }
	void   AdvanceMaxId(NodeId minMax) { if (minMax > m_nextId) m_nextId = minMax; }
		void   AssignIds(Node& node);   // id==0 인 노드(하위 포함)에 ID 발급
	// 파일/스냅샷 복원용: 노드 ID 를 지정하고 발급 카운터가 그 값보다 커지도록 보장 (이후 새 ID 와 충돌 방지)
	void   RestoreNodeId(Node& node, NodeId id);
	// 트리 전체에서 ID 0 / 중복 ID 를 찾아 새 ID 로 교체 (파일에서 읽은 문서의 무결성 보장)
	void   EnsureUniqueIds();

private:
	std::vector<std::unique_ptr<LayerNode>> m_layers;   // 0 = 최하단 레이어
	int                                      m_activeLayer = 0;
	std::vector<Node*>                       m_selection;
	NodeId                                   m_isolationId = 0;
	NodeId                                   m_nextId = 0;
};

// ============================================================================
// 2.5 Geometry Transform (좌표 변환 + 도형 변형)
// ============================================================================

// 로컬 <-> 월드 <-> 화면 좌표 변환 (기존 CoordinateSpace 대응)
class CoordinateSpace {
public:
    static Vec2 LocalToScreen(Vec2 localPt, const Matrix2D& worldXf, const Viewport& view);
    static std::optional<Vec2> ScreenToLocal(Vec2 screenPt, const Matrix2D& worldXf, const Viewport& view);

#ifndef GE_NO_D2D
    static D2D1_POINT_2F LocalToScreen(D2D1_POINT_2F localPt, const Matrix2D& worldXf, const Viewport& view) {
        Vec2 r = LocalToScreen(Vec2{ localPt.x, localPt.y }, worldXf, view);
        return r.ToD2D();
    }
    static D2D1_POINT_2F LocalToScreen(D2D1_POINT_2F localPt, const D2D1_MATRIX_3X2_F& worldXf, const Viewport& view) {
        Vec2 r = LocalToScreen(Vec2{ localPt.x, localPt.y }, Matrix2D::FromD2D(worldXf), view);
        return r.ToD2D();
    }
    static std::optional<D2D1_POINT_2F> ScreenToLocal(D2D1_POINT_2F screenPt, const Matrix2D& worldXf, const Viewport& view) {
        auto r = ScreenToLocal(Vec2{ screenPt.x, screenPt.y }, worldXf, view);
        if (!r.has_value()) return std::nullopt;
        return r->ToD2D();
    }
    static std::optional<D2D1_POINT_2F> ScreenToLocal(D2D1_POINT_2F screenPt, const D2D1_MATRIX_3X2_F& worldXf, const Viewport& view) {
        auto r = ScreenToLocal(Vec2{ screenPt.x, screenPt.y }, Matrix2D::FromD2D(worldXf), view);
        if (!r.has_value()) return std::nullopt;
        return r->ToD2D();
    }
#endif
};

// 도형 변형 (선택 툴 / 속성창 / 메뉴 공용).
// 노드를 직접 바꾸지 않고 '새 변환 행렬'을 계산해 반환 -> 호출 측이 SetTransform() 하거나 Command 로 기록.
class Transformer {
public:
    // 1. 평행 이동
    static Matrix2D Translate(Vec2 deltaWorld, const Matrix2D& initialXf);
#ifndef GE_NO_D2D
    static Matrix2D Translate(D2D1_POINT_2F deltaWorld, const D2D1_MATRIX_3X2_F& initialXf) {
        return Translate({ deltaWorld.x, deltaWorld.y }, Matrix2D::FromD2D(initialXf));
    }
#endif

    // 2. 바운딩 박스 8개 핸들 기준 스케일
    //    handleIdx: 1=TL, 2=T, 3=TR, 4=R, 5=BR, 6=B, 7=BL, 8=L
    //    실패(역행렬 불가, 크기 0 등) 시 nullopt
    static std::optional<Matrix2D> ScaleByHandle(int handleIdx, Vec2 curWorld,
                                                 const Rect2& initialLocalBounds,
                                                 const Matrix2D& initialXf);
#ifndef GE_NO_D2D
    static std::optional<Matrix2D> ScaleByHandle(int handleIdx, D2D1_POINT_2F curWorld,
                                                 const D2D1_RECT_F& initialLocalBounds,
                                                 const D2D1_MATRIX_3X2_F& initialXf) {
        return ScaleByHandle(handleIdx, { curWorld.x, curWorld.y },
                             Rect2::FromD2D(initialLocalBounds),
                             Matrix2D::FromD2D(initialXf));
    }
#endif

    // 3. 중심점 기준 회전
    static Matrix2D RotateAboutCenter(Vec2 startWorld, Vec2 curWorld,
                                      const Rect2& initialLocalBounds,
                                      const Matrix2D& initialXf);
#ifndef GE_NO_D2D
    static Matrix2D RotateAboutCenter(D2D1_POINT_2F startWorld, D2D1_POINT_2F curWorld,
                                      const D2D1_RECT_F& initialLocalBounds,
                                      const D2D1_MATRIX_3X2_F& initialXf) {
        return RotateAboutCenter({ startWorld.x, startWorld.y }, { curWorld.x, curWorld.y },
                                 Rect2::FromD2D(initialLocalBounds),
                                 Matrix2D::FromD2D(initialXf));
    }
#endif

    // 4. 로컬 바운딩 박스 기준 핸들 9개 좌표 (0~7: TL,T,TR,R,BR,B,BL,L / 8: 회전 핸들)
    static void GetLocalHandlePoints(const Rect2& b, Vec2 outHandles[9]);
#ifndef GE_NO_D2D
    static void GetLocalHandlePoints(const D2D1_RECT_F& b, D2D1_POINT_2F outHandles[9]) {
        Vec2 handles[9];
        GetLocalHandlePoints(Rect2::FromD2D(b), handles);
        for (int i = 0; i < 9; ++i) outHandles[i] = handles[i].ToD2D();
    }
#endif

    // 5. 단일 도형 가장자리 핸들(2=T, 6=B: 수평 전단 / 4=R, 8=L: 수직 전단) 기준 전단(Skew/Shear) 변형 (Step 6.14)
    static std::optional<Matrix2D> SkewByEdgeHandle(int handleIdx, Vec2 startWorld, Vec2 curWorld,
                                                    const Rect2& initialLocalBounds,
                                                    const Matrix2D& initialXf);
};

#ifndef GE_NO_D2D
// ============================================================================
// 2.6 Path Operations / Pathfinder (불린 연산: 합치기 / 빼기 / 교차 / 제외)
// ============================================================================
class PathOp {
public:

	// PathNode 로부터 D2D PathGeometry 생성 (로컬 좌표계)
	static ComPtr<ID2D1PathGeometry> BuildGeometry(const PathNode& node, ID2D1Factory* factory);

	// Step 6.6: 프로세스 공용 지오메트리 캐시 조회 (NodeId + Version + Factory 일치 시 재사용)
	static ComPtr<ID2D1PathGeometry> GetCachedGeometry(const PathNode& node, ID2D1Factory* factory);

	// PathNode 로부터 월드 변환이 적용된 ID2D1Geometry 생성 (ID2D1TransformedGeometry)
	static ComPtr<ID2D1Geometry> BuildWorldGeometry(const PathNode& node, ID2D1Factory* factory);

	// ID2D1Geometry 로부터 SubPath 목록 추출 (SimplifiedGeometrySink 사용)
	static bool ExtractSubPaths(ID2D1Geometry* geometry, std::vector<SubPath>& outPaths,
	                            D2D1_FILL_MODE* outFillMode = nullptr,
	                            float flatteningTolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE);

	// 두 PathNode의 지오메트리를 불린 연산하여 결과 정점을 담은 새 PathNode를 생성해 반환
	// 실패하거나 겹치지 않는 경우 nullopt 또는 nullptr 반환
	static std::unique_ptr<PathNode> Combine(const PathNode& a, const PathNode& b,
	                                         BooleanMode mode, ID2D1Factory* factory,
	                                         float flatteningTolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE);

	// 패스 연산 (Step 2.6)
	// 1) 외곽선 확장: 선(Stroke)을 닫힌 면(Fill) 패스로 변환
	static std::unique_ptr<PathNode> OutlineStroke(const PathNode& node, ID2D1Factory* factory,
	                                              float flatteningTolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE,
	                                              float widthOverride = -1.0f);

	// 2) 패스 단순화: RDP 알고리즘으로 과도한 정점 제거
	static bool Simplify(PathNode& node, float tolerance = 2.0f);

	// 2b) 부드럽게: 정점을 줄인 뒤(RDP) 남은 점을 지나는 부드러운 베지에 곡선(Catmull-Rom)으로 변환. 각진 꺾은선/자유곡선을 곡선화
	static bool Smooth(PathNode& node, float tolerance = 1.0f);

	// 3) 패스 방향 반전: 서브패스 정점 순서 및 핸들 반전
	static void ReversePath(PathNode& node);

	// 4) 패스 결합: 인접한 끝점을 가진 서브패스 또는 두 패스를 하나로 결합
	static std::unique_ptr<PathNode> Join(const PathNode& a, const PathNode& b, float maxDistance = 10.0f);

	// 이미지 바이트의 원본 픽셀 크기 (WIC). 실패하면 false
	static bool DecodeImageInfo(const std::vector<uint8_t>& bytes, int& w, int& h);

	// 영역 텍스트의 자동 줄바꿈 결과 (줄 단위 문자열, 끝 공백/개행 제거). 점 텍스트는 \n 기준 줄
	static std::vector<std::wstring> WrapLines(const TextNode& text);

	// 텍스트 크기를 DirectWrite 로 즉시 재서 노드에 기록 (속성창에서 글꼴/크기를 바꾼 직후 W/H 를 바로 갱신하기 위함)
	static void MeasureText(TextNode& text);

	// 텍스트 -> 윤곽선 패스 (DirectWrite 글리프 윤곽). 원본과 같은 로컬 좌표/변환/채우기를 가져 화면상 모양이 같다
	static std::unique_ptr<PathNode> TextToPath(const TextNode& text, ID2D1Factory* factory);
};

// ============================================================================
// 3. Renderer
// ============================================================================
class IRenderer {
public:
	virtual ~IRenderer() = default;
	virtual void BeginDraw() = 0;
	virtual void EndDraw() = 0;
	virtual void DrawDocument(Document& doc, const Viewport& view) = 0;
	virtual void DrawNode(Node& node, const Matrix2D& parentXf, const Viewport& view) = 0;
};

// Direct2D 구현 (D2D 객체와 지오메트리 캐시는 여기서만 보유)
class D2DRenderer : public IRenderer {
public:
	D2DRenderer() = default;

	bool Initialize(ID2D1RenderTarget* rt, ID2D1Factory* factory);
	void Shutdown();

	void BeginDraw() override;
	void EndDraw() override;
	void DrawDocument(Document& doc, const Viewport& view) override;
	void DrawNode(Node& node, const Matrix2D& parentXf, const Viewport& view) override;

	// 노드 -> D2D 지오메트리 (m_dirty 일 때만 재생성)
	ID2D1Geometry* GetGeometry(PathNode& node);
	// 도형의 실제 지오메트리 경계 (컨트롤 포인트 제외). 그라디언트 objectBoundingBox 의 기준 - 렌더러/기즈모/SVG 가 같은 박스를 쓴다
	bool GetFillBounds(PathNode& node, Rect2& out);
		void ClearCache() { m_geoCache.clear(); m_strokeStyleCache.clear(); m_gradientCache.clear(); }
		ID2D1Factory* GetFactory() const { return m_factory; }

private:
	void DrawPath(PathNode& node, const Matrix2D& worldXf, const Viewport& view);
	void DrawGroup(GroupNode& node, const Matrix2D& worldXf, const Viewport& view);
	void DrawTextNode(TextNode& node, const Matrix2D& worldXf, const Viewport& view);
	void DrawImageNode(ImageNode& node, const Matrix2D& worldXf, const Viewport& view);
	// 테이블 격자선 (D2DTableRender.cpp). 칸 내용은 DrawGroup 이 자식으로 그린다
	void DrawTableGrid(TableNode& node, const Matrix2D& worldXf, const Viewport& view);
	// 공통 그림자 (4.5.17, D2DShadowRender.cpp): 노드 윤곽 뒤에 블러 근사 그림자. 글자는 레이아웃 모양대로
	void DrawNodeShadow(Node& node, const Matrix2D& worldXf, const Viewport& view);
	void DrawNodeInsetShadow(Node& node, const Matrix2D& worldXf, const Viewport& view);   // 안쪽 그림자: 노드 위에 클립 안에서
	void DrawTextShadow(IDWriteTextLayout* layout, float x0, const Shadow& sh, const Matrix2D& worldXf, const Viewport& view);

	// 디코딩한 비트맵 캐시 (렌더 타깃에 종속이라 Initialize/Shutdown 에서 비움). 키 데이터를 함께 잡아 주소 재사용을 막음
	struct ImageCacheEntry { std::shared_ptr<const ImageData> keep; ComPtr<ID2D1Bitmap> bitmap; };
	std::unordered_map<const ImageData*, ImageCacheEntry> m_imageCache;
	ComPtr<ID2D1PathGeometry> BuildGeometry(const PathNode& node);
	// 기본값(Butt/Miter/실선)이면 nullptr, 아니면 캐시된 스트로크 스타일
	ID2D1StrokeStyle* GetStrokeStyle(const Stroke& stroke);
	// 그라디언트 채우기 브러시 (노드 버전이 같으면 캐시 재사용). 만들 수 없으면 nullptr
	ID2D1Brush* GetGradientBrush(const Node& node, const Rect2& bounds);

	ID2D1RenderTarget*             m_rt      = nullptr;   // 비소유
	ID2D1Factory*                  m_factory = nullptr;   // 비소유
	ComPtr<ID2D1SolidColorBrush>   m_brush;
	// PathNode 별 지오메트리 캐시
	struct GeoCache { uint32_t version = 0; ComPtr<ID2D1PathGeometry> geo; };
	// Stroke(cap/join/miter/dash) 조합별 D2D 스트로크 스타일 캐시 (키는 정규화된 값 문자열)
	std::unordered_map<std::string, ComPtr<ID2D1StrokeStyle>> m_strokeStyleCache;
	// 그라디언트 브러시 캐시 (렌더 타깃에 종속되므로 Initialize/Shutdown 에서 폐기)
	struct GradientCache { uint32_t version = 0; ComPtr<ID2D1Brush> brush; };
	std::unordered_map<NodeId, GradientCache> m_gradientCache;
	std::unordered_map<NodeId, GeoCache> m_geoCache;
};

#endif

} // namespace ge
