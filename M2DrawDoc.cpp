
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

	m_shapes.clear();
	m_currentPath.reset();
	m_doc = ge::Document();
	m_currentPathNode.reset();

	return TRUE;
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
			if (idx >= 0 && idx < static_cast<int>(m_shapes.size()) && m_shapes[idx])
			{
				ge::Node* pNode = m_shapes[idx].get();
				if (std::find(targetNodes.begin(), targetNodes.end(), pNode) == targetNodes.end())
				{
					targetNodes.push_back(pNode);
				}
			}
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

	// 2. 삭제 대상의 이름 보존 (이름 기준 동기화용)
	std::vector<std::wstring> targetNames;
	for (auto* n : targetNodes)
	{
		if (n && !n->GetName().empty())
		{
			targetNames.push_back(n->GetName());
		}
	}

	// 3. m_doc (신형 씬그래프)에서 노드 제거
	for (auto* n : targetNodes)
	{
		if (n)
		{
			m_doc.RemoveNode(n);
		}
	}

	// 4. m_shapes (구형 브릿지 벡터)에서 동시 제거
	for (auto it = m_shapes.begin(); it != m_shapes.end(); )
	{
		bool bFound = false;
		for (auto* n : targetNodes)
		{
			if (it->get() == n)
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			for (const auto& name : targetNames)
			{
				if ((*it)->GetName() == name)
				{
					bFound = true;
					break;
				}
			}
		}

		if (bFound)
		{
			it = m_shapes.erase(it);
		}
		else
		{
			++it;
		}
	}

	// 5. 선택 상태 초기화
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

	// 레이어에 속한 자식들의 이름 및 포인터 수집
	std::vector<ge::Node*> childNodes;
	std::vector<std::wstring> childNames;
	for (const auto& child : pActive->GetChildren())
	{
		if (child)
		{
			childNodes.push_back(child.get());
			if (!child->GetName().empty())
				childNames.push_back(child->GetName());
		}
	}

	// m_doc에서 레이어 제거
	if (!m_doc.RemoveLayer(pActive))
		return false;

	// m_shapes에서 해당 레이어 자식 도형들도 제거
	for (auto it = m_shapes.begin(); it != m_shapes.end(); )
	{
		bool bFound = false;
		for (auto* n : childNodes)
		{
			if (it->get() == n) { bFound = true; break; }
		}
		if (!bFound)
		{
			for (const auto& name : childNames)
			{
				if ((*it)->GetName() == name)
				{
					bFound = true;
					break;
				}
			}
		}
		if (bFound)
			it = m_shapes.erase(it);
		else
			++it;
	}

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
	strLog.Format(_T("[레이어 패널] 레이어 삭제됨: '%s' (하위 도형 %d개 포함)"), (LPCTSTR)layerName, static_cast<int>(childNodes.size()));
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
