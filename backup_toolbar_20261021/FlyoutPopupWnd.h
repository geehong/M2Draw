#pragma once
#include <afxwin.h>
#include <d2d1.h>
#include <vector>
#include <map>
#include "ToolManager.h"

class CToolBoxPane;

// 툴박스 외부(캔버스 상단)로 떠오르는 플로팅 팝업 윈도우
// Direct2D로 렌더링되며 클리핑 없이 자유롭게 펼쳐집니다.
class CFlyoutPopupWnd : public CWnd
{
	DECLARE_DYNAMIC(CFlyoutPopupWnd)

public:
	CFlyoutPopupWnd();
	virtual ~CFlyoutPopupWnd();

	BOOL CreatePopup(CToolBoxPane* pParent, const std::vector<ToolType>& tools, CPoint ptScreen, int parentBtnIdx);
	void ClosePopup();

protected:
	CToolBoxPane*         m_pToolBoxPane = nullptr;
	std::vector<ToolType> m_tools;
	int                   m_parentBtnIdx = -1;
	int                   m_hoverIndex = -1;

	ID2D1Factory*          m_pD2DFactory = nullptr;
	ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
	ID2D1SolidColorBrush*  m_pBrush = nullptr;

	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();
	int HitTest(CPoint pt);

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()
};
