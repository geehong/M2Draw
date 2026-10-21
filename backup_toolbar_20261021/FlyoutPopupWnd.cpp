#include "pch.h"
#include "FlyoutPopupWnd.h"
#include "ToolBoxPane.h"
#include "M2Draw.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CFlyoutPopupWnd, CWnd)

BEGIN_MESSAGE_MAP(CFlyoutPopupWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_KILLFOCUS()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CFlyoutPopupWnd::CFlyoutPopupWnd()
{
}

CFlyoutPopupWnd::~CFlyoutPopupWnd()
{
	DiscardDeviceResources();
	if (m_pD2DFactory)
	{
		m_pD2DFactory->Release();
		m_pD2DFactory = nullptr;
	}
}

BOOL CFlyoutPopupWnd::CreatePopup(CToolBoxPane* pParent, const std::vector<ToolType>& tools, CPoint ptScreen, int parentBtnIdx)
{
	m_pToolBoxPane = pParent;
	m_tools = tools;
	m_parentBtnIdx = parentBtnIdx;
	m_hoverIndex = -1;

	if (tools.empty()) return FALSE;

	// 크기 계산: 아이템당 38px + 여백
	const int itemSz = 38;
	const int pad = 4;
	const int totalW = static_cast<int>(tools.size()) * itemSz + pad * 2;
	const int totalH = itemSz + pad * 2;

	CString className = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_SAVEBITS,
		::LoadCursor(nullptr, IDC_ARROW),
		reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH)),
		nullptr
	);

	CRect rc(ptScreen.x, ptScreen.y, ptScreen.x + totalW, ptScreen.y + totalH);

	// 팝업 윈도우 생성 (WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST)
	BOOL bOk = CreateEx(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		className,
		_T("FlyoutPopup"),
		WS_POPUP | WS_VISIBLE,
		rc,
		pParent,
		0
	);

	if (bOk)
	{
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
		SetCapture();
		ShowWindow(SW_SHOW);
		UpdateWindow();
	}

	return bOk;
}

void CFlyoutPopupWnd::ClosePopup()
{
	if (::GetCapture() == m_hWnd)
	{
		::ReleaseCapture();
	}
	if (GetSafeHwnd())
	{
		DestroyWindow();
	}
}

void CFlyoutPopupWnd::OnDestroy()
{
	DiscardDeviceResources();
	CWnd::OnDestroy();
}

void CFlyoutPopupWnd::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	ClosePopup();
}

HRESULT CFlyoutPopupWnd::CreateDeviceResources()
{
	if (m_pRenderTarget) return S_OK;

	CRect rc;
	GetClientRect(&rc);
	D2D1_SIZE_U size = D2D1::SizeU(max(1, rc.Width()), max(1, rc.Height()));

	HRESULT hr = S_OK;
	if (!m_pD2DFactory)
	{
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	}

	if (SUCCEEDED(hr))
	{
		hr = m_pD2DFactory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(),
			D2D1::HwndRenderTargetProperties(m_hWnd, size),
			&m_pRenderTarget
		);
	}

	if (SUCCEEDED(hr))
	{
		hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pBrush);
	}

	return hr;
}

void CFlyoutPopupWnd::DiscardDeviceResources()
{
	if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
	if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
}

int CFlyoutPopupWnd::HitTest(CPoint pt)
{
	CRect rc;
	GetClientRect(&rc);
	if (!rc.PtInRect(pt)) return -1;

	const int itemSz = 38;
	const int pad = 4;
	int x = pt.x - pad;
	if (x < 0) return -1;

	int idx = x / itemSz;
	if (idx >= 0 && idx < static_cast<int>(m_tools.size()))
		return idx;

	return -1;
}

BOOL CFlyoutPopupWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CFlyoutPopupWnd::OnPaint()
{
	CPaintDC dc(this);
	if (FAILED(CreateDeviceResources()) || !m_pRenderTarget) return;

	m_pRenderTarget->BeginDraw();

	// 다크 플로팅 테마 배경 (#242428)
	m_pRenderTarget->Clear(D2D1::ColorF(0.14f, 0.14f, 0.16f, 1.0f));

	CRect rc;
	GetClientRect(&rc);
	D2D1_RECT_F fullR = D2D1::RectF(0, 0, static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));

	// 외곽 테두리 (#3A3A40)
	m_pBrush->SetColor(D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f));
	m_pRenderTarget->DrawRectangle(fullR, m_pBrush, 1.5f);

	ToolType activeTool = GetApp()->GetToolManager().GetActiveType();
	const float itemSz = 38.0f;
	const float pad = 4.0f;

	for (size_t i = 0; i < m_tools.size(); ++i)
	{
		D2D1_RECT_F btnR = D2D1::RectF(
			pad + i * itemSz,
			pad,
			pad + (i + 1) * itemSz,
			pad + itemSz
		);

		bool isHover = (static_cast<int>(i) == m_hoverIndex);
		bool isSel = (m_tools[i] == activeTool);

		if (isSel)
		{
			// 활성 선택: 블루 액센트
			m_pBrush->SetColor(D2D1::ColorF(0.10f, 0.40f, 0.85f, 0.35f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnR, 4.0f, 4.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.25f, 0.55f, 0.95f, 1.0f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(btnR, 4.0f, 4.0f), m_pBrush, 1.2f);
		}
		else if (isHover)
		{
			// 호버: 은은한 회색 하이라이트
			m_pBrush->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnR, 4.0f, 4.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.40f, 0.40f, 0.46f, 1.0f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(btnR, 4.0f, 4.0f), m_pBrush, 1.0f);
		}

		if (m_pToolBoxPane)
		{
			m_pToolBoxPane->DrawButtonIcon(m_pRenderTarget, m_pBrush, m_tools[i], btnR, isSel || isHover);
		}
	}

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
		Invalidate(FALSE);
	}
}

void CFlyoutPopupWnd::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	int hit = HitTest(point);
	if (hit != m_hoverIndex)
	{
		m_hoverIndex = hit;
		Invalidate(FALSE);
	}
}

void CFlyoutPopupWnd::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	int hit = HitTest(point);
	if (hit < 0)
	{
		ClosePopup();
	}
}

void CFlyoutPopupWnd::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	int hit = HitTest(point);
	if (hit >= 0 && hit < static_cast<int>(m_tools.size()))
	{
		ToolType chosen = m_tools[hit];
		if (m_pToolBoxPane && m_parentBtnIdx >= 0)
		{
			m_pToolBoxPane->SelectFlyoutTool(m_parentBtnIdx, chosen);
		}
	}
	ClosePopup();
}
