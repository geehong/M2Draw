#pragma once
#include "SnapManager.h"
#include "D2DGraphicEngine.h"

class CM2DrawView : public CView
{
protected:
	CM2DrawView() noexcept;
	DECLARE_DYNCREATE(CM2DrawView)

// 특성입니다.
public:
	CM2DrawDoc* GetDocument() const;

// 재정의입니다.
public:
	virtual void OnDraw(CDC* pDC);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual void OnInitialUpdate();
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

// 구현입니다.
public:
	virtual ~CM2DrawView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

// 캔버스 뷰포트 (줌/팬)
protected:
	float         m_fZoom;
	D2D1_POINT_2F m_ptPan;
	BOOL          m_bIsPanning;
	CPoint        m_ptLastMouse;
	bool          m_bCenteredOriginOnLoad = false; // 최초 로드 시 중앙 원점 맞춤 여부
	// 격리 편집(5.3) 브레드크럼 막대: 클릭 영역 (id 0 = 격리 종료)
	std::vector<std::pair<D2D1_RECT_F, ge::NodeId>> m_isoCrumbs;
	D2D1_RECT_F   m_isoBarRect{ 0, 0, 0, 0 };
	void DrawIsolationBar(float width, float top);
	bool HandleIsolationBarClick(CPoint point);
	void RefreshAfterIsolationChange();
	SnapResult    m_lastSnap;                      // 마우스 포인터의 최신 스냅 결과 (인디케이터 표시용)

public:
	float GetZoom() const { return m_fZoom; }
	void SetZoom(float fZoom) { m_fZoom = fZoom; Invalidate(FALSE); }
	D2D1_POINT_2F GetPan() const { return m_ptPan; }
	void SetPan(D2D1_POINT_2F ptPan) { m_ptPan = ptPan; Invalidate(FALSE); }
	ID2D1Factory* GetD2DFactory() const { return m_pD2DFactory; }

	// 화면(Client) 좌표 기준 줌 인/아웃 공통 메서드 (마우스 휠 & 돋보기 도구 공용)
	void ZoomAtPoint(CPoint clientPt, float factor);
	void ZoomAtRect(CRect marqueeClientRc);

	// 캔버스의 (0,0) 원점을 화면 중앙에 맞춤
	void CenterOrigin();

	// 화면(Client) ↔ 월드(World) 좌표계 변환 (문서 좌하단/웹 좌상단 원점 모드 자동 반영)
	D2D1_POINT_2F ScreenToWorld(D2D1_POINT_2F screenPt) const;
	D2D1_POINT_2F ScreenToWorld(CPoint screenPt) const;
	D2D1_POINT_2F WorldToScreen(D2D1_POINT_2F worldPt) const;

	// 캔버스 위 텍스트 직접 인라인 편집 (Step 3.2.2 ~ 3.2.4)
	bool IsTextInlineEditing() const { return m_pEditingTextNode != nullptr; }
	bool m_nudgePending = false;   // 방향키로 이동한 뒤 아직 Undo 기록을 남기지 않음 (키를 뗄 때 한 번만 기록)
	bool BeginTextInlineEdit(ge::TextNode* pNode);
	void CommitTextInlineEdit();
	void CancelTextInlineEdit();
	void UpdateTextEditCtrlLayout();

protected:
	CEdit         m_wndTextInlineEdit;
	CFont         m_textEditFont;
	ge::TextNode* m_pEditingTextNode = nullptr;
	std::wstring  m_textBeforeInlineEdit;

// Direct2D 리소스
protected:
	ID2D1Factory*          m_pD2DFactory;
	ID2D1HwndRenderTarget* m_pRenderTarget;
	ID2D1SolidColorBrush*  m_pSolidBrush;
	ge::D2DRenderer        m_renderer;      // 신엔진 일원화 렌더러

public:
	ge::D2DRenderer&       GetRenderer() { return m_renderer; }

protected:
	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();

// 생성된 메시지 맵 함수
protected:
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnViewGrid();
	afx_msg void OnUpdateViewGrid(CCmdUI* pCmdUI);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnEditSelectAll();
	afx_msg void OnUpdateDocCommand(CCmdUI* pCmdUI);
	afx_msg void OnEditDuplicate();
	afx_msg void OnEditDeselect();
	afx_msg void OnObjectEnterGroup();
	afx_msg void OnObjectExitGroup();
	afx_msg void OnUpdateObjectEnterGroup(CCmdUI* pCmdUI);
	afx_msg void OnUpdateObjectExitGroup(CCmdUI* pCmdUI);
	afx_msg void OnViewActualSize();
	afx_msg void OnViewFit();
	afx_msg void OnViewRuler();
	afx_msg void OnUpdateViewRuler(CCmdUI* pCmdUI);
	afx_msg void OnViewSmartGuides();
	afx_msg void OnUpdateViewSmartGuides(CCmdUI* pCmdUI);
	afx_msg void OnViewSnapGrid();
	afx_msg void OnUpdateViewSnapGrid(CCmdUI* pCmdUI);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnEditClear();
	afx_msg void OnUpdateEditClear(CCmdUI* pCmdUI);
	afx_msg void OnTextEditKillFocus();
	afx_msg void OnTextEditChange();

public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG
inline CM2DrawDoc* CM2DrawView::GetDocument() const
   { return reinterpret_cast<CM2DrawDoc*>(m_pDocument); }
#endif
