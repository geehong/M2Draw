#include "pch.h"
#include "LayerWnd.h"
#include "M2DrawDoc.h"
#include "Resource.h"
#include "M2Draw.h"
#include "MainFrm.h"
#include "ITool.h"

#define IDC_LAYER_TREE         42001
#define IDC_LAYER_BTN_NEW      42002
#define IDC_LAYER_BTN_UP       42003
#define IDC_LAYER_BTN_DOWN     42004
#define IDC_LAYER_BTN_DEL      42005

// ============================================================================
// COpacityPopupWnd: 순수 불투명도(Opacity) 슬라이더 플로팅 팝업
// ============================================================================
BEGIN_MESSAGE_MAP(COpacityPopupWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

COpacityPopupWnd::COpacityPopupWnd()
{
}

COpacityPopupWnd::~COpacityPopupWnd()
{
	ReleaseD2D();
}

void COpacityPopupWnd::InitD2D()
{
	if (!m_pD2DFactory)
	{
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	}
	if (!m_pDWriteFactory)
	{
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
	}
	if (m_pDWriteFactory && !m_pTextFormat)
	{
		m_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ko-kr", &m_pTextFormat);
	}
	if (m_pDWriteFactory && !m_pTextFormatSmall)
	{
		m_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"ko-kr", &m_pTextFormatSmall);
	}
}

void COpacityPopupWnd::ReleaseD2D()
{
	if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
	if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
	if (m_pTextFormatSmall) { m_pTextFormatSmall->Release(); m_pTextFormatSmall = nullptr; }
	if (m_pTextFormat) { m_pTextFormat->Release(); m_pTextFormat = nullptr; }
	if (m_pDWriteFactory) { m_pDWriteFactory->Release(); m_pDWriteFactory = nullptr; }
	if (m_pD2DFactory) { m_pD2DFactory->Release(); m_pD2DFactory = nullptr; }
}

void COpacityPopupWnd::ShowPopup(CPoint ptScreen, ge::Node* pTargetNode, CM2DrawDoc* pDoc, CWnd* pOwnerWnd)
{
	m_pTargetNode = pTargetNode;
	m_pDoc = pDoc;
	m_pOwnerWnd = pOwnerWnd;
	if (!m_pTargetNode) return;

	int w = 220;
	int h = 64;

	if (GetSafeHwnd() == nullptr)
	{
		CString strClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW), (HBRUSH)::GetStockObject(BLACK_BRUSH), nullptr);
		CreateEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, strClass, _T("OpacityPopup"), WS_POPUP | WS_BORDER,
			CRect(ptScreen.x, ptScreen.y, ptScreen.x + w, ptScreen.y + h), pOwnerWnd, 0);
	}

	SetWindowPos(&wndTopMost, ptScreen.x, ptScreen.y, w, h, SWP_SHOWWINDOW);

	InitD2D();
	if (m_pD2DFactory && !m_pRenderTarget)
	{
		CRect rc;
		GetClientRect(&rc);
		D2D1_SIZE_U size = D2D1::SizeU(rc.Width(), rc.Height());
		m_pD2DFactory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f),
			D2D1::HwndRenderTargetProperties(GetSafeHwnd(), size),
			&m_pRenderTarget
		);
	}

	SetCapture();
	Invalidate(FALSE);
}

void COpacityPopupWnd::HidePopup()
{
	if (GetSafeHwnd() && IsWindowVisible())
	{
		ReleaseCapture();
		ShowWindow(SW_HIDE);
		CommitActiveDocUndo(L"불투명도 변경");   // 팝업에서 값을 바꾼 경우(슬라이더 외 입력 포함) 닫힐 때 확정
		if (m_pOwnerWnd) m_pOwnerWnd->Invalidate(FALSE);
	}
}

void COpacityPopupWnd::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	HidePopup();
}

BOOL COpacityPopupWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void COpacityPopupWnd::OnPaint()
{
	CPaintDC dc(this);
	if (!m_pRenderTarget) return;

	CRect rcClient;
	GetClientRect(&rcClient);
	D2D1_SIZE_U size = D2D1::SizeU(rcClient.Width(), rcClient.Height());
	m_pRenderTarget->Resize(size);

	m_pRenderTarget->BeginDraw();
	m_pRenderTarget->Clear(D2D1::ColorF(0.16f, 0.16f, 0.18f, 1.0f));

	if (!m_pBrush)
	{
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &m_pBrush);
	}

	D2D1_RECT_F rcBox = D2D1::RectF(0.0f, 0.0f, static_cast<float>(rcClient.Width()), static_cast<float>(rcClient.Height()));
	m_pBrush->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.40f, 1.0f));
	m_pRenderTarget->DrawRectangle(rcBox, m_pBrush, 1.0f);

	if (m_pTargetNode)
	{
		float left = 12.0f;
		float right = rcBox.right - 12.0f;
		float curY = 8.0f;

		// 1. 불투명도 텍스트 헤더
		float opacityVal = m_pTargetNode->GetOpacity();
		int opacityPct = static_cast<int>(opacityVal * 100.0f + 0.5f);

		wchar_t szHeader[64];
		swprintf_s(szHeader, L"불투명도: %d%%", opacityPct);
		m_pBrush->SetColor(D2D1::ColorF(0.90f, 0.90f, 0.92f));
		m_pRenderTarget->DrawText(szHeader, static_cast<UINT32>(wcslen(szHeader)), m_pTextFormat,
			D2D1::RectF(left, curY, right, curY + 18.0f), m_pBrush);
		curY += 22.0f;

		// 2. 불투명도 슬라이더 드로우 (CD2DColorWheel::DrawSlider 활용)
		CD2DColorWheel::DrawSlider(m_pRenderTarget, m_pBrush, nullptr, nullptr,
			left, right, curY, opacityVal, D2D1::ColorF(0.18f, 0.52f, 0.95f));
	}

	m_pRenderTarget->EndDraw();
}

void COpacityPopupWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	CRect rcClient;
	GetClientRect(&rcClient);
	if (!rcClient.PtInRect(point))
	{
		HidePopup();
		return;
	}

	if (m_pTargetNode)
	{
		float left = 12.0f;
		float right = static_cast<float>(rcClient.right - 12);
		float sliderY = 30.0f;

		float valNorm = 0.0f;
		if (CD2DColorWheel::HitTestSlider(point, left, right, sliderY, valNorm))
		{
			m_bDraggingSlider = true;
			m_pTargetNode->SetOpacity(valNorm);
			if (m_pDoc) m_pDoc->UpdateAllViews(nullptr);
			Invalidate(FALSE);
			return;
		}
	}

	CWnd::OnLButtonDown(nFlags, point);
}

void COpacityPopupWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_bDraggingSlider && m_pTargetNode)
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		float left = 12.0f;
		float right = static_cast<float>(rcClient.right - 12);
		float sliderW = right - left;
		if (sliderW > 0.0f)
		{
			float val = (static_cast<float>(point.x) - left) / sliderW;
			val = (std::max)(0.0f, (std::min)(1.0f, val));
			m_pTargetNode->SetOpacity(val);
			if (m_pDoc) m_pDoc->UpdateAllViews(nullptr);
			Invalidate(FALSE);
		}
		return;
	}

	CWnd::OnMouseMove(nFlags, point);
}

void COpacityPopupWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bDraggingSlider)
	{
		m_bDraggingSlider = false;
		CommitActiveDocUndo(L"불투명도 변경");   // 슬라이더 드래그가 끝난 시점에 한 단계로 기록
		return;
	}

	CWnd::OnLButtonUp(nFlags, point);
}

// ============================================================================
// SVG 기반 정밀 벡터 아이콘 드로우 도우미 함수들
// ============================================================================

// ============================================================================
// CLayerView 구현: 자식 윈도우(CWnd) 기반 순수 Direct2D 및 M2DrawControl 렌더링
// CDockablePane 의 캡션 및 그리퍼 비클라이언트 오프셋 간섭을 완전히 제거하여 1:1 좌표 보장
// ============================================================================
#define IDC_LAYER_EDIT_NAME 42050

// 1. 투명도 (transparency.svg): 격자 체커보드 + 박스 (섬세하고 작은 크기)
void CLayerView::DrawIconTransparency(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, D2D1_COLOR_F clr)
{
	if (!pRT || !pBrush) return;

	float cx = (r.left + r.right) * 0.5f;
	float cy = (r.top + r.bottom) * 0.5f;
	float half = 5.0f; // 10x10 크기
	D2D1_RECT_F inner = D2D1::RectF(cx - half, cy - half, cx + half, cy + half);

	pBrush->SetColor(clr);
	pRT->DrawRoundedRectangle(D2D1::RoundedRect(inner, 1.2f, 1.2f), pBrush, 1.1f);

	// 좌상단 및 우하단 체커보드 채우기
	pRT->FillRectangle(D2D1::RectF(inner.left + 0.8f, inner.top + 0.8f, cx, cy), pBrush);
	pRT->FillRectangle(D2D1::RectF(cx, cy, inner.right - 0.8f, inner.bottom - 0.8f), pBrush);
}

// 2. 혼합 모드 (blending-mode.svg): 교차하는 두 원
void CLayerView::DrawIconBlendingMode(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, D2D1_COLOR_F clr)
{
	if (!pRT || !pBrush) return;

	float cx = (r.left + r.right) * 0.5f;
	float cy = (r.top + r.bottom) * 0.5f;
	float rad = 3.8f; // 아담한 반지름

	pBrush->SetColor(clr);
	pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - rad * 0.55f, cy), rad, rad), pBrush, 1.1f);

	// 우측 원 채우기
	pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + rad * 0.55f, cy), rad, rad), pBrush, 1.1f);
	D2D1_COLOR_F clrAlpha = clr;
	clrAlpha.a *= 0.55f;
	pBrush->SetColor(clrAlpha);
	pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + rad * 0.55f, cy), rad, rad), pBrush);
}

// 3. 눈(보이기/숨김) (eye.svg / eye-closed.svg) (콤팩트한 크기)
void CLayerView::DrawIconEye(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, bool visible, D2D1_COLOR_F clr)
{
	if (!pRT || !pBrush) return;

	float cx = (r.left + r.right) * 0.5f;
	float cy = (r.top + r.bottom) * 0.5f;
	float halfW = 5.5f; // 가로 11px
	float halfH = 3.0f; // 세로 6px

	pBrush->SetColor(clr);

	if (visible)
	{
		// 외곽 아몬드 형상 (eye.svg)
		D2D1_POINT_2F pLeft  = D2D1::Point2F(cx - halfW, cy);
		D2D1_POINT_2F pRight = D2D1::Point2F(cx + halfW, cy);
		D2D1_POINT_2F pTop   = D2D1::Point2F(cx, cy - halfH);
		D2D1_POINT_2F pBot   = D2D1::Point2F(cx, cy + halfH);

		pRT->DrawLine(pLeft, pTop, pBrush, 1.2f);
		pRT->DrawLine(pTop, pRight, pBrush, 1.2f);
		pRT->DrawLine(pLeft, pBot, pBrush, 1.2f);
		pRT->DrawLine(pBot, pRight, pBrush, 1.2f);

		// 동공 채우기 (반지름 1.6px)
		pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 1.6f, 1.6f), pBrush);
	}
	else
	{
		// 감은 눈 (eye-closed.svg): 아래로 볼록한 눈꺼풀 곡선 + 작은 속눈썹
		D2D1_POINT_2F pLeft  = D2D1::Point2F(cx - halfW, cy - 0.5f);
		D2D1_POINT_2F pRight = D2D1::Point2F(cx + halfW, cy - 0.5f);
		D2D1_POINT_2F pBot   = D2D1::Point2F(cx, cy + 2.0f);

		pRT->DrawLine(pLeft, pBot, pBrush, 1.2f);
		pRT->DrawLine(pBot, pRight, pBrush, 1.2f);

		// 속눈썹 3개
		pRT->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 1.0f), D2D1::Point2F(cx - 4.5f, cy + 3.8f), pBrush, 1.1f);
		pRT->DrawLine(D2D1::Point2F(cx, cy + 2.0f), D2D1::Point2F(cx, cy + 5.0f), pBrush, 1.1f);
		pRT->DrawLine(D2D1::Point2F(cx + 3.0f, cy + 1.0f), D2D1::Point2F(cx + 4.5f, cy + 3.8f), pBrush, 1.1f);
	}
}

// 4. 자물쇠(잠금/풀림) (lock-sharp.svg / unlock-sharp.svg) (콤팩트한 크기)
void CLayerView::DrawIconLock(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& r, bool locked, D2D1_COLOR_F clr)
{
	if (!pRT || !pBrush) return;

	float cx = (r.left + r.right) * 0.5f;
	float cy = (r.top + r.bottom) * 0.5f;

	pBrush->SetColor(clr);

	// 자물쇠 몸통 사각형 (7.5x5.5)
	D2D1_RECT_F body = D2D1::RectF(cx - 3.8f, cy - 0.2f, cx + 3.8f, cy + 5.5f);
	pRT->FillRoundedRectangle(D2D1::RoundedRect(body, 1.0f, 1.0f), pBrush);

	// 열쇠구멍 점
	pBrush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));
	pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 2.2f), 1.0f, 1.0f), pBrush);

	pBrush->SetColor(clr);
	if (locked)
	{
		// 닫힌 섀클
		D2D1_RECT_F shackle = D2D1::RectF(cx - 2.6f, cy - 4.8f, cx + 2.6f, cy - 0.2f);
		pRT->DrawRoundedRectangle(D2D1::RoundedRect(shackle, 2.0f, 2.0f), pBrush, 1.2f);
	}
	else
	{
		// 열린 섀클
		D2D1_RECT_F shackle = D2D1::RectF(cx - 2.6f, cy - 6.5f, cx + 2.6f, cy - 1.8f);
		pRT->DrawRoundedRectangle(D2D1::RoundedRect(shackle, 2.0f, 2.0f), pBrush, 1.2f);
		pRT->DrawLine(D2D1::Point2F(cx - 2.6f, cy - 1.8f), D2D1::Point2F(cx - 2.6f, cy - 0.2f), pBrush, 1.2f);
	}
}

BEGIN_MESSAGE_MAP(CLayerView, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEWHEEL()
	ON_WM_KILLFOCUS()
	ON_WM_KEYDOWN()
	ON_EN_KILLFOCUS(IDC_LAYER_EDIT_NAME, &CLayerView::OnEditKillFocus)
END_MESSAGE_MAP()

CLayerView::CLayerView()
{
}

CLayerView::~CLayerView()
{
	DiscardDeviceResources();
}

CM2DrawDoc* CLayerView::GetDocSafe()
{
	if (m_pDoc) return m_pDoc;
	CFrameWnd* pMainFrame = dynamic_cast<CFrameWnd*>(AfxGetMainWnd());
	if (pMainFrame)
	{
		CView* pView = pMainFrame->GetActiveView();
		if (pView)
		{
			m_pDoc = dynamic_cast<CM2DrawDoc*>(pView->GetDocument());
		}
	}
	return m_pDoc;
}

void CLayerView::SetDocument(CM2DrawDoc* pDoc)
{
	m_pDoc = pDoc;
	RefreshLayers();
}
HRESULT CLayerView::CreateDeviceResources()
{
	HRESULT hr = S_OK;

	if (!m_pD2DFactory)
	{
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
		if (FAILED(hr)) return hr;
	}

	if (!m_pDWriteFactory)
	{
		hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
		);
		if (FAILED(hr)) return hr;
	}

	if (!m_pTextFormat && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			12.0f,
			L"ko-kr",
			&m_pTextFormat
		);
		if (m_pTextFormat)
		{
			m_pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}

	if (!m_pTextFormatBold && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_SEMI_BOLD,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			12.0f,
			L"ko-kr",
			&m_pTextFormatBold
		);
		if (m_pTextFormatBold)
		{
			m_pTextFormatBold->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			m_pTextFormatBold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}

	if (!m_pTextFormatSmall && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			11.0f,
			L"ko-kr",
			&m_pTextFormatSmall
		);
		if (m_pTextFormatSmall)
		{
			m_pTextFormatSmall->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			m_pTextFormatSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}

	if (!m_pRenderTarget && m_pD2DFactory && GetSafeHwnd())
	{
		CRect rc;
		GetClientRect(&rc);
		D2D1_SIZE_U size = D2D1::SizeU(max(1, rc.Width()), max(1, rc.Height()));

		hr = m_pD2DFactory->CreateHwndRenderTarget(
			D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
				96.0f, 96.0f // 마우스 좌표(픽셀)와 D2D 좌표(DIP)를 1:1로 일치시킴 (High-DPI 히트테스트 어긋남 방지)
			),
			D2D1::HwndRenderTargetProperties(GetSafeHwnd(), size),
			&m_pRenderTarget
		);

		if (SUCCEEDED(hr) && m_pRenderTarget)
		{
			hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_pBrush);
		}
	}

	return hr;
}

void CLayerView::DiscardDeviceResources()
{
	if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
	if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
	if (m_pTextFormatSmall) { m_pTextFormatSmall->Release(); m_pTextFormatSmall = nullptr; }
	if (m_pTextFormatBold) { m_pTextFormatBold->Release(); m_pTextFormatBold = nullptr; }
	if (m_pTextFormat) { m_pTextFormat->Release(); m_pTextFormat = nullptr; }
	if (m_pDWriteFactory) { m_pDWriteFactory->Release(); m_pDWriteFactory = nullptr; }
	if (m_pD2DFactory) { m_pD2DFactory->Release(); m_pD2DFactory = nullptr; }
}

int CLayerView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	// 1. 상단 툴바 D2D 커스텀 버튼 (M2DrawButton)
	m_btnNewLayer = std::make_shared<M2DrawButton>(L"+ Layer");
	m_btnNewLayer->SetOnClick([this](M2DrawButton*) {
		CM2DrawDoc* pDoc = GetDocSafe();
		if (!pDoc) return;

		auto& doc = pDoc->GetDoc();
		int nextIdx = static_cast<int>(doc.GetLayers().size()) + 1;
		std::wstring newName = L"Layer " + std::to_wstring(nextIdx);
		doc.AddLayer(newName);

		CString strLog;
		strLog.Format(_T("[레이어 패널] 새 레이어 생성: '%s' (총 %d개 레이어)"),
			newName.c_str(), static_cast<int>(pDoc->GetDoc().GetLayers().size()));
		LogAppEvent(strLog);
		SetAppStatusText(strLog);

		pDoc->UpdateAllViews(nullptr);
		RefreshLayers();
		SafeInvalidate(FALSE);
	});

	m_btnMoveUp = std::make_shared<M2DrawButton>(L"▲");
	m_btnMoveUp->SetOnClick([this](M2DrawButton*) {
		CM2DrawDoc* pDoc = GetDocSafe();
		if (!pDoc) return;
		auto& doc = pDoc->GetDoc();
		int active = doc.GetActiveLayerIndex();
		if (active < static_cast<int>(doc.GetLayers().size()) - 1)
		{
			doc.MoveLayer(doc.GetActiveLayer(), active + 1);
			LogAppEvent(_T("[레이어 패널] 레이어 위로 이동"));
			pDoc->UpdateAllViews(nullptr);
			RefreshLayers();
			SafeInvalidate(FALSE);
		}
	});

	m_btnMoveDown = std::make_shared<M2DrawButton>(L"▼");
	m_btnMoveDown->SetOnClick([this](M2DrawButton*) {
		CM2DrawDoc* pDoc = GetDocSafe();
		if (!pDoc) return;
		auto& doc = pDoc->GetDoc();
		int active = doc.GetActiveLayerIndex();
		if (active > 0)
		{
			doc.MoveLayer(doc.GetActiveLayer(), active - 1);
			LogAppEvent(_T("[레이어 패널] 레이어 아래로 이동"));
			pDoc->UpdateAllViews(nullptr);
			RefreshLayers();
			SafeInvalidate(FALSE);
		}
	});

	m_btnDelete = std::make_shared<M2DrawButton>(L"🗑");
	m_btnDelete->SetOnClick([this](M2DrawButton*) {
		CM2DrawDoc* pDoc = GetDocSafe();
		if (!pDoc) return;
		pDoc->PerformSmartDelete();
		RefreshLayers();
		SafeInvalidate(FALSE);
	});

	// 2. 인라인 이름 편집 에디트 컨트롤 생성
	CRect rectDummy(0, 0, 0, 0);
	m_wndEditName.Create(WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, rectDummy, this, IDC_LAYER_EDIT_NAME);
	CFont font;
	font.CreatePointFont(90, _T("Segoe UI"));
	m_wndEditName.SetFont(&font);

	RefreshLayers();
	return 0;
}

void CLayerView::OnDestroy()
{
	m_wndOpacityPopup.HidePopup();
	DiscardDeviceResources();
	CWnd::OnDestroy();
}

void CLayerView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);

	if (m_pRenderTarget && cx > 0 && cy > 0)
	{
		m_pRenderTarget->Resize(D2D1::SizeU(cx, cy));
	}

	UpdateLayout(static_cast<float>(cx), static_cast<float>(cy));
	SafeInvalidate(FALSE);
}

void CLayerView::UpdateLayout(float width, float height)
{
	const float pad = 6.0f;
	const float topH = 26.0f;

	// 상단 툴바 버튼 배치
	float btnX = pad;
	if (m_btnNewLayer)
	{
		m_btnNewLayer->SetBounds(D2D1::RectF(btnX, pad, btnX + 70.0f, pad + topH));
		btnX += 74.0f;
	}
	if (m_btnMoveUp)
	{
		m_btnMoveUp->SetBounds(D2D1::RectF(btnX, pad, btnX + 26.0f, pad + topH));
		btnX += 30.0f;
	}
	if (m_btnMoveDown)
	{
		m_btnMoveDown->SetBounds(D2D1::RectF(btnX, pad, btnX + 26.0f, pad + topH));
		btnX += 30.0f;
	}
	if (m_btnDelete)
	{
		m_btnDelete->SetBounds(D2D1::RectF(btnX, pad, btnX + 26.0f, pad + topH));
		btnX += 30.0f;
	}
	// 리스트 아이템 행 위치 재계산
	float curY = pad + topH + 8.0f - m_scrollY;
	const float rowH = 28.0f;

	for (auto& row : m_rowItems)
	{
		row.rowRect = D2D1::RectF(0.0f, curY, width, curY + rowH);

		float indent = 6.0f + 14.0f * row.depth;

		// 1. 접기/펼치기 화살표 (자식이 있는 레이어/그룹). 없는 행도 아이콘 정렬을 위해 자리는 확보
		if (row.hasChildren)
		{
			row.expandRect = D2D1::RectF(indent, curY + 6.0f, indent + 12.0f, curY + rowH - 6.0f);
		}
		else
		{
			row.expandRect = D2D1::RectF(0, 0, 0, 0);
		}
		indent += 15.0f;

		// 3. 아이콘 영역 (14x14 크기)
		row.iconRect = D2D1::RectF(indent, curY + 7.0f, indent + 14.0f, curY + 21.0f);
		indent += 18.0f;

		// 4. 이름 라벨 영역 (좌측 정렬로 텍스트 너비 맞춤)
		float labelW = 65.0f;
		row.labelRect = D2D1::RectF(indent, curY, indent + labelW, curY + rowH);
		indent += labelW + 6.0f;

		// 5. 버튼/체크박스 모음 (Layer 이름 바로 옆에 나란히 좌측 정렬 배치!)
		// 순서: [보기 👁] [투명도] [혼합모드 (레이어만)] [잠금 🔒]
		const float btnW = 18.0f;
		const float btnGap = 4.0f;

		if (row.chkVisible)
		{
			row.chkVisible->SetBounds(D2D1::RectF(indent, curY + 5.0f, indent + btnW, curY + rowH - 5.0f));
			indent += btnW + btnGap;
		}
		if (row.btnOpacity)
		{
			row.btnOpacity->SetBounds(D2D1::RectF(indent, curY + 5.0f, indent + btnW, curY + rowH - 5.0f));
			indent += btnW + btnGap;
		}
		if (row.btnBlendMode)
		{
			row.btnBlendMode->SetBounds(D2D1::RectF(indent, curY + 5.0f, indent + btnW, curY + rowH - 5.0f));
			indent += btnW + btnGap;
		}
		if (row.chkLocked)
		{
			row.chkLocked->SetBounds(D2D1::RectF(indent, curY + 5.0f, indent + btnW, curY + rowH - 5.0f));
			indent += btnW + btnGap;
		}

		curY += rowH;
	}

	m_totalContentHeight = curY + m_scrollY;
}

// 가시성/잠금 변경 후 숨김·잠김(조상 포함) 노드를 선택에서 제외.
// 레이어 패널은 재구성하지 않음 (체크박스 콜백 실행 중 RefreshLayers 로 행이 파괴되는 것을 방지)
static void PruneSelectionAfterToggle(CM2DrawDoc* pDoc)
{
	if (pDoc && pDoc->PruneNonInteractiveSelection())
	{
		ge::Node* pPrimary = pDoc->GetPrimarySelection();
		SyncPropertiesWithSelection(pPrimary, pDoc);
		SyncOptionBarWithSelection(pPrimary, pDoc);
	}
}

// 노드 하나(와 펼쳐진 후손)의 행을 재귀적으로 추가: 레이어 -> 그룹 -> 도형 임의 깊이
void CLayerView::AppendNodeRows(ge::Node* node, int depth)
{
	if (!node) return;

	auto* group = dynamic_cast<ge::GroupNode*>(node);   // 레이어도 GroupNode
	const bool isLayer = (node->Kind() == ge::NodeKind::Layer);

	LayerRowItem row;
	row.pNode = node;
	row.isLayer = isLayer;
	row.depth = depth;
	row.hasChildren = group && !group->GetChildren().empty();
	row.isExpanded = (m_collapsedIds.find(node->GetId()) == m_collapsedIds.end());

	if (isLayer)
	{
		// M2DrawControl: [3. 혼합 모드 아이콘 버튼] (blending-mode.svg 드롭다운 메뉴 트리거)
		row.btnBlendMode = std::make_shared<M2DrawButton>(L"");
		row.btnBlendMode->SetOnClick([this, node](M2DrawButton* pBtn) {
			D2D1_RECT_F b = pBtn->GetBounds();
			CPoint pt(static_cast<int>(b.left), static_cast<int>(b.bottom));
			ClientToScreen(&pt);

			CMenu menu;
			if (menu.CreatePopupMenu())
			{
				for (int i = 0; i < ge::kBlendModeCount; ++i)
				{
					UINT flags = MF_STRING;
					if (node->GetBlendMode() == static_cast<ge::LayerBlendMode>(i))
					{
						flags |= MF_CHECKED;
					}
					menu.AppendMenu(flags, 50000 + i, ge::BlendModeName(static_cast<ge::LayerBlendMode>(i)));
				}

				int cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, this);
				if (cmd >= 50000 && cmd < 50000 + ge::kBlendModeCount)
				{
					int selIdx = cmd - 50000;
					node->SetBlendMode(static_cast<ge::LayerBlendMode>(selIdx));
					CM2DrawDoc* pActiveDoc = GetDocSafe();
					if (pActiveDoc) pActiveDoc->UpdateAllViews(nullptr);
					CString strLog;
					strLog.Format(_T("[레이어 패널] 레이어 '%s' 혼합 모드 변경: '%s'"),
						node->GetName().c_str(), ge::BlendModeName(static_cast<ge::LayerBlendMode>(selIdx)));
					LogAppEvent(strLog);
					SafeInvalidate(FALSE);
				}
			}
		});
	}

	// M2DrawControl: [투명도] 버튼 생성
	row.btnOpacity = std::make_shared<M2DrawButton>(L"");
	row.btnOpacity->SetOnClick([this, node](M2DrawButton* pBtn) {
		CM2DrawDoc* pActiveDoc = GetDocSafe();
		if (!pActiveDoc || !node) return;

		D2D1_RECT_F b = pBtn->GetBounds();
		CPoint pt(static_cast<int>(b.left), static_cast<int>(b.bottom));
		ClientToScreen(&pt);
		m_wndOpacityPopup.ShowPopup(pt, node, pActiveDoc, this);
	});

	// M2DrawControl: [👁] 보기 체크박스 생성
	row.chkVisible = std::make_shared<M2DrawCheckBox>(L"", node->IsVisible());
	row.chkVisible->SetOnChanged([this, node](M2DrawCheckBox*, bool bChecked) {
		node->SetVisible(bChecked);
		CM2DrawDoc* pActiveDoc = GetDocSafe();
		if (pActiveDoc)
		{
			PruneSelectionAfterToggle(pActiveDoc);
			pActiveDoc->UpdateAllViews(nullptr);
		}
		CString strLog;
		strLog.Format(_T("[레이어 패널] [보기 👁] 토글: '%s' (가시성: %s)"),
			node->GetName().c_str(), bChecked ? _T("표시") : _T("숨김"));
		LogAppEvent(strLog);
		SafeInvalidate(FALSE);
	});

	// M2DrawControl: [🔒] 잠금 체크박스 생성
	row.chkLocked = std::make_shared<M2DrawCheckBox>(L"", node->IsLocked());
	row.chkLocked->SetOnChanged([this, node](M2DrawCheckBox*, bool bChecked) {
		node->SetLocked(bChecked);
		CM2DrawDoc* pActiveDoc = GetDocSafe();
		if (pActiveDoc) { PruneSelectionAfterToggle(pActiveDoc); pActiveDoc->UpdateAllViews(nullptr); }
		CString strLog;
		strLog.Format(_T("[레이어 패널] [잠금 🔒] 토글: '%s' (상태: %s)"),
			node->GetName().c_str(), bChecked ? _T("잠김") : _T("해제"));
		LogAppEvent(strLog);
		SafeInvalidate(FALSE);
	});

	const bool expanded = row.isExpanded;
	m_rowItems.push_back(std::move(row));

	// 자식은 위(z-order 최상단) -> 아래 순서로, 펼쳐진 경우에만
	if (group && expanded)
	{
		const auto& children = group->GetChildren();
		for (int ci = static_cast<int>(children.size()) - 1; ci >= 0; --ci)
			AppendNodeRows(children[ci].get(), depth + 1);
	}
}

void CLayerView::RefreshLayers()
{
	CM2DrawDoc* pDoc = GetDocSafe();
	m_rowItems.clear();
	if (!pDoc)
	{
		SafeInvalidate(FALSE);
		return;
	}

	const auto& layers = pDoc->GetDoc().GetLayers();
	for (int li = static_cast<int>(layers.size()) - 1; li >= 0; --li)
		AppendNodeRows(layers[li].get(), 0);

	CRect rc;
	GetClientRect(&rc);
	UpdateLayout(static_cast<float>(rc.Width()), static_cast<float>(rc.Height()));
	SafeInvalidate(FALSE);
}

void CLayerView::SelectNode(ge::Node* pNode)
{
	if (!pNode) return;
	SafeInvalidate(FALSE);
}

void CLayerView::DrawNodeIcon(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F& iconRect, ge::Node* pNode)
{
	if (!pRT || !pBrush || !pNode) return;

	if (pNode->Kind() == ge::NodeKind::Layer)
	{
		// 폴더/레이어 다층 아이콘
		pBrush->SetColor(D2D1::ColorF(0.85f, 0.70f, 0.25f, 1.0f));
		D2D1_RECT_F r1 = D2D1::RectF(iconRect.left + 0.5f, iconRect.top + 2.0f, iconRect.right - 0.5f, iconRect.top + 5.5f);
		D2D1_RECT_F r2 = D2D1::RectF(iconRect.left + 0.5f, iconRect.top + 6.5f, iconRect.right - 0.5f, iconRect.bottom - 1.5f);
		pRT->DrawRoundedRectangle(D2D1::RoundedRect(r1, 1.0f, 1.0f), pBrush, 1.1f);
		pRT->DrawRoundedRectangle(D2D1::RoundedRect(r2, 1.5f, 1.5f), pBrush, 1.1f);
		return;
	}

	if (pNode->Kind() == ge::NodeKind::Group)
	{
		// 그룹 아이콘: 겹친 두 사각형
		pBrush->SetColor(D2D1::ColorF(0.55f, 0.80f, 0.55f, 1.0f));
		D2D1_RECT_F g1 = D2D1::RectF(iconRect.left + 0.5f, iconRect.top + 0.5f, iconRect.right - 4.5f, iconRect.bottom - 4.5f);
		D2D1_RECT_F g2 = D2D1::RectF(iconRect.left + 4.5f, iconRect.top + 4.5f, iconRect.right - 0.5f, iconRect.bottom - 0.5f);
		pRT->DrawRectangle(g1, pBrush, 1.1f);
		pRT->DrawRectangle(g2, pBrush, 1.1f);
		return;
	}

	ge::PathNode* pPath = dynamic_cast<ge::PathNode*>(pNode);
	ge::ShapeKind kind = pPath ? pPath->GetShapeKind() : ge::ShapeKind::Path;

	pBrush->SetColor(D2D1::ColorF(0.25f, 0.65f, 0.95f, 1.0f));
	switch (kind)
	{
	case ge::ShapeKind::Rect:
		pRT->DrawRectangle(D2D1::RectF(iconRect.left + 1.5f, iconRect.top + 1.5f, iconRect.right - 1.5f, iconRect.bottom - 1.5f), pBrush, 1.1f);
		break;
	case ge::ShapeKind::Ellipse:
	{
		float cx = (iconRect.left + iconRect.right) * 0.5f;
		float cy = (iconRect.top + iconRect.bottom) * 0.5f;
		pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.5f, 4.5f), pBrush, 1.1f);
		break;
	}
	case ge::ShapeKind::Star:
	case ge::ShapeKind::Polygon:
	default:
	{
		// 패스/선 아이콘
		pRT->DrawLine(D2D1::Point2F(iconRect.left + 1.5f, iconRect.bottom - 1.5f),
			D2D1::Point2F(iconRect.right - 1.5f, iconRect.top + 1.5f), pBrush, 1.2f);
		pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(iconRect.left + 1.5f, iconRect.bottom - 1.5f), 1.5f, 1.5f), pBrush, 0.9f);
		pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(iconRect.right - 1.5f, iconRect.top + 1.5f), 1.5f, 1.5f), pBrush, 0.9f);
		break;
	}
	}
}

BOOL CLayerView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CLayerView::OnPaint()
{
	CPaintDC dc(this);

	if (FAILED(CreateDeviceResources()) || !m_pRenderTarget)
		return;

	m_pRenderTarget->BeginDraw();

	// 다크 테마 패널 배경 (#1e1e22)
	m_pRenderTarget->Clear(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));

	CRect rc;
	GetClientRect(&rc);
	float width = static_cast<float>(rc.Width());
	float height = static_cast<float>(rc.Height());

	// 도킹 창 크기 변경 시 RenderTarget 크기 동기화 보장
	if (m_pRenderTarget && rc.Width() > 0 && rc.Height() > 0)
	{
		D2D1_SIZE_U rtSize = m_pRenderTarget->GetPixelSize();
		if (rtSize.width != static_cast<UINT32>(rc.Width()) || rtSize.height != static_cast<UINT32>(rc.Height()))
		{
			m_pRenderTarget->Resize(D2D1::SizeU(rc.Width(), rc.Height()));
		}
	}

	UpdateLayout(width, height);

	// 1. 상단 툴바 버튼 그리기
	if (m_btnNewLayer) m_btnNewLayer->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
	if (m_btnMoveUp)   m_btnMoveUp->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
	if (m_btnMoveDown) m_btnMoveDown->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
	if (m_btnDelete)   m_btnDelete->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);

	// 툴바 구분선
	const float pad = 6.0f;
	const float topH = 26.0f;
	float lineY = pad + topH + 3.0f;
	m_pBrush->SetColor(D2D1::ColorF(0.22f, 0.22f, 0.26f, 1.0f));
	m_pRenderTarget->DrawLine(D2D1::Point2F(pad, lineY), D2D1::Point2F(width - pad, lineY), m_pBrush, 1.0f);

	// 2. 리스트 행 영역 클리핑 및 렌더링
	m_pRenderTarget->PushAxisAlignedClip(D2D1::RectF(0.0f, lineY + 1.0f, width, height), D2D1_ANTIALIAS_MODE_ALIASED);

	CM2DrawDoc* pDoc = GetDocSafe();
	ge::Node* pActiveLayer = pDoc ? pDoc->GetDoc().GetActiveLayer() : nullptr;
	const auto& selNodes = pDoc ? pDoc->GetDoc().GetSelection() : std::vector<ge::Node*>();

	std::shared_ptr<M2DrawComboBox> activeDroppedCombo;

	for (auto& row : m_rowItems)
	{
		if (row.rowRect.bottom < lineY || row.rowRect.top > height)
			continue;

		bool isSelected = false;
		if (row.isLayer && row.pNode == pActiveLayer && selNodes.empty())
		{
			isSelected = true;
		}
		else
		{
			for (ge::Node* selNode : selNodes)
			{
				if (selNode == row.pNode)
				{
					isSelected = true;
					break;
				}
			}
		}

		// 행 배경
		if (isSelected)
		{
			m_pBrush->SetColor(D2D1::ColorF(0.16f, 0.33f, 0.60f, 0.85f));
			m_pRenderTarget->FillRectangle(row.rowRect, m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.45f, 0.85f, 1.0f));
			m_pRenderTarget->DrawRectangle(row.rowRect, m_pBrush, 1.0f);
		}

		// 접기/펼치기 화살표 (자식이 있는 레이어/그룹): 펼침 ▽ / 접힘 ▷
		if (row.hasChildren && row.expandRect.right > row.expandRect.left)
		{
			float cx = (row.expandRect.left + row.expandRect.right) * 0.5f;
			float cy = (row.expandRect.top + row.expandRect.bottom) * 0.5f;
			m_pBrush->SetColor(D2D1::ColorF(0.70f, 0.70f, 0.75f, 1.0f));
			D2D1_POINT_2F pA, pB, pC;
			if (row.isExpanded)
			{
				pA = D2D1::Point2F(cx - 3.5f, cy - 2.0f);
				pB = D2D1::Point2F(cx + 3.5f, cy - 2.0f);
				pC = D2D1::Point2F(cx, cy + 2.5f);
			}
			else
			{
				pA = D2D1::Point2F(cx - 2.0f, cy - 3.5f);
				pB = D2D1::Point2F(cx - 2.0f, cy + 3.5f);
				pC = D2D1::Point2F(cx + 2.5f, cy);
			}
			m_pRenderTarget->DrawLine(pA, pB, m_pBrush, 1.2f);
			m_pRenderTarget->DrawLine(pB, pC, m_pBrush, 1.2f);
			m_pRenderTarget->DrawLine(pC, pA, m_pBrush, 1.2f);
		}

		// 아이콘
		DrawNodeIcon(m_pRenderTarget, m_pBrush, row.iconRect, row.pNode);

		// 라벨 (이름 텍스트) - 우측 컨트롤 영역 침범 방지 클리핑
		if (row.pNode && row.labelRect.right > row.labelRect.left)
		{
			std::wstring name = row.pNode->GetName();
			m_pBrush->SetColor(isSelected ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.88f, 0.88f, 0.92f, 1.0f));
			m_pRenderTarget->PushAxisAlignedClip(row.labelRect, D2D1_ANTIALIAS_MODE_ALIASED);
			m_pRenderTarget->DrawText(
				name.c_str(), static_cast<UINT32>(name.length()),
				row.isLayer ? m_pTextFormatBold : m_pTextFormat,
				row.labelRect, m_pBrush
			);
			m_pRenderTarget->PopAxisAlignedClip();
		}

		// 우측 M2DrawControl 위젯들 렌더링
		// 순서: [보기 👁] [투명도] [혼합 모드 아이콘 (레이어 전용)] [잠금 🔒]
		// 1) 보기 아이콘 체크박스 (eye.svg / eye-closed.svg)
		if (row.chkVisible)
		{
			row.chkVisible->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
			bool bVis = row.pNode ? row.pNode->IsVisible() : true;
			DrawIconEye(m_pRenderTarget, m_pBrush, row.chkVisible->GetBounds(), bVis,
				bVis ? D2D1::ColorF(0.90f, 0.90f, 0.95f, 1.0f) : D2D1::ColorF(0.50f, 0.50f, 0.55f, 0.8f));
		}

		// 2) 투명도 아이콘 버튼 (transparency.svg)
		if (row.btnOpacity)
		{
			row.btnOpacity->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
			DrawIconTransparency(m_pRenderTarget, m_pBrush, row.btnOpacity->GetBounds(), D2D1::ColorF(0.85f, 0.85f, 0.90f, 1.0f));
		}

		// 3) 혼합 모드 아이콘 버튼 (blending-mode.svg - 레이어 전용)
		if (row.btnBlendMode)
		{
			row.btnBlendMode->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
			DrawIconBlendingMode(m_pRenderTarget, m_pBrush, row.btnBlendMode->GetBounds(), D2D1::ColorF(0.85f, 0.85f, 0.95f, 1.0f));
		}

		// 4) 잠금 아이콘 체크박스 (lock-sharp.svg / unlock-sharp.svg)
		if (row.chkLocked)
		{
			row.chkLocked->Draw(m_pRenderTarget, m_pBrush, m_pDWriteFactory, m_pTextFormatSmall);
			bool bLock = row.pNode ? row.pNode->IsLocked() : false;
			DrawIconLock(m_pRenderTarget, m_pBrush, row.chkLocked->GetBounds(), bLock,
				bLock ? D2D1::ColorF(0.95f, 0.65f, 0.20f, 1.0f) : D2D1::ColorF(0.50f, 0.50f, 0.55f, 0.8f));
		}

		// 행 하단 연한 가름선
		m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.5f));
		m_pRenderTarget->DrawLine(
			D2D1::Point2F(row.rowRect.left, row.rowRect.bottom),
			D2D1::Point2F(row.rowRect.right, row.rowRect.bottom),
			m_pBrush, 1.0f
		);
	}

	m_pRenderTarget->PopAxisAlignedClip();

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
	}
}

void CLayerView::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	CommitEditName();
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	// 1. 상단 툴바 버튼 클릭
	if (m_btnNewLayer && m_btnNewLayer->HitTest(ptD2D))
	{
		if (m_btnNewLayer->OnLButtonDown(ptD2D))
		{
			m_capturedControl = m_btnNewLayer;
			SetCapture();
			SafeInvalidate(FALSE);
			return;
		}
	}
	if (m_btnMoveUp && m_btnMoveUp->HitTest(ptD2D))
	{
		if (m_btnMoveUp->OnLButtonDown(ptD2D))
		{
			m_capturedControl = m_btnMoveUp;
			SetCapture();
			SafeInvalidate(FALSE);
			return;
		}
	}
	if (m_btnMoveDown && m_btnMoveDown->HitTest(ptD2D))
	{
		if (m_btnMoveDown->OnLButtonDown(ptD2D))
		{
			m_capturedControl = m_btnMoveDown;
			SetCapture();
			SafeInvalidate(FALSE);
			return;
		}
	}
	if (m_btnDelete && m_btnDelete->HitTest(ptD2D))
	{
		if (m_btnDelete->OnLButtonDown(ptD2D))
		{
			m_capturedControl = m_btnDelete;
			SetCapture();
			SafeInvalidate(FALSE);
			return;
		}
	}

	// 2. 리스트 행 아이템 히트 테스트
	CM2DrawDoc* pDoc = GetDocSafe();
	for (auto& row : m_rowItems)
	{
		if (ptD2D.y >= row.rowRect.top && ptD2D.y <= row.rowRect.bottom)
		{
			// 0. 접기/펼치기 화살표 클릭 -> 상태 토글 후 행 재구성 (row 무효화되므로 즉시 반환)
			if (row.hasChildren && row.pNode &&
				ptD2D.x >= row.expandRect.left - 2.0f && ptD2D.x <= row.expandRect.right + 2.0f)
			{
				const ge::NodeId id = row.pNode->GetId();
				if (!m_collapsedIds.erase(id)) m_collapsedIds.insert(id);
				RefreshLayers();
				return;
			}

			// A. [보기 👁] 체크박스 클릭
			if (row.chkVisible && row.chkVisible->HitTest(ptD2D))
			{
				if (row.chkVisible->OnLButtonDown(ptD2D))
				{
					SafeInvalidate(FALSE);
					return;
				}
			}

			// B. [투명도] 버튼 클릭
			if (row.btnOpacity && row.btnOpacity->HitTest(ptD2D))
			{
				if (row.btnOpacity->OnLButtonDown(ptD2D))
				{
					m_capturedControl = row.btnOpacity;
					SetCapture();
					SafeInvalidate(FALSE);
					return;
				}
			}

			// C. [혼합 모드] 버튼 클릭 (레이어 전용)
			if (row.btnBlendMode && row.btnBlendMode->HitTest(ptD2D))
			{
				if (row.btnBlendMode->OnLButtonDown(ptD2D))
				{
					m_capturedControl = row.btnBlendMode;
					SetCapture();
					SafeInvalidate(FALSE);
					return;
				}
			}

			// D. [잠금 🔒] 체크박스 클릭
			if (row.chkLocked && row.chkLocked->HitTest(ptD2D))
			{
				if (row.chkLocked->OnLButtonDown(ptD2D))
				{
					SafeInvalidate(FALSE);
					return;
				}
			}

			// 아래 선택 갱신이 RefreshLayers() 로 m_rowItems 를 재구성하므로(row 무효화) 필요한 값을 먼저 복사
			ge::Node* const pRowNode = row.pNode;
			const bool bRowIsLayer = row.isLayer;
			const D2D1_RECT_F labelRect = row.labelRect;
			const D2D1_RECT_F rowRect = row.rowRect;
			const bool bInNameArea = IsInNameArea(row, ptD2D);

			// E. 라벨 또는 행 클릭 -> 캔버스 및 속성창 선택 동기화!
			if (pDoc && pRowNode)
			{
				if (bRowIsLayer)
				{
					auto& doc = pDoc->GetDoc();
					for (size_t i = 0; i < doc.GetLayers().size(); ++i)
					{
						if (doc.GetLayers()[i].get() == pRowNode)
						{
							doc.SetActiveLayer(static_cast<int>(i));
							doc.ClearSelection();
							UpdateShapePropertiesSelection(nullptr, pDoc);
							break;
						}
					}
					CString strLog;
					strLog.Format(_T("[레이어 패널] 활성 레이어 전환: '%s'"), pRowNode->GetName().c_str());
					LogAppEvent(strLog);
				}
				else
				{
					// 노드 선택 + 소속 레이어 활성화 + 속성창/옵션바 반영
					SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
					if (pSelect)
					{
						pSelect->SelectNode(pRowNode, pDoc);
					}

					CString strLog;
					strLog.Format(_T("[레이어 패널] 오브젝트 선택됨: '%s'"), pRowNode->GetName().c_str());
					LogAppEvent(strLog);
				}

				pDoc->UpdateAllViews(nullptr);
				SafeInvalidate(FALSE);
			}

			// 천천히 더블클릭/재클릭 감지 -> 이름 변경 인라인 에디트
			DWORD dwNow = ::GetTickCount();
			if (pRowNode && m_pLastClickedNode == pRowNode && (dwNow - m_dwLastClickTime) > 500 && (dwNow - m_dwLastClickTime) < 1800)
			{
				if (bInNameArea)
				{
					m_pLastClickedNode = nullptr;
					BeginEditName(pRowNode, labelRect, rowRect);
					return;
				}
			}

			m_pLastClickedNode = pRowNode;
			m_dwLastClickTime = dwNow;
			return;
		}
	}
}

bool CLayerView::IsInNameArea(const LayerRowItem& row, D2D1_POINT_2F pt)
{
	if (pt.y < row.rowRect.top || pt.y > row.rowRect.bottom) return false;
	// 오른쪽 경계: 첫 번째 버튼(보기 체크박스) 시작점, 없으면 라벨 끝 + 여유
	float right = row.labelRect.right + 6.0f;
	if (row.chkVisible) right = row.chkVisible->GetBounds().left;
	return pt.x >= row.iconRect.left && pt.x < right;
}

void CLayerView::BeginEditName(ge::Node* pNode, const D2D1_RECT_F& labelRect, const D2D1_RECT_F& rowRect)
{
	if (!pNode || !m_wndEditName.GetSafeHwnd()) return;
	m_pEditingNode = pNode;
	// 라벨 폭(65px)보다 약간 넓게(110px), 단 행 오른쪽 끝은 넘지 않음
	const float kEditWidth = 110.0f;
	const float right = (std::min)(labelRect.left + kEditWidth, rowRect.right - 6.0f);
	CRect rcEdit(static_cast<int>(labelRect.left), static_cast<int>(labelRect.top + 2),
		static_cast<int>(right), static_cast<int>(labelRect.bottom - 2));
	m_wndEditName.MoveWindow(&rcEdit);
	m_wndEditName.SetWindowText(pNode->GetName().c_str());
	m_wndEditName.ShowWindow(SW_SHOW);
	m_wndEditName.SetFocus();
	m_wndEditName.SetSel(0, -1);
}

void CLayerView::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	for (const auto& row : m_rowItems)
	{
		if (row.pNode && IsInNameArea(row, ptD2D))
		{
			// 첫 번째 클릭(OnLButtonDown)의 "천천히 두 번 클릭" 타이머가 이어서 재발동하지 않도록 초기화
			m_pLastClickedNode = nullptr;
			BeginEditName(row.pNode, row.labelRect, row.rowRect);
			return;
		}
	}
}

void CLayerView::CommitEditName()
{
	if (!m_wndEditName.GetSafeHwnd() || !m_wndEditName.IsWindowVisible() || !m_pEditingNode)
		return;

	// 아래 포커스 이동이 EN_KILLFOCUS -> CommitEditName 을 다시 부르므로 먼저 편집 상태를 해제 (재진입 방지)
	ge::Node* pNode = m_pEditingNode;
	m_pEditingNode = nullptr;

	CString strText;
	m_wndEditName.GetWindowText(strText);
	strText.Trim();
	HideEditBox();

	if (!strText.IsEmpty())
	{
		pNode->SetName((LPCTSTR)strText);
		CString strLog;
		strLog.Format(_T("[레이어 패널] 이름 변경 완료: '%s'"), (LPCTSTR)strText);
		LogAppEvent(strLog);

		CM2DrawDoc* pDoc = GetDocSafe();
		if (pDoc) pDoc->UpdateAllViews(nullptr);
		CommitActiveDocUndo(L"이름 변경");
	}
	SafeInvalidate(FALSE);
}

void CLayerView::CancelEditName()
{
	if (m_wndEditName.GetSafeHwnd() && m_wndEditName.IsWindowVisible())
	{
		m_pEditingNode = nullptr;
		HideEditBox();
		SafeInvalidate(FALSE);
	}
}

void CLayerView::HideEditBox()
{
	// 편집 상자가 포커스를 가진 채 숨겨지면 키 입력이 숨은 창으로 가므로 레이어 뷰로 포커스를 돌려줌
	// (EN_KILLFOCUS 로 들어온 경우엔 포커스가 이미 다른 곳으로 이동 중이므로 건드리지 않음)
	if (::GetFocus() == m_wndEditName.GetSafeHwnd())
		SetFocus();
	m_wndEditName.ShowWindow(SW_HIDE);
}

void CLayerView::OnEditKillFocus()
{
	CommitEditName();
}

void CLayerView::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	if (m_capturedControl)
	{
		if (m_capturedControl->OnMouseMove(ptD2D))
		{
			SafeInvalidate(FALSE);
		}
		return;
	}

	bool needRepaint = false;
	if (m_btnNewLayer && m_btnNewLayer->OnMouseMove(ptD2D)) needRepaint = true;
	if (m_btnMoveUp && m_btnMoveUp->OnMouseMove(ptD2D)) needRepaint = true;
	if (m_btnMoveDown && m_btnMoveDown->OnMouseMove(ptD2D)) needRepaint = true;
	if (m_btnDelete && m_btnDelete->OnMouseMove(ptD2D)) needRepaint = true;

	for (auto& row : m_rowItems)
	{
		if (row.chkVisible && row.chkVisible->OnMouseMove(ptD2D)) needRepaint = true;
		if (row.btnOpacity && row.btnOpacity->OnMouseMove(ptD2D)) needRepaint = true;
		if (row.btnBlendMode && row.btnBlendMode->OnMouseMove(ptD2D)) needRepaint = true;
		if (row.chkLocked && row.chkLocked->OnMouseMove(ptD2D)) needRepaint = true;
	}

	if (needRepaint)
	{
		SafeInvalidate(FALSE);
	}
}

void CLayerView::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	D2D1_POINT_2F ptD2D = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));

	if (m_capturedControl)
	{
		m_capturedControl->OnLButtonUp(ptD2D);
		m_capturedControl.reset();
	}

	if (GetCapture() == this)
	{
		ReleaseCapture();
	}

	// 새 레이어/이동/삭제 버튼, 보이기·잠금 토글, 혼합 모드 등 한 번의 조작이 끝난 시점에 변경 기록
	CommitActiveDocUndo(L"레이어 패널");
	SafeInvalidate(FALSE);
}

BOOL CLayerView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	CRect rc;
	GetClientRect(&rc);
	float viewH = static_cast<float>(rc.Height());

	if (m_totalContentHeight > viewH)
	{
		m_scrollY -= (zDelta > 0 ? 30.0f : -30.0f);
		m_scrollY = max(0.0f, min(m_totalContentHeight - viewH, m_scrollY));
		UpdateLayout(static_cast<float>(rc.Width()), viewH);
		SafeInvalidate(FALSE);
		return TRUE;
	}

	return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CLayerView::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	// 포커스가 이름 편집 상자로 넘어가는 경우(편집 시작)는 확정하지 않음.
	// (이전에는 여기서 확정 -> 방금 연 편집 상자가 즉시 닫혀, 레이어 뷰가 포커스를 가진 뒤로는 이름 변경이 시작되지 않았음)
	const bool bFocusToEdit = pNewWnd && pNewWnd->GetSafeHwnd() == m_wndEditName.GetSafeHwnd();
	if (!bFocusToEdit)
		CommitEditName();
	if (m_capturedControl)
	{
		m_capturedControl->ResetState();
		m_capturedControl.reset();
	}
	SafeInvalidate(FALSE);
}

BOOL CLayerView::PreTranslateMessage(MSG* pMsg)
{
	// 이름 편집 상자: Enter = 확정, Esc = 취소 (단일행 CEdit 은 Enter 를 스스로 처리하지 않음)
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_wndEditName.GetSafeHwnd()
		&& m_wndEditName.IsWindowVisible())
	{
		if (pMsg->wParam == VK_RETURN) { CommitEditName(); return TRUE; }
		if (pMsg->wParam == VK_ESCAPE) { CancelEditName(); return TRUE; }
	}

	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_DELETE || pMsg->wParam == VK_BACK))
	{
		if (m_wndEditName.GetSafeHwnd() && m_wndEditName.IsWindowVisible())
		{
			return CWnd::PreTranslateMessage(pMsg);
		}

		CM2DrawDoc* pDoc = GetDocSafe();
		if (pDoc)
		{
			pDoc->PerformSmartDelete();
			RefreshLayers();
			SafeInvalidate(FALSE);
			return TRUE;
		}
	}
	return CWnd::PreTranslateMessage(pMsg);
}

void CLayerView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_DELETE || nChar == VK_BACK)
	{
		if (m_wndEditName.GetSafeHwnd() && m_wndEditName.IsWindowVisible())
		{
			CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
			return;
		}

		CM2DrawDoc* pDoc = GetDocSafe();
		if (pDoc)
		{
			pDoc->PerformSmartDelete();
			RefreshLayers();
			SafeInvalidate(FALSE);
			return;
		}
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

// ============================================================================
// CLayerWnd (CDockablePane) 구현
// ============================================================================
IMPLEMENT_DYNAMIC(CLayerWnd, CDockablePane)

BEGIN_MESSAGE_MAP(CLayerWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_GETMINMAXINFO()
	ON_WM_SETFOCUS()
END_MESSAGE_MAP()

CLayerWnd::CLayerWnd() noexcept
{
}

CLayerWnd::~CLayerWnd()
{
}

int CLayerWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rectDummy(0, 0, 0, 0);
	CString strD2DClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), (HBRUSH)::GetStockObject(BLACK_BRUSH), nullptr);
	if (!m_wndLayerView.Create(strD2DClass, _T("LayerView"), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, rectDummy, this, 4))
	{
		TRACE0("레이어 뷰를 만들지 못했습니다.\n");
		return -1;
	}

	// 최소 도킹 크기 설정
	SetMinSize(CSize(240, 180));

	AdjustLayout();
	return 0;
}

void CLayerWnd::OnDestroy()
{
	CDockablePane::OnDestroy();
}

void CLayerWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	AdjustLayout();
}

void CLayerWnd::AdjustLayout()
{
	if (GetSafeHwnd() == nullptr || (AfxGetMainWnd() && AfxGetMainWnd()->IsIconic()))
	{
		return;
	}

	CRect rectClient;
	GetClientRect(rectClient);

	if (m_wndLayerView.GetSafeHwnd())
	{
		m_wndLayerView.SetWindowPos(nullptr, rectClient.left, rectClient.top, rectClient.Width(), rectClient.Height(), SWP_NOACTIVATE | SWP_NOZORDER);
		m_wndLayerView.ShowWindow(SW_SHOW);
		m_wndLayerView.SafeInvalidate(FALSE);
	}
}

void CLayerWnd::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDockablePane::OnGetMinMaxInfo(lpMMI);
	lpMMI->ptMinTrackSize.x = 240;
	lpMMI->ptMinTrackSize.y = 180;
	lpMMI->ptMaxTrackSize.x = 420;
}

void CLayerWnd::OnSetFocus(CWnd* pOldWnd)
{
	CDockablePane::OnSetFocus(pOldWnd);
	if (m_wndLayerView.GetSafeHwnd())
	{
		m_wndLayerView.SetFocus();
	}
}