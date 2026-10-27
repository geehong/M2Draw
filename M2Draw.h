#pragma once

#ifndef __AFXWIN_H__
	#error "PCH에 대해 이 파일을 포함하기 전에 'pch.h'를 포함합니다."
#endif

#include "resource.h"

#include "GridRenderer.h"
#include "RulerRenderer.h"
#include "ToolManager.h"

#include "SnapManager.h"

class CM2DrawApp : public CWinAppEx
{
public:
	CM2DrawApp() noexcept;

protected:
	GridOptions   m_defaultGridOptions;
	GridRenderer  m_grid;
	RulerRenderer m_ruler;
	SnapManager   m_snapMgr;
	ToolManager   m_toolMgr;

public:
	GridOptions&   GetDefaultGridOptions() { return m_defaultGridOptions; }
	GridRenderer&  GetGrid() { return m_grid; }
	RulerRenderer& GetRuler() { return m_ruler; }
	SnapManager&   GetSnapManager() { return m_snapMgr; }
	ToolManager&   GetToolManager() { return m_toolMgr; }

public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();

	BOOL  m_bHiColorIcons;

	virtual void PreLoadState();
	virtual void LoadCustomState();
	virtual void SaveCustomState();

	afx_msg void OnAppAbout();
	afx_msg void OnFileOpen();
	DECLARE_MESSAGE_MAP()
};

extern CM2DrawApp theApp;

inline CM2DrawApp* GetApp() { return (CM2DrawApp*)AfxGetApp(); }

void LogAppEvent(const CString& strMsg, int tabIndex = 0);

// 패널(속성창/옵션바/레이어 패널)이 편집을 끝낸 시점에 호출: 활성 문서가 마지막 확정 상태와 달라졌으면 Undo 이력에 기록하고 수정 표시를 켬
bool CommitActiveDocUndo(const wchar_t* actionName);

void SetAppStatusText(const CString& strMsg);

namespace ge { class Node; }

// 선택된 도형(nullptr = 선택 없음)을 속성창 / 옵션바 / 레이어 패널에 반영
void UpdateShapePropertiesSelection(ge::Node* pShape, CM2DrawDoc* pDoc);
void SyncOptionBarWithSelection(ge::Node* pShape, CM2DrawDoc* pDoc);
void SyncPropertiesWithSelection(ge::Node* pShape, CM2DrawDoc* pDoc);

void UpdateOptionBarCreationCoords(float x, float y, float w, float h, bool hasFixedStart);