#pragma once
#include <afxdockablepane.h>
#include <d2d1.h>
#include <wincodec.h>
#include <vector>
#include <map>
#include "ToolManager.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")

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

	// 최소 도킹 크기 제약 (너비 44px 고정 슬림 툴박스)
	void GetMinSize(CSize& size) const override
	{
		size.cx = 44;
		size.cy = 200;
	}

	// 외부 및 팝업 창 연동
	void SelectFlyoutTool(int parentBtnIdx, ToolType chosenTool);
	void DrawButtonIcon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	                    ToolType type, D2D1_RECT_F btnRect, bool isSelected);

protected:
	ID2D1Factory*          m_pD2DFactory;
	ID2D1HwndRenderTarget* m_pRenderTarget;
	ID2D1SolidColorBrush*  m_pBrush;
	IWICImagingFactory*    m_pWICFactory;

	// 아이콘 비트맵 캐시 (일반 / 활성화)
	std::map<ToolType, ID2D1Bitmap*> m_iconNormal;
	std::map<ToolType, ID2D1Bitmap*> m_iconActive;

	int  m_hoverIndex;
	bool m_bTrackingMouse;
	std::vector<ToolButtonItem> m_buttons;

	bool m_bMouseDown = false;
	CPoint m_mouseDownPoint;

	void OpenFlyout(int btnIndex);
	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();
	ID2D1Bitmap* LoadBitmapFromFile(const wchar_t* filePath);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);

	DECLARE_MESSAGE_MAP()
};
