#pragma once

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

public:
	float GetZoom() const { return m_fZoom; }
	void SetZoom(float fZoom) { m_fZoom = fZoom; Invalidate(FALSE); }
	ID2D1Factory* GetD2DFactory() const { return m_pD2DFactory; }

// Direct2D 리소스
protected:
	ID2D1Factory*          m_pD2DFactory;
	ID2D1HwndRenderTarget* m_pRenderTarget;
	ID2D1SolidColorBrush*  m_pSolidBrush;

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

	DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG
inline CM2DrawDoc* CM2DrawView::GetDocument() const
   { return reinterpret_cast<CM2DrawDoc*>(m_pDocument); }
#endif
