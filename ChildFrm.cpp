
// ChildFrm.cpp: CChildFrame 클래스의 구현
//

#include "pch.h"
#include "framework.h"
#include "M2Draw.h"

#include "ChildFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CChildFrame

IMPLEMENT_DYNCREATE(CChildFrame, CMDIChildWndEx)

BEGIN_MESSAGE_MAP(CChildFrame, CMDIChildWndEx)
END_MESSAGE_MAP()

// CChildFrame 생성/소멸

CChildFrame::CChildFrame() noexcept
{
	// TODO: 여기에 멤버 초기화 코드를 추가합니다.
}

CChildFrame::~CChildFrame()
{
}


BOOL CChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: CREATESTRUCT cs를 수정하여 여기에서 Window 클래스 또는 스타일을 수정합니다.
	if( !CMDIChildWndEx::PreCreateWindow(cs) )
		return FALSE;

	return TRUE;
}

void CChildFrame::OnUpdateFrameTitle(BOOL bAddToTitle)
{
	CMDIChildWndEx::OnUpdateFrameTitle(bAddToTitle);

	CDocument* pDoc = GetActiveDocument();
	if (pDoc != nullptr)
	{
		CString strTitle;
		GetWindowText(strTitle);

		bool bModified = (pDoc->IsModified() == TRUE);
		bool bHasStar = (strTitle.Right(2) == _T(" *") || strTitle.Right(1) == _T("*"));

		if (bModified && !bHasStar)
		{
			strTitle += _T(" *");
			SetWindowText(strTitle);
		}
		else if (!bModified && bHasStar)
		{
			if (strTitle.Right(2) == _T(" *"))
				strTitle = strTitle.Left(strTitle.GetLength() - 2);
			else if (strTitle.Right(1) == _T("*"))
				strTitle = strTitle.Left(strTitle.GetLength() - 1);
			SetWindowText(strTitle);
		}
	}
}

// CChildFrame 진단

#ifdef _DEBUG
void CChildFrame::AssertValid() const
{
	CMDIChildWndEx::AssertValid();
}

void CChildFrame::Dump(CDumpContext& dc) const
{
	CMDIChildWndEx::Dump(dc);
}
#endif //_DEBUG

// CChildFrame 메시지 처리기
