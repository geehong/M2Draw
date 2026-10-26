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
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnEditClear();
	afx_msg void OnUpdateEditClear(CCmdUI* pCmdUI);

public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG
inline CM2DrawDoc* CM2DrawView::GetDocument() const
   { return reinterpret_cast<CM2DrawDoc*>(m_pDocument); }
#endif
