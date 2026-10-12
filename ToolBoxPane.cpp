#include "pch.h"
#include "ToolBoxPane.h"
#include "M2Draw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CToolBoxPane, CDockablePane)

BEGIN_MESSAGE_MAP(CToolBoxPane, CDockablePane)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()

CToolBoxPane::CToolBoxPane() noexcept
	: m_pD2DFactory(nullptr)
	, m_pRenderTarget(nullptr)
	, m_pBrush(nullptr)
	, m_hoverIndex(-1)
	, m_bTrackingMouse(false)
{
	m_buttons = {
		{ ToolType::Select,  L"선택 (V)",     L'V', CRect() },
		{ ToolType::Pan,     L"손도구 (H)",   L'H', CRect() },
		{ ToolType::Pen,     L"펜도구 (P)",   L'P', CRect() },
		{ ToolType::Line,    L"직선 (L)",     L'L', CRect() },
		{ ToolType::Rect,    L"사각형 (M)",   L'M', CRect() },
		{ ToolType::Ellipse, L"타원 (E)",     L'E', CRect() },
		{ ToolType::Polygon, L"다각형 (G)",   L'G', CRect() }
	};
}

CToolBoxPane::~CToolBoxPane()
{
	DiscardDeviceResources();
	if (m_pD2DFactory)
	{
		m_pD2DFactory->Release();
		m_pD2DFactory = nullptr;
	}
}

int CToolBoxPane::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	// ToolManager 변경 알림 등록
	GetApp()->GetToolManager().AddListener(this);
	return 0;
}

void CToolBoxPane::OnDestroy()
{
	GetApp()->GetToolManager().RemoveListener(this);
	CDockablePane::OnDestroy();
}

void CToolBoxPane::OnToolChanged(ToolType /*newTool*/)
{
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

HRESULT CToolBoxPane::CreateDeviceResources()
{
	HRESULT hr = S_OK;
	if (!m_pD2DFactory)
	{
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	}

	if (SUCCEEDED(hr) && !m_pRenderTarget)
	{
		CRect rc;
		GetClientRect(&rc);
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
			hr = m_pRenderTarget->CreateSolidColorBrush(
				D2D1::ColorF(D2D1::ColorF::Black),
				&m_pBrush
			);
		}
	}
	return hr;
}

void CToolBoxPane::DiscardDeviceResources()
{
	if (m_pBrush)
	{
		m_pBrush->Release();
		m_pBrush = nullptr;
	}
	if (m_pRenderTarget)
	{
		m_pRenderTarget->Release();
		m_pRenderTarget = nullptr;
	}
}

void CToolBoxPane::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);

	if (m_pRenderTarget && cx > 0 && cy > 0)
	{
		m_pRenderTarget->Resize(D2D1::SizeU(cx, cy));
	}

	// 1열 버튼 레이아웃 계산 (너비에 맞게 36~40px)
	const int btnSize = 36;
	const int margin = 4;
	const int startY = 8;

	int left = (cx - btnSize) / 2;
	if (left < 2) left = 2;

	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		int top = startY + static_cast<int>(i) * (btnSize + margin);
		m_buttons[i].rect = CRect(left, top, left + btnSize, top + btnSize);
	}
}

BOOL CToolBoxPane::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CToolBoxPane::OnPaint()
{
	CPaintDC dc(this);

	if (FAILED(CreateDeviceResources()))
		return;

	CRect rcClient;
	GetClientRect(&rcClient);

	m_pRenderTarget->BeginDraw();
	// 모던 툴박스 배경색 (밝은 회색)
	m_pRenderTarget->Clear(D2D1::ColorF(0.96f, 0.96f, 0.97f));

	ToolType activeTool = GetApp()->GetToolManager().GetActiveType();

	for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i)
	{
		const auto& btn = m_buttons[i];
		D2D1_RECT_F r = D2D1::RectF(
			static_cast<float>(btn.rect.left),
			static_cast<float>(btn.rect.top),
			static_cast<float>(btn.rect.right),
			static_cast<float>(btn.rect.bottom)
		);

		bool isSelected = (btn.type == activeTool);
		bool isHover = (i == m_hoverIndex);

		// 버튼 배경 그리기
		if (isSelected)
		{
			// 선택된 버튼 (활성 파란색 테두리 및 은은한 배경)
			m_pBrush->SetColor(D2D1::ColorF(0.82f, 0.88f, 0.98f, 1.0f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 4.0f, 4.0f), m_pBrush);

			m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.45f, 0.85f, 1.0f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(r, 4.0f, 4.0f), m_pBrush, 1.5f);
		}
		else if (isHover)
		{
			// 마우스 호버 배경
			m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.90f, 0.94f, 1.0f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 4.0f, 4.0f), m_pBrush);

			m_pBrush->SetColor(D2D1::ColorF(0.70f, 0.74f, 0.82f, 1.0f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(r, 4.0f, 4.0f), m_pBrush, 1.0f);
		}

		// Direct2D 순수 벡터 지오메트리 아이콘 그리기 (DPI 무관 선명도 보장)
		DrawButtonIcon(m_pRenderTarget, m_pBrush, btn.type, r, isSelected);
	}

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
		Invalidate(FALSE);
	}
}

void CToolBoxPane::DrawButtonIcon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                 ToolType type, D2D1_RECT_F btnRect, bool isSelected)
{
	// 아이콘 색상 설정
	D2D1_COLOR_F iconColor = isSelected ? D2D1::ColorF(0.12f, 0.35f, 0.80f) : D2D1::ColorF(0.25f, 0.28f, 0.35f);
	brush->SetColor(iconColor);

	float cx = (btnRect.left + btnRect.right) * 0.5f;
	float cy = (btnRect.top + btnRect.bottom) * 0.5f;

	switch (type)
	{
	case ToolType::Select: // 화살표 커서 모양
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				pSink->BeginFigure(D2D1::Point2F(cx - 6.0f, cy - 8.0f), D2D1_FIGURE_BEGIN_FILLED);
				pSink->AddLine(D2D1::Point2F(cx - 6.0f, cy + 7.0f));
				pSink->AddLine(D2D1::Point2F(cx - 2.0f, cy + 3.0f));
				pSink->AddLine(D2D1::Point2F(cx + 4.0f, cy + 8.0f));
				pSink->AddLine(D2D1::Point2F(cx + 6.0f, cy + 6.0f));
				pSink->AddLine(D2D1::Point2F(cx + 0.5f, cy + 1.0f));
				pSink->AddLine(D2D1::Point2F(cx + 5.0f, cy + 0.5f));
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();
			}
			rt->FillGeometry(pGeo, brush);
			pGeo->Release();
		}
		break;
	}
	case ToolType::Pan: // 손바닥 모양 기호
	{
		rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 6.0f, cy - 6.0f, cx + 6.0f, cy + 7.0f), 3.0f, 3.0f), brush, 1.8f);
		rt->DrawLine(D2D1::Point2F(cx - 2.0f, cy - 6.0f), D2D1::Point2F(cx - 2.0f, cy), brush, 1.5f);
		rt->DrawLine(D2D1::Point2F(cx + 2.0f, cy - 6.0f), D2D1::Point2F(cx + 2.0f, cy), brush, 1.5f);
		break;
	}
	case ToolType::Pen: // 펜촉 모양
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				pSink->BeginFigure(D2D1::Point2F(cx, cy - 8.0f), D2D1_FIGURE_BEGIN_HOLLOW);
				pSink->AddLine(D2D1::Point2F(cx + 6.0f, cy));
				pSink->AddLine(D2D1::Point2F(cx + 4.0f, cy + 7.0f));
				pSink->AddLine(D2D1::Point2F(cx - 4.0f, cy + 7.0f));
				pSink->AddLine(D2D1::Point2F(cx - 6.0f, cy));
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();
			}
			rt->DrawGeometry(pGeo, brush, 1.8f);
			rt->DrawLine(D2D1::Point2F(cx, cy - 8.0f), D2D1::Point2F(cx, cy - 1.0f), brush, 1.5f);
			rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 1.0f), 1.2f, 1.2f), brush);
			pGeo->Release();
		}
		break;
	}
	case ToolType::Line: // 대각선
	{
		rt->DrawLine(D2D1::Point2F(cx - 7.0f, cy + 7.0f), D2D1::Point2F(cx + 7.0f, cy - 7.0f), brush, 2.2f);
		break;
	}
	case ToolType::Rect: // 직사각형
	{
		rt->DrawRectangle(D2D1::RectF(cx - 7.0f, cy - 6.0f, cx + 7.0f, cy + 6.0f), brush, 1.8f);
		break;
	}
	case ToolType::Ellipse: // 타원
	{
		rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 7.5f, 6.0f), brush, 1.8f);
		break;
	}
	case ToolType::Polygon: // 6각형
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				pSink->BeginFigure(D2D1::Point2F(cx, cy - 8.0f), D2D1_FIGURE_BEGIN_HOLLOW);
				pSink->AddLine(D2D1::Point2F(cx + 7.0f, cy - 4.0f));
				pSink->AddLine(D2D1::Point2F(cx + 7.0f, cy + 4.0f));
				pSink->AddLine(D2D1::Point2F(cx, cy + 8.0f));
				pSink->AddLine(D2D1::Point2F(cx - 7.0f, cy + 4.0f));
				pSink->AddLine(D2D1::Point2F(cx - 7.0f, cy - 4.0f));
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();
			}
			rt->DrawGeometry(pGeo, brush, 1.8f);
			pGeo->Release();
		}
		break;
	}
	default:
		break;
	}
}

void CToolBoxPane::OnMouseMove(UINT nFlags, CPoint point)
{
	int hit = -1;
	for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i)
	{
		if (m_buttons[i].rect.PtInRect(point))
		{
			hit = i;
			break;
		}
	}

	if (hit != m_hoverIndex)
	{
		m_hoverIndex = hit;
		Invalidate(FALSE);
	}

	if (!m_bTrackingMouse)
	{
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
		TrackMouseEvent(&tme);
		m_bTrackingMouse = true;
	}

	CDockablePane::OnMouseMove(nFlags, point);
}

void CToolBoxPane::OnMouseLeave()
{
	m_bTrackingMouse = false;
	if (m_hoverIndex != -1)
	{
		m_hoverIndex = -1;
		Invalidate(FALSE);
	}
	CDockablePane::OnMouseLeave();
}

void CToolBoxPane::OnLButtonDown(UINT nFlags, CPoint point)
{
	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		if (m_buttons[i].rect.PtInRect(point))
		{
			// ToolManager의 활성 도구 변경
			GetApp()->GetToolManager().SetActiveTool(m_buttons[i].type);
			Invalidate(FALSE);
			return;
		}
	}

	CDockablePane::OnLButtonDown(nFlags, point);
}
