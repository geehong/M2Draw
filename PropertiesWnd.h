#pragma once
#include <d2d1.h>

#include "D2DPropertyView.h"

class CM2DrawDoc;

class CPropertiesToolBar : public CMFCToolBar
{
public:
	virtual void OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL bDisableIfNoHndler)
	{
		CFrameWnd* pFrame = DYNAMIC_DOWNCAST(CFrameWnd, AfxGetMainWnd());
		if (pFrame != nullptr)
		{
			CMFCToolBar::OnUpdateCmdUI(pFrame, bDisableIfNoHndler);
		}
	}

	virtual BOOL AllowShowOnList() const { return FALSE; }
};

class CPropertiesWnd : public CDockablePane
{
public:
	CPropertiesWnd(CD2DPropertyView::PaneMode mode = CD2DPropertyView::MODE_SHAPE) noexcept;

	void AdjustLayout();
	void SetSelectedShape(int shapeIndex, CM2DrawDoc* pDoc);
	CD2DPropertyView::PaneMode GetPaneMode() const { return m_paneMode; }
	CD2DPropertyView& GetD2DPropView() { return m_wndD2DProp; }

public:
	void SetVSDotNetLook(BOOL bSet)
	{
		m_wndPropList.SetVSDotNetLook(bSet);
		m_wndPropList.SetGroupNameFullWidth(bSet);
	}

protected:
	CD2DPropertyView::PaneMode m_paneMode;
	CFont m_fntPropList;
	CComboBox m_wndObjectCombo;
	CPropertiesToolBar m_wndToolBar;
	CMFCPropertyGridCtrl m_wndPropList;
	CD2DPropertyView m_wndD2DProp;

	int m_selectedShapeIndex = -1;
	CM2DrawDoc* m_pActiveDoc = nullptr;

public:
	virtual ~CPropertiesWnd();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnExpandAllProperties();
	afx_msg void OnUpdateExpandAllProperties(CCmdUI* pCmdUI);
	afx_msg void OnSortProperties();
	afx_msg void OnUpdateSortProperties(CCmdUI* pCmdUI);
	afx_msg void OnProperties1();
	afx_msg void OnUpdateProperties1(CCmdUI* pCmdUI);
	afx_msg void OnProperties2();
	afx_msg void OnUpdateProperties2(CCmdUI* pCmdUI);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg void OnComboSelectChanged();
	afx_msg LRESULT OnTabChanged(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPropertyChanged(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

	void InitCanvasPropList();
	void InitShapePropList();
	void InitTestPropList();
	void SetPropListFont();

	int m_nComboHeight;
};