
// MainFrm.cpp: CMainFrame 클래스의 구현
//

#include "pch.h"
#include "framework.h"
#include "M2Draw.h"

#include "MainFrm.h"
#include "M2DrawDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CMainFrame

IMPLEMENT_DYNAMIC(CMainFrame, CMDIFrameWndEx)

const int  iMaxUserToolbars = 10;
const UINT uiFirstUserToolBarId = AFX_IDW_CONTROLBAR_FIRST + 40;
const UINT uiLastUserToolBarId = uiFirstUserToolBarId + iMaxUserToolbars - 1;

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWndEx)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_COMMAND(ID_WINDOW_MANAGER, &CMainFrame::OnWindowManager)
	ON_COMMAND(ID_VIEW_CUSTOMIZE, &CMainFrame::OnViewCustomize)
	ON_REGISTERED_MESSAGE(AFX_WM_CREATETOOLBAR, &CMainFrame::OnToolbarCreateNew)
	ON_WM_SETTINGCHANGE()
	ON_COMMAND_RANGE(ID_OPT_FIRST, ID_OPT_LAST, &CMainFrame::OnOptionBarCommit)
	ON_UPDATE_COMMAND_UI_RANGE(ID_OPT_LABEL, ID_OPT_LAST, &CMainFrame::OnUpdateOptionBar)
	ON_COMMAND_RANGE(ID_TOOL_RECT, ID_TOOL_ZOOMOUT, &CMainFrame::OnSubToolCommand)
	ON_COMMAND(ID_TOOL_POLYLINE, &CMainFrame::OnSubPolyline)
	ON_COMMAND(ID_SUB_OUTLINE, &CMainFrame::OnSubOutline)
	ON_COMMAND(ID_SUB_SIMPLIFY, &CMainFrame::OnSubSimplify)
	ON_COMMAND(ID_SUB_SMOOTH, &CMainFrame::OnSubSmooth)
	ON_COMMAND(ID_SUB_APPLY, &CMainFrame::OnSubApply)
	ON_COMMAND(ID_SUB_VALUE, &CMainFrame::OnSubApply)   // 입력창에서 Enter = 적용
	ON_UPDATE_COMMAND_UI(ID_SUB_VALUE, &CMainFrame::OnUpdateSubValue)
	ON_COMMAND(ID_SUB_CANCEL, &CMainFrame::OnSubCancel)
	ON_UPDATE_COMMAND_UI_RANGE(ID_TOOL_RECT, ID_TOOL_ZOOMOUT, &CMainFrame::OnUpdateSubToolCommand)
END_MESSAGE_MAP()

static UINT indicators[] =
{
	ID_SEPARATOR,           // 상태 줄 표시기
	ID_INDICATOR_CAPS,
	ID_INDICATOR_NUM,
	ID_INDICATOR_SCRL,
};

// CMainFrame 생성/소멸

CMainFrame::CMainFrame() noexcept
	: m_wndProperties(CD2DPropertyView::MODE_SHAPE)
	, m_wndGridProp(CD2DPropertyView::MODE_GRID)
	, m_wndCanvasProp(CD2DPropertyView::MODE_CANVAS)
{
}

CMainFrame::~CMainFrame()
{
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CMDIFrameWndEx::OnCreate(lpCreateStruct) == -1)
		return -1;

	BOOL bNameValid;

	CMDITabInfo mdiTabParams;
	mdiTabParams.m_style = CMFCTabCtrl::STYLE_3D_ONENOTE; // 사용할 수 있는 다른 스타일...
	mdiTabParams.m_bActiveTabCloseButton = TRUE;      // FALSE로 설정하여 탭 영역 오른쪽에 닫기 단추를 배치합니다.
	mdiTabParams.m_bTabIcons = FALSE;    // TRUE로 설정하여 MDI 탭의 문서 아이콘을 활성화합니다.
	mdiTabParams.m_bAutoColor = TRUE;    // FALSE로 설정하여 MDI 탭의 자동 색 지정을 비활성화합니다.
	mdiTabParams.m_bDocumentMenu = TRUE; // 탭 영역의 오른쪽 가장자리에 문서 메뉴를 활성화합니다.
	EnableMDITabbedGroups(TRUE, mdiTabParams);

	if (!m_wndMenuBar.Create(this))
	{
		TRACE0("메뉴 모음을 만들지 못했습니다.\n");
		return -1;      // 만들지 못했습니다.
	}

	m_wndMenuBar.SetPaneStyle(m_wndMenuBar.GetPaneStyle() | CBRS_SIZE_DYNAMIC | CBRS_TOOLTIPS | CBRS_FLYBY);

	// 메뉴 모음을 활성화해도 포커스가 이동하지 않게 합니다.
	CMFCPopupMenu::SetForceMenuFocus(FALSE);

	if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_GRIPPER | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC) ||
		!m_wndToolBar.LoadToolBar(theApp.m_bHiColorIcons ? IDR_MAINFRAME_256 : IDR_MAINFRAME))
	{
		TRACE0("도구 모음을 만들지 못했습니다.\n");
		return -1;      // 만들지 못했습니다.
	}

	CString strToolBarName;
	bNameValid = strToolBarName.LoadString(IDS_TOOLBAR_STANDARD);
	ASSERT(bNameValid);
	m_wndToolBar.SetWindowText(strToolBarName);

	CString strCustomize;
	bNameValid = strCustomize.LoadString(IDS_TOOLBAR_CUSTOMIZE);
	ASSERT(bNameValid);
	m_wndToolBar.EnableCustomizeButton(TRUE, ID_VIEW_CUSTOMIZE, strCustomize);

	// 사용자 정의 도구 모음 작업을 허용합니다.
	InitUserToolbars(nullptr, uiFirstUserToolBarId, uiLastUserToolBarId);

	// 상단 도구 옵션바 (MFC 기본 컨트롤 기반)
	if (!m_wndOptionBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC, CRect(1, 1, 1, 1), AFX_IDW_CONTROLBAR_FIRST + 60))
	{
		TRACE0("도구 옵션바를 만들지 못했습니다.\n");
		return -1;
	}
	m_wndOptionBar.SetWindowText(_T("도구 옵션"));

	// 상단 서브도구 툴바 (윈도우 비트맵 리소스 IDB_SUBTOOLS 기반 표준 CMFCToolBar)
	if (!m_wndSubToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC, CRect(1, 1, 1, 1), AFX_IDW_CONTROLBAR_FIRST + 61))
	{
		TRACE0("서브도구 모음을 만들지 못했습니다.\n");
		return -1;
	}
	m_wndSubToolBar.SetWindowText(_T("서브 도구"));
	m_wndSubToolBar.LoadBitmap(IDB_SUBTOOLS, 0, 0, TRUE /* 잠금 */);

	if (!m_wndStatusBar.Create(this))
	{
		TRACE0("상태 표시줄을 만들지 못했습니다.\n");
		return -1;      // 만들지 못했습니다.
	}
	m_wndStatusBar.SetIndicators(indicators, sizeof(indicators)/sizeof(UINT));

	// TODO: 도구 모음 및 메뉴 모음을 도킹할 수 없게 하려면 이 다섯 줄을 삭제하십시오.
	m_wndMenuBar.EnableDocking(CBRS_ALIGN_ANY);
	m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
	m_wndSubToolBar.EnableDocking(CBRS_ALIGN_TOP | CBRS_ALIGN_BOTTOM);
	m_wndOptionBar.EnableDocking(CBRS_ALIGN_TOP | CBRS_ALIGN_BOTTOM); // 세로 도킹 금지
	EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndMenuBar);
	DockPane(&m_wndToolBar);
	DockPane(&m_wndSubToolBar);   // 서브도구 바는 버튼 수가 많아 메인 툴바 옆이 아니라 자기 줄에 도킹
	DockPane(&m_wndOptionBar);
	m_wndOptionBar.Attach();

	// 도구 변경 리스너 등록 및 초기 서브도구 툴바 구성
	GetApp()->GetToolManager().AddListener(this);
	UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());


	// Visual Studio 2005 스타일 도킹 창 동작을 활성화합니다.
	CDockingManager::SetDockingMode(DT_SMART);
	// Visual Studio 2005 스타일 도킹 창 자동 숨김 동작을 활성화합니다.
	EnableAutoHidePanes(CBRS_ALIGN_ANY);

	// 메뉴 항목 이미지를 로드합니다(표준 도구 모음에 없음).
	CMFCToolBar::AddToolBarForImageCollection(IDR_MENU_IMAGES, theApp.m_bHiColorIcons ? IDB_MENU_IMAGES_24 : 0);

	// 도킹 창을 만듭니다.
	if (!CreateDockingWindows())
	{
		TRACE0("도킹 창을 만들지 못했습니다.\n");
		return -1;
	}

	theApp.CleanState();

	m_wndToolBox.EnableDocking(CBRS_ALIGN_LEFT);
	DockPane(&m_wndToolBox);

	m_wndOutput.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndOutput);

	m_wndProperties.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndProperties);

	m_wndGridProp.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndGridProp);

	m_wndCanvasProp.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndCanvasProp);

	m_wndLayers.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndLayers);

	// Grid Settings를 Object Settings 아래로 도킹, Canvas Settings를 Grid Settings와 탭으로 그룹핑
	m_wndGridProp.DockToWindow(&m_wndProperties, CBRS_ALIGN_BOTTOM);
	m_wndCanvasProp.DockToWindow(&m_wndGridProp, CBRS_ALIGN_BOTTOM);

	// Layers and Objects를 Object Settings의 탭으로 안전하게 병합
	m_wndLayers.DockToWindow(&m_wndProperties, CBRS_ALIGN_BOTTOM);
	m_wndLayers.AttachToTabWnd(&m_wndProperties, DM_SHOW, FALSE);


	// 모든 사용자 인터페이스 요소를 그리는 데 사용하는 비주얼 관리자를 설정합니다.
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	// 향상된 창 관리 대화 상자를 활성화합니다.
	EnableWindowsDialog(ID_WINDOW_MANAGER, ID_WINDOW_MANAGER, TRUE);

	// 도구 모음 및 도킹 창 메뉴 바꾸기를 활성화합니다.
	EnablePaneMenu(TRUE, ID_VIEW_CUSTOMIZE, strCustomize, ID_VIEW_TOOLBAR);

	// 빠른(<Alt> 키를 누른 채 끌기) 도구 모음 사용자 지정을 활성화합니다.
	CMFCToolBar::EnableQuickCustomization();

	if (CMFCToolBar::GetUserImages() == nullptr)
	{
		// 사용자 정의 도구 모음 이미지를 로드합니다.
		if (m_UserImages.Load(_T(".\\UserImages.bmp")))
		{
			CMFCToolBar::SetUserImages(&m_UserImages);
		}
	}

	// 메뉴 개인 설정을 활성화합니다(가장 최근에 사용한 명령).
	// TODO: 사용자의 기본 명령을 정의하여 각 풀다운 메뉴에 하나 이상의 기본 명령을 포함시킵니다.
	CList<UINT, UINT> lstBasicCommands;

	lstBasicCommands.AddTail(ID_FILE_NEW);
	lstBasicCommands.AddTail(ID_FILE_OPEN);
	lstBasicCommands.AddTail(ID_FILE_SAVE);
	lstBasicCommands.AddTail(ID_FILE_PRINT);
	lstBasicCommands.AddTail(ID_APP_EXIT);
	lstBasicCommands.AddTail(ID_EDIT_CUT);
	lstBasicCommands.AddTail(ID_EDIT_PASTE);
	lstBasicCommands.AddTail(ID_EDIT_UNDO);
	lstBasicCommands.AddTail(ID_APP_ABOUT);
	lstBasicCommands.AddTail(ID_VIEW_STATUS_BAR);
	lstBasicCommands.AddTail(ID_VIEW_TOOLBAR);
	lstBasicCommands.AddTail(ID_SORTING_SORTALPHABETIC);
	lstBasicCommands.AddTail(ID_SORTING_SORTBYTYPE);
	lstBasicCommands.AddTail(ID_SORTING_SORTBYACCESS);
	lstBasicCommands.AddTail(ID_SORTING_GROUPBYTYPE);

	CMFCToolBar::SetBasicCommands(lstBasicCommands);

	// 창 제목 표시줄에서 문서 이름 및 애플리케이션 이름의 순서를 전환합니다.
	// 문서 이름이 축소판 그림과 함께 표시되므로 작업 표시줄의 기능성이 개선됩니다.
	ModifyStyle(0, FWS_PREFIXTITLE);

	return 0;
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if( !CMDIFrameWndEx::PreCreateWindow(cs) )
		return FALSE;
	// TODO: CREATESTRUCT cs를 수정하여 여기에서
	//  Window 클래스 또는 스타일을 수정합니다.

	return TRUE;
}

BOOL CMainFrame::CreateDockingWindows()
{
	BOOL bNameValid;

	// 좌측 슬림 툴박스를 만듭니다. (폭 44px)
	if (!m_wndToolBox.Create(_T("도구"), this, CRect(0, 0, 44, 400), TRUE, 32800, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_LEFT | CBRS_FLOAT_MULTI))
	{
		TRACE0("도구 상자 창을 만들지 못했습니다.\n");
		return FALSE;
	}

	// 출력 창을 만듭니다.
	CString strOutputWnd;
	bNameValid = strOutputWnd.LoadString(IDS_OUTPUT_WND);
	ASSERT(bNameValid);
	if (!m_wndOutput.Create(strOutputWnd, this, CRect(0, 0, 100, 100), TRUE, ID_VIEW_OUTPUTWND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_BOTTOM | CBRS_FLOAT_MULTI))
	{
		TRACE0("출력 창을 만들지 못했습니다.\n");
		return FALSE; // 만들지 못했습니다.
	}

	// Object Settings 창을 만듭니다. (너비 260px)
	CString strPropertiesWnd = _T("Object Settings");
	if (!m_wndProperties.Create(strPropertiesWnd, this, CRect(0, 0, 260, 300), TRUE, ID_VIEW_PROPERTIESWND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Object Settings 창을 만들지 못했습니다.\n");
		return FALSE;
	}

	// Grid Settings 창을 만듭니다. (너비 260px)
	CString strGridWnd = _T("Grid Settings");
	if (!m_wndGridProp.Create(strGridWnd, this, CRect(0, 0, 260, 300), TRUE, ID_VIEW_PROPERTIESWND + 1, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Grid Settings 창을 만들지 못했습니다.\n");
		return FALSE;
	}

	// Canvas Settings 창을 만듭니다. (너비 260px)
	CString strCanvasWnd = _T("Canvas Settings");
	if (!m_wndCanvasProp.Create(strCanvasWnd, this, CRect(0, 0, 260, 300), TRUE, ID_VIEW_PROPERTIESWND + 2, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Canvas Settings 창을 만들지 못했습니다.\n");
		return FALSE;
	}

	// Layers and Objects 창을 만듭니다. (너비 260px)
	CString strLayersWnd = _T("Layers and Objects");
	if (!m_wndLayers.Create(strLayersWnd, this, CRect(0, 0, 260, 300), TRUE, ID_VIEW_PROPERTIESWND + 3, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Layers and Objects 창을 만들지 못했습니다.\n");
		return FALSE;
	}

	SetDockingWindowIcons(theApp.m_bHiColorIcons);
	return TRUE;
}

void CMainFrame::SetDockingWindowIcons(BOOL bHiColorIcons)
{
	HICON hOutputBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_OUTPUT_WND_HC : IDI_OUTPUT_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndOutput.SetIcon(hOutputBarIcon, FALSE);

	HICON hPropertiesBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_PROPERTIES_WND_HC : IDI_PROPERTIES_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndProperties.SetIcon(hPropertiesBarIcon, FALSE);
	m_wndGridProp.SetIcon(hPropertiesBarIcon, FALSE);
	m_wndCanvasProp.SetIcon(hPropertiesBarIcon, FALSE);
	m_wndLayers.SetIcon(hPropertiesBarIcon, FALSE);

	UpdateMDITabbedBarsIcons();
}

// CMainFrame 진단

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
	CMDIFrameWndEx::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CMDIFrameWndEx::Dump(dc);
}
#endif //_DEBUG


// CMainFrame 메시지 처리기

void CMainFrame::OnWindowManager()
{
	ShowWindowsDialog();
}

void CMainFrame::OnViewCustomize()
{
	CMFCToolBarsCustomizeDialog* pDlgCust = new CMFCToolBarsCustomizeDialog(this, TRUE /* 메뉴를 검색합니다. */);
	pDlgCust->EnableUserDefinedToolbars();
	pDlgCust->Create();
}

LRESULT CMainFrame::OnToolbarCreateNew(WPARAM wp,LPARAM lp)
{
	LRESULT lres = CMDIFrameWndEx::OnToolbarCreateNew(wp,lp);
	if (lres == 0)
	{
		return 0;
	}

	CMFCToolBar* pUserToolbar = (CMFCToolBar*)lres;
	ASSERT_VALID(pUserToolbar);

	BOOL bNameValid;
	CString strCustomize;
	bNameValid = strCustomize.LoadString(IDS_TOOLBAR_CUSTOMIZE);
	ASSERT(bNameValid);

	pUserToolbar->EnableCustomizeButton(TRUE, ID_VIEW_CUSTOMIZE, strCustomize);
	return lres;
}


BOOL CMainFrame::LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd, CCreateContext* pContext)
{
	// 기본 클래스가 실제 작업을 수행합니다.

	if (!CMDIFrameWndEx::LoadFrame(nIDResource, dwDefaultStyle, pParentWnd, pContext))
	{
		return FALSE;
	}


	// 모든 사용자 도구 모음에 사용자 지정 단추를 활성화합니다.
	BOOL bNameValid;
	CString strCustomize;
	bNameValid = strCustomize.LoadString(IDS_TOOLBAR_CUSTOMIZE);
	ASSERT(bNameValid);

	for (int i = 0; i < iMaxUserToolbars; i ++)
	{
		CMFCToolBar* pUserToolbar = GetUserToolBarByIndex(i);
		if (pUserToolbar != nullptr)
		{
			pUserToolbar->EnableCustomizeButton(TRUE, ID_VIEW_CUSTOMIZE, strCustomize);
		}
	}

	return TRUE;
}


void CMainFrame::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CMDIFrameWndEx::OnSettingChange(uFlags, lpszSection);
	m_wndOutput.UpdateFonts();
}

void CMainFrame::OnOptionBarCommit(UINT nID)
{
	m_wndOptionBar.OnOptionCommit(nID);
}

void CMainFrame::OnUpdateOptionBar(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

CM2DrawDoc* CMainFrame::GetActiveM2DrawDoc()
{
	CMDIChildWndEx* pChild = DYNAMIC_DOWNCAST(CMDIChildWndEx, MDIGetActive());
	if (pChild)
	{
		return DYNAMIC_DOWNCAST(CM2DrawDoc, pChild->GetActiveDocument());
	}
	return nullptr;
}

void CMainFrame::OnDestroy()
{
	GetApp()->GetToolManager().RemoveListener(this);
	CMDIFrameWndEx::OnDestroy();
}

void CMainFrame::OnToolChanged(ToolType newTool)
{
	UpdateSubToolBar(newTool);
}

void CMainFrame::UpdateSubToolBar(ToolType curTool)
{
	if (!m_wndSubToolBar.GetSafeHwnd())
		return;

	// (버튼 구성이 이전과 같으면 아래에서 바로 반환: 드래그 중 반복 호출되어도 툴바를 다시 만들지 않음)

	// 다중 선택 등으로 불리언 연산 또는 클리핑 마스크가 가능한 상태인지 확인
	CM2DrawDoc* pDoc = GetActiveM2DrawDoc();
	const bool bCanBoolean = pDoc && pDoc->CanBoolean();
	const bool bCanMakeMask = pDoc && pDoc->CanMakeClipMask();
	const bool bCanReleaseMask = pDoc && pDoc->CanReleaseClipMask();
	const bool bCanOutline = pDoc && pDoc->CanOutlineStroke();
	const bool bCanSimplify = pDoc && pDoc->CanSimplify();
	const bool bCanReverse = pDoc && pDoc->CanReversePath();
	const bool bCanJoin = pDoc && pDoc->CanJoinPaths();
	const bool bIsPathSelected = bCanOutline || bCanSimplify || bCanReverse || bCanJoin;

	// 선택에 닫힌 패스(면)가 있는지: 열린 선만 선택하면 불리언 연산은 의미가 없으므로 숨김
	bool hasClosedPath = false;
	if (pDoc)
		for (ge::Node* n : pDoc->GetDoc().GetSelection())
			if (n && n->Kind() == ge::NodeKind::Path)
				for (const auto& sp : static_cast<ge::PathNode*>(n)->GetPaths())
					if (sp.closed) hasClosedPath = true;

	if (m_subInputMode != SubInputMode::None && !(bCanOutline || bCanSimplify))
		m_subInputMode = SubInputMode::None;   // 대상 선택이 사라졌으면 입력 모드 해제

	// 도구별 서브도구 버튼 목록 구성 (필요한 메뉴만 동적으로)
	std::vector<UINT> btnIds;
	const bool boolTool = curTool == ToolType::ActionBooleanUnion || curTool == ToolType::ActionBooleanSubtract ||
		curTool == ToolType::ActionBooleanIntersect || curTool == ToolType::ActionBooleanExclude || curTool == ToolType::ActionBooleanTrim;
	if (bCanReleaseMask)
	{
		btnIds = { ID_OBJECT_RELEASE_MASK };   // 이미 마스크가 적용된 그룹이 선택된 경우 해제 버튼 노출
	}
	else if (m_subInputMode != SubInputMode::None)
	{
		btnIds = { ID_SUB_VALUE, ID_SUB_APPLY, ID_SUB_CANCEL };   // 다른 메뉴는 밀려나고 값 입력 + 적용/취소만 표시
	}
	else
	{
		if ((bCanBoolean && hasClosedPath) || boolTool)
			btnIds.insert(btnIds.end(), { ID_BOOLEAN_UNION, ID_BOOLEAN_SUBTRACT, ID_BOOLEAN_INTERSECT, ID_BOOLEAN_EXCLUDE, ID_BOOLEAN_TRIM });
		if (bCanMakeMask || curTool == ToolType::ActionClipMask)
			btnIds.push_back(ID_OBJECT_CLIP_MASK);
		if (pDoc && pDoc->GetDoc().GetSelection().size() >= 2)
			btnIds.insert(btnIds.end(), { ID_ALIGN_LEFT, ID_ALIGN_CENTERH, ID_ALIGN_RIGHT, ID_ALIGN_TOP, ID_ALIGN_CENTERV, ID_ALIGN_BOTTOM, ID_DISTRIBUTE_H, ID_DISTRIBUTE_V });
		if (bIsPathSelected || curTool == ToolType::Pen)
			btnIds.insert(btnIds.end(), { ID_SUB_OUTLINE, ID_SUB_SIMPLIFY, ID_SUB_SMOOTH, ID_PATH_REVERSE, ID_PATH_JOIN });
	}

	if (btnIds.empty())
	{
		switch (curTool)
		{
		case ToolType::Rect:
		case ToolType::Triangle:
			btnIds = { ID_TOOL_RECT, ID_TOOL_TRIANGLE };
			break;
		case ToolType::Polygon:
		case ToolType::Polyline:
		case ToolType::Star:
		case ToolType::Gear:
		case ToolType::Spiral:
		case ToolType::Heart:
			btnIds = { ID_TOOL_POLYGON, ID_TOOL_POLYLINE, ID_TOOL_STAR, ID_TOOL_GEAR, ID_TOOL_SPIRAL, ID_TOOL_HEART };
			break;
		case ToolType::ZoomIn:
		case ToolType::ZoomOut:
			btnIds = { ID_TOOL_ZOOMIN, ID_TOOL_ZOOMOUT };
			break;
		case ToolType::ActionGroup:
		case ToolType::ActionUngroup:
			btnIds = { ID_OBJECT_GROUP, ID_OBJECT_UNGROUP };
			break;
		case ToolType::ActionToFront:
		case ToolType::ActionForward:
		case ToolType::ActionBackward:
		case ToolType::ActionToBack:
			btnIds = { ID_OBJECT_FRONT, ID_OBJECT_FORWARD, ID_OBJECT_BACKWARD, ID_OBJECT_BACK };
			break;
		default:
			// 기본: 도형 생성 서브도구
			btnIds = { ID_TOOL_RECT, ID_TOOL_TRIANGLE, ID_TOOL_POLYGON, ID_TOOL_POLYLINE, ID_TOOL_STAR, ID_TOOL_GEAR, ID_TOOL_SPIRAL, ID_TOOL_HEART };
			break;
		}
	}

	// 구성이 그대로면 재구성(버튼 삭제/생성 + 프레임 레이아웃 재계산)을 건너뜀
	if (btnIds == m_lastSubIds && m_subInputMode == m_lastSubMode && m_wndSubToolBar.GetCount() > 0)
		return;
	m_lastSubIds = btnIds;
	m_lastSubMode = m_subInputMode;
	m_wndSubToolBar.RemoveAllButtons();

	for (UINT id : btnIds)
	{
		int imgIdx = -1;
		switch (id)
		{
		case ID_TOOL_RECT:          imgIdx = 0; break;
		case ID_TOOL_TRIANGLE:      imgIdx = 1; break;
		case ID_TOOL_POLYGON:       imgIdx = 2; break;
		case ID_TOOL_STAR:          imgIdx = 3; break;
		case ID_TOOL_GEAR:          imgIdx = 4; break;
		case ID_TOOL_SPIRAL:        imgIdx = 5; break;
		case ID_TOOL_HEART:         imgIdx = 6; break;
		case ID_TOOL_ZOOMIN:        imgIdx = 7; break;
		case ID_TOOL_ZOOMOUT:       imgIdx = 8; break;
		case ID_OBJECT_GROUP:       imgIdx = 9; break;
		case ID_OBJECT_UNGROUP:     imgIdx = 10; break;
		case ID_BOOLEAN_UNION:      imgIdx = 11; break;
		case ID_BOOLEAN_SUBTRACT:   imgIdx = 12; break;
		case ID_BOOLEAN_INTERSECT:  imgIdx = 13; break;
		case ID_BOOLEAN_EXCLUDE:    imgIdx = 14; break;
		case ID_BOOLEAN_TRIM:       imgIdx = 15; break;
		case ID_OBJECT_CLIP_MASK:   imgIdx = 16; break;
		case ID_OBJECT_RELEASE_MASK:imgIdx = 17; break;
		case ID_PATH_OUTLINE:
		case ID_SUB_OUTLINE:        imgIdx = 18; break;
		case ID_PATH_SIMPLIFY:
		case ID_SUB_SIMPLIFY:       imgIdx = 19; break;
		case ID_PATH_REVERSE:       imgIdx = 20; break;
		case ID_PATH_JOIN:          imgIdx = 21; break;
		case ID_TOOL_POLYLINE:      imgIdx = 22; break;
		case ID_SUB_SMOOTH:         imgIdx = 31; break;
		case ID_ALIGN_LEFT:         imgIdx = 23; break;
		case ID_ALIGN_CENTERH:      imgIdx = 24; break;
		case ID_ALIGN_RIGHT:        imgIdx = 25; break;
		case ID_ALIGN_TOP:          imgIdx = 26; break;
		case ID_ALIGN_CENTERV:      imgIdx = 27; break;
		case ID_ALIGN_BOTTOM:       imgIdx = 28; break;
		case ID_DISTRIBUTE_H:       imgIdx = 29; break;
		case ID_DISTRIBUTE_V:       imgIdx = 30; break;
		default: break;
		}

		if (id == ID_SUB_VALUE)
		{
			CMFCToolBarEditBoxButton edit(id, -1, ES_AUTOHSCROLL | WS_BORDER, 64);
			m_wndSubToolBar.InsertButton(edit);
			continue;
		}
		if (id == ID_SUB_APPLY || id == ID_SUB_CANCEL)
		{
			CMFCToolBarButton txt(id, -1, id == ID_SUB_APPLY ? _T("\u2713 \uC801\uC6A9") : _T("\u2715 \uCDE8\uC18C"));
			m_wndSubToolBar.InsertButton(txt);
			continue;
		}

		CMFCToolBarButton btn(id, imgIdx);
		m_wndSubToolBar.InsertButton(btn);

		// 툴팁: 이미지 버튼에 마우스를 올리면 설명 표시
		const wchar_t* tip = nullptr;
		switch (id)
		{
		case ID_TOOL_RECT: tip = L"사각형"; break;
		case ID_TOOL_TRIANGLE: tip = L"삼각형"; break;
		case ID_TOOL_POLYGON: tip = L"정다각형"; break;
		case ID_TOOL_POLYLINE: tip = L"열린 다각형 (폴리라인): 클릭으로 꼭짓점, 더블클릭/우클릭으로 완성"; break;
		case ID_TOOL_STAR: tip = L"별"; break;
		case ID_TOOL_GEAR: tip = L"톱니바퀴"; break;
		case ID_TOOL_SPIRAL: tip = L"나선"; break;
		case ID_TOOL_HEART: tip = L"하트"; break;
		case ID_TOOL_ZOOMIN: tip = L"확대"; break;
		case ID_TOOL_ZOOMOUT: tip = L"축소"; break;
		case ID_OBJECT_GROUP: tip = L"그룹 (Ctrl+G)"; break;
		case ID_OBJECT_UNGROUP: tip = L"그룹 해제 (Ctrl+Shift+G)"; break;
		case ID_BOOLEAN_UNION: tip = L"합치기 (Union)"; break;
		case ID_BOOLEAN_SUBTRACT: tip = L"빼기 (Subtract)"; break;
		case ID_BOOLEAN_INTERSECT: tip = L"교차 (Intersect)"; break;
		case ID_BOOLEAN_EXCLUDE: tip = L"제외 (Exclude)"; break;
		case ID_BOOLEAN_TRIM: tip = L"자르기 (Trim)"; break;
		case ID_OBJECT_CLIP_MASK: tip = L"클리핑 마스크 만들기 (Ctrl+7)"; break;
		case ID_OBJECT_RELEASE_MASK: tip = L"클리핑 마스크 해제 (Ctrl+Alt+7)"; break;
		case ID_SUB_OUTLINE: tip = L"외곽선 확장: 선을 면(닫힌 패스)으로 변환 (두께 입력 후 적용)"; break;
		case ID_SUB_SIMPLIFY: tip = L"단순화 / 부드럽게: 정점 줄이기 (허용 오차 입력 후 적용)"; break;
		case ID_PATH_REVERSE: tip = L"방향 반전"; break;
		case ID_PATH_JOIN: tip = L"패스 결합 (Ctrl+J)"; break;
		case ID_ALIGN_LEFT: tip = L"왼쪽 정렬"; break;
		case ID_ALIGN_CENTERH: tip = L"가로 가운데 정렬"; break;
		case ID_ALIGN_RIGHT: tip = L"오른쪽 정렬"; break;
		case ID_ALIGN_TOP: tip = L"위쪽 정렬"; break;
		case ID_ALIGN_CENTERV: tip = L"세로 가운데 정렬"; break;
		case ID_ALIGN_BOTTOM: tip = L"아래쪽 정렬"; break;
		case ID_DISTRIBUTE_H: tip = L"가로 균등 분배 (3개 이상)"; break;
		case ID_DISTRIBUTE_V: tip = L"세로 균등 분배 (3개 이상)"; break;
		case ID_OBJECT_FRONT: tip = L"맨 앞으로 (Ctrl+Shift+])"; break;
		case ID_OBJECT_FORWARD: tip = L"앞으로 (Ctrl+])"; break;
		case ID_OBJECT_BACKWARD: tip = L"뒤로 (Ctrl+[)"; break;
		case ID_OBJECT_BACK: tip = L"맨 뒤로 (Ctrl+Shift+[)"; break;
		}
		if (tip)
		{
			const int idx = m_wndSubToolBar.CommandToIndex(id);
			if (idx >= 0) m_wndSubToolBar.SetToolBarBtnText(idx, tip, FALSE, TRUE);
		}
	}

	// 입력 모드: 입력창에 기본값 채우기
	if (m_subInputMode != SubInputMode::None)
	{
		const int vi = m_wndSubToolBar.CommandToIndex(ID_SUB_VALUE);
		if (vi >= 0)
			if (auto* eb = DYNAMIC_DOWNCAST(CMFCToolBarEditBoxButton, m_wndSubToolBar.GetButton(vi)))
			{
				CString v; v.Format(_T("%g"), m_subInputValue);
				eb->SetContents(v);
			}
	}

	m_wndSubToolBar.AdjustLayout();
	m_wndSubToolBar.AdjustSizeImmediate();   // 버튼 수가 바뀌면 바 폭을 즉시 다시 계산 (안 하면 처음 폭에서 잘림)
	RecalcLayout();
	m_wndSubToolBar.Invalidate();
}

// 명령 핸들러가 없는 툴바 버튼(입력창)은 MFC 가 자동으로 비활성화하므로 항상 활성으로 표시
void CMainFrame::OnUpdateSubValue(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OnSubOutline()
{
	CM2DrawDoc* pDoc = GetActiveM2DrawDoc();
	if (!pDoc || !pDoc->CanOutlineStroke()) return;
	float w = 2.0f;
	for (ge::Node* n : pDoc->GetDoc().GetSelection())
		if (n && n->Kind() == ge::NodeKind::Path) { w = static_cast<ge::PathNode*>(n)->GetStyle().stroke.width; break; }
	m_subInputMode = SubInputMode::Outline;
	m_subInputValue = w;
	UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
}

void CMainFrame::OnSubSimplify()
{
	CM2DrawDoc* pDoc = GetActiveM2DrawDoc();
	if (!pDoc || !pDoc->CanSimplify()) return;
	m_subInputMode = SubInputMode::Simplify;
	m_subInputValue = 2.0f;
	UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
}

void CMainFrame::OnSubSmooth()
{
	CM2DrawDoc* pDoc = GetActiveM2DrawDoc();
	if (!pDoc || !pDoc->CanSimplify()) return;
	m_subInputMode = SubInputMode::Smooth;
	m_subInputValue = 1.0f;   // 점 줄이기 허용 오차(px): 작을수록 원래 점을 많이 남기고 곡선만 부드럽게
	UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
}

void CMainFrame::OnSubApply()
{
	CM2DrawDoc* pDoc = GetActiveM2DrawDoc();
	const SubInputMode mode = m_subInputMode;
	float v = m_subInputValue;
	const int vi = m_wndSubToolBar.CommandToIndex(ID_SUB_VALUE);
	if (vi >= 0)
		if (auto* eb = DYNAMIC_DOWNCAST(CMFCToolBarEditBoxButton, m_wndSubToolBar.GetButton(vi)))
		{
			const float parsed = static_cast<float>(_tstof(CMFCToolBarEditBoxButton::GetContentsAll(ID_SUB_VALUE)));
			if (parsed > 0.0f) v = parsed;
		}
	m_subInputMode = SubInputMode::None;
	if (pDoc)
	{
		if (mode == SubInputMode::Outline) pDoc->OutlineStroke(v);
		else if (mode == SubInputMode::Simplify) pDoc->SimplifyPath(v);
		else if (mode == SubInputMode::Smooth) pDoc->SmoothPath(v);
	}
	UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
}

void CMainFrame::OnSubCancel()
{
	m_subInputMode = SubInputMode::None;
	UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
}

void CMainFrame::OnSubPolyline()
{
	GetApp()->GetToolManager().SetActiveTool(ToolType::Polyline);
}

void CMainFrame::OnSubToolCommand(UINT nID)
{
	ToolType targetTool = ToolType::Select;
	switch (nID)
	{
	case ID_TOOL_RECT:     targetTool = ToolType::Rect; break;
	case ID_TOOL_TRIANGLE: targetTool = ToolType::Triangle; break;
	case ID_TOOL_POLYGON:  targetTool = ToolType::Polygon; break;
	case ID_TOOL_STAR:     targetTool = ToolType::Star; break;
	case ID_TOOL_GEAR:     targetTool = ToolType::Gear; break;
	case ID_TOOL_SPIRAL:   targetTool = ToolType::Spiral; break;
	case ID_TOOL_HEART:    targetTool = ToolType::Heart; break;
	case ID_TOOL_ZOOMIN:   targetTool = ToolType::ZoomIn; break;
	case ID_TOOL_ZOOMOUT:  targetTool = ToolType::ZoomOut; break;
	default: return;
	}

	GetApp()->GetToolManager().SetActiveTool(targetTool);
}

void CMainFrame::OnUpdateSubToolCommand(CCmdUI* pCmdUI)
{
	ToolType active = GetApp()->GetToolManager().GetActiveType();
	bool isCur = false;
	switch (pCmdUI->m_nID)
	{
	case ID_TOOL_RECT:     isCur = (active == ToolType::Rect); break;
	case ID_TOOL_TRIANGLE: isCur = (active == ToolType::Triangle); break;
	case ID_TOOL_POLYGON:  isCur = (active == ToolType::Polygon); break;
	case ID_TOOL_STAR:     isCur = (active == ToolType::Star); break;
	case ID_TOOL_GEAR:     isCur = (active == ToolType::Gear); break;
	case ID_TOOL_SPIRAL:   isCur = (active == ToolType::Spiral); break;
	case ID_TOOL_HEART:    isCur = (active == ToolType::Heart); break;
	case ID_TOOL_ZOOMIN:   isCur = (active == ToolType::ZoomIn); break;
	case ID_TOOL_ZOOMOUT:  isCur = (active == ToolType::ZoomOut); break;
	default: break;
	}
	pCmdUI->Enable(TRUE);
	pCmdUI->SetCheck(isCur ? 1 : 0);
}


