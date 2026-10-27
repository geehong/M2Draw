#pragma once
// ============================================================================
// D2DGraphicEngine.h  -  M2Draw 2D 그래픽 엔진 (뼈대)
//
//  [계층 구조]
//   1. Math      : Vec2 / Rect2 / Matrix2D            (D2D 비의존)
//   2. Model     : Node -> PathNode / GroupNode -> LayerNode, Document (Scene Graph)
//   3. Renderer  : IRenderer -> D2DRenderer           (D2D 의존은 여기에만)
//   4. HitTester : 클릭 판정
//   5. Command   : Undo / Redo
//
//  - 기존 D2DVectorEngine.h 와 공존하도록 모두 namespace ge 안에 정의.
//  - 변수명은 기존 벡터 엔진(anchor, hIn, hOut, m_xf, m_style, m_dirty ...)을 따름.
//  - 순서: 배열 인덱스 0 = 최하단, 마지막 = 최상단 (기존과 동일).
// ============================================================================

#include <d2d1_1.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <optional>
#include <functional>
#include <unordered_map>
#include <variant>

namespace ge {

using Microsoft::WRL::ComPtr;

// ============================================================================
// 1. Math
// ============================================================================
struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	Vec2() = default;
	Vec2(float _x, float _y) : x(_x), y(_y) {}
	Vec2(const D2D1_POINT_2F& p) : x(p.x), y(p.y) {}

	operator D2D1_POINT_2F() const { return D2D1::Point2F(x, y); }
	Vec2& operator=(const D2D1_POINT_2F& p) { x = p.x; y = p.y; return *this; }

	D2D1_POINT_2F ToD2D() const { return D2D1::Point2F(x, y); }
	static Vec2 FromD2D(D2D1_POINT_2F p) { return { p.x, p.y }; }
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
	Rect2(const D2D1_RECT_F& r) : left(r.left), top(r.top), right(r.right), bottom(r.bottom) {}

	operator D2D1_RECT_F() const { return D2D1::RectF(left, top, right, bottom); }
	Rect2& operator=(const D2D1_RECT_F& r) { left = r.left; top = r.top; right = r.right; bottom = r.bottom; return *this; }

	D2D1_RECT_F ToD2D() const { return D2D1::RectF(left, top, right, bottom); }
	static Rect2 FromD2D(D2D1_RECT_F r) { return { r.left, r.top, r.right, r.bottom }; }
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
	Matrix2D(const D2D1_MATRIX_3X2_F& m)
		: m11(m._11), m12(m._12), m21(m._21), m22(m._22), dx(m._31), dy(m._32) {}

	operator D2D1_MATRIX_3X2_F() const { return ToD2D(); }
	operator D2D1::Matrix3x2F() const {
		return *reinterpret_cast<const D2D1::Matrix3x2F*>(&ToD2D());
	}
	Matrix2D& operator=(const D2D1_MATRIX_3X2_F& m) {
		m11 = m._11; m12 = m._12; m21 = m._21; m22 = m._22; dx = m._31; dy = m._32;
		return *this;
	}

	static Matrix2D Identity() { return {}; }
	static Matrix2D Translation(float x, float y);
	static Matrix2D Scale(float sx, float sy, Vec2 center = {});
	static Matrix2D Rotation(float degree, Vec2 center = {});

	Matrix2D operator*(const Matrix2D& rhs) const;
	Matrix2D operator*(const D2D1_MATRIX_3X2_F& rhs) const {
		return (*this) * Matrix2D::FromD2D(rhs);
	}
	bool     Invert();                           // 실패 시 false
	Vec2     TransformPoint(Vec2 p) const;
	D2D1_POINT_2F TransformPoint(D2D1_POINT_2F p) const {
		Vec2 r = TransformPoint(Vec2(p.x, p.y));
		return D2D1::Point2F(r.x, r.y);
	}
	Rect2    TransformRect(const Rect2& r) const; // AABB 반환

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
};

inline Matrix2D operator*(const D2D1_MATRIX_3X2_F& lhs, const Matrix2D& rhs) {
	return Matrix2D::FromD2D(lhs) * rhs;
}

// 뷰포트 (기존 ViewportTransform 대응)
struct Viewport {
	float zoom = 1.0f;
	Vec2  pan{};
	Matrix2D ToMatrix() const;
	Vec2 ScreenToWorld(Vec2 screenPt) const;
	Vec2 WorldToScreen(Vec2 worldPt) const;

	D2D1_POINT_2F ScreenToWorld(D2D1_POINT_2F screenPt) const {
		Vec2 res = ScreenToWorld(Vec2{ screenPt.x, screenPt.y });
		return res.ToD2D();
	}
	D2D1_POINT_2F WorldToScreen(D2D1_POINT_2F worldPt) const {
		Vec2 res = WorldToScreen(Vec2{ worldPt.x, worldPt.y });
		return res.ToD2D();
	}
};

// ============================================================================
// 2. Model (Scene Graph) - 렌더링 API에 의존하지 않는 순수 데이터
// ============================================================================
struct Color {
	float r = 0, g = 0, b = 0, a = 1;

	Color() = default;
	Color(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
	Color(const D2D1_COLOR_F& c) : r(c.r), g(c.g), b(c.b), a(c.a) {}

	operator D2D1_COLOR_F() const { return D2D1::ColorF(r, g, b, a); }
	Color& operator=(const D2D1_COLOR_F& c) { r = c.r; g = c.g; b = c.b; a = c.a; return *this; }

	D2D1_COLOR_F ToD2D() const { return D2D1::ColorF(r, g, b, a); }
	static Color FromD2D(const D2D1_COLOR_F& c) { return { c.r, c.g, c.b, c.a }; }
	static Color FromD2D(const Color& c) { return c; }
};

enum class FillType : uint8_t { None = 0, Solid = 1, Pattern = 2 };
enum class DashStyle : uint8_t { Solid = 0, Dash, Dot, DashDot, DashDotDot };
enum class FillMode  : uint8_t { Alternate = 0, Winding };

inline DashStyle FromD2D(D2D1_DASH_STYLE ds) {
	return static_cast<DashStyle>(ds);
}
inline D2D1_DASH_STYLE ToD2D(DashStyle ds) {
	return static_cast<D2D1_DASH_STYLE>(ds);
}

struct DashStyleWrapper {
	DashStyle value = DashStyle::Solid;

	DashStyleWrapper() = default;
	DashStyleWrapper(DashStyle s) : value(s) {}
	DashStyleWrapper(D2D1_DASH_STYLE s) : value(static_cast<DashStyle>(s)) {}

	operator D2D1_DASH_STYLE() const { return static_cast<D2D1_DASH_STYLE>(value); }
	explicit operator DashStyle() const { return value; }
	DashStyle ToDashStyle() const { return value; }
	DashStyleWrapper& operator=(DashStyle s) { value = s; return *this; }
	DashStyleWrapper& operator=(D2D1_DASH_STYLE s) { value = static_cast<DashStyle>(s); return *this; }
	bool operator==(DashStyle s) const { return value == s; }
	bool operator==(D2D1_DASH_STYLE s) const { return static_cast<D2D1_DASH_STYLE>(value) == s; }
	bool operator!=(DashStyle s) const { return value != s; }
	bool operator!=(D2D1_DASH_STYLE s) const { return static_cast<D2D1_DASH_STYLE>(value) != s; }
};

struct Style {
	FillType         fillType    = FillType::None;
	Color            fill        = { 0.2f, 0.6f, 1.0f, 0.25f };
	Color            stroke      = { 0.12f, 0.56f, 1.0f, 1.0f };
	float            strokeWidth = 2.0f;
	DashStyleWrapper dashStyle   = DashStyle::Solid;
	FillMode         fillMode    = FillMode::Alternate;
};

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

enum class NodeKind : uint8_t { Path, Group, Layer };

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
	Heart
};

struct RectParams     { float rx = 0.0f; float ry = 0.0f; bool linkRxRy = true; };
struct TriangleParams { bool equilateral = false; };
struct EllipseParams  { bool linkRxRy = true; };
struct PolygonParams  { int sides = 5; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct StarParams     { int points = 5; float innerRatio = 0.5f; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct GearParams     { int teeth = 12; float depthRatio = 0.2f; float boreRatio = 0.3f; float toothWidthRatio = 0.5f; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct SpiralParams   { float turns = 3.0f; float innerRatio = 0.1f; float radius = 0.0f; float cx = 0.0f; float cy = 0.0f; };
struct HeartParams    { };

using ShapeParams = std::variant<std::monostate, RectParams, TriangleParams, EllipseParams,
                                 PolygonParams, StarParams, GearParams, SpiralParams, HeartParams>;

using NodeId = uint64_t;

// 히트테스트/노드 편집에서 공용으로 쓰는 부위 종류
enum class HitPart { None, Fill, Stroke, Anchor, HandleIn, HandleOut };

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

// ---- 모든 씬 노드의 기반 클래스 ---------------------------------------------
class GroupNode;

class Node {
public:
	virtual ~Node() = default;
	virtual NodeKind Kind() const = 0;
	virtual std::unique_ptr<Node> Clone() const = 0;

	// 로컬 바운딩 박스 (자신의 m_xf 적용 전)
	virtual bool GetLocalBounds(Rect2& out) = 0;
	virtual bool GetLocalBounds(ID2D1Factory* f, D2D1_RECT_F& outBounds) {
		Rect2 r;
		if (GetLocalBounds(r)) {
			outBounds = r.ToD2D();
			return true;
		}
		return false;
	}

	// 계층 구조
	NodeId            GetId() const          { return m_id; }
	GroupNode*        GetParent() const      { return m_parent; }

	// getter / setter
	const std::wstring& GetName() const      { return m_name; }
	void SetName(const std::wstring& v)      { m_name = v; }

	const Matrix2D& GetTransform() const     { return m_xf; }
	// 변환은 로컬 지오메트리와 무관하므로 지오메트리 캐시를 무효화하지 않음
	void SetTransform(const Matrix2D& v)     { m_xf = v; }

	const Style& GetStyle() const            { return m_style; }
	// 수정용 접근자: 호출 시 지오메트리 캐시 무효화 (fillType/fillMode 가 지오메트리에 영향). 읽기는 GetStyle() 사용
	Style&       GetStyleRef()               { Invalidate(); return m_style; }
	void SetStyle(const Style& v)            { m_style = v; Invalidate(); }

	bool IsVisible() const                   { return m_visible; }
	void SetVisible(bool v)                  { m_visible = v; }
	bool IsLocked() const                    { return m_locked; }
	void SetLocked(bool v)                   { m_locked = v; }
	float GetOpacity() const                 { return m_opacity; }
	void SetOpacity(float v)                 { m_opacity = v; }

	void Invalidate()                        { m_dirty = true; ++m_version; }
	bool IsDirty() const                     { return m_dirty; }

	// 부모 체인을 곱한 월드 변환 (자신 포함)
	Matrix2D GetWorldTransform() const;

	// 부모 불투명도까지 곱한 최종 불투명도
	float GetEffectiveOpacity() const;

	// 변경 횟수 (렌더러 지오메트리 캐시 무효화 판단용)
	uint32_t GetVersion() const { return m_version; }

	virtual const wchar_t* GetShapeTypeName() const { return L"쉐이프"; }

	void ApplyTranslation(D2D1_POINT_2F deltaWorld, const D2D1::Matrix3x2F& initialXf) {
		m_xf = Matrix2D::FromD2D(initialXf) * Matrix2D::Translation(deltaWorld.x, deltaWorld.y);
		Invalidate();
	}

	void ScaleByHandle(int handleIdx, D2D1_POINT_2F curWorld, const D2D1_RECT_F& initialLocalBounds, const D2D1::Matrix3x2F& initialXf) {
		Matrix2D invInitial = Matrix2D::FromD2D(initialXf);
		if (!invInitial.Invert()) return;

		Vec2 curLocal = invInitial.TransformPoint(Vec2(curWorld.x, curWorld.y));
		float origW = initialLocalBounds.right - initialLocalBounds.left;
		float origH = initialLocalBounds.bottom - initialLocalBounds.top;
		if (origW <= 0.001f || origH <= 0.001f) return;

		Vec2 anchorLocal = { 0.0f, 0.0f };
		float sx = 1.0f;
		float sy = 1.0f;

		switch (handleIdx) {
		case 5: // BR -> 고정점 TL
			anchorLocal = { initialLocalBounds.left, initialLocalBounds.top };
			sx = (curLocal.x - anchorLocal.x) / origW;
			sy = (curLocal.y - anchorLocal.y) / origH;
			break;
		case 1: // TL -> 고정점 BR
			anchorLocal = { initialLocalBounds.right, initialLocalBounds.bottom };
			sx = (anchorLocal.x - curLocal.x) / origW;
			sy = (anchorLocal.y - curLocal.y) / origH;
			break;
		case 3: // TR -> 고정점 BL
			anchorLocal = { initialLocalBounds.left, initialLocalBounds.bottom };
			sx = (curLocal.x - anchorLocal.x) / origW;
			sy = (anchorLocal.y - curLocal.y) / origH;
			break;
		case 7: // BL -> 고정점 TR
			anchorLocal = { initialLocalBounds.right, initialLocalBounds.top };
			sx = (anchorLocal.x - curLocal.x) / origW;
			sy = (curLocal.y - anchorLocal.y) / origH;
			break;
		case 4: // R -> 고정점 L
			anchorLocal = { initialLocalBounds.left, (initialLocalBounds.top + initialLocalBounds.bottom) * 0.5f };
			sx = (curLocal.x - anchorLocal.x) / origW;
			sy = 1.0f;
			break;
		case 8: // L -> 고정점 R
			anchorLocal = { initialLocalBounds.right, (initialLocalBounds.top + initialLocalBounds.bottom) * 0.5f };
			sx = (anchorLocal.x - curLocal.x) / origW;
			sy = 1.0f;
			break;
		case 6: // B -> 고정점 T
			anchorLocal = { (initialLocalBounds.left + initialLocalBounds.right) * 0.5f, initialLocalBounds.top };
			sx = 1.0f;
			sy = (curLocal.y - anchorLocal.y) / origH;
			break;
		case 2: // T -> 고정점 B
			anchorLocal = { (initialLocalBounds.left + initialLocalBounds.right) * 0.5f, initialLocalBounds.bottom };
			sx = 1.0f;
			sy = (anchorLocal.y - curLocal.y) / origH;
			break;
		default:
			return;
		}

		if (std::abs(sx) < 0.01f) sx = 0.01f;
		if (std::abs(sy) < 0.01f) sy = 0.01f;

		Matrix2D scaleMat = Matrix2D::Scale(sx, sy, anchorLocal);
		m_xf = scaleMat * Matrix2D::FromD2D(initialXf);
		Invalidate();
	}

	void RotateAboutCenter(D2D1_POINT_2F startWorld, D2D1_POINT_2F curWorld, const D2D1_RECT_F& initialLocalBounds, const D2D1::Matrix3x2F& initialXf) {
		Vec2 centerLocal = {
			(initialLocalBounds.left + initialLocalBounds.right) * 0.5f,
			(initialLocalBounds.top + initialLocalBounds.bottom) * 0.5f
		};
		Matrix2D initXf = Matrix2D::FromD2D(initialXf);
		Vec2 centerWorld = initXf.TransformPoint(centerLocal);

		float angleStart = std::atan2(startWorld.y - centerWorld.y, startWorld.x - centerWorld.x);
		float angleCur   = std::atan2(curWorld.y - centerWorld.y, curWorld.x - centerWorld.x);
		float angleDeg   = (angleCur - angleStart) * (180.0f / 3.14159265f);

		m_xf = initXf * Matrix2D::Rotation(angleDeg, centerWorld);
		Invalidate();
	}

	static void GetLocalHandlePoints(const D2D1_RECT_F& b, D2D1_POINT_2F outHandles[9]) {
		float midX = (b.left + b.right) * 0.5f;
		float midY = (b.top + b.bottom) * 0.5f;

		outHandles[0] = D2D1::Point2F(b.left,  b.top);
		outHandles[1] = D2D1::Point2F(midX,    b.top);
		outHandles[2] = D2D1::Point2F(b.right, b.top);
		outHandles[3] = D2D1::Point2F(b.right, midY);
		outHandles[4] = D2D1::Point2F(b.right, b.bottom);
		outHandles[5] = D2D1::Point2F(midX,    b.bottom);
		outHandles[6] = D2D1::Point2F(b.left,  b.bottom);
		outHandles[7] = D2D1::Point2F(b.left,  midY);
		outHandles[8] = D2D1::Point2F(midX,    b.top - 20.0f);
	}

	// 렌더링 호환용 Draw
	void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, ID2D1Factory* f, const Viewport& view);

protected:
	// Clone 규약: 복제본은 문서 식별 정보(id/parent/version)를 갖지 않음 -> 문서 삽입 시 AssignIds 로 새 ID 발급
	void CopyNodePropsFrom(const Node& o) {
		m_name = o.m_name; m_xf = o.m_xf; m_style = o.m_style;
		m_visible = o.m_visible; m_locked = o.m_locked; m_opacity = o.m_opacity;
	}
	void ResetIdentity() { m_id = 0; m_parent = nullptr; m_version = 1; m_dirty = true; }

	uint32_t     m_version = 1;
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
	bool         m_dirty   = true;
};

// ---- 패스 도형 --------------------------------------------------------------
class PathNode : public Node {
public:
	NodeKind Kind() const override { return NodeKind::Path; }
	std::unique_ptr<Node> Clone() const override;
	bool GetLocalBounds(Rect2& out) override;
	using Node::GetLocalBounds;

	const std::vector<SubPath>& GetPaths() const { return m_paths; }
	// 수정용 접근자: 호출 시 지오메트리 캐시 무효화. 읽기는 GetPaths() 사용
	std::vector<SubPath>&       GetPathsRef()    { Invalidate(); return m_paths; }
	void SetPaths(const std::vector<SubPath>& v) { m_paths = v; Invalidate(); }
	void AddAnchor(Vec2 localPt, bool forceNewSubPath = false);
	void AddAnchor(D2D1_POINT_2F localPt, bool forceNewSubPath = false) {
		AddAnchor(Vec2{ localPt.x, localPt.y }, forceNewSubPath);
	}

	// 파라메트릭 도형 종류 및 설정
	ShapeKind GetKind() const { return m_kind; }
	void SetKind(ShapeKind k) { m_kind = k; }

	const ShapeParams& GetParams() const { return m_params; }
	ShapeParams&       GetParamsRef()    { return m_params; }
	void SetParams(const ShapeParams& p) { m_params = p; }

	template<typename T>
	const T* GetParam() const { return std::get_if<T>(&m_params); }

	template<typename T>
	T* GetParamRef() { return std::get_if<T>(&m_params); }

	// ---- 노드 편집 API ----
	// 앵커/핸들 이동 (worldPt 는 월드 좌표)
	void MoveNodeOrHandle(HitPart part, int subPathIdx, int nodeIdx, Vec2 worldPt, bool breakTangent = false);
	void MoveNodeOrHandle(HitPart part, int subPathIdx, int nodeIdx, D2D1_POINT_2F worldPt, bool breakTangent = false) {
		MoveNodeOrHandle(part, subPathIdx, nodeIdx, Vec2(worldPt.x, worldPt.y), breakTangent);
	}

	HitResult HitTestScreenSpace(D2D1_POINT_2F screenPt, float tolPx, const Viewport& view, ID2D1Factory* f);
	// 세그먼트를 t(0~1) 위치에서 분할(De Casteljau)해 노드 삽입. 새 노드 인덱스 반환, 실패 시 -1
	int  InsertNodeOnSegment(int subPathIdx, int segIdx, float t);
	// 노드 삭제 (서브패스가 비면 서브패스도 삭제)
	bool RemoveNode(int subPathIdx, int nodeIdx);
	// 로컬 좌표에서 가장 가까운 세그먼트 검색 (노드 추가 툴용)
	bool FindClosestSegment(Vec2 localPt, int& spIdx, int& segIdx, float& t, float& distSq) const;
	// 세그먼트 위의 t 지점 좌표
	static Vec2 EvalSegment(const BezierNode& a, const BezierNode& b, float t);

private:
	std::vector<SubPath> m_paths;
	ShapeKind            m_kind = ShapeKind::Path;
	ShapeParams          m_params;
};

// ---- 그룹 (컴포지트) --------------------------------------------------------
class GroupNode : public Node {
public:
	NodeKind Kind() const override { return NodeKind::Group; }
	std::unique_ptr<Node> Clone() const override;
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

protected:
	std::vector<std::unique_ptr<Node>> m_children;   // 0 = 최하단
};

} // namespace ge

#include "D2DLayer.h"

namespace ge {

// ---- 문서 (Scene 루트) ------------------------------------------------------
class Document {
public:
	Document();

	// 레이어
	const std::vector<std::unique_ptr<LayerNode>>& GetLayers() const { return m_layers; }
	LayerNode* AddLayer(const std::wstring& name, int index = -1);
	bool       RemoveLayer(LayerNode* layer);
	void       MoveLayer(LayerNode* layer, int newIndex);
	LayerNode* GetActiveLayer() const;
	void       SetActiveLayer(int index);
	int        GetActiveLayerIndex() const { return m_activeLayer; }

	// 도형 추가 : 항상 활성 레이어의 최상단 (기존 push_back 대체 지점)
	Node* AddShape(std::unique_ptr<Node> node);

	// 선택 (인덱스가 아닌 포인터/ID 기반)
	const std::vector<Node*>& GetSelection() const { return m_selection; }
	void SetSelection(const std::vector<Node*>& nodes) { m_selection = nodes; }
	void ClearSelection()                               { m_selection.clear(); }

	// 그룹화
	GroupNode* GroupSelection();
	void       UngroupSelection();

	// 복제 (Ctrl+D): 선택 항목을 같은 부모의 원본 바로 위에 복제, 새 ID/이름 발급 후 복제본을 선택
	std::vector<Node*> DuplicateSelection();

		// 노드 제거 (소유권 반환, 선택에서도 제외)
		std::unique_ptr<Node> RemoveNode(Node* node);
		Node* FindById(NodeId id) const;

		// 자동 명명 (Inkscape 스타일: rect1, ellipse1, path1 등)
		std::wstring GenerateNodeName(ShapeKind kind) const;
		std::wstring GenerateLayerName() const;

		// 선택 항목 Z-Order 변경 (각자의 부모 안에서)
		enum class ZOrder { Front, Back, Forward, Backward };
		void ArrangeSelection(ZOrder order);

		// 월드 좌표 바운딩 박스
		static bool GetWorldBounds(Node& node, Rect2& out);
		bool GetSelectionBounds(Rect2& out) const;

	// 정렬 / 분배 (선택 기준)
	enum class AlignMode { Left, CenterH, Right, Top, CenterV, Bottom };
	void AlignSelection(AlignMode mode, bool toCanvas);
	void DistributeSelection(bool horizontal);

	// 캔버스 크기
	float width  = 1920.0f;
	float height = 1080.0f;

	NodeId IssueId() { return ++m_nextId; }
		void   AssignIds(Node& node);   // id==0 인 노드(하위 포함)에 ID 발급

private:
	std::vector<std::unique_ptr<LayerNode>> m_layers;   // 0 = 최하단 레이어
	int                                      m_activeLayer = 0;
	std::vector<Node*>                       m_selection;
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
};

// 도형 변형 (선택 툴 / 속성창 / 메뉴 공용).
// 노드를 직접 바꾸지 않고 '새 변환 행렬'을 계산해 반환 -> 호출 측이 SetTransform() 하거나 Command 로 기록.
class Transformer {
public:
    // 1. 평행 이동
    static Matrix2D Translate(Vec2 deltaWorld, const Matrix2D& initialXf);
    static Matrix2D Translate(D2D1_POINT_2F deltaWorld, const D2D1_MATRIX_3X2_F& initialXf) {
        return Translate({ deltaWorld.x, deltaWorld.y }, Matrix2D::FromD2D(initialXf));
    }

    // 2. 바운딩 박스 8개 핸들 기준 스케일
    //    handleIdx: 1=TL, 2=T, 3=TR, 4=R, 5=BR, 6=B, 7=BL, 8=L
    //    실패(역행렬 불가, 크기 0 등) 시 nullopt
    static std::optional<Matrix2D> ScaleByHandle(int handleIdx, Vec2 curWorld,
                                                 const Rect2& initialLocalBounds,
                                                 const Matrix2D& initialXf);
    static std::optional<Matrix2D> ScaleByHandle(int handleIdx, D2D1_POINT_2F curWorld,
                                                 const D2D1_RECT_F& initialLocalBounds,
                                                 const D2D1_MATRIX_3X2_F& initialXf) {
        return ScaleByHandle(handleIdx, { curWorld.x, curWorld.y },
                             Rect2::FromD2D(initialLocalBounds),
                             Matrix2D::FromD2D(initialXf));
    }

    // 3. 중심점 기준 회전
    static Matrix2D RotateAboutCenter(Vec2 startWorld, Vec2 curWorld,
                                      const Rect2& initialLocalBounds,
                                      const Matrix2D& initialXf);
    static Matrix2D RotateAboutCenter(D2D1_POINT_2F startWorld, D2D1_POINT_2F curWorld,
                                      const D2D1_RECT_F& initialLocalBounds,
                                      const D2D1_MATRIX_3X2_F& initialXf) {
        return RotateAboutCenter({ startWorld.x, startWorld.y }, { curWorld.x, curWorld.y },
                                 Rect2::FromD2D(initialLocalBounds),
                                 Matrix2D::FromD2D(initialXf));
    }

    // 4. 로컬 바운딩 박스 기준 핸들 9개 좌표 (0~7: TL,T,TR,R,BR,B,BL,L / 8: 회전 핸들)
    static void GetLocalHandlePoints(const Rect2& b, Vec2 outHandles[9]);
    static void GetLocalHandlePoints(const D2D1_RECT_F& b, D2D1_POINT_2F outHandles[9]) {
        Vec2 handles[9];
        GetLocalHandlePoints(Rect2::FromD2D(b), handles);
        for (int i = 0; i < 9; ++i) outHandles[i] = handles[i].ToD2D();
    }
};

// ============================================================================
// 2.6 Path Operations / Pathfinder (불린 연산: 합치기 / 빼기 / 교차 / 제외)
// ============================================================================
class PathOp {
public:
	enum class BooleanMode {
		Union = 0,     // 합집합 (D2D1_COMBINE_MODE_UNION)
		Intersect,     // 교집합 (D2D1_COMBINE_MODE_INTERSECT)
		Subtract,      // 차집합 / 빼기 (D2D1_COMBINE_MODE_EXCLUDE: A에서 B 빼기)
		Exclude        // 배타적 논리합 / 겹친 곳 제외 (D2D1_COMBINE_MODE_XOR)
	};

	// 두 PathNode의 지오메트리를 불린 연산하여 결과 정점을 담은 새 PathNode를 생성해 반환
	// 실패하거나 겹치지 않는 경우 nullopt 또는 nullptr 반환
	static std::unique_ptr<PathNode> Combine(const PathNode& a, const PathNode& b,
	                                         BooleanMode mode, ID2D1Factory* factory);
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
		void ClearCache() { m_geoCache.clear(); }
		ID2D1Factory* GetFactory() const { return m_factory; }

private:
	void DrawPath(PathNode& node, const Matrix2D& worldXf, const Viewport& view);
	void DrawGroup(GroupNode& node, const Matrix2D& worldXf, const Viewport& view);
	ComPtr<ID2D1PathGeometry> BuildGeometry(const PathNode& node);

	ID2D1RenderTarget*             m_rt      = nullptr;   // 비소유
	ID2D1Factory*                  m_factory = nullptr;   // 비소유
	ComPtr<ID2D1SolidColorBrush>   m_brush;
	// PathNode 별 지오메트리 캐시
	struct GeoCache { uint32_t version = 0; ComPtr<ID2D1PathGeometry> geo; };
		ComPtr<ID2D1StrokeStyle> m_strokeStyles[5];   // DashStyle 별 스트로크 스타일
	std::unordered_map<NodeId, GeoCache> m_geoCache;
};

// ============================================================================
// 4. HitTester
// ============================================================================

class HitTester {
public:
	// 최상단 -> 최하단 역순으로 순회, 숨김/잠금 레이어 제외
	static HitResult HitTest(Document& doc, Vec2 screenPt, float tolPx,
							 const Viewport& view, D2DRenderer& renderer);

								 // 히트된 노드가 속한 '레이어 바로 아래' 최상위 노드 (그룹 단위 선택용)
								 static Node* FindSelectable(Node* hit);
};

// ============================================================================
// 5. Command (Undo / Redo)
// ============================================================================
class ICommand {
public:
	virtual ~ICommand() = default;
	virtual void Execute(Document& doc) = 0;
	virtual void Undo(Document& doc) = 0;
	virtual const wchar_t* GetName() const { return L"Command"; }
};

class CommandStack {
public:
	void Execute(std::unique_ptr<ICommand> cmd, Document& doc);
	bool CanUndo() const { return !m_undo.empty(); }
	bool CanRedo() const { return !m_redo.empty(); }
	void Undo(Document& doc);
	void Redo(Document& doc);
	void Clear();

private:
	std::vector<std::unique_ptr<ICommand>> m_undo;
	std::vector<std::unique_ptr<ICommand>> m_redo;
};

} // namespace ge
