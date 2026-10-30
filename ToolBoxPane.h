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
	ToolType       type;       // 현재 대표 도구 (플라이아웃 그룹이면 마지막 선택한 서브도구)
	const wchar_t* name;
	wchar_t        shortcut;
	CRect          rect;
	std::vector<ToolType> subTools; // 비어 있지 않으면 우하단 삼각 표시 + 플라이아웃 그룹
	int            gapBefore = 0; // 이 버튼 위에 두는 추가 간격(px) - 도구 묶음과 동작 버튼 구분용
	const wchar_t* profileKey = nullptr; // 플라이아웃에서 마지막으로 고른 항목을 저장하는 레지스트리 키 (그룹일 때만)
};

// 서브도구 플라이아웃 팝업 (그리기 전용, 포커스를 가져가지 않음)
// 마우스 캡처는 툴박스 창이 유지하고, 이 창은 좌표 판정과 렌더링만 담당합니다.
class CToolFlyoutWnd : public CWnd
{
public:
	BOOL     Open(CWnd* pOwner, CPoint screenTopLeft, const std::vector<ToolType>& items, ToolType current, IWICImagingFactory* pWic = nullptr);
	int      HitTest(CPoint screenPt) const;   // 팝업 밖이면 -1
	void     SetHot(int index);
	ToolType GetItem(int index) const { return m_items[index]; }

protected:
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg int  OnMouseActivate(CWnd*, UINT, UINT) { return MA_NOACTIVATE; }
	DECLARE_MESSAGE_MAP()

private:
	std::vector<ToolType> m_items;
	int      m_hot = -1;
	ToolType m_current = ToolType::Select;
	IWICImagingFactory* m_pWic = nullptr;
	static const int kItemW = 228;   // "그룹 해제 (Ctrl+Shift+G)" 같은 긴 이름이 잘리지 않는 폭
	static const int kItemH = 32;
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
	IWICImagingFactory*    m_pWICFactory;

	// 아이콘 비트맵 캐시 (일반 / 활성화)
	std::map<ToolType, ID2D1Bitmap*> m_iconNormal;
	std::map<ToolType, ID2D1Bitmap*> m_iconActive;

	int  m_hoverIndex;
	bool m_bTrackingMouse;
	std::vector<ToolButtonItem> m_buttons;

	// 플라이아웃 상태
	static const UINT_PTR kFlyoutTimer = 1001;
	static const UINT     kFlyoutDelayMs = 300;
	int             m_pressIndex = -1;   // 서브도구 그룹 버튼을 누르고 있는 인덱스
	bool            m_flyoutOpen = false;
	CToolFlyoutWnd  m_flyout;

	// 동작 버튼(그룹/그룹 해제) 활성 상태: 현재 문서의 선택에 따라 바뀌므로 타이머로 감시해 바뀌면 다시 그림
	static const UINT_PTR kStateTimer = 1002;
	static const UINT     kStateIntervalMs = 200;
	UINT            m_enabledMask = 0;     // bit0: 그룹 가능, bit1: 그룹 해제 가능
	CString         m_tipText;             // 툴팁 문자열 보관 (TTN_NEEDTEXT 가 포인터를 읽는 동안 유효해야 함)
	ID2D1StrokeStyle* m_pDashStyle = nullptr;

	UINT ComputeEnabledMask() const;
	bool IsCommandEnabled(UINT commandId) const;
	void DrawActionIcon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, UINT commandId, D2D1_RECT_F btnRect, bool enabled);

	void OpenFlyout(int buttonIndex);
	void CloseFlyout();
	bool IsButtonActive(const ToolButtonItem& btn, ToolType active) const;

	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();
	ID2D1Bitmap* LoadBitmapFromFile(const wchar_t* filePath);

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
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	afx_msg BOOL OnToolTipText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);

	virtual INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const override;

	DECLARE_MESSAGE_MAP()
};
