#pragma once
#include <afxdockablepane.h>
#include <d2d1.h>
#include <vector>
#include "ToolManager.h"

#pragma comment(lib, "d2d1.lib")

struct ToolButtonItem
{
	ToolType       type;
	const wchar_t* name;
	wchar_t        shortcut;
	CRect          rect;
};

class CToolBoxPane : public CDockablePane, public IToolChangeListener
{
	DECLARE_DYNAMIC(CToolBoxPane)

public:
	CToolBoxPane() noexcept;
	virtual ~CToolBoxPane();

	// IToolChangeListener 구현
	void OnToolChanged(ToolType newTool) override;

protected:
	ID2D1Factory*          m_pD2DFactory;
	ID2D1HwndRenderTarget* m_pRenderTarget;
	ID2D1SolidColorBrush*  m_pBrush;

	int  m_hoverIndex;
	bool m_bTrackingMouse;
	std::vector<ToolButtonItem> m_buttons;

	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();

	void DrawButtonIcon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                    ToolType type, D2D1_RECT_F btnRect, bool isSelected);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);

	DECLARE_MESSAGE_MAP()
};
