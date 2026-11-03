#pragma once

#include <afxwin.h>

// 웹폰트 링크 입력 대화상자 (Step 7.1.4): 웹 출력 <head> 에 넣을 HTML 을 여러 줄로 입력한다
class CWebFontDlg : public CDialog
{
public:
	explicit CWebFontDlg(const CString& initial, CWnd* pParent = nullptr);
	CString m_text;

protected:
	void DoDataExchange(CDataExchange* pDX) override;
};
