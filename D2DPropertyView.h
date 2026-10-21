#pragma once
#include <afxwin.h>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <vector>
#include "M2DrawControl.h"

class CM2DrawDoc;

// M2DrawControl 기반 다크 테마 D2D 속성 뷰 (오브젝트 속성 & 캔버스 환경설정)
class CD2DPropertyView : public CWnd
{
public:
	enum PaneMode
	{
		MODE_SHAPE = 0,    // Object Settings (Fill / Stroke / Opacity / Close etc.)
		MODE_GRID = 1,     // Grid Settings (View, Snap, Unit ListBox, Step Bar/Spin, Major Every Bar/Spin, Colors, Wheel)
		MODE_CANVAS = 2    // Canvas Settings (Canvas Size, Background Color etc.)
	};

	CD2DPropertyView(PaneMode initialMode = MODE_SHAPE);
	virtual ~CD2DPropertyView();

	// 패널 모드 전환
	void SetPaneMode(PaneMode mode);
	PaneMode GetPaneMode() const { return m_paneMode; }

	// 타깃 도형 설정 및 적용
	void SetTargetShape(CM2DrawDoc* pDoc, int shapeIndex);
	void ApplyToShape();
	void ApplyCanvasSettings();
	void SyncControlsFromData();
	void SyncTransformOnly();

	enum FillMode
	{
		FILL_NONE = 0,
		FILL_SOLID = 1,
		FILL_LINEAR_GRAD = 2,
		FILL_RADIAL_GRAD = 3,
		FILL_PATTERN = 4
	};

protected:
	PaneMode    m_paneMode = MODE_SHAPE;
	CM2DrawDoc* m_pActiveDoc = nullptr;
	int         m_selectedShapeIndex = -1;
	bool        m_bUpdating = false;

	// 도형 상태 데이터
	CString      m_strShapeName;
	CString      m_strShapeType;
	int          m_colorTarget = 0; // 0: 채움(Fill), 1: 윤곽선(Stroke)
	FillMode     m_fillMode = FILL_SOLID;
	bool         m_bFillEnabled = true;
	float        m_fStrokeWidth = 2.0f;
	float        m_fOpacity = 100.0f;
	D2D1_COLOR_F m_fillColor = D2D1::ColorF(0.2f, 0.6f, 1.0f, 0.5f);
	D2D1_COLOR_F m_strokeColor = D2D1::ColorF(D2D1::ColorF::DodgerBlue);
	float        m_fFillHue = 210.0f;
	float        m_fFillSat = 0.8f;
	float        m_fFillLight = 0.5f;
	float        m_fStrokeHue = 210.0f;
	float        m_fStrokeSat = 0.8f;
	float        m_fStrokeLight = 0.5f;
	int          m_selectedDash = 0; // 0: 실선, 1: 대시선, 2: 점선, 3: 대시-점선

	// 캔버스 설정 상태
	int          m_canvasColorTarget = 0; // 0: 보조선, 1: 주선
	float        m_fMinorHue = 220.0f;
	float        m_fMinorSat = 0.5f;
	float        m_fMinorLight = 0.7f;
	float        m_fMajorHue = 225.0f;
	float        m_fMajorSat = 0.6f;
	float        m_fMajorLight = 0.6f;

	// D2D / DirectWrite 기본 장치 리소스
	ID2D1Factory*          m_pD2DFactory = nullptr;
	ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
	IDWriteFactory*        m_pDWriteFactory = nullptr;
	IDWriteTextFormat*     m_pTextFormat = nullptr;
	IDWriteTextFormat*     m_pTextFormatBold = nullptr;
	IDWriteTextFormat*     m_pTextFormatSmall = nullptr;
	IDWriteTextFormat*     m_pTextFormatCenter = nullptr;
	ID2D1SolidColorBrush*  m_pBrush = nullptr;

	// M2DrawControl 기반 UI 패널 및 자식 컨트롤 멤버
	std::shared_ptr<M2DrawPanel> m_pCurrentPanel;

	// [1. Object Settings (MODE_SHAPE)]
	std::shared_ptr<M2DrawLabel>        m_lblShapeHeader;
	std::shared_ptr<M2DrawLabel>        m_lblTransformHeader;  // "● Transform (X, Y, W, H)"
	std::shared_ptr<M2DrawNumericInput> m_spinTransX;          // X 위치
	std::shared_ptr<M2DrawNumericInput> m_spinTransY;          // Y 위치
	std::shared_ptr<M2DrawNumericInput> m_spinTransW;          // W 너비
	std::shared_ptr<M2DrawNumericInput> m_spinTransH;          // H 높이
	std::shared_ptr<M2DrawCheckBox>     m_chkAspectLink;       // 비율 고정 🔗
	std::shared_ptr<M2DrawButton>       m_btnTabFill;
	std::shared_ptr<M2DrawButton>       m_btnTabStroke;
	std::vector<std::shared_ptr<M2DrawButton>> m_btnFillModes; // 5 modes
	std::shared_ptr<M2DrawColorPalette> m_palette16;          // 16 color palette
	std::shared_ptr<M2DrawExpander>     m_expShapeWheel;       // Color Wheel expander
	std::shared_ptr<M2DrawColorWheel>   m_shapeWheel;          // Color Wheel
	std::shared_ptr<M2DrawLabel>        m_lblOpacity;          // "Opacity"
	std::shared_ptr<M2DrawSlider>       m_sliderOpacity;       // Opacity bar
	std::shared_ptr<M2DrawNumericInput> m_spinOpacity;         // Opacity number input
	std::shared_ptr<M2DrawLabel>        m_lblStrokeWidth;      // "Stroke Width"
	std::shared_ptr<M2DrawSlider>       m_sliderStrokeWidth;   // Stroke width bar
	std::shared_ptr<M2DrawNumericInput> m_spinStrokeWidth;     // Stroke width number input
	std::vector<std::shared_ptr<M2DrawButton>> m_btnDashStyles; // Dash styles 4
	std::shared_ptr<M2DrawCheckBox>     m_chkCloseShape;       // Close checkbox (Multi-point / Path)

	// [2. Grid Settings (MODE_GRID)]
	std::shared_ptr<M2DrawLabel>        m_lblGridHeader;
	std::shared_ptr<M2DrawCheckBox>     m_chkGridView;         // View (Grid) Checkbox
	std::shared_ptr<M2DrawCheckBox>     m_chkGridSnap;         // Snap Checkbox
	std::shared_ptr<M2DrawCheckBox>     m_chkRulerView;        // View (Ruler) Checkbox
	std::shared_ptr<M2DrawLabel>        m_lblUnit;             // "Unit"
	std::shared_ptr<M2DrawComboBox>     m_cboUnit;             // Unit ComboBox (px, mm, cm, in, rem)
	std::shared_ptr<M2DrawLabel>        m_lblGridStep;         // "Step"
	std::shared_ptr<M2DrawSlider>       m_sliderGridStep;      // Step Bar
	std::shared_ptr<M2DrawNumericInput> m_spinGridStep;        // Step Number Input
	std::shared_ptr<M2DrawLabel>        m_lblGridMajor;        // "Major Every"
	std::shared_ptr<M2DrawSlider>       m_sliderGridMajor;     // Major Every Bar
	std::shared_ptr<M2DrawNumericInput> m_spinGridMajor;       // Major Every Number Input
	std::shared_ptr<M2DrawButton>       m_btnGridMinorClr;     // Minor Grid Color Tab
	std::shared_ptr<M2DrawButton>       m_btnGridMajorClr;     // Major Grid Color Tab
	std::shared_ptr<M2DrawExpander>     m_expGridWheel;        // Grid Wheel Expander
	std::shared_ptr<M2DrawColorWheel>   m_gridWheel;           // Grid Color Wheel
	std::shared_ptr<M2DrawLabel>        m_lblGridOpacity;      // "Grid Opacity"
	std::shared_ptr<M2DrawSlider>       m_sliderGridOpacity;   // Grid Opacity Slider
	std::shared_ptr<M2DrawNumericInput> m_spinGridOpacity;     // Grid Opacity Number Input

	// [3. Canvas Settings (MODE_CANVAS)]
	std::shared_ptr<M2DrawLabel>        m_lblCanvasHeader;
	std::shared_ptr<M2DrawLabel>        m_lblCanvasSize;
	std::shared_ptr<M2DrawNumericInput> m_spinCanvasW;
	std::shared_ptr<M2DrawNumericInput> m_spinCanvasH;
	std::shared_ptr<M2DrawLabel>        m_lblCanvasBg;
	std::shared_ptr<M2DrawColorPalette> m_paletteCanvasBg;

	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();

	void SafeInvalidate(BOOL bErase = FALSE)
	{
		if (GetSafeHwnd())
		{
			Invalidate(bErase);
		}
	}

	void InitControls();
	void UpdateLayout(float width, float height);

	// MFC 메시지 맵 핸들러
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnPaint();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg UINT OnGetDlgCode();

	virtual BOOL PreTranslateMessage(MSG* pMsg) override;

	DECLARE_MESSAGE_MAP()
};

