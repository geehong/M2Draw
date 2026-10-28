#pragma once
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

public:
	COutputWnd& GetOutputWnd() { return m_wndOutput; }
	CToolBoxPane& GetToolBox() { return m_wndToolBox; }
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
	afx_msg void OnWindowManager();
	afx_msg void OnViewCustomize();
	afx_msg LRESULT OnToolbarCreateNew(WPARAM wp, LPARAM lp);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg void OnOptionBarCommit(UINT nID);
	afx_msg void OnUpdateOptionBar(CCmdUI* pCmdUI);
	afx_msg void OnSubToolCommand(UINT nID);
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
