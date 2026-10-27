
// M2DrawDoc.cpp: CM2DrawDoc 클래스의 구현
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS는 미리 보기, 축소판 그림 및 검색 필터 처리기를 구현하는 ATL 프로젝트에서 정의할 수 있으며
// 해당 프로젝트와 문서 코드를 공유하도록 해 줍니다.
#ifndef SHARED_HANDLERS
#include "M2Draw.h"
#endif

#include "M2DrawDoc.h"
#include "MainFrm.h"
#include "ITool.h"
#include "SvgWriter.h"
#include "SvgReader.h"
#include <algorithm>
#include <vector>

#include <propkey.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CM2DrawDoc

IMPLEMENT_DYNCREATE(CM2DrawDoc, CDocument)

BEGIN_MESSAGE_MAP(CM2DrawDoc, CDocument)
	ON_COMMAND(ID_EDIT_UNDO, &CM2DrawDoc::OnEditUndo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, &CM2DrawDoc::OnUpdateEditUndo)
	ON_COMMAND(ID_EDIT_REDO, &CM2DrawDoc::OnEditRedo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, &CM2DrawDoc::OnUpdateEditRedo)
END_MESSAGE_MAP()


// CM2DrawDoc 생성/소멸

CM2DrawDoc::CM2DrawDoc() noexcept
{
	// TODO: 여기에 일회성 생성 코드를 추가합니다.

}

CM2DrawDoc::~CM2DrawDoc()
{
}

BOOL CM2DrawDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	m_currentPath.reset();
	m_doc = ge::Document();
	m_currentPathNode.reset();
	m_undoMgr.Reset(m_doc);
	ResetToolInteraction();
	SetModifiedFlag(FALSE);

	return TRUE;
}

ge::Node* CM2DrawDoc::GetPrimarySelection() const
{
	const auto& sel = m_doc.GetSelection();
	return sel.empty() ? nullptr : sel.front();
}

bool CM2DrawDoc::PruneNonInteractiveSelection()
{
	const auto& sel = m_doc.GetSelection();
	std::vector<ge::Node*> kept;
	for (ge::Node* n : sel)
	{
		if (n && n->IsEffectivelyVisible() && !n->IsEffectivelyLocked())
			kept.push_back(n);
	}
	if (kept.size() == sel.size())
		return false;
	m_doc.SetSelection(kept);
	return true;
}




// CM2DrawDoc serialization (Phase 1: SVG 포맷 기반 역직렬화/직렬화)

void CM2DrawDoc::Serialize(CArchive& /*ar*/)
{
	// M2Draw 는 표준 SVG / M2Draw 확장 SVG 기반으로 파일 저장 및 열기를 처리하므로
	// 기본 CArchive 직렬화 대신 OnSaveDocument / OnOpenDocument 를 직접 사용합니다.
}

BOOL CM2DrawDoc::OnSaveDocument(LPCTSTR lpszPathName)
{
	if (!lpszPathName || lpszPathName[0] == _T('\0'))
		return FALSE;

	ge::SvgWriter writer;
	if (!writer.WriteToFile(m_doc, lpszPathName))
	{
		AfxMessageBox(_T("SVG 파일을 저장하는 중 오류가 발생했습니다."), MB_ICONERROR);
		return FALSE;
	}

	SetModifiedFlag(FALSE);

	CString strLog;
	strLog.Format(_T("[파일 저장 완료] %s"), lpszPathName);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	return TRUE;
}

BOOL CM2DrawDoc::OnOpenDocument(LPCTSTR lpszPathName)
{
	if (!lpszPathName || lpszPathName[0] == _T('\0'))
		return FALSE;

	// 임시 문서로 먼저 읽어 실패 시 현재 문서를 그대로 유지 (깨진 파일이 문서를 망가뜨리지 않도록)
	ge::Document loaded;
	ge::SvgReader reader;
	if (!reader.ReadFromFile(lpszPathName, loaded))
	{
		AfxMessageBox(_T("SVG 파일을 열거나 파싱하는 중 오류가 발생했습니다."), MB_ICONERROR);
		return FALSE;
	}

	m_currentPath.reset();
	m_currentPathNode.reset();
	m_doc = std::move(loaded);
	ResetToolInteraction();

	SetPathName(lpszPathName);
	SetModifiedFlag(FALSE);
	m_undoMgr.Reset(m_doc);

	UpdateAllViews(nullptr);
	UpdateShapePropertiesSelection(nullptr, this);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}

	CString strLog;
	strLog.Format(_T("[파일 열기 완료] %s"), lpszPathName);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	return TRUE;
}

BOOL CM2DrawDoc::DoSave(LPCTSTR lpszPathName, BOOL bReplace)
{
	CString newName = lpszPathName;
	if (newName.IsEmpty())
	{
		CString filter = _T("SVG 파일 (*.svg)|*.svg|M2Draw 확장 SVG (*.m2svg)|*.m2svg|모든 파일 (*.*)|*.*||");
		CFileDialog dlg(FALSE, _T("svg"), nullptr, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, filter);
		dlg.m_ofn.lpstrTitle = _T("다른 이름으로 저장");
		if (dlg.DoModal() != IDOK)
			return FALSE;
		newName = dlg.GetPathName();
	}

	CWaitCursor wait;
	if (!OnSaveDocument(newName))
		return FALSE;

	if (bReplace)
		SetPathName(newName);

	return TRUE;
}

void CM2DrawDoc::UpdateTitleWithModified()
{
	CString strTitle = GetTitle();
	bool bHasStar = (strTitle.Right(2) == _T(" *") || strTitle.Right(1) == _T("*"));

	if (IsModified() && !bHasStar)
	{
		strTitle += _T(" *");
		SetTitle(strTitle);
	}
	else if (!IsModified() && bHasStar)
	{
		if (strTitle.Right(2) == _T(" *"))
			strTitle = strTitle.Left(strTitle.GetLength() - 2);
		else if (strTitle.Right(1) == _T("*"))
			strTitle = strTitle.Left(strTitle.GetLength() - 1);
		SetTitle(strTitle);
	}
}

void CM2DrawDoc::SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU)
{
	CDocument::SetPathName(lpszPathName, bAddToMRU);
	UpdateTitleWithModified();
}

void CM2DrawDoc::SetModifiedFlag(BOOL bModified)
{
	CDocument::SetModifiedFlag(bModified);
	UpdateTitleWithModified();

	// 문서와 연결된 모든 뷰의 프레임 창 타이틀을 갱신하도록 통지
	POSITION pos = GetFirstViewPosition();
	while (pos != nullptr)
	{
		CView* pView = GetNextView(pos);
		if (pView)
		{
			CFrameWnd* pFrame = pView->GetParentFrame();
			if (pFrame)
			{
				pFrame->OnUpdateFrameTitle(TRUE);
			}
		}
	}
}

bool CM2DrawDoc::CommitUndoPoint(const std::wstring& actionName)
{
	if (!m_undoMgr.CommitChange(actionName, m_doc))
		return false;
	SetModifiedFlag(TRUE);
	return true;
}

void CM2DrawDoc::ResetToolInteraction()
{
	// 문서가 통째로 교체되면(Undo/Redo/열기/새 문서) 도구가 들고 있던 노드 포인터가 무효가 되므로 진행 중 상태를 폐기
	CM2DrawApp* pApp = dynamic_cast<CM2DrawApp*>(AfxGetApp());
	SelectTool* pSelect = pApp ? pApp->GetToolManager().GetSelectTool() : nullptr;
	if (pSelect) pSelect->ResetInteraction();
}

void CM2DrawDoc::OnEditUndo()
{
	// 기록되지 않은 편집이 남아 있으면 먼저 확정해서 "방금 한 작업" 이 되돌려지도록 함
	CommitUndoPoint(L"편집");
	if (!m_undoMgr.CanUndo())
		return;

	std::wstring action = m_undoMgr.GetLastUndoActionName();
	if (m_undoMgr.Undo(m_doc))
	{
		ResetToolInteraction();
		SetModifiedFlag(TRUE);
		UpdateAllViews(nullptr);
		UpdateShapePropertiesSelection(GetPrimarySelection(), this);

		CWnd* pMainWnd = AfxGetMainWnd();
		if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		{
			static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		}

		CString strLog;
		strLog.Format(_T("[실행 취소] %s 취소됨"), action.c_str());
		LogAppEvent(strLog);
		SetAppStatusText(strLog);
	}
}

void CM2DrawDoc::OnUpdateEditUndo(CCmdUI* pCmdUI)
{
	if (pCmdUI)
		pCmdUI->Enable(m_undoMgr.CanUndo() ? TRUE : FALSE);
}

void CM2DrawDoc::OnEditRedo()
{
	if (!m_undoMgr.CanRedo())
		return;

	std::wstring action = m_undoMgr.GetLastRedoActionName();
	if (m_undoMgr.Redo(m_doc))
	{
		ResetToolInteraction();
		SetModifiedFlag(TRUE);
		UpdateAllViews(nullptr);
		UpdateShapePropertiesSelection(GetPrimarySelection(), this);

		CWnd* pMainWnd = AfxGetMainWnd();
		if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		{
			static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		}

		CString strLog;
		strLog.Format(_T("[다시 실행] %s 다시 실행됨"), action.c_str());
		LogAppEvent(strLog);
		SetAppStatusText(strLog);
	}
}

void CM2DrawDoc::OnUpdateEditRedo(CCmdUI* pCmdUI)
{
	if (pCmdUI)
		pCmdUI->Enable(m_undoMgr.CanRedo() ? TRUE : FALSE);
}

#ifdef SHARED_HANDLERS

// 축소판 그림을 지원합니다.
void CM2DrawDoc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
	// 문서의 데이터를 그리려면 이 코드를 수정하십시오.
	dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

	CString strText = _T("TODO: implement thumbnail drawing here");
	LOGFONT lf;

	CFont* pDefaultGUIFont = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
	pDefaultGUIFont->GetLogFont(&lf);
	lf.lfHeight = 36;

	CFont fontDraw;
	fontDraw.CreateFontIndirect(&lf);

	CFont* pOldFont = dc.SelectObject(&fontDraw);
	dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
	dc.SelectObject(pOldFont);
}

// 검색 처리기를 지원합니다.
void CM2DrawDoc::InitializeSearchContent()
{
	CString strSearchContent;
	// 문서의 데이터에서 검색 콘텐츠를 설정합니다.
	// 콘텐츠 부분은 ";"로 구분되어야 합니다.

	// 예: strSearchContent = _T("point;rectangle;circle;ole object;");
	SetSearchContent(strSearchContent);
}

void CM2DrawDoc::SetSearchContent(const CString& value)
{
	if (value.IsEmpty())
	{
		RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
	}
	else
	{
		CMFCFilterChunkValueImpl *pChunk = nullptr;
		ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
		if (pChunk != nullptr)
		{
			pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
			SetChunkValue(pChunk);
		}
	}
}

#endif // SHARED_HANDLERS

// CM2DrawDoc 진단

#ifdef _DEBUG
void CM2DrawDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CM2DrawDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CM2DrawDoc 명령

bool CM2DrawDoc::HasSelectedObjects() const
{
	return !m_doc.GetSelection().empty();
}

bool CM2DrawDoc::DeleteSelectedObjects()
{
	// 1. 선택된 노드들 수집 (복사본: RemoveNode 가 선택 목록을 수정함)
	const std::vector<ge::Node*> targetNodes = m_doc.GetSelection();

	if (targetNodes.empty())
		return false;


	// 2. 문서에서 노드 제거 (소유권 반환 -> 즉시 해제)
	for (auto* n : targetNodes)
	{
		if (n)
		{
			m_doc.RemoveNode(n);
		}
	}

	// 3. 선택 상태 초기화
	m_doc.ClearSelection();
	UpdateShapePropertiesSelection(nullptr, this);

	// 6. UI 및 뷰 동기화
	CString strLog;
	strLog.Format(_T("[도형 삭제] 선택된 %d개 오브젝트 삭제 완료"), static_cast<int>(targetNodes.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"도형 삭제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetLayerWnd().RefreshLayers();
	}

	return true;
}

bool CM2DrawDoc::DeleteActiveLayer()
{
	auto& layers = m_doc.GetLayers();
	if (layers.size() <= 1)
	{
		CString strMsg = _T("[레이어 삭제 제한] 최소 1개의 레이어는 항상 유지되어야 합니다.");
		LogAppEvent(strMsg);
		SetAppStatusText(strMsg);
		return false;
	}

	auto* pActive = m_doc.GetActiveLayer();
	if (!pActive) return false;


	CString layerName = pActive->GetName().c_str();

	const int childCount = static_cast<int>(pActive->GetChildren().size());

	// m_doc에서 레이어 제거 (하위 도형 포함)
	if (!m_doc.RemoveLayer(pActive))
		return false;

	// 선택 정리
	m_doc.ClearSelection();
	UpdateShapePropertiesSelection(nullptr, this);

	CString strLog;
	strLog.Format(_T("[레이어 패널] 레이어 삭제됨: '%s' (하위 도형 %d개 포함)"), (LPCTSTR)layerName, childCount);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"레이어 삭제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetLayerWnd().RefreshLayers();
	}

	return true;
}

bool CM2DrawDoc::PerformSmartDelete()
{
	// 1. 오브젝트가 선택되어 있으면 오브젝트 삭제 우선!
	if (HasSelectedObjects())
	{
		return DeleteSelectedObjects();
	}

	// 2. 오브젝트 선택이 없다면 선택된(활성) 레이어 삭제 시도
	return DeleteActiveLayer();
}

bool CM2DrawDoc::DuplicateSelectedObjects()
{

	// 원본과 같은 부모(레이어) 안에서 바로 위에 복제, 복제본이 새 선택이 됨
	std::vector<ge::Node*> clones = m_doc.DuplicateSelection();
	if (clones.empty())
		return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);

	CString strLog;
	strLog.Format(_T("[도형 복제] %d개 오브젝트 복제 완료"), static_cast<int>(clones.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"도형 복제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}
	return true;
}
