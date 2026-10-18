#pragma once

#include <d2d1.h>
#include <vector>
#include <set>
#include <memory>
#include "D2DVectorEngine.h"

class CM2DrawDoc;

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
	ViewportTransform  view;       // 현재 뷰포트 상태
	CM2DrawDoc*        pDoc;       // 현재 활성 문서 포인터
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
	Text,       // T (텍스트 도구)
	PaintBucket,// K (페인트/채우기 도구)
	ZoomIn,     // Z (확대 도구)
	ZoomOut     // X (축소 도구)
};

// 도구 생성 설정값 (속성창 및 문서와 연동되는 공통 스타일/도형 설정)
struct ToolCreationSetting
{
	bool            bClosed = false;          // 폐곡선(닫힘) 여부 (기본: 열림)
	FillType        fillType = FillType::None;
	D2D1_COLOR_F    strokeColor = D2D1::ColorF(D2D1::ColorF::DodgerBlue);
	D2D1_COLOR_F    fillColor   = D2D1::ColorF(0.2f, 0.6f, 1.0f, 0.25f);
	float           strokeWidth = 2.0f;
	D2D1_DASH_STYLE dashStyle = D2D1_DASH_STYLE_SOLID;
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
	enum class Mode { None, Moving, Scaling, Rotating, EditNode };
	enum class Handle { None, TL, T, TR, R, BR, B, BL, L, Rotate };

	HCURSOR GetCursor() const override;

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	int GetSelectedIndex() const { return m_selectedIndices.empty() ? -1 : *m_selectedIndices.begin(); }
	void SetSelectedIndex(int index, CM2DrawDoc* pDoc = nullptr);
	const std::set<int>& GetSelectedIndices() const { return m_selectedIndices; }
	std::set<int>& GetSelectedIndices() { return m_selectedIndices; }
	void ClearSelection() { m_selectedIndices.clear(); }

	Handle GetActiveHandle() const { return m_handle; }
	void SetActiveHandle(Handle h) { m_handle = h; }

private:
	std::set<int> m_selectedIndices;
	Mode   m_mode = Mode::None;
	Handle m_handle = Handle::None;
	Handle m_hoverHandle = Handle::None;

	D2D1_POINT_2F m_dragStartWorld = { 0.0f, 0.0f };
	D2D1_POINT_2F m_dragCurrentWorld = { 0.0f, 0.0f };
	D2D1::Matrix3x2F m_initialXf = D2D1::Matrix3x2F::Identity();
	D2D1_RECT_F   m_initialLocalBounds = { 0.0f, 0.0f, 0.0f, 0.0f };

	// 노드(앵커/핸들) 편집 상태 (열린 패스: 직선, 곡선 등)
	int            m_editShapeIdx = -1;
	HitResult      m_editHit;
	bool           m_hoverIsNode = false;

	static bool IsOpenPath(Shape* shape);
	void ApplyNodeEdit(PathShape* shape, const ToolEvent& e);

	Handle HitHandle(D2D1_POINT_2F screenPt, const D2D1_POINT_2F handles[9], float tolPx) const;
	bool GetHandleScreenPositions(Shape* shape, const ViewportTransform& view, ID2D1Factory* f, D2D1_POINT_2F outHandles[9], D2D1_RECT_F* outLocalBounds = nullptr) const;
	void ApplyMove(Shape* shape);
	void ApplyScale(Shape* shape);
	void ApplyRotate(Shape* shape);
};

// ============================================================================
// 3. 도형 생성 도구 공통 기저 클래스 (BaseShapeTool)
// ============================================================================
class BaseShapeTool : public ITool
{
protected:
	ToolCreationSetting         m_setting;
	D2D1_POINT_2F               m_hoverPt{ 0.0f, 0.0f };
	bool                        m_hasHover = false;
	std::unique_ptr<PathShape>  m_previewShape; // 엔진이 계산하고 렌더링할 미리보기 도형
	int                         m_lastCommittedIndex = -1; // 방금 완료된 도형 인덱스 (대기 상태에서 앵커 표시)

public:
	void SetSetting(const ToolCreationSetting& s) { m_setting = s; }
	const ToolCreationSetting& GetSetting() const { return m_setting; }

	void OnMove(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                 float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	// 현재 도형 작성 중인지 여부 (미리보기 도형 또는 점 목록 존재 여부)
	virtual bool HasActiveShape() const { return m_previewShape != nullptr; }

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
		Ellipse
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

	// 2포인트(마우스 업): 도형 완성 및 등록 (단, 도구는 유지되어 연속 그리기 지원!)
	void OnUp(const ToolEvent& e) override;

	void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) override;

	// 2포인트 도형은 드래그로 이미 완성되므로 더블클릭 시 툴을 바꾸지 않고 연속 그리기 유지
	bool OnDoubleClicked(CM2DrawDoc* pDoc) override { return false; }

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

	// 엔진 기하학 팩토리/업데이터 (정점 계산)
	std::unique_ptr<PathShape> CreateShapeInstance(D2D1_POINT_2F p0, D2D1_POINT_2F p1);
	void UpdateGeometry(PathShape* pShape, D2D1_POINT_2F p0, D2D1_POINT_2F p1);
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
