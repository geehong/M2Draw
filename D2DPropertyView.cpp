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

	struct CanvasPresetInfo
	{
		std::wstring name;
		float width;
		float height;
		ge::CanvasOriginMode originMode;
	};

	static const std::vector<CanvasPresetInfo> g_canvasPresets = {
		{ L"사용자 지정 (Custom)", 0.0f, 0.0f, ge::CanvasOriginMode::TopLeft },
		// ISO A 시리즈 (가로 / 세로) -> 문서/인쇄: BottomLeft
		{ L"A4 가로 (297 x 210 mm)", 1123.0f, 794.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A4 세로 (210 x 297 mm)", 794.0f, 1123.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A3 가로 (420 x 297 mm)", 1587.0f, 1123.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A3 세로 (297 x 420 mm)", 1123.0f, 1587.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A2 가로 (594 x 420 mm)", 2245.0f, 1587.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A2 세로 (420 x 594 mm)", 1587.0f, 2245.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A1 가로 (841 x 594 mm)", 3179.0f, 2245.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A1 세로 (594 x 841 mm)", 2245.0f, 3179.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A0 가로 (1189 x 841 mm)", 4494.0f, 3179.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A0 세로 (841 x 1189 mm)", 3179.0f, 4494.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A5 가로 (210 x 148 mm)", 794.0f, 559.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"A5 세로 (148 x 210 mm)", 559.0f, 794.0f, ge::CanvasOriginMode::BottomLeft },
		// ISO B 시리즈 -> 문서/인쇄: BottomLeft
		{ L"B4 가로 (353 x 250 mm)", 1334.0f, 945.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"B4 세로 (250 x 353 mm)", 945.0f, 1334.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"B5 가로 (250 x 176 mm)", 945.0f, 665.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"B5 세로 (176 x 250 mm)", 665.0f, 945.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"B0 세로 (1000 x 1414 mm)", 3780.0f, 5344.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"B1 세로 (707 x 1000 mm)", 2672.0f, 3780.0f, ge::CanvasOriginMode::BottomLeft },
		// US 북미 규격 -> 문서/인쇄: BottomLeft
		{ L"US Letter 가로 (11 x 8.5 in)", 1056.0f, 816.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"US Letter 세로 (8.5 x 11 in)", 816.0f, 1056.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"US Legal 세로 (8.5 x 14 in)", 816.0f, 1344.0f, ge::CanvasOriginMode::BottomLeft },
		{ L"US Tabloid (11 x 17 in)", 1056.0f, 1632.0f, ge::CanvasOriginMode::BottomLeft },
		// 웹 & 디스플레이 표준 -> 웹/화면: TopLeft
		{ L"웹 FHD (1920 x 1080 px)", 1920.0f, 1080.0f, ge::CanvasOriginMode::TopLeft },
		{ L"웹 표준 XGA (1024 x 768 px)", 1024.0f, 768.0f, ge::CanvasOriginMode::TopLeft },
		{ L"노트북 HD (1366 x 768 px)", 1366.0f, 768.0f, ge::CanvasOriginMode::TopLeft },
		{ L"디스플레이 QHD (2560 x 1440 px)", 2560.0f, 1440.0f, ge::CanvasOriginMode::TopLeft },
		{ L"4K UHD (3840 x 2160 px)", 3840.0f, 2160.0f, ge::CanvasOriginMode::TopLeft },
		// 모바일 & 태블릿 -> 웹/화면: TopLeft
		{ L"모바일 세로 (390 x 844 px)", 390.0f, 844.0f, ge::CanvasOriginMode::TopLeft },
		{ L"모바일 세로 (412 x 915 px)", 412.0f, 915.0f, ge::CanvasOriginMode::TopLeft },
		{ L"태블릿 iPad 가로 (1080 x 810 px)", 1080.0f, 810.0f, ge::CanvasOriginMode::TopLeft },
		{ L"태블릿 iPad 세로 (810 x 1080 px)", 810.0f, 1080.0f, ge::CanvasOriginMode::TopLeft },
		{ L"태블릿 iPad Pro (1024 x 1366 px)", 1024.0f, 1366.0f, ge::CanvasOriginMode::TopLeft },
		// SNS / 정사각형 -> 웹/화면: TopLeft
		{ L"정사각형 SNS (1080 x 1080 px)", 1080.0f, 1080.0f, ge::CanvasOriginMode::TopLeft }
	};
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
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_WM_MOUSEACTIVATE()
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

ge::Node* CD2DPropertyView::GetSelectedShape() const
{
	return (m_pActiveDoc && m_selectedNodeId) ? m_pActiveDoc->GetDoc().FindById(m_selectedNodeId) : nullptr;
}

bool CD2DPropertyView::IsGroupSelected() const
{
	const ge::Node* n = GetSelectedShape();
	return n && n->Kind() == ge::NodeKind::Group;
}

void CD2DPropertyView::ApplyGroupAppearance()
{
	ge::Node* node = GetSelectedShape();
	if (!m_pActiveDoc || !node || node->Kind() != ge::NodeKind::Group) return;

	node->SetOpacity(m_fOpacity / 100.0f);
	if (m_cboBlend)
	{
		const int sel = m_cboBlend->GetCurSel();
		if (sel >= 0 && sel < ge::kBlendModeCount)
			node->SetBlendMode(static_cast<ge::LayerBlendMode>(sel));
	}
	m_pActiveDoc->UpdateAllViews(nullptr);
}

void CD2DPropertyView::SetTargetShape(CM2DrawDoc* pDoc, ge::Node* pShape)
{
	m_pActiveDoc = pDoc;
	m_selectedNodeId = (pDoc && pShape) ? pShape->GetId() : 0;

	if (ge::Node* shape = GetSelectedShape())
	{
		m_strShapeType = shape->GetShapeTypeName();
		m_strShapeName = shape->GetName().empty() ? m_strShapeType : CString(shape->GetName().c_str());

		const ge::Style& st = shape->GetStyle();
		m_bFillEnabled = (st.fill.type != ge::PaintType::None);
		m_fillColor = (st.fill.type == ge::PaintType::Gradient) ? st.fill.gradient.FallbackColor() : st.fill.color;
		RGBtoHSL(m_fillColor, m_fFillHue, m_fFillSat, m_fFillLight);

		CanvasUnit curUnit = GetApp()->GetGrid().unit;
		float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
		if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;
		m_fStrokeWidth = st.stroke.width / pxPerUnit;
		m_strokeColor = st.stroke.color;
		RGBtoHSL(m_strokeColor, m_fStrokeHue, m_fStrokeSat, m_fStrokeLight);

		m_fOpacity = (m_colorTarget == 0 ? m_fillColor.a : m_strokeColor.a) * 100.0f;

		m_lineCap = st.stroke.cap;
		m_lineJoin = st.stroke.join;
		m_customDash = st.stroke.dashArray;

		// dashArray -> 프리셋 버튼: 4개 버튼에 없는 패턴(대시-점-점 포함)은 "사용자 정의"
		switch (ge::ClassifyDashArray(st.stroke.dashArray, st.stroke.width))
		{
		case ge::DashStyle::Solid:   m_selectedDash = 0; break;
		case ge::DashStyle::Dash:    m_selectedDash = 1; break;
		case ge::DashStyle::Dot:     m_selectedDash = 2; break;
		case ge::DashStyle::DashDot: m_selectedDash = 3; break;
		default:                     m_selectedDash = 4; break;
		}

		// 그룹: "Opacity" 는 채움/선 알파가 아니라 그룹 노드 자체의 불투명도
		if (shape->Kind() == ge::NodeKind::Group)
			m_fOpacity = shape->GetOpacity() * 100.0f;
	}
	else
	{
		m_strShapeName = _T("No Selection");
		m_strShapeType.Empty();
	}

	SyncControlsFromData();
	SafeInvalidate(FALSE);
}

std::vector<float> CD2DPropertyView::EffectiveDashArray() const
{
	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;
	const float widthPx = max(0.1f, m_fStrokeWidth * pxPerUnit);

	switch (m_selectedDash)
	{
	case 1: return ge::MakeDashArray(ge::DashStyle::Dash, widthPx);
	case 2: return ge::MakeDashArray(ge::DashStyle::Dot, widthPx);
	case 3: return ge::MakeDashArray(ge::DashStyle::DashDot, widthPx);
	case 4: return ge::IsValidDashArray(m_customDash) ? m_customDash : std::vector<float>{};
	default: return {};
	}
}

void CD2DPropertyView::ApplyToShape()
{
	if (!GetSelectedShape())
		return;

	ge::Node* shape = GetSelectedShape();
	if (!shape) return;

	// 그룹은 자체 스타일을 렌더링하지 않으므로 채움/선 값을 쓰지 않음 (그룹 속성은 ApplyGroupAppearance)
	if (shape->Kind() == ge::NodeKind::Group) return;

	ge::Style& style = shape->GetStyleRef();
	if (m_bFillEnabled)
	{
		style.fill.type = ge::PaintType::Solid;
		style.fill.color = m_fillColor;
	}
	else
	{
		style.fill.type = ge::PaintType::None;
	}

	style.stroke.color = m_strokeColor;

	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	style.stroke.width = max(0.1f, m_fStrokeWidth * pxPerUnit);

	style.stroke.cap = m_lineCap;
	style.stroke.join = m_lineJoin;

	// 프리셋은 현재 선 두께의 배수로 절대값을 만들어 저장 (두께를 바꾸면 패턴도 같은 비율로 따라감)
	style.stroke.dashArray = EffectiveDashArray();

	shape->Invalidate();
	m_pActiveDoc->UpdateAllViews(nullptr);
}

CM2DrawDoc* CD2DPropertyView::GetActiveDoc() const
{
	if (m_pActiveDoc) return m_pActiveDoc;
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd)))
	{
		CMDIFrameWnd* pFrame = static_cast<CMDIFrameWnd*>(pMainWnd);
		CMDIChildWnd* pChild = pFrame->MDIGetActive();
		if (pChild)
		{
			CDocument* pDoc = pChild->GetActiveDocument();
			if (pDoc && pDoc->IsKindOf(RUNTIME_CLASS(CM2DrawDoc)))
			{
				return static_cast<CM2DrawDoc*>(pDoc);
			}
		}
	}
	return nullptr;
}

void CD2DPropertyView::UpdatePresetComboFromInputs()
{
	if (!m_cboCanvasPreset || !m_spinCanvasW || !m_spinCanvasH) return;
	float w = static_cast<float>(m_spinCanvasW->GetValue());
	float h = static_cast<float>(m_spinCanvasH->GetValue());
	int matchedIdx = 0; // 사용자 지정 (Custom)
	for (size_t i = 1; i < g_canvasPresets.size(); ++i)
	{
		if (std::abs(g_canvasPresets[i].width - w) < 0.5f &&
		    std::abs(g_canvasPresets[i].height - h) < 0.5f)
		{
			matchedIdx = static_cast<int>(i);
			break;
		}
	}
	m_cboCanvasPreset->SetCurSel(matchedIdx, false);
}

void CD2DPropertyView::NotifyGridChanged()
{
	CM2DrawDoc* pDoc = GetActiveDoc();
	if (pDoc)
	{
		pDoc->UpdateAllViews(nullptr);
	}
	else
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
						CDocument* pDocIter = pDocTemplate->GetNextDoc(posDoc);
						if (pDocIter)
						{
							pDocIter->UpdateAllViews(nullptr);
						}
					}
				}
			}
		}
	}

	CMainFrame* pMain = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (pMain)
	{
		CD2DPropertyView& objPropView = pMain->GetPropertiesWnd().GetD2DPropView();
		if (objPropView.GetSafeHwnd() && &objPropView != this)
		{
			objPropView.SyncControlsFromData();
			objPropView.SafeInvalidate(FALSE);
		}
	}

	SafeInvalidate(FALSE);
}

void CD2DPropertyView::ApplyCanvasSettings(bool bLive, bool bForceCommit)
{
	if (m_bUpdating) return;
	// Canvas Settings 모드가 아닐 때는 문서 캔버스/원점 설정을 절대 건드리지 않도록 방어
	if (m_paneMode != MODE_CANVAS) return;

	CM2DrawDoc* pDoc = GetActiveDoc();
	if (pDoc)
	{
		ge::Document& doc = pDoc->GetDoc();
		float newW = m_spinCanvasW ? static_cast<float>(m_spinCanvasW->GetValue()) : doc.width;
		float newH = m_spinCanvasH ? static_cast<float>(m_spinCanvasH->GetValue()) : doc.height;
		
		// 1. 도큐멘트 배경색 (투명도 포함)
		D2D1_COLOR_F docClr = m_paletteDocBg ? m_paletteDocBg->GetSelectedColor() : D2D1::ColorF(1, 1, 1, 1);
		float docAlpha = m_sliderDocOpacity ? (m_sliderDocOpacity->GetValue() / 100.0f) : doc.backgroundColor.a;
		ge::Color newDocBg(docClr.r, docClr.g, docClr.b, docAlpha);

		// 2. 캔버스 작업대(전체 화면) 배경색
		D2D1_COLOR_F canvasClr = m_paletteCanvasBg ? m_paletteCanvasBg->GetSelectedColor() : D2D1::ColorF(0.84f, 0.85f, 0.88f, 1.0f);
		ge::Color newCanvasBg(canvasClr.r, canvasClr.g, canvasClr.b, 1.0f);

		ge::CanvasOriginMode newOrigin = (m_cboOriginMode && m_cboOriginMode->GetCurSel() == 0)
			? ge::CanvasOriginMode::BottomLeft : ge::CanvasOriginMode::TopLeft;

		bool bChanged = doc.width != newW || doc.height != newH || doc.backgroundColor != newDocBg ||
		    doc.canvasColor != newCanvasBg || doc.originMode != newOrigin;
		if (bChanged)
		{
			doc.width = newW;
			doc.height = newH;
			doc.backgroundColor = newDocBg;
			doc.canvasColor = newCanvasBg;
			doc.originMode = newOrigin;
			if (!bLive)
				pDoc->CommitUndoPoint(L"Document & Canvas Settings");
			pDoc->UpdateAllViews(nullptr);
		}
		else if (bForceCommit)
		{
			pDoc->CommitUndoPoint(L"Document & Canvas Settings");
		}

		// 드래그 중에는 다른 문서/패널 동기화를 생략해 부드럽게 갱신
		if (bLive) return;
	}

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
					CDocument* pDocIter = pDocTemplate->GetNextDoc(posDoc);
					if (pDocIter && pDocIter != pDoc)
					{
						pDocIter->UpdateAllViews(nullptr);
					}
				}
			}
		}

		CWnd* pMainWnd = AfxGetMainWnd();
		if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		{
			CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
			pMainFrame->GetOptionBar().SyncSelection(pDoc, GetSelectedShape());
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
			if (!GetSelectedShape()) return;
			ge::Node* shape = GetSelectedShape();
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
			if (!GetSelectedShape()) return;
			ge::Node* shape = GetSelectedShape();
			if (!shape) return;
			CanvasUnit curUnit = GetApp()->GetGrid().unit;
			float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
			float inputPx = static_cast<float>(val) * pxPerUnit;
			bool isBottomLeft = (m_pActiveDoc && m_pActiveDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
			float targetPixelY = (isBottomLeft && m_pActiveDoc) ? (m_pActiveDoc->GetDoc().height - inputPx) : inputPx;
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
			if (!GetSelectedShape()) return;
			ge::Node* shape = GetSelectedShape();
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
			if (!GetSelectedShape()) return;
			ge::Node* shape = GetSelectedShape();
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
			if (IsGroupSelected())
			{
				ApplyGroupAppearance();
				SafeInvalidate(FALSE);
				return;
			}
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
			if (IsGroupSelected())
			{
				ApplyGroupAppearance();
				SafeInvalidate(FALSE);
				return;
			}
			if (m_colorTarget == 0)
				m_fillColor.a = m_fOpacity / 100.0f;
			else
				m_strokeColor.a = m_fOpacity / 100.0f;
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_spinOpacity);

		// 6-1. 혼합 모드 (그룹 전용: 도형은 채움/선 알파만, 노드 단위 혼합 모드는 그룹/레이어에서 지정)
		m_lblBlend = std::make_shared<M2DrawLabel>(L"Blend Mode");
		m_pCurrentPanel->AddChild(m_lblBlend);

		m_cboBlend = std::make_shared<M2DrawComboBox>();
		m_cboBlend->SetItemHeight(20.0f);
		for (int i = 0; i < ge::kBlendModeCount; ++i)
			m_cboBlend->AddItem(ge::BlendModeName(static_cast<ge::LayerBlendMode>(i)));
		m_cboBlend->SetCurSel(0, false);
		m_cboBlend->SetOnSelectionChanged([this](M2DrawComboBox* /*pCombo*/, int /*sel*/) {
			ApplyGroupAppearance();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_cboBlend);

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

		// 8-1. 사용자 정의 Dash Array 입력 (문서 단위 숫자 목록, SVG stroke-dasharray 와 같은 규칙)
		m_lblDashArray = std::make_shared<M2DrawLabel>(L"Dash Array (px, 예: 8 4 2 4)");
		m_pCurrentPanel->AddChild(m_lblDashArray);

		m_txtDashArray = std::make_shared<M2DrawTextInput>();
		m_txtDashArray->SetAllowedChars(L"0123456789.,- ");
		m_txtDashArray->SetPlaceholder(L"실선");
		m_txtDashArray->SetText(ge::FormatDashArrayText(m_customDash));
		m_txtDashArray->SetOnCommit([this](M2DrawTextInput* pInput, const std::wstring& text) {
			// 입력 확정: 파싱 결과가 유효하면 사용자 정의 대시로 적용, 비우면 실선. 잘못된 입력은 현재 값으로 되돌림
			const bool empty = (text.find_first_not_of(L" ,") == std::wstring::npos);
			std::vector<float> parsed = ge::ParseDashArrayText(text);
			if (empty)
			{
				m_customDash.clear();
				m_selectedDash = 0;
			}
			else if (!parsed.empty())
			{
				m_customDash = parsed;
				m_selectedDash = 4;
			}
			pInput->SetText(ge::FormatDashArrayText(EffectiveDashArray()));
			for (int k = 0; k < static_cast<int>(m_btnDashStyles.size()); ++k)
				m_btnDashStyles[k]->SetChecked(k == m_selectedDash);
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_txtDashArray);

		// 8-2. 선 끝(Cap) / 접합(Join) 모양
		m_lblCapJoin = std::make_shared<M2DrawLabel>(L"Cap  /  Join");
		m_pCurrentPanel->AddChild(m_lblCapJoin);

		m_cboLineCap = std::make_shared<M2DrawComboBox>();
		m_cboLineCap->SetItemHeight(20.0f);
		m_cboLineCap->AddItem(L"Butt");
		m_cboLineCap->AddItem(L"Round");
		m_cboLineCap->AddItem(L"Square");
		m_cboLineCap->SetCurSel(static_cast<int>(m_lineCap), false);
		m_cboLineCap->SetOnSelectionChanged([this](M2DrawComboBox* /*pCombo*/, int sel) {
			m_lineCap = static_cast<ge::LineCap>(sel);
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_cboLineCap);

		m_cboLineJoin = std::make_shared<M2DrawComboBox>();
		m_cboLineJoin->SetItemHeight(20.0f);
		m_cboLineJoin->AddItem(L"Miter");
		m_cboLineJoin->AddItem(L"Round");
		m_cboLineJoin->AddItem(L"Bevel");
		m_cboLineJoin->SetCurSel(static_cast<int>(m_lineJoin), false);
		m_cboLineJoin->SetOnSelectionChanged([this](M2DrawComboBox* /*pCombo*/, int sel) {
			m_lineJoin = static_cast<ge::LineJoin>(sel);
			ApplyToShape();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_cboLineJoin);

		// 9. Close Shape Checkbox (For Multi-point / Path Shapes)
		m_chkCloseShape = std::make_shared<M2DrawCheckBox>(L"Close Path (Connect start/end)", false);
		m_chkCloseShape->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			if (GetSelectedShape())
			{
				ge::PathNode* pathShape = dynamic_cast<ge::PathNode*>(GetSelectedShape());
				if (pathShape)
				{
					for (auto& sp : pathShape->GetPathsRef())
					{
						sp.closed = bChecked;
					}
					pathShape->Invalidate();
					m_pActiveDoc->UpdateAllViews(nullptr);
					SafeInvalidate(FALSE);
					SyncOptionBarWithSelection(GetSelectedShape(), m_pActiveDoc);
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
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_chkGridView);

		m_chkGridSnap = std::make_shared<M2DrawCheckBox>(L"Snap to Grid", grid.snapToGrid);
		m_chkGridSnap->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			GetApp()->GetGrid().snapToGrid = bChecked;
			GetApp()->GetSnapManager().EnableTarget(SnapTargets::Grid, bChecked);
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_chkGridSnap);

		m_chkRulerView = std::make_shared<M2DrawCheckBox>(L"View Ruler", ruler.enabled);
		m_chkRulerView->SetOnChanged([this](M2DrawCheckBox* /*pChk*/, bool bChecked) {
			GetApp()->GetRuler().enabled = bChecked;
			NotifyGridChanged();
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
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_cboUnit);

		// Grid Step: Bar & Number Input (최소값 1.0으로 수정)
		m_lblGridStep = std::make_shared<M2DrawLabel>(L"● Grid Step");
		m_pCurrentPanel->AddChild(m_lblGridStep);

		m_sliderGridStep = std::make_shared<M2DrawSlider>(1.0f, 200.0f, max(1.0f, grid.stepInUnit));
		m_sliderGridStep->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			GetApp()->GetGrid().stepInUnit = val;
			if (m_spinGridStep) m_spinGridStep->SetValue(val, false);
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_sliderGridStep);

		m_spinGridStep = std::make_shared<M2DrawNumericInput>(1.0, 500.0, max(1.0f, grid.stepInUnit), 1.0);
		m_spinGridStep->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			GetApp()->GetGrid().stepInUnit = static_cast<float>(val);
			if (m_sliderGridStep) m_sliderGridStep->SetValue(static_cast<float>(val), false);
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_spinGridStep);

		// Major Every: Bar & Number Input
		m_lblGridMajor = std::make_shared<M2DrawLabel>(L"● Major Line Frequency");
		m_pCurrentPanel->AddChild(m_lblGridMajor);

		m_sliderGridMajor = std::make_shared<M2DrawSlider>(1.0f, 20.0f, static_cast<float>(grid.majorEvery));
		m_sliderGridMajor->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			int ival = static_cast<int>(val + 0.5f);
			GetApp()->GetGrid().majorEvery = ival;
			if (m_spinGridMajor) m_spinGridMajor->SetValue(ival, false);
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_sliderGridMajor);

		m_spinGridMajor = std::make_shared<M2DrawNumericInput>(1.0, 20.0, static_cast<double>(grid.majorEvery), 1.0);
		m_spinGridMajor->SetSuffix(L"x");
		m_spinGridMajor->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			int ival = static_cast<int>(val);
			GetApp()->GetGrid().majorEvery = ival;
			if (m_sliderGridMajor) m_sliderGridMajor->SetValue(static_cast<float>(ival), false);
			NotifyGridChanged();
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
			NotifyGridChanged();
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
			if (m_spinGridOpacity) m_spinGridOpacity->SetValue(val, false);
			if (m_canvasColorTarget == 0)
				g.minor.a = val / 100.0f;
			else
				g.major.a = val / 100.0f;
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_sliderGridOpacity);

		m_spinGridOpacity = std::make_shared<M2DrawNumericInput>(0.0, 100.0, initGridAlpha, 5.0);
		m_spinGridOpacity->SetSuffix(L"%");
		m_spinGridOpacity->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			GridRenderer& g = GetApp()->GetGrid();
			if (m_sliderGridOpacity) m_sliderGridOpacity->SetValue(static_cast<float>(val), false);
			if (m_canvasColorTarget == 0)
				g.minor.a = static_cast<float>(val) / 100.0f;
			else
				g.major.a = static_cast<float>(val) / 100.0f;
			NotifyGridChanged();
		});
		m_pCurrentPanel->AddChild(m_spinGridOpacity);
	}
	else
	{
		// [3. Document & Canvas Settings (MODE_CANVAS)]
		m_lblCanvasHeader = std::make_shared<M2DrawLabel>(L"■ Document & Canvas Settings");
		m_pCurrentPanel->AddChild(m_lblCanvasHeader);

		// 1. Document Dimensions (문서 규격 W x H)
		m_lblDocSize = std::make_shared<M2DrawLabel>(L"● Document Dimensions (W x H)");
		m_pCurrentPanel->AddChild(m_lblDocSize);

		m_spinCanvasW = std::make_shared<M2DrawNumericInput>(100.0, 10000.0, 1920.0, 50.0);
		m_spinCanvasW->SetSuffix(L"px");
		m_spinCanvasW->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double /*val*/) {
			UpdatePresetComboFromInputs();
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_spinCanvasW);

		m_spinCanvasH = std::make_shared<M2DrawNumericInput>(100.0, 10000.0, 1080.0, 50.0);
		m_spinCanvasH->SetSuffix(L"px");
		m_spinCanvasH->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double /*val*/) {
			UpdatePresetComboFromInputs();
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_spinCanvasH);

		// 캔버스 표준 규격 프리셋 콤보박스 (드롭다운)
		m_cboCanvasPreset = std::make_shared<M2DrawComboBox>();
		m_cboCanvasPreset->SetItemHeight(20.0f);
		for (const auto& item : g_canvasPresets)
		{
			m_cboCanvasPreset->AddItem(item.name);
		}
		m_cboCanvasPreset->SetCurSel(21, false); // Default: "웹 FHD (1920 x 1080 px)"
		m_cboCanvasPreset->SetOnSelectionChanged([this](M2DrawComboBox* /*pCombo*/, int sel) {
			if (sel > 0 && sel < static_cast<int>(g_canvasPresets.size()))
			{
				const auto& preset = g_canvasPresets[sel];
				if (m_spinCanvasW) m_spinCanvasW->SetValue(preset.width, false);
				if (m_spinCanvasH) m_spinCanvasH->SetValue(preset.height, false);
				if (m_cboOriginMode)
				{
					m_cboOriginMode->SetCurSel(preset.originMode == ge::CanvasOriginMode::BottomLeft ? 0 : 1, false);
				}
				ApplyCanvasSettings();
				SafeInvalidate(FALSE);
			}
		});
		m_pCurrentPanel->AddChild(m_cboCanvasPreset);

		// 좌표계 원점 모드 선택
		m_lblOriginMode = std::make_shared<M2DrawLabel>(L"● Coordinate Origin (0,0)");
		m_pCurrentPanel->AddChild(m_lblOriginMode);

		m_cboOriginMode = std::make_shared<M2DrawComboBox>();
		m_cboOriginMode->SetItemHeight(20.0f);
		m_cboOriginMode->AddItem(L"좌하단 (0,0) - 문서/인쇄 규격 (Y↑)");
		m_cboOriginMode->AddItem(L"좌상단 (0,0) - 웹/화면 규격 (Y↓)");
		CM2DrawDoc* pCurDoc = GetActiveDoc();
		int defaultOriginSel = (pCurDoc && pCurDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft) ? 0 : 1;
		m_cboOriginMode->SetCurSel(defaultOriginSel, false);
		m_cboOriginMode->SetOnSelectionChanged([this](M2DrawComboBox* /*pCombo*/, int /*sel*/) {
			ApplyCanvasSettings();
			SafeInvalidate(FALSE);
		});
		m_pCurrentPanel->AddChild(m_cboOriginMode);

		// 2. Document Background Color (도큐멘트 용지 배경색 + 투명도)
		m_lblDocBg = std::make_shared<M2DrawLabel>(L"● Document Background Color");
		m_pCurrentPanel->AddChild(m_lblDocBg);

		m_paletteDocBg = std::make_shared<M2DrawColorPalette>();
		m_paletteDocBg->SetSelectedColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
		m_paletteDocBg->SetOnColorSelected([this](M2DrawColorPalette* /*pPal*/, D2D1_COLOR_F /*clr*/) {
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_paletteDocBg);

		m_lblDocOpacity = std::make_shared<M2DrawLabel>(L"Document Opacity (%)");
		m_pCurrentPanel->AddChild(m_lblDocOpacity);

		float initDocAlpha = pCurDoc ? (pCurDoc->GetDoc().backgroundColor.a * 100.0f) : 100.0f;
		m_sliderDocOpacity = std::make_shared<M2DrawSlider>(0.0f, 100.0f, initDocAlpha);
		m_sliderDocOpacity->SetOnChanged([this](M2DrawSlider* /*pSlider*/, float val) {
			if (m_spinDocOpacity) m_spinDocOpacity->SetValue(val, false);
			// 드래그 중이면 가벼운 라이브 갱신, 클릭 등 단발 변경이면 즉시 확정
			ApplyCanvasSettings(m_sliderDocOpacity && m_sliderDocOpacity->IsDragging());
		});
		m_sliderDocOpacity->SetOnReleased([this](M2DrawSlider* /*pSlider*/) {
			ApplyCanvasSettings(false, true);
		});
		m_pCurrentPanel->AddChild(m_sliderDocOpacity);

		m_spinDocOpacity = std::make_shared<M2DrawNumericInput>(0.0, 100.0, initDocAlpha, 5.0);
		m_spinDocOpacity->SetSuffix(L"%");
		m_spinDocOpacity->SetOnChanged([this](M2DrawNumericInput* /*pNum*/, double val) {
			if (m_sliderDocOpacity) m_sliderDocOpacity->SetValue(static_cast<float>(val), false);
			ApplyCanvasSettings();
		});
		m_pCurrentPanel->AddChild(m_spinDocOpacity);

		// 3. Canvas Workspace Color (전체 월드 화면 배경색)
		m_lblCanvasBg = std::make_shared<M2DrawLabel>(L"● Canvas Workspace Color (전체 화면)");
		m_pCurrentPanel->AddChild(m_lblCanvasBg);

		m_paletteCanvasBg = std::make_shared<M2DrawColorPalette>();
		m_paletteCanvasBg->SetSelectedColor(D2D1::ColorF(0.84f, 0.85f, 0.88f, 1.0f));
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
			if (IsGroupSelected())
			{
				const ge::GroupNode* grp = static_cast<const ge::GroupNode*>(GetSelectedShape());
				str.Format(_T("● Group: [%s] (%d objects)"), (LPCTSTR)m_strShapeName, static_cast<int>(grp->GetChildren().size()));
			}
			else if (GetSelectedShape())
				str.Format(_T("● Shape: [%s]"), (LPCTSTR)m_strShapeName);
			else
				str = _T("● No Selection (Preview Mode)");
			m_lblShapeHeader->SetText((LPCTSTR)str);
		}

		// 그룹이면 "Opacity" 가 노드 불투명도임을 라벨로 구분하고 혼합 모드 콤보를 현재 값으로 맞춤
		if (m_lblOpacity)
			m_lblOpacity->SetText(IsGroupSelected() ? L"Group Opacity (%)" : L"Opacity (%)");
		if (m_cboBlend && IsGroupSelected())
			m_cboBlend->SetCurSel(static_cast<int>(GetSelectedShape()->GetBlendMode()), false);

		CanvasUnit curUnit = GetApp()->GetGrid().unit;
		const wchar_t* unitSuffix = GetCanvasUnitSuffix(curUnit);
		float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
		if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;

		if (m_spinTransX) m_spinTransX->SetSuffix(unitSuffix);
		if (m_spinTransY) m_spinTransY->SetSuffix(unitSuffix);
		if (m_spinTransW) m_spinTransW->SetSuffix(unitSuffix);
		if (m_spinTransH) m_spinTransH->SetSuffix(unitSuffix);

		if (GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
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
					bool isBottomLeft = (m_pActiveDoc && m_pActiveDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
					float dispY = (isBottomLeft && m_pActiveDoc) ? (m_pActiveDoc->GetDoc().height - origin.y) : origin.y;
					if (m_spinTransX) m_spinTransX->SetValue(origin.x / pxPerUnit, false);
					if (m_spinTransY) m_spinTransY->SetValue(dispY / pxPerUnit, false);
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
		if (GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			if (shape)
			{
				m_fStrokeWidth = shape->GetStyle().stroke.width / pxPerUnit;
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
		if (m_txtDashArray) m_txtDashArray->SetText(ge::FormatDashArrayText(EffectiveDashArray()));
		if (m_cboLineCap)   m_cboLineCap->SetCurSel(static_cast<int>(m_lineCap), false);
		if (m_cboLineJoin)  m_cboLineJoin->SetCurSel(static_cast<int>(m_lineJoin), false);

		if (m_chkCloseShape)
		{
			bool isClosed = false;
			if (GetSelectedShape())
			{
				ge::PathNode* pathShape = dynamic_cast<ge::PathNode*>(GetSelectedShape());
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
		CM2DrawDoc* pDoc = GetActiveDoc();
		if (pDoc)
		{
			const ge::Document& doc = pDoc->GetDoc();
			const bool prev = m_bUpdating;
			m_bUpdating = true;
			if (m_spinCanvasW) m_spinCanvasW->SetValue(doc.width, false);
			if (m_spinCanvasH) m_spinCanvasH->SetValue(doc.height, false);
			UpdatePresetComboFromInputs();
			if (m_cboOriginMode)
			{
				m_cboOriginMode->SetCurSel(doc.originMode == ge::CanvasOriginMode::BottomLeft ? 0 : 1, false);
			}
			if (m_paletteDocBg)
			{
				m_paletteDocBg->SetSelectedColor(D2D1::ColorF(
					doc.backgroundColor.r,
					doc.backgroundColor.g,
					doc.backgroundColor.b,
					1.0f
				));
			}
			float docAlpha = doc.backgroundColor.a * 100.0f;
			if (m_sliderDocOpacity) m_sliderDocOpacity->SetValue(docAlpha);
			if (m_spinDocOpacity)   m_spinDocOpacity->SetValue(docAlpha);

			if (m_paletteCanvasBg)
			{
				m_paletteCanvasBg->SetSelectedColor(D2D1::ColorF(
					doc.canvasColor.r,
					doc.canvasColor.g,
					doc.canvasColor.b,
					1.0f
				));
			}
			m_bUpdating = prev;
		}
	}
}

void CD2DPropertyView::SyncTransformOnly()
{
	if (m_paneMode != MODE_SHAPE) return;
	if (!GetSelectedShape())
		return;

	ge::Node* shape = GetSelectedShape();
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
		bool isBottomLeft = (m_pActiveDoc && m_pActiveDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
		float dispY = (isBottomLeft && m_pActiveDoc) ? (m_pActiveDoc->GetDoc().height - origin.y) : origin.y;
		if (m_spinTransX) m_spinTransX->SetValue(origin.x / pxPerUnit, false);
		if (m_spinTransY) m_spinTransY->SetValue(dispY / pxPerUnit, false);
		if (m_spinTransW) m_spinTransW->SetValue(curW / pxPerUnit, false);
		if (m_spinTransH) m_spinTransH->SetValue(curH / pxPerUnit, false);
		m_bUpdating = prev;
	}
}

float CD2DPropertyView::LayoutGroupPanel(float left, float right, float contentW, float y)
{
	// 그룹은 자체 채움/선을 렌더링하지 않으므로 도형용 스타일 컨트롤을 모두 숨김
	if (m_btnTabFill)   m_btnTabFill->SetVisible(false);
	if (m_btnTabStroke) m_btnTabStroke->SetVisible(false);
	for (auto& b : m_btnFillModes) b->SetVisible(false);
	if (m_palette16)    m_palette16->SetVisible(false);
	if (m_expShapeWheel) m_expShapeWheel->SetVisible(false);
	if (m_shapeWheel)   m_shapeWheel->SetVisible(false);
	if (m_lblStrokeWidth)    m_lblStrokeWidth->SetVisible(false);
	if (m_sliderStrokeWidth) m_sliderStrokeWidth->SetVisible(false);
	if (m_spinStrokeWidth)   m_spinStrokeWidth->SetVisible(false);
	for (auto& b : m_btnDashStyles) b->SetVisible(false);
	if (m_lblDashArray) m_lblDashArray->SetVisible(false);
	if (m_txtDashArray) m_txtDashArray->SetVisible(false);
	if (m_lblCapJoin)   m_lblCapJoin->SetVisible(false);
	if (m_cboLineCap)   m_cboLineCap->SetVisible(false);
	if (m_cboLineJoin)  m_cboLineJoin->SetVisible(false);
	if (m_chkCloseShape) m_chkCloseShape->SetVisible(false);

	// Appearance: 그룹 불투명도 + 혼합 모드
	if (m_lblOpacity)
	{
		m_lblOpacity->SetVisible(true);
		m_lblOpacity->SetBounds(left, y, right, y + 18.0f);
		y += 20.0f;
	}
	const float numW = 80.0f;
	const float sliderW = contentW - numW - 8.0f;
	if (m_sliderOpacity)
	{
		m_sliderOpacity->SetVisible(true);
		m_sliderOpacity->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
	}
	if (m_spinOpacity)
	{
		m_spinOpacity->SetVisible(true);
		m_spinOpacity->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
	}
	y += 34.0f;

	if (m_lblBlend)
	{
		m_lblBlend->SetVisible(true);
		m_lblBlend->SetBounds(left, y, right, y + 18.0f);
		y += 20.0f;
	}
	if (m_cboBlend)
	{
		m_cboBlend->SetVisible(true);
		m_cboBlend->SetBounds(left, y, right, y + 26.0f);
		y += 34.0f;
	}
	return y;
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

		// 그룹: 도형용 채우기/선 UI 대신 Appearance(불투명도/혼합 모드)만 표시
		if (IsGroupSelected())
		{
			y = LayoutGroupPanel(left, right, contentW, y);
			m_pCurrentPanel->SetBounds(0, 0, width, max(y + 20.0f, 600.0f));
			return;
		}
		// 도형/미리보기: 그룹 전용 컨트롤 숨기고 도형용 탭 복원
		if (m_lblBlend) m_lblBlend->SetVisible(false);
		if (m_cboBlend) m_cboBlend->SetVisible(false);
		if (m_btnTabFill)   m_btnTabFill->SetVisible(true);
		if (m_btnTabStroke) m_btnTabStroke->SetVisible(true);

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

			// 사용자 정의 Dash Array 입력
			if (m_lblDashArray)
			{
				m_lblDashArray->SetVisible(true);
				m_lblDashArray->SetBounds(left, y, right, y + 18.0f);
				y += 20.0f;
			}
			if (m_txtDashArray)
			{
				m_txtDashArray->SetVisible(true);
				m_txtDashArray->SetBounds(left, y, right, y + 24.0f);
				y += 32.0f;
			}

			// Cap / Join 콤보 (좌우 반반)
			if (m_lblCapJoin)
			{
				m_lblCapJoin->SetVisible(true);
				m_lblCapJoin->SetBounds(left, y, right, y + 18.0f);
				y += 20.0f;
			}
			const float comboW = (contentW - 6.0f) * 0.5f;
			if (m_cboLineCap)
			{
				m_cboLineCap->SetVisible(true);
				m_cboLineCap->SetBounds(left, y, left + comboW, y + 26.0f);
			}
			if (m_cboLineJoin)
			{
				m_cboLineJoin->SetVisible(true);
				m_cboLineJoin->SetBounds(left + comboW + 6.0f, y, right, y + 26.0f);
			}
			y += 34.0f;
		}
		else
		{
			if (m_lblStrokeWidth) m_lblStrokeWidth->SetVisible(false);
			if (m_sliderStrokeWidth) m_sliderStrokeWidth->SetVisible(false);
			if (m_spinStrokeWidth) m_spinStrokeWidth->SetVisible(false);
			for (int i = 0; i < 4; ++i)
				m_btnDashStyles[i]->SetVisible(false);
			if (m_lblDashArray) m_lblDashArray->SetVisible(false);
			if (m_txtDashArray) m_txtDashArray->SetVisible(false);
			if (m_lblCapJoin)   m_lblCapJoin->SetVisible(false);
			if (m_cboLineCap)   m_cboLineCap->SetVisible(false);
			if (m_cboLineJoin)  m_cboLineJoin->SetVisible(false);
		}

		// 8. Close Shape Checkbox (For PathShape)
		if (m_chkCloseShape)
		{
			bool isPath = false;
			if (GetSelectedShape())
			{
				isPath = (dynamic_cast<ge::PathNode*>(GetSelectedShape()) != nullptr);
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
		// [MODE_CANVAS: Document & Canvas Settings]
		if (m_lblCanvasHeader)
		{
			m_lblCanvasHeader->SetBounds(left, y, right, y + 20.0f);
			y += 24.0f;
		}

		if (m_lblDocSize)
		{
			m_lblDocSize->SetBounds(left, y, right, y + 18.0f);
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
		y += 32.0f;

		if (m_cboCanvasPreset)
		{
			m_cboCanvasPreset->SetBounds(left, y, right, y + 26.0f);
			y += 34.0f;
		}

		if (m_lblOriginMode)
		{
			m_lblOriginMode->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}

		if (m_cboOriginMode)
		{
			m_cboOriginMode->SetBounds(left, y, right, y + 26.0f);
			y += 34.0f;
		}

		// 2. Document Background Color (도큐멘트 용지 배경색)
		if (m_lblDocBg)
		{
			m_lblDocBg->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}

		if (m_paletteDocBg)
		{
			m_paletteDocBg->SetBounds(left, y, right, y + 42.0f);
			y += 46.0f;
		}

		if (m_lblDocOpacity)
		{
			m_lblDocOpacity->SetBounds(left, y, right, y + 18.0f);
			y += 20.0f;
		}
		float numW = 75.0f;
		float sliderW = contentW - numW - 8.0f;
		if (m_sliderDocOpacity)
		{
			m_sliderDocOpacity->SetBounds(left, y + 2.0f, left + sliderW, y + 22.0f);
		}
		if (m_spinDocOpacity)
		{
			m_spinDocOpacity->SetBounds(left + sliderW + 8.0f, y, right, y + 24.0f);
		}
		y += 34.0f;

		// 3. Canvas Workspace Color (전체 월드 화면 배경색)
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
	SetFocus();
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	// 이벤트 처리 중 콜백이 패널을 재구성(SetPaneMode -> InitControls)해도 실행 중인 패널이 해제되지 않도록 지역 참조로 유지
	if (std::shared_ptr<M2DrawPanel> panel = m_pCurrentPanel)
	{
		if (panel->OnLButtonDown(ptD2D))
		{
			SetCapture();
			SafeInvalidate(FALSE);
		}
	}
}

void CD2DPropertyView::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	// 이벤트 처리 중 콜백이 패널을 재구성(SetPaneMode -> InitControls)해도 실행 중인 패널이 해제되지 않도록 지역 참조로 유지
	if (std::shared_ptr<M2DrawPanel> panel = m_pCurrentPanel)
	{
		if (panel->OnMouseMove(ptD2D))
		{
			SafeInvalidate(FALSE);
		}
	}
}

void CD2DPropertyView::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	// 이벤트 처리 중 콜백이 패널을 재구성(SetPaneMode -> InitControls)해도 실행 중인 패널이 해제되지 않도록 지역 참조로 유지
	if (std::shared_ptr<M2DrawPanel> panel = m_pCurrentPanel)
	{
		panel->OnLButtonUp(ptD2D);
		// 슬라이더/스핀/체크박스 등 한 번의 조작이 끝난 시점에 변경이 있었으면 Undo 이력에 기록 (드래그 중에는 기록하지 않음)
		CommitActiveDocUndo(L"속성 변경");
	}

	if (GetCapture() == this)
	{
		ReleaseCapture();
	}
	SafeInvalidate(FALSE);
}

void CD2DPropertyView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// 이벤트 처리 중 콜백이 패널을 재구성(SetPaneMode -> InitControls)해도 실행 중인 패널이 해제되지 않도록 지역 참조로 유지
	if (std::shared_ptr<M2DrawPanel> panel = m_pCurrentPanel)
	{
		if (panel->OnKeyDown(nChar))
		{
			CommitActiveDocUndo(L"속성 변경");
			SafeInvalidate(FALSE);
			return;
		}
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CD2DPropertyView::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// 이벤트 처리 중 콜백이 패널을 재구성(SetPaneMode -> InitControls)해도 실행 중인 패널이 해제되지 않도록 지역 참조로 유지
	if (std::shared_ptr<M2DrawPanel> panel = m_pCurrentPanel)
	{
		if (panel->OnChar(nChar))
		{
			CommitActiveDocUndo(L"속성 변경");
			SafeInvalidate(FALSE);
			return;
		}
	}
	CWnd::OnChar(nChar, nRepCnt, nFlags);
}

void CD2DPropertyView::OnSetFocus(CWnd* pOldWnd)
{
	CWnd::OnSetFocus(pOldWnd);
	SafeInvalidate(FALSE);
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

int CD2DPropertyView::OnMouseActivate(CWnd* /*pDesktopWnd*/, UINT /*nHitTest*/, UINT /*message*/)
{
	return MA_ACTIVATE;
}

UINT CD2DPropertyView::OnGetDlgCode()
{
	return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
}

BOOL CD2DPropertyView::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (m_pCurrentPanel && m_pCurrentPanel->OnKeyDown(static_cast<UINT>(pMsg->wParam)))
		{
			SafeInvalidate(FALSE);
			return TRUE;
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
