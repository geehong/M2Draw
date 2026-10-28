#pragma once
#include <afxwin.h>
#include <afxdockablepane.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <memory>
#include <unordered_set>
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"
#include "D2DColorWheel.h"
#include "M2DrawControl.h"

class CM2DrawDoc;
class CLayerWnd;

// ----------------------------------------------------------------------------
// COpacityPopupWnd: 트리 위로 완전히 독립적으로 뜨는 최상위 Direct2D 팝업 윈도우
// ----------------------------------------------------------------------------
class COpacityPopupWnd : public CWnd
{
public:
	COpacityPopupWnd();
	virtual ~COpacityPopupWnd();

	void ShowPopup(CPoint ptScreen, ge::Node* pTargetNode, CM2DrawDoc* pDoc, CWnd* pOwnerWnd);
	void HidePopup();

protected:
	ge::Node*              m_pTargetNode = nullptr;
	CM2DrawDoc*            m_pDoc = nullptr;
	CWnd*                  m_pOwnerWnd = nullptr;
	bool                   m_bDraggingSlider = false;

	ID2D1Factory*          m_pD2DFactory = nullptr;
	IDWriteFactory*        m_pDWriteFactory = nullptr;
	IDWriteTextFormat*     m_pTextFormat = nullptr;
	IDWriteTextFormat*     m_pTextFormatSmall = nullptr;
	ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
	ID2D1SolidColorBrush*  m_pBrush = nullptr;

	void InitD2D();
	void ReleaseD2D();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	DECLARE_MESSAGE_MAP()
};

class CLayerView : public CWnd
{
public:
	CLayerView();
	virtual ~CLayerView();

	void SetDocument(CM2DrawDoc* pDoc);
	void RefreshLayers();
	void SelectNode(ge::Node* pNode);
	void CommitEditName();
	void CancelEditName();

	void SafeInvalidate(BOOL bErase = FALSE)
	{
		HWND hWnd = GetSafeHwnd();
		if (hWnd && ::IsWindow(hWnd))
		{
			::InvalidateRect(hWnd, nullptr, bErase);
		}
	}

protected:
	struct LayerRowItem
	{
		ge::Node*                           pNode = nullptr;
		bool                                isLayer = false;
		int                                 depth = 0;           // 0: 레이어, 1+: 그룹/도형 (임의 깊이)
		bool                                hasChildren = false; // 접기/펼치기 화살표 표시 여부
		bool                                isExpanded = true;
		D2D1_RECT_F                         rowRect = { 0, 0, 0, 0 };
		D2D1_RECT_F                         expandRect = { 0, 0, 0, 0 };
		D2D1_RECT_F                         iconRect = { 0, 0, 0, 0 };
		D2D1_RECT_F                         labelRect = { 0, 0, 0, 0 };

		// M2DrawControl 커스텀 컨트롤들
		std::shared_ptr<M2DrawCheckBox>     chkVisible;   // [1. 👁/eye-closed 보기 아이콘 체크박스]
		std::shared_ptr<M2DrawButton>       btnOpacity;   // [2. 투명도 아이콘 버튼]
		std::shared_ptr<M2DrawButton>       btnBlendMode; // [3. 혼합 모드 아이콘 버튼 (레이어 전용)]
		std::shared_ptr<M2DrawCheckBox>     chkLocked;    // [4. 🔒/unlock 잠금 아이콘 체크박스]
	};

	// 이름 변경 더블클릭 대상 영역: 아이콘 ~ 라벨 ~ 첫 번째 버튼 직전까지
	static bool IsInNameArea(const LayerRowItem& row, D2D1_POINT_2F pt);
	// 인라인 이름 편집 시작 (편집 상자는 라벨 위치에서 110px 폭)
	void BeginEditName(ge::Node* pNode, const D2D1_RECT_F& labelRect, const D2D1_RECT_F& rowRect);
	// 편집 상자 숨김 (편집 상자가 포커스를 가지고 있으면 레이어 뷰로 포커스 반환)
	void HideEditBox();

	// 노드와 (펼쳐진) 후손의 행을 m_rowItems 에 재귀 추가
	void AppendNodeRows(ge::Node* node, int depth);

	CM2DrawDoc*                        m_pDoc = nullptr;
	std::vector<LayerRowItem>          m_rowItems;
	std::unordered_set<ge::NodeId>     m_collapsedIds;   // 접힌 레이어/그룹 (RefreshLayers 사이에 유지)
	std::shared_ptr<M2DrawControl>     m_capturedControl;

	// 상단 툴바 D2D 커스텀 버튼 (M2DrawButton)
	std::shared_ptr<M2DrawButton>      m_btnNewLayer;
	std::shared_ptr<M2DrawButton>      m_btnMoveUp;
	std::shared_ptr<M2DrawButton>      m_btnMoveDown;
	std::shared_ptr<M2DrawButton>      m_btnDelete;

	// 행 드래그로 순서/부모 변경 (포토샵/일러스트 스타일)
	enum class DropMode { None, Above, Below, Into };
	struct DropTarget
	{
		DropMode       mode = DropMode::None;
		int            row = -1;            // m_rowItems 인덱스 (표시선/강조 위치)
		ge::GroupNode* parent = nullptr;    // 노드 이동: 새 부모
		int            index = -1;          // 노드 이동: 부모 기준 삽입 위치 / 레이어 이동: 최종 레이어 인덱스
	};
	ge::Node*                          m_dragNode = nullptr;
	CPoint                             m_dragStart;
	CPoint                             m_dragPt;
	bool                               m_dragging = false;
	DropTarget                         m_drop;
	DropTarget ComputeDropTarget(const ge::Node* dragNode, D2D1_POINT_2F pt) const;
	void       PerformDrop(ge::Node* dragNode, const DropTarget& t);
	void       MoveSelectionOrLayer(bool up);   // ▲/▼ 버튼: 선택 오브젝트가 있으면 그 순서, 없으면 활성 레이어

	// 스크롤 및 레이아웃 상태
	float                              m_scrollY = 0.0f;
	float                              m_totalContentHeight = 0.0f;

	// 인라인 라벨 편집용 CEdit
	CEdit                              m_wndEditName;
	ge::Node*                          m_pEditingNode = nullptr;
	DWORD                              m_dwLastClickTime = 0;
	ge::Node*                          m_pLastClickedNode = nullptr;

	// 잉크스케이프 스타일 독립 플로팅 팝업 윈도우 (슬라이더 전용)
	COpacityPopupWnd                   m_wndOpacityPopup;

	// Direct2D / DirectWrite 장치 리소스
	ID2D1Factory*                      m_pD2DFactory = nullptr;
	ID2D1HwndRenderTarget*             m_pRenderTarget = nullptr;
	IDWriteFactory*                    m_pDWriteFactory = nullptr;
	IDWriteTextFormat*                 m_pTextFormat = nullptr;
	IDWriteTextFormat*                 m_pTextFormatBold = nullptr;
	IDWriteTextFormat*                 m_pTextFormatSmall = nullptr;
	ID2D1SolidColorBrush*              m_pBrush = nullptr;

	CM2DrawDoc* GetDocSafe();
	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();
	void UpdateLayout(float width, float height);
	void DrawNodeIcon(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& iconRect, ge::Node* pNode);

	// SVG 기반 고정밀 벡터 아이콘 드로우 헬퍼
	static void DrawIconTransparency(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, D2D1_COLOR_F clr);
	static void DrawIconBlendingMode(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, D2D1_COLOR_F clr);
	static void DrawIconEye(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, bool visible, D2D1_COLOR_F clr);
	static void DrawIconLock(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, bool locked, D2D1_COLOR_F clr);

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnEditKillFocus();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);

public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()
};

// ----------------------------------------------------------------------------
// 잉크스케이프 스타일 레이어/오브젝트 관리 패널 (Layers and Objects)
// - CDockablePane 컨테이너: CLayerView 자식 윈도우를 클라이언트 영역에 채워 도킹 프레임과 완벽 분리
// ----------------------------------------------------------------------------
class CLayerWnd : public CDockablePane
{
	DECLARE_DYNAMIC(CLayerWnd)
public:
	CLayerWnd() noexcept;
	virtual ~CLayerWnd();

	void SetDocument(CM2DrawDoc* pDoc)
	{
		m_wndLayerView.SetDocument(pDoc);
	}

	void RefreshLayers()
	{
		m_wndLayerView.RefreshLayers();
	}

	void SelectNode(ge::Node* pNode)
	{
		m_wndLayerView.SelectNode(pNode);
	}

	void CommitEditName()
	{
		m_wndLayerView.CommitEditName();
	}

	void CancelEditName()
	{
		m_wndLayerView.CancelEditName();
	}

	void AdjustLayout();

	void GetMinSize(CSize& size) const override
	{
		size.cx = 240;
		size.cy = 180;
	}

protected:
	CLayerView m_wndLayerView;

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnSetFocus(CWnd* pOldWnd);

	DECLARE_MESSAGE_MAP()
};

