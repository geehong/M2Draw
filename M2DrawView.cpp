// M2DrawView.cpp: CM2DrawView 클래스의 구현
//

#include "pch.h"
#include "framework.h"
#ifndef SHARED_HANDLERS
#include "M2Draw.h"
#endif

#include "M2DrawDoc.h"
#include "HtmlComponents/TableComponent.h"
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
	ON_EN_KILLFOCUS(1099, &CM2DrawView::OnTextEditKillFocus)
	ON_EN_CHANGE(1099, &CM2DrawView::OnTextEditChange)
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

		CM2DrawDoc* pDoc = GetDocument();
		bool isBottomLeft = (pDoc && pDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
		float docW = (pDoc ? pDoc->GetDoc().width : 1920.0f) * m_fZoom;
		float docH = (pDoc ? pDoc->GetDoc().height : 1080.0f) * m_fZoom;

		// 캔버스 페이지가 뷰포트 정중앙에 오도록 Pan 설정
		m_ptPan.x = rulerT + (availableW - docW) * 0.5f;
		m_ptPan.y = rulerT + (availableH - docH) * 0.5f;
		m_bCenteredOriginOnLoad = true;
		Invalidate(FALSE);
	}
}

D2D1_POINT_2F CM2DrawView::ScreenToWorld(D2D1_POINT_2F screenPt) const
{
	float wx = (screenPt.x - m_ptPan.x) / m_fZoom;
	float wy = (screenPt.y - m_ptPan.y) / m_fZoom;
	return D2D1::Point2F(wx, wy);
}

D2D1_POINT_2F CM2DrawView::ScreenToWorld(CPoint screenPt) const
{
	return ScreenToWorld(D2D1::Point2F(static_cast<float>(screenPt.x), static_cast<float>(screenPt.y)));
}

D2D1_POINT_2F CM2DrawView::WorldToScreen(D2D1_POINT_2F worldPt) const
{
	float sx = worldPt.x * m_fZoom + m_ptPan.x;
	float sy = worldPt.y * m_fZoom + m_ptPan.y;
	return D2D1::Point2F(sx, sy);
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
	if (IsTextInlineEditing()) UpdateTextEditCtrlLayout();
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

		if (IsTextInlineEditing()) UpdateTextEditCtrlLayout();
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

	if (IsTextInlineEditing()) UpdateTextEditCtrlLayout();
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

// ---- 그룹 격리 편집 (Step 5.3): 브레드크럼 막대 / 진입·종료 ----
void CM2DrawView::RefreshAfterIsolationChange()
{
	if (CM2DrawDoc* pDoc = GetDocument())
	{
		UpdateShapePropertiesSelection(nullptr, pDoc);
		CWnd* pMain = AfxGetMainWnd();
		if (pMain && pMain->IsKindOf(RUNTIME_CLASS(CMainFrame)))
			static_cast<CMainFrame*>(pMain)->GetLayerWnd().RefreshLayers();
	}
	Invalidate(FALSE);
}

void CM2DrawView::DrawIsolationBar(float width, float top)
{
	m_isoCrumbs.clear();
	m_isoBarRect = D2D1::RectF(0, 0, 0, 0);
	CM2DrawDoc* pDoc = GetDocument();
	if (!pDoc || !m_pRenderTarget || !m_pSolidBrush) return;
	const std::vector<ge::GroupNode*> path = pDoc->GetDoc().IsolationPath();
	if (path.empty()) return;

	static IDWriteFactory* s_dw = nullptr;
	static IDWriteTextFormat* s_fmt = nullptr;
	if (!s_dw)
	{
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s_dw));
		if (s_dw)
		{
			s_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"ko-kr", &s_fmt);
			if (s_fmt)
			{
				s_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
				s_fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			}
		}
	}
	if (!s_fmt) return;

	const float barH = 26.0f;
	m_isoBarRect = D2D1::RectF(0.0f, top, width, top + barH);
	m_pSolidBrush->SetColor(D2D1::ColorF(0.13f, 0.15f, 0.20f, 0.94f));
	m_pRenderTarget->FillRectangle(m_isoBarRect, m_pSolidBrush);

	auto measure = [&](const std::wstring& s) -> float {
		float w = static_cast<float>(s.size()) * 7.0f;
		IDWriteTextLayout* lay = nullptr;
		if (SUCCEEDED(s_dw->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), s_fmt, 2000.0f, 20.0f, &lay)) && lay)
		{
			DWRITE_TEXT_METRICS tm{};
			if (SUCCEEDED(lay->GetMetrics(&tm))) w = tm.widthIncludingTrailingWhitespace;
			lay->Release();
		}
		return w;
	};
	auto drawText = [&](const std::wstring& s, float x, float w, const D2D1_COLOR_F& c) {
		m_pSolidBrush->SetColor(c);
		m_pRenderTarget->DrawText(s.c_str(), static_cast<UINT32>(s.size()), s_fmt, D2D1::RectF(x, top, x + w + 2.0f, top + barH), m_pSolidBrush);
	};

	float x = 10.0f;
	// 맨 앞: 레이어(= 격리 종료로 이동)
	for (size_t i = 0; i < path.size(); ++i)
	{
		ge::GroupNode* g = path[i];
		std::wstring label = g->Kind() == ge::NodeKind::Layer ? (L"◂ " + (g->GetName().empty() ? std::wstring(L"Layer") : g->GetName()))
		                                                       : (g->GetName().empty() ? std::wstring(L"Group") : g->GetName());
		const float w = measure(label);
		const bool last = (i + 1 == path.size());
		const bool clickable = (g->Kind() == ge::NodeKind::Layer) || (ge::Document::CanIsolate(g) && !last);
		const D2D1_RECT_F hit = D2D1::RectF(x - 4.0f, top, x + w + 4.0f, top + barH);
		if (clickable) m_isoCrumbs.push_back({ hit, g->Kind() == ge::NodeKind::Layer ? 0 : g->GetId() });
		drawText(label, x, w, last ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.62f, 0.78f, 1.0f, 1.0f));
		x += w + 8.0f;
		if (!last)
		{
			drawText(L"›", x, 8.0f, D2D1::ColorF(0.6f, 0.62f, 0.7f, 1.0f));
			x += 16.0f;
		}
	}
	const std::wstring hint = L"Esc / 바깥 더블클릭: 격리 종료";
	const float hw = measure(hint);
	drawText(hint, (std::max)(x + 20.0f, width - hw - 12.0f), hw, D2D1::ColorF(0.65f, 0.68f, 0.75f, 1.0f));
}

bool CM2DrawView::HandleIsolationBarClick(CPoint point)
{
	CM2DrawDoc* pDoc = GetDocument();
	if (!pDoc || !pDoc->GetDoc().GetIsolationGroup()) return false;
	const D2D1_POINT_2F p = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));
	if (p.x < m_isoBarRect.left || p.x > m_isoBarRect.right || p.y < m_isoBarRect.top || p.y > m_isoBarRect.bottom) return false;
	for (const auto& c : m_isoCrumbs)
	{
		if (p.x < c.first.left || p.x > c.first.right) continue;
		auto& doc = pDoc->GetDoc();
		if (c.second == 0) doc.ClearIsolation();
		else if (auto* g = dynamic_cast<ge::GroupNode*>(doc.FindById(c.second))) doc.EnterIsolation(g);
		RefreshAfterIsolationChange();
		break;
	}
	return true;   // 막대 위 클릭은 캔버스 선택으로 전달하지 않음
}

void CM2DrawView::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (HandleIsolationBarClick(point)) return;
	if (IsTextInlineEditing())
	{
		CommitTextInlineEdit();
	}

	CM2DrawDoc* pDoc = GetDocument();
	bool isBottomLeft = (pDoc && pDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
	D2D1_POINT_2F rawWorld = ScreenToWorld(point);

	const ge::Document* pGeDoc = pDoc ? &pDoc->GetDoc() : nullptr;
	m_lastSnap = GetApp()->GetSnapManager().FindSnapPoint(rawWorld, m_fZoom, GetApp()->GetGrid(), pGeDoc);
	D2D1_POINT_2F worldPt = m_lastSnap.snapped ? m_lastSnap.pt : rawWorld;

	float unitScale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);
	const wchar_t* suffix = GetCanvasUnitSuffix(GetApp()->GetGrid().unit);

	ToolEvent e;
	e.screenPt = point;
	e.worldPt = worldPt;
	float unitY = (isBottomLeft && pDoc) ? ((pDoc->GetDoc().height - worldPt.y) / unitScale) : (worldPt.y / unitScale);
	e.unitPt = D2D1::Point2F(worldPt.x / unitScale, unitY);
	e.flags = nFlags;
	e.altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	e.shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	e.ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	e.view = { m_fZoom, { m_ptPan.x, m_ptPan.y } };
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
	CM2DrawDoc* pDoc = GetDocument();
	bool isBottomLeft = (pDoc && pDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
	D2D1_POINT_2F rawWorld = ScreenToWorld(point);

	const ge::Document* pGeDoc = pDoc ? &pDoc->GetDoc() : nullptr;
	m_lastSnap = GetApp()->GetSnapManager().FindSnapPoint(rawWorld, m_fZoom, GetApp()->GetGrid(), pGeDoc);
	D2D1_POINT_2F worldPt = m_lastSnap.snapped ? m_lastSnap.pt : rawWorld;

	float unitScale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);

	ToolEvent e;
	e.screenPt = point;
	e.worldPt = worldPt;
	float unitY = (isBottomLeft && pDoc) ? ((pDoc->GetDoc().height - worldPt.y) / unitScale) : (worldPt.y / unitScale);
	e.unitPt = D2D1::Point2F(worldPt.x / unitScale, unitY);
	e.flags = nFlags;
	e.altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	e.shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	e.ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	e.view = { m_fZoom, { m_ptPan.x, m_ptPan.y } };
	e.pDoc = pDoc;
	e.pView = this;
	e.pD2DFactory = m_pD2DFactory;

	ITool* pTool = GetApp()->GetToolManager().GetActiveTool();
	if (pTool)
	{
		pTool->OnUp(e);
		// 도형 생성/이동/변형/노드 편집 등 한 번의 마우스 조작이 끝난 시점에 변경이 있었으면 Undo 이력에 기록
		if (pDoc)
		{
			pDoc->CommitUndoPoint(pTool->GetName());
		}
		Invalidate(FALSE);
	}

	CView::OnLButtonUp(nFlags, point);
}

void CM2DrawView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	CM2DrawDoc* pDoc = GetDocument();
	ToolType activeType = GetApp()->GetToolManager().GetActiveType();

	// 0-iso. 선택 도구: 일반 그룹을 더블클릭하면 그 그룹 내부 격리 편집으로 진입, 격리 중 빈 곳을 더블클릭하면 한 단계 위로 (Step 5.3)
	if (activeType == ToolType::Select && pDoc)
	{
		auto& gdoc = pDoc->GetDoc();
		ge::Node* selN = pDoc->GetPrimarySelection();
		if (gdoc.GetSelection().size() == 1 && ge::Document::CanIsolate(selN))
		{
			gdoc.EnterIsolation(static_cast<ge::GroupNode*>(selN));
			LogAppEvent(_T("[격리 편집] 그룹 내부로 진입"));
			RefreshAfterIsolationChange();
			return;
		}
		if (gdoc.GetIsolationGroup() && gdoc.GetSelection().empty())
		{
			gdoc.ExitIsolation();
			LogAppEvent(_T("[격리 편집] 상위로 복귀"));
			RefreshAfterIsolationChange();
			return;
		}
	}

	// 0-a. 선택 도구: 선택된 텍스트를 더블클릭하면 캔버스 위에서 직접 인라인 편집
	if (activeType == ToolType::Select && pDoc && pDoc->GetDoc().GetSelection().size() == 1)
	{
		ge::Node* sel = pDoc->GetPrimarySelection();
		if (sel && sel->Kind() == ge::NodeKind::Text)
		{
			ge::Rect2 b;
			ge::Matrix2D inv = sel->GetWorldTransform();
			const D2D1_POINT_2F w = ScreenToWorld(point);
			if (sel->GetLocalBounds(b) && inv.Invert())
			{
				const ge::Vec2 l = inv.TransformPoint(ge::Vec2{ w.x, w.y });
				if (l.x >= b.left && l.x <= b.right && l.y >= b.top && l.y <= b.bottom)
				{
					auto* tn = static_cast<ge::TextNode*>(sel);
					BeginTextInlineEdit(tn);
					return;
				}
			}
		}
	}

	// 0-table. 선택 도구: 선택된 테이블의 칸을 더블클릭하면 그 칸의 내용(텍스트)을 입력/편집
	if (activeType == ToolType::Select && pDoc && pDoc->GetDoc().GetSelection().size() == 1)
	{
		if (auto* tb = dynamic_cast<ge::TableNode*>(pDoc->GetPrimarySelection()))
		{
			ge::Matrix2D inv = tb->GetWorldTransform();
			const D2D1_POINT_2F w = ScreenToWorld(point);
			int r = -1, c = -1;
			if (inv.Invert() && tb->CellAt(inv.TransformPoint(ge::Vec2{ w.x, w.y }), r, c))
			{
					const ge::Rect2 cell = tb->SpanRect(r, c);   // merged cell: whole span
					tb->SetActiveCell(r, c);
					ge::TextNode* content = nullptr;
					{
						const auto texts = ge::TableComponent::CellTexts(*tb, r, c);
						if (!texts.empty()) content = texts.back();
					}

				TextEditProps props;
				if (content)
				{
					props.text = content->GetText();
					props.family = content->GetFontFamily();
					props.size = content->GetFontSize();
					props.bold = content->IsBold();
					props.italic = content->IsItalic();
					props.align = static_cast<int>(content->GetAlign());
				}
				else
				{
					props.family = L"Segoe UI";
					props.size = 16.0f;
				}

				if (EditTextPropsDialog(this, props))
				{
					if (content)
					{
						content->SetText(props.text);
						content->SetFontFamily(props.family);
						content->SetFontSize(props.size);
						content->SetBold(props.bold);
						content->SetItalic(props.italic);
						content->SetAlign(static_cast<ge::TextAlign>((std::max)(0, (std::min)(2, props.align))));
					}
					else if (!props.text.empty())
					{
						auto tn = std::make_unique<ge::TextNode>();
						tn->SetText(props.text);
						tn->SetFontFamily(props.family);
						tn->SetFontSize(props.size);
						tn->SetBold(props.bold);
						tn->SetItalic(props.italic);
						tn->SetAlign(ge::TextAlign::Left);
						tn->SetName(L"텍스트");
						tn->SetTransform(ge::Matrix2D::Translation(cell.left + tb->GetCellPadding(), cell.top + tb->GetCellPadding()));
						pDoc->GetDoc().AssignIds(*tn);
						tb->AddChild(std::move(tn));
					}
					pDoc->CommitUndoPoint(L"테이블 칸 편집");
					LogAppEvent(_T("[테이블] 칸 내용 편집"));
					pDoc->UpdateAllViews(nullptr);
				}
				return;
			}
		}
	}

	// 0. 선택 도구: 선택된 패스 위 더블클릭 = 앵커 삭제(앵커 위) / 앵커 추가(선분 위)
	if (activeType == ToolType::Select && pDoc && pDoc->GetDoc().GetSelection().size() == 1)
	{
		auto* ps = dynamic_cast<ge::PathNode*>(pDoc->GetPrimarySelection());
		if (ps && m_pD2DFactory)
		{
			const ge::Viewport view{ m_fZoom, { m_ptPan.x, m_ptPan.y } };
			const D2D1_POINT_2F sp = D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y));
			const ge::HitResult hr = ps->HitTestScreenSpace(sp, 8.0f, view, m_pD2DFactory);
			bool edited = false;
			const wchar_t* label = nullptr;
			if (hr.part == ge::HitPart::Anchor)
			{
				// 점이 2개 미만이 되는 삭제는 막음 (선/면이 사라지지 않게)
				if (ps->GetPaths()[hr.subPath].nodes.size() > 2 && ps->RemoveNode(hr.subPath, hr.nodeIdx))
				{
					edited = true;
					label = L"앵커 삭제";
				}
			}
			else if (hr.part == ge::HitPart::Stroke)
			{
				ge::Matrix2D inv = ps->GetWorldTransform();
				if (inv.Invert())
				{
					const D2D1_POINT_2F w = ScreenToWorld(point);
					const ge::Vec2 local = inv.TransformPoint(ge::Vec2{ w.x, w.y });
					int spi = -1, seg = -1; float t = 0.0f, d2 = 0.0f;
					if (ps->FindClosestSegment(local, spi, seg, t, d2) && ps->InsertNodeOnSegment(spi, seg, t) >= 0)
					{
						edited = true;
						label = L"앵커 추가";
					}
				}
			}
			if (edited)
			{
				ps->SetShapeKind(ge::ShapeKind::Path);   // 노드를 편집한 도형은 더 이상 파라메트릭이 아님
				ps->Invalidate();
				CString str; str.Format(_T("[선택 도구] %s (더블클릭)"), label);
				LogAppEvent(str);
				pDoc->CommitUndoPoint(label);
				UpdateShapePropertiesSelection(ps, pDoc);
				pDoc->UpdateAllViews(nullptr);
				return;
			}
		}
	}

	// 1. 활성 도구의 OnDoubleClicked()에 더블클릭 이벤트 위임
	// 도구가 완료 및 선택 전환 신호(true)를 보낼 때만 선택 도구로 전환
	ITool* pActiveTool = GetApp()->GetToolManager().GetActiveTool();
	if (pActiveTool && activeType != ToolType::Select && activeType != ToolType::Pan)
	{
		bool switchSelect = pActiveTool->OnDoubleClicked(pDoc);
		if (pDoc) pDoc->CommitUndoPoint(pActiveTool->GetName());   // 더블클릭으로 마무리된 도형/패스 기록
		if (switchSelect)
		{
			// 도구 전환(OnToolChanged)이 선택을 해제하므로 방금 완료된 도형을 먼저 기억
			ge::Node* pCommitted = pDoc ? pDoc->GetPrimarySelection() : nullptr;
			GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect && pCommitted)
			{
				pSelect->SelectNode(pCommitted, pDoc);
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
	CM2DrawDoc* pDoc = GetDocument();
	bool isBottomLeft = (pDoc && pDoc->GetDoc().originMode == ge::CanvasOriginMode::BottomLeft);
	D2D1_POINT_2F rawWorld = ScreenToWorld(point);

	const ge::Document* pGeDoc = pDoc ? &pDoc->GetDoc() : nullptr;
	m_lastSnap = GetApp()->GetSnapManager().FindSnapPoint(rawWorld, m_fZoom, GetApp()->GetGrid(), pGeDoc);
	D2D1_POINT_2F worldPt = m_lastSnap.snapped ? m_lastSnap.pt : rawWorld;

	float unitScale = GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit);
	const wchar_t* suffix = GetCanvasUnitSuffix(GetApp()->GetGrid().unit);

	float dispY = (isBottomLeft && pDoc) ? (pDoc->GetDoc().height - worldPt.y) : worldPt.y;
	float dispUnitY = dispY / unitScale;

	CString strStatus;
	if (m_lastSnap.snapped && m_lastSnap.description && m_lastSnap.description[0] != L'\0')
	{
		strStatus.Format(_T("마우스: (%d, %d) px | 월드: (%.1f, %.1f) px | 단위: (%.2f, %.2f) %s | 줌: %.0f%% | [스냅: %s]"),
			point.x, point.y, worldPt.x, dispY, worldPt.x / unitScale, dispUnitY, suffix, m_fZoom * 100.0f, m_lastSnap.description);
	}
	else
	{
		strStatus.Format(_T("마우스: (%d, %d) px | 월드: (%.1f, %.1f) px | 단위: (%.2f, %.2f) %s | 줌: %.0f%%"),
			point.x, point.y, worldPt.x, dispY, worldPt.x / unitScale, dispUnitY, suffix, m_fZoom * 100.0f);
	}
	SetAppStatusText(strStatus);

	// 활성 도구에 이동 이벤트 전달
	ToolEvent e;
	e.screenPt = point;
	e.worldPt = worldPt;
	e.unitPt = D2D1::Point2F(worldPt.x / unitScale, dispUnitY);
	e.flags = nFlags;
	e.altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	e.shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	e.ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	e.view = { m_fZoom, { m_ptPan.x, m_ptPan.y } };
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
	// 인라인 텍스트 편집 중 키보드 메시지 처리: Enter = 확정(Shift+Enter는 줄바꿈), Esc = 취소
	if (IsTextInlineEditing() && m_wndTextInlineEdit.GetSafeHwnd() && pMsg->hwnd == m_wndTextInlineEdit.GetSafeHwnd())
	{
		if (pMsg->message == WM_KEYDOWN)
		{
			if (pMsg->wParam == VK_RETURN)
			{
				const bool shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
				if (!shiftDown)
				{
					CommitTextInlineEdit();
					return TRUE;
				}
			}
			else if (pMsg->wParam == VK_ESCAPE)
			{
				CancelTextInlineEdit();
				return TRUE;
			}
		}
		return CView::PreTranslateMessage(pMsg);
	}

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
	// 격리 편집 중 Esc: 한 단계 위로 (Step 5.3)
	if (nChar == VK_ESCAPE)
	{
		CM2DrawDoc* pIsoDoc = GetDocument();
		if (pIsoDoc && pIsoDoc->GetDoc().GetIsolationGroup())
		{
			pIsoDoc->GetDoc().ExitIsolation();
			LogAppEvent(_T("[격리 편집] Esc 로 상위 복귀"));
			RefreshAfterIsolationChange();
			return;
		}
	}

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

	// 0-1. Ctrl+D: 선택 오브젝트 복제 (도구 단축키보다 먼저 처리)
	if (nChar == 'D' && (::GetKeyState(VK_CONTROL) & 0x8000))
	{
		CM2DrawDoc* pDoc = GetDocument();
		if (pDoc && pDoc->DuplicateSelectedObjects())
			Invalidate(FALSE);
		return;
	}

	// 0-2. Ctrl+G: 그룹화, Ctrl+Shift+G: 그룹 해제 (G 도구 단축키보다 먼저 처리)
	if (nChar == 'G' && (::GetKeyState(VK_CONTROL) & 0x8000))
	{
		CM2DrawDoc* pDoc = GetDocument();
		if (pDoc)
		{
			const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if (shift ? pDoc->UngroupSelectedObjects() : pDoc->GroupSelectedObjects())
				Invalidate(FALSE);
		}
		return;
	}

	// 0-3. Ctrl+] 앞으로 / Ctrl+[ 뒤로 (Shift 를 함께 누르면 맨 앞으로 / 맨 뒤로)
	if ((nChar == VK_OEM_6 || nChar == VK_OEM_4) && (::GetKeyState(VK_CONTROL) & 0x8000))
	{
		CM2DrawDoc* pDoc = GetDocument();
		if (pDoc)
		{
			const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			ge::Document::ZOrder order = (nChar == VK_OEM_6)
				? (shift ? ge::Document::ZOrder::Front : ge::Document::ZOrder::Forward)
				: (shift ? ge::Document::ZOrder::Back : ge::Document::ZOrder::Backward);
			if (pDoc->ArrangeSelectedObjects(order))
				Invalidate(FALSE);
		}
		return;
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
		// Enter 로 마무리한 펜 패스 등 키 입력으로 생긴 변경 기록
		if (CM2DrawDoc* pKeyDoc = GetDocument()) pKeyDoc->CommitUndoPoint(pTool->GetName());
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

	const ge::Document& geDoc = pDoc->GetDoc();
	float docW = geDoc.width;
	float docH = geDoc.height;
	bool isBottomLeft = (geDoc.originMode == ge::CanvasOriginMode::BottomLeft);

	m_pRenderTarget->BeginDraw();
	// 0. 데스크톱 작업대(캔버스 월드) 배경 클리어 (geDoc.canvasColor)
	m_pRenderTarget->Clear(D2D1::ColorF(
		geDoc.canvasColor.r,
		geDoc.canvasColor.g,
		geDoc.canvasColor.b,
		1.0f
	));

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

	// 0.5. 도큐멘트 대지(용지 아트보드) 렌더링: (0, 0) ~ (doc.width, doc.height)
	float paperLeft   = m_ptPan.x;
	float paperRight  = m_ptPan.x + docW * m_fZoom;
	float paperTop    = m_ptPan.y;
	float paperBottom = m_ptPan.y + docH * m_fZoom;

	D2D1_RECT_F paperRc = D2D1::RectF(paperLeft, paperTop, paperRight, paperBottom);

	// 종이 그림자 (Drop Shadow - 문서가 불투명할 때만 표시)
	if (geDoc.backgroundColor.a > 0.05f)
	{
		D2D1_RECT_F shadowRc = D2D1::RectF(paperLeft + 5.0f, paperTop + 5.0f, paperRight + 5.0f, paperBottom + 5.0f);
		m_pSolidBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.16f * geDoc.backgroundColor.a));
		m_pRenderTarget->FillRectangle(shadowRc, m_pSolidBrush);
	}

	// 투명도 있는 문서일 경우: 투명 체커보드(격자 무늬) 패턴 렌더링
	if (geDoc.backgroundColor.a < 0.999f)
	{
		const float checkSize = 12.0f;
		m_pRenderTarget->PushAxisAlignedClip(paperRc, D2D1_ANTIALIAS_MODE_ALIASED);
		int c0 = static_cast<int>(floorf(paperLeft / checkSize));
		int c1 = static_cast<int>(ceilf(paperRight / checkSize));
		int r0 = static_cast<int>(floorf(paperTop / checkSize));
		int r1 = static_cast<int>(ceilf(paperBottom / checkSize));
		for (int r = r0; r < r1; ++r)
		{
			for (int c = c0; c < c1; ++c)
			{
				m_pSolidBrush->SetColor(((r + c) % 2 == 0)
					? D2D1::ColorF(0.96f, 0.96f, 0.96f, 1.0f)
					: D2D1::ColorF(0.86f, 0.86f, 0.88f, 1.0f));
				m_pRenderTarget->FillRectangle(
					D2D1::RectF(c * checkSize, r * checkSize, (c + 1) * checkSize, (r + 1) * checkSize),
					m_pSolidBrush
				);
			}
		}
		m_pRenderTarget->PopAxisAlignedClip();
	}

	// 도큐멘트 본체 (Document Paper Background)
	if (geDoc.backgroundColor.a > 0.001f)
	{
		m_pSolidBrush->SetColor(D2D1::ColorF(
			geDoc.backgroundColor.r,
			geDoc.backgroundColor.g,
			geDoc.backgroundColor.b,
			geDoc.backgroundColor.a
		));
		m_pRenderTarget->FillRectangle(paperRc, m_pSolidBrush);
	}

	// 도큐멘트 외곽 테두리선 (Document Paper Border)
	m_pSolidBrush->SetColor(D2D1::ColorF(0.52f, 0.55f, 0.62f, 0.9f));
	m_pRenderTarget->DrawRectangle(paperRc, m_pSolidBrush, 1.0f);

	float originWorldY = isBottomLeft ? docH : GetApp()->GetGrid().originWorld.y;
	GetApp()->GetGrid().Render(
		m_pRenderTarget,
		m_pSolidBrush,
		D2D1::SizeF(width, height),
		m_fZoom,
		m_ptPan,
		originWorldY
	);

	// 1. 문서 씬 그래프 렌더링 (단일 모델)
	ge::Viewport view = { m_fZoom, { m_ptPan.x, m_ptPan.y } };
	m_renderer.DrawDocument(pDoc->GetDoc(), view);

	// 격리 편집(5.3): 격리 대상 밖은 반투명 덮개로 흐리게 하고, 격리 그룹만 다시 위에 그려 또렷하게 보인다
	ge::GroupNode* isoGroup = pDoc->GetDoc().GetIsolationGroup();
	if (isoGroup)
	{
		m_pSolidBrush->SetColor(D2D1::ColorF(0.93f, 0.94f, 0.97f, 0.72f));
		m_pRenderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, width, height), m_pSolidBrush);
		const ge::Matrix2D parentWorld = isoGroup->GetParent() ? isoGroup->GetParent()->GetWorldTransform() : ge::Matrix2D::Identity();
		m_renderer.DrawNode(*isoGroup, parentWorld, view);
	}

	// 2. 활성 도구의 오버레이(작성 중인 선, 사각형, 펜 핸들 등) 렌더링
	ITool* pActiveTool = GetApp()->GetToolManager().GetActiveTool();
	if (pActiveTool)
	{
		pActiveTool->DrawOverlay(m_pRenderTarget, m_pSolidBrush, m_fZoom, m_ptPan, pDoc);
	}

	// 3. 작업 중인 선택 도형의 에디트 포인트(변형 핸들) 상시 렌더링 (활성 도구가 SelectTool이 아닐 때도 표시)
	// (그라디언트 도구는 자체 기즈모만 표시하므로 선택 핸들/기즈모가 겹쳐 그려지지 않게 제외)
	SelectTool* pSelectTool = GetApp()->GetToolManager().GetSelectTool();
	if (pSelectTool && pActiveTool != pSelectTool && !(pActiveTool && pActiveTool->GetType() == ToolType::Gradient))
	{
		pSelectTool->DrawOverlay(m_pRenderTarget, m_pSolidBrush, m_fZoom, m_ptPan, pDoc);
	}

	// 4. 스냅 인디케이터(자석 원형/다이아몬드/삼각형/십자선) 렌더링
	if (m_lastSnap.snapped)
	{
		GetApp()->GetSnapManager().DrawSnapIndicator(m_pRenderTarget, m_pSolidBrush, m_lastSnap, m_fZoom, m_ptPan);
	}

	DrawIsolationBar(width, rulerT);

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
		GetApp()->GetGrid(),
		docH,
		isBottomLeft
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
		if (pDoc) pDoc->CommitUndoPoint(pActiveTool->GetName());   // 우클릭으로 마무리된 도형/패스 기록
		if (switchSelect)
		{
			// 도구 전환(OnToolChanged)이 선택을 해제하므로 방금 완료된 도형을 먼저 기억
			ge::Node* pCommitted = pDoc ? pDoc->GetPrimarySelection() : nullptr;
			GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect && pCommitted)
			{
				pSelect->SelectNode(pCommitted, pDoc);
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

// ----------------------------------------------------------------------------
// 캔버스 위 텍스트 직접 인라인 편집 (Step 3.2.2 ~ 3.2.4)
// ----------------------------------------------------------------------------
bool CM2DrawView::BeginTextInlineEdit(ge::TextNode* pNode)
{
	if (!pNode || !GetSafeHwnd()) return false;

	if (m_pEditingTextNode && m_pEditingTextNode != pNode)
	{
		CommitTextInlineEdit();
	}

	m_pEditingTextNode = pNode;
	m_textBeforeInlineEdit = pNode->GetText();

	// EDIT 컨트롤이 없으면 동적 생성
	if (!m_wndTextInlineEdit.GetSafeHwnd())
	{
		const DWORD dwStyle = WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
		if (!m_wndTextInlineEdit.Create(dwStyle, CRect(0, 0, 10, 10), this, 1099))
		{
			m_pEditingTextNode = nullptr;
			return false;
		}
	}

	// 텍스트 노드의 글꼴 및 크기 반영
	if (m_textEditFont.GetSafeHandle())
	{
		m_textEditFont.DeleteObject();
	}
	int screenPtSize = static_cast<int>(std::round(pNode->GetFontSize() * m_fZoom));
	if (screenPtSize < 9) screenPtSize = 9;

	LOGFONT lf{};
	lf.lfHeight = -screenPtSize;
	lf.lfWeight = pNode->IsBold() ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = pNode->IsItalic() ? TRUE : FALSE;
	lf.lfCharSet = DEFAULT_CHARSET;
	_tcsncpy_s(lf.lfFaceName, LF_FACESIZE, pNode->GetFontFamily().c_str(), _TRUNCATE);
	m_textEditFont.CreateFontIndirect(&lf);
	m_wndTextInlineEdit.SetFont(&m_textEditFont);

	CString s(pNode->GetText().c_str());
	s.Replace(_T("\n"), _T("\r\n"));
	m_wndTextInlineEdit.SetWindowText(s);

	UpdateTextEditCtrlLayout();

	m_wndTextInlineEdit.ShowWindow(SW_SHOW);
	m_wndTextInlineEdit.SetFocus();
	// 전체 선택
	m_wndTextInlineEdit.SetSel(0, -1);

	Invalidate(FALSE);
	return true;
}

void CM2DrawView::UpdateTextEditCtrlLayout()
{
	if (!m_pEditingTextNode || !m_wndTextInlineEdit.GetSafeHwnd()) return;

	ge::Rect2 b{};
	m_pEditingTextNode->GetLocalBounds(b);
	const ge::Matrix2D worldXf = m_pEditingTextNode->GetWorldTransform();

	const ge::Viewport view{ m_fZoom, { m_ptPan.x, m_ptPan.y } };
	const D2D1_POINT_2F p0 = view.WorldToScreen(worldXf.TransformPoint(ge::Vec2{ b.left, b.top }));
	const D2D1_POINT_2F p1 = view.WorldToScreen(worldXf.TransformPoint(ge::Vec2{ b.right, b.bottom }));

	int left = static_cast<int>(std::floor((std::min)(p0.x, p1.x)));
	int top = static_cast<int>(std::floor((std::min)(p0.y, p1.y)));
	int width = static_cast<int>(std::ceil(std::fabs(p1.x - p0.x)));
	int height = static_cast<int>(std::ceil(std::fabs(p1.y - p0.y)));

	// 마진 및 최소 크기 확보
	const int kPadding = 4;
	if (width < 60) width = 60;
	if (height < 24) height = 24;

	CRect rcEdit(left - kPadding, top - kPadding, left + width + kPadding * 2, top + height + kPadding * 2);
	m_wndTextInlineEdit.MoveWindow(&rcEdit);
}

void CM2DrawView::CommitTextInlineEdit()
{
	if (!m_pEditingTextNode || !m_wndTextInlineEdit.GetSafeHwnd()) return;

	ge::TextNode* pNode = m_pEditingTextNode;
	m_pEditingTextNode = nullptr; // 재진입 방지

	CString s;
	m_wndTextInlineEdit.GetWindowText(s);
	s.Replace(_T("\r\n"), _T("\n"));
	const std::wstring newText = (LPCTSTR)s;

	m_wndTextInlineEdit.ShowWindow(SW_HIDE);

	CM2DrawDoc* pDoc = GetDocument();
	if (newText != m_textBeforeInlineEdit)
	{
		pNode->SetText(newText);
		ge::PathOp::MeasureText(*pNode);
		if (auto* fr = dynamic_cast<ge::FrameNode*>(pNode->GetParent()))
		{
			fr->ApplyLayout();
		}
		if (pDoc)
		{
			pDoc->SetModifiedFlag(TRUE);
			pDoc->CommitUndoPoint(L"텍스트 편집");
			UpdateShapePropertiesSelection(pNode, pDoc);
			pDoc->UpdateAllViews(nullptr);
		}
		LogAppEvent(_T("[인라인 편집] 텍스트 변경 확정"));
	}
	else
	{
		if (pDoc) pDoc->UpdateAllViews(nullptr);
	}

	SetFocus();
	Invalidate(FALSE);
}

void CM2DrawView::CancelTextInlineEdit()
{
	if (!m_pEditingTextNode || !m_wndTextInlineEdit.GetSafeHwnd()) return;

	ge::TextNode* pNode = m_pEditingTextNode;
	m_pEditingTextNode = nullptr; // 재진입 방지

	m_wndTextInlineEdit.ShowWindow(SW_HIDE);

	if (pNode->GetText() != m_textBeforeInlineEdit)
	{
		pNode->SetText(m_textBeforeInlineEdit);
		ge::PathOp::MeasureText(*pNode);
		CM2DrawDoc* pDoc = GetDocument();
		if (pDoc)
		{
			UpdateShapePropertiesSelection(pNode, pDoc);
			pDoc->UpdateAllViews(nullptr);
		}
	}

	LogAppEvent(_T("[인라인 편집] 텍스트 편집 취소"));
	SetFocus();
	Invalidate(FALSE);
}

void CM2DrawView::OnTextEditKillFocus()
{
	if (m_pEditingTextNode)
	{
		CommitTextInlineEdit();
	}
}

void CM2DrawView::OnTextEditChange()
{
	if (!m_pEditingTextNode || !m_wndTextInlineEdit.GetSafeHwnd()) return;

	CString s;
	m_wndTextInlineEdit.GetWindowText(s);
	s.Replace(_T("\r\n"), _T("\n"));
	m_pEditingTextNode->SetText((LPCTSTR)s);
	ge::PathOp::MeasureText(*m_pEditingTextNode);
	UpdateTextEditCtrlLayout();
}
