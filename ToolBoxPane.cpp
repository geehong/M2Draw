#include "pch.h"
#include "ToolBoxPane.h"
#include "M2Draw.h"
#include "MainFrm.h"
#include "M2DrawDoc.h"

// 그룹/순서/불리언 등은 도구가 아니라 동작: 플라이아웃 그룹의 항목으로 ToolType::Action* 를 사용하고, 선택하면 명령을 실행한다
static bool IsActionType(ToolType t)
{
	return t == ToolType::ActionGroup || t == ToolType::ActionUngroup || t == ToolType::ActionToFront ||
	       t == ToolType::ActionForward || t == ToolType::ActionBackward || t == ToolType::ActionToBack ||
	       t == ToolType::ActionBooleanUnion || t == ToolType::ActionBooleanSubtract ||
	       t == ToolType::ActionBooleanIntersect || t == ToolType::ActionBooleanExclude ||
	       t == ToolType::ActionBooleanTrim ||
	       t == ToolType::ActionClipMask || t == ToolType::ActionReleaseMask;
}
static UINT CommandOfType(ToolType t)
{
	switch (t)
	{
	case ToolType::ActionGroup:           return ID_OBJECT_GROUP;
	case ToolType::ActionUngroup:         return ID_OBJECT_UNGROUP;
	case ToolType::ActionToFront:         return ID_OBJECT_FRONT;
	case ToolType::ActionForward:         return ID_OBJECT_FORWARD;
	case ToolType::ActionBackward:        return ID_OBJECT_BACKWARD;
	case ToolType::ActionToBack:          return ID_OBJECT_BACK;
	case ToolType::ActionBooleanUnion:    return ID_BOOLEAN_UNION;
	case ToolType::ActionBooleanSubtract: return ID_BOOLEAN_SUBTRACT;
	case ToolType::ActionBooleanIntersect:return ID_BOOLEAN_INTERSECT;
	case ToolType::ActionBooleanExclude:  return ID_BOOLEAN_EXCLUDE;
	case ToolType::ActionBooleanTrim:     return ID_BOOLEAN_TRIM;
	case ToolType::ActionClipMask:        return ID_OBJECT_CLIP_MASK;
	case ToolType::ActionReleaseMask:     return ID_OBJECT_RELEASE_MASK;
	default:                              return 0;
	}
}
static bool IsArrangeCommand(UINT id) { return id >= ID_OBJECT_FRONT && id <= ID_OBJECT_BACK; }
static bool IsBooleanCommand(UINT id) { return id >= ID_BOOLEAN_UNION && id <= ID_BOOLEAN_TRIM; }
static bool IsClipMaskCommand(UINT id) { return id == ID_OBJECT_CLIP_MASK || id == ID_OBJECT_RELEASE_MASK; }
static const wchar_t* GetFlyoutToolName(ToolType t);

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
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, &CToolBoxPane::OnToolTipText)
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
		{ ToolType::Gradient,    L"그라디언트 (D)",  L'D', CRect() },
		{ ToolType::ZoomIn,      L"돋보기 (확대 Z / 축소 X)", L'Z', CRect() },   // 확대/축소 플라이아웃 그룹
		// 동작 버튼 그룹 (도구가 아님): 그룹화 / 그룹 해제. 도구 묶음과 간격을 둠
		{ ToolType::ActionGroup,        L"그룹 / 그룹 해제",          0, CRect(), {}, 12 },
		// 순서(Z-order) 동작 그룹: 맨 앞으로 / 앞으로 / 뒤로 / 맨 뒤로
		{ ToolType::ActionToFront,      L"순서",                       0, CRect(), {}, 0 },
		// 불리언(패스파인더) 연산 그룹: 합치기 / 빼기 / 교차 / 제외 / 트림
		{ ToolType::ActionBooleanUnion, L"불리언 (패스파인더)",        0, CRect(), {}, 0 },
		// 클리핑 마스크 그룹: 만들기 / 해제
		{ ToolType::ActionClipMask,     L"클리핑 마스크",              0, CRect(), {}, 0 }
	};

	// 사각형 그룹: 사각형 / 삼각형 (마지막 선택은 앱 설정에 저장)
	for (auto& btn : m_buttons)
	{
		if (btn.type == ToolType::Rect)
		{
			btn.subTools = { ToolType::Rect, ToolType::Triangle, ToolType::Frame };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("RectGroupTool"), static_cast<int>(ToolType::Rect));
			if (saved == static_cast<int>(ToolType::Triangle))
				btn.type = ToolType::Triangle;
			else if (saved == static_cast<int>(ToolType::Frame))
				btn.type = ToolType::Frame;
		}
		else if (btn.type == ToolType::Polygon)
		{
			// 다각형 & 특수 도형 그룹: 정다각형, 별, 기어, 스파이럴, 하트
			btn.subTools = { ToolType::Polygon, ToolType::Polyline, ToolType::Star, ToolType::Gear, ToolType::Spiral, ToolType::Heart };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("PolygonGroupTool"), static_cast<int>(ToolType::Polygon));
			for (ToolType st : btn.subTools)
			{
				if (saved == static_cast<int>(st))
				{
					btn.type = st;
					break;
				}
			}
		}
		else if (btn.type == ToolType::ZoomIn)
		{
			// 돋보기 그룹: 확대 / 축소
			btn.subTools = { ToolType::ZoomIn, ToolType::ZoomOut };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("ZoomGroupTool"), static_cast<int>(ToolType::ZoomIn));
			if (saved == static_cast<int>(ToolType::ZoomOut))
				btn.type = ToolType::ZoomOut;
		}
		else if (btn.type == ToolType::ActionGroup)
		{
			// 그룹 동작 그룹: 그룹화 / 그룹 해제 (마지막에 쓴 동작이 대표)
			btn.subTools = { ToolType::ActionGroup, ToolType::ActionUngroup };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("GroupActionTool"), static_cast<int>(ToolType::ActionGroup));
			if (saved == static_cast<int>(ToolType::ActionUngroup))
				btn.type = ToolType::ActionUngroup;
		}
		else if (btn.type == ToolType::ActionToFront)
		{
			// 순서 동작 그룹 (마지막에 쓴 동작이 대표)
			btn.subTools = { ToolType::ActionToFront, ToolType::ActionForward, ToolType::ActionBackward, ToolType::ActionToBack };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("ArrangeActionTool"), static_cast<int>(ToolType::ActionToFront));
			for (ToolType st : btn.subTools)
				if (saved == static_cast<int>(st)) { btn.type = st; break; }
		}
		else if (btn.type == ToolType::ActionBooleanUnion)
		{
			// 불리언 동작 그룹: 합치기 / 빼기 / 교차 / 제외 / 트림
			btn.subTools = { ToolType::ActionBooleanUnion, ToolType::ActionBooleanSubtract, ToolType::ActionBooleanIntersect, ToolType::ActionBooleanExclude, ToolType::ActionBooleanTrim };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("BooleanActionTool"), static_cast<int>(ToolType::ActionBooleanUnion));
			for (ToolType st : btn.subTools)
				if (saved == static_cast<int>(st)) { btn.type = st; break; }
		}
		else if (btn.type == ToolType::ActionClipMask)
		{
			// 클리핑 마스크 그룹: 만들기 / 해제
			btn.subTools = { ToolType::ActionClipMask, ToolType::ActionReleaseMask };
			const int saved = AfxGetApp()->GetProfileInt(_T("ToolBox"), _T("ClipMaskActionTool"), static_cast<int>(ToolType::ActionClipMask));
			if (saved == static_cast<int>(ToolType::ActionReleaseMask))
				btn.type = ToolType::ActionReleaseMask;
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

	EnableToolTips(TRUE);
	SetTimer(kStateTimer, kStateIntervalMs, nullptr);   // 그룹/그룹 해제 버튼 활성 상태 감시
	return 0;
}

void CToolBoxPane::OnDestroy()
{
	KillTimer(kStateTimer);
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
	if (IsActionType(btn.type))
		return false;   // 동작 버튼은 선택 상태가 없음
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
				{ ToolType::Triangle,    L"triangle-line.png",   L"triangle-line_active.png" },
				{ ToolType::Ellipse,     L"circle-line.png",     L"circle-line_active.png" },
				{ ToolType::Polygon,     L"hexagon-line.png",    L"hexagon-line_active.png" },
				{ ToolType::Star,        L"star-line.png",       L"star-line_active.png" },
				{ ToolType::Gear,        L"gear-line.png",       L"gear-line_active.png" },
				{ ToolType::Spiral,      L"spiral-line.png",     L"spiral-line_active.png" },
				{ ToolType::Heart,       L"heart-line.png",      L"heart-line_active.png" },
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
	if (m_pDashStyle) { m_pDashStyle->Release(); m_pDashStyle = nullptr; }
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

	int top = startY;
	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		top += m_buttons[i].gapBefore;
		m_buttons[i].rect = CRect(left, top, left + btnSize, top + btnSize);
		top += btnSize + margin;
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

		const bool isAction = IsActionType(btn.type);
		const bool actionEnabled = isAction && IsCommandEnabled(CommandOfType(btn.type));
		bool isSelected = IsButtonActive(btn, activeTool);
		bool isHover = (i == m_hoverIndex) && (!isAction || actionEnabled);

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
		if (isAction)
			DrawActionIcon(m_pRenderTarget, m_pBrush, CommandOfType(btn.type), r, actionEnabled);
		else
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
	case ToolType::Frame: // 프레임
	{
		rt->DrawRectangle(D2D1::RectF(cx - 6.0f, cy - 6.0f, cx + 6.0f, cy + 6.0f), brush, 1.6f);
		rt->DrawLine(D2D1::Point2F(cx - 9.0f, cy - 3.0f), D2D1::Point2F(cx - 6.0f, cy - 3.0f), brush, 1.4f);
		rt->DrawLine(D2D1::Point2F(cx - 9.0f, cy + 3.0f), D2D1::Point2F(cx - 6.0f, cy + 3.0f), brush, 1.4f);
		rt->DrawLine(D2D1::Point2F(cx - 3.0f, cy - 9.0f), D2D1::Point2F(cx - 3.0f, cy - 6.0f), brush, 1.4f);
		rt->DrawLine(D2D1::Point2F(cx + 3.0f, cy - 9.0f), D2D1::Point2F(cx + 3.0f, cy - 6.0f), brush, 1.4f);
		break;
	}
	case ToolType::Polyline: // 열린 꺾은선
	{
		const D2D1_POINT_2F pl[4] = { D2D1::Point2F(cx - 8.0f, cy + 6.0f), D2D1::Point2F(cx - 3.0f, cy - 6.0f), D2D1::Point2F(cx + 3.0f, cy + 4.0f), D2D1::Point2F(cx + 8.0f, cy - 6.0f) };
		for (int i = 0; i < 3; ++i) rt->DrawLine(pl[i], pl[i + 1], brush, 1.8f);
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
	case ToolType::Star: // 5각 별
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				const float PI = 3.141592653589793f;
				const float rOut = 8.0f;
				const float rIn = 3.8f;
				for (int i = 0; i < 10; ++i)
				{
					float a = -PI * 0.5f + (i * PI / 5.0f);
					float r = (i % 2 == 0) ? rOut : rIn;
					D2D1_POINT_2F pt = D2D1::Point2F(cx + r * cosf(a), cy + r * sinf(a));
					if (i == 0) pSink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
					else pSink->AddLine(pt);
				}
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();
			}
			rt->DrawGeometry(pGeo, brush, 1.6f);
			pGeo->Release();
		}
		break;
	}
	case ToolType::Gear: // 8개 톱니 기어
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				const float PI = 3.141592653589793f;
				const float rOut = 8.0f;
				const float rIn = 5.8f;
				for (int i = 0; i < 16; ++i)
				{
					float a = -PI * 0.5f + (i * PI / 8.0f);
					int mod = i % 4;
					float r = (mod == 1 || mod == 2) ? rOut : rIn;
					D2D1_POINT_2F pt = D2D1::Point2F(cx + r * cosf(a), cy + r * sinf(a));
					if (i == 0) pSink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
					else pSink->AddLine(pt);
				}
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();
			}
			rt->DrawGeometry(pGeo, brush, 1.5f);
			rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.5f, 2.5f), brush, 1.2f);
			pGeo->Release();
		}
		break;
	}
	case ToolType::Spiral: // 아르키메데스 나선
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				const float PI = 3.141592653589793f;
				const int segs = 24;
				for (int i = 0; i <= segs; ++i)
				{
					float t = static_cast<float>(i) / segs;
					float a = t * 2.5f * 2.0f * PI;
					float r = 1.0f + t * 7.0f;
					D2D1_POINT_2F pt = D2D1::Point2F(cx + r * cosf(a), cy + r * sinf(a));
					if (i == 0) pSink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
					else pSink->AddLine(pt);
				}
				pSink->EndFigure(D2D1_FIGURE_END_OPEN);
				pSink->Close();
				pSink->Release();
			}
			rt->DrawGeometry(pGeo, brush, 1.6f);
			pGeo->Release();
		}
		break;
	}
	case ToolType::Heart: // 하트
	{
		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				pSink->BeginFigure(D2D1::Point2F(cx, cy - 2.5f), D2D1_FIGURE_BEGIN_HOLLOW);
				pSink->AddBezier(D2D1::BezierSegment(
					D2D1::Point2F(cx + 4.0f, cy - 8.0f),
					D2D1::Point2F(cx + 8.5f, cy - 3.0f),
					D2D1::Point2F(cx + 6.0f, cy + 2.0f)));
				pSink->AddLine(D2D1::Point2F(cx, cy + 7.5f));
				pSink->AddLine(D2D1::Point2F(cx - 6.0f, cy + 2.0f));
				pSink->AddBezier(D2D1::BezierSegment(
					D2D1::Point2F(cx - 8.5f, cy - 3.0f),
					D2D1::Point2F(cx - 4.0f, cy - 8.0f),
					D2D1::Point2F(cx, cy - 2.5f)));
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();
			}
			rt->DrawGeometry(pGeo, brush, 1.6f);
			pGeo->Release();
		}
		break;
	}
	case ToolType::Gradient: // 그라디언트 도구: 가로 그라디언트 사각형 + 시작/끝 핸들
	{
		const D2D1_RECT_F gr = D2D1::RectF(cx - 8.5f, cy - 7.0f, cx + 8.5f, cy + 7.0f);
		ID2D1GradientStopCollection* coll = nullptr;
		const D2D1_GRADIENT_STOP gs[2] = { D2D1::GradientStop(0.0f, iconColor), D2D1::GradientStop(1.0f, D2D1::ColorF(iconColor.r, iconColor.g, iconColor.b, 0.08f)) };
		if (SUCCEEDED(rt->CreateGradientStopCollection(gs, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)) && coll)
		{
			ID2D1LinearGradientBrush* lin = nullptr;
			if (SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(gr.left, cy), D2D1::Point2F(gr.right, cy)), coll, &lin)) && lin)
			{
				rt->FillRectangle(gr, lin);
				lin->Release();
			}
			coll->Release();
		}
		brush->SetColor(iconColor);
		rt->DrawRectangle(gr, brush, 1.4f);
		// 시작(사각형) -> 끝(원) 핸들
		brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
		rt->FillRectangle(D2D1::RectF(gr.left - 1.5f, cy - 2.5f, gr.left + 3.5f, cy + 2.5f), brush);
		rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(gr.right - 1.0f, cy), 2.8f, 2.8f), brush);
		brush->SetColor(iconColor);
		rt->DrawRectangle(D2D1::RectF(gr.left - 1.5f, cy - 2.5f, gr.left + 3.5f, cy + 2.5f), brush, 1.2f);
		rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(gr.right - 1.0f, cy), 2.8f, 2.8f), brush, 1.2f);
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
			// 불리언 또는 클리핑 마스크 버튼을 클릭한 경우 상단 서브도구 툴바를 즉시 해당 연산자 모음으로 전환
			if (m_buttons[i].type == ToolType::ActionBooleanUnion ||
				(!m_buttons[i].subTools.empty() && m_buttons[i].subTools[0] == ToolType::ActionBooleanUnion))
			{
				if (CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
				{
					pMain->UpdateSubToolBar(ToolType::ActionBooleanUnion);
				}
			}
			else if (m_buttons[i].type == ToolType::ActionClipMask ||
				(!m_buttons[i].subTools.empty() && m_buttons[i].subTools[0] == ToolType::ActionClipMask))
			{
				if (CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
				{
					pMain->UpdateSubToolBar(ToolType::ActionClipMask);
				}
			}

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
	const bool wasFlyout = m_flyoutOpen;

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
		else if (!m_buttons[idx].subTools.empty() && m_buttons[idx].subTools[0] == ToolType::Polygon)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("PolygonGroupTool"), static_cast<int>(chosen));
		else if (!m_buttons[idx].subTools.empty() && m_buttons[idx].subTools[0] == ToolType::ZoomIn)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("ZoomGroupTool"), static_cast<int>(chosen));
		else if (!m_buttons[idx].subTools.empty() && m_buttons[idx].subTools[0] == ToolType::ActionGroup)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("GroupActionTool"), static_cast<int>(chosen));
		else if (!m_buttons[idx].subTools.empty() && m_buttons[idx].subTools[0] == ToolType::ActionToFront)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("ArrangeActionTool"), static_cast<int>(chosen));
		else if (!m_buttons[idx].subTools.empty() && m_buttons[idx].subTools[0] == ToolType::ActionBooleanUnion)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("BooleanActionTool"), static_cast<int>(chosen));
		else if (!m_buttons[idx].subTools.empty() && m_buttons[idx].subTools[0] == ToolType::ActionClipMask)
			AfxGetApp()->WriteProfileInt(_T("ToolBox"), _T("ClipMaskActionTool"), static_cast<int>(chosen));

		if (IsActionType(chosen))
		{
			const UINT cmd = CommandOfType(chosen);
			const bool isBoolean = IsBooleanCommand(cmd);
			const bool isClipMask = IsClipMaskCommand(cmd);

			if (wasFlyout)
			{
				// 플라이아웃 메뉴에서 세부 연산자를 직접 클릭하여 선택한 경우: 해당 연산 즉시 실행!
				if (IsCommandEnabled(cmd))
				{
					if (CWnd* pMain = AfxGetMainWnd())
						pMain->SendMessage(WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
					m_enabledMask = ComputeEnabledMask();
				}
			}
			else
			{
				// 사이드 메뉴 버튼을 단순히 클릭한 경우:
				if (isBoolean)
				{
					// 상단 서브도구 툴바에 불리언 연산자들 펼치기
					if (CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
					{
						pMain->UpdateSubToolBar(ToolType::ActionBooleanUnion);
					}
				}
				else if (isClipMask)
				{
					// 상단 서브도구 툴바에 클리핑 마스크 서브도구 펼치기
					if (CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd()))
					{
						pMain->UpdateSubToolBar(ToolType::ActionClipMask);
					}
				}
				else
				{
					// 그룹/순서 등 즉각 적용 동작: 활성일 때만 실행 (오브젝트 메뉴와 같은 명령 경로 -> 활성 문서가 처리)
					if (IsCommandEnabled(cmd))
					{
						if (CWnd* pMain = AfxGetMainWnd())
							pMain->SendMessage(WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
						m_enabledMask = ComputeEnabledMask();
					}
				}
			}
		}
		else
		{
			GetApp()->GetToolManager().SetActiveTool(chosen);
		}
	}
	Invalidate(FALSE);
}

void CToolBoxPane::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kStateTimer)
	{
		const UINT mask = ComputeEnabledMask();
		if (mask != m_enabledMask)
		{
			m_enabledMask = mask;
			Invalidate(FALSE);
		}
		return;
	}
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

// ---------------------------------------------------------------------------
// 동작 버튼(그룹 / 그룹 해제) + 툴팁
// ---------------------------------------------------------------------------
UINT CToolBoxPane::ComputeEnabledMask() const
{
	CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	CM2DrawDoc* pDoc = pMain ? pMain->GetActiveM2DrawDoc() : nullptr;
	UINT mask = 0;
	if (pDoc)
	{
		if (pDoc->CanGroup())          mask |= 1;
		if (pDoc->CanUngroup())        mask |= 2;
		if (pDoc->CanArrange())        mask |= 4;
		if (pDoc->CanBoolean())        mask |= 8;
		if (pDoc->CanMakeClipMask())    mask |= 16;
		if (pDoc->CanReleaseClipMask()) mask |= 32;
	}
	return mask;
}

bool CToolBoxPane::IsCommandEnabled(UINT commandId) const
{
	if (commandId == ID_OBJECT_GROUP)        return (m_enabledMask & 1) != 0;
	if (commandId == ID_OBJECT_UNGROUP)      return (m_enabledMask & 2) != 0;
	if (IsArrangeCommand(commandId))         return (m_enabledMask & 4) != 0;
	if (IsBooleanCommand(commandId))         return (m_enabledMask & 8) != 0;
	if (commandId == ID_OBJECT_CLIP_MASK)    return (m_enabledMask & 16) != 0;
	if (commandId == ID_OBJECT_RELEASE_MASK) return (m_enabledMask & 32) != 0;
	return true;
}

// 그룹: 실선 상자 안에 겹친 두 사각형 / 그룹 해제: 점선 상자 안에 떨어진 두 사각형
void CToolBoxPane::DrawActionIcon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, UINT commandId, D2D1_RECT_F r, bool enabled)
{
	const float cx = (r.left + r.right) * 0.5f;
	const float cy = (r.top + r.bottom) * 0.5f;
	const bool group = (commandId == ID_OBJECT_GROUP);
	const D2D1_COLOR_F col = enabled ? D2D1::ColorF(0.25f, 0.28f, 0.35f) : D2D1::ColorF(0.70f, 0.72f, 0.77f);

	if (IsArrangeCommand(commandId))
	{
		// 순서: 겹친 두 사각형(채워진 쪽이 이동하는 오브젝트) + 이동 방향 화살표. 맨 앞/맨 뒤는 화살표 끝에 막대
		const bool toFront = (commandId == ID_OBJECT_FRONT || commandId == ID_OBJECT_FORWARD);
		const bool extreme = (commandId == ID_OBJECT_FRONT || commandId == ID_OBJECT_BACK);
		const D2D1_RECT_F back = D2D1::RectF(cx - 11.0f, cy - 8.0f, cx - 2.0f, cy + 1.0f);
		const D2D1_RECT_F mover = D2D1::RectF(cx - 6.0f, cy - 3.0f, cx + 3.0f, cy + 6.0f);
		brush->SetColor(col);
		if (toFront)
		{
			rt->DrawRectangle(back, brush, 1.4f);
			brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
			rt->FillRectangle(mover, brush);
			brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, 0.35f));
			rt->FillRectangle(mover, brush);
			brush->SetColor(col);
			rt->DrawRectangle(mover, brush, 1.4f);
		}
		else
		{
			brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, 0.35f));
			rt->FillRectangle(mover, brush);
			brush->SetColor(col);
			rt->DrawRectangle(mover, brush, 1.4f);
			brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
			rt->FillRectangle(back, brush);
			brush->SetColor(col);
			rt->DrawRectangle(back, brush, 1.4f);
		}
		const float ax = cx + 10.0f;
		const float y0 = cy + (toFront ? 7.0f : -7.0f);
		const float y1 = cy + (toFront ? -6.0f : 6.0f);
		const float dir = toFront ? 1.0f : -1.0f;   // 화살촉이 위(+) / 아래(-)
		rt->DrawLine(D2D1::Point2F(ax, y0), D2D1::Point2F(ax, y1), brush, 1.6f);
		rt->DrawLine(D2D1::Point2F(ax - 3.5f, y1 + dir * 3.5f), D2D1::Point2F(ax, y1), brush, 1.6f);
		rt->DrawLine(D2D1::Point2F(ax + 3.5f, y1 + dir * 3.5f), D2D1::Point2F(ax, y1), brush, 1.6f);
		if (extreme)
			rt->DrawLine(D2D1::Point2F(ax - 4.0f, y1 - dir * 1.5f), D2D1::Point2F(ax + 4.0f, y1 - dir * 1.5f), brush, 1.6f);
		return;
	}

	if (IsBooleanCommand(commandId))
	{
		brush->SetColor(col);
		const D2D1_ELLIPSE c1 = D2D1::Ellipse(D2D1::Point2F(cx - 3.5f, cy), 6.5f, 6.5f);
		const D2D1_ELLIPSE c2 = D2D1::Ellipse(D2D1::Point2F(cx + 3.5f, cy), 6.5f, 6.5f);

		// 채움 영역
		brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, 0.25f));
		if (commandId == ID_BOOLEAN_UNION)
		{
			rt->FillEllipse(c1, brush);
			rt->FillEllipse(c2, brush);
		}
		else if (commandId == ID_BOOLEAN_SUBTRACT)
		{
			rt->FillEllipse(c1, brush);
		}
		else if (commandId == ID_BOOLEAN_INTERSECT)
		{
			// 교차 영역 느낌: 중심 작은 타원
			rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.5f, 5.0f), brush);
		}
		else if (commandId == ID_BOOLEAN_EXCLUDE)
		{
			rt->FillEllipse(c1, brush);
			rt->FillEllipse(c2, brush);
		}
		else if (commandId == ID_BOOLEAN_TRIM)
		{
			rt->FillEllipse(c1, brush);
			rt->FillEllipse(c2, brush);
		}

		// 외곽선
		brush->SetColor(col);
		rt->DrawEllipse(c1, brush, 1.3f);
		rt->DrawEllipse(c2, brush, 1.3f);
		if (commandId == ID_BOOLEAN_TRIM)
		{
			// 트림 분할선 표시
			rt->DrawLine(D2D1::Point2F(cx, cy - 8.0f), D2D1::Point2F(cx, cy + 8.0f), brush, 1.5f);
		}
		return;
	}

	if (IsClipMaskCommand(commandId))
	{
		brush->SetColor(col);
		const D2D1_RECT_F sq = D2D1::RectF(cx - 7.0f, cy - 5.0f, cx + 5.0f, cy + 7.0f);
		const D2D1_ELLIPSE cir = D2D1::Ellipse(D2D1::Point2F(cx + 2.0f, cy - 2.0f), 5.5f, 5.5f);
		if (commandId == ID_OBJECT_CLIP_MASK)
		{
			// 마스크 만들기: 사각형 외곽선 + 안쪽 원 채움
			brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, 0.3f));
			rt->FillEllipse(cir, brush);
			brush->SetColor(col);
			rt->DrawRectangle(sq, brush, 1.3f);
			rt->DrawEllipse(cir, brush, 1.3f);
		}
		else
		{
			// 마스크 해제: 사각형 + 원 + 해제 대각선
			rt->DrawRectangle(sq, brush, 1.3f);
			rt->DrawEllipse(cir, brush, 1.3f);
			rt->DrawLine(D2D1::Point2F(cx - 8.0f, cy + 8.0f), D2D1::Point2F(cx + 8.0f, cy - 8.0f), brush, 1.6f);
		}
		return;
	}

	if (!group && !m_pDashStyle && m_pD2DFactory)
	{
		m_pD2DFactory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
			D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f), nullptr, 0, &m_pDashStyle);
	}

	brush->SetColor(col);
	const D2D1_RECT_F outer = D2D1::RectF(cx - 11.0f, cy - 10.0f, cx + 11.0f, cy + 10.0f);
	rt->DrawRoundedRectangle(D2D1::RoundedRect(outer, 2.0f, 2.0f), brush, 1.3f, group ? nullptr : m_pDashStyle);

	D2D1_RECT_F a, b;
	if (group)
	{
		a = D2D1::RectF(cx - 8.0f, cy - 6.0f, cx + 0.5f, cy + 2.0f);
		b = D2D1::RectF(cx - 1.5f, cy - 1.5f, cx + 7.0f, cy + 6.5f);
	}
	else
	{
		a = D2D1::RectF(cx - 8.0f, cy - 7.0f, cx - 1.5f, cy - 0.5f);
		b = D2D1::RectF(cx + 1.5f, cy + 0.5f, cx + 8.0f, cy + 7.0f);
	}
	brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, 0.18f));
	rt->FillRectangle(b, brush);
	brush->SetColor(col);
	rt->DrawRectangle(a, brush, 1.4f);
	rt->DrawRectangle(b, brush, 1.4f);
}

INT_PTR CToolBoxPane::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
{
	for (size_t i = 0; i < m_buttons.size(); ++i)
	{
		if (!m_buttons[i].rect.PtInRect(point))
			continue;
		if (pTI)
		{
			pTI->hwnd = m_hWnd;
			pTI->uId = static_cast<UINT_PTR>(i + 1);   // OnToolTipText 에서 버튼 인덱스 + 1 로 해석
			pTI->uFlags &= ~TTF_IDISHWND;
			pTI->rect = m_buttons[i].rect;
			pTI->lpszText = LPSTR_TEXTCALLBACK;
		}
		return static_cast<INT_PTR>(i + 1);
	}
	return -1;
}

BOOL CToolBoxPane::OnToolTipText(UINT /*id*/, NMHDR* pNMHDR, LRESULT* pResult)
{
	TOOLTIPTEXTW* pTTT = reinterpret_cast<TOOLTIPTEXTW*>(pNMHDR);
	const UINT_PTR idx = pNMHDR->idFrom;
	if (idx == 0 || idx > m_buttons.size())
		return FALSE;

	const ToolButtonItem& tipBtn = m_buttons[idx - 1];
	if (IsActionType(tipBtn.type))
		m_tipText = CString(GetFlyoutToolName(tipBtn.type)) + _T("   (오른쪽 삼각형/길게 누르기: 다른 동작)");
	else
		m_tipText = tipBtn.name;
	pTTT->lpszText = const_cast<LPWSTR>(static_cast<LPCWSTR>(m_tipText));
	*pResult = 0;
	return TRUE;
}

void CToolBoxPane::OpenFlyout(int buttonIndex)
{
	if (buttonIndex < 0 || buttonIndex >= static_cast<int>(m_buttons.size()))
		return;

	KillTimer(kFlyoutTimer);
	const ToolButtonItem& btn = m_buttons[buttonIndex];
	CPoint pt(btn.rect.right + 4, btn.rect.top);
	ClientToScreen(&pt);

	if (m_flyout.Open(this, pt, btn.subTools, btn.type, m_pWICFactory))
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
	case ToolType::Polygon:  return L"정다각형 (G)";
	case ToolType::Polyline: return L"열린 다각형 (폴리라인)";
	case ToolType::Frame:    return L"프레임 (F)";
	case ToolType::Star:     return L"별 (Star)";
	case ToolType::Gear:     return L"기어 (Gear)";
	case ToolType::Spiral:   return L"스파이럴 (Spiral)";
	case ToolType::Heart:    return L"하트 (Heart)";
	case ToolType::ZoomIn:   return L"돋보기 확대 (Z)";
	case ToolType::ZoomOut:  return L"돋보기 축소 (X)";
	case ToolType::ActionGroup:   return L"그룹 (Ctrl+G)";
	case ToolType::ActionUngroup: return L"그룹 해제 (Ctrl+Shift+G)";
	case ToolType::ActionToFront:  return L"맨 앞으로 (Ctrl+Shift+])";
	case ToolType::ActionForward:  return L"앞으로 (Ctrl+])";
	case ToolType::ActionBackward: return L"뒤로 (Ctrl+[)";
	case ToolType::ActionToBack:   return L"맨 뒤로 (Ctrl+Shift+[)";
	case ToolType::ActionBooleanUnion:     return L"합치기 (Ctrl+Shift+U)";
	case ToolType::ActionBooleanSubtract:  return L"빼기 (Ctrl+Shift+S)";
	case ToolType::ActionBooleanIntersect: return L"교차 (Ctrl+Shift+I)";
	case ToolType::ActionBooleanExclude:   return L"제외 (Ctrl+Shift+X)";
	case ToolType::ActionBooleanTrim:      return L"트림 / 분할 (Ctrl+Shift+T)";
	case ToolType::ActionClipMask:         return L"클리핑 마스크 만들기 (Ctrl+7)";
	case ToolType::ActionReleaseMask:      return L"클리핑 마스크 해제 (Ctrl+Alt+7)";
	default:                 return L"";
	}
}

static HBITMAP LoadFlyoutPng(const wchar_t* filename, IWICImagingFactory* pWic)
{
	IWICImagingFactory* pFactory = pWic;
	bool bReleaseFactory = false;
	if (!pFactory)
	{
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
		bReleaseFactory = (pFactory != nullptr);
	}
	if (!pFactory) return nullptr;

	CString fullPath = _T("c:\\www\\project\\desktop\\ApiDDraw\\M2Draw\\res\\icon\\png\\");
	fullPath += filename;

	IWICBitmapDecoder* pDec = nullptr;
	HRESULT hr = pFactory->CreateDecoderFromFilename(fullPath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDec);
	if (FAILED(hr) || !pDec)
	{
		if (bReleaseFactory) pFactory->Release();
		return nullptr;
	}

	IWICBitmapFrameDecode* pFrame = nullptr;
	pDec->GetFrame(0, &pFrame);

	IWICFormatConverter* pConv = nullptr;
	if (pFrame && SUCCEEDED(pFactory->CreateFormatConverter(&pConv)))
	{
		pConv->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
	}

	HBITMAP hBmp = nullptr;
	if (pConv)
	{
		UINT w = 0, h = 0;
		pConv->GetSize(&w, &h);

		BITMAPINFO bi = { 0 };
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = w;
		bi.bmiHeader.biHeight = -(LONG)h; // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		void* pBits = nullptr;
		hBmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
		if (hBmp && pBits)
		{
			pConv->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(pBits));
		}
		pConv->Release();
	}

	if (pFrame) pFrame->Release();
	if (pDec) pDec->Release();
	if (bReleaseFactory) pFactory->Release();

	return hBmp;
}

static bool DrawFlyoutPng(CDC& dc, ToolType t, bool active, CRect box, IWICImagingFactory* pWic)
{
	static std::map<std::pair<ToolType, bool>, HBITMAP> s_pngCache;

	auto key = std::make_pair(t, active);
	auto it = s_pngCache.find(key);
	if (it == s_pngCache.end())
	{
		const wchar_t* fname = nullptr;
		switch (t)
		{
		case ToolType::Rect:     fname = active ? L"square-line_active.png" : L"square-line.png"; break;
		case ToolType::Triangle: fname = active ? L"triangle-line_active.png" : L"triangle-line.png"; break;
		case ToolType::Polygon:  fname = active ? L"hexagon-line_active.png" : L"hexagon-line.png"; break;
		case ToolType::Star:     fname = active ? L"star-line_active.png" : L"star-line.png"; break;
		case ToolType::Gear:     fname = active ? L"gear-line_active.png" : L"gear-line.png"; break;
		case ToolType::Spiral:   fname = active ? L"spiral-line_active.png" : L"spiral-line.png"; break;
		case ToolType::Heart:    fname = active ? L"heart-line_active.png" : L"heart-line.png"; break;
		case ToolType::ZoomIn:   fname = active ? L"zoom-in-line_active.png" : L"zoom-in-line.png"; break;
		case ToolType::ZoomOut:  fname = active ? L"zoom-out-line_active.png" : L"zoom-out-line.png"; break;
		default: break;
		}

		HBITMAP hBmp = fname ? LoadFlyoutPng(fname, pWic) : nullptr;
		s_pngCache[key] = hBmp;
		it = s_pngCache.find(key);
	}

	if (!it->second)
		return false;

	HBITMAP hBmp = it->second;
	BITMAP bm;
	GetObject(hBmp, sizeof(bm), &bm);

	CDC memDC;
	memDC.CreateCompatibleDC(&dc);
	HGDIOBJ old = memDC.SelectObject(hBmp);

	const int iconSize = 20;
	const CPoint c = box.CenterPoint();
	int dstX = c.x - iconSize / 2;
	int dstY = c.y - iconSize / 2;

	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	::AlphaBlend(dc.GetSafeHdc(), dstX, dstY, iconSize, iconSize, memDC.GetSafeHdc(), 0, 0, bm.bmWidth, bm.bmHeight, bf);

	memDC.SelectObject(old);
	return true;
}

static void DrawFlyoutGlyph(CDC& dc, ToolType t, CRect box, COLORREF color, bool active, IWICImagingFactory* pWic)
{
	// 1. 고해상도 투명 PNG 비트맵 우선 렌더링
	if (DrawFlyoutPng(dc, t, active, box, pWic))
		return;

	// 2. 비트맵 누락 시 깔끔한 벡터 지오메트리 폴백
	CPen pen(PS_SOLID, 1, color);
	CPen* pOldPen = dc.SelectObject(&pen);
	CBrush* pOldBrush = static_cast<CBrush*>(dc.SelectStockObject(NULL_BRUSH));

	const CPoint c = box.CenterPoint();
	switch (t)
	{
	case ToolType::Rect:
		dc.Rectangle(c.x - 7, c.y - 6, c.x + 8, c.y + 7);
		break;
	case ToolType::ActionGroup: // 그룹: 실선 상자 안에 겹친 두 사각형
		dc.Rectangle(c.x - 9, c.y - 8, c.x + 10, c.y + 9);
		dc.Rectangle(c.x - 6, c.y - 5, c.x + 2, c.y + 3);
		dc.Rectangle(c.x - 1, c.y - 1, c.x + 7, c.y + 7);
		break;
	case ToolType::ActionToFront:
	case ToolType::ActionForward:
	case ToolType::ActionBackward:
	case ToolType::ActionToBack: // 순서: 겹친 두 사각형 + 이동 방향 화살표 (맨 앞/맨 뒤는 화살표 끝 막대)
	{
		const bool up = (t == ToolType::ActionToFront || t == ToolType::ActionForward);
		const bool bar = (t == ToolType::ActionToFront || t == ToolType::ActionToBack);
		dc.Rectangle(c.x - 10, c.y - 8, c.x - 1, c.y + 1);
		dc.Rectangle(c.x - 5, c.y - 3, c.x + 4, c.y + 6);
		const int ax = c.x + 9;
		const int tip = c.y + (up ? -7 : 7);
		const int tail = c.y + (up ? 7 : -7);
		const int d = up ? 4 : -4;
		dc.MoveTo(ax, tail); dc.LineTo(ax, tip);
		dc.MoveTo(ax - 4, tip + d); dc.LineTo(ax, tip); dc.LineTo(ax + 5, tip + d);
		if (bar) { dc.MoveTo(ax - 4, tip - d / 2); dc.LineTo(ax + 5, tip - d / 2); }
		break;
	}
	case ToolType::ActionUngroup: // 그룹 해제: 점선 상자 안에 떨어진 두 사각형
	{
		CPen dotted(PS_DOT, 1, color);
		CPen* pPrev = dc.SelectObject(&dotted);
		dc.Rectangle(c.x - 9, c.y - 8, c.x + 10, c.y + 9);
		dc.SelectObject(pPrev);
		dc.Rectangle(c.x - 6, c.y - 6, c.x - 1, c.y - 1);
		dc.Rectangle(c.x + 2, c.y + 2, c.x + 7, c.y + 7);
		break;
	}
	case ToolType::ActionBooleanUnion:
	case ToolType::ActionBooleanSubtract:
	case ToolType::ActionBooleanIntersect:
	case ToolType::ActionBooleanExclude:
	case ToolType::ActionBooleanTrim:
	{
		// 겹친 두 원
		dc.Ellipse(c.x - 9, c.y - 6, c.x + 3, c.y + 6);
		dc.Ellipse(c.x - 3, c.y - 6, c.x + 9, c.y + 6);
		if (t == ToolType::ActionBooleanTrim)
		{
			dc.MoveTo(c.x, c.y - 7);
			dc.LineTo(c.x, c.y + 7);
		}
		break;
	}
	case ToolType::ActionClipMask:
	case ToolType::ActionReleaseMask:
	{
		dc.Rectangle(c.x - 7, c.y - 5, c.x + 5, c.y + 7);
		dc.Ellipse(c.x - 3, c.y - 7, c.x + 7, c.y + 3);
		if (t == ToolType::ActionReleaseMask)
		{
			dc.MoveTo(c.x - 8, c.y + 8);
			dc.LineTo(c.x + 8, c.y - 8);
		}
		break;
	}
	case ToolType::Triangle:
	{
		CPoint pts[3] = { CPoint(c.x, c.y - 7), CPoint(c.x + 8, c.y + 6), CPoint(c.x - 8, c.y + 6) };
		dc.Polygon(pts, 3);
		break;
	}
	case ToolType::Frame: // 프레임: 모서리 눈금이 있는 사각형
	{
		dc.Rectangle(c.x - 6, c.y - 6, c.x + 7, c.y + 7);
		dc.MoveTo(c.x - 9, c.y - 3); dc.LineTo(c.x - 6, c.y - 3);
		dc.MoveTo(c.x - 9, c.y + 3); dc.LineTo(c.x - 6, c.y + 3);
		dc.MoveTo(c.x - 3, c.y - 9); dc.LineTo(c.x - 3, c.y - 6);
		dc.MoveTo(c.x + 3, c.y - 9); dc.LineTo(c.x + 3, c.y - 6);
		break;
	}
	case ToolType::Polyline: // 열린 꺾은선
	{
		CPoint pts[4] = { CPoint(c.x - 8, c.y + 6), CPoint(c.x - 3, c.y - 6), CPoint(c.x + 3, c.y + 4), CPoint(c.x + 8, c.y - 6) };
		dc.Polyline(pts, 4);
		break;
	}
	case ToolType::Polygon:
	{
		const int r = 8;
		CPoint pts[6];
		for (int i = 0; i < 6; ++i)
		{
			double a = -3.141592653589793 / 2.0 + (i * 3.141592653589793 / 3.0);
			pts[i] = CPoint(c.x + static_cast<int>(std::round(r * cos(a))), c.y + static_cast<int>(std::round(r * sin(a))));
		}
		dc.Polygon(pts, 6);
		break;
	}
	case ToolType::Star:
	{
		const double rOut = 8.0;
		const double rIn = 3.8;
		CPoint pts[10];
		for (int i = 0; i < 10; ++i)
		{
			double a = -3.141592653589793 / 2.0 + (i * 3.141592653589793 / 5.0);
			double r = (i % 2 == 0) ? rOut : rIn;
			pts[i] = CPoint(c.x + static_cast<int>(std::round(r * cos(a))), c.y + static_cast<int>(std::round(r * sin(a))));
		}
		dc.Polygon(pts, 10);
		break;
	}
	case ToolType::Gear:
	{
		const double rOut = 8.0;
		const double rIn = 5.8;
		CPoint pts[16];
		for (int i = 0; i < 16; ++i)
		{
			double a = -3.141592653589793 / 2.0 + (i * 3.141592653589793 / 8.0);
			int mod = i % 4;
			double r = (mod == 1 || mod == 2) ? rOut : rIn;
			pts[i] = CPoint(c.x + static_cast<int>(std::round(r * cos(a))), c.y + static_cast<int>(std::round(r * sin(a))));
		}
		dc.Polygon(pts, 16);
		dc.Ellipse(c.x - 2, c.y - 2, c.x + 3, c.y + 3);
		break;
	}
	case ToolType::Spiral:
	{
		const int ptsCount = 24;
		CPoint pts[ptsCount];
		for (int i = 0; i < ptsCount; ++i)
		{
			double tVal = static_cast<double>(i) / (ptsCount - 1);
			double a = tVal * 2.5 * 3.141592653589793 * 2.0;
			double r = 1.0 + tVal * 7.0;
			pts[i] = CPoint(c.x + static_cast<int>(std::round(r * cos(a))), c.y + static_cast<int>(std::round(r * sin(a))));
		}
		dc.Polyline(pts, ptsCount);
		break;
	}
	case ToolType::Heart:
	{
		CPoint pts[12] = {
			CPoint(c.x, c.y - 2),
			CPoint(c.x + 3, c.y - 7),
			CPoint(c.x + 7, c.y - 6),
			CPoint(c.x + 8, c.y - 2),
			CPoint(c.x + 6, c.y + 2),
			CPoint(c.x, c.y + 8),
			CPoint(c.x - 6, c.y + 2),
			CPoint(c.x - 8, c.y - 2),
			CPoint(c.x - 7, c.y - 6),
			CPoint(c.x - 3, c.y - 7),
			CPoint(c.x, c.y - 2),
			CPoint(c.x, c.y - 2)
		};
		dc.Polyline(pts, 11);
		break;
	}
	default:
		break;
	}

	dc.SelectObject(pOldBrush);
	dc.SelectObject(pOldPen);
}

BOOL CToolFlyoutWnd::Open(CWnd* pOwner, CPoint screenTopLeft, const std::vector<ToolType>& items, ToolType current, IWICImagingFactory* pWic)
{
	if (GetSafeHwnd())
		DestroyWindow();

	m_items = items;
	m_current = current;
	m_hot = -1;
	m_pWic = pWic;

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

	CFont font;
	font.CreatePointFont(90, _T("Malgun Gothic"));
	CFont* pOldFont = dc.SelectObject(&font);
	dc.SetBkMode(TRANSPARENT);

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
	{
		CRect item(2, 2 + i * kItemH, 2 + kItemW, 2 + (i + 1) * kItemH);

		const bool hot = (i == m_hot);
		const bool isCur = (m_items[i] == m_current);
		if (hot)
			dc.FillSolidRect(item, RGB(225, 236, 252));
		else if (isCur)
			dc.FillSolidRect(item, RGB(242, 245, 252));

		const COLORREF color = hot ? RGB(25, 80, 195) : (isCur ? RGB(30, 85, 205) : RGB(60, 66, 80));
		CRect glyph(item.left, item.top, item.left + kItemH, item.bottom);
		DrawFlyoutGlyph(dc, m_items[i], glyph, color, hot || isCur, m_pWic);

		// 현재 대표 도구 표시 (좌측 파란색 인디케이터 바)
		if (isCur)
			dc.FillSolidRect(item.left, item.top + 6, 3, item.Height() - 12, RGB(51, 115, 217));

		CRect text(item.left + kItemH, item.top, item.right - 4, item.bottom);
		dc.SetTextColor(color);
		dc.DrawText(GetFlyoutToolName(m_items[i]), -1, text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
	}

	dc.SelectObject(pOldFont);
}
