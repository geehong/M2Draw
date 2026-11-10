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

	// 이름 편집 중 Enter/Esc 가 프레임의 키 처리에 먹히지 않고 편집 컨트롤에 닿게 한다
	BOOL PreTranslateMessage(MSG* pMsg) override;

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpcs);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTreeDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnTreeRClick(NMHDR* pNMHDR, LRESULT* pResult);   // 우클릭: 이름 바꾸기 / 프로젝트에서 빼기
	afx_msg void OnBeginLabelEdit(NMHDR* pNMHDR, LRESULT* pResult);   // 페이지 항목만 이름을 고칠 수 있다
	afx_msg void OnEndLabelEdit(NMHDR* pNMHDR, LRESULT* pResult);     // 입력한 이름으로 페이지 이름 바꾸기
	afx_msg LRESULT OnSyncDeferred(WPARAM, LPARAM);                   // 편집 알림이 끝난 뒤 트리를 다시 채운다
	DECLARE_MESSAGE_MAP()

	void Rebuild();

	CTreeCtrl    m_tree;
	std::wstring m_sig;   // 마지막으로 그린 프로젝트 상태 (ProjectTreeSignature)
};
