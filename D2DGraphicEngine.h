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

namespace ge {

using Microsoft::WRL::ComPtr;

// ============================================================================
// 1. Math
// ============================================================================
struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

struct Rect2 {
	float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
	float Width()  const { return right - left; }
	float Height() const { return bottom - top; }
	float MidX()   const { return (left + right) * 0.5f; }
	float MidY()   const { return (top + bottom) * 0.5f; }
	bool  IsEmpty() const { return right <= left || bottom <= top; }
	void  Union(const Rect2& o);                 // 두 박스 합집합
};

// D2D1::Matrix3x2F 와 동일한 레이아웃/곱셈 순서: (P * A) * B
struct Matrix2D {
	float m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;

	static Matrix2D Identity() { return {}; }
	static Matrix2D Translation(float x, float y);
	static Matrix2D Scale(float sx, float sy, Vec2 center = {});
	static Matrix2D Rotation(float degree, Vec2 center = {});

	Matrix2D operator*(const Matrix2D& rhs) const;
	bool     Invert();                           // 실패 시 false
	Vec2     TransformPoint(Vec2 p) const;
	Rect2    TransformRect(const Rect2& r) const; // AABB 반환
};

// 뷰포트 (기존 ViewportTransform 대응)
struct Viewport {
	float zoom = 1.0f;
	Vec2  pan{};
	Matrix2D ToMatrix() const;
	Vec2 ScreenToWorld(Vec2 screenPt) const;
	Vec2 WorldToScreen(Vec2 worldPt) const;
};

// ============================================================================
// 2. Model (Scene Graph) - 렌더링 API에 의존하지 않는 순수 데이터
// ============================================================================
struct Color { float r = 0, g = 0, b = 0, a = 1; };

enum class FillType : uint8_t { None = 0, Solid = 1, Pattern = 2 };
enum class DashStyle : uint8_t { Solid = 0, Dash, Dot, DashDot, DashDotDot };
enum class FillMode  : uint8_t { Alternate = 0, Winding };

struct Style {
	FillType  fillType    = FillType::None;
	Color     fill        = { 0.2f, 0.6f, 1.0f, 0.25f };
	Color     stroke      = { 0.12f, 0.56f, 1.0f, 1.0f };
	float     strokeWidth = 2.0f;
	DashStyle dashStyle   = DashStyle::Solid;
	FillMode  fillMode    = FillMode::Alternate;
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
};

struct SubPath {
	std::vector<BezierNode> nodes;
	bool closed = false;
};

enum class NodeKind : uint8_t { Path, Group, Layer };

using NodeId = uint64_t;

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

	// 계층 구조
	NodeId            GetId() const          { return m_id; }
	GroupNode*        GetParent() const      { return m_parent; }

	// getter / setter
	const std::wstring& GetName() const      { return m_name; }
	void SetName(const std::wstring& v)      { m_name = v; }

	const Matrix2D& GetTransform() const     { return m_xf; }
	void SetTransform(const Matrix2D& v)     { m_xf = v; Invalidate(); }

	const Style& GetStyle() const            { return m_style; }
	Style&       GetStyleRef()               { return m_style; }
	void SetStyle(const Style& v)            { m_style = v; Invalidate(); }

	bool IsVisible() const                   { return m_visible; }
	void SetVisible(bool v)                  { m_visible = v; }
	bool IsLocked() const                    { return m_locked; }
	void SetLocked(bool v)                   { m_locked = v; }
	float GetOpacity() const                 { return m_opacity; }
	void SetOpacity(float v)                 { m_opacity = v; }

	void Invalidate()                        { m_dirty = true; }
	bool IsDirty() const                     { return m_dirty; }

	// 부모 체인을 곱한 월드 변환 (자신 포함)
	Matrix2D GetWorldTransform() const;

protected:
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

	const std::vector<SubPath>& GetPaths() const { return m_paths; }
	std::vector<SubPath>&       GetPathsRef()    { return m_paths; }
	void SetPaths(const std::vector<SubPath>& v) { m_paths = v; Invalidate(); }
	void AddAnchor(Vec2 localPt, bool forceNewSubPath = false);

private:
	std::vector<SubPath> m_paths;
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

// ---- 레이어 (최상위 그룹 + 속성) --------------------------------------------
class LayerNode : public GroupNode {
public:
	NodeKind Kind() const override { return NodeKind::Layer; }
	std::unique_ptr<Node> Clone() const override;
};

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

	// 정렬 / 분배 (선택 기준)
	enum class AlignMode { Left, CenterH, Right, Top, CenterV, Bottom };
	void AlignSelection(AlignMode mode, bool toCanvas);
	void DistributeSelection(bool horizontal);

	// 캔버스 크기
	float width  = 1920.0f;
	float height = 1080.0f;

	NodeId IssueId() { return ++m_nextId; }

private:
	std::vector<std::unique_ptr<LayerNode>> m_layers;   // 0 = 최하단 레이어
	int                                      m_activeLayer = 0;
	std::vector<Node*>                       m_selection;
	NodeId                                   m_nextId = 0;
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

private:
	void DrawPath(PathNode& node, const Matrix2D& worldXf, const Viewport& view);
	void DrawGroup(GroupNode& node, const Matrix2D& worldXf, const Viewport& view);
	ComPtr<ID2D1PathGeometry> BuildGeometry(const PathNode& node);

	ID2D1RenderTarget*             m_rt      = nullptr;   // 비소유
	ID2D1Factory*                  m_factory = nullptr;   // 비소유
	ComPtr<ID2D1SolidColorBrush>   m_brush;
	// PathNode 별 지오메트리 캐시
	struct GeoCache { NodeId id = 0; ComPtr<ID2D1PathGeometry> geo; };
	std::vector<GeoCache>          m_geoCache;
};

// ============================================================================
// 4. HitTester
// ============================================================================
struct HitResult {
	enum class Part { None, Fill, Stroke, Anchor, HandleIn, HandleOut } part = Part::None;
	Node* node    = nullptr;
	int   subPath = -1;
	int   nodeIdx = -1;
};

class HitTester {
public:
	// 최상단 -> 최하단 역순으로 순회, 숨김/잠금 레이어 제외
	static HitResult HitTest(Document& doc, Vec2 screenPt, float tolPx,
							 const Viewport& view, ID2D1Factory* factory);
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
