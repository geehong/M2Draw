// PropertiesWnd.cpp: implementation of the CPropertiesWnd class
#include "pch.h"
#include "framework.h"
#include "PropertiesWnd.h"
#include "Resource.h"
#include "MainFrm.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"
#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

CPropertiesWnd::CPropertiesWnd(CD2DPropertyView::PaneMode mode) noexcept
	: m_paneMode(mode)
	, m_wndD2DProp(mode)
	, m_pActiveDoc(nullptr)
{
}

CPropertiesWnd::~CPropertiesWnd()
{
}

BEGIN_MESSAGE_MAP(CPropertiesWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_GETMINMAXINFO()
	ON_REGISTERED_MESSAGE(AFX_WM_CHANGE_ACTIVE_TAB, OnTabChanged)
	ON_WM_SETFOCUS()
END_MESSAGE_MAP()

int CPropertiesWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	// 최소 도킹 크기 설정 (너비 240px, 높이 180px)
	SetMinSize(CSize(240, 180));

	CRect rectDummy;
	rectDummy.SetRectEmpty();

	// D2D Vector GUI 속성 뷰 생성 (전용 윈도우 클래스 등록)
	CString strD2DClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), (HBRUSH)::GetStockObject(BLACK_BRUSH), nullptr);
	if (!m_wndD2DProp.Create(strD2DClass, _T("D2DPropertyView"), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, rectDummy, this, 3))
	{
		TRACE0("D2D 속성 뷰를 만들지 못했습니다.\n");
	}

	m_wndD2DProp.SetPaneMode(m_paneMode);

	AdjustLayout();
	return 0;
}

void CPropertiesWnd::AdjustLayout()
{
	if (GetSafeHwnd() == nullptr || (AfxGetMainWnd() && AfxGetMainWnd()->IsIconic()))
	{
		return;
	}

	CRect rectClient;
	GetClientRect(rectClient);

	int yContent = rectClient.top;
	int hContent = rectClient.Height();
	if (hContent < 0) hContent = 0;

	if (m_wndD2DProp.GetSafeHwnd())
	{
		m_wndD2DProp.SetPaneMode(m_paneMode);
		m_wndD2DProp.SetWindowPos(nullptr, rectClient.left, yContent, rectClient.Width(), hContent, SWP_NOACTIVATE | SWP_NOZORDER);
		m_wndD2DProp.ShowWindow(SW_SHOW);
		m_wndD2DProp.Invalidate();
	}
}

LRESULT CPropertiesWnd::OnTabChanged(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	AdjustLayout();
	return 0;
}

ge::Node* CPropertiesWnd::GetSelectedShape() const
{
	return (m_pActiveDoc && m_selectedNodeId) ? m_pActiveDoc->GetDoc().FindById(m_selectedNodeId) : nullptr;
}

void CPropertiesWnd::SetSelectedShape(ge::Node* pShape, CM2DrawDoc* pDoc)
{
	if (GetSafeHwnd() == nullptr)
		return;
	const uint64_t newId = (pDoc && pShape) ? pShape->GetId() : 0;
	// 이미 동일한 도형이 선택되어 있는 상태에서 값만 바뀐 경우: 컨트롤 전체를 다시 만들지 않고 모델에서 값만 다시 읽는다
	// (Transform 뿐 아니라 채우기 종류/그라디언트 스톱·방향 등 스타일도 도구/기즈모 조작으로 바뀔 수 있음)
	if (newId != 0 && m_selectedNodeId == newId && m_pActiveDoc == pDoc)
	{
		if (m_wndD2DProp.GetSafeHwnd())
		{
			m_wndD2DProp.SetTargetShape(pDoc, pShape);
		}
		return;
	}
	m_selectedNodeId = newId;
	m_pActiveDoc = pDoc;

	if (ge::Node* shape = GetSelectedShape())
	{
		CString strTitle;
		if (!shape->GetName().empty())
			strTitle.Format(_T("{%s} 오브젝트 설정"), shape->GetName().c_str());
		else
			strTitle.Format(_T("{%s} 오브젝트 설정"), shape->GetShapeTypeName());
		if (m_paneMode == CD2DPropertyView::MODE_SHAPE)
		{
			SetWindowText(strTitle);
		}
		if (m_wndD2DProp.GetSafeHwnd())
		{
			m_wndD2DProp.SetTargetShape(pDoc, shape);
			m_wndD2DProp.SetPaneMode(CD2DPropertyView::MODE_SHAPE);
		}
	}
	else
	{
		if (m_wndD2DProp.GetSafeHwnd())
		{
			m_wndD2DProp.SetTargetShape(nullptr, nullptr);
		}
		if (m_paneMode == CD2DPropertyView::MODE_SHAPE)
		{
			SetWindowText(_T("오브젝트 설정"));
		}
	}
}

void CPropertiesWnd::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CDockablePane::OnWindowPosChanging(lpwndpos);

	if (!(lpwndpos->flags & SWP_NOSIZE))
	{
		const int MIN_DOCK_WIDTH = 240;
		const int MAX_DOCK_WIDTH = 420;

		// 도킹되어 있거나 도킹 컨테이너 내부에 있을 때
		if (IsDocked())
		{
			// 우측에 도킹되어 있는 경우 너비가 변하면 x 좌표도 보정하여 스플리터 경계와 일치시킴
			DWORD dwAlignment = GetCurrentAlignment();
			if (dwAlignment == CBRS_ALIGN_RIGHT)
			{
				if (lpwndpos->cx > MAX_DOCK_WIDTH)
				{
					int diff = lpwndpos->cx - MAX_DOCK_WIDTH;
					lpwndpos->cx = MAX_DOCK_WIDTH;
					lpwndpos->x += diff;
				}
				else if (lpwndpos->cx < MIN_DOCK_WIDTH)
				{
					int diff = MIN_DOCK_WIDTH - lpwndpos->cx;
					lpwndpos->cx = MIN_DOCK_WIDTH;
					lpwndpos->x -= diff;
				}
			}
			else
			{
				if (lpwndpos->cx > MAX_DOCK_WIDTH)
				{
					lpwndpos->cx = MAX_DOCK_WIDTH;
				}
				else if (lpwndpos->cx < MIN_DOCK_WIDTH)
				{
					lpwndpos->cx = MIN_DOCK_WIDTH;
				}
			}
		}
	}
}

void CPropertiesWnd::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDockablePane::OnGetMinMaxInfo(lpMMI);
	lpMMI->ptMinTrackSize.x = 240; // 최소 너비 240px
	lpMMI->ptMinTrackSize.y = 180; // 최소 높이 180px
	lpMMI->ptMaxTrackSize.x = 420; // 최대 너비 420px
}

void CPropertiesWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	AdjustLayout();
}

void CPropertiesWnd::OnSetFocus(CWnd* pOldWnd)
{
	CDockablePane::OnSetFocus(pOldWnd);
	if (m_wndD2DProp.GetSafeHwnd() && m_wndD2DProp.IsWindowVisible())
	{
		m_wndD2DProp.SetFocus();
	}
}