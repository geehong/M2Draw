#pragma once

#include "ChatAgentWnd.h"
#include "CodeViewWnd.h"

/////////////////////////////////////////////////////////////////////////////
// COutputList 창

class COutputList : public CListBox
{
// 생성입니다.
public:
	COutputList() noexcept;

// 구현입니다.
public:
	virtual ~COutputList();

protected:
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnEditCopy();
	afx_msg void OnEditClear();
	afx_msg void OnViewOutput();

	DECLARE_MESSAGE_MAP()
};

class COutputWnd : public CDockablePane
{
// 생성입니다.
public:
	COutputWnd() noexcept;

	void UpdateFonts();
	CChatAgentWnd& GetChat() { return m_wndChat; }

	// 외부(뷰, 프레임, 속성창 등)에서 출력 창으로 로그 메시지를 기록하는 함수 (로그 탭 하나에 쌓는다)
	void LogMessage(const CString& strMessage, int tabIndex = 0);
	void ClearLog(int tabIndex = -1);

// 특성입니다.
protected:
	CMFCTabCtrl	m_wndTabs;

	COutputList   m_wndLog;       // Log 탭 (앱 이벤트 로그)
	CChatAgentWnd m_wndChat;      // ChatAgent 탭 (에이전트와 대화)
	CCodeViewWnd  m_wndCode;      // Code 탭 (SVG / HTML 태그 / HTML 내보내기 코드, 읽기 전용)

protected:
	void AdjustHorzScroll(CListBox& wndListBox);

// 구현입니다.
public:
	virtual ~COutputWnd();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);

	DECLARE_MESSAGE_MAP()
};
