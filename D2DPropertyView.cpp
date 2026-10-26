#include "pch.h"
#include "D2DPropertyView.h"
#include "M2DrawDoc.h"
#include "M2Draw.h"
#include "MainFrm.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
	D2D1_COLOR_F HSLtoRGB(float h, float s, float l, float a = 1.0f)
	{
		float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
		float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
		float m = l - c / 2.0f;
		float r = 0, g = 0, b = 0;

		if (h >= 0 && h < 60)        { r = c; g = x; b = 0; }
		else if (h >= 60 && h < 120) { r = x; g = c; b = 0; }
		else if (h >= 120 && h < 180){ r = 0; g = c; b = x; }
		else if (h >= 180 && h < 240){ r = 0; g = x; b = c; }
		else if (h >= 240 && h < 300){ r = x; g = 0; b = c; }
		else                         { r = c; g = 0; b = x; }

		return D2D1::ColorF(r + m, g + m, b + m, a);
	}

	void RGBtoHSL(const D2D1_COLOR_F& clr, float& h, float& s, float& l)
	{
		float r = clr.r, g = clr.g, b = clr.b;
		float maxVal = max(r, max(g, b));
		float minVal = min(r, min(g, b));
		float delta = maxVal - minVal;

		l = (maxVal + minVal) * 0.5f;

		if (delta < 0.0001f)
		{
			h = 0.0f;
			s = 0.0f;
			return;
		}

		s = (l > 0.5f) ? (delta / (2.0f - maxVal - minVal)) : (delta / (maxVal + minVal));

		if (maxVal == r)
			h = 60.0f * std::fmod((g - b) / delta, 6.0f);
		else if (maxVal == g)
			h = 60.0f * (((b - r) / delta) + 2.0f);
		else
			h = 60.0f * (((r - g) / delta) + 4.0f);

		if (h < 0.0f) h += 360.0f;
	}
}

BEGIN_MESSAGE_MAP(CD2DPropertyView, CWnd)
	ON_WM_CREATE()
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_KEYDOWN()
	ON_WM_CHAR()
	ON_WM_KILLFOCUS()
	ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

CD2DPropertyView::CD2DPropertyView(PaneMode initialMode)
	: m_paneMode(initialMode)
{
	InitControls();
}

int CD2DPropertyView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	InitControls();
	return 0;
}

CD2DPropertyView::~CD2DPropertyView()
{
	DiscardDeviceResources();

	if (m_pTextFormatCenter) { m_pTextFormatCenter->Release(); m_pTextFormatCenter = nullptr; }
	if (m_pTextFormatSmall)  { m_pTextFormatSmall->Release();  m_pTextFormatSmall = nullptr; }
	if (m_pTextFormatBold)   { m_pTextFormatBold->Release();   m_pTextFormatBold = nullptr; }
	if (m_pTextFormat)       { m_pTextFormat->Release();       m_pTextFormat = nullptr; }
	if (m_pDWriteFactory)    { m_pDWriteFactory->Release();    m_pDWriteFactory = nullptr; }
	if (m_pD2DFactory)       { m_pD2DFactory->Release();       m_pD2DFactory = nullptr; }
}

void CD2DPropertyView::SetPaneMode(PaneMode mode)
{
	m_paneMode = mode;
	InitControls();
	if (GetSafeHwnd())
	{
		CRect rc;
		GetClientRect(&rc);
		UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
		SafeInvalidate(FALSE);
	}
}

void CD2DPropertyView::SetTargetShape(CM2DrawDoc* pDoc, int shapeIndex)
{
	m_pActiveDoc = pDoc;
	m_selectedShapeIndex = shapeIndex;

	if (pDoc && shapeIndex >= 0 && shapeIndex < static_cast<int>(pDoc->GetShapes().size()))
	{
		Shape* shape = pDoc->GetShapes()[shapeIndex].get();
		m_strShapeType = shape->GetShapeTypeName();
		m_strShapeName = shape->GetName().empty() ? m_strShapeType : CString(shape->GetName().c_str());

		m_bFillEnabled = (shape->GetStyleRef().fillType != FillType::None);
		m_fillColor = shape->GetStyleRef().fill;
		RGBtoHSL(m_fillColor, m_fFillHue, m_fFillSat, m_fFillLight);

		CanvasUnit curUnit = GetApp()->GetGrid().unit;
		float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
		if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;
		m_fStrokeWidth = shape->GetStyleRef().strokeWidth / pxPerUnit;
		m_strokeColor = shape->GetStyleRef().stroke;
		RGBtoHSL(m_strokeColor, m_fStrokeHue, m_fStrokeSat, m_fStrokeLight);

		m_fOpacity = (m_colorTarget == 0 ? m_fillColor.a : m_strokeColor.a) * 100.0f;

		switch (shape->GetStyleRef().dashStyle)
		{
		case D2D1_DASH_STYLE_DASH:      m_selectedDash = 1; break;
		case D2D1_DASH_STYLE_DOT:       m_selectedDash = 2; break;
		case D2D1_DASH_STYLE_DASH_DOT:  m_selectedDash = 3; break;
		default:                        m_selectedDash = 0; break;
		}
	}
	else
	{
		m_strShapeName = _T("No Selection");
		m_strShapeType.Empty();
	}

	SyncControlsFromData();
	SafeInvalidate(FALSE);
}

void CD2DPropertyView::ApplyToShape()
{
	if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size()))
		return;

	Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
	if (!shape) return;

	if (m_bFillEnabled)
	{
		shape->GetStyleRef().fillType = FillType::Solid;
		shape->GetStyleRef().fill = m_fillColor;
	}
	else
	{
		shape->GetStyleRef().fillType = FillType::None;
	}

	shape->GetStyleRef().stroke = m_strokeColor;

	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	shape->GetStyleRef().strokeWidth = max(0.1f, m_fStrokeWidth * pxPerUnit);

	switch (m_selectedDash)
	{
	case 1: shape->GetStyleRef().dashStyle = D2D1_DASH_STYLE_DASH; break;
	case 2: shape->GetStyleRef().dashStyle = D2D1_DASH_STYLE_DOT; break;
	case 3: shape->GetStyleRef().dashStyle = D2D1_DASH_STYLE_DASH_DOT; break;
	default: shape->GetStyleRef().dashStyle = D2D1_DASH_STYLE_SOLID; break;
	}

	shape->Invalidate();

	// 신엔진 ge::Document 에도 스타일 동기화
	if (m_pActiveDoc && m_selectedShapeIndex >= 0)
	{
		auto* layer = m_pActiveDoc->GetDoc().GetActiveLayer();
		if (layer && m_selectedShapeIndex < static_cast<int>(layer->GetChildren().size()))
		{
			auto* node = layer->GetChildren()[m_selectedShapeIndex].get();
			if (node)
			{
				ge::Style st;
				st.fillType = static_cast<ge::FillType>(shape->GetStyleRef().fillType);
				st.fill = ge::Color::FromD2D(shape->GetStyleRef().fill);
				st.stroke = ge::Color::FromD2D(shape->GetStyleRef().stroke);
				st.strokeWidth = shape->GetStyleRef().strokeWidth;
				st.dashStyle = static_cast<ge::DashStyle>(shape->GetStyleRef().dashStyle);
				node->SetStyle(st);
			}
		}
	}

	m_pActiveDoc->UpdateAllViews(nullptr);
}

void CD2DPropertyView::ApplyCanvasSettings()
{
	CWinApp* pApp = AfxGetApp();
	if (pApp)
	{
		POSITION posDocTemplate = pApp->GetFirstDocTemplatePosition();
		while (posDocTemplate)
		{
			CDocTemplate* pDocTemplate = pApp->GetNextDocTemplate(posDocTemplate);
			if (pDocTemplate)
			{
				POSITION posDoc = pDocTemplate->GetFirstDocPosition();
				while (posDoc)
				{
					CDocument* pDoc = pDocTemplate->GetNextDoc(posDoc);
					if (pDoc)
					{
						pDoc->UpdateAllViews(nullptr);
					}
				}
			}
		}

		CWnd* pMainWnd = AfxGetMainWnd();
		if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		{
			CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
			pMainFrame->GetOptionBar().SyncSelection(m_pActiveDoc, m_selectedShapeIndex);
		}
	}

	// 그리드 설정(단위 등)이 변경되었을 때 오브젝트 설정 창의 단위 및 값 즉시 동기화
	CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	if (pMain)
	{
		CD2DPropertyView& objPropView = pMain->GetPropertiesWnd().GetD2DPropView();
		if (&objPropView != this && objPropView.GetSafeHwnd())
		{
			objPropView.SyncControlsFromData();
			objPropView.SafeInvalidate(FALSE);
		}
	}
}

void CD2DPropertyView::InitControls()
{
	m_pCurrentPanel = std::make_shared<M2DrawPanel>();

	if (m_paneMode == MODE_SHAPE)
	{
		// 1. Header Label
		m_lblShapeHeader = std::make_shared<M2DrawLabel>(L"● No Selection (Preview Mode)");
		m_pCurrentPanel->AddChild(m_lblShapeHeader);

		// Transform Header & Inputs (X, Y, W, H, 🔗)
		m_lblTransformHeader = std::make_shared<M2DrawLabel>(L"● Transform (X, Y, W, H)");
		m_pCurrentPanel->AddChild(m_lblTransformHeader);

		m_spinTransX = std::make_shared<M2DrawNumericInput>(-10000.0, 10000.0, 0.0, 1.0);
		m_spinTransX->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			if (m_bUpdating) return;
			if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size())) return;
			Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
			if (!shape) return;
			CanvasUnit curUnit = GetApp()->GetGrid().unit;
			float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
			float targetPixelX = static_cast<float>(val) * pxPerUnit;
			D2D1_RECT_F localBounds;
			if (shape->GetLocalBounds(nullptr, localBounds)) {
				D2D1_POINT_2F curOrigin = shape->GetTransform().TransformPoint(D2D1::Point2F(localBounds.left, localBounds.top));
				float dx = targetPixelX - curOrigin.x;
				shape->SetTransform(shape->GetTransform() * D2D1::Matrix3x2F::Translation(dx, 0.0f));
				shape->Invalidate();
				m_pActiveDoc->UpdateAllViews(nullptr);
			}
		});
		m_pCurrentPanel->AddChild(m_spinTransX);

		m_spinTransY = std::make_shared<M2DrawNumericInput>(-10000.0, 10000.0, 0.0, 1.0);
		m_spinTransY->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			if (m_bUpdating) return;
			if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size())) return;
			Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
			if (!shape) return;
			CanvasUnit curUnit = GetApp()->GetGrid().unit;
			float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
			float targetPixelY = static_cast<float>(val) * pxPerUnit;
			D2D1_RECT_F localBounds;
			if (shape->GetLocalBounds(nullptr, localBounds)) {
				D2D1_POINT_2F curOrigin = shape->GetTransform().TransformPoint(D2D1::Point2F(localBounds.left, localBounds.top));
				float dy = targetPixelY - curOrigin.y;
				shape->SetTransform(shape->GetTransform() * D2D1::Matrix3x2F::Translation(0.0f, dy));
				shape->Invalidate();
				m_pActiveDoc->UpdateAllViews(nullptr);
			}
		});
		m_pCurrentPanel->AddChild(m_spinTransY);

		m_spinTransW = std::make_shared<M2DrawNumericInput>(0.1, 10000.0, 100.0, 1.0);
		m_spinTransW->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			if (m_bUpdating) return;
			if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size())) return;
			Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
			if (!shape) return;
			CanvasUnit curUnit = GetApp()->GetGrid().unit;
			float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
			float targetPixelW = max(1.0f, static_cast<float>(val) * pxPerUnit);
			D2D1_RECT_F lb;
			if (shape->GetLocalBounds(nullptr, lb)) {
				float origW = lb.right - lb.left;
				if (origW > 0.001f) {
					float curScaleX = std::sqrt(shape->GetTransform()._11 * shape->GetTransform()._11 + shape->GetTransform()._12 * shape->GetTransform()._12);
					if (curScaleX > 0.0001f) {
						float factor = (targetPixelW / (origW * curScaleX));
						D2D1_POINT_2F origin = shape->GetTransform().TransformPoint(D2D1::Point2F(lb.left, lb.top));
						if (m_chkAspectLink && m_chkAspectLink->IsChecked()) {
							shape->SetTransform(shape->GetTransform() * D2D1::Matrix3x2F::Scale(factor, factor, origin));
							if (m_spinTransH) m_spinTransH->SetValue(m_spinTransH->GetValue() * factor, false);
						} else {
							shape->SetTransform(shape->GetTransform() * D2D1::Matrix3x2F::Scale(factor, 1.0f, origin));
						}
						shape->Invalidate();
						m_pActiveDoc->UpdateAllViews(nullptr);
					}
				}
			}
		});
		m_pCurrentPanel->AddChild(m_spinTransW);

		m_spinTransH = std::make_shared<M2DrawNumericInput>(0.1, 10000.0, 100.0, 1.0);
		m_spinTransH->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			if (m_bUpdating) return;
			if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size())) return;
			Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
			if (!shape) return;
			CanvasUnit curUnit = GetApp()->GetGrid().unit;
			float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
			float targetPixelH = max(1.0f, static_cast<float>(val) * pxPerUnit);
			D2D1_RECT_F lb;
			if (shape->GetLocalBounds(nullptr, lb)) {
				float origH = lb.bottom - lb.top;
				if (origH > 0.001f) {
					float curScaleY = std::sqrt(shape->GetTransform()._21 * shape->GetTransform()._21 + shape->GetTransform()._22 * shape->GetTransform()._22);
					if (curScaleY > 0.0001f) {
						float factor = (targetPixelH / (origH * curScaleY));
						D2D1_POINT_2F origin = shape->GetTransform().TransformPoint(D2D1::Point2F(lb.left, lb.top));
						if (m_chkAspectLink && m_chkAspectLink->IsChecked()) {
							shape->SetTransform(shape->GetTransform() * D2D1::Matrix3x2F::Scale(factor, factor, origin));
							if (m_spinTransW) m_spinTransW->SetValue(m_spinTransW->GetValue() * factor);
						} else {
							shape->SetTransform(shape->GetTransform() * D2D1::Matrix3x2F::Scale(1.0f, factor, origin));
						}
						shape->Invalidate();
						m_pActiveDoc->UpdateAllViews(nullptr);
					}
				}
			}
		});
		m_pCurrentPanel->AddChild(m_spinTransH);

		m_chkAspectLink = std::make_shared<M2DrawCheckBox>(L"🔗 Link Ratio", true);
		m_pCurrentPanel->AddChild(m_chkAspectLink);

		// 2. Tabs: [Fill] vs [Stroke]
		m_btnTabFill = std::make_shared<M2DrawButton>(L"Fill");
		m_btnTabFill->SetToggleMode(true);
		m_btnTabFill->SetChecked(m_colorTarget == 0);
		m_btnTabFill->SetOnClick([this](M2DrawButton* /*pBtn*/) {
			m_colorTarget = 0;
			m_btnTabFill->SetChecked(true);
			m_btnTabStroke->SetChecked(false);
			m_fOpacity = m_fillColor.a * 100.0f;
			SyncControlsFromData();
			if (GetSafeHwnd())
			{
				CRect rc;
				GetClientRect(&rc);
				UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
				SafeInvalidate(FALSE);
			}
		});
		m_pCurrentPanel->AddChild(m_btnTabFill);

		m_btnTabStroke = std::make_shared<M2DrawButton>(L"Stroke");
		m_btnTabStroke->SetToggleMode(true);
		m_btnTabStroke->SetChecked(m_colorTarget == 1);
		m_btnTabStroke->SetOnClick([this](M2DrawButton* /*pBtn*/) {
			m_colorTarget = 1;
			m_btnTabFill->SetChecked(false);
			m_btnTabStroke->SetChecked(true);
			m_fOpacity = m_strokeColor.a * 100.0f;
			SyncControlsFromData();
			if (GetSafeHwnd())
			{
				CRect rc;
				GetClientRect(&rc);
				UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
				SafeInvalidate(FALSE);
			}
		});
		m_pCurrentPanel->AddChild(m_btnTabStroke);

		// 3. Fill Mode Toolbar (5 modes)
		const wchar_t* modeTitles[] = { L"None", L"Solid", L"Linear", L"Radial", L"Pattern" };
		m_btnFillModes.clear();
		for (int i = 0; i < 5; ++i)
		{
			auto btn = std::make_shared<M2DrawButton>(modeTitles[i]);
			btn->SetToggleMode(true);
			btn->SetChecked(static_cast<int>(m_fillMode) == i);
			btn->SetOnClick([this, i](M2DrawButton* /*pBtn*/) {
				m_fillMode = static_cast<FillMode>(i);
				m_bFillEnabled = (m_fillMode != FILL_NONE);
				for (int k = 0; k < 5; ++k)
					m_btnFillModes[k]->SetChecked(k == i);
				ApplyToShape();
				SafeInvalidate(FALSE);
			});
			m_btnFillModes.push_back(btn);
			m_pCurrentPanel->AddChild(btn);
		}

		// 4. Standard 16-Color Palette
		m_palette16 = std::make_shared<M2DrawColorPalette>();
		m_palette16->SetOnColorSelected([this](M2DrawColorPalette* /*pPal*/, D2D1_COLOR_F clr) {
			if (m_colorTarget == 0) // Fill
			{
				m_fillColor = D2D1::ColorF(clr.r, clr.g, clr.b, m_fOpacity / 100.0f);
				RGBtoHSL(m_fillColor, m_fFillHue, m_fFillSat, m_fFillLight);
				if (m_shapeWheel) m_shapeWheel->SetColorHSL(m_fFillHue, m_fFillSat, m_fFillLight);
			}
			else // Stroke
			{
				m_strokeColor = D2D1::ColorF(clr.r, clr.g, clr.b, m_fOpacity / 100.0f);
				RGBtoHSL(m_strokeColor, m_fStrokeHue, m_fStrokeSat, m_fStrokeLight);
				if (m_shapeWheel) m_shapeWheel->SetColorHSL(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight);
			}
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_palette16);

		// 5. Color Wheel Expander
		m_expShapeWheel = std::make_shared<M2DrawExpander>(L"Color Wheel", true);
		m_shapeWheel = std::make_shared<M2DrawColorWheel>();
		m_shapeWheel->SetOnColorChanged([this](float h, float s, float l, bool /*isFinal*/) {
			if (m_colorTarget == 0)
			{
				m_fFillHue = h; m_fFillSat = s; m_fFillLight = l;
				m_fillColor = HSLtoRGB(m_fFillHue, m_fFillSat, m_fFillLight, m_fOpacity / 100.0f);
				if (m_palette16) m_palette16->SetSelectedColor(m_fillColor);
			}
			else
			{
				m_fStrokeHue = h; m_fStrokeSat = s; m_fStrokeLight = l;
				m_strokeColor = HSLtoRGB(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight, m_fOpacity / 100.0f);
				if (m_palette16) m_palette16->SetSelectedColor(m_strokeColor);
			}
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_expShapeWheel->AddChild(m_shapeWheel);
		m_expShapeWheel->SetOnToggle([this](M2DrawExpander* /*pExp*/, bool /*bExpanded*/) {
			if (GetSafeHwnd())
			{
				CRect rc;
				GetClientRect(&rc);
				UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
				SafeInvalidate(FALSE);
			}
		});
		m_pCurrentPanel->AddChild(m_expShapeWheel);

		// 6. Opacity Label & Slider Bar & Number Input
		m_lblOpacity = std::make_shared<M2DrawLabel>(L"Opacity (%)");
		m_pCurrentPanel->AddChild(m_lblOpacity);

		m_sliderOpacity = std::make_shared<M2DrawSlider>(0.0f, 100.0f, m_fOpacity);
		m_sliderOpacity->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			m_fOpacity = val;
			if (m_spinOpacity) m_spinOpacity->SetValue(val);
			if (m_colorTarget == 0)
				m_fillColor.a = m_fOpacity / 100.0f;
			else
				m_strokeColor.a = m_fOpacity / 100.0f;
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_sliderOpacity);

		m_spinOpacity = std::make_shared<M2DrawNumericInput>(0.0, 100.0, m_fOpacity, 1.0);
		m_spinOpacity->SetSuffix(L"%");
		m_spinOpacity->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			m_fOpacity = static_cast<float>(val);
			if (m_sliderOpacity) m_sliderOpacity->SetValue(m_fOpacity);
			if (m_colorTarget == 0)
				m_fillColor.a = m_fOpacity / 100.0f;
			else
				m_strokeColor.a = m_fOpacity / 100.0f;
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_spinOpacity);

		// 7. Stroke Width Label & Slider Bar & Number Input
		CanvasUnit curUnit = GetApp()->GetGrid().unit;
		const wchar_t* unitSuffix = GetCanvasUnitSuffix(curUnit);
		CString strStrokeLbl;
		strStrokeLbl.Format(_T("Stroke Width (%s)"), unitSuffix);
		m_lblStrokeWidth = std::make_shared<M2DrawLabel>((LPCTSTR)strStrokeLbl);
		m_pCurrentPanel->AddChild(m_lblStrokeWidth);

		float defaultMax = 50.0f;
		double stepVal = 0.5;
		switch (curUnit)
		{
		case CanvasUnit::Pixel: defaultMax = 50.0f; stepVal = 0.5; break;
		case CanvasUnit::MM:    defaultMax = 20.0f; stepVal = 0.2; break;
		case CanvasUnit::CM:    defaultMax = 5.0f;  stepVal = 0.05; break;
		case CanvasUnit::Inch:  defaultMax = 2.0f;  stepVal = 0.02; break;
		case CanvasUnit::Rem:   defaultMax = 5.0f;  stepVal = 0.1; break;
		}

		float initMaxSlider = max(defaultMax, m_fStrokeWidth * 1.5f);
		m_sliderStrokeWidth = std::make_shared<M2DrawSlider>(0.1f, initMaxSlider, m_fStrokeWidth);
		m_sliderStrokeWidth->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			m_fStrokeWidth = val;
			if (m_spinStrokeWidth) m_spinStrokeWidth->SetValue(val);
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_sliderStrokeWidth);

		m_spinStrokeWidth = std::make_shared<M2DrawNumericInput>(0.01, 1000000.0, m_fStrokeWidth, stepVal);
		m_spinStrokeWidth->SetSuffix(unitSuffix);
		m_spinStrokeWidth->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			m_fStrokeWidth = static_cast<float>(val);
			if (m_sliderStrokeWidth)
			{
				if (m_fStrokeWidth > m_sliderStrokeWidth->GetMaxVal())
				{
					m_sliderStrokeWidth->SetRange(0.1f, m_fStrokeWidth * 1.5f);
				}
				m_sliderStrokeWidth->SetValue(m_fStrokeWidth);
			}
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_spinStrokeWidth);

		// 8. Dash Styles 4 Buttons
		const wchar_t* dashTitles[] = { L"Solid", L"Dash", L"Dot", L"Dash-Dot" };
		m_btnDashStyles.clear();
		for (int i = 0; i < 4; ++i)
		{
			auto btn = std::make_shared<M2DrawButton>(dashTitles[i]);
			btn->SetToggleMode(true);
			btn->SetChecked(m_selectedDash == i);
			btn->SetOnClick([this, i](M2DrawButton* /*pBtn*/) {
				m_selectedDash = i;
				for (int k = 0; k < 4; ++k)
					m_btnDashStyles[k]->SetChecked(k == i);
				ApplyToShape();
				SafeInvalidate(FALSE);
			});
			m_btnDashStyles.push_back(btn);
			m_pCurrentPanel->AddChild(btn);
		}

		// 9. Close Shape Checkbox (For Multi-point / Path Shapes)
		m_chkCloseShape = std::make_shared<M2DrawCheckBox>(L"Close Path (Connect start/end)", false);
		m_chkCloseShape->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			if (m_pActiveDoc && m_selectedShapeIndex >= 0 && m_selectedShapeIndex < static_cast<int>(m_pActiveDoc->GetShapes().size()))
			{
				PathShape* pathShape = dynamic_cast<PathShape*>(m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get());
				if (pathShape)
				{
					for (auto& sp : pathShape->GetPathsRef())
					{
						sp.closed = bChecked;
					}
					pathShape->Invalidate();
					m_pActiveDoc->UpdateAllViews(nullptr);
					SafeInvalidate(FALSE);
					SyncOptionBarWithSelection(m_selectedShapeIndex, m_pActiveDoc);
				}
			}
		});
		m_pCurrentPanel->AddChild(m_chkCloseShape);
	}
	else if (m_paneMode == MODE_GRID)
	{
		// [2. Grid Settings Mode]
		GridRenderer& grid = GetApp()->GetGrid();
		RulerRenderer& ruler = GetApp()->GetRuler();

		// Header
		m_lblGridHeader = std::make_shared<M2DrawLabel>(L"■ Grid & Ruler Settings");
		m_pCurrentPanel->AddChild(m_lblGridHeader);

		// Checkboxes: View Grid, Snap to Grid, View Ruler
		m_chkGridView = std::make_shared<M2DrawCheckBox>(L"View Grid", grid.enabled);
		m_chkGridView->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			GetApp()->GetGrid().enabled = bChecked;
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_chkGridView);

		m_chkGridSnap = std::make_shared<M2DrawCheckBox>(L"Snap to Grid", grid.snapToGrid);
		m_chkGridSnap->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			GetApp()->GetGrid().snapToGrid = bChecked;
			GetApp()->GetSnapManager().EnableTarget(SnapTargets::Grid, bChecked);
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_chkGridSnap);

		m_chkRulerView = std::make_shared<M2DrawCheckBox>(L"View Ruler", ruler.enabled);
		m_chkRulerView->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			GetApp()->GetRuler().enabled = bChecked;
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_chkRulerView);

		// Unit ComboBox (Dropdown)
		m_lblUnit = std::make_shared<M2DrawLabel>(L"● Measurement Unit");
		m_pCurrentPanel->AddChild(m_lblUnit);

		m_cboUnit = std::make_shared<M2DrawComboBox>();
		m_cboUnit->AddItem(L"Pixel (px)");
		m_cboUnit->AddItem(L"Millimeter (mm)");
		m_cboUnit->AddItem(L"Centimeter (cm)");
		m_cboUnit->AddItem(L"Inch (in)");
		m_cboUnit->AddItem(L"Web REM (rem)");
		m_cboUnit->SetCurSel(static_cast<int>(grid.unit));
		m_cboUnit->SetOnSelectionChanged([this](M2DrawComboBox* /*pCombo*/, int sel) {
			GetApp()->GetGrid().SetUnit(static_cast<CanvasUnit>(sel));
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_cboUnit);

		// Grid Step: Bar & Number Input (최소값 1.0으로 수정)
		m_lblGridStep = std::make_shared<M2DrawLabel>(L"● Grid Step");
		m_pCurrentPanel->AddChild(m_lblGridStep);

		m_sliderGridStep = std::make_shared<M2DrawSlider>(1.0f, 200.0f, max(1.0f, grid.stepInUnit));
		m_sliderGridStep->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			GetApp()->GetGrid().stepInUnit = val;
			if (m_spinGridStep) m_spinGridStep->SetValue(val);
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_sliderGridStep);

		m_spinGridStep = std::make_shared<M2DrawNumericInput>(1.0, 500.0, max(1.0f, grid.stepInUnit), 1.0);
		m_spinGridStep->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			GetApp()->GetGrid().stepInUnit = static_cast<float>(val);
			if (m_sliderGridStep) m_sliderGridStep->SetValue(static_cast<float>(val));
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_spinGridStep);

		// Major Every: Bar & Number Input
		m_lblGridMajor = std::make_shared<M2DrawLabel>(L"● Major Line Frequency");
		m_pCurrentPanel->AddChild(m_lblGridMajor);

		m_sliderGridMajor = std::make_shared<M2DrawSlider>(1.0f, 20.0f, static_cast<float>(grid.majorEvery));
		m_sliderGridMajor->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			int ival = static_cast<int>(val + 0.5f);
			GetApp()->GetGrid().majorEvery = ival;
			if (m_spinGridMajor) m_spinGridMajor->SetValue(ival);
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_sliderGridMajor);

		m_spinGridMajor = std::make_shared<M2DrawNumericInput>(1.0, 20.0, static_cast<double>(grid.majorEvery), 1.0);
		m_spinGridMajor->SetSuffix(L"x");
		m_spinGridMajor->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			int ival = static_cast<int>(val);
			GetApp()->GetGrid().majorEvery = ival;
			if (m_sliderGridMajor) m_sliderGridMajor->SetValue(static_cast<float>(ival));
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_spinGridMajor);

		// Minor vs Major Color Tabs
		m_btnGridMinorClr = std::make_shared<M2DrawButton>(L"Minor Grid Color");
		m_btnGridMinorClr->SetToggleMode(true);
		m_btnGridMinorClr->SetChecked(m_canvasColorTarget == 0);
		m_btnGridMinorClr->SetOnClick([this](M2DrawButton* /*pBtn*/) {
			m_canvasColorTarget = 0;
			m_btnGridMinorClr->SetChecked(true);
			m_btnGridMajorClr->SetChecked(false);
			if (m_gridWheel) m_gridWheel->SetColorHSL(m_fMinorHue, m_fMinorSat, m_fMinorLight);
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_btnGridMinorClr);

		m_btnGridMajorClr = std::make_shared<M2DrawButton>(L"Major Grid Color");
		m_btnGridMajorClr->SetToggleMode(true);
		m_btnGridMajorClr->SetChecked(m_canvasColorTarget == 1);
		m_btnGridMajorClr->SetOnClick([this](M2DrawButton* /*pBtn*/) {
			m_canvasColorTarget = 1;
			m_btnGridMinorClr->SetChecked(false);
			m_btnGridMajorClr->SetChecked(true);
			if (m_gridWheel) m_gridWheel->SetColorHSL(m_fMajorHue, m_fMajorSat, m_fMajorLight);
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_btnGridMajorClr);

		// Grid Color Wheel Expander
		m_expGridWheel = std::make_shared<M2DrawExpander>(L"Grid Color Wheel", true);
		m_gridWheel = std::make_shared<M2DrawColorWheel>();
		m_gridWheel->SetOnColorChanged([this](float h, float s, float l, bool /*isFinal*/) {
			GridRenderer& g = GetApp()->GetGrid();
			if (m_canvasColorTarget == 0)
			{
				m_fMinorHue = h; m_fMinorSat = s; m_fMinorLight = l;
				g.minor = HSLtoRGB(m_fMinorHue, m_fMinorSat, m_fMinorLight, g.minor.a);
			}
			else
			{
				m_fMajorHue = h; m_fMajorSat = s; m_fMajorLight = l;
				g.major = HSLtoRGB(m_fMajorHue, m_fMajorSat, m_fMajorLight, g.major.a);
			}
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_expGridWheel->AddChild(m_gridWheel);
		m_expGridWheel->SetOnToggle([this](M2DrawExpander* /*pExp*/, bool /*bExpanded*/) {
			if (GetSafeHwnd())
			{
				CRect rc;
				GetClientRect(&rc);
				UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
				SafeInvalidate(FALSE);
			}
		});
		m_pCurrentPanel->AddChild(m_expGridWheel);

		// Grid Opacity: Bar & Number Input
		m_lblGridOpacity = std::make_shared<M2DrawLabel>(L"Grid Line Opacity (%)");
		m_pCurrentPanel->AddChild(m_lblGridOpacity);

		float initGridAlpha = (m_canvasColorTarget == 0 ? grid.minor.a : grid.major.a) * 100.0f;
		m_sliderGridOpacity = std::make_shared<M2DrawSlider>(0.0f, 100.0f, initGridAlpha);
		m_sliderGridOpacity->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			GridRenderer& g = GetApp()->GetGrid();
			if (m_spinGridOpacity) m_spinGridOpacity->SetValue(val);
			if (m_canvasColorTarget == 0)
				g.minor.a = val / 100.0f;
			else
				g.major.a = val / 100.0f;
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_sliderGridOpacity);

		m_spinGridOpacity = std::make_shared<M2DrawNumericInput>(0.0, 100.0, initGridAlpha, 5.0);
		m_spinGridOpacity->SetSuffix(L"%");
		m_spinGridOpacity->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			GridRenderer& g = GetApp()->GetGrid();
			if (m_sliderGridOpacity) m_sliderGridOpacity->SetValue(static_cast<float>(val));
			if (m_canvasColorTarget == 0)
				g.minor.a = static_cast<float>(val) / 100.0f;
			else
				g.major.a = static_cast<float>(val) / 100.0f;
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_spinGridOpacity);
	}
	else
	{
		// [3. Canvas Settings Mode (MODE_CANVAS)]
		m_lblCanvasHeader = std::make_shared<M2DrawLabel>(L"■ Canvas Settings");
		m_pCurrentPanel->AddChild(m_lblCanvasHeader);

		m_lblCanvasSize = std::make_shared<M2DrawLabel>(L"● Canvas Dimensions (W x H)");
		m_pCurrentPanel->AddChild(m_lblCanvasSize);

		m_spinCanvasW = std::make_shared<M2DrawNumericInput>(100.0, 10000.0, 1920.0, 50.0);
		m_spinCanvasW->SetSuffix(L"px");
		m_spinCanvasW->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double /*val*/) {
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_spinCanvasW);

		m_spinCanvasH = std::make_shared<M2DrawNumericInput>(100.0, 10000.0, 1080.0, 50.0);
		m_spinCanvasH->SetSuffix(L"px");
		m_spinCanvasH->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double /*val*/) {
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_spinCanvasH);

		m_lblCanvasBg = std::make_shared<M2DrawLabel>(L"● Background Color");
		m_pCurrentPanel->AddChild(m_lblCanvasBg);

		m_paletteCanvasBg = std::make_shared<M2DrawColorPalette>();
		m_paletteCanvasBg->SetOnColorSelected([this](M2DrawColorPalette* /*pPal*/, D2D1_COLOR_F /*clr*/) {
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_paletteCanvasBg);
	}

	SyncControlsFromData();
}

void CD2DPropertyView::SyncControlsFromData()
{
	if (m_paneMode == MODE_SHAPE)
	{
		if (m_lblShapeHeader)
		{
			CString str;
			if (m_pActiveDoc && m_selectedShapeIndex >= 0)
				str.Format(_T("● Shape: [%s]"), (LPCTSTR)m_strShapeName);
			else
				str = _T("● No Selection (Preview Mode)");
			m_lblShapeHeader->SetText((LPCTSTR)str);
		}

		CanvasUnit curUnit = GetApp()->GetGrid().unit;
		const wchar_t* unitSuffix = GetCanvasUnitSuffix(curUnit);
		float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
		if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;

		if (m_spinTransX) m_spinTransX->SetSuffix(unitSuffix);
		if (m_spinTransY) m_spinTransY->SetSuffix(unitSuffix);
		if (m_spinTransW) m_spinTransW->SetSuffix(unitSuffix);
		if (m_spinTransH) m_spinTransH->SetSuffix(unitSuffix);

		if (m_pActiveDoc && m_selectedShapeIndex >= 0 && m_selectedShapeIndex < static_cast<int>(m_pActiveDoc->GetShapes().size()))
		{
			Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
			if (shape)
			{
				D2D1_RECT_F lb;
				if (shape->GetLocalBounds(nullptr, lb))
				{
					D2D1_POINT_2F origin = shape->GetTransform().TransformPoint(D2D1::Point2F(lb.left, lb.top));
					float curW = (lb.right - lb.left) * std::sqrt(shape->GetTransform()._11 * shape->GetTransform()._11 + shape->GetTransform()._12 * shape->GetTransform()._12);
					float curH = (lb.bottom - lb.top) * std::sqrt(shape->GetTransform()._21 * shape->GetTransform()._21 + shape->GetTransform()._22 * shape->GetTransform()._22);

					const bool prev = m_bUpdating;
					m_bUpdating = true;
					if (m_spinTransX) m_spinTransX->SetValue(origin.x / pxPerUnit, false);
					if (m_spinTransY) m_spinTransY->SetValue(origin.y / pxPerUnit, false);
					if (m_spinTransW) m_spinTransW->SetValue(curW / pxPerUnit, false);
					if (m_spinTransH) m_spinTransH->SetValue(curH / pxPerUnit, false);
					m_bUpdating = prev;
				}
			}
		}

		if (m_btnTabFill)   m_btnTabFill->SetChecked(m_colorTarget == 0);
		if (m_btnTabStroke) m_btnTabStroke->SetChecked(m_colorTarget == 1);

		for (int i = 0; i < static_cast<int>(m_btnFillModes.size()); ++i)
			m_btnFillModes[i]->SetChecked(static_cast<int>(m_fillMode) == i);

		D2D1_COLOR_F curActiveClr = (m_colorTarget == 0) ? m_fillColor : m_strokeColor;
		if (m_palette16) m_palette16->SetSelectedColor(curActiveClr);

		if (m_shapeWheel)
		{
			if (m_colorTarget == 0)
				m_shapeWheel->SetColorHSL(m_fFillHue, m_fFillSat, m_fFillLight);
			else
				m_shapeWheel->SetColorHSL(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight);
		}

		if (m_sliderOpacity) m_sliderOpacity->SetValue(m_fOpacity);
		if (m_spinOpacity)   m_spinOpacity->SetValue(m_fOpacity);

		// 만약 선택된 도형이 있다면 도형의 픽셀 단위 선 굵기로부터 현재 단위값 갱신
		if (m_pActiveDoc && m_selectedShapeIndex >= 0 && m_selectedShapeIndex < static_cast<int>(m_pActiveDoc->GetShapes().size()))
		{
			Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
			if (shape)
			{
				m_fStrokeWidth = shape->GetStyleRef().strokeWidth / pxPerUnit;
			}
		}

		if (m_lblStrokeWidth)
		{
			CString strStrokeLbl;
			strStrokeLbl.Format(_T("Stroke Width (%s)"), unitSuffix);
			m_lblStrokeWidth->SetText((LPCTSTR)strStrokeLbl);
		}

		float defaultMax = 50.0f;
		double stepVal = 0.5;
		switch (curUnit)
		{
		case CanvasUnit::Pixel: defaultMax = 50.0f; stepVal = 0.5; break;
		case CanvasUnit::MM:    defaultMax = 20.0f; stepVal = 0.2; break;
		case CanvasUnit::CM:    defaultMax = 5.0f;  stepVal = 0.05; break;
		case CanvasUnit::Inch:  defaultMax = 2.0f;  stepVal = 0.02; break;
		case CanvasUnit::Rem:   defaultMax = 5.0f;  stepVal = 0.1; break;
		}

		if (m_sliderStrokeWidth)
		{
			float safeVal = max(0.1f, m_fStrokeWidth);
			float targetMax = max(defaultMax, safeVal * 1.5f);
			m_sliderStrokeWidth->SetRange(0.1f, targetMax);
			m_sliderStrokeWidth->SetValue(safeVal);
		}

		if (m_spinStrokeWidth)
		{
			m_spinStrokeWidth->SetSuffix(unitSuffix);
			m_spinStrokeWidth->SetStep(stepVal);
			m_spinStrokeWidth->SetRange(0.01, 1000000.0);
			m_spinStrokeWidth->SetValue(m_fStrokeWidth);
		}

		for (int i = 0; i < static_cast<int>(m_btnDashStyles.size()); ++i)
			m_btnDashStyles[i]->SetChecked(m_selectedDash == i);

		if (m_chkCloseShape)
		{
			bool isClosed = false;
			if (m_pActiveDoc && m_selectedShapeIndex >= 0 && m_selectedShapeIndex < static_cast<int>(m_pActiveDoc->GetShapes().size()))
			{
				PathShape* pathShape = dynamic_cast<PathShape*>(m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get());
				if (pathShape && !pathShape->GetPaths().empty())
				{
					isClosed = pathShape->GetPaths()[0].closed;
				}
			}
			m_chkCloseShape->SetChecked(isClosed);
		}
	}
	else if (m_paneMode == MODE_GRID)
	{
		GridRenderer& grid = GetApp()->GetGrid();
		RulerRenderer& ruler = GetApp()->GetRuler();

		if (m_chkGridView)  m_chkGridView->SetChecked(grid.enabled);
		if (m_chkGridSnap)  m_chkGridSnap->SetChecked(grid.snapToGrid);
		if (m_chkRulerView) m_chkRulerView->SetChecked(ruler.enabled);

		if (m_cboUnit) m_cboUnit->SetCurSel(static_cast<int>(grid.unit));

		if (m_sliderGridStep) m_sliderGridStep->SetValue(grid.stepInUnit);
		if (m_spinGridStep)   m_spinGridStep->SetValue(grid.stepInUnit);

		if (m_sliderGridMajor) m_sliderGridMajor->SetValue(static_cast<float>(grid.majorEvery));
		if (m_spinGridMajor)   m_spinGridMajor->SetValue(static_cast<double>(grid.majorEvery));

		if (m_btnGridMinorClr) m_btnGridMinorClr->SetChecked(m_canvasColorTarget == 0);
		if (m_btnGridMajorClr) m_btnGridMajorClr->SetChecked(m_canvasColorTarget == 1);

		if (m_gridWheel)
		{
			if (m_canvasColorTarget == 0)
				m_gridWheel->SetColorHSL(m_fMinorHue, m_fMinorSat, m_fMinorLight);
			else
				m_gridWheel->SetColorHSL(m_fMajorHue, m_fMajorSat, m_fMajorLight);
		}

		float curAlpha = (m_canvasColorTarget == 0 ? grid.minor.a : grid.major.a) * 100.0f;
		if (m_sliderGridOpacity) m_sliderGridOpacity->SetValue(curAlpha);
		if (m_spinGridOpacity)   m_spinGridOpacity->SetValue(curAlpha);
	}
	else
	{
		// Canvas Settings Mode
		// Future expansion
	}
}

void CD2DPropertyView::SyncTransformOnly()
{
	if (m_paneMode != MODE_SHAPE) return;
	if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size()))
		return;

	Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();
	if (!shape) return;

	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;

	D2D1_RECT_F lb{};
	if (shape->GetLocalBounds(nullptr, lb))
	{
		D2D1_POINT_2F origin = shape->GetTransform().TransformPoint(D2D1::Point2F(lb.left, lb.top));
		float curW = (lb.right - lb.left) * std::sqrt(shape->GetTransform()._11 * shape->GetTransform()._11 + shape->GetTransform()._12 * shape->GetTransform()._12);
		float curH = (lb.bottom - lb.top) * std::sqrt(shape->GetTransform()._21 * shape->GetTransform()._21 + shape->GetTransform()._22 * shape->GetTransform()._22);

		const bool prev = m_bUpdating;
		m_bUpdating = true;
		if (m_spinTransX) m_spinTransX->SetValue(origin.x / pxPerUnit, false);
		if (m_spinTransY) m_spinTransY->SetValue(origin.y / pxPerUnit, false);
		if (m_spinTransW) m_spinTransW->SetValue(curW / pxPerUnit, false);
		if (m_spinTransH) m_spinTransH->SetValue(curH / pxPerUnit, false);
		m_bUpdating = prev;
	}
}

void CD2DPropertyView::UpdateLayout(float width, float /*height*/)
{
	if (!m_pCurrentPanel) return;

	float left = 14.0f;
	float right = width - 14.0f;
	// 패널이 임의로 넓어지더라도 내부 컨트롤들이 기형적으로 늘어나지 않도록 최대 폭 제한
	float contentW = right - left;
	if (contentW > 400.0f)
	{
		contentW = 400.0f;
		right = left + contentW;
	}
	float y = 14.0f;

	if (m_paneMode == MODE_SHAPE)
	{
		// 1. Header Label
		if (m_lblShapeHeader)
		{
			m_lblShapeHeader->SetBounds(left, y, right, y + 20.0f);
			y += 24.0f;
		}

		// Transform Header & Inputs
		if (m_lblTransformHeader)
		{
			m_lblTransformHeader->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}

		float halfW = (contentW - 6.0f) * 0.5f;
		if (m_spinTransX && m_spinTransY)
		{
			m_spinTransX->SetBounds(left, y, left + halfW, y + 24.0f);
			m_spinTransY->SetBounds(left + halfW + 6.0f, y, right, y + 24.0f);
			y += 28.0f;
		}

		if (m_spinTransW && m_spinTransH)
		{
			m_spinTransW->SetBounds(left, y, left + halfW, y + 24.0f);
			m_spinTransH->SetBounds(left + halfW + 6.0f, y, right, y + 24.0f);
			y += 28.0f;
		}

		if (m_chkAspectLink)
		{
			m_chkAspectLink->SetBounds(left, y, right, y + 20.0f);
			y += 26.0f;
		}

		// 2. Tabs: [Fill] vs [Stroke]
		float halfTabW = (contentW - 6.0f) * 0.5f;
		if (m_btnTabFill)   m_btnTabFill->SetBounds(left, y, left + halfTabW, y + 26.0f);
		if (m_btnTabStroke) m_btnTabStroke->SetBounds(left + halfTabW + 6.0f, y, right, y + 26.0f);
		y += 32.0f;

		bool isFill = (m_colorTarget == 0);

		// 3. Fill Mode Toolbar (5 modes)
		if (isFill)
		{
			float btnW = (contentW - (4.0f * 6.0f)) / 5.0f;
			for (int i = 0; i < 5; ++i)
			{
				float bx1 = left + i * (btnW + 6.0f);
				m_btnFillModes[i]->SetVisible(true);
				m_btnFillModes[i]->SetBounds(bx1, y, bx1 + btnW, y + 24.0f);
			}
			y += 30.0f;
		}
		else
		{
			for (int i = 0; i < 5; ++i)
				m_btnFillModes[i]->SetVisible(false);
		}

		// 4. Standard 16-Color Palette
		if (m_palette16)
		{
			m_palette16->SetVisible(true);
			m_palette16->SetBounds(left, y, right, y + 42.0f);
			y += 48.0f;
		}

		// 5. Color Wheel Expander
		if (m_expShapeWheel)
		{
			m_expShapeWheel->SetVisible(true);
			float expH = m_expShapeWheel->GetHeaderHeight();
			if (m_expShapeWheel->IsExpanded())
			{
				float wheelH = min(contentW, 220.0f);
				float wheelLeft = left + (contentW - wheelH) * 0.5f;
				expH += wheelH + 10.0f;
				if (m_shapeWheel)
				{
					m_shapeWheel->SetVisible(true);
					m_shapeWheel->SetBounds(wheelLeft, y + m_expShapeWheel->GetHeaderHeight() + 6.0f, wheelLeft + wheelH, y + expH - 4.0f);
				}
			}
			else
			{
				if (m_shapeWheel) m_shapeWheel->SetVisible(false);
			}
			m_expShapeWheel->SetBounds(left, y, right, y + expH);
			y += expH + 12.0f;
		}

		// 6. Opacity (Label + Slider Bar + Number Input)
		if (m_lblOpacity)
		{
			m_lblOpacity->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}
		float numW = 80.0f; // 100% 텍스트가 잘리거나 줄바꿈되지 않도록 80px로 넉넉하게 확장
		float sliderW = contentW - numW - 8.0f;
		if (m_sliderOpacity)
		{
			m_sliderOpacity->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
		}
		if (m_spinOpacity)
		{
			m_spinOpacity->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
		}
		y += 30.0f;

		// 7. Stroke Width & Dash Styles (Stroke Mode only)
		if (!isFill)
		{
			if (m_lblStrokeWidth)
			{
				m_lblStrokeWidth->SetVisible(true);
				m_lblStrokeWidth->SetBounds(left, y, right, y + 18.0f);
				y += 20.0f;
			}
			if (m_sliderStrokeWidth)
			{
				m_sliderStrokeWidth->SetVisible(true);
				m_sliderStrokeWidth->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
			}
			if (m_spinStrokeWidth)
			{
				m_spinStrokeWidth->SetVisible(true);
				m_spinStrokeWidth->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
			}
			y += 30.0f;

			float dashW = (contentW - (3.0f * 6.0f)) / 4.0f;
			for (int i = 0; i < 4; ++i)
			{
				float bx1 = left + i * (dashW + 6.0f);
				m_btnDashStyles[i]->SetVisible(true);
				m_btnDashStyles[i]->SetBounds(bx1, y, bx1 + dashW, y + 24.0f);
			}
			y += 32.0f;
		}
		else
		{
			if (m_lblStrokeWidth) m_lblStrokeWidth->SetVisible(false);
			if (m_sliderStrokeWidth) m_sliderStrokeWidth->SetVisible(false);
			if (m_spinStrokeWidth) m_spinStrokeWidth->SetVisible(false);
			for (int i = 0; i < 4; ++i)
				m_btnDashStyles[i]->SetVisible(false);
		}

		// 8. Close Shape Checkbox (For PathShape)
		if (m_chkCloseShape)
		{
			bool isPath = false;
			if (m_pActiveDoc && m_selectedShapeIndex >= 0 && m_selectedShapeIndex < static_cast<int>(m_pActiveDoc->GetShapes().size()))
			{
				isPath = (dynamic_cast<PathShape*>(m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get()) != nullptr);
			}
			m_chkCloseShape->SetVisible(isPath);
			if (isPath)
			{
				m_chkCloseShape->SetBounds(left, y, right, y + 24.0f);
				y += 30.0f;
			}
		}
	}
	else if (m_paneMode == MODE_GRID)
	{
		// [MODE_GRID: Grid & Ruler Settings]
		// 1. Header
		if (m_lblGridHeader)
		{
			m_lblGridHeader->SetBounds(left, y, right, y + 20.0f);
			y += 24.0f;
		}

		// 2. Checkboxes (View Grid, Snap to Grid, View Ruler)
		float chkW = (contentW - 12.0f) / 3.0f;
		if (m_chkGridView)  m_chkGridView->SetBounds(left, y, left + chkW, y + 24.0f);
		if (m_chkGridSnap)  m_chkGridSnap->SetBounds(left + chkW + 6.0f, y, left + (chkW * 2.0f) + 6.0f, y + 24.0f);
		if (m_chkRulerView) m_chkRulerView->SetBounds(left + (chkW * 2.0f) + 12.0f, y, right, y + 24.0f);
		y += 32.0f;

		// 3. Measurement Unit ComboBox (Dropdown)
		if (m_lblUnit)
		{
			m_lblUnit->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}
		if (m_cboUnit)
		{
			m_cboUnit->SetBounds(left, y, right, y + 26.0f);
			y += 34.0f;
		}

		// 4. Grid Step (Label + Bar + Number Input)
		if (m_lblGridStep)
		{
			m_lblGridStep->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}
		float numW = 75.0f;
		float sliderW = contentW - numW - 8.0f;
		if (m_sliderGridStep)
		{
			m_sliderGridStep->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
		}
		if (m_spinGridStep)
		{
			m_spinGridStep->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
		}
		y += 30.0f;

		// 5. Major Line Frequency (Label + Bar + Number Input)
		if (m_lblGridMajor)
		{
			m_lblGridMajor->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}
		if (m_sliderGridMajor)
		{
			m_sliderGridMajor->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
		}
		if (m_spinGridMajor)
		{
			m_spinGridMajor->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
		}
		y += 32.0f;

		// 6. Minor vs Major Grid Color Tabs
		float halfTabW = (contentW - 6.0f) * 0.5f;
		if (m_btnGridMinorClr) m_btnGridMinorClr->SetBounds(left, y, left + halfTabW, y + 26.0f);
		if (m_btnGridMajorClr) m_btnGridMajorClr->SetBounds(left + halfTabW + 6.0f, y, right, y + 26.0f);
		y += 32.0f;

		// 7. Grid Color Wheel Expander
		if (m_expGridWheel)
		{
			m_expGridWheel->SetVisible(true);
			float expH = m_expGridWheel->GetHeaderHeight();
			if (m_expGridWheel->IsExpanded())
			{
				float wheelH = min(contentW, 220.0f);
				float wheelLeft = left + (contentW - wheelH) * 0.5f;
				expH += wheelH + 10.0f;
				if (m_gridWheel)
				{
					m_gridWheel->SetVisible(true);
					m_gridWheel->SetBounds(wheelLeft, y + m_expGridWheel->GetHeaderHeight() + 6.0f, wheelLeft + wheelH, y + expH - 4.0f);
				}
			}
			else
			{
				if (m_gridWheel) m_gridWheel->SetVisible(false);
			}
			m_expGridWheel->SetBounds(left, y, right, y + expH);
			y += expH + 12.0f;
		}

		// 8. Grid Line Opacity (Label + Bar + Number Input)
		if (m_lblGridOpacity)
		{
			m_lblGridOpacity->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}
		if (m_sliderGridOpacity)
		{
			m_sliderGridOpacity->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
		}
		if (m_spinGridOpacity)
		{
			m_spinGridOpacity->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
		}
		y += 30.0f;
	}
	else
	{
		// [MODE_CANVAS: Canvas Settings]
		if (m_lblCanvasHeader)
		{
			m_lblCanvasHeader->SetBounds(left, y, right, y + 20.0f);
			y += 24.0f;
		}

		if (m_lblCanvasSize)
		{
			m_lblCanvasSize->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}

		float halfW = (contentW - 6.0f) * 0.5f;
		if (m_spinCanvasW)
		{
			m_spinCanvasW->SetBounds(left, y, left + halfW, y + 26.0f);
		}
		if (m_spinCanvasH)
		{
			m_spinCanvasH->SetBounds(left + halfW + 6.0f, y, right, y + 26.0f);
		}
		y += 34.0f;

		if (m_lblCanvasBg)
		{
			m_lblCanvasBg->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}

		if (m_paletteCanvasBg)
		{
			m_paletteCanvasBg->SetBounds(left, y, right, y + 42.0f);
			y += 48.0f;
		}
	}

	m_pCurrentPanel->SetBounds(0, 0, width, max(y + 20.0f, 600.0f));
}

HRESULT CD2DPropertyView::CreateDeviceResources()
{
	HRESULT hr = S_OK;

	if (!m_pD2DFactory)
	{
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	}

	if (SUCCEEDED(hr) && !m_pDWriteFactory)
	{
		hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormat && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI", nullptr,
			DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			12.0f, L"ko-kr", &m_pTextFormat
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormatBold && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI", nullptr,
			DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			13.0f, L"ko-kr", &m_pTextFormatBold
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormatSmall && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI", nullptr,
			DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			11.0f, L"ko-kr", &m_pTextFormatSmall
		);
		if (m_pTextFormatSmall)
		{
			m_pTextFormatSmall->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		}
	}

	if (SUCCEEDED(hr) && !m_pTextFormatCenter && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI", nullptr,
			DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			11.0f, L"ko-kr", &m_pTextFormatCenter
		);
		if (m_pTextFormatCenter)
		{
			m_pTextFormatCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			m_pTextFormatCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}

	if (SUCCEEDED(hr) && !m_pRenderTarget)
	{
		CRect rc;
		if (GetSafeHwnd()) GetClientRect(&rc);
		D2D1_SIZE_U size = D2D1::SizeU(max(1, rc.Width()), max(1, rc.Height()));

		D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(),
			96.0f, 96.0f
		);

		hr = m_pD2DFactory->CreateHwndRenderTarget(
			rtProps,
			D2D1::HwndRenderTargetProperties(m_hWnd, size),
			&m_pRenderTarget
		);

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pBrush);
		}
	}

	return hr;
}

void CD2DPropertyView::DiscardDeviceResources()
{
	if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
	if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
}

BOOL CD2DPropertyView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CD2DPropertyView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_pRenderTarget)
	{
		m_pRenderTarget->Resize(D2D1::SizeU(cx, cy));
	}
	UpdateLayout(static_cast<float>(cx), static_cast<float>(cy));
	SafeInvalidate(FALSE);
}

void CD2DPropertyView::OnPaint()
{
	CPaintDC dc(this);

	if (FAILED(CreateDeviceResources()) || !m_pRenderTarget)
		return;

	m_pRenderTarget->BeginDraw();

	// 다크 테마 배경 (#1f1f23)
	m_pRenderTarget->Clear(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));

	CRect rc;
	if (GetSafeHwnd()) GetClientRect(&rc);
	float width = static_cast<float>(rc.Width());
	float height = static_cast<float>(rc.Height());
	UpdateLayout(width, height);

	// M2DrawControl 객체 기반 렌더링
	if (m_pCurrentPanel)
	{
		m_pCurrentPanel->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
	}

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
	}
}

void CD2DPropertyView::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	if (m_pCurrentPanel)
	{
		if (m_pCurrentPanel->OnLButtonDown(ptD2D))
		{
			SetCapture();
			SafeInvalidate(FALSE);
		}
	}
}

void CD2DPropertyView::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	if (m_pCurrentPanel)
	{
		if (m_pCurrentPanel->OnMouseMove(ptD2D))
		{
			SafeInvalidate(FALSE);
		}
	}
}

void CD2DPropertyView::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	if (m_pCurrentPanel)
	{
		m_pCurrentPanel->OnLButtonUp(ptD2D);
	}

	if (GetCapture() == this)
	{
		ReleaseCapture();
	}
	SafeInvalidate(FALSE);
}

void CD2DPropertyView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (m_pCurrentPanel)
	{
		if (m_pCurrentPanel->OnKeyDown(nChar))
		{
			SafeInvalidate(FALSE);
			return;
		}
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CD2DPropertyView::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (m_pCurrentPanel)
	{
		if (m_pCurrentPanel->OnChar(nChar))
		{
			SafeInvalidate(FALSE);
			return;
		}
	}
	CWnd::OnChar(nChar, nRepCnt, nFlags);
}

void CD2DPropertyView::OnKillFocus(CWnd* pNewWnd)
{
	if (pNewWnd && ::IsWindow(pNewWnd->GetSafeHwnd()))
	{
		CWnd::OnKillFocus(pNewWnd);
	}
	else
	{
		Default();
	}

	if (m_pCurrentPanel)
	{
		m_pCurrentPanel->ResetState();
		SafeInvalidate(FALSE);
	}
}

UINT CD2DPropertyView::OnGetDlgCode()
{
	return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
}

BOOL CD2DPropertyView::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE || pMsg->wParam == VK_BACK)
		{
			if (m_pCurrentPanel && m_pCurrentPanel->OnKeyDown(static_cast<UINT>(pMsg->wParam)))
			{
				SafeInvalidate(FALSE);
				return TRUE;
			}
		}
	}
	else if (pMsg->message == WM_CHAR)
	{
		if (m_pCurrentPanel && m_pCurrentPanel->OnChar(static_cast<UINT>(pMsg->wParam)))
		{
			SafeInvalidate(FALSE);
			return TRUE;
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}
