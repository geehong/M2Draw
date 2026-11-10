#include "pch.h"
#include "ProjectTreeWnd.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"
#include "MainFrm.h"
#include "PanelStyle.h"

// M2DrawDoc.cpp 의 프로젝트 읽기 도우미 (8.8)
bool ProjectTreeSaved();
std::wstring ProjectTreeName();
std::vector<ProjectPageEntry> ProjectTreePages();
std::vector<ProjectRefEntry> ProjectTreeRefs();
std::wstring ProjectTreeSignature();

namespace {
const UINT kProjectTreeId = 33100;
const UINT kSyncDeferredMsg = WM_APP + 41;
const wchar_t* kPagesLabel = L"페이지";
const wchar_t* kRefsLabel = L"레퍼런스";
}

IMPLEMENT_DYNAMIC(CProjectTreeWnd, CDockablePane)

BEGIN_MESSAGE_MAP(CProjectTreeWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_NOTIFY(NM_DBLCLK, kProjectTreeId, &CProjectTreeWnd::OnTreeDblClk)
	ON_NOTIFY(NM_RCLICK, kProjectTreeId, &CProjectTreeWnd::OnTreeRClick)
	ON_NOTIFY(TVN_BEGINLABELEDIT, kProjectTreeId, &CProjectTreeWnd::OnBeginLabelEdit)
	ON_NOTIFY(TVN_ENDLABELEDIT, kProjectTreeId, &CProjectTreeWnd::OnEndLabelEdit)
	ON_MESSAGE(kSyncDeferredMsg, &CProjectTreeWnd::OnSyncDeferred)
END_MESSAGE_MAP()

CProjectTreeWnd::CProjectTreeWnd() noexcept {}
CProjectTreeWnd::~CProjectTreeWnd() {}

int CProjectTreeWnd::OnCreate(LPCREATESTRUCT lpcs)
{
	if (CDockablePane::OnCreate(lpcs) == -1) return -1;
	m_tree.Create(WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
		CRect(0, 0, 0, 0), this, kProjectTreeId);
	m_tree.SetBkColor(PanelStyle::kPanelBg);      // 다른 패널과 같은 다크 테마
	m_tree.SetTextColor(PanelStyle::kPanelText);
	Rebuild();
	return 0;
}

void CProjectTreeWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	if (m_tree.GetSafeHwnd()) m_tree.MoveWindow(0, 0, cx, cy);
}

BOOL CProjectTreeWnd::OnEraseBkgnd(CDC* pDC)
{
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(rc, PanelStyle::kPanelBg);
	return TRUE;
}

void CProjectTreeWnd::Sync()
{
	if (ProjectTreeSignature() != m_sig) Rebuild();
}

void CProjectTreeWnd::Rebuild()
{
	if (!m_tree.GetSafeHwnd()) return;
	m_tree.SetRedraw(FALSE);
	m_tree.DeleteAllItems();

	const std::wstring name = ProjectTreeName();
	HTREEITEM root = m_tree.InsertItem(name.empty() ? L"프로젝트 없음" : name.c_str(), TVI_ROOT, TVI_LAST);
	if (!name.empty()) {
		HTREEITEM pages = m_tree.InsertItem(kPagesLabel, root, TVI_LAST);
		for (const auto& pg : ProjectTreePages()) m_tree.InsertItem(pg.name.c_str(), pages, TVI_LAST);
		HTREEITEM refs = m_tree.InsertItem(kRefsLabel, root, TVI_LAST);
		for (const auto& r : ProjectTreeRefs()) m_tree.InsertItem(r.leaf.c_str(), refs, TVI_LAST);
		m_tree.Expand(pages, TVE_EXPAND);
		m_tree.Expand(refs, TVE_EXPAND);
	}
	m_tree.Expand(root, TVE_EXPAND);
	m_tree.SetRedraw(TRUE);
	m_tree.Invalidate(FALSE);
	m_sig = ProjectTreeSignature();
}

// 페이지를 더블클릭하면 그 페이지를 자기 탭으로 연다 (이미 열려 있으면 그 탭을 활성화)
void CProjectTreeWnd::OnTreeDblClk(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	*pResult = 0;
	HTREEITEM item = m_tree.GetSelectedItem();
	if (!item) return;
	HTREEITEM parent = m_tree.GetParentItem(item);
	if (!parent || m_tree.GetItemText(parent) != kPagesLabel) return;   // 페이지 항목만 연다
	auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (!mf) return;
	if (CM2DrawDoc* doc = mf->GetActiveM2DrawDoc())
		doc->OpenPageTab(std::wstring((LPCTSTR)m_tree.GetItemText(item)));
}

// 페이지 항목 우클릭 > 프로젝트에서 빼기 (파일은 남긴다)
void CProjectTreeWnd::OnTreeRClick(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	*pResult = 0;
	CPoint screenPt;
	GetCursorPos(&screenPt);
	CPoint treePt = screenPt;
	m_tree.ScreenToClient(&treePt);
	HTREEITEM item = m_tree.HitTest(treePt);
	if (!item) return;
	m_tree.SelectItem(item);
	HTREEITEM parent = m_tree.GetParentItem(item);
	if (!parent || m_tree.GetItemText(parent) != kPagesLabel) return;   // 페이지 항목만 뺀다
	if (!ProjectTreeSaved()) return;   // 저장 전(DefaultProject)에는 이름 바꾸기·빼기 없음

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, _T("이름 바꾸기"));
	menu.AppendMenu(MF_STRING, 2, _T("프로젝트에서 빼기"));
	const UINT cmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_RETURNCMD, screenPt.x, screenPt.y, this);
	if (cmd == 1) {
		m_tree.SetFocus();
		m_tree.EditLabel(item);
		return;
	}
	if (cmd != 2) return;

	auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (!mf) return;
	CM2DrawDoc* doc = mf->GetActiveM2DrawDoc();
	if (!doc) return;
	doc->RemoveProjectPage(std::wstring((LPCTSTR)m_tree.GetItemText(item)));
	Sync();
}

// 페이지 항목만 이름을 고칠 수 있다 (프로젝트·폴더·레퍼런스 항목은 막는다)
void CProjectTreeWnd::OnBeginLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
	auto* info = reinterpret_cast<NMTVDISPINFO*>(pNMHDR);
	HTREEITEM parent = m_tree.GetParentItem(info->item.hItem);
	*pResult = (parent && m_tree.GetItemText(parent) == kPagesLabel && ProjectTreeSaved()) ? 0 : 1;   // 1 = 편집 거부
}

// 입력한 이름으로 페이지 이름을 바꾼다. 글자 자체는 트리가 바꾸지 않고(거부), 바뀐 프로젝트 상태로 다시 채운다
void CProjectTreeWnd::OnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	auto* info = reinterpret_cast<NMTVDISPINFO*>(pNMHDR);
	if (info->item.pszText == nullptr) return;   // Esc 로 취소
	CString newName(info->item.pszText);
	newName.Trim();
	const std::wstring oldName((LPCTSTR)m_tree.GetItemText(info->item.hItem));
	if (newName.IsEmpty() || std::wstring((LPCTSTR)newName) == oldName) return;

	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
		if (CM2DrawDoc* doc = mf->GetActiveM2DrawDoc())
			doc->RenameProjectPage(oldName, std::wstring((LPCTSTR)newName));
	PostMessage(kSyncDeferredMsg);
}

LRESULT CProjectTreeWnd::OnSyncDeferred(WPARAM, LPARAM)
{
	Sync();
	return 0;
}

// 이름 편집 중 Enter = 확정, Esc = 취소. 프레임의 메시지 처리(단축키 등)가 먼저 먹지 않도록 여기서 직접 처리한다
BOOL CProjectTreeWnd::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE) &&
		m_tree.GetSafeHwnd() && m_tree.GetEditControl() != nullptr)
	{
		m_tree.SendMessage(TVM_ENDEDITLABELNOW, pMsg->wParam == VK_ESCAPE ? TRUE : FALSE, 0);
		return TRUE;
	}
	return CDockablePane::PreTranslateMessage(pMsg);
}
