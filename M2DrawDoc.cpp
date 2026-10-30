
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
#include "M2DrawView.h"
#include "MainFrm.h"
#include "ITool.h"
#include "SvgWriter.h"
#include "SvgReader.h"
#include "HtmlExporter.h"
#include "ClipboardSvg.h"
#include "RasterExport.h"
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
	ON_COMMAND(ID_OBJECT_GROUP, &CM2DrawDoc::OnObjectGroup)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_GROUP, &CM2DrawDoc::OnUpdateObjectGroup)
	ON_COMMAND(ID_OBJECT_UNGROUP, &CM2DrawDoc::OnObjectUngroup)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_UNGROUP, &CM2DrawDoc::OnUpdateObjectUngroup)
	ON_COMMAND(ID_OBJECT_WRAP_FRAME, &CM2DrawDoc::OnObjectWrapFrame)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_WRAP_FRAME, &CM2DrawDoc::OnUpdateObjectWrapFrame)
	ON_COMMAND_RANGE(ID_OBJECT_FRONT, ID_OBJECT_BACK, &CM2DrawDoc::OnObjectArrange)
	ON_UPDATE_COMMAND_UI_RANGE(ID_OBJECT_FRONT, ID_OBJECT_BACK, &CM2DrawDoc::OnUpdateObjectArrange)
	ON_COMMAND_RANGE(ID_BOOLEAN_UNION, ID_BOOLEAN_TRIM, &CM2DrawDoc::OnBooleanOperation)
	ON_UPDATE_COMMAND_UI_RANGE(ID_BOOLEAN_UNION, ID_BOOLEAN_TRIM, &CM2DrawDoc::OnUpdateBooleanOperation)
	ON_COMMAND(ID_OBJECT_CLIP_MASK, &CM2DrawDoc::OnObjectClipMask)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_CLIP_MASK, &CM2DrawDoc::OnUpdateObjectClipMask)
	ON_COMMAND(ID_OBJECT_RELEASE_MASK, &CM2DrawDoc::OnObjectReleaseMask)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_RELEASE_MASK, &CM2DrawDoc::OnUpdateObjectReleaseMask)
	ON_COMMAND_RANGE(ID_ALIGN_LEFT, ID_DISTRIBUTE_V, &CM2DrawDoc::OnAlignCommand)
	ON_UPDATE_COMMAND_UI_RANGE(ID_ALIGN_LEFT, ID_DISTRIBUTE_V, &CM2DrawDoc::OnUpdateAlignCommand)
	ON_COMMAND(ID_FILE_PLACE_IMAGE, &CM2DrawDoc::OnFilePlaceImage)
	ON_COMMAND(ID_FILE_EXPORT_SVG, &CM2DrawDoc::OnFileExportSvg)
	ON_COMMAND(ID_FILE_EXPORT_HTML, &CM2DrawDoc::OnFileExportHtml)
	ON_COMMAND(ID_FILE_EXPORT_IMAGE, &CM2DrawDoc::OnFileExportImage)
	ON_COMMAND(ID_EDIT_COPY, &CM2DrawDoc::OnEditCopy)
	ON_COMMAND(ID_EDIT_CUT, &CM2DrawDoc::OnEditCut)
	ON_COMMAND(ID_EDIT_PASTE, &CM2DrawDoc::OnEditPaste)
	ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, &CM2DrawDoc::OnUpdateEditCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, &CM2DrawDoc::OnUpdateEditCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, &CM2DrawDoc::OnUpdateEditPaste)
	ON_COMMAND(ID_TEXT_TO_PATH, &CM2DrawDoc::OnTextToPath)
	ON_UPDATE_COMMAND_UI(ID_TEXT_TO_PATH, &CM2DrawDoc::OnUpdateTextToPath)
	ON_COMMAND(ID_PATH_OUTLINE, &CM2DrawDoc::OnPathOutline)
	ON_UPDATE_COMMAND_UI(ID_PATH_OUTLINE, &CM2DrawDoc::OnUpdatePathOutline)
	ON_COMMAND(ID_PATH_SIMPLIFY, &CM2DrawDoc::OnPathSimplify)
	ON_UPDATE_COMMAND_UI(ID_PATH_SIMPLIFY, &CM2DrawDoc::OnUpdatePathSimplify)
	ON_COMMAND(ID_PATH_REVERSE, &CM2DrawDoc::OnPathReverse)
	ON_UPDATE_COMMAND_UI(ID_PATH_REVERSE, &CM2DrawDoc::OnUpdatePathReverse)
	ON_COMMAND(ID_PATH_JOIN, &CM2DrawDoc::OnPathJoin)
	ON_UPDATE_COMMAND_UI(ID_PATH_JOIN, &CM2DrawDoc::OnUpdatePathJoin)
END_MESSAGE_MAP()


// CM2DrawDoc 생성/소멸

CM2DrawDoc::CM2DrawDoc() noexcept
{
	// TODO: 여기에 일회성 생성 코드를 추가합니다.

}

CM2DrawDoc::~CM2DrawDoc()
{
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pFrame = static_cast<CMainFrame*>(pMainWnd);
		pFrame->GetPropertiesWnd().SetSelectedShape(nullptr, nullptr);
		pFrame->GetGridPropertiesWnd().SetSelectedShape(nullptr, nullptr);
		pFrame->GetCanvasPropertiesWnd().SetSelectedShape(nullptr, nullptr);
	}
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
	m_doc.EnsureUniqueIds();   // 컴포넌트 Rebuild 등으로 새로 생긴 노드에 ID 발급 (스냅샷/선택 복원이 ID 기준)
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
	if (GradientTool* pGradient = pApp ? pApp->GetToolManager().GetGradientTool() : nullptr) pGradient->ResetInteraction();
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

// ---------------------------------------------------------------------------
// 그룹화 / 그룹 해제
// ---------------------------------------------------------------------------
bool CM2DrawDoc::GroupSelectedObjects()
{
	// 선택된 오브젝트 2개 이상을 하나의 그룹으로 (부모가 달라도 월드 위치 유지, 맨 위 오브젝트가 있던 자리에 생성)
	ge::GroupNode* group = m_doc.GroupSelection();
	if (!group)
		return false;

	UpdateShapePropertiesSelection(group, this);

	CString strLog;
	strLog.Format(_T("[그룹화] '%s' 생성 (오브젝트 %d개)"), group->GetName().c_str(), static_cast<int>(group->GetChildren().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"그룹");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

bool CM2DrawDoc::UngroupSelectedObjects()
{
	if (!m_doc.CanUngroupSelection())
		return false;

	m_doc.UngroupSelection();   // 자식들이 그룹 자리로 올라오고(월드 위치/불투명도 유지) 자식들이 새 선택이 됨
	UpdateShapePropertiesSelection(GetPrimarySelection(), this);

	CString strLog;
	strLog.Format(_T("[그룹 해제] 오브젝트 %d개 선택됨"), static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"그룹 해제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectGroup()               { GroupSelectedObjects(); }
void CM2DrawDoc::OnUpdateObjectGroup(CCmdUI* pCmdUI)   { pCmdUI->Enable(CanGroup()); }
void CM2DrawDoc::OnObjectUngroup()             { UngroupSelectedObjects(); }
void CM2DrawDoc::OnUpdateObjectUngroup(CCmdUI* pCmdUI) { pCmdUI->Enable(CanUngroup()); }

bool CM2DrawDoc::WrapSelectedInFrame()
{
	ge::FrameNode* frame = m_doc.WrapSelectionInFrame();
	if (!frame)
		return false;

	UpdateShapePropertiesSelection(frame, this);

	CString strLog;
	strLog.Format(_T("[프레임으로 감싸기] '%s' 생성 (자식 %d개)"), frame->GetName().c_str(), static_cast<int>(frame->GetChildren().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"프레임으로 감싸기");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectWrapFrame()               { WrapSelectedInFrame(); }
void CM2DrawDoc::OnUpdateObjectWrapFrame(CCmdUI* pCmdUI) { pCmdUI->Enable(m_doc.CanWrapInFrame()); }

// ---------------------------------------------------------------------------
// 순서(Z-order) 변경
// ---------------------------------------------------------------------------
bool CM2DrawDoc::ArrangeSelectedObjects(ge::Document::ZOrder order)
{
	if (!m_doc.CanArrangeSelection())
		return false;

	m_doc.ArrangeSelection(order);   // 선택 노드는 각자의 부모 안에서 이동, 선택 노드끼리의 상대 순서는 유지

	const wchar_t* label = L"순서 변경";
	switch (order)
	{
	case ge::Document::ZOrder::Front:    label = L"맨 앞으로"; break;
	case ge::Document::ZOrder::Forward:  label = L"앞으로";   break;
	case ge::Document::ZOrder::Backward: label = L"뒤로";     break;
	case ge::Document::ZOrder::Back:     label = L"맨 뒤로";  break;
	}

	CString strLog;
	strLog.Format(_T("[순서 변경] %s (선택 %d개)"), label, static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(label);   // 순서가 실제로 바뀌지 않았으면(이미 맨 앞 등) 기록되지 않음
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectArrange(UINT nID)
{
	switch (nID)
	{
	case ID_OBJECT_FRONT:    ArrangeSelectedObjects(ge::Document::ZOrder::Front);    break;
	case ID_OBJECT_FORWARD:  ArrangeSelectedObjects(ge::Document::ZOrder::Forward);  break;
	case ID_OBJECT_BACKWARD: ArrangeSelectedObjects(ge::Document::ZOrder::Backward); break;
	case ID_OBJECT_BACK:     ArrangeSelectedObjects(ge::Document::ZOrder::Back);     break;
	}
}

void CM2DrawDoc::OnUpdateObjectArrange(CCmdUI* pCmdUI) { pCmdUI->Enable(CanArrange()); }

// ---------------------------------------------------------------------------
// 불리언 연산 / 패스파인더
// ---------------------------------------------------------------------------
bool CM2DrawDoc::PerformBooleanOperation(ge::BooleanMode mode)
{
	if (!CanBoolean())
		return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView))) {
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
		}
	}
	if (!pFactory)
		return false;

	ge::PathNode* result = m_doc.BooleanOperation(mode, pFactory);
	if (!result)
		return false;

	UpdateShapePropertiesSelection(result, this);

	const wchar_t* label = L"불리언 연산";
	switch (mode) {
	case ge::BooleanMode::Union:     label = L"패스 합치기"; break;
	case ge::BooleanMode::Subtract:  label = L"패스 빼기"; break;
	case ge::BooleanMode::Intersect: label = L"패스 교차"; break;
	case ge::BooleanMode::Exclude:   label = L"패스 제외"; break;
	}

	CString strLog;
	strLog.Format(_T("[%s] '%s' 생성 완료"), label, result->GetName().c_str());
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(label);
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}
	return true;
}

void CM2DrawDoc::OnBooleanOperation(UINT nID)
{
	switch (nID) {
	case ID_BOOLEAN_UNION:     PerformBooleanOperation(ge::BooleanMode::Union);     break;
	case ID_BOOLEAN_SUBTRACT:  PerformBooleanOperation(ge::BooleanMode::Subtract);  break;
	case ID_BOOLEAN_INTERSECT: PerformBooleanOperation(ge::BooleanMode::Intersect); break;
	case ID_BOOLEAN_EXCLUDE:   PerformBooleanOperation(ge::BooleanMode::Exclude);   break;
	case ID_BOOLEAN_TRIM:
		AfxMessageBox(_T("트림/분할 기능은 다음 단계에서 추가될 예정입니다."), MB_ICONINFORMATION);
		break;
	}
}

void CM2DrawDoc::OnUpdateBooleanOperation(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanBoolean());
}

bool CM2DrawDoc::MakeClipMask()
{
	ge::GroupNode* g = m_doc.MakeClipMask();
	if (!g) return false;

	UpdateShapePropertiesSelection(g, this);
	CommitUndoPoint(L"클리핑 마스크 만들기");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

bool CM2DrawDoc::ReleaseClipMask()
{
	if (!CanReleaseClipMask()) return false;
	m_doc.ReleaseClipMask();

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"클리핑 마스크 해제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

void CM2DrawDoc::OnObjectClipMask()
{
	MakeClipMask();
}

void CM2DrawDoc::OnUpdateObjectClipMask(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanMakeClipMask());
}

void CM2DrawDoc::OnObjectReleaseMask()
{
	ReleaseClipMask();
}

void CM2DrawDoc::OnUpdateObjectReleaseMask(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanReleaseClipMask());
}

bool CM2DrawDoc::OutlineStroke(float widthOverride)
{
	if (!CanOutlineStroke()) return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView))) {
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
		}
	}
	if (!pFactory) return false;

	auto results = m_doc.OutlineStrokeSelection(pFactory, widthOverride);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"윤곽선 만들기 (외곽선 확장)");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

bool CM2DrawDoc::ConvertTextToPath()
{
	if (!CanConvertTextToPath()) return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView)))
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
	}
	if (!pFactory) return false;

	auto results = m_doc.TextToPathSelection(pFactory);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"텍스트를 윤곽선으로 변환");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnTextToPath() { ConvertTextToPath(); }

// 이미지 파일을 읽어 ImageData 로 (실패하면 메시지 후 nullptr)
static std::shared_ptr<ge::ImageData> LoadImageFile(const CString& path)
{
	auto data = std::make_shared<ge::ImageData>();
	{
		CFile f;
		if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite)) { AfxMessageBox(_T("이미지 파일을 열 수 없습니다.")); return nullptr; }
		const ULONGLONG len = f.GetLength();
		if (len == 0 || len > 256ull * 1024 * 1024) { AfxMessageBox(_T("이미지 파일 크기가 올바르지 않습니다.")); return nullptr; }
		data->bytes.resize(static_cast<size_t>(len));
		f.Read(data->bytes.data(), static_cast<UINT>(len));
	}
	int pw = 0, ph = 0;
	if (!ge::PathOp::DecodeImageInfo(data->bytes, pw, ph)) { AfxMessageBox(_T("지원하지 않는 이미지 형식이거나 손상된 파일입니다.")); return nullptr; }
	data->pixelW = pw; data->pixelH = ph;
	CString ext = path.Mid(path.ReverseFind(_T('.')) + 1); ext.MakeLower();
	data->mime = (ext == _T("jpg") || ext == _T("jpeg")) ? "image/jpeg" : ext == _T("gif") ? "image/gif" : ext == _T("bmp") ? "image/bmp" : "image/png";
	return data;
}

static const TCHAR* kImageFilter = _T("이미지 파일 (*.png;*.jpg;*.jpeg;*.gif;*.bmp)|*.png;*.jpg;*.jpeg;*.gif;*.bmp|모든 파일 (*.*)|*.*||");

// 이미지 파일을 문서 중앙에(문서 크기의 60% 이내로 맞춰) ImageNode 로 추가
bool CM2DrawDoc::PlaceImageFile(const CString& path)
{
	auto data = LoadImageFile(path);
	if (!data) return false;

	float w = static_cast<float>(data->pixelW), h = static_cast<float>(data->pixelH);
	const float maxW = m_doc.width * 0.6f, maxH = m_doc.height * 0.6f;
	const float sc = (std::min)(1.0f, (std::min)(maxW / w, maxH / h));
	w *= sc; h *= sc;

	auto im = std::make_unique<ge::ImageNode>();
	im->SetData(data);
	im->SetSize(w, h);
	const int slash = (std::max)(path.ReverseFind(_T('\\')), path.ReverseFind(_T('/')));
	CString title = path.Mid(slash + 1);
	const int dot = title.ReverseFind(_T('.'));
	if (dot > 0) title = title.Left(dot);
	im->SetName((LPCTSTR)title);
	im->SetTransform(ge::Matrix2D::Translation((m_doc.width - w) * 0.5f, (m_doc.height - h) * 0.5f));
	ge::Node* added = m_doc.AddShape(std::move(im));
	if (!added) return false;
	m_doc.SetSelection({ added });
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"이미지 가져오기");
	LogAppEvent(_T("[이미지] 이미지 가져오기 완료"));
	UpdateShapePropertiesSelection(added, this);
	UpdateAllViews(nullptr);
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

// 내보내기 옵션 대화상자 (메모리 템플릿): 텍스트를 윤곽선으로 변환할지 선택
namespace {
class CExportOptionsDlg : public CDialog
{
public:
	bool m_outlineText = false;

	INT_PTR Run(CWnd* parent)
	{
		std::vector<WORD> w;
		auto add32 = [&](DWORD v) { w.push_back(LOWORD(v)); w.push_back(HIWORD(v)); };
		auto addStr = [&](const wchar_t* s) { while (*s) w.push_back(*s++); w.push_back(0); };
		auto align = [&]() { if (w.size() % 2) w.push_back(0); };
		add32(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
		add32(0);
		w.push_back(4);
		w.push_back(0); w.push_back(0); w.push_back(250); w.push_back(104);
		w.push_back(0); w.push_back(0);
		addStr(L"SVG 내보내기");
		w.push_back(9); addStr(L"Segoe UI");
		auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t* title) {
			align();
			add32(style | WS_CHILD | WS_VISIBLE);
			add32(0);
			w.push_back(x); w.push_back(y); w.push_back(cx); w.push_back(cy);
			w.push_back(id);
			w.push_back(0xFFFF); w.push_back(atom);
			addStr(title);
			w.push_back(0);
		};
		item(SS_LEFT, 8, 8, 234, 30, 0xFFFF, 0x0082, L"순수 SVG(Illustrator, Inkscape, Figma, 브라우저 호환)로 내보냅니다. M2Draw 전용 속성은 포함하지 않으며, 글꼴에는 대체 글꼴 목록이 붙습니다.");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 8, 46, 234, 12, 1001, 0x0080, L"텍스트를 윤곽선으로 변환하여 출력 (글꼴이 없어도 똑같이 보임)");
		item(BS_DEFPUSHBUTTON | WS_TABSTOP, 136, 80, 50, 14, IDOK, 0x0080, L"내보내기");
		item(BS_PUSHBUTTON | WS_TABSTOP, 192, 80, 50, 14, IDCANCEL, 0x0080, L"취소");
		if (!InitModalIndirect(reinterpret_cast<LPCDLGTEMPLATE>(w.data()), parent)) return IDCANCEL;
		return DoModal();
	}
protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		CheckDlgButton(1001, m_outlineText ? BST_CHECKED : BST_UNCHECKED);
		return TRUE;
	}
	void OnOK() override
	{
		m_outlineText = IsDlgButtonChecked(1001) == BST_CHECKED;
		CDialog::OnOK();
	}
};
} // namespace

void CM2DrawDoc::OnFileExportSvg()
{
	CExportOptionsDlg opt;
	opt.m_outlineText = AfxGetApp()->GetProfileInt(_T("Export"), _T("OutlineText"), 0) != 0;
	if (opt.Run(AfxGetMainWnd()) != IDOK) return;
	AfxGetApp()->WriteProfileInt(_T("Export"), _T("OutlineText"), opt.m_outlineText ? 1 : 0);

	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);
	CFileDialog dlg(FALSE, _T("svg"), suggested + _T(".svg"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("SVG 파일 (*.svg)|*.svg||"));
	if (dlg.DoModal() != IDOK) return;

	ge::SvgWriteOptions options(ge::SvgWriteMode::Clean);
	options.textAsOutlines = opt.m_outlineText;
	ge::SvgWriter writer;
	if (!writer.WriteToFile(m_doc, (LPCTSTR)dlg.GetPathName(), options))
	{
		AfxMessageBox(_T("SVG 파일을 저장하지 못했습니다."));
		return;
	}
	CString log;
	log.Format(_T("[내보내기] 순수 SVG 저장: %s%s"), (LPCTSTR)dlg.GetPathName(), opt.m_outlineText ? _T(" (텍스트 윤곽선)") : _T(""));
	LogAppEvent(log);
	SetAppStatusText(log);
}

// ============================================================================
// 클립보드 (Step 5.4): SVG 문자열로 교환. 형식: "image/svg+xml"(UTF-8 바이트) + CF_UNICODETEXT
// ============================================================================
static UINT SvgClipboardFormat()
{
	static const UINT f = ::RegisterClipboardFormatW(L"image/svg+xml");
	return f;
}

static std::string ReadClipboardSvgText()
{
	std::string out;
	CWnd* wnd = AfxGetMainWnd();
	if (!wnd || !wnd->OpenClipboard()) return out;
	auto readBytes = [&](UINT fmt, bool wide) -> std::string {
		HANDLE h = ::GetClipboardData(fmt);
		if (!h) return std::string();
		const void* p = ::GlobalLock(h);
		if (!p) return std::string();
		std::string r;
		if (wide) {
			const wchar_t* w = static_cast<const wchar_t*>(p);
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			if (n > 1) { r.resize(static_cast<size_t>(n) - 1); ::WideCharToMultiByte(CP_UTF8, 0, w, -1, r.data(), n, nullptr, nullptr); }
		} else {
			r.assign(static_cast<const char*>(p));
		}
		::GlobalUnlock(h);
		return r;
	};
	if (::IsClipboardFormatAvailable(SvgClipboardFormat())) out = readBytes(SvgClipboardFormat(), false);
	if (!ge::LooksLikeSvg(out) && ::IsClipboardFormatAvailable(CF_UNICODETEXT)) out = readBytes(CF_UNICODETEXT, true);
	::CloseClipboard();
	return out;
}

bool CM2DrawDoc::CopySelectionToClipboard()
{
	const std::string svg = ge::SerializeNodesToSvg(m_doc, m_doc.GetSelection());
	if (svg.empty()) return false;
	CWnd* wnd = AfxGetMainWnd();
	if (!wnd || !wnd->OpenClipboard()) return false;
	::EmptyClipboard();

	HGLOBAL hSvg = ::GlobalAlloc(GMEM_MOVEABLE, svg.size() + 1);
	if (hSvg) {
		if (void* p = ::GlobalLock(hSvg)) { memcpy(p, svg.c_str(), svg.size() + 1); ::GlobalUnlock(hSvg); }
		if (!::SetClipboardData(SvgClipboardFormat(), hSvg)) ::GlobalFree(hSvg);
	}
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, svg.c_str(), -1, nullptr, 0);
	if (n > 0) {
		HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(n) * sizeof(wchar_t));
		if (hText) {
			if (void* p = ::GlobalLock(hText)) { ::MultiByteToWideChar(CP_UTF8, 0, svg.c_str(), -1, static_cast<wchar_t*>(p), n); ::GlobalUnlock(hText); }
			if (!::SetClipboardData(CF_UNICODETEXT, hText)) ::GlobalFree(hText);
		}
	}
	::CloseClipboard();
	m_pasteCount = 0;
	CString log;
	log.Format(_T("[클립보드] SVG 복사 (%d개 선택)"), static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(log);
	return true;
}

bool CM2DrawDoc::PasteFromClipboard()
{
	const std::string svg = ReadClipboardSvgText();
	if (svg.empty()) return false;
	ge::Document tmp;
	std::vector<const ge::Node*> src;
	if (!ge::ParseSvgNodes(svg, tmp, src)) return false;

	// 선택이 프레임(또는 프레임 안)이면 그 프레임에 붙여넣기, 아니면 활성 레이어
	ge::FrameNode* targetFrame = nullptr;
	if (ge::Node* sel = GetPrimarySelection())
		for (ge::Node* p = sel; p; p = p->GetParent())
			if (auto* fr = dynamic_cast<ge::FrameNode*>(p)) { targetFrame = fr; break; }

	std::vector<ge::Node*> imported = m_doc.ImportNodes(src);
	if (imported.empty()) return false;

	++m_pasteCount;
	const float off = 16.0f * static_cast<float>(((m_pasteCount - 1) % 6) + 1);   // 연속 붙여넣기 시 겹치지 않게
	for (ge::Node* n : imported)
		n->SetTransform(n->GetTransform() * ge::Matrix2D::Translation(off, off));
	if (targetFrame)
		for (ge::Node* n : imported) {
			if (m_doc.MoveNodeTo(n, targetFrame, -1)) ge::FrameNode::ClampNode(n, false);
		}

	m_doc.SetSelection(imported);
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"붙여넣기");
	LogAppEvent(_T("[클립보드] SVG 붙여넣기"));
	UpdateShapePropertiesSelection(imported.front(), this);
	UpdateAllViews(nullptr);
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnEditCopy() { CopySelectionToClipboard(); }

void CM2DrawDoc::OnEditCut()
{
	if (!CopySelectionToClipboard()) return;
	PerformSmartDelete();
	CommitUndoPoint(L"잘라내기");
	UpdateAllViews(nullptr);
}

void CM2DrawDoc::OnEditPaste() { PasteFromClipboard(); }

void CM2DrawDoc::OnUpdateEditCopy(CCmdUI* pCmdUI) { pCmdUI->Enable(HasSelectedObjects()); }

void CM2DrawDoc::OnUpdateEditPaste(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(::IsClipboardFormatAvailable(SvgClipboardFormat()) || ::IsClipboardFormatAvailable(CF_UNICODETEXT));
}

// ============================================================================
// PNG/JPG 내보내기 (Step 5.5)
// ============================================================================
namespace {
class CExportImageDlg : public CDialog
{
public:
	int   m_format = 0;          // 0 = PNG, 1 = JPG
	int   m_scaleIdx = 1;        // 0:1x 1:2x 2:4x 3:8x
	int   m_dpi = 96;
	bool  m_transparent = true;
	bool  m_selectionOnly = false;
	bool  m_hasSelection = false;
	CString m_sizeText;

	INT_PTR Run(CWnd* parent)
	{
		std::vector<WORD> w;
		auto add32 = [&](DWORD v) { w.push_back(LOWORD(v)); w.push_back(HIWORD(v)); };
		auto addStr = [&](const wchar_t* s) { while (*s) w.push_back(*s++); w.push_back(0); };
		auto align = [&]() { if (w.size() % 2) w.push_back(0); };
		add32(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
		add32(0);
		w.push_back(11);   // 항목 수: 정적 4 + 콤보 2 + 편집 1 + 체크 2 + 크기 문구 1(위 합산 포함) + 버튼 2 = 11
		w.push_back(0); w.push_back(0); w.push_back(250); w.push_back(152);
		w.push_back(0); w.push_back(0);
		addStr(L"이미지 내보내기 (PNG / JPG)");
		w.push_back(9); addStr(L"Segoe UI");
		auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t* title) {
			align();
			add32(style | WS_CHILD | WS_VISIBLE);
			add32(0);
			w.push_back(x); w.push_back(y); w.push_back(cx); w.push_back(cy);
			w.push_back(id);
			w.push_back(0xFFFF); w.push_back(atom);
			addStr(title);
			w.push_back(0);
		};
		item(SS_LEFT, 8, 10, 40, 10, 0xFFFF, 0x0082, L"형식");
		item(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 52, 8, 100, 60, 1001, 0x0085, L"");
		item(SS_LEFT, 8, 30, 40, 10, 0xFFFF, 0x0082, L"배율");
		item(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 52, 28, 100, 70, 1002, 0x0085, L"");
		item(SS_LEFT, 8, 50, 40, 10, 0xFFFF, 0x0082, L"해상도(DPI)");
		item(ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 52, 48, 50, 12, 1003, 0x0081, L"96");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 8, 68, 234, 12, 1004, 0x0080, L"투명 배경 (PNG 만 해당, JPG 는 문서 배경색)");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 8, 84, 234, 12, 1005, 0x0080, L"선택한 오브젝트 영역만 내보내기");
		item(SS_LEFT, 8, 102, 234, 10, 1006, 0x0082, L"");
		item(BS_DEFPUSHBUTTON | WS_TABSTOP, 136, 130, 50, 14, IDOK, 0x0080, L"내보내기");
		item(BS_PUSHBUTTON | WS_TABSTOP, 192, 130, 50, 14, IDCANCEL, 0x0080, L"취소");
		if (!InitModalIndirect(reinterpret_cast<LPCDLGTEMPLATE>(w.data()), parent)) return IDCANCEL;
		return DoModal();
	}
	float Scale() const { static const float k[] = { 1.0f, 2.0f, 4.0f, 8.0f }; return k[(std::max)(0, (std::min)(3, m_scaleIdx))]; }

protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		CComboBox* f = static_cast<CComboBox*>(GetDlgItem(1001));
		f->AddString(_T("PNG (무손실, 투명 배경 가능)"));
		f->AddString(_T("JPG (불투명)"));
		f->SetCurSel(m_format);
		CComboBox* s = static_cast<CComboBox*>(GetDlgItem(1002));
		s->AddString(_T("1x"));
		s->AddString(_T("2x"));
		s->AddString(_T("4x"));
		s->AddString(_T("8x"));
		s->SetCurSel(m_scaleIdx);
		SetDlgItemInt(1003, m_dpi, FALSE);
		CheckDlgButton(1004, m_transparent ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(1005, (m_selectionOnly && m_hasSelection) ? BST_CHECKED : BST_UNCHECKED);
		GetDlgItem(1005)->EnableWindow(m_hasSelection);
		SetDlgItemText(1006, m_sizeText);
		return TRUE;
	}
	void OnOK() override
	{
		m_format = static_cast<CComboBox*>(GetDlgItem(1001))->GetCurSel();
		m_scaleIdx = static_cast<CComboBox*>(GetDlgItem(1002))->GetCurSel();
		BOOL ok = FALSE;
		const UINT d = GetDlgItemInt(1003, &ok, FALSE);
		m_dpi = ok ? static_cast<int>((std::max)(1u, (std::min)(d, 2400u))) : 96;
		m_transparent = IsDlgButtonChecked(1004) == BST_CHECKED;
		m_selectionOnly = m_hasSelection && IsDlgButtonChecked(1005) == BST_CHECKED;
		CDialog::OnOK();
	}
};
} // namespace

void CM2DrawDoc::OnFileExportImage()
{
	CExportImageDlg dlg;
	dlg.m_format = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Format"), 0);
	dlg.m_scaleIdx = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Scale"), 1);
	dlg.m_dpi = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Dpi"), 96);
	dlg.m_transparent = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Transparent"), 1) != 0;
	dlg.m_hasSelection = HasSelectedObjects();
	dlg.m_sizeText.Format(_T("문서 크기 %d x %d px  (출력 = 크기 x 배율)"), static_cast<int>(m_doc.width), static_cast<int>(m_doc.height));
	if (dlg.Run(AfxGetMainWnd()) != IDOK) return;
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Format"), dlg.m_format);
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Scale"), dlg.m_scaleIdx);
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Dpi"), dlg.m_dpi);
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Transparent"), dlg.m_transparent ? 1 : 0);

	ge::RasterExportOptions opt;
	opt.jpeg = dlg.m_format == 1;
	opt.scale = dlg.Scale();
	opt.dpi = static_cast<float>(dlg.m_dpi);
	opt.transparent = dlg.m_transparent;

	ge::Rect2 region{};
	const ge::Rect2* pRegion = nullptr;
	if (dlg.m_selectionOnly && m_doc.GetSelectionBounds(region)) pRegion = &region;

	unsigned pw = 0, ph = 0;
	if (!ge::RasterExportPixelSize(m_doc, opt, pRegion, pw, ph))
	{
		AfxMessageBox(_T("출력 크기가 너무 큽니다 (한 변 최대 16384px). 배율을 낮춰 주세요."));
		return;
	}

	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);
	const BOOL jpeg = opt.jpeg;
	CFileDialog fd(FALSE, jpeg ? _T("jpg") : _T("png"), suggested + (jpeg ? _T(".jpg") : _T(".png")), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
		jpeg ? _T("JPG 이미지 (*.jpg)|*.jpg||") : _T("PNG 이미지 (*.png)|*.png||"));
	if (fd.DoModal() != IDOK) return;

	CWaitCursor wait;
	std::wstring err;
	if (!ge::ExportRaster(m_doc, (LPCTSTR)fd.GetPathName(), opt, pRegion, &err))
	{
		CString msg;
		msg.Format(_T("이미지를 저장하지 못했습니다.\n%s"), err.c_str());
		AfxMessageBox(msg);
		return;
	}
	CString log;
	log.Format(_T("[내보내기] %s 저장: %s (%u x %u px, %.0fx, %d DPI)"), jpeg ? _T("JPG") : _T("PNG"), (LPCTSTR)fd.GetPathName(), pw, ph, static_cast<double>(opt.scale), dlg.m_dpi);
	LogAppEvent(log);
	SetAppStatusText(log);
}

ge::FrameNode* CM2DrawDoc::GetSelectedTopArtboard()
{
	auto* sel = GetPrimarySelection();
	for (ge::Node* p = sel; p; p = p->GetParent()) {
		if (auto* fr = dynamic_cast<ge::FrameNode*>(p)) {
			if (fr->GetParent() && fr->GetParent()->Kind() == ge::NodeKind::Layer && !fr->IsUiComponent()) return fr;
		}
	}
	return nullptr;
}

std::string CM2DrawDoc::BuildPreviewHtml()
{
	ge::HtmlExportOptions opt;
	opt.singleFile = true;
	opt.extractCssClasses = true;
	CString t = GetTitle();
	opt.title = (const char*)CStringA(t);
	opt.targetArtboard = GetSelectedTopArtboard();
	ge::HtmlExporter exporter;
	return exporter.Export(m_doc, opt).html;
}

void CM2DrawDoc::OnFileExportHtml()
{
	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);

	CFileDialog dlg(FALSE, _T("html"), suggested + _T(".html"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("HTML 파일 (*.html)|*.html||"));
	if (dlg.DoModal() != IDOK) return;

	ge::HtmlExportOptions opt;
	opt.singleFile = true; // 단일 파일 기본 (인라인 CSS & 이미지)
	opt.extractCssClasses = true;
	opt.title = (const char*)CStringA(suggested);

	// 선택된 최상위 아트보드가 있다면 그것만 내보내기, 없으면 문서 전체
	auto* sel = GetPrimarySelection();
	if (sel) {
		for (ge::Node* p = sel; p; p = p->GetParent()) {
			if (auto* fr = dynamic_cast<ge::FrameNode*>(p)) {
				if (fr->GetParent() && fr->GetParent()->Kind() == ge::NodeKind::Layer) {
					opt.targetArtboard = fr;
					break;
				}
			}
		}
	}

	ge::HtmlExporter exporter;
	if (!exporter.ExportToFile(m_doc, (LPCTSTR)dlg.GetPathName(), opt))
	{
		AfxMessageBox(_T("HTML 파일을 저장하지 못했습니다."));
		return;
	}

	CString log;
	log.Format(_T("[내보내기] 시맨틱 HTML/CSS 저장: %s%s"), (LPCTSTR)dlg.GetPathName(),
		opt.targetArtboard ? _T(" (선택된 아트보드)") : _T(" (전체)"));
	LogAppEvent(log);
	SetAppStatusText(log);
}

void CM2DrawDoc::OnFilePlaceImage()
{
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, kImageFilter);
	if (dlg.DoModal() != IDOK) return;
	PlaceImageFile(dlg.GetPathName());
}

// 선택된 이미지의 데이터만 교체: 위치/폭/맞춤은 유지하고 높이는 새 이미지 비율로 맞춤
bool CM2DrawDoc::ReplaceSelectedImage()
{
	auto* im = dynamic_cast<ge::ImageNode*>(GetPrimarySelection());
	if (!im) return false;
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, kImageFilter);
	if (dlg.DoModal() != IDOK) return false;
	auto data = LoadImageFile(dlg.GetPathName());
	if (!data) return false;
	im->SetData(data);
	if (data->pixelW > 0) im->SetSize(im->GetWidth(), im->GetWidth() * static_cast<float>(data->pixelH) / static_cast<float>(data->pixelW));
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"이미지 교체");
	LogAppEvent(_T("[이미지] 이미지 교체 완료"));
	UpdateShapePropertiesSelection(im, this);
	UpdateAllViews(nullptr);
	return true;
}
void CM2DrawDoc::OnUpdateTextToPath(CCmdUI* pCmdUI) { pCmdUI->Enable(CanConvertTextToPath()); }

bool CM2DrawDoc::SmoothPath(float tolerance)
{
	if (!CanSimplify()) return false;

	auto results = m_doc.SmoothSelection(tolerance);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"패스 부드럽게");
	UpdateAllViews(nullptr);
	return true;
}

bool CM2DrawDoc::SimplifyPath(float tolerance)
{
	if (!CanSimplify()) return false;

	auto results = m_doc.SimplifySelection(tolerance);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"패스 단순화");
	UpdateAllViews(nullptr);
	return true;
}

bool CM2DrawDoc::ReversePath()
{
	if (!CanReversePath()) return false;

	auto results = m_doc.ReversePathSelection();
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"패스 방향 반전");
	UpdateAllViews(nullptr);
	return true;
}

bool CM2DrawDoc::JoinPaths()
{
	if (!CanJoinPaths()) return false;

	ge::PathNode* joined = m_doc.JoinSelection();
	if (!joined) return false;

	UpdateShapePropertiesSelection(joined, this);
	CommitUndoPoint(L"패스 결합 (Join)");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

void CM2DrawDoc::OnAlignCommand(UINT nID)
{
	const bool distribute = (nID == ID_DISTRIBUTE_H || nID == ID_DISTRIBUTE_V);
	if (m_doc.GetSelection().size() < (distribute ? 3u : 2u)) return;

	const wchar_t* label = L"정렬";
	switch (nID)
	{
	case ID_ALIGN_LEFT:    m_doc.AlignSelection(ge::Document::AlignMode::Left, false);    label = L"왼쪽 정렬"; break;
	case ID_ALIGN_CENTERH: m_doc.AlignSelection(ge::Document::AlignMode::CenterH, false); label = L"가로 가운데 정렬"; break;
	case ID_ALIGN_RIGHT:   m_doc.AlignSelection(ge::Document::AlignMode::Right, false);   label = L"오른쪽 정렬"; break;
	case ID_ALIGN_TOP:     m_doc.AlignSelection(ge::Document::AlignMode::Top, false);     label = L"위쪽 정렬"; break;
	case ID_ALIGN_CENTERV: m_doc.AlignSelection(ge::Document::AlignMode::CenterV, false); label = L"세로 가운데 정렬"; break;
	case ID_ALIGN_BOTTOM:  m_doc.AlignSelection(ge::Document::AlignMode::Bottom, false);  label = L"아래쪽 정렬"; break;
	case ID_DISTRIBUTE_H:  m_doc.DistributeSelection(true);  label = L"가로 균등 분배"; break;
	case ID_DISTRIBUTE_V:  m_doc.DistributeSelection(false); label = L"세로 균등 분배"; break;
	}

	CString strLog;
	strLog.Format(_T("[정렬] %s (선택 %d개)"), label, static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(strLog);
	CommitUndoPoint(label);   // 위치가 실제로 바뀌지 않았으면 기록되지 않음
	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	UpdateAllViews(nullptr);
}

void CM2DrawDoc::OnUpdateAlignCommand(CCmdUI* pCmdUI)
{
	const bool distribute = (pCmdUI->m_nID == ID_DISTRIBUTE_H || pCmdUI->m_nID == ID_DISTRIBUTE_V);
	pCmdUI->Enable(m_doc.GetSelection().size() >= (distribute ? 3u : 2u));
}

void CM2DrawDoc::OnPathOutline()
{
	OutlineStroke();
}

void CM2DrawDoc::OnUpdatePathOutline(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanOutlineStroke());
}

void CM2DrawDoc::OnPathSimplify()
{
	SimplifyPath(2.0f);
}

void CM2DrawDoc::OnUpdatePathSimplify(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanSimplify());
}

void CM2DrawDoc::OnPathReverse()
{
	ReversePath();
}

void CM2DrawDoc::OnUpdatePathReverse(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanReversePath());
}

void CM2DrawDoc::OnPathJoin()
{
	JoinPaths();
}

void CM2DrawDoc::OnUpdatePathJoin(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanJoinPaths());
}



