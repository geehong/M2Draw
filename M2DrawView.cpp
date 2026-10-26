// M2DrawView.cpp: CM2DrawView 클래스의 구현
//

#include "pch.h"
#include "framework.h"
#ifndef SHARED_HANDLERS
#include "M2Draw.h"
#endif

#include "M2DrawDoc.h"
#include "M2DrawView.h"
#include "MainFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CM2DrawView

IMPLEMENT_DYNCREATE(CM2DrawView, CView)

BEGIN_MESSAGE_MAP(CM2DrawView, CView)
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CM2DrawView::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_KEYDOWN()
	ON_WM_KEYUP()
	ON_WM_SETCURSOR()
	ON_COMMAND(ID_VIEW_GRID, &CM2DrawView::OnViewGrid)
	ON_UPDATE_COMMAND_UI(ID_VIEW_GRID, &CM2DrawView::OnUpdateViewGrid)
	ON_COMMAND(ID_EDIT_CLEAR, &CM2DrawView::OnEditClear)
	ON_UPDATE_COMMAND_UI(ID_EDIT_CLEAR, &CM2DrawView::OnUpdateEditClear)
	ON_COMMAND(0x9999, &CM2DrawView::CenterOrigin)
END_MESSAGE_MAP()

// CM2DrawView 생성/소멸

CM2DrawView::CM2DrawView() noexcept
	: m_fZoom(1.0f)
	, m_ptPan(D2D1::Point2F(0.0f, 0.0f))
	, m_bIsPanning(FALSE)
	, m_ptLastMouse(0, 0)
	, m_pD2DFactory(nullptr)
	, m_pRenderTarget(nullptr)
	, m_pSolidBrush(nullptr)
{
}

CM2DrawView::~CM2DrawView()
{
	DiscardDeviceResources();
	if (m_pD2DFactory)
	{
		m_pD2DFactory->Release();
		m_pD2DFactory = nullptr;
	}
}

HRESULT CM2DrawView::CreateDeviceResources()
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

		D2D1_SIZE_U size = D2D1::SizeU(
			max(1, rc.Width()),
			max(1, rc.Height())
		);

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
				&m_pSolidBrush
			);
		}

		if (SUCCEEDED(hr))
		{
			m_renderer.Initialize(m_pRenderTarget, m_pD2DFactory);
		}
	}

	return hr;
}

void CM2DrawView::DiscardDeviceResources()
{
	m_renderer.Shutdown();

	if (m_pSolidBrush)
	{
		m_pSolidBrush->Release();
		m_pSolidBrush = nullptr;
	}
	if (m_pRenderTarget)
	{
		m_pRenderTarget->Release();
		m_pRenderTarget = nullptr;
	}
}

void CM2DrawView::OnInitialUpdate()
{
	CView::OnInitialUpdate();

	// 생성 직후에는 아직 MDI 탭 및 독 패인 배치가 완료되지 않았으므로
	// 메시지 큐를 통해 배치가 끝난 직후 1회 중앙 정렬하도록 비동기 호출
	PostMessage(WM_COMMAND, 0x9999, 0);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetLayerWnd().SetDocument(GetDocument());
	}
}

void CM2DrawView::CenterOrigin()
{
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() > 50 && rc.Height() > 50)
	{
		const float rulerT = GetApp()->GetRuler().enabled ? GetApp()->GetRuler().thickness : 0.0f;
		float availableW = static_cast<float>(rc.Width()) - rulerT;
		float availableH = static_cast<float>(rc.Height()) - rulerT;

		// 캔버스 가용 영역의 정확한 중앙에 (0, 0) 원점이 오도록 Pan 설정
		m_ptPan.x = rulerT + availableW * 0.5f;
		m_ptPan.y = rulerT + availableH * 0.5f;
		m_bCenteredOriginOnLoad = true;
		Invalidate(FALSE);
	}
}

void CM2DrawView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);

	if (m_pRenderTarget && cx > 0 && cy > 0)
	{
		m_pRenderTarget->Resize(D2D1::SizeU(cx, cy));
	}

	// 최초 안정화된 크기(100px 이상)로 창이 확장될 때 중앙 정렬 보정
	if (!m_bCenteredOriginOnLoad && cx > 200 && cy > 200)
	{
		CenterOrigin();
	}
}

BOOL CM2DrawView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CM2DrawView::ZoomAtPoint(CPoint clientPt, float factor)
{
	float oldZoom = m_fZoom;
	float newZoom = oldZoom * factor;

	if (newZoom < 0.05f) newZoom = 0.05f;
	if (newZoom > 32.0f) newZoom = 32.0f;

	if (newZoom != oldZoom)
	{
		float mx = static_cast<float>(clientPt.x);
		float my = static_cast<float>(clientPt.y);

		m_ptPan.x = mx - ((mx - m_ptPan.x) / oldZoom) * newZoom;
		m_ptPan.y = my - ((my - m_ptPan.y) / oldZoom) * newZoom;
		m_fZoom = newZoom;

		CString strLog;
		strLog.Format(_T("[캔버스 줌] %.1f%% (%.2fx) | 앵커: (%d, %d) | Pan: (%.1f, %.1f)"),
			m_fZoom * 100.0f, m_fZoom, clientPt.x, clientPt.y, m_ptPan.x, m_ptPan.y);
		LogAppEvent(strLog);

		Invalidate(FALSE);
	}
}

void CM2DrawView::ZoomAtRect(CRect marqueeClientRc)
{
	float dx = static_cast<float>(std::abs(marqueeClientRc.Width()));
	float dy = static_cast<float>(std::abs(marqueeClientRc.Height()));

	if (dx < 5.0f || dy < 5.0f) return;

	CRect clientRc;
	GetClientRect(&clientRc);
	float viewW = static_cast<float>(clientRc.Width());
	float viewH = static_cast<float>(clientRc.Height());

	if (viewW <= 10.0f || viewH <= 10.0f) return;

	float scaleX = viewW / dx;
	float scaleY = viewH / dy;
	float factor = min(scaleX, scaleY);

	float oldZoom = m_fZoom;
	float newZoom = min(32.0f, max(0.05f, oldZoom * factor));

	float minSX = static_cast<float>(min(marqueeClientRc.left, marqueeClientRc.right));
	float minSY = static_cast<float>(min(marqueeClientRc.top, marqueeClientRc.bottom));

	// 마키 영역 중심의 월드 좌표
	float centerWorldX = (minSX + dx * 0.5f - m_ptPan.x) / oldZoom;
	float centerWorldY = (minSY + dy * 0.5f - m_ptPan.y) / oldZoom;

	m_ptPan.x = (viewW * 0.5f) - (centerWorldX * newZoom);
	m_ptPan.y = (viewH * 0.5f) - (centerWorldY * newZoom);
	m_fZoom = newZoom;

	CString strLog;
	strLog.Format(_T("[영역 확대 줌] %.1f%% (%.2fx) | Pan: (%.1f, %.1f)"),
		m_fZoom * 100.0f, m_fZoom, m_ptPan.x, m_ptPan.y);
	LogAppEvent(strLog);

	Invalidate(FALSE);
}

BOOL CM2DrawView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (nFlags & MK_CONTROL)
	{
		CPoint clientPt = pt;
		ScreenToClient(&clientPt);

		float factor = (zDelta > 0) ? 1.15f : (1.0f / 1.15f);
		ZoomAtPoint(clientPt, factor);
		return TRUE;
	}

	return CView::OnMouseWheel(nFlags, zDelta, pt);
}

void CM2DrawView::OnLButtonDown(UINT nFlags, CPoint point)
{
	float worldX = (point.x - m_ptPan.x) / m_fZoom;
	float worldY = (point.y - m_ptPan.y) / m_fZoom;
	D2D1_POINT_2F rawWorld = D2D1::Point2F(worldX, worldY);

	CM2DrawDoc* pDoc = GetDocument();
	const ge::Document* pGeDoc = pDoc ? &pDoc->GetDoc() : nullptr;
	m_lastSnap = GetApp()->GetSnapManager().FindSnapPoint(rawWorld, m_fZoom, GetApp()->GetGrid(), pGeDoc);
	D2D1_POINT_2F worldPt = m_lastSnap.snapped ? m_lastSnap.pt : rawWorld;

	float unitScale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);
	const wchar_t* suffix = GetCanvasUnitSuffix(GetApp()->GetGrid().unit);

	ToolEvent e;
	e.screenPt = point;
	e.worldPt = worldPt;
	e.unitPt = D2D1::Point2F(worldPt.x / unitScale, worldPt.y / unitScale);
	e.flags = nFlags;
	e.altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	e.shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	e.ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	e.view = { m_fZoom, m_ptPan };
	e.pDoc = pDoc;
	e.pView = this;
	e.pD2DFactory = m_pD2DFactory;

	// 활성 도구에 이벤트 전달
	ITool* pTool = GetApp()->GetToolManager().GetActiveTool();
	if (pTool)
	{
		pTool->OnDown(e);
		Invalidate(FALSE);
	}

	CView::OnLButtonDown(nFlags, point);
}

void CM2DrawView::OnLButtonUp(UINT nFlags, CPoint point)
{
	float worldX = (point.x - m_ptPan.x) / m_fZoom;
	float worldY = (point.y - m_ptPan.y) / m_fZoom;
	D2D1_POINT_2F rawWorld = D2D1::Point2F(worldX, worldY);

	CM2DrawDoc* pDoc = GetDocument();
	const ge::Document* pGeDoc = pDoc ? &pDoc->GetDoc() : nullptr;
	m_lastSnap = GetApp()->GetSnapManager().FindSnapPoint(rawWorld, m_fZoom, GetApp()->GetGrid(), pGeDoc);
	D2D1_POINT_2F worldPt = m_lastSnap.snapped ? m_lastSnap.pt : rawWorld;

	float unitScale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);

	ToolEvent e;
	e.screenPt = point;
	e.worldPt = worldPt;
	e.unitPt = D2D1::Point2F(worldPt.x / unitScale, worldPt.y / unitScale);
	e.flags = nFlags;
	e.altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	e.shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	e.ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	e.view = { m_fZoom, m_ptPan };
	e.pDoc = pDoc;
	e.pView = this;
	e.pD2DFactory = m_pD2DFactory;

	ITool* pTool = GetApp()->GetToolManager().GetActiveTool();
	if (pTool)
	{
		pTool->OnUp(e);
		Invalidate(FALSE);
	}

	CView::OnLButtonUp(nFlags, point);
}

void CM2DrawView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	CM2DrawDoc* pDoc = GetDocument();
	ToolType activeType = GetApp()->GetToolManager().GetActiveType();

	// 1. 활성 도구의 OnDoubleClicked()에 더블클릭 이벤트 위임
	// 도구가 완료 및 선택 전환 신호(true)를 보낼 때만 선택 도구로 전환
	ITool* pActiveTool = GetApp()->GetToolManager().GetActiveTool();
	if (pActiveTool && activeType != ToolType::Select && activeType != ToolType::Pan)
	{
		bool switchSelect = pActiveTool->OnDoubleClicked(pDoc);
		if (switchSelect)
		{
			GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect && pDoc && !pDoc->GetShapes().empty())
			{
				pSelect->SetSelectedIndex(static_cast<int>(pDoc->GetShapes().size()) - 1, pDoc);
			}
			Invalidate(FALSE);
			return;
		}
	}

	CView::OnLButtonDblClk(nFlags, point);
}

void CM2DrawView::OnMButtonDown(UINT nFlags, CPoint point)
{
	m_bIsPanning = TRUE;
	m_ptLastMouse = point;
	SetCapture();

	CString strLog;
	strLog.Format(_T("[캔버스 이동 시작] 마우스 휠버튼 드래그: (%d, %d)"), point.x, point.y);
	LogAppEvent(strLog);

	CView::OnMButtonDown(nFlags, point);
}

void CM2DrawView::OnMButtonUp(UINT nFlags, CPoint point)
{
	if (m_bIsPanning)
	{
		m_bIsPanning = FALSE;
		ReleaseCapture();

		CString strLog;
		strLog.Format(_T("[캔버스 이동 종료] 최종 오프셋: Pan(%.1f, %.1f) px"), m_ptPan.x, m_ptPan.y);
		LogAppEvent(strLog);
	}
	CView::OnMButtonUp(nFlags, point);
}

void CM2DrawView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_bIsPanning)
	{
		float dx = static_cast<float>(point.x - m_ptLastMouse.x);
		float dy = static_cast<float>(point.y - m_ptLastMouse.y);
		m_ptLastMouse = point;

		m_ptPan.x += dx;
		m_ptPan.y += dy;
		Invalidate(FALSE);
	}

	// 상태표시줄(하단 좌측 붉은색 영역)에 마우스 포인터 좌표 실시간 표시
	float worldX = (point.x - m_ptPan.x) / m_fZoom;
	float worldY = (point.y - m_ptPan.y) / m_fZoom;
	D2D1_POINT_2F rawWorld = D2D1::Point2F(worldX, worldY);

	CM2DrawDoc* pDoc = GetDocument();
	const ge::Document* pGeDoc = pDoc ? &pDoc->GetDoc() : nullptr;
	m_lastSnap = GetApp()->GetSnapManager().FindSnapPoint(rawWorld, m_fZoom, GetApp()->GetGrid(), pGeDoc);
	D2D1_POINT_2F worldPt = m_lastSnap.snapped ? m_lastSnap.pt : rawWorld;

	float unitScale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);
	const wchar_t* suffix = GetCanvasUnitSuffix(GetApp()->GetGrid().unit);

	CString strStatus;
	if (m_lastSnap.snapped && m_lastSnap.description && m_lastSnap.description[0] != L'\0')
	{
		strStatus.Format(_T("마우스: (%d, %d) px | 월드: (%.1f, %.1f) px | 단위: (%.2f, %.2f) %s | 줌: %.0f%% | [스냅: %s]"),
			point.x, point.y, worldPt.x, worldPt.y, worldPt.x / unitScale, worldPt.y / unitScale, suffix, m_fZoom * 100.0f, m_lastSnap.description);
	}
	else
	{
		strStatus.Format(_T("마우스: (%d, %d) px | 월드: (%.1f, %.1f) px | 단위: (%.2f, %.2f) %s | 줌: %.0f%%"),
			point.x, point.y, worldPt.x, worldPt.y, worldPt.x / unitScale, worldPt.y / unitScale, suffix, m_fZoom * 100.0f);
	}
	SetAppStatusText(strStatus);

	// 활성 도구에 이동 이벤트 전달
	ToolEvent e;
	e.screenPt = point;
	e.worldPt = worldPt;
	e.unitPt = D2D1::Point2F(worldPt.x / unitScale, worldPt.y / unitScale);
	e.flags = nFlags;
	e.altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	e.shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	e.ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	e.view = { m_fZoom, m_ptPan };
	e.pDoc = pDoc;
	e.pView = this;
	e.pD2DFactory = m_pD2DFactory;

	ITool* pTool = GetApp()->GetToolManager().GetActiveTool();
	if (pTool)
	{
		pTool->OnMove(e);

		// 마우스 이동 시 실시간 오버레이 갱신 (호버 십자선, 드래그 미리보기 등)
		Invalidate(FALSE);
	}

	CView::OnMouseMove(nFlags, point);
}

BOOL CM2DrawView::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_DELETE || pMsg->wParam == VK_BACK))
	{
		CM2DrawDoc* pDoc = GetDocument();
		if (pDoc && pDoc->HasSelectedObjects())
		{
			pDoc->DeleteSelectedObjects();
			Invalidate(FALSE);
			return TRUE;
		}
	}
	return CView::PreTranslateMessage(pMsg);
}

void CM2DrawView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// 0. Delete / Backspace 키: 선택된 오브젝트 삭제
	if (nChar == VK_DELETE || nChar == VK_BACK)
	{
		CM2DrawDoc* pDoc = GetDocument();
		if (pDoc && pDoc->DeleteSelectedObjects())
		{
			Invalidate(FALSE);
			return;
		}
	}

	// 1. Space 키 누르고 있는 동안 임시 패닝(Pan) 모드 활성화
	if (nChar == VK_SPACE && (nRepCnt == 1))
	{
		GetApp()->GetToolManager().BeginTempPan();
		return;
	}

	// 2. 일러스트레이터 표준 도구 단축키 (V, H, P, L, M, E, G 등)
	if (GetApp()->GetToolManager().HandleShortcut(static_cast<wchar_t>(nChar)))
	{
		return;
	}

	// 3. 활성 도구에 키 이벤트 전달 (Enter: 패스 완료, Esc: 취소 등)
	ITool* pTool = GetApp()->GetToolManager().GetActiveTool();
	if (pTool)
	{
		pTool->OnKeyDown(nChar, GetDocument());
		Invalidate(FALSE);
	}

	CString strLog;
	strLog.Format(_T("[키보드 입력] 가상키 코드: 0x%02X"), nChar);
	LogAppEvent(strLog);

	CView::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CM2DrawView::OnEditClear()
{
	CM2DrawDoc* pDoc = GetDocument();
	if (pDoc)
	{
		pDoc->PerformSmartDelete();
		Invalidate(FALSE);
	}
}

void CM2DrawView::OnUpdateEditClear(CCmdUI* pCmdUI)
{
	CM2DrawDoc* pDoc = GetDocument();
	pCmdUI->Enable(pDoc && (pDoc->HasSelectedObjects() || pDoc->GetDoc().GetLayers().size() > 1));
}

void CM2DrawView::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// Space 키를 떼면 이전 도구로 복귀
	if (nChar == VK_SPACE)
	{
		GetApp()->GetToolManager().EndTempPan();
		return;
	}

	CView::OnKeyUp(nChar, nRepCnt, nFlags);
}

BOOL CM2DrawView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT)
	{
		ITool* pTool = GetApp()->GetToolManager().GetActiveTool();
		if (pTool)
		{
			::SetCursor(pTool->GetCursor());
			return TRUE;
		}
	}
	return CView::OnSetCursor(pWnd, nHitTest, message);
}

void CM2DrawView::OnViewGrid()
{
	GetApp()->GetGrid().enabled = !GetApp()->GetGrid().enabled;
	CString strLog;
	strLog.Format(_T("[단축키/메뉴] 그리드 토글: %s"), GetApp()->GetGrid().enabled ? _T("ON") : _T("OFF"));
	LogAppEvent(strLog);
	Invalidate(FALSE);
}

void CM2DrawView::OnUpdateViewGrid(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(GetApp()->GetGrid().enabled ? 1 : 0);
}

BOOL CM2DrawView::PreCreateWindow(CREATESTRUCT& cs)
{
	cs.style |= CS_DBLCLKS;
	return CView::PreCreateWindow(cs);
}

// CM2DrawView 그리기

void CM2DrawView::OnDraw(CDC* /*pDC*/)
{
	CM2DrawDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	if (!pDoc)
		return;

	if (FAILED(CreateDeviceResources()))
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	float width = static_cast<float>(rcClient.Width());
	float height = static_cast<float>(rcClient.Height());

	m_pRenderTarget->BeginDraw();
	m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

	m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

	// 눈금자 두께만큼 클리핑하여 그리드가 눈금자 뒤로만 그려지도록 처리
	const float rulerT = GetApp()->GetRuler().enabled ? GetApp()->GetRuler().thickness : 0.0f;
	if (rulerT > 0.0f)
	{
		m_pRenderTarget->PushAxisAlignedClip(
			D2D1::RectF(rulerT, rulerT, width, height),
			D2D1_ANTIALIAS_MODE_ALIASED
		);
	}

	GetApp()->GetGrid().Render(
		m_pRenderTarget,
		m_pSolidBrush,
		D2D1::SizeF(width, height),
		m_fZoom,
		m_ptPan
	);

	// 1. 신형 엔진 Document 씬 그래프 렌더링
	ge::Viewport view = { m_fZoom, { m_ptPan.x, m_ptPan.y } };
	m_renderer.DrawDocument(pDoc->GetDoc(), view);

	// 1-1. 기존 엔진 저장 도형 렌더링 (도구 마이그레이션 완료 전까지 병행 렌더링)
	ViewportTransform vt = { m_fZoom, m_ptPan };
	for (const auto& sp : pDoc->GetShapes())
	{
		if (sp)
		{
			sp->Draw(m_pRenderTarget, m_pSolidBrush, m_pD2DFactory, vt);
		}
	}

	// 2. 활성 도구의 오버레이(작성 중인 선, 사각형, 펜 핸들 등) 렌더링
	ITool* pActiveTool = GetApp()->GetToolManager().GetActiveTool();
	if (pActiveTool)
	{
		pActiveTool->DrawOverlay(m_pRenderTarget, m_pSolidBrush, m_fZoom, m_ptPan, pDoc);
	}

	// 3. 작업 중인 선택 도형의 에디트 포인트(변형 핸들) 상시 렌더링 (활성 도구가 SelectTool이 아닐 때도 표시)
	SelectTool* pSelectTool = GetApp()->GetToolManager().GetSelectTool();
	if (pSelectTool && pActiveTool != pSelectTool)
	{
		pSelectTool->DrawOverlay(m_pRenderTarget, m_pSolidBrush, m_fZoom, m_ptPan, pDoc);
	}

	// 4. 스냅 인디케이터(자석 원형/다이아몬드/삼각형/십자선) 렌더링
	if (m_lastSnap.snapped)
	{
		GetApp()->GetSnapManager().DrawSnapIndicator(m_pRenderTarget, m_pSolidBrush, m_lastSnap, m_fZoom, m_ptPan);
	}

	if (rulerT > 0.0f)
	{
		m_pRenderTarget->PopAxisAlignedClip();
	}

	// 최상단에 상단/좌측 눈금자(Ruler) 렌더링
	GetApp()->GetRuler().Render(
		m_pRenderTarget,
		m_pSolidBrush,
		D2D1::SizeF(width, height),
		m_fZoom,
		m_ptPan,
		GetApp()->GetGrid()
	);

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
		Invalidate(FALSE);
	}
}

// CM2DrawView 인쇄

void CM2DrawView::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

BOOL CM2DrawView::OnPreparePrinting(CPrintInfo* pInfo)
{
	return DoPreparePrinting(pInfo);
}

void CM2DrawView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
}

void CM2DrawView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
}

void CM2DrawView::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	CM2DrawDoc* pDoc = GetDocument();
	ToolType activeType = GetApp()->GetToolManager().GetActiveType();

	// 1. 그리기 도구(다각형, 펜, 사각형, 원, 직선 등) 상태에서 우클릭 시:
	// 작성 중인 도형이 있으면 완료하고, 선택 도구(Select)로 전환
	ITool* pActiveTool = GetApp()->GetToolManager().GetActiveTool();
	if (pActiveTool && activeType != ToolType::Select && activeType != ToolType::Pan)
	{
		bool switchSelect = pActiveTool->OnRightClicked(pDoc);
		if (switchSelect)
		{
			GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect && pDoc && !pDoc->GetShapes().empty())
			{
				pSelect->SetSelectedIndex(static_cast<int>(pDoc->GetShapes().size()) - 1, pDoc);
			}
			Invalidate(FALSE);
			return;
		}
	}

	// 2. 그 외(선택 도구 등)일 때만 기본 우클릭 컨텍스트 메뉴 표시
	ClientToScreen(&point);
	OnContextMenu(this, point);
}

void CM2DrawView::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}

#ifdef _DEBUG
void CM2DrawView::AssertValid() const
{
	CView::AssertValid();
}

void CM2DrawView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CM2DrawDoc* CM2DrawView::GetDocument() const
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CM2DrawDoc)));
	return (CM2DrawDoc*)m_pDocument;
}
#endif //_DEBUG
