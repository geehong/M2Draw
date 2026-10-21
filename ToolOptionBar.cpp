#include "pch.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"
#include "ToolOptionBar.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ----------------------------------------------------------------------------
// CToolBarLabel 구현 (선명하고 굵은 진한 텍스트 렌더링)
// ----------------------------------------------------------------------------
IMPLEMENT_DYNCREATE(CToolBarLabel, CMFCToolBarButton)

void CToolBarLabel::OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* /*pImages*/,
	BOOL /*bHorz*/, BOOL /*bCustomizeMode*/,
	BOOL /*bHighlight*/, BOOL /*bDrawBorder*/,
	BOOL /*bGrayDisabledButtons*/)
{
	if (!pDC || m_strText.IsEmpty()) return;

	// 선명하고 또렷한 짙은 블랙 (#0f172a)
	COLORREF oldColor = pDC->SetTextColor(RGB(15, 23, 42));
	int oldBkMode = pDC->SetBkMode(TRANSPARENT);

	// 굵은 폰트(Bold) 적용
	CFont fontBold;
	CFont* pCurFont = pDC->GetCurrentFont();
	LOGFONT lf{};
	if (pCurFont)
	{
		pCurFont->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		fontBold.CreateFontIndirect(&lf);
	}

	CFont* pOldFont = fontBold.GetSafeHandle() ? pDC->SelectObject(&fontBold) : nullptr;

	CRect rcText = rect;
	rcText.DeflateRect(4, 0);
	pDC->DrawText(m_strText, rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

	if (pOldFont) pDC->SelectObject(pOldFont);
	pDC->SetTextColor(oldColor);
	pDC->SetBkMode(oldBkMode);
}

// ----------------------------------------------------------------------------
// CSpinEditCtrl 구현
// ----------------------------------------------------------------------------
IMPLEMENT_DYNAMIC(CSpinEditCtrl, CEdit)

BEGIN_MESSAGE_MAP(CSpinEditCtrl, CEdit)
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_WM_CTLCOLOR_REFLECT()
	ON_NOTIFY(UDN_DELTAPOS, 1001, &CSpinEditCtrl::OnDeltaposSpin)
	ON_CONTROL_REFLECT(EN_CHANGE, &CSpinEditCtrl::OnEnChange)
END_MESSAGE_MAP()

HBRUSH CSpinEditCtrl::CtlColor(CDC* pDC, UINT /*nCtlColor*/)
{
	pDC->SetTextColor(RGB(0, 0, 0)); // 완전 블랙
	pDC->SetBkColor(RGB(255, 255, 255));
	static CBrush s_whiteBrush(RGB(255, 255, 255));
	return s_whiteBrush;
}

CSpinEditCtrl::CSpinEditCtrl(CToolOptionBar* pBar, UINT nFieldID) noexcept
	: m_pBar(pBar), m_nFieldID(nFieldID)
{
}

CSpinEditCtrl::~CSpinEditCtrl()
{
}

void CSpinEditCtrl::InitSpin()
{
	if (m_spin.GetSafeHwnd() == nullptr && GetSafeHwnd() != nullptr)
	{
		CRect rc;
		GetClientRect(&rc);
		const int spinW = 16;
		CRect rcSpin(rc.right - spinW, 0, rc.right, rc.bottom);

		m_spin.Create(WS_CHILD | WS_VISIBLE | UDS_ARROWKEYS | UDS_HOTTRACK, rcSpin, this, 1001);
		m_spin.SetRange32(-30000, 30000);
		m_spin.SetPos32(0);
		SetMargins(0, spinW + 2);

		// 진한 SemiBold 폰트 적용
		CFont* pCurFont = GetFont();
		if (pCurFont)
		{
			LOGFONT lf{};
			pCurFont->GetLogFont(&lf);
			lf.lfWeight = FW_BOLD;
			m_fontBold.CreateFontIndirect(&lf);
			SetFont(&m_fontBold);
		}
	}
}

void CSpinEditCtrl::OnSize(UINT nType, int cx, int cy)
{
	CEdit::OnSize(nType, cx, cy);

	if (m_spin.GetSafeHwnd() && cx > 20)
	{
		const int spinW = 16;
		m_spin.MoveWindow(cx - spinW, 0, spinW, cy, TRUE);
		SetMargins(0, spinW + 2);
	}
}

void CSpinEditCtrl::OnDeltaposSpin(NMHDR* pNMHDR, LRESULT* pResult)
{
	NM_UPDOWN* pNMUpDown = reinterpret_cast<NM_UPDOWN*>(pNMHDR);
	if (!pNMUpDown)
	{
		*pResult = 0;
		return;
	}

	// Windows 기본: iDelta < 0 이면 위쪽 화살표(▲, 증가), iDelta > 0 이면 아래쪽 화살표(▼, 감소)
	double step = (pNMUpDown->iDelta < 0) ? 1.0 : -1.0;

	// Shift 키 누르면 10배 고속 증감
	if (::GetKeyState(VK_SHIFT) & 0x8000)
		step *= 10.0;
	// Ctrl 키 누르면 0.1배 정밀 증감
	else if (::GetKeyState(VK_CONTROL) & 0x8000)
		step *= 0.1;

	AdjustValue(step);

	*pResult = 1; // 스핀 컨트롤이 에디트 텍스트를 정수로 덮어쓰지 못하도록 차단
}

BOOL CSpinEditCtrl::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	double step = (zDelta > 0) ? 1.0 : -1.0;
	if (nFlags & MK_SHIFT)
		step *= 10.0;
	else if (nFlags & MK_CONTROL)
		step *= 0.1;

	AdjustValue(step);
	return TRUE;
}

void CSpinEditCtrl::AdjustValue(double step)
{
	CString strText;
	GetWindowText(strText);
	double curVal = _wtof(strText);
	double newVal = curVal + step;

	// 수치 종류에 따른 최소값 제한
	if (m_nFieldID == ID_OPT_W || m_nFieldID == ID_OPT_H)
	{
		if (newVal < 1.0) newVal = 1.0;
	}
	else if (m_nFieldID == ID_OPT_RX || m_nFieldID == ID_OPT_RY)
	{
		if (newVal < 0.0) newVal = 0.0;
	}

	CString strNew;
	strNew.Format(_T("%.1f"), newVal);
	SetWindowText(strNew);

	if (m_pBar)
	{
		m_pBar->OnOptionCommit(m_nFieldID);
	}
}

void CSpinEditCtrl::OnEnChange()
{
	if (m_pBar && !m_pBar->IsUpdating() && ::IsWindow(GetSafeHwnd()))
	{
		CString text;
		GetWindowText(text);
		text.Trim();
		if (!text.IsEmpty() && text != _T("-") && text != _T("+") && text != _T("."))
		{
			m_pBar->OnOptionCommit(m_nFieldID);
		}
	}
}

// ----------------------------------------------------------------------------
// COptionSpinEditButton 구현
// ----------------------------------------------------------------------------
IMPLEMENT_DYNCREATE(COptionSpinEditButton, CMFCToolBarEditBoxButton)

CEdit* COptionSpinEditButton::CreateEdit(CWnd* pWndParent, const CRect& rect)
{
	CToolOptionBar* pBar = DYNAMIC_DOWNCAST(CToolOptionBar, pWndParent);
	CSpinEditCtrl* pEdit = new CSpinEditCtrl(pBar, m_nID);
	if (!pEdit->Create(m_dwStyle, rect, pWndParent, m_nID))
	{
		delete pEdit;
		return nullptr;
	}
	pEdit->InitSpin();
	return pEdit;
}

IMPLEMENT_DYNAMIC(CToolOptionBar, CMFCToolBar)

BEGIN_MESSAGE_MAP(CToolOptionBar, CMFCToolBar)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CToolOptionBar::CToolOptionBar() noexcept
{
}

void CToolOptionBar::Attach()
{
	if (m_bAttached)
		return;
	m_bAttached = true;
	GetApp()->GetToolManager().AddListener(this);
	SetMode(GetApp()->GetToolManager().GetActiveType(), false);
}

void CToolOptionBar::OnDestroy()
{
	if (m_bAttached)
	{
		GetApp()->GetToolManager().RemoveListener(this);
		m_bAttached = false;
	}
	CMFCToolBar::OnDestroy();
}

void CToolOptionBar::OnToolChanged(ToolType newTool)
{
	m_selectedIndex = -1;
	SetMode(newTool, false);
	SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
	if (pSelect)
	{
		pSelect->ClearSelection();
	}
}

CString CToolOptionBar::GetModeLabel(ToolType tool, bool editing)
{
	const wchar_t* name = L"";
	switch (tool)
	{
	case ToolType::Select:   return editing ? L"Selection Edit" : L"Selection";
	case ToolType::Pen:      name = L"Path";     break;
	case ToolType::Rect:     name = L"Rect";     break;
	case ToolType::Ellipse:  name = L"Ellipse";  break;
	case ToolType::Polygon:  name = L"Polygon";  break;
	case ToolType::Triangle: name = L"Triangle"; break;
	case ToolType::Star:     name = L"Star";     break;
	case ToolType::Gear:     name = L"Gear";     break;
	case ToolType::Spiral:   name = L"Spiral";   break;
	case ToolType::Heart:    name = L"Heart";    break;
	default:                 return L"";
	}
	CString s;
	s.Format(L"%s %s", name, editing ? L"Edit" : L"Create");
	return s;
}

void CToolOptionBar::AddLabel(const CString& text)
{
	// 선명하고 굵은 진한 라벨 버튼 사용
	CToolBarLabel* pLabel = new CToolBarLabel(ID_OPT_LABEL, text);
	InsertButton(pLabel);
}

void CToolOptionBar::AddEdit(UINT id, const CString& caption, int width)
{
	AddLabel(caption);
	COptionSpinEditButton* pEdit = new COptionSpinEditButton(id, -1, ES_AUTOHSCROLL | ES_RIGHT, width);
	InsertButton(pEdit);
}

void CToolOptionBar::AddButton(UINT id, const CString& text)
{
	CMFCToolBarButton* pBtn = new CMFCToolBarButton(id, -1, text);
	pBtn->m_bText = TRUE;
	pBtn->m_bImage = FALSE;
	InsertButton(pBtn);
}

void CToolOptionBar::SetMode(ToolType tool, bool editing)
{
	if (GetSafeHwnd() == nullptr)
		return;

	m_curTool = tool;
	m_bEditing = editing;

	m_bUpdating = true;
	RemoveAllButtons();

	const CString label = GetModeLabel(tool, editing);
	if (!label.IsEmpty())
	{
		AddLabel(label);
		InsertSeparator();
	}

	// ------------------------------------------------------------------------
	// [계획 L111-L114 명세 상세 컨트롤 구성 (영문)]
	// 0. Path: [X] [Y] [Close Path / Open Path] [Done✓] [Cancel✕]
	// 1. Selection: [X] [Y] [W] [H] [Lock Ratio 🔗] [Angle°]
	// 2. Rect: [Sx] [Sy] [W] [H] [Rx] [Ry] [Rx=Ry 🔗] [Sharp Corner ⏢]
	// 3. Triangle: [Sx] [Sy] [W] [H] [Equilateral]
	// 4. Ellipse: [Cx] [Cy] [Rx] [Ry] [Circle 🔗]
	// ------------------------------------------------------------------------
	switch (tool)
	{
	case ToolType::Pen:
		if (editing)
		{
			// 패스가 만들어진 후 (수정 모드): X, Y, W, H, 비율링크, 각도, 닫힌패스/열린패스 토글
			AddEdit(ID_OPT_X, L"X");
			AddEdit(ID_OPT_Y, L"Y");
			AddEdit(ID_OPT_W, L"W");
			AddEdit(ID_OPT_H, L"H");
			AddButton(ID_OPT_LINK_RATIO, m_bLinkRatio ? L"[🔗Lock Ratio]" : L"[Free Ratio]");
			AddEdit(ID_OPT_ROT, L"Angle°", 64);
			AddButton(ID_OPT_PATH_CLOSED, m_bPathClosed ? L"[Close Path✓]" : L"[Open Path]");
		}
		else
		{
			// 패스를 그리고 있는 중 (만들기 모드): X, Y, 닫힌패스/열린패스 토글, 완료, 취소
			AddEdit(ID_OPT_X, L"X");
			AddEdit(ID_OPT_Y, L"Y");
			AddButton(ID_OPT_PATH_CLOSED, m_bPathClosed ? L"[Close Path✓]" : L"[Open Path]");
			AddButton(ID_OPT_PATH_COMPLETE, L"[Done✓]");
			AddButton(ID_OPT_PATH_CANCEL, L"[Cancel✕]");
		}
		break;

	case ToolType::Select:
		AddEdit(ID_OPT_X, L"X");
		AddEdit(ID_OPT_Y, L"Y");
		AddEdit(ID_OPT_W, L"W");
		AddEdit(ID_OPT_H, L"H");
		AddButton(ID_OPT_LINK_RATIO, m_bLinkRatio ? L"[🔗Lock Ratio]" : L"[Free Ratio]");
		AddEdit(ID_OPT_ROT, L"Angle°", 64);
		break;

	case ToolType::Rect:
		AddEdit(ID_OPT_X, L"X");
		AddEdit(ID_OPT_Y, L"Y");
		AddEdit(ID_OPT_W, L"W");
		AddEdit(ID_OPT_H, L"H");
		AddEdit(ID_OPT_RX, L"Rx", 64);
		AddEdit(ID_OPT_RY, L"Ry", 64);
		AddButton(ID_OPT_LINK_RATIO, m_bLinkRxRy ? L"[Rx=Ry 🔗]" : L"[Separate Rx,Ry]");
		AddButton(ID_OPT_RESET_CORNER, L"[⏢Sharp]");
		break;

	case ToolType::Triangle:
		AddEdit(ID_OPT_X, L"X");
		AddEdit(ID_OPT_Y, L"Y");
		AddEdit(ID_OPT_W, L"W");
		AddEdit(ID_OPT_H, L"H");
		AddButton(ID_OPT_EQUILATERAL, m_bEquilateral ? L"[Equilateral✓]" : L"[Equilateral]");
		break;

	case ToolType::Ellipse:
		AddEdit(ID_OPT_X, L"Cx");
		AddEdit(ID_OPT_Y, L"Cy");
		AddEdit(ID_OPT_RX, L"Rx");
		AddEdit(ID_OPT_RY, L"Ry");
		AddButton(ID_OPT_LINK_RATIO, m_bLinkRxRy ? L"[Circle 🔗]" : L"[Free Ellipse]");
		break;

	case ToolType::Polygon:
	case ToolType::Star:
	case ToolType::Gear:
	case ToolType::Spiral:
	case ToolType::Heart:
		AddEdit(ID_OPT_X, L"X");
		AddEdit(ID_OPT_Y, L"Y");
		AddEdit(ID_OPT_W, L"W");
		AddEdit(ID_OPT_H, L"H");
		break;

	default:
		break;
	}

	AdjustLayout();
	RecalcLayout();
	Invalidate();
	m_bUpdating = false;

	if (m_pDoc && m_selectedIndex >= 0)
	{
		SyncSelection(m_pDoc, m_selectedIndex);
	}
}

CString CToolOptionBar::GetFieldText(UINT id)
{
	for (int i = 0; i < GetCount(); ++i)
	{
		CMFCToolBarEditBoxButton* pEdit = DYNAMIC_DOWNCAST(CMFCToolBarEditBoxButton, GetButton(i));
		if (pEdit != nullptr && pEdit->m_nID == id)
		{
			CEdit* pWndEdit = pEdit->GetEditBox();
			if (pWndEdit && ::IsWindow(pWndEdit->GetSafeHwnd()))
			{
				CString str;
				pWndEdit->GetWindowText(str);
				pEdit->SetContents(str);
				return str;
			}
			return pEdit->GetContentsAll(id);
		}
	}
	return CString();
}

void CToolOptionBar::SetFieldText(UINT id, const CString& text)
{
	for (int i = 0; i < GetCount(); ++i)
	{
		CMFCToolBarEditBoxButton* pEdit = DYNAMIC_DOWNCAST(CMFCToolBarEditBoxButton, GetButton(i));
		if (pEdit != nullptr && pEdit->m_nID == id)
		{
			const bool prev = m_bUpdating;
			m_bUpdating = true;
			pEdit->SetContents(text);
			CEdit* pWndEdit = pEdit->GetEditBox();
			if (pWndEdit && ::IsWindow(pWndEdit->GetSafeHwnd()))
			{
				CString curStr;
				pWndEdit->GetWindowText(curStr);
				if (curStr != text)
				{
					pWndEdit->SetWindowText(text);
				}
			}
			m_bUpdating = prev;
			return;
		}
	}
}

void CToolOptionBar::SyncSelection(CM2DrawDoc* pDoc, int selectedIndex)
{
	m_pDoc = pDoc;
	m_selectedIndex = selectedIndex;

	if (!pDoc || selectedIndex < 0 || selectedIndex >= static_cast<int>(pDoc->GetShapes().size()))
	{
		if (m_bEditing)
		{
			SetMode(GetApp()->GetToolManager().GetActiveType(), false);
		}
		return;
	}

	Shape* pBaseShape = pDoc->GetShapes()[selectedIndex].get();
	if (!pBaseShape)
		return;

	PathShape* pPathShape = dynamic_cast<PathShape*>(pBaseShape);

	// 편집 중인 도형 종류에 맞춰 모드 전환
	ToolType shapeTool = ToolType::Select;
	if (pPathShape)
	{
		switch (pPathShape->GetKind())
		{
		case ::ShapeKind::Path:     shapeTool = ToolType::Pen;      break;
		case ::ShapeKind::Rect:     shapeTool = ToolType::Rect;     break;
		case ::ShapeKind::Ellipse:  shapeTool = ToolType::Ellipse;  break;
		case ::ShapeKind::Triangle: shapeTool = ToolType::Triangle; break;
		default:                    shapeTool = ToolType::Pen;      break;
		}
	}

	if (shapeTool == ToolType::Pen && pPathShape && !pPathShape->paths.empty())
	{
		m_bPathClosed = pPathShape->paths[0].closed;
	}

	if (m_curTool != shapeTool || !m_bEditing)
	{
		SetMode(shapeTool, true);
	}
	else if (shapeTool == ToolType::Pen)
	{
		for (int i = 0; i < GetCount(); ++i)
		{
			CMFCToolBarButton* pBtn = GetButton(i);
			if (pBtn && pBtn->m_nID == ID_OPT_PATH_CLOSED)
			{
				CString newText = m_bPathClosed ? _T("[Close Path✓]") : _T("[Open Path]");
				if (pBtn->m_strText != newText)
				{
					pBtn->m_strText = newText;
					Invalidate();
				}
				break;
			}
		}
	}

	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;

	D2D1_RECT_F lb{};
	if (pBaseShape->GetLocalBounds(nullptr, lb))
	{
		D2D1_POINT_2F origin = pBaseShape->xf.TransformPoint(D2D1::Point2F(lb.left, lb.top));
		float curW = (lb.right - lb.left) * std::sqrt(pBaseShape->xf._11 * pBaseShape->xf._11 + pBaseShape->xf._12 * pBaseShape->xf._12);
		float curH = (lb.bottom - lb.top) * std::sqrt(pBaseShape->xf._21 * pBaseShape->xf._21 + pBaseShape->xf._22 * pBaseShape->xf._22);

		// 회전 각도 계산 (-180 ~ +180)
		float rotDeg = std::atan2(pBaseShape->xf._12, pBaseShape->xf._11) * (180.0f / 3.14159265f);

		CString strVal;

		if (shapeTool == ToolType::Ellipse)
		{
			// 타원은 Cx, Cy, Rx, Ry
			float cx = origin.x + curW * 0.5f;
			float cy = origin.y + curH * 0.5f;
			float rx = curW * 0.5f;
			float ry = curH * 0.5f;

			strVal.Format(_T("%.1f"), cx / pxPerUnit); SetFieldText(ID_OPT_X, strVal);
			strVal.Format(_T("%.1f"), cy / pxPerUnit); SetFieldText(ID_OPT_Y, strVal);
			strVal.Format(_T("%.1f"), rx / pxPerUnit); SetFieldText(ID_OPT_RX, strVal);
			strVal.Format(_T("%.1f"), ry / pxPerUnit); SetFieldText(ID_OPT_RY, strVal);
		}
		else
		{
			strVal.Format(_T("%.1f"), origin.x / pxPerUnit); SetFieldText(ID_OPT_X, strVal);
			strVal.Format(_T("%.1f"), origin.y / pxPerUnit); SetFieldText(ID_OPT_Y, strVal);
			strVal.Format(_T("%.1f"), curW / pxPerUnit);     SetFieldText(ID_OPT_W, strVal);
			strVal.Format(_T("%.1f"), curH / pxPerUnit);     SetFieldText(ID_OPT_H, strVal);
			strVal.Format(_T("%.1f"), rotDeg);              SetFieldText(ID_OPT_ROT, strVal);

			if (shapeTool == ToolType::Rect && pPathShape)
			{
				const RectParams* rp = pPathShape->GetParam<RectParams>();
				float rxVal = rp ? rp->rx : 0.0f;
				float ryVal = rp ? rp->ry : 0.0f;
				strVal.Format(_T("%.1f"), rxVal / pxPerUnit); SetFieldText(ID_OPT_RX, strVal);
				strVal.Format(_T("%.1f"), ryVal / pxPerUnit); SetFieldText(ID_OPT_RY, strVal);
			}
		}
	}
}

void CToolOptionBar::SyncCreationCoordinates(float x, float y, float w, float h, bool hasFixedStart)
{
	// 편집 중인 상태(기존 도형 선택 수정 모드)가 아닐 때만 만들기 실시간 좌표 반영
	if (m_bEditing)
		return;

	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;

	CString strVal;
	if (!hasFixedStart)
	{
		// 첫 포인트 찍기 전: 마우스 위치를 X, Y에 실시간 표시 (W, H는 0.0)
		strVal.Format(_T("%.1f"), x / pxPerUnit); SetFieldText(ID_OPT_X, strVal);
		strVal.Format(_T("%.1f"), y / pxPerUnit); SetFieldText(ID_OPT_Y, strVal);
		SetFieldText(ID_OPT_W, _T("0.0"));
		SetFieldText(ID_OPT_H, _T("0.0"));

		if (m_curTool == ToolType::Ellipse)
		{
			SetFieldText(ID_OPT_RX, _T("0.0"));
			SetFieldText(ID_OPT_RY, _T("0.0"));
		}
	}
	else
	{
		// 첫 포인트 고정 후: X, Y는 고정 시작점, W, H는 실시간 드래그 크기
		strVal.Format(_T("%.1f"), x / pxPerUnit); SetFieldText(ID_OPT_X, strVal);
		strVal.Format(_T("%.1f"), y / pxPerUnit); SetFieldText(ID_OPT_Y, strVal);

		if (m_curTool == ToolType::Ellipse)
		{
			strVal.Format(_T("%.1f"), (w * 0.5f) / pxPerUnit); SetFieldText(ID_OPT_RX, strVal);
			strVal.Format(_T("%.1f"), (h * 0.5f) / pxPerUnit); SetFieldText(ID_OPT_RY, strVal);
		}
		else
		{
			strVal.Format(_T("%.1f"), w / pxPerUnit); SetFieldText(ID_OPT_W, strVal);
			strVal.Format(_T("%.1f"), h / pxPerUnit); SetFieldText(ID_OPT_H, strVal);
		}
	}
}

void CToolOptionBar::OnOptionCommit(UINT id)
{
	if (m_bUpdating)
		return;

	// 1. 토글 버튼 이벤트 처리
	if (id == ID_OPT_LINK_RATIO)
	{
		if (m_curTool == ToolType::Rect || m_curTool == ToolType::Ellipse)
		{
			m_bLinkRxRy = !m_bLinkRxRy;
			// 툴박스 생성 설정에도 반영
			BaseShapeTool* pBox = dynamic_cast<BaseShapeTool*>(GetApp()->GetToolManager().GetActiveTool());
			if (pBox)
			{
				ToolCreationSetting s = pBox->GetSetting();
				s.bLinkRxRy = m_bLinkRxRy;
				pBox->SetSetting(s);
			}
		}
		else
		{
			m_bLinkRatio = !m_bLinkRatio;
		}
		SetMode(m_curTool, m_bEditing);
		return;
	}
	else if (id == ID_OPT_RESET_CORNER)
	{
		// 모서리 리셋 (각지게 0)
		SetFieldText(ID_OPT_RX, _T("0.0"));
		SetFieldText(ID_OPT_RY, _T("0.0"));

		BaseShapeTool* pBox = dynamic_cast<BaseShapeTool*>(GetApp()->GetToolManager().GetActiveTool());
		if (pBox)
		{
			ToolCreationSetting s = pBox->GetSetting();
			s.cornerRx = s.cornerRy = 0.0f;
			pBox->SetSetting(s);
		}

		if (m_pDoc && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_pDoc->GetShapes().size()))
		{
			PathShape* pShape = dynamic_cast<PathShape*>(m_pDoc->GetShapes()[m_selectedIndex].get());
			if (pShape && pShape->GetKind() == ::ShapeKind::Rect)
			{
				RectParams* rp = pShape->GetParamRef<RectParams>();
				if (rp) { rp->rx = rp->ry = 0.0f; }

				D2D1_RECT_F lb;
				if (pShape->GetLocalBounds(nullptr, lb))
				{
					// 일반 4정점 직각 사각형으로 복원
					pShape->paths.clear();
					SubPath sp;
					sp.closed = true;
					for (int i = 0; i < 4; ++i) sp.nodes.emplace_back();
					sp.nodes[0].anchor = D2D1::Point2F(lb.left, lb.top);
					sp.nodes[1].anchor = D2D1::Point2F(lb.right, lb.top);
					sp.nodes[2].anchor = D2D1::Point2F(lb.right, lb.bottom);
					sp.nodes[3].anchor = D2D1::Point2F(lb.left, lb.bottom);
					pShape->paths.push_back(sp);
					pShape->Invalidate();
					m_pDoc->UpdateAllViews(nullptr);
				}
			}
		}
		return;
	}
	else if (id == ID_OPT_EQUILATERAL)
	{
		m_bEquilateral = !m_bEquilateral;
		SetMode(m_curTool, m_bEditing);

		if (m_bEquilateral)
		{
			// H = W * sqrt(3)/2 구속 적용
			CString strW = GetFieldText(ID_OPT_W);
			double wVal = _wtof(strW);
			if (wVal > 0.001)
			{
				double hVal = wVal * 0.86602540378; // sqrt(3)/2
				CString strH;
				strH.Format(_T("%.1f"), hVal);
				SetFieldText(ID_OPT_H, strH);
				OnOptionCommit(ID_OPT_H);
			}
		}
		return;
	}
	else if (id == ID_OPT_PATH_CLOSED)
	{
		m_bPathClosed = !m_bPathClosed;

		if (m_bEditing && m_pDoc && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_pDoc->GetShapes().size()))
		{
			PathShape* pShape = dynamic_cast<PathShape*>(m_pDoc->GetShapes()[m_selectedIndex].get());
			if (pShape && !pShape->paths.empty())
			{
				for (auto& sp : pShape->paths)
				{
					sp.closed = m_bPathClosed;
				}
				pShape->Invalidate();
				m_pDoc->UpdateAllViews(nullptr);
				LogAppEvent(m_bPathClosed ? _T("[Path Edit] Close Path (Connected)") : _T("[Path Edit] Open Path (Disconnected)"));
			}

			// 우측 오브젝트 설정 창의 Close Path 체크박스도 즉시 상호 동기화!
			SyncPropertiesWithSelection(m_selectedIndex, m_pDoc);
		}
		else if (m_pDoc && m_pDoc->GetCurrentPath())
		{
			auto& spList = m_pDoc->GetCurrentPath()->paths;
			if (!spList.empty())
			{
				spList.back().closed = m_bPathClosed;
				m_pDoc->GetCurrentPath()->Invalidate();
				m_pDoc->UpdateAllViews(nullptr);
				LogAppEvent(m_bPathClosed ? _T("[Path Tool] Close Path Mode") : _T("[Path Tool] Open Path Mode"));
			}
		}

		// 상단 옵션바 버튼 텍스트 즉시 갱신
		for (int i = 0; i < GetCount(); ++i)
		{
			CMFCToolBarButton* pBtn = GetButton(i);
			if (pBtn && pBtn->m_nID == ID_OPT_PATH_CLOSED)
			{
				pBtn->m_strText = m_bPathClosed ? _T("[Close Path✓]") : _T("[Open Path]");
				Invalidate();
				break;
			}
		}
		return;
	}
	else if (id == ID_OPT_PATH_COMPLETE)
	{
		PenTool* pPen = dynamic_cast<PenTool*>(GetApp()->GetToolManager().GetActiveTool());
		if (pPen && m_pDoc)
		{
			pPen->OnDoubleClicked(m_pDoc);
			GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect && !m_pDoc->GetShapes().empty())
			{
				pSelect->SetSelectedIndex(static_cast<int>(m_pDoc->GetShapes().size()) - 1, m_pDoc);
			}
			m_pDoc->UpdateAllViews(nullptr);
		}
		return;
	}
	else if (id == ID_OPT_PATH_CANCEL)
	{
		if (m_pDoc)
		{
			m_pDoc->GetCurrentPath() = nullptr;
			m_pDoc->UpdateAllViews(nullptr);
			LogAppEvent(_T("[Path Tool] Current Path Drawing Cancelled"));
		}
		SetFieldText(ID_OPT_X, _T("0.0"));
		SetFieldText(ID_OPT_Y, _T("0.0"));
		return;
	}

	// 2. 수치 입력 필드 커밋 처리 (단위 변환 및 도형/문서 반영)
	CString text = GetFieldText(id);
	double val = _wtof(text);

	CanvasUnit curUnit = GetApp()->GetGrid().unit;
	float pxPerUnit = GetPixelsPerCanvasUnit(curUnit);
	if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;

	// 패스 도구 작성 중 X, Y 변경 시 마지막 앵커 위치 이동
	if (m_curTool == ToolType::Pen && m_pDoc && m_pDoc->GetCurrentPath())
	{
		auto& spList = m_pDoc->GetCurrentPath()->paths;
		if (!spList.empty() && !spList.back().nodes.empty())
		{
			auto& node = spList.back().nodes.back();
			if (id == ID_OPT_X)
				node.anchor.x = static_cast<float>(val) * pxPerUnit;
			else if (id == ID_OPT_Y)
				node.anchor.y = static_cast<float>(val) * pxPerUnit;
			m_pDoc->GetCurrentPath()->Invalidate();
			m_pDoc->UpdateAllViews(nullptr);
		}
		return;
	}

	// (A) 다음 도형 만들기 모드일 때: BaseShapeTool 설정값 갱신
	if (!m_bEditing || !m_pDoc || m_selectedIndex < 0)
	{
		BaseShapeTool* pBox = dynamic_cast<BaseShapeTool*>(GetApp()->GetToolManager().GetActiveTool());
		if (pBox)
		{
			ToolCreationSetting s = pBox->GetSetting();
			if (id == ID_OPT_RX)
			{
				s.cornerRx = static_cast<float>(val) * pxPerUnit;
				if (m_bLinkRxRy) { s.cornerRy = s.cornerRx; SetFieldText(ID_OPT_RY, text); }
			}
			else if (id == ID_OPT_RY)
			{
				s.cornerRy = static_cast<float>(val) * pxPerUnit;
				if (m_bLinkRxRy) { s.cornerRx = s.cornerRy; SetFieldText(ID_OPT_RX, text); }
			}
			pBox->SetSetting(s);
		}
		return;
	}

	// (B) 선택된 도형 수정 모드일 때
	if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_pDoc->GetShapes().size()))
		return;

	Shape* pShape = m_pDoc->GetShapes()[m_selectedIndex].get();
	if (!pShape) return;

	PathShape* pPathShape = dynamic_cast<PathShape*>(pShape);

	D2D1_RECT_F lb{};
	if (!pShape->GetLocalBounds(nullptr, lb)) return;

	D2D1_POINT_2F curOrigin = pShape->xf.TransformPoint(D2D1::Point2F(lb.left, lb.top));
	float origW = lb.right - lb.left;
	float origH = lb.bottom - lb.top;
	float curScaleX = std::sqrt(pShape->xf._11 * pShape->xf._11 + pShape->xf._12 * pShape->xf._12);
	float curScaleY = std::sqrt(pShape->xf._21 * pShape->xf._21 + pShape->xf._22 * pShape->xf._22);

	if (id == ID_OPT_X)
	{
		float targetPixelX = static_cast<float>(val) * pxPerUnit;
		if (m_curTool == ToolType::Ellipse)
		{
			float curW = origW * curScaleX;
			targetPixelX -= curW * 0.5f; // Cx 입력이므로 좌측 원점으로 보정
		}
		float dx = targetPixelX - curOrigin.x;
		pShape->xf = pShape->xf * D2D1::Matrix3x2F::Translation(dx, 0.0f);
	}
	else if (id == ID_OPT_Y)
	{
		float targetPixelY = static_cast<float>(val) * pxPerUnit;
		if (m_curTool == ToolType::Ellipse)
		{
			float curH = origH * curScaleY;
			targetPixelY -= curH * 0.5f; // Cy 입력이므로 상단 원점으로 보정
		}
		float dy = targetPixelY - curOrigin.y;
		pShape->xf = pShape->xf * D2D1::Matrix3x2F::Translation(0.0f, dy);
	}
	else if (id == ID_OPT_W || (m_curTool == ToolType::Ellipse && id == ID_OPT_RX))
	{
		float targetPixelW = static_cast<float>(val) * pxPerUnit;
		if (m_curTool == ToolType::Ellipse && id == ID_OPT_RX)
			targetPixelW *= 2.0f; // Rx 입력 시 전체 폭 W = 2 * Rx

		targetPixelW = max(1.0f, targetPixelW);
		if (origW > 0.001f && curScaleX > 0.0001f)
		{
			float factor = (targetPixelW / (origW * curScaleX));
			D2D1_POINT_2F origin = pShape->xf.TransformPoint(D2D1::Point2F(lb.left, lb.top));

			if (((m_curTool == ToolType::Select || m_curTool == ToolType::Pen) && m_bLinkRatio) ||
				(m_curTool == ToolType::Ellipse && m_bLinkRxRy))
			{
				pShape->xf = pShape->xf * D2D1::Matrix3x2F::Scale(factor, factor, origin);
			}
			else if (m_curTool == ToolType::Triangle && m_bEquilateral)
			{
				float targetH = targetPixelW * 0.86602540378f;
				float factorY = (origH * curScaleY > 0.0001f) ? (targetH / (origH * curScaleY)) : factor;
				pShape->xf = pShape->xf * D2D1::Matrix3x2F::Scale(factor, factorY, origin);
			}
			else
			{
				pShape->xf = pShape->xf * D2D1::Matrix3x2F::Scale(factor, 1.0f, origin);
			}
		}
	}
	else if (id == ID_OPT_H || (m_curTool == ToolType::Ellipse && id == ID_OPT_RY))
	{
		float targetPixelH = static_cast<float>(val) * pxPerUnit;
		if (m_curTool == ToolType::Ellipse && id == ID_OPT_RY)
			targetPixelH *= 2.0f; // Ry 입력 시 전체 높이 H = 2 * Ry

		targetPixelH = max(1.0f, targetPixelH);
		if (origH > 0.001f && curScaleY > 0.0001f)
		{
			float factor = (targetPixelH / (origH * curScaleY));
			D2D1_POINT_2F origin = pShape->xf.TransformPoint(D2D1::Point2F(lb.left, lb.top));

			if (((m_curTool == ToolType::Select || m_curTool == ToolType::Pen) && m_bLinkRatio) ||
				(m_curTool == ToolType::Ellipse && m_bLinkRxRy))
			{
				pShape->xf = pShape->xf * D2D1::Matrix3x2F::Scale(factor, factor, origin);
			}
			else
			{
				pShape->xf = pShape->xf * D2D1::Matrix3x2F::Scale(1.0f, factor, origin);
			}
		}
	}
	else if (id == ID_OPT_ROT)
	{
		// 중심 기준 절대 회전각 설정
		D2D1_POINT_2F centerLocal = D2D1::Point2F((lb.left + lb.right) * 0.5f, (lb.top + lb.bottom) * 0.5f);
		D2D1_POINT_2F centerWorld = pShape->xf.TransformPoint(centerLocal);

		float curAngleDeg = std::atan2(pShape->xf._12, pShape->xf._11) * (180.0f / 3.14159265f);
		float targetDeg = static_cast<float>(val);
		float deltaDeg = targetDeg - curAngleDeg;

		pShape->xf = pShape->xf * D2D1::Matrix3x2F::Rotation(deltaDeg, centerWorld);
	}
	else if ((id == ID_OPT_RX || id == ID_OPT_RY) && pPathShape && pPathShape->GetKind() == ::ShapeKind::Rect)
	{
		// 사각형 모서리 둥글기 실시간 재빌드
		RectParams* rp = pPathShape->GetParamRef<RectParams>();
		if (rp)
		{
			float newR = static_cast<float>(val) * pxPerUnit;
			if (id == ID_OPT_RX)
			{
				rp->rx = newR;
				if (m_bLinkRxRy) { rp->ry = newR; SetFieldText(ID_OPT_RY, text); }
			}
			else
			{
				rp->ry = newR;
				if (m_bLinkRxRy) { rp->rx = newR; SetFieldText(ID_OPT_RX, text); }
			}

			// 변 길이 절반으로 Clamp
			float maxR = (std::min)(origW, origH) * 0.5f;
			rp->rx = (std::min)(maxR, (std::max)(0.0f, rp->rx));
			rp->ry = (std::min)(maxR, (std::max)(0.0f, rp->ry));

			// 둥근 사각형 베이지어 노드 재생성
			pPathShape->paths.clear();
			SubPath sp;
			sp.closed = true;

			if (rp->rx > 0.001f || rp->ry > 0.001f)
			{
				for (int i = 0; i < 8; ++i) {
					BezierNode bn; bn.type = BezierNode::Type::Corner;
					sp.nodes.push_back(bn);
				}
				const float kx = rp->rx * 0.55228475f;
				const float ky = rp->ry * 0.55228475f;
				float l = lb.left, r = lb.right, t = lb.top, b = lb.bottom;

				sp.nodes[0].anchor = D2D1::Point2F(l + rp->rx, t);
				sp.nodes[0].hIn = D2D1::Point2F(-kx, 0.0f); sp.nodes[0].hOut = D2D1::Point2F(0, 0);

				sp.nodes[1].anchor = D2D1::Point2F(r - rp->rx, t);
				sp.nodes[1].hIn = D2D1::Point2F(0, 0); sp.nodes[1].hOut = D2D1::Point2F(kx, 0.0f);

				sp.nodes[2].anchor = D2D1::Point2F(r, t + rp->ry);
				sp.nodes[2].hIn = D2D1::Point2F(0.0f, -ky); sp.nodes[2].hOut = D2D1::Point2F(0, 0);

				sp.nodes[3].anchor = D2D1::Point2F(r, b - rp->ry);
				sp.nodes[3].hIn = D2D1::Point2F(0, 0); sp.nodes[3].hOut = D2D1::Point2F(0.0f, ky);

				sp.nodes[4].anchor = D2D1::Point2F(r - rp->rx, b);
				sp.nodes[4].hIn = D2D1::Point2F(kx, 0.0f); sp.nodes[4].hOut = D2D1::Point2F(0, 0);

				sp.nodes[5].anchor = D2D1::Point2F(l + rp->rx, b);
				sp.nodes[5].hIn = D2D1::Point2F(0, 0); sp.nodes[5].hOut = D2D1::Point2F(-kx, 0.0f);

				sp.nodes[6].anchor = D2D1::Point2F(l, b - rp->ry);
				sp.nodes[6].hIn = D2D1::Point2F(0.0f, ky); sp.nodes[6].hOut = D2D1::Point2F(0, 0);

				sp.nodes[7].anchor = D2D1::Point2F(l, t + rp->ry);
				sp.nodes[7].hIn = D2D1::Point2F(0, 0); sp.nodes[7].hOut = D2D1::Point2F(0.0f, -ky);
			}
			else
			{
				for (int i = 0; i < 4; ++i) sp.nodes.emplace_back();
				sp.nodes[0].anchor = D2D1::Point2F(lb.left, lb.top);
				sp.nodes[1].anchor = D2D1::Point2F(lb.right, lb.top);
				sp.nodes[2].anchor = D2D1::Point2F(lb.right, lb.bottom);
				sp.nodes[3].anchor = D2D1::Point2F(lb.left, lb.bottom);
			}
			pPathShape->paths.push_back(sp);
		}
	}

	pShape->Invalidate();
	m_pDoc->UpdateAllViews(nullptr);
	SyncSelection(m_pDoc, m_selectedIndex);
}
