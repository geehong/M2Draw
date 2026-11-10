#pragma once
#include "ProjectTreeWnd.h"
#include "OutputWnd.h"
#include "PropertiesWnd.h"
#include "ToolBoxPane.h"
#include "ToolOptionBar.h"
#include "LayerWnd.h"
#include "ToolManager.h"

class CMainFrame : public CMDIFrameWndEx, public IToolChangeListener
{
	DECLARE_DYNAMIC(CMainFrame)
public:
	CMainFrame() noexcept;

public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL LoadFrame(UINT nIDResource, DWORD dwDefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, CWnd* pParentWnd = nullptr, CCreateContext* pContext = nullptr);

	// IToolChangeListener 구현
	void OnToolChanged(ToolType newTool) override;
	void UpdateSubToolBar(ToolType curTool);
	// 외곽선 확장/단순화: 상단에 값 입력창 + 적용/취소를 띄우는 입력 모드 (버튼을 눌러도 바로 적용하지 않음)
	enum class SubInputMode { None, Outline, Simplify, Smooth };
	SubInputMode m_subInputMode = SubInputMode::None;
	std::vector<UINT> m_lastSubIds;                           // 마지막으로 만든 서브도구 버튼 구성 (같으면 재구성 생략)
	SubInputMode      m_lastSubMode = SubInputMode::None;
	float        m_subInputValue = 2.0f;

public:
	virtual ~CMainFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:  // 컨트롤 모음이 포함된 멤버입니다.
	CMFCMenuBar       m_wndMenuBar;
	CMFCToolBar       m_wndToolBar;
	CMFCToolBar       m_wndSubToolBar;
	CToolOptionBar    m_wndOptionBar;
	CMFCStatusBar     m_wndStatusBar;
	CMFCToolBarImages m_UserImages;
	CToolBoxPane      m_wndToolBox;
	COutputWnd        m_wndOutput;
	CPropertiesWnd    m_wndProperties;
	CPropertiesWnd    m_wndGridProp;
	CPropertiesWnd    m_wndCanvasProp;
	CLayerWnd         m_wndLayers;
	CProjectTreeWnd   m_wndProject;   // 프로젝트 트리 (8.8)

public:
	COutputWnd& GetOutputWnd() { return m_wndOutput; }
	CString GetUrlText() { return GetUrlEditText(); }           // 주소창의 현재 글자 (비어 있을 수 있음)
	void FocusUrlEdit();                                       // 주소창에 커서를 둔다 (주소를 입력받을 때)
	void NoteUnsavedDocument();                                // 작업이 끝났을 때 저장 안 된 문서면 채팅에 한 줄 안내 (8.2)
	const void* m_chatDoc = nullptr;   // 채팅 연결이 마지막으로 따라간 문서 (탭 전환 감지)
	CToolBoxPane& GetToolBox() { return m_wndToolBox; }
	void RefreshProjectTree() { m_wndProject.Sync(); }   // 프로젝트 상태가 바뀌었을 때 바로 트리를 다시 그린다 (타이머를 기다리지 않음)
	CToolOptionBar& GetOptionBar() { return m_wndOptionBar; }
	CMFCStatusBar& GetStatusBar() { return m_wndStatusBar; }
	CPropertiesWnd& GetPropertiesWnd() { return m_wndProperties; }
	CPropertiesWnd& GetGridPropertiesWnd() { return m_wndGridProp; }
	CPropertiesWnd& GetCanvasPropertiesWnd() { return m_wndCanvasProp; }
	CLayerWnd& GetLayerWnd() { return m_wndLayers; }
	CM2DrawDoc* GetActiveM2DrawDoc();
	void SetStatusMessage(const CString& strMsg)
	{
		CString cur;
		m_wndStatusBar.GetPaneText(0, cur);
		if (cur != strMsg)
		{
			m_wndStatusBar.SetPaneText(0, strMsg);
		}
	}

// 생성된 메시지 맵 함수
protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);   // 외부 수정 감지 (7.2.4)
	afx_msg void OnTimer(UINT_PTR nIDEvent);   // 2초마다 외부 수정 확인 (7.2.4)
	afx_msg void OnUrlGet();                             // Get Ref 버튼: 주소창의 주소로 레퍼런스 가져오기 (7.1.6)
	afx_msg void OnUpdateUrlLabel(CCmdUI* pCmdUI);
	afx_msg void OnUpdateUrlEdit(CCmdUI* pCmdUI);
	CString GetUrlEditText();
	bool m_bClosing = false;   // 종료 중: 서브도구 바를 다시 만들면 파괴된 툴팁(DelTool assert)을 건드리므로 막음
	afx_msg void OnWindowManager();
	afx_msg void OnViewCustomize();
	afx_msg void OnFilePreviewHtml();
	afx_msg void OnToolMenuCommand(UINT nID);
	afx_msg void OnUpdateToolMenuCommand(CCmdUI* pCmdUI);
	afx_msg void OnHelpShortcuts();
	afx_msg LRESULT OnToolbarCreateNew(WPARAM wp, LPARAM lp);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg void OnOptionBarCommit(UINT nID);
	afx_msg void OnUpdateOptionBar(CCmdUI* pCmdUI);
	afx_msg void OnSubToolCommand(UINT nID);
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnSubPolyline();
	afx_msg void OnUpdateSubValue(CCmdUI* pCmdUI);
	afx_msg void OnSubOutline();
	afx_msg void OnSubSimplify();
	afx_msg void OnSubSmooth();
	afx_msg void OnSubApply();
	afx_msg void OnSubCancel();
	afx_msg void OnUpdateSubToolCommand(CCmdUI* pCmdUI);
	DECLARE_MESSAGE_MAP()

	BOOL CreateDockingWindows();
	void SetDockingWindowIcons(BOOL bHiColorIcons);
};
