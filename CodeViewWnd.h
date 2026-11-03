#pragma once

// 출력 창의 "코드" 탭: 활성 문서의 SVG 코드, 가져온 사이트의 DOM 요약(HTML 태그 정보), HTML 내보내기 코드를
// 읽기 전용으로 보여 준다. 드롭다운으로 종류를 고르고, 새로고침을 눌렀을 때만 다시 만든다.
class CCodeViewWnd : public CWnd
{
public:
	CCodeViewWnd();
	virtual ~CCodeViewWnd();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnKindChanged();
	afx_msg void OnRefreshClicked();
	afx_msg void OnCopyClicked();
	DECLARE_MESSAGE_MAP()

private:
	void Refresh();

	CComboBox m_kind;
	CButton   m_refresh;
	CButton   m_copy;
	CEdit     m_view;
	CString   m_fullText;   // 복사용 원문 (화면에는 이미지 데이터를 줄여 보인다)
};
