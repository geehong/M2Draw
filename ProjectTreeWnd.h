#pragma once
// 프로젝트 트리 패널 (8.8): 프로젝트 > 페이지 / 레퍼런스 를 보여 준다. 더블클릭 = 페이지 열기
#include <afxwin.h>
#include <afxext.h>
#include <string>

class CProjectTreeWnd : public CDockablePane
{
	DECLARE_DYNAMIC(CProjectTreeWnd)

public:
	CProjectTreeWnd() noexcept;
	virtual ~CProjectTreeWnd();

	// 프로젝트가 바뀌었으면 트리를 다시 채운다 (메인 프레임의 타이머에서 호출)
	void Sync();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpcs);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTreeDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTreeRClick(NMHDR* pNMHDR, LRESULT* pResult);   // 우클릭: 페이지를 프로젝트에서 뺀다
	DECLARE_MESSAGE_MAP()

	void Rebuild();

	CTreeCtrl    m_tree;
	std::wstring m_sig;   // 마지막으로 그린 프로젝트 상태 (ProjectTreeSignature)
};
