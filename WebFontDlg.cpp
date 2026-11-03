#include "pch.h"
#include "WebFontDlg.h"
#include "Resource.h"

CWebFontDlg::CWebFontDlg(const CString& initial, CWnd* pParent)
	: CDialog(IDD_WEBFONT_LINK, pParent), m_text(initial)
{
}

void CWebFontDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_WEBFONT_EDIT, m_text);
}