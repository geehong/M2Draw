#include "pch.h"
#include "ToolOptionBar.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"
#include "M2DrawView.h"
#include "MainFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CToolOptionBar, CDockablePane)

BEGIN_MESSAGE_MAP(CToolOptionBar, CDockablePane)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

CToolOptionBar::CToolOptionBar() noexcept
{
}

CToolOptionBar::~CToolOptionBar()
{
	DiscardDeviceResources();
	if (m_pTextFormatSmall) { m_pTextFormatSmall->Release(); m_pTextFormatSmall = nullptr; }
	if (m_pTextFormat) { m_pTextFormat->Release(); m_pTextFormat = nullptr; }
	if (m_pDWriteFactory) { m_pDWriteFactory->Release(); m_pDWriteFactory = nullptr; }
	if (m_pD2DFactory) { m_pD2DFactory->Release(); m_pD2DFactory = nullptr; }
}

int CToolOptionBar::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));

	if (m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ko-kr", &m_pTextFormat);

		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"ko-kr", &m_pTextFormatSmall);
	}

	InitControls();
	GetApp()->GetToolManager().AddListener(this);

	EnableGripper(FALSE);
	SetMinSize(CSize(400, 38));

	return 0;
}

void CToolOptionBar::OnDestroy()
{
	GetApp()->GetToolManager().RemoveListener(this);
	CDockablePane::OnDestroy();
}

void CToolOptionBar::InitControls()
{
	m_pPanel = std::make_shared<M2DrawPanel>();

	// 1. 도구/도형 타이틀 라벨
	m_lblTitle = std::make_shared<M2DrawLabel>(L"도형 만들기:");
	m_pPanel->AddChild(m_lblTitle);

	// 2. Sx (시작점 X 또는 현재 X)
	m_spinX = std::make_shared<M2DrawNumericInput>(-9999.0, 9999.0, 0.0, 1.0);
	m_spinX->SetSuffix(L"px");
	m_pPanel->AddChild(m_spinX);

	// 3. Sy (시작점 Y 또는 현재 Y)
	m_spinY = std::make_shared<M2DrawNumericInput>(-9999.0, 9999.0, 0.0, 1.0);
	m_spinY->SetSuffix(L"px");
	m_pPanel->AddChild(m_spinY);

	// 4. W (너비)
	m_spinW = std::make_shared<M2DrawNumericInput>(1.0, 9999.0, 100.0, 5.0);
	m_spinW->SetSuffix(L"W");
	m_spinW->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		m_curW = static_cast<float>(val);
	});
	m_pPanel->AddChild(m_spinW);

	// 5. H (높이)
	m_spinH = std::make_shared<M2DrawNumericInput>(1.0, 9999.0, 80.0, 5.0);
	m_spinH->SetSuffix(L"H");
	m_spinH->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		m_curH = static_cast<float>(val);
	});
	m_pPanel->AddChild(m_spinH);

	// 6. Rx (모서리 X)
	m_spinRx = std::make_shared<M2DrawNumericInput>(0.0, 500.0, 0.0, 1.0);
	m_spinRx->SetSuffix(L"Rx");
	m_spinRx->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.cornerRx = static_cast<float>(val);
			if (s.bLinkRxRy) { s.cornerRy = static_cast<float>(val); m_spinRy->SetValue(val); }
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_spinRx);

	// 7. Ry (모서리 Y)
	m_spinRy = std::make_shared<M2DrawNumericInput>(0.0, 500.0, 0.0, 1.0);
	m_spinRy->SetSuffix(L"Ry");
	m_spinRy->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.cornerRy = static_cast<float>(val);
			if (s.bLinkRxRy) { s.cornerRx = static_cast<float>(val); m_spinRx->SetValue(val); }
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_spinRy);

	// 8. Rx=Ry 비율 링크
	m_chkLinkRxRy = std::make_shared<M2DrawCheckBox>(L"🔗", true);
	m_chkLinkRxRy->SetOnChanged([this](M2DrawCheckBox* /*p*/, bool bChecked) {
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.bLinkRxRy = bChecked;
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_chkLinkRxRy);

	// 9. 모서리 뾰족하게 리셋
	m_btnResetCorners = std::make_shared<M2DrawButton>(L"⏢ 리셋");
	m_btnResetCorners->SetOnClick([this](M2DrawButton* /*p*/) {
		m_spinRx->SetValue(0.0f);
		m_spinRy->SetValue(0.0f);
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.cornerRx = 0.0f;
			s.cornerRy = 0.0f;
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_btnResetCorners);

	// 10. 꼭짓점 / 톱니 수 (별, 정다각형, 기어)
	m_spinPoints = std::make_shared<M2DrawNumericInput>(3.0, 64.0, 5.0, 1.0);
	m_spinPoints->SetSuffix(L"pts");
	m_spinPoints->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.starPoints = static_cast<int>(val);
			s.polySides = static_cast<int>(val);
			s.gearTeeth = static_cast<int>(val);
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_spinPoints);

	// 11. 비율 (별 내경 비율 / 기어 톱니 깊이)
	m_spinRatio = std::make_shared<M2DrawNumericInput>(0.05, 0.95, 0.5, 0.05);
	m_spinRatio->SetSuffix(L"ratio");
	m_spinRatio->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.starInnerRatio = static_cast<float>(val);
			s.gearDepthRatio = static_cast<float>(val);
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_spinRatio);

	// 12. 나선 바퀴 수
	m_spinTurns = std::make_shared<M2DrawNumericInput>(0.5, 20.0, 3.0, 0.5);
	m_spinTurns->SetSuffix(L"turns");
	m_spinTurns->SetOnChanged([this](M2DrawNumericInput* /*p*/, double val) {
		auto* tool = GetApp()->GetToolManager().GetActiveTool();
		auto* baseTool = dynamic_cast<BaseShapeTool*>(tool);
		if (baseTool)
		{
			ToolCreationSetting s = baseTool->GetSetting();
			s.spiralTurns = static_cast<float>(val);
			baseTool->SetSetting(s);
		}
	});
	m_pPanel->AddChild(m_spinTurns);

	SyncFromToolSetting();
}

void CToolOptionBar::OnToolChanged(ToolType newTool)
{
	m_currentTool = newTool;
	m_selectedShapeIdx = -1;
	SyncFromToolSetting();
	if (GetSafeHwnd())
	{
		CRect rc;
		GetClientRect(&rc);
		UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
		Invalidate(FALSE);
	}
}

void CToolOptionBar::SyncFromToolSetting()
{
	CString title;
	switch (m_currentTool)
	{
	case ToolType::Select:      title = L"선택 / 편집:"; break;
	case ToolType::Rect:        title = L"사각형 만들기:"; break;
	case ToolType::Triangle:    title = L"삼각형 만들기:"; break;
	case ToolType::Ellipse:     title = L"원/타원 만들기:"; break;
	case ToolType::Polygon:     title = L"다각형 만들기:"; break;
	case ToolType::Star:        title = L"별 만들기:"; break;
	case ToolType::Gear:        title = L"기어 만들기:"; break;
	case ToolType::Spiral:      title = L"스파이럴 만들기:"; break;
	case ToolType::Heart:       title = L"하트 만들기:"; break;
	case ToolType::Pen:         title = L"펜/베이지어:"; break;
	default:                    title = L"도구 옵션:"; break;
	}

	if (m_lblTitle) m_lblTitle->SetText((LPCTSTR)title);

	// 컨트롤 표시/숨김 분기
	bool isRect = (m_currentTool == ToolType::Rect);
	bool isPoints = (m_currentTool == ToolType::Star || m_currentTool == ToolType::Gear || m_currentTool == ToolType::Polygon);
	bool isRatio = (m_currentTool == ToolType::Star || m_currentTool == ToolType::Gear);
	bool isSpiral = (m_currentTool == ToolType::Spiral);

	if (m_spinRx) m_spinRx->SetVisible(isRect);
	if (m_spinRy) m_spinRy->SetVisible(isRect);
	if (m_chkLinkRxRy) m_chkLinkRxRy->SetVisible(isRect);
	if (m_btnResetCorners) m_btnResetCorners->SetVisible(isRect);

	if (m_spinPoints) m_spinPoints->SetVisible(isPoints);
	if (m_spinRatio) m_spinRatio->SetVisible(isRatio);
	if (m_spinTurns) m_spinTurns->SetVisible(isSpiral);
}

void CToolOptionBar::SyncFromSelectedShape(CM2DrawDoc* pDoc, int shapeIdx)
{
	m_selectedShapeIdx = shapeIdx;
	if (!pDoc || shapeIdx < 0 || shapeIdx >= static_cast<int>(pDoc->GetShapes().size()))
	{
		SyncFromToolSetting();
		return;
	}

	auto* shape = pDoc->GetShapes()[shapeIdx].get();
	CString title;
	title.Format(L"%s #%d 수정:", shape->name.IsEmpty() ? L"도형" : (LPCTSTR)shape->name, shapeIdx + 1);
	if (m_lblTitle) m_lblTitle->SetText((LPCTSTR)title);

	D2D1_RECT_F b{};
	if (m_pD2DFactory && shape->GetLocalBounds(m_pD2DFactory, b))
	{
		float scale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);
		m_spinW->SetValue((b.right - b.left) / scale);
		m_spinH->SetValue((b.bottom - b.top) / scale);
		m_spinX->SetValue(shape->xf.dx / scale);
		m_spinY->SetValue(shape->xf.dy / scale);
	}

	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CToolOptionBar::UpdateLayout(float width, float height)
{
	if (!m_pPanel) return;

	float x = 10.0f;
	float y = (height - 24.0f) * 0.5f;

	if (m_lblTitle) { m_lblTitle->SetBounds(x, y + 3.0f, x + 110.0f, y + 23.0f); x += 115.0f; }
	if (m_spinX) { m_spinX->SetBounds(x, y, x + 75.0f, y + 24.0f); x += 80.0f; }
	if (m_spinY) { m_spinY->SetBounds(x, y, x + 75.0f, y + 24.0f); x += 80.0f; }
	if (m_spinW) { m_spinW->SetBounds(x, y, x + 85.0f, y + 24.0f); x += 90.0f; }
	if (m_spinH) { m_spinH->SetBounds(x, y, x + 85.0f, y + 24.0f); x += 90.0f; }

	if (m_spinRx && m_spinRx->IsVisible()) { m_spinRx->SetBounds(x, y, x + 80.0f, y + 24.0f); x += 85.0f; }
	if (m_spinRy && m_spinRy->IsVisible()) { m_spinRy->SetBounds(x, y, x + 80.0f, y + 24.0f); x += 85.0f; }
	if (m_chkLinkRxRy && m_chkLinkRxRy->IsVisible()) { m_chkLinkRxRy->SetBounds(x, y, x + 35.0f, y + 24.0f); x += 40.0f; }
	if (m_btnResetCorners && m_btnResetCorners->IsVisible()) { m_btnResetCorners->SetBounds(x, y, x + 65.0f, y + 24.0f); x += 70.0f; }

	if (m_spinPoints && m_spinPoints->IsVisible()) { m_spinPoints->SetBounds(x, y, x + 90.0f, y + 24.0f); x += 95.0f; }
	if (m_spinRatio && m_spinRatio->IsVisible()) { m_spinRatio->SetBounds(x, y, x + 85.0f, y + 24.0f); x += 90.0f; }
	if (m_spinTurns && m_spinTurns->IsVisible()) { m_spinTurns->SetBounds(x, y, x + 85.0f, y + 24.0f); x += 90.0f; }

	m_pPanel->SetBounds(0, 0, width, height);
}

HRESULT CToolOptionBar::CreateDeviceResources()
{
	if (m_pRenderTarget) return S_OK;

	CRect rc;
	GetClientRect(&rc);
	D2D1_SIZE_U size = D2D1::SizeU(max(1, rc.Width()), max(1, rc.Height()));

	HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(
		D2D1::RenderTargetProperties(),
		D2D1::HwndRenderTargetProperties(m_hWnd, size),
		&m_pRenderTarget);

	if (SUCCEEDED(hr))
	{
		hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &m_pBrush);
	}
	return hr;
}

void CToolOptionBar::DiscardDeviceResources()
{
	if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
	if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
}

void CToolOptionBar::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	if (m_pRenderTarget)
	{
		m_pRenderTarget->Resize(D2D1::SizeU(cx, cy));
	}
	UpdateLayout(static_cast<float>(cx), static_cast<float>(cy));
	Invalidate(FALSE);
}

void CToolOptionBar::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CDockablePane::OnWindowPosChanging(lpwndpos);

	if (!(lpwndpos->flags & SWP_NOSIZE))
	{
		// 사용자가 높이를 조절할 수 있도록 하되, 최소 38px 이상 유지
		if (lpwndpos->cy < 38)
		{
			lpwndpos->cy = 38;
		}
	}
}

BOOL CToolOptionBar::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CToolOptionBar::OnPaint()
{
	CPaintDC dc(this);
	if (FAILED(CreateDeviceResources())) return;

	m_pRenderTarget->BeginDraw();
	// Inkscape 스타일 다크 테마 배경 (#28282C)
	m_pRenderTarget->Clear(D2D1::ColorF(0.16f, 0.16f, 0.18f, 1.0f));

	CRect rc;
	GetClientRect(&rc);
	float w = static_cast<float>(rc.Width());
	float h = static_cast<float>(rc.Height());

	// 상/하단 분할 경계선 (섬세한 구분선)
	m_pBrush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.13f, 1.0f));
	m_pRenderTarget->DrawLine(D2D1::Point2F(0, 0), D2D1::Point2F(w, 0), m_pBrush, 1.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.23f, 1.0f));
	m_pRenderTarget->DrawLine(D2D1::Point2F(0, h - 1.0f), D2D1::Point2F(w, h - 1.0f), m_pBrush, 1.0f);

	if (m_pPanel)
	{
		m_pPanel->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
	}

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
		Invalidate(FALSE);
	}
}

void CToolOptionBar::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	if (m_pPanel)
	{
		if (m_pPanel->OnMouseMove(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y))))
		{
			Invalidate(FALSE);
		}
	}
}

void CToolOptionBar::OnMouseLeave()
{
	if (m_pPanel)
	{
		m_pPanel->ResetState();
		Invalidate(FALSE);
	}
}

void CToolOptionBar::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	SetFocus();
	if (m_pPanel)
	{
		if (m_pPanel->OnLButtonDown(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y))))
		{
			Invalidate(FALSE);
		}
	}
}

void CToolOptionBar::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	if (m_pPanel)
	{
		if (m_pPanel->OnLButtonUp(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y))))
		{
			Invalidate(FALSE);
		}
	}
}
