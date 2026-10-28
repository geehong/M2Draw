#pragma once

#include <d2d1.h>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include "D2DGraphicEngine.h"

class CM2DrawDoc;
class CM2DrawView;

using ViewportTransform = ge::Viewport;
using CoordinateSpace = ge::CoordinateSpace;
using HitResult = ge::HitResult;

using ShapeKind = ge::ShapeKind;
using RectParams = ge::RectParams;
using TriangleParams = ge::TriangleParams;
using EllipseParams = ge::EllipseParams;
using PolygonParams = ge::PolygonParams;
using StarParams = ge::StarParams;
using GearParams = ge::GearParams;
using SpiralParams = ge::SpiralParams;
using HeartParams = ge::HeartParams;
using ShapeParams = ge::ShapeParams;
using BezierNode = ge::BezierNode;
using SubPath = ge::SubPath;

// 도구 이벤트 데이터 구조체
struct ToolEvent
{
	CPoint             screenPt;   // 뷰 윈도우 픽셀 좌표
	D2D1_POINT_2F      worldPt;    // 캔버스 월드 좌표 (px)
	D2D1_POINT_2F      unitPt;     // 현재 선택된 단위 기준 좌표 (mm, in 등)
	UINT               flags;      // MK_LBUTTON, MK_CONTROL, MK_SHIFT 등
	bool               altDown;
	bool               shiftDown;
	bool               ctrlDown;
	ge::Viewport       view;       // 현재 뷰포트 상태
	CM2DrawDoc*        pDoc;       // 현재 활성 문서 포인터
	CM2DrawView*       pView;      // 현재 활성 뷰 포인터 (줌/팬 제어용)
	ID2D1Factory*      pD2DFactory;// Direct2D 팩토리

	ToolEvent()
		: screenPt(0, 0)
		, worldPt(D2D1::Point2F(0, 0))
		, unitPt(D2D1::Point2F(0, 0))
		, flags(0)
		, altDown(false)
		, shiftDown(false)
		, ctrlDown(false)
		, pDoc(nullptr)
		, pView(nullptr)
		, pD2DFactory(nullptr)
	{
	}
};

// 도구 종류 열거형
enum class ToolType
{
	Select = 0, // V (선택/이동)
	Pan,        // H / Space (패닝/핸드)
	Pen,        // P (펜/베이지어 곡선)
	Line,       // L (직선 프리셋)
	Rect,       // M (사각형)
	Ellipse,    // E (타원/원)
	Polygon,    // G (다각형 프리셋)
	Triangle,   // 삼각형
	Star,       // 별
	Gear,       // 기어
	Spiral,     // 스파이럴(나선)
	Heart,      // 하트
	Text,       // T (텍스트 도구)
	PaintBucket,// K (페인트/채우기 도구)
	ZoomIn,     // Z (확대 도구)
	ZoomOut,    // X (축소 도구)
	Gradient,   // D (그라디언트 도구) - 값은 앱 설정에 저장되는 기존 도구 값이 바뀌지 않도록 맨 끝에 추가
	// 아래 둘은 도구가 아니라 도구상자의 "동작" 항목(그룹/그룹 해제)을 플라이아웃 그룹에 담기 위한 표시용 값. 활성 도구가 되지 않음
	ActionGroup,
	ActionUngroup,
	ActionToFront,   // 순서: 맨 앞으로
	ActionForward,   // 앞으로
	ActionBackward,  // 뒤로
	ActionToBack     // 맨 뒤로
};

// 도구 생성 설정값 (속성창 및 문서와 연동되는 공통 스타일/도형 설정)
struct ToolCreationSetting
{
	bool            bClosed = false;          // 폐곡선(닫힘) 여부 (기본: 열림)
	ge::Style       style;                    // 새 도형의 채우기/선 스타일 (기본: 채우기 없음, DodgerBlue 2px 실선)

	// 도형별 파라미터 (상단 ToolOptionBar 연동)
	float cornerRx = 0.0f;       // 사각형 모서리 Rx
	float cornerRy = 0.0f;       // 사각형 모서리 Ry
	bool  bLinkRxRy = true;      // Rx=Ry 비율 고정
	int   polySides = 5;         // 정다각형 변의 개수 (5, 6, 8 등)
	int   starPoints = 5;        // 별 꼭짓점 수
	float starInnerRatio = 0.5f; // 별 내경 비율 (0.1 ~ 0.9)
	int   gearTeeth = 12;        // 기어 톱니 수
	float gearDepthRatio = 0.2f; // 기어 톱니 깊이 비율
	float gearHoleRatio = 0.3f;  // 기어 중앙 구멍 비율
	float spiralTurns = 3.0f;    // 나선 회전 바퀴 수
	float heartRound = 1.0f;     // 하트 곡률 계수
	bool  bDrawFromCenter = false; // 중심 => 바깥쪽 그리기 모드

	ge::Style ToGeStyle() const { return style; }
	void FromGeStyle(const ge::Style& st) { style = st; }
};

// ============================================================================
// 1. 최상위 도구 인터페이스 (ITool)
// ============================================================================
class ITool
{
public:
	virtual ~ITool() = default;

	virtual ToolType GetType() const = 0;
	virtual const wchar_t* GetName() const = 0;
	virtual wchar_t GetShortcut() const = 0;
	virtual HCURSOR GetCursor() const { return ::LoadCursor(nullptr, IDC_ARROW); }

	// 기본 마우스/키보드 이벤트
	virtual void OnDown(const ToolEvent& e) {}
	virtual void OnMove(const ToolEvent& e) {}
	virtual void OnUp(const ToolEvent& e) {}
	virtual void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) {}
	virtual void OnKeyUp(UINT nChar, CM2DrawDoc* pDoc) {}

	// 현재 도형 작성 중인지 여부 (미리보기 도형 또는 점 목록 존재 여부)
	virtual bool HasActiveShape() const { return false; }

	// 신호 인터페이스: 뷰(View)에서 더블클릭이나 우클릭이 발생했을 때
	// true를 반환하면 "도형 완료 후 Select 툴로 전환" 신호
	virtual bool OnDoubleClicked(CM2DrawDoc* pDoc) { return false; }
	virtual bool OnRightClicked(CM2DrawDoc* pDoc) { return false; }

	// 오버레이 및 미리보기 렌더링
	virtual void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                         float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) {}
};

// ============================================================================
// 2. 선택 및 변형 도구 (SelectTool)
// ============================================================================
class SelectTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Select; }
	const wchar_t* GetName() const override { return L"선택 도구 (Select)"; }
	wchar_t GetShortcut() const override { return L'V'; }
	enum class Mode { None, Moving, Scaling, Rotating, EditNode, Marquee, GradientHandle };
	enum class Handle { None, TL, T, TR, R, BR, B, BL, L, Rotate };

	HCURSOR GetCursor() const override;

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	// 선택 상태의 단일 출처는 ge::Document::GetSelection() (SelectTool 은 별도 선택 상태를 갖지 않음)
	// 단일 선택 (nullptr = 해제). 노드가 속한 레이어를 활성화하고 속성창/옵션바/레이어 패널에 반영
	void SelectNode(ge::Node* node, CM2DrawDoc* pDoc);
	// 문서 선택만 해제 (패널 갱신은 호출 측 책임)
	void ClearSelection(CM2DrawDoc* pDoc);
	// 진행 중인 드래그/편집 상태 폐기 (문서가 교체되어 노드 포인터가 무효가 된 경우)
	void ResetInteraction() { m_mode = Mode::None; m_handle = Handle::None; m_editShape = nullptr; m_xfTarget = nullptr; m_initialXfs.clear(); m_hoverIsNode = false; m_hoverOnShape = false; m_gradShape = nullptr; m_gradHandle = GradHandle::None; }

	Handle GetActiveHandle() const { return m_handle; }
	void SetActiveHandle(Handle h) { m_handle = h; }

private:
	Mode   m_mode = Mode::None;
	Handle m_handle = Handle::None;
	Handle m_hoverHandle = Handle::None;

	D2D1_POINT_2F m_dragStartWorld = { 0.0f, 0.0f };
	D2D1_POINT_2F m_dragCurrentWorld = { 0.0f, 0.0f };
	D2D1::Matrix3x2F m_initialXf = D2D1::Matrix3x2F::Identity();
	std::map<ge::Node*, D2D1::Matrix3x2F> m_initialXfs; // 다중 선택 이동을 위한 도형별 초기 변환 행렬 (드래그 동안만 유효)
	D2D1_RECT_F   m_initialLocalBounds = { 0.0f, 0.0f, 0.0f, 0.0f };
	ge::Node*     m_xfTarget = nullptr;   // 스케일/회전 핸들을 잡은 도형 (드래그 동안만 유효)

	// 노드(앵커/핸들) 편집 상태 (열린 패스: 직선, 곡선 등) — 드래그 동안만 유효
	ge::PathNode*     m_editShape = nullptr;
	HitResult      m_editHit;
	bool           m_hoverIsNode = false;
	bool           m_hoverOnShape = false; // 스트로크/면 위에 마우스가 올라가 있는지

	// 그라디언트 기즈모 (선택된 단일 도형의 그라디언트 시작/끝/중심/초점/반지름 핸들) — 드래그 동안만 유효
	enum class GradHandle { None, Start, End, Radius };   // 방사형: Start=중심, End=초점
	GradHandle        m_gradHandle = GradHandle::None;
	ge::PathNode*     m_gradShape = nullptr;
	ge::Rect2         m_gradBounds{};                      // 드래그 시작 시점의 지오메트리 경계 (드래그 중 변하지 않음)
	void ApplyGradientDrag(const ToolEvent& e);

	static bool IsOpenPath(ge::Node* shape);
	void ApplyNodeEdit(ge::PathNode* shape, const ToolEvent& e);

	Handle HitHandle(D2D1_POINT_2F screenPt, const D2D1_POINT_2F handles[9], float tolPx) const;
	bool GetHandleScreenPositions(ge::Node* shape, const ViewportTransform& view, ID2D1Factory* f, D2D1_POINT_2F outHandles[9], D2D1_RECT_F* outLocalBounds = nullptr) const;
	void ApplyMove(const std::vector<ge::Node*>& selection);
	void ApplyScale(ge::Node* shape);
	void ApplyRotate(ge::Node* shape);
};

// ============================================================================
// 2-2. 그라디언트 도구 (GradientTool) - 일러스트레이터/잉크스케이프식 전통적인 그라디언트 도구
//  - 도형을 클릭해 선택, 선택된 도형 위(또는 어디서든)에서 드래그하면 시작점 -> 끝점으로 그라디언트를 만들거나 방향을 다시 지정
//    (선형: 드래그 방향이 그라디언트 방향, 방사형: 드래그 시작점이 중심, 거리가 반지름)
//  - 기즈모 핸들(시작/끝/반지름/초점)을 드래그해 조정, 축 위를 클릭하면 컬러 스톱 추가,
//    스톱 점을 드래그하면 위치 이동, Alt+클릭하면 스톱 삭제 (최소 2개 유지)
//  - 더블클릭: 선택된 도형에 기본 그라디언트 적용(이미 있으면 기본 방향으로 초기화)
//  - 그룹 안쪽 도형도 직접 집을 수 있음 (선택 도구와 달리 그룹 단위가 아님)
// ============================================================================
class GradientTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Gradient; }
	const wchar_t* GetName() const override { return L"그라디언트 도구 (Gradient)"; }
	wchar_t GetShortcut() const override { return L'D'; }   // G 는 정다각형 도구가 사용 중
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	bool OnDoubleClicked(CM2DrawDoc* pDoc) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	// 새로 만드는 그라디언트의 종류 (옵션바의 Linear / Radial 토글)
	ge::Gradient::Kind GetKind() const { return m_kind; }
	void SetKind(ge::Gradient::Kind k) { m_kind = k; }

	// 진행 중인 드래그 폐기 (문서가 교체되어 노드가 무효가 된 경우)
	void ResetInteraction() { m_drag = Drag::None; m_shapeId = 0; m_part = 0; m_stop = -1; m_activeStop = -1; }

private:
	enum class Drag { None, Create, Part, Stop };

	Drag               m_drag = Drag::None;
	ge::NodeId         m_shapeId = 0;                 // 드래그 대상 도형 (포인터 대신 ID: Undo 로 문서가 바뀌어도 안전)
	ge::Rect2          m_bounds{};                    // 드래그 시작 시점의 지오메트리 경계
	int                m_part = 0;                    // Part 드래그: 1=시작/중심, 2=끝/초점, 3=반지름
	int                m_stop = -1;                   // Stop 드래그 중인 스톱 인덱스
	int                m_activeStop = -1;             // 마지막으로 조작한 스톱 (오버레이 강조용)
	D2D1_POINT_2F      m_downScreen{ 0.0f, 0.0f };
	D2D1_POINT_2F      m_curScreen{ 0.0f, 0.0f };
	D2D1_POINT_2F      m_axisA{ 0.0f, 0.0f }, m_axisB{ 0.0f, 0.0f };   // 스톱 드래그 중 기즈모 축(화면 좌표)
	bool               m_moved = false;
	ge::Gradient::Kind m_kind = ge::Gradient::Kind::Linear;
};

// ============================================================================
// 3. 도형 생성 도구 공통 기저 클래스 (BaseShapeTool)
// ============================================================================
class BaseShapeTool : public ITool
{
protected:
	ToolCreationSetting            m_setting;
	D2D1_POINT_2F                  m_hoverPt{ 0.0f, 0.0f };
	bool                           m_hasHover = false;
	std::unique_ptr<ge::PathNode>     m_previewShape; // 구형 미리보기 도형
	std::unique_ptr<ge::PathNode>  m_previewNode;  // 신엔진 미리보기 노드
	ge::NodeId                     m_lastCommittedId = 0;     // 방금 완료된 도형 NodeId (대기 상태에서 앵커 표시)

public:
	void SetSetting(const ToolCreationSetting& s) { m_setting = s; }
	const ToolCreationSetting& GetSetting() const { return m_setting; }

	void OnMove(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                 float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	// 현재 도형 작성 중인지 여부 (미리보기 도형 또는 점 목록 존재 여부)
	virtual bool HasActiveShape() const { return m_previewShape != nullptr || m_previewNode != nullptr; }

	// 도형 확정 및 문서 등록
	bool CommitShape(CM2DrawDoc* pDoc);
	void CancelShape();

	// 더블클릭 및 우클릭 시: 도형을 확정하고 Select 전환(true) 신호 반환
	bool OnDoubleClicked(CM2DrawDoc* pDoc) override;
	bool OnRightClicked(CM2DrawDoc* pDoc) override;

protected:
	// 마우스 이동 시 기하학을 갱신하는 훅
	virtual void UpdatePreviewGeometry(D2D1_POINT_2F curPt) {}

	// 공통 헬퍼: 십자선 및 앵커 포인트 오버레이 렌더링
	void DrawCrosshairAndAnchor(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                            D2D1_POINT_2F screenPt);

	// 확정된 최근 도형의 앵커/핸들 표시 헬퍼
	void DrawCommittedShapeAnchors(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                              const ViewportTransform& view, CM2DrawDoc* pDoc);
};

// ============================================================================
// 3. 단일 클래스 인스턴스 툴 (BoxShapeTool: 사각형, 원, 직선, 삼각, 별 등)
//    - 사각/원/삼각/직선 등 모든 2포인트 바운딩 도형을 이 단 하나의 클래스로 처리!
// ============================================================================
class BoxShapeTool : public BaseShapeTool
{
public:
	enum class ShapeKind
	{
		Line,
		Rect,
		Ellipse,
		Triangle,
		Star,
		Gear,
		Spiral,
		Heart,
		RegularPoly
	};

protected:
	ShapeKind      m_kind;
	ToolType       m_toolType;
	const wchar_t* m_pName;
	wchar_t        m_shortcut;

	D2D1_POINT_2F  m_startPt{ 0.0f, 0.0f };
	bool           m_isDragging = false;
	bool           m_waitingSecondClick = false; // 클릭-이동-클릭 모드 플래그

public:
	BoxShapeTool(ShapeKind kind, ToolType toolType, const wchar_t* pName, wchar_t shortcut)
		: m_kind(kind)
		, m_toolType(toolType)
		, m_pName(pName)
		, m_shortcut(shortcut)
	{
	}

	ToolType GetType() const override { return m_toolType; }
	const wchar_t* GetName() const override { return m_pName; }
	wchar_t GetShortcut() const override { return m_shortcut; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	// 1포인트 클릭: 시작 위치 기록 & 엔진 인스턴스 생성
	void OnDown(const ToolEvent& e) override;

	// 마우스 이동: 실시간 옵션바 좌표/크기 반영 및 미리보기 갱신
	void OnMove(const ToolEvent& e) override;

	// 2포인트(마우스 업): 도형 완성 및 등록 (단, 도구는 유지되어 연속 그리기 지원!)
	void OnUp(const ToolEvent& e) override;

	void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) override;

	// 더블클릭 시: 작성 중인 도형이 있으면 확정 등록하고 선택 도구로 전환
	bool OnDoubleClicked(CM2DrawDoc* pDoc) override
	{
		if (m_waitingSecondClick && m_previewShape)
		{
			CommitShape(pDoc);
		}
		CancelShape();
		m_isDragging = false;
		m_waitingSecondClick = false;
		return true; // 우클릭과 동일하게 선택 툴(Select)로 즉시 전환
	}

	// 시작 앵커, 이동 중인 2번째 앵커 및 미리보기 도형 오버레이 렌더링
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                 float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	// 우클릭 시에만 선택 툴로 전환 신호 반환
	bool OnRightClicked(CM2DrawDoc* pDoc) override
	{
		CancelShape();
		m_isDragging = false;
		m_waitingSecondClick = false;
		return true;
	}

protected:
	void UpdatePreviewGeometry(D2D1_POINT_2F curPt) override;

	// 중심/모서리 시작 및 Shift/Alt에 따른 외곽 정점(p0, p1) 계산
	void CalculateExtents(D2D1_POINT_2F curPt, bool shiftDown, bool altDown, D2D1_POINT_2F& outP0, D2D1_POINT_2F& outP1) const;

	// 엔진 기하학 팩토리/업데이터 (정점 계산)
	std::unique_ptr<ge::PathNode> CreateShapeInstance(D2D1_POINT_2F p0, D2D1_POINT_2F p1, CM2DrawDoc* pDoc = nullptr);
	void UpdateGeometry(ge::PathNode* pShape, D2D1_POINT_2F p0, D2D1_POINT_2F p1);
};

// ============================================================================
// 4. 멀티 포인트 클릭 생성 툴 (MultiPointShapeTool: 다각형, 열린 폴리라인 등)
//    - 점을 계속 추가해 나가며, Enter / 첫점 스냅 / 더블클릭 / 우클릭으로 완성하는 도구!
// ============================================================================
class MultiPointShapeTool : public BaseShapeTool
{
protected:
	ToolType       m_toolType;
	const wchar_t* m_pName;
	wchar_t        m_shortcut;

public:
	MultiPointShapeTool(ToolType toolType, const wchar_t* pName, wchar_t shortcut)
		: m_toolType(toolType)
		, m_pName(pName)
		, m_shortcut(shortcut)
	{
		// 다각형(Polygon) 도구는 기본적으로 닫힌 도형(폐곡선)으로 설정
		if (m_toolType == ToolType::Polygon)
		{
			m_setting.bClosed = true;
		}
	}

	ToolType GetType() const override { return m_toolType; }
	const wchar_t* GetName() const override { return m_pName; }
	wchar_t GetShortcut() const override { return m_shortcut; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	// 점 클릭마다 정점 추가 & 첫점 스냅 시 폐합 완료
	void OnDown(const ToolEvent& e) override;

	// Enter 키로 완료 (설정에 따라 열림/닫힘 처리), Esc 키로 취소
	void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) override;

	// 더블클릭 및 우클릭 시: 현재까지의 정점으로 도형을 완성(최소 2점 이상일 때)하고 Select로 전환
	bool OnDoubleClicked(CM2DrawDoc* pDoc) override;
	bool OnRightClicked(CM2DrawDoc* pDoc) override;

	// 실시간 연결선, 가상 폐합선, 확정 앵커 박스 오버레이 렌더링
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                 float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;
};

// ============================================================================
// 5. 손 / 화면 이동 도구 (PanTool)
//    - 마우스 좌클릭 드래그로 캔버스(월드 좌표계)를 패닝 이동
// ============================================================================
class PanTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Pan; }
	const wchar_t* GetName() const override { return L"손 도구 (Pan)"; }
	wchar_t GetShortcut() const override { return L'H'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_SIZEALL); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;

private:
	bool   m_isPanning = false;
	CPoint m_lastScreenPt{ 0, 0 };
};

// ============================================================================
// 6. 돋보기 도구 (ZoomTool / ZoomInTool / ZoomOutTool)
//    - 클릭 시 클릭 지점 기준 Zoom In/Out
//    - 드래그 시 지정 영역을 화면 가득 맞춤 확대 (Marquee Zoom)
// ============================================================================
class ZoomInTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::ZoomIn; }
	const wchar_t* GetName() const override { return L"돋보기 확대 도구 (Zoom In)"; }
	wchar_t GetShortcut() const override { return L'Z'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                 float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

private:
	bool   m_isDragging = false;
	CPoint m_dragStartScreen{ 0, 0 };
	CPoint m_dragCurScreen{ 0, 0 };
};

class ZoomOutTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::ZoomOut; }
	const wchar_t* GetName() const override { return L"돋보기 축소 도구 (Zoom Out)"; }
	wchar_t GetShortcut() const override { return L'X'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
};



