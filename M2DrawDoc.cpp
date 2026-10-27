
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
#include <algorithm>
#include <vector>

#include <propkey.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CM2DrawDoc

IMPLEMENT_DYNCREATE(CM2DrawDoc, CDocument)

BEGIN_MESSAGE_MAP(CM2DrawDoc, CDocument)
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

	return TRUE;
}

const std::vector<std::unique_ptr<ge::Node>>& CM2DrawDoc::GetShapes() const
{
	static const std::vector<std::unique_ptr<ge::Node>> s_empty;
	const ge::LayerNode* pLayer = m_doc.GetActiveLayer();
	return pLayer ? pLayer->GetChildren() : s_empty;
}




// CM2DrawDoc serialization

void CM2DrawDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: 여기에 저장 코드를 추가합니다.
	}
	else
	{
		// TODO: 여기에 로딩 코드를 추가합니다.
	}
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
	if (!m_doc.GetSelection().empty())
		return true;

	CM2DrawApp* pApp = dynamic_cast<CM2DrawApp*>(AfxGetApp());
	if (pApp)
	{
		SelectTool* pSelect = pApp->GetToolManager().GetSelectTool();
		if (pSelect && !pSelect->GetSelectedIndices().empty())
			return true;
	}
	return false;
}

bool CM2DrawDoc::DeleteSelectedObjects()
{
	// 1. 선택된 노드들 수집
	std::vector<ge::Node*> targetNodes = m_doc.GetSelection();

	// SelectTool의 선택 인덱스에서도 추가 수집
	CM2DrawApp* pApp = dynamic_cast<CM2DrawApp*>(AfxGetApp());
	SelectTool* pSelect = pApp ? pApp->GetToolManager().GetSelectTool() : nullptr;
	if (pSelect)
	{
		auto* pActiveLayer = m_doc.GetActiveLayer();
		const auto& indices = pSelect->GetSelectedIndices();
		for (int idx : indices)
		{
			if (pActiveLayer && idx >= 0 && idx < static_cast<int>(pActiveLayer->GetChildren().size()))
			{
				ge::Node* pNode = pActiveLayer->GetChildren()[idx].get();
				if (std::find(targetNodes.begin(), targetNodes.end(), pNode) == targetNodes.end())
				{
					targetNodes.push_back(pNode);
				}
			}
		}
	}

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
	if (pSelect)
	{
		pSelect->ClearSelection();
	}
	UpdateShapePropertiesSelection(-1, this);

	// 6. UI 및 뷰 동기화
	CString strLog;
	strLog.Format(_T("[도형 삭제] 선택된 %d개 오브젝트 삭제 완료"), static_cast<int>(targetNodes.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

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
	CM2DrawApp* pApp = dynamic_cast<CM2DrawApp*>(AfxGetApp());
	SelectTool* pSelect = pApp ? pApp->GetToolManager().GetSelectTool() : nullptr;
	if (pSelect)
	{
		pSelect->ClearSelection();
	}
	UpdateShapePropertiesSelection(-1, this);

	CString strLog;
	strLog.Format(_T("[레이어 패널] 레이어 삭제됨: '%s' (하위 도형 %d개 포함)"), (LPCTSTR)layerName, childCount);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

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
	CM2DrawApp* pApp = dynamic_cast<CM2DrawApp*>(AfxGetApp());
	SelectTool* pSelect = pApp ? pApp->GetToolManager().GetSelectTool() : nullptr;

	// SelectTool 이 아직 활성 레이어 인덱스 기반이므로 활성 레이어 최상위 도형만 허용 (Step 0.2b 2단계에서 해제)
	auto* pActiveLayer = m_doc.GetActiveLayer();
	if (!pActiveLayer) return false;
	for (ge::Node* n : m_doc.GetSelection())
	{
		if (!n || n->GetParent() != pActiveLayer)
		{
			SetAppStatusText(_T("[도형 복제] 활성 레이어의 최상위 도형만 복제할 수 있습니다."));
			return false;
		}
	}

	std::vector<ge::Node*> clones = m_doc.DuplicateSelection();
	if (clones.empty())
		return false;

	std::vector<int> cloneIdx;
	for (ge::Node* n : clones)
		cloneIdx.push_back(pActiveLayer->IndexOf(n));

	if (pSelect)
	{
		auto& indices = pSelect->GetSelectedIndices();
		indices.clear();
		indices.insert(cloneIdx.begin(), cloneIdx.end());
		UpdateShapePropertiesSelection(pSelect->GetSelectedIndex(), this);
	}

	CString strLog;
	strLog.Format(_T("[도형 복제] %d개 오브젝트 복제 완료"), static_cast<int>(clones.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	SetModifiedFlag();
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}
	return true;
}
