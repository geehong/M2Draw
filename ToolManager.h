#pragma once
#include <d2d1.h>
#include <vector>
#include <memory>
#include <set>
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
	Polygon     // G (다각형 프리셋)
};

// 도구 인터페이스
class ITool
{
public:
	virtual ~ITool() = default;

	virtual ToolType GetType() const = 0;
	virtual const wchar_t* GetName() const = 0;
	virtual wchar_t GetShortcut() const = 0;
	virtual HCURSOR GetCursor() const { return ::LoadCursor(nullptr, IDC_ARROW); }

	virtual void OnDown(const ToolEvent& e) {}
	virtual void OnMove(const ToolEvent& e) {}
	virtual void OnUp(const ToolEvent& e) {}
	virtual void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) {}
	virtual void OnKeyUp(UINT nChar, CM2DrawDoc* pDoc) {}

	// 오버레이 및 미리보기 그리기
	virtual void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) {}
};

// 도구별 클래스 선언
class SelectTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Select; }
	const wchar_t* GetName() const override { return L"선택 도구 (Select)"; }
	wchar_t GetShortcut() const override { return L'V'; }
	enum class Mode { None, Moving, Scaling, Rotating };
	enum class Handle { None, TL, T, TR, R, BR, B, BL, L, Rotate };

	HCURSOR GetCursor() const override;

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	int GetSelectedIndex() const { return m_selectedIndices.empty() ? -1 : *m_selectedIndices.begin(); }
	void SetSelectedIndex(int index)
	{
		m_selectedIndices.clear();
		if (index >= 0) m_selectedIndices.insert(index);
	}
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

	Handle HitHandle(D2D1_POINT_2F screenPt, const D2D1_POINT_2F handles[9], float tolPx) const;
	bool GetHandleScreenPositions(Shape* shape, const ViewportTransform& view, ID2D1Factory* f, D2D1_POINT_2F outHandles[9], D2D1_RECT_F* outLocalBounds = nullptr) const;
	void ApplyMove(Shape* shape);
	void ApplyScale(Shape* shape);
	void ApplyRotate(Shape* shape);
};

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
};

class PenTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Pen; }
	const wchar_t* GetName() const override { return L"펜 / 베이지어 도구 (Pen)"; }
	wchar_t GetShortcut() const override { return L'P'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;
};

class LineTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Line; }
	const wchar_t* GetName() const override { return L"직선 도구 (Line)"; }
	wchar_t GetShortcut() const override { return L'L'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

private:
	bool          m_dragging = false;
	D2D1_POINT_2F m_startPt{};
	D2D1_POINT_2F m_currentPt{};
};

class RectTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Rect; }
	const wchar_t* GetName() const override { return L"사각형 도구 (Rectangle)"; }
	wchar_t GetShortcut() const override { return L'M'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

private:
	bool          m_dragging = false;
	D2D1_POINT_2F m_startPt{};
	D2D1_POINT_2F m_currentPt{};
};

class EllipseTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Ellipse; }
	const wchar_t* GetName() const override { return L"타원 도구 (Ellipse)"; }
	wchar_t GetShortcut() const override { return L'E'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

private:
	bool          m_dragging = false;
	D2D1_POINT_2F m_startPt{};
	D2D1_POINT_2F m_currentPt{};
};

class PolygonTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Polygon; }
	const wchar_t* GetName() const override { return L"다각형 도구 (Polygon)"; }
	wchar_t GetShortcut() const override { return L'G'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;
};

// 도구 변경 리스너
class IToolChangeListener
{
public:
	virtual ~IToolChangeListener() = default;
	virtual void OnToolChanged(ToolType newTool) = 0;
};

// 도구 관리자
class ToolManager
{
private:
	ToolType m_activeType;
	ToolType m_prevType;
	bool     m_isTempPan;
	std::vector<std::unique_ptr<ITool>> m_tools;
	std::vector<IToolChangeListener*>   m_listeners;

public:
	ToolManager();

	void SetActiveTool(ToolType type);
	ToolType GetActiveType() const { return m_activeType; }
	ITool* GetActiveTool();

	void BeginTempPan();
	void EndTempPan();

	void AddListener(IToolChangeListener* pListener);
	void RemoveListener(IToolChangeListener* pListener);

	bool HandleShortcut(wchar_t key);
	SelectTool* GetSelectTool();
};
