#pragma once
#include <afxdockablepane.h>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include "M2DrawControl.h"
#include "ToolManager.h"

// Inkscape / Illustrator 스타일 상단 도구 제어바 (Tool Controls Bar)
// 활성 도구에 따라 만들기/수정 모드 전환 및 [Sx, Sy] [W, H] [Rx, Ry] [별/기어/나선] 수치 컨트롤 제공
class CToolOptionBar : public CDockablePane, public IToolChangeListener
{
	DECLARE_DYNAMIC(CToolOptionBar)

public:
	CToolOptionBar() noexcept;
	virtual ~CToolOptionBar();

	// IToolChangeListener 구현
	void OnToolChanged(ToolType newTool) override;

	// 외부(선택 도구, 캔버스 등)에서 수치 변경 시 동기화
	void SyncFromToolSetting();
	void SyncFromSelectedShape(CM2DrawDoc* pDoc, int shapeIdx);

	// Inkscape 스타일 상단 슬림 바 (닫기/자동숨김 비활성화, 고정 높이 34px)
	virtual BOOL CanBeClosed() const override { return FALSE; }
	virtual BOOL CanAutoHide() const override { return FALSE; }
	virtual void GetMinSize(CSize& size) const override { size.cx = 400; size.cy = 38; }

protected:
	ID2D1Factory*          m_pD2DFactory = nullptr;
	ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
	IDWriteFactory*        m_pDWriteFactory = nullptr;
	IDWriteTextFormat*     m_pTextFormat = nullptr;
	IDWriteTextFormat*     m_pTextFormatSmall = nullptr;
	ID2D1SolidColorBrush*  m_pBrush = nullptr;

	std::shared_ptr<M2DrawPanel> m_pPanel;

	// UI 컨트롤들
	std::shared_ptr<M2DrawLabel>        m_lblTitle;       // "도형 만들기" / "도형 #1 수정"
	std::shared_ptr<M2DrawNumericInput> m_spinX;          // Sx / X
	std::shared_ptr<M2DrawNumericInput> m_spinY;          // Sy / Y
	std::shared_ptr<M2DrawNumericInput> m_spinW;          // W (너비)
	std::shared_ptr<M2DrawNumericInput> m_spinH;          // H (높이)
	std::shared_ptr<M2DrawNumericInput> m_spinRx;         // Rx (모서리 X)
	std::shared_ptr<M2DrawNumericInput> m_spinRy;         // Ry (모서리 Y)
	std::shared_ptr<M2DrawCheckBox>     m_chkLinkRxRy;    // 🔗 링크 버튼
	std::shared_ptr<M2DrawButton>       m_btnResetCorners;// ⏢ 모서리 리셋

	// 특수 도형 파라미터
	std::shared_ptr<M2DrawNumericInput> m_spinPoints;     // 꼭짓점/톱니 수
	std::shared_ptr<M2DrawNumericInput> m_spinRatio;      // 별 내경 비율 / 톱니 깊이
	std::shared_ptr<M2DrawNumericInput> m_spinTurns;      // 나선 바퀴 수

	ToolType m_currentTool = ToolType::Select;
	int      m_selectedShapeIdx = -1;
	float    m_curW = 100.0f;
	float    m_curH = 80.0f;
	float    m_curX = 0.0f;
	float    m_curY = 0.0f;

	void InitControls();
	void UpdateLayout(float width, float height);
	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();

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

	DECLARE_MESSAGE_MAP()
};
