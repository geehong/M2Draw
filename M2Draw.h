#pragma once

#ifndef __AFXWIN_H__
	#error "PCH에 대해 이 파일을 포함하기 전에 'pch.h'를 포함합니다."
#endif

#include "resource.h"

#include "GridRenderer.h"
#include "RulerRenderer.h"
#include "ToolManager.h"

class CM2DrawApp : public CWinAppEx
{
public:
	CM2DrawApp() noexcept;

protected:
	GridRenderer  m_grid;
	RulerRenderer m_ruler;
	ToolManager   m_toolMgr;

public:
	GridRenderer&  GetGrid() { return m_grid; }
	RulerRenderer& GetRuler() { return m_ruler; }
	ToolManager&   GetToolManager() { return m_toolMgr; }

public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();

	BOOL  m_bHiColorIcons;

	virtual void PreLoadState();
	virtual void LoadCustomState();
	virtual void SaveCustomState();

	afx_msg void OnAppAbout();
	DECLARE_MESSAGE_MAP()
};

extern CM2DrawApp theApp;

inline CM2DrawApp* GetApp() { return (CM2DrawApp*)AfxGetApp(); }

void LogAppEvent(const CString& strMsg, int tabIndex = 0);

void SetAppStatusText(const CString& strMsg);

void UpdateShapePropertiesSelection(int shapeIndex, CM2DrawDoc* pDoc);