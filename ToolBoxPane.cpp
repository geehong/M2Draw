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
	ON_WM_LBUTTONUP()
	ON_WM_TIMER()
	ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(CToolFlyoutWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

CToolBoxPane::CToolBoxPane() noexcept
	: m_pD2DFactory(nullptr)
	, m_pRenderTarget(nullptr)
	, m_pBrush(nullptr)
	, m_pWICFactory(nullptr)
	, m_hoverIndex(-1)
	, m_bTrackingMouse(false)
{
	m_buttons = {
		{ ToolType::Select,      L"선택 (V)",        L'V', CRect() },
		{ ToolType::Pan,         L"손도구 (H)",      L'H', CRect() },
		{ ToolType::Pen,         L"펜도구 (P)",      L'P', CRect() },
		{ ToolType::Line,        L"직선 (L)",        L'L', CRect() },
		{ ToolType::Rect,        L"사각형 (M)",      L'M', CRect() },
		{ ToolType::Ellipse,     L"타원 (E)",        L'E', CRect() },
		{ ToolType::Polygon,     L"다각형 (G)",      L'G', CRect() },
		{ ToolType::Text,        L"텍스트 (T)",      L'T', CRect() },
		{ ToolType::PaintBucket, L"페인트 통 (K)",   L'K', CRect() },
		{ ToolType::ZoomIn,      L"돋보기 확대 (Z)", L'Z', CRect() },
		{ ToolType::ZoomOut,     L"돋보기 축소 (X)", L'X', CRect() }
	};

	// 사각형 그룹: 사각형 / 삼각형 (마지막 선택은 앱 설정에 저장)
	for (auto& btn : m_buttons)
	{
		if (btn.type == ToolType::Rect)
		{
			btn.subTools = { ToolType::Rect, ToolType::Triangle };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("RectGroupTool"), static_cast<int>(ToolType::Rect));
			if (saved == static_cast<int>(ToolType::Triangle))
				btn.type = ToolType::Triangle;
		}
	}
}

CToolBoxPane::~CToolBoxPane()
{
	DiscardDeviceResources();
	if (m_pWICFactory)
	{
		m_pWICFactory->Release();
		m_pWICFactory = nullptr;
	}
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

	// WIC Imaging Factory 생성
	CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&m_pWICFactory)
	);

	// ToolManager 변경 알림 등록
	GetApp()->GetToolManager().AddListener(this);
	return 0;
}

void CToolBoxPane::OnDestroy()
{
	GetApp()->GetToolManager().RemoveListener(this);
	CDockablePane::OnDestroy();
}

void CToolBoxPane::OnToolChanged(ToolType newTool)
{
	// 다른 경로(단축키 등)로 서브도구가 활성화되면 그룹 대표 아이콘도 맞춥니다.
	for (auto& btn : m_buttons)
	{
		for (ToolType t : btn.subTools)
		{
			if (t == newTool)
				btn.type = newTool;
		}
	}

	if (GetSafeHwnd())
		Invalidate(FALSE);
}

bool CToolBoxPane::IsButtonActive(const ToolButtonItem& btn, ToolType active) const
{
	if (btn.type == active)
		return true;
	for (ToolType t : btn.subTools)
	{
		if (t == active)
			return true;
	}
	return false;
}

ID2D1Bitmap* CToolBoxPane::LoadBitmapFromFile(const wchar_t* filePath)
{
	if (!m_pRenderTarget || !m_pWICFactory)
		return nullptr;

	IWICBitmapDecoder* pDecoder = nullptr;
	HRESULT hr = m_pWICFactory->CreateDecoderFromFilename(
		filePath,
		nullptr,
		GENERIC_READ,
		WICDecodeMetadataCacheOnDemand,
		&pDecoder
	);

	if (FAILED(hr) || !pDecoder)
		return nullptr;

	IWICBitmapFrameDecode* pSource = nullptr;
	hr = pDecoder->GetFrame(0, &pSource);

	IWICFormatConverter* pConverter = nullptr;
	if (SUCCEEDED(hr))
	{
		hr = m_pWICFactory->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr))
	{
		hr = pConverter->Initialize(
			pSource,
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0f,
			WICBitmapPaletteTypeCustom
		);
	}

	ID2D1Bitmap* pBitmap = nullptr;
	if (SUCCEEDED(hr))
	{
		hr = m_pRenderTarget->CreateBitmapFromWicBitmap(pConverter, nullptr, &pBitmap);
	}

	if (pConverter) pConverter->Release();
	if (pSource) pSource->Release();
	if (pDecoder) pDecoder->Release();

	return SUCCEEDED(hr) ? pBitmap : nullptr;
}

HRESULT CToolBoxPane::CreateDeviceResources()
{
	HRESULT hr = S_OK;
	if (!m_pD2DFactory)
	{
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	}

	if (!m_pWICFactory)
	{
		CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&m_pWICFactory)
		);
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

		// 아이콘 비트맵 로드 및 캐시
		if (SUCCEEDED(hr))
		{
			// 실행 파일 또는 리소스 기준 경로 구성
			wchar_t exePath[MAX_PATH] = { 0 };
			GetModuleFileName(nullptr, exePath, MAX_PATH);
			PathRemoveFileSpec(exePath);

			// 프로젝트 및 설치 경로 모두 탐색
			CString basePath = _T("c:\\www\\project\\desktop\\ApiDDraw\\M2Draw\\res\\icon\\png\\");

			struct IconMapItem {
				ToolType type;
				const wchar_t* normName;
				const wchar_t* actName;
			} mappings[] = {
				{ ToolType::Select,      L"cursor-line.png",     L"cursor-line_active.png" },
				{ ToolType::Pan,         L"hand.png",            L"hand_active.png" },
				{ ToolType::Pen,         L"spline-pointer.png",  L"spline-pointer_active.png" },
				{ ToolType::Line,        L"slash.png",           L"slash_active.png" },
				{ ToolType::Rect,        L"square-line.png",     L"square-line_active.png" },
				{ ToolType::Ellipse,     L"circle-line.png",     L"circle-line_active.png" },
				{ ToolType::Polygon,     L"hexagon-line.png",    L"hexagon-line_active.png" },
				{ ToolType::Text,        L"type.png",            L"type_active.png" },
				{ ToolType::PaintBucket, L"paint-bucket.png",    L"paint-bucket_active.png" },
				{ ToolType::ZoomIn,      L"zoom-in-line.png",    L"zoom-in-line_active.png" },
				{ ToolType::ZoomOut,     L"zoom-out-line.png",   L"zoom-out-line_active.png" }
			};

			for (const auto& item : mappings)
			{
				CString normalPath = basePath + item.normName;
				CString activePath = basePath + item.actName;

				m_iconNormal[item.type] = LoadBitmapFromFile(normalPath);
				m_iconActive[item.type] = LoadBitmapFromFile(activePath);
			}
		}
	}
	return hr;
}

void CToolBoxPane::DiscardDeviceResources()
{
	for (auto& pair : m_iconNormal)
	{
		if (pair.second) { pair.second->Release(); }
	}
	m_iconNormal.clear();

	for (auto& pair : m_iconActive)
	{
		if (pair.second) { pair.second->Release(); }
	}
	m_iconActive.clear();

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

		bool isSelected = IsButtonActive(btn, activeTool);
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

		// 서브도구 그룹 표시: 우하단 작은 삼각형
		if (!btn.subTools.empty() && m_pD2DFactory)
		{
			ID2D1PathGeometry* pInd = nullptr;
			m_pD2DFactory->CreatePathGeometry(&pInd);
			if (pInd)
			{
				ID2D1GeometrySink* pSink = nullptr;
				pInd->Open(&pSink);
				if (pSink)
				{
					pSink->BeginFigure(D2D1::Point2F(r.right - 3.0f, r.bottom - 9.0f), D2D1_FIGURE_BEGIN_FILLED);
					pSink->AddLine(D2D1::Point2F(r.right - 3.0f, r.bottom - 3.0f));
					pSink->AddLine(D2D1::Point2F(r.right - 9.0f, r.bottom - 3.0f));
					pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
					pSink->Close();
					pSink->Release();
				}
				m_pBrush->SetColor(isSelected ? D2D1::ColorF(0.20f, 0.45f, 0.85f) : D2D1::ColorF(0.40f, 0.43f, 0.50f));
				m_pRenderTarget->FillGeometry(pInd, m_pBrush);
				pInd->Release();
			}
		}
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
	// 1. 등록된 고해상도 투명 PNG 비트맵이 있으면 비트맵을 우선 렌더링
	ID2D1Bitmap* pBmp = nullptr;
	if (isSelected)
	{
		auto it = m_iconActive.find(type);
		if (it != m_iconActive.end() && it->second) pBmp = it->second;
	}
	if (!pBmp)
	{
		auto it = m_iconNormal.find(type);
		if (it != m_iconNormal.end() && it->second) pBmp = it->second;
	}

	if (pBmp)
	{
		// 버튼 내부 중앙에 22x22 또는 24x24 크기로 부드럽게 스케일링하여 렌더링
		const float iconSize = 22.0f;
		float cx = (btnRect.left + btnRect.right) * 0.5f;
		float cy = (btnRect.top + btnRect.bottom) * 0.5f;

		D2D1_RECT_F destRect = D2D1::RectF(
			cx - iconSize * 0.5f,
			cy - iconSize * 0.5f,
			cx + iconSize * 0.5f,
			cy + iconSize * 0.5f
		);

		rt->DrawBitmap(
			pBmp,
			destRect,
			1.0f,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
		);
		return;
	}

	// 2. 비트맵이 없을 경우 벡터 지오메트리 폴백
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
	case ToolType::Triangle: // 삼각형
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				pSink->BeginFigure(D2D1::Point2F(cx, cy - 7.0f), D2D1_FIGURE_BEGIN_HOLLOW);
				pSink->AddLine(D2D1::Point2F(cx + 8.0f, cy + 6.0f));
				pSink->AddLine(D2D1::Point2F(cx - 8.0f, cy + 6.0f));
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
	// 플라이아웃이 열려 있으면(마우스 캡처 중) 팝업 위 항목만 하이라이트
	if (m_flyoutOpen)
	{
		CPoint sp = point;
		ClientToScreen(&sp);
		m_flyout.SetHot(m_flyout.HitTest(sp));
		return;
	}

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
		if (!m_buttons[i].rect.PtInRect(point))
			continue;

		if (!m_buttons[i].subTools.empty())
		{
			// 그룹 버튼: 짧게 클릭하면 대표 도구 선택, 길게 누르거나 우하단 삼각 영역이면 플라이아웃 펼침
			m_pressIndex = static_cast<int>(i);
			SetCapture();

			const CRect& r = m_buttons[i].rect;
			CRect indicator(r.right - 12, r.bottom - 12, r.right, r.bottom);
			if (indicator.PtInRect(point))
				OpenFlyout(static_cast<int>(i));
			else
				SetTimer(kFlyoutTimer, kFlyoutDelayMs, nullptr);
			return;
		}

		// ToolManager의 활성 도구 변경
		GetApp()->GetToolManager().SetActiveTool(m_buttons[i].type);
		Invalidate(FALSE);
		return;
	}

	CDockablePane::OnLButtonDown(nFlags, point);
}

void CToolBoxPane::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_pressIndex < 0)
	{
		CDockablePane::OnLButtonUp(nFlags, point);
		return;
	}

	const int idx = m_pressIndex;
	KillTimer(kFlyoutTimer);

	bool selected = false;
	ToolType chosen = m_buttons[idx].type;

	if (m_flyoutOpen)
	{
		// 팝업 항목 위에서 놓으면 확정, 밖이면 취소
		CPoint sp = point;
		ClientToScreen(&sp);
		const int hit = m_flyout.HitTest(sp);
		if (hit >= 0)
		{
			chosen = m_flyout.GetItem(hit);
			selected = true;
		}
	}
	else
	{
		// 짧은 클릭: 현재 대표 도구
		selected = true;
	}

	// OnCaptureChanged가 취소 로직을 타지 않도록 먼저 상태를 해제
	m_pressIndex = -1;
	CloseFlyout();
	if (GetCapture() == this)
		ReleaseCapture();

	if (selected)
	{
		m_buttons[idx].type = chosen;
		if (m_buttons[idx].subTools.size() > 1 && m_buttons[idx].subTools[0] == ToolType::Rect)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("RectGroupTool"), static_cast<int>(chosen));
		GetApp()->GetToolManager().SetActiveTool(chosen);
	}
	Invalidate(FALSE);
}

void CToolBoxPane::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kFlyoutTimer)
	{
		KillTimer(kFlyoutTimer);
		if (m_pressIndex >= 0 && !m_flyoutOpen)
			OpenFlyout(m_pressIndex);
		return;
	}
	CDockablePane::OnTimer(nIDEvent);
}

void CToolBoxPane::OnCaptureChanged(CWnd* pWnd)
{
	// 포커스 상실 등으로 캡처를 잃으면 플라이아웃을 취소합니다.
	if (m_pressIndex >= 0)
	{
		m_pressIndex = -1;
		KillTimer(kFlyoutTimer);
		CloseFlyout();
		Invalidate(FALSE);
	}
	CDockablePane::OnCaptureChanged(pWnd);
}

void CToolBoxPane::OpenFlyout(int buttonIndex)
{
	if (buttonIndex < 0 || buttonIndex >= static_cast<int>(m_buttons.size()))
		return;

	KillTimer(kFlyoutTimer);
	const ToolButtonItem& btn = m_buttons[buttonIndex];
	CPoint pt(btn.rect.right + 4, btn.rect.top);
	ClientToScreen(&pt);

	if (m_flyout.Open(this, pt, btn.subTools, btn.type))
		m_flyoutOpen = true;
}

void CToolBoxPane::CloseFlyout()
{
	if (m_flyout.GetSafeHwnd())
		m_flyout.DestroyWindow();
	m_flyoutOpen = false;
}

// ---------------------------------------------------------------------------
// CToolFlyoutWnd
// ---------------------------------------------------------------------------

static const wchar_t* GetFlyoutToolName(ToolType t)
{
	switch (t)
	{
	case ToolType::Rect:     return L"사각형 (M)";
	case ToolType::Triangle: return L"삼각형";
	default:                 return L"";
	}
}

static void DrawFlyoutGlyph(CDC& dc, ToolType t, CRect box, COLORREF color)
{
	CPen pen(PS_SOLID, 2, color);
	CPen* pOldPen = dc.SelectObject(&pen);
	CBrush* pOldBrush = static_cast<CBrush*>(dc.SelectStockObject(NULL_BRUSH));

	const CPoint c = box.CenterPoint();
	switch (t)
	{
	case ToolType::Rect:
		dc.Rectangle(c.x - 8, c.y - 6, c.x + 8, c.y + 6);
		break;
	case ToolType::Triangle:
	{
		CPoint pts[3] = { CPoint(c.x, c.y - 7), CPoint(c.x + 9, c.y + 6), CPoint(c.x - 9, c.y + 6) };
		dc.Polygon(pts, 3);
		break;
	}
	default:
		break;
	}

	dc.SelectObject(pOldBrush);
	dc.SelectObject(pOldPen);
}

BOOL CToolFlyoutWnd::Open(CWnd* pOwner, CPoint screenTopLeft, const std::vector<ToolType>& items, ToolType current)
{
	if (GetSafeHwnd())
		DestroyWindow();

	m_items = items;
	m_current = current;
	m_hot = -1;

	const int w = kItemW + 4;
	const int h = kItemH * static_cast<int>(m_items.size()) + 4;

	const CString cls = AfxRegisterWndClass(CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW, ::LoadCursor(nullptr, IDC_ARROW));
	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, cls, _T(""), WS_POPUP,
	              screenTopLeft.x, screenTopLeft.y, w, h, pOwner->GetSafeHwnd(), nullptr))
		return FALSE;

	ShowWindow(SW_SHOWNOACTIVATE);
	return TRUE;
}

int CToolFlyoutWnd::HitTest(CPoint screenPt) const
{
	if (!GetSafeHwnd())
		return -1;

	CPoint pt = screenPt;
	const_cast<CToolFlyoutWnd*>(this)->ScreenToClient(&pt);

	CRect rc;
	GetClientRect(&rc);
	if (!rc.PtInRect(pt))
		return -1;

	const int idx = (pt.y - 2) / kItemH;
	return (idx >= 0 && idx < static_cast<int>(m_items.size())) ? idx : -1;
}

void CToolFlyoutWnd::SetHot(int index)
{
	if (index != m_hot)
	{
		m_hot = index;
		if (GetSafeHwnd())
			Invalidate(FALSE);
	}
}

BOOL CToolFlyoutWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CToolFlyoutWnd::OnPaint()
{
	CPaintDC dc(this);

	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(rc, RGB(250, 250, 252));
	dc.Draw3dRect(rc, RGB(170, 175, 190), RGB(170, 175, 190));

	CFont* pOldFont = static_cast<CFont*>(dc.SelectStockObject(DEFAULT_GUI_FONT));
	dc.SetBkMode(TRANSPARENT);

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
	{
		CRect item(2, 2 + i * kItemH, 2 + kItemW, 2 + (i + 1) * kItemH);

		const bool hot = (i == m_hot);
		if (hot)
			dc.FillSolidRect(item, RGB(208, 224, 250));

		const COLORREF color = hot ? RGB(30, 90, 200) : RGB(60, 66, 80);
		CRect glyph(item.left, item.top, item.left + kItemH, item.bottom);
		DrawFlyoutGlyph(dc, m_items[i], glyph, color);

		// 현재 대표 도구 표시
		if (m_items[i] == m_current)
			dc.FillSolidRect(item.left, item.top + 6, 3, item.Height() - 12, RGB(51, 115, 217));

		CRect text(item.left + kItemH, item.top, item.right - 4, item.bottom);
		dc.SetTextColor(color);
		dc.DrawText(GetFlyoutToolName(m_items[i]), -1, text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
	}

	dc.SelectObject(pOldFont);
}
