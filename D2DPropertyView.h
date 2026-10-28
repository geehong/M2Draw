#pragma once
#include <afxwin.h>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <vector>
#include "M2DrawControl.h"
#include "D2DGraphicEngine.h"

class CM2DrawDoc;
namespace ge { class Node; }

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
	void SetTargetShape(CM2DrawDoc* pDoc, ge::Node* pShape);
	void ApplyToShape();
	// bLive: 슬라이더 드래그 중 미리보기 - undo 기록/패널 동기화 없이 현재 문서 뷰만 갱신
	// bForceCommit: 값이 이미 반영되어 있어도 undo 지점을 확정 (드래그 종료 시)
	void ApplyCanvasSettings(bool bLive = false, bool bForceCommit = false);
	void SyncControlsFromData();
	void SyncTransformOnly();
	CM2DrawDoc* GetActiveDoc() const;
	void UpdatePresetComboFromInputs();

	enum FillMode
	{
		FILL_NONE = 0,
		FILL_SOLID = 1,
		FILL_LINEAR_GRAD = 2,
		FILL_RADIAL_GRAD = 3
	};

protected:
	PaneMode    m_paneMode = MODE_SHAPE;
	CM2DrawDoc* m_pActiveDoc = nullptr;
	uint64_t    m_selectedNodeId = 0;      // 대상 도형 NodeId (0 = 없음). 포인터 대신 ID 로 보관해 삭제/새 문서 시 dangling 방지
	bool        m_bUpdating = false;

	ge::Node* GetSelectedShape() const;   // m_selectedNodeId 를 문서에서 조회 (없으면 nullptr)
	bool IsGroupSelected() const;
	bool IsTextSelected() const;          // 선택된 노드가 텍스트면 Transform 위에 글꼴/크기/굵기/정렬 UI 표시         // 선택된 노드가 그룹이면 도형용 스타일 UI 대신 그룹 전용 UI 표시

	// --- 그라디언트 편집 (채우기 탭 + Linear/Radial 모드 + 그라디언트 채움 도형일 때만 표시) ---
	bool IsGradientMode() const;          // 그라디언트 편집 UI 를 보여야 하는지
	void SetFillMode(FillMode mode);      // None/Solid/Linear/Radial 전환 (선택된 도형에 즉시 반영)
	void ApplyGradientGeometry();         // 시작/끝/반지름 수치 입력 -> 모델
	void OnGradientStopsEdited();         // 스톱 바 편집 결과 -> 모델 + 선택 스톱 색 동기화
	void SetGradientControlsVisible(bool v);
	int  m_selStop = 0;                   // 선택된 컬러 스톱 (팔레트/색상환/불투명도는 이 스톱의 색을 편집)
	void ApplyGroupAppearance();          // 그룹 노드 불투명도/혼합 모드를 모델에 반영
	float LayoutGroupPanel(float left, float right, float contentW, float y);   // 그룹 모드 컨트롤 배치 (끝 y 반환)

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
	int          m_selectedDash = 0; // 0: 실선, 1: 대시선, 2: 점선, 3: 대시-점선, 4: 사용자 정의(m_customDash)
	ge::LineCap  m_lineCap  = ge::LineCap::Butt;
	ge::LineJoin m_lineJoin = ge::LineJoin::Miter;
	// 현재 선택(프리셋/사용자 정의)이 가리키는 절대값 dashArray (프리셋은 현재 선 두께의 배수)
	std::vector<float> EffectiveDashArray() const;
	std::vector<float> m_customDash;   // 사용자 정의 대시 (문서 단위 절대값, SVG stroke-dasharray 와 동일 의미)

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
	std::shared_ptr<M2DrawButton>       m_btnGroup;            // 선택 오브젝트 그룹화 (Ctrl+G)
	std::shared_ptr<M2DrawButton>       m_btnUngroup;          // 그룹 해제 (Ctrl+Shift+G)
	std::shared_ptr<M2DrawButton>       m_btnTabFill;
	std::shared_ptr<M2DrawButton>       m_btnTabStroke;
	std::vector<std::shared_ptr<M2DrawButton>> m_btnFillModes; // 5 modes
	std::shared_ptr<M2DrawColorPalette> m_palette16;          // 16 color palette
	std::shared_ptr<M2DrawExpander>     m_expShapeWheel;       // Color Wheel expander
	std::shared_ptr<M2DrawColorWheel>   m_shapeWheel;          // Color Wheel
	std::shared_ptr<M2DrawLabel>        m_lblOpacity;          // "Opacity"
	std::shared_ptr<M2DrawSlider>       m_sliderOpacity;       // Opacity bar
	std::shared_ptr<M2DrawNumericInput> m_spinOpacity;         // Opacity number input
	std::shared_ptr<M2DrawGradientBar>  m_gradBar;             // 그라디언트 컬러 스톱 바
	std::shared_ptr<M2DrawButton>       m_btnStopAdd;          // 스톱 추가
	std::shared_ptr<M2DrawButton>       m_btnStopRemove;       // 스톱 삭제
	std::shared_ptr<M2DrawButton>       m_btnStopReverse;      // 방향 반전
	std::shared_ptr<M2DrawLabel>        m_lblGradStart;        // "Start (X, Y) %" / 방사형 "Center (X, Y) %"
	std::shared_ptr<M2DrawNumericInput> m_spinGradX1;
	std::shared_ptr<M2DrawNumericInput> m_spinGradY1;
	std::shared_ptr<M2DrawLabel>        m_lblGradEnd;          // "End (X, Y) %" / 방사형 "Focus (X, Y) %"
	std::shared_ptr<M2DrawNumericInput> m_spinGradX2;
	std::shared_ptr<M2DrawNumericInput> m_spinGradY2;
	std::shared_ptr<M2DrawLabel>        m_lblGradRadius;       // "Radius %" (방사형 전용)
	std::shared_ptr<M2DrawNumericInput> m_spinGradR;
	std::shared_ptr<M2DrawLabel>        m_lblBlend;            // "Blend Mode" (그룹 전용)
	std::shared_ptr<M2DrawComboBox>     m_cboBlend;            // 혼합 모드 콤보 (그룹 전용)

	// 텍스트 전용 (Transform 바로 위): 변경 즉시 노드에 반영
	std::shared_ptr<M2DrawLabel>        m_lblText;
	std::shared_ptr<M2DrawComboBox>     m_cboFont;
	std::shared_ptr<M2DrawNumericInput> m_spinFontSize;
	std::shared_ptr<M2DrawComboBox>     m_cboTextAlign;
	std::shared_ptr<M2DrawCheckBox>     m_chkBold;
	std::shared_ptr<M2DrawCheckBox>     m_chkItalic;
	void ApplyTextProps(const std::function<void(ge::TextNode&)>& fn);
	std::shared_ptr<M2DrawLabel>        m_lblStrokeWidth;      // "Stroke Width"
	std::shared_ptr<M2DrawSlider>       m_sliderStrokeWidth;   // Stroke width bar
	std::shared_ptr<M2DrawNumericInput> m_spinStrokeWidth;     // Stroke width number input
	std::vector<std::shared_ptr<M2DrawButton>> m_btnDashStyles; // Dash styles 4
	std::shared_ptr<M2DrawLabel>        m_lblDashArray;        // "Dash Array"
	std::shared_ptr<M2DrawTextInput>    m_txtDashArray;        // 사용자 정의 대시 입력 ("8 4 2 4")
	std::shared_ptr<M2DrawLabel>        m_lblCapJoin;          // "Cap / Join"
	std::shared_ptr<M2DrawComboBox>     m_cboLineCap;          // Butt / Round / Square
	std::shared_ptr<M2DrawComboBox>     m_cboLineJoin;         // Miter / Round / Bevel
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

	// [3. Document & Canvas Settings (MODE_CANVAS)]
	std::shared_ptr<M2DrawLabel>        m_lblCanvasHeader;
	std::shared_ptr<M2DrawLabel>        m_lblDocSize;
	std::shared_ptr<M2DrawNumericInput> m_spinCanvasW;
	std::shared_ptr<M2DrawNumericInput> m_spinCanvasH;
	std::shared_ptr<M2DrawComboBox>     m_cboCanvasPreset;
	std::shared_ptr<M2DrawLabel>        m_lblOriginMode;
	std::shared_ptr<M2DrawComboBox>     m_cboOriginMode;
	std::shared_ptr<M2DrawLabel>        m_lblDocBg;
	std::shared_ptr<M2DrawColorPalette> m_paletteDocBg;
	std::shared_ptr<M2DrawLabel>        m_lblDocOpacity;
	std::shared_ptr<M2DrawSlider>       m_sliderDocOpacity;
	std::shared_ptr<M2DrawNumericInput> m_spinDocOpacity;
	std::shared_ptr<M2DrawLabel>        m_lblCanvasBg;
	std::shared_ptr<M2DrawColorPalette> m_paletteCanvasBg;

	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();

	void SafeInvalidate(BOOL bErase = FALSE)
	{
		HWND hWnd = GetSafeHwnd();
		if (hWnd && ::IsWindow(hWnd))
		{
			::InvalidateRect(hWnd, nullptr, bErase);
		}
	}

	void InitControls();
	void UpdateLayout(float width, float height);
	void NotifyGridChanged();

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
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg UINT OnGetDlgCode();

	virtual BOOL PreTranslateMessage(MSG* pMsg) override;

	DECLARE_MESSAGE_MAP()
};

