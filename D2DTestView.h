#pragma once
#include <afxwin.h>
#include <d2d1.h>
#include <dwrite.h>

class CM2DrawDoc;

// Inkscape / Illustrator 스타일의 다크 테마 D2D 그래픽 컨트롤 테스트 뷰
class CD2DTestView : public CWnd
{
public:
	CD2DTestView();
	virtual ~CD2DTestView();

	enum GuiTab
	{
		TAB_D2D = 0,    // Inkscape 스타일 Direct2D 커스텀 벡터 컨트롤
		TAB_IMGUI = 1,  // Dear ImGui 다크 오버레이 스타일 컨트롤
		TAB_MFC = 2     // MFC Windows 클래식 / 모던 다이얼로그 스타일 컨트롤
	};

	GuiTab m_currentTab = TAB_D2D;

	// 연결된 쉐이프 및 문서
	CM2DrawDoc* m_pActiveDoc = nullptr;
	int         m_selectedShapeIndex = -1;
	CString     m_strShapeName;
	CString     m_strShapeType;

	enum PaneMode
	{
		MODE_SHAPE = 0,    // 도형 속성 (채움 / 윤곽선 / 컬러휠 등)
		MODE_CANVAS = 1    // 캔버스 & 격자 설정 (그리드, 스냅, 눈금자, 단위, 색상)
	};
	PaneMode m_paneMode = MODE_SHAPE;

	void SetPaneMode(PaneMode mode)
	{
		m_paneMode = mode;
		Invalidate(FALSE);
	}

	void SetTargetShape(CM2DrawDoc* pDoc, int shapeIndex);
	void ApplyToShape();
	void ApplyCanvasSettings();

	// Inkscape Fill Mode: 0 = 채우기 없음(X), 1 = 단색(Solid), 2 = 선형 그라디언트, 3 = 방사형 그라디언트, 4 = 패턴/메시
	enum FillMode
	{
		FILL_NONE = 0,
		FILL_SOLID = 1,
		FILL_LINEAR_GRAD = 2,
		FILL_RADIAL_GRAD = 3,
		FILL_PATTERN = 4
	};
	FillMode m_fillMode = FILL_SOLID;

	// 컨트롤 상태 데이터 (도형 속성과 1:1 바인딩)
	bool  m_bFillEnabled = true;
	float m_fStrokeWidth = 2.0f; // 0.5 ~ 20.0 px
	float m_fOpacity = 100.0f;
	float m_fHue = 210.0f;
	D2D1_COLOR_F m_curColor = D2D1::ColorF(1.0f, 0.0f, 0.8f, 1.0f);
	
	// 색상 분리: 채움 색상 & 선 색상
	D2D1_COLOR_F m_fillColor = D2D1::ColorF(0.2f, 0.6f, 1.0f, 0.5f);
	D2D1_COLOR_F m_strokeColor = D2D1::ColorF(D2D1::ColorF::DodgerBlue);
	float m_fFillHue = 210.0f;
	float m_fFillSat = 0.8f;
	float m_fFillLight = 0.5f;

	float m_fStrokeHue = 210.0f;
	float m_fStrokeSat = 0.8f;
	float m_fStrokeLight = 0.5f;

	// 현재 탭 조작 대상: 0 = 채움(Fill), 1 = 윤곽선(Stroke)
	// (요구사항 3: 채움이 먼저 나오고 선이 뒤로)
	int   m_colorTarget = 0; // 0: 채움(Fill), 1: 선(Stroke)

	int   m_selectedDash = 0; // 0: 실선, 1: 대시선, 2: 점선, 3: 대시-점선

	// ImGui / MFC 탭 호환용 상태
	bool  m_bSnapAll = true;
	bool  m_bSnapBBox = false;
	bool  m_bSnapNode = true;
	int   m_selectedBlend = 0;

	// 캔버스 설정 인터랙션 상태
	int   m_canvasColorTarget = 0; // 0: 보조 격자선 색상, 1: 주 격자선 색상
	float m_fMinorHue = 220.0f;
	float m_fMinorSat = 0.5f;
	float m_fMinorLight = 0.7f;

	float m_fMajorHue = 225.0f;
	float m_fMajorSat = 0.6f;
	float m_fMajorLight = 0.6f;

	bool  m_isDraggingStepSlider = false;
	bool  m_isDraggingMajorSlider = false;
	bool  m_isDraggingCanvasOpacity = false;

protected:
	ID2D1Factory*          m_pD2DFactory = nullptr;
	ID2D1HwndRenderTarget* m_pRenderTarget = nullptr;
	IDWriteFactory*        m_pDWriteFactory = nullptr;
	IDWriteTextFormat*     m_pTextFormat = nullptr;
	IDWriteTextFormat*     m_pTextFormatBold = nullptr;
	IDWriteTextFormat*     m_pTextFormatSmall = nullptr;
	IDWriteTextFormat*     m_pTextFormatCenter = nullptr;
	ID2D1SolidColorBrush*  m_pBrush = nullptr;

	bool m_isDraggingWheel = false;
	bool m_isDraggingTriangle = false;
	bool m_isDraggingSlider = false;
	bool m_isDraggingOpacitySlider = false;
	bool m_isDraggingStrokeWidthSlider = false;

	HRESULT CreateDeviceResources();
	void DiscardDeviceResources();
	void DrawColorWheel(D2D1_POINT_2F center, float radiusOuter, float radiusInner, float activeHue, float activeSat, float activeLight);
	void DrawTabHeaders(float width, float& y);
	void DrawD2DContent(float width, float y);
	void DrawCanvasContent(float width, float y);
	void DrawImGuiContent(float width, float y);
	void DrawMFCContent(float width, float y);

	afx_msg void OnPaint();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);

	DECLARE_MESSAGE_MAP()
};
