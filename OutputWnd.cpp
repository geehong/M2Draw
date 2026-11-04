#include "pch.h"
#include "framework.h"

#include "OutputWnd.h"
#include "Resource.h"
#include "MainFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// COutputBar

COutputWnd::COutputWnd() noexcept
{
}

COutputWnd::~COutputWnd()
{
}

BEGIN_MESSAGE_MAP(COutputWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
END_MESSAGE_MAP()

int COutputWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rectDummy;
	rectDummy.SetRectEmpty();

	// 탭 창을 만듭니다.
	if (!m_wndTabs.Create(CMFCTabCtrl::STYLE_FLAT, rectDummy, this, 1))
	{
		TRACE0("출력 탭 창을 만들지 못했습니다.\n");
		return -1;
	}

	// 탭 안의 창은 WS_VISIBLE 없이 만든다. 보이는 탭은 탭 컨트롤이 정한다 (다른 탭 내용이 겹쳐 보이지 않도록)
	const DWORD dwStyle = LBS_NOINTEGRALHEIGHT | WS_CHILD | WS_HSCROLL | WS_VSCROLL;

	if (!m_wndLog.Create(dwStyle, rectDummy, &m_wndTabs, 2))
	{
		TRACE0("로그 창을 만들지 못했습니다.\n");
		return -1;
	}

	// ChatAgent 탭 (에이전트와 대화)
	if (!m_wndChat.Create(nullptr, _T(""), WS_CHILD, rectDummy, &m_wndTabs, 5))
	{
		TRACE0("ChatAgent 창을 만들지 못했습니다.\n");
		return -1;
	}

	// Code 탭 (읽기 전용 코드 보기)
	if (!m_wndCode.Create(nullptr, _T(""), WS_CHILD, rectDummy, &m_wndTabs, 6))
	{
		TRACE0("Code 창을 만들지 못했습니다.\n");
		return -1;
	}

	UpdateFonts();

	// 탭 순서: ChatAgent, Code, Log
	m_wndTabs.AddTab(&m_wndChat, _T("ChatAgent"), (UINT)0);
	m_wndTabs.AddTab(&m_wndCode, _T("Code"), (UINT)1);
	m_wndTabs.AddTab(&m_wndLog, _T("출력 및 명령"), (UINT)2);
	m_wndTabs.SetActiveTab(0);

	m_wndLog.AddString(_T("[System] Event log initialized."));

	return 0;
}

void COutputWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	m_wndTabs.SetWindowPos(nullptr, -1, -1, cx, cy, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void COutputWnd::AdjustHorzScroll(CListBox& wndListBox)
{
	CClientDC dc(this);
	CFont* pOldFont = dc.SelectObject(&afxGlobalData.fontRegular);

	int cxExtentMax = 0;
	for (int i = 0; i < wndListBox.GetCount(); i++)
	{
		CString strItem;
		wndListBox.GetText(i, strItem);
		cxExtentMax = max(cxExtentMax, (int)dc.GetTextExtent(strItem).cx);
	}

	wndListBox.SetHorizontalExtent(cxExtentMax + 10);
	dc.SelectObject(pOldFont);
}

void COutputWnd::LogMessage(const CString& strMessage, int /*tabIndex*/)
{
	// 로그는 Log 탭 하나에 모은다 (빌드/디버그/찾기 탭은 제거됨)
	if (m_wndLog.GetSafeHwnd())
	{
		// 현재 시각 타임스탬프 붙이기
		CTime timeNow = CTime::GetCurrentTime();
		CString strFormatted;
		strFormatted.Format(_T("[%02d:%02d:%02d] %s"),
			timeNow.GetHour(), timeNow.GetMinute(), timeNow.GetSecond(),
			(LPCTSTR)strMessage);

		int idx = m_wndLog.AddString(strFormatted);
		if (idx != LB_ERR)
		{
			m_wndLog.SetCurSel(idx);
			m_wndLog.SetTopIndex(idx);
		}

		AdjustHorzScroll(m_wndLog);
	}
}

void COutputWnd::ClearLog(int /*tabIndex*/)
{
	if (m_wndLog.GetSafeHwnd())
		m_wndLog.ResetContent();
}

void COutputWnd::UpdateFonts()
{
	m_wndLog.SetFont(&afxGlobalData.fontRegular);
}

/////////////////////////////////////////////////////////////////////////////
// COutputList

COutputList::COutputList() noexcept
{
}

COutputList::~COutputList()
{
}

BEGIN_MESSAGE_MAP(COutputList, CListBox)
	ON_WM_CONTEXTMENU()
	ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
	ON_COMMAND(ID_EDIT_CLEAR, OnEditClear)
	ON_COMMAND(ID_VIEW_OUTPUTWND, OnViewOutput)
	ON_WM_WINDOWPOSCHANGING()
END_MESSAGE_MAP()

void COutputList::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CMenu menu;
	menu.LoadMenu(IDR_OUTPUT_POPUP);

	CMenu* pSumMenu = menu.GetSubMenu(0);

	if (AfxGetMainWnd()->IsKindOf(RUNTIME_CLASS(CMDIFrameWndEx)))
	{
		CMFCPopupMenu* pPopupMenu = new CMFCPopupMenu;
		if (!pPopupMenu->Create(this, point.x, point.y, (HMENU)pSumMenu->m_hMenu, FALSE, TRUE))
			return;

		((CMDIFrameWndEx*)AfxGetMainWnd())->OnShowPopupMenu(pPopupMenu);
		UpdateDialogControls(this, FALSE);
	}

	SetFocus();
}

void COutputList::OnEditCopy()
{
	int sel = GetCurSel();
	if (sel != LB_ERR)
	{
		CString strText;
		GetText(sel, strText);
		if (OpenClipboard())
		{
			EmptyClipboard();
			size_t size = (strText.GetLength() + 1) * sizeof(TCHAR);
			HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
			if (hMem)
			{
				memcpy(GlobalLock(hMem), (LPCTSTR)strText, size);
				GlobalUnlock(hMem);
				SetClipboardData(CF_UNICODETEXT, hMem);
			}
			CloseClipboard();
		}
	}
}

void COutputList::OnEditClear()
{
	ResetContent();
}

void COutputList::OnViewOutput()
{
	CDockablePane* pParentBar = DYNAMIC_DOWNCAST(CDockablePane, GetOwner());
	CMDIFrameWndEx* pMainFrame = DYNAMIC_DOWNCAST(CMDIFrameWndEx, GetTopLevelFrame());

	if (pMainFrame != nullptr && pParentBar != nullptr)
	{
		pMainFrame->SetFocus();
		pMainFrame->ShowPane(pParentBar, FALSE, FALSE, FALSE);
		pMainFrame->RecalcLayout();
	}
}
