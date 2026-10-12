// PropertiesWnd.cpp: implementation of the CPropertiesWnd class
#include "pch.h"
#include "framework.h"

#include "PropertiesWnd.h"
#include "Resource.h"
#include "MainFrm.h"
#include "M2Draw.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

CPropertiesWnd::CPropertiesWnd() noexcept
{
	m_nComboHeight = 0;
}

CPropertiesWnd::~CPropertiesWnd()
{
}

BEGIN_MESSAGE_MAP(CPropertiesWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_COMMAND(ID_EXPAND_ALL, OnExpandAllProperties)
	ON_UPDATE_COMMAND_UI(ID_EXPAND_ALL, OnUpdateExpandAllProperties)
	ON_COMMAND(ID_SORTPROPERTIES, OnSortProperties)
	ON_UPDATE_COMMAND_UI(ID_SORTPROPERTIES, OnUpdateSortProperties)
	ON_COMMAND(ID_PROPERTIES1, OnProperties1)
	ON_UPDATE_COMMAND_UI(ID_PROPERTIES1, OnUpdateProperties1)
	ON_COMMAND(ID_PROPERTIES2, OnProperties2)
	ON_UPDATE_COMMAND_UI(ID_PROPERTIES2, OnUpdateProperties2)
	ON_WM_SETFOCUS()
	ON_WM_SETTINGCHANGE()
	ON_REGISTERED_MESSAGE(AFX_WM_PROPERTY_CHANGED, OnPropertyChanged)
END_MESSAGE_MAP()

int CPropertiesWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rectDummy;
	rectDummy.SetRectEmpty();

	const DWORD dwViewStyle = WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_BORDER | CBS_SORT | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

	if (!m_wndObjectCombo.Create(dwViewStyle, rectDummy, this, 1))
	{
		TRACE0("속성 콤보 상자를 만들지 못했습니다.\n");
		return -1;
	}

	m_wndObjectCombo.AddString(_T("격자 및 캔버스 설정"));
	m_wndObjectCombo.SetCurSel(0);

	CRect rectCombo;
	m_wndObjectCombo.GetClientRect(&rectCombo);
	m_nComboHeight = rectCombo.Height();

	if (!m_wndPropList.Create(WS_VISIBLE | WS_CHILD, rectDummy, this, 2))
	{
		TRACE0("속성 그리드를 만들지 못했습니다.\n");
		return -1;
	}

	InitPropList();

	m_wndToolBar.Create(this, AFX_DEFAULT_TOOLBAR_STYLE, IDR_PROPERTIES);
	m_wndToolBar.LoadToolBar(IDR_PROPERTIES, 0, 0, TRUE /* 잠금 */);
	m_wndToolBar.CleanUpLockedImages();
	m_wndToolBar.LoadBitmap(IDR_PROPERTIES, 0, 0, TRUE /* 잠금 */);

	m_wndToolBar.SetPaneStyle(m_wndToolBar.GetPaneStyle() | CBRS_TOOLTIPS | CBRS_FLYBY);
	m_wndToolBar.SetPaneStyle(m_wndToolBar.GetPaneStyle() & ~(CBRS_GRIPPER | CBRS_SIZE_DYNAMIC | CBRS_BORDER_TOP | CBRS_BORDER_BOTTOM | CBRS_BORDER_LEFT | CBRS_BORDER_RIGHT));
	m_wndToolBar.SetOwner(this);
	m_wndToolBar.SetRouteCommandsViaFrame(FALSE);

	AdjustLayout();
	return 0;
}

void CPropertiesWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	AdjustLayout();
}

void CPropertiesWnd::AdjustLayout()
{
	if (GetSafeHwnd() == nullptr || (AfxGetMainWnd() && AfxGetMainWnd()->IsIconic()))
	{
		return;
	}

	CRect rectClient;
	GetClientRect(rectClient);

	int cyTlb = m_wndToolBar.CalcFixedLayout(FALSE, TRUE).cy;

	m_wndObjectCombo.SetWindowPos(nullptr, rectClient.left, rectClient.top, rectClient.Width(), m_nComboHeight, SWP_NOACTIVATE | SWP_NOZORDER);
	m_wndToolBar.SetWindowPos(nullptr, rectClient.left, rectClient.top + m_nComboHeight, rectClient.Width(), cyTlb, SWP_NOACTIVATE | SWP_NOZORDER);
	m_wndPropList.SetWindowPos(nullptr, rectClient.left, rectClient.top + m_nComboHeight + cyTlb, rectClient.Width(), rectClient.Height() - (m_nComboHeight + cyTlb), SWP_NOACTIVATE | SWP_NOZORDER);
}

void CPropertiesWnd::OnExpandAllProperties()
{
	m_wndPropList.ExpandAll();
}

void CPropertiesWnd::OnUpdateExpandAllProperties(CCmdUI* /* pCmdUI */)
{
}

void CPropertiesWnd::OnSortProperties()
{
	m_wndPropList.SetAlphabeticMode(!m_wndPropList.IsAlphabeticMode());
}

void CPropertiesWnd::OnUpdateSortProperties(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_wndPropList.IsAlphabeticMode());
}

void CPropertiesWnd::OnProperties1()
{
}

void CPropertiesWnd::OnUpdateProperties1(CCmdUI* /* pCmdUI */)
{
}

void CPropertiesWnd::OnProperties2()
{
}

void CPropertiesWnd::OnUpdateProperties2(CCmdUI* /* pCmdUI */)
{
}

void CPropertiesWnd::InitPropList()
{
	SetPropListFont();

	m_wndPropList.EnableHeaderCtrl(FALSE);
	m_wndPropList.EnableDescriptionArea();
	m_wndPropList.SetVSDotNetLook();
	m_wndPropList.MarkModifiedProperties();

	// 1. 그리드 및 눈금자 설정 그룹
	CMFCPropertyGridProperty* pGroupGrid = new CMFCPropertyGridProperty(_T("그리드 및 눈금자 설정"));

	// 그리드 표시 ON/OFF
	CMFCPropertyGridProperty* pPropShow = new CMFCPropertyGridProperty(
		_T("그리드 표시"),
		(_variant_t)(GetApp()->GetGrid().enabled ? true : false),
		_T("화면에 작업 그리드 격자선을 표시합니다.")
	);
	pPropShow->SetData(1);
	pGroupGrid->AddSubItem(pPropShow);

	// 그리드 스냅 ON/OFF
	CMFCPropertyGridProperty* pPropSnap = new CMFCPropertyGridProperty(
		_T("그리드 스냅"),
		(_variant_t)(GetApp()->GetGrid().snapToGrid ? true : false),
		_T("마우스 포인터와 도형 정점을 그리드 교차점에 자동으로 맞춥니다.")
	);
	pPropSnap->SetData(9);
	pGroupGrid->AddSubItem(pPropSnap);

	// 눈금자 표시 ON/OFF
	CMFCPropertyGridProperty* pPropRulerShow = new CMFCPropertyGridProperty(
		_T("눈금자 표시"),
		(_variant_t)(GetApp()->GetRuler().enabled ? true : false),
		_T("상단 및 좌측에 치수 눈금자를 표시합니다.")
	);
	pPropRulerShow->SetData(7);
	pGroupGrid->AddSubItem(pPropRulerShow);

	// 측정 단위 드롭다운 (px, mm, cm, in, rem)
	CMFCPropertyGridProperty* pPropUnit = new CMFCPropertyGridProperty(
		_T("측정 단위"),
		_T("픽셀 / 포인트 (px/pt)"),
		_T("캔버스의 기본 치수 단위를 설정합니다.")
	);
	pPropUnit->AddOption(_T("픽셀 / 포인트 (px/pt)"));
	pPropUnit->AddOption(_T("밀리미터 (mm)"));
	pPropUnit->AddOption(_T("센티미터 (cm)"));
	pPropUnit->AddOption(_T("인치 (in)"));
	pPropUnit->AddOption(_T("웹 rem (16px)"));
	pPropUnit->AllowEdit(FALSE);
	pPropUnit->SetValue((_variant_t)GetCanvasUnitName(GetApp()->GetGrid().unit));
	pPropUnit->SetData(8);
	pGroupGrid->AddSubItem(pPropUnit);

	// 그리드 간격 (단위 기준)
	CMFCPropertyGridProperty* pPropStep = new CMFCPropertyGridProperty(
		_T("그리드 간격 (단위)"),
		(_variant_t)(double)GetApp()->GetGrid().stepInUnit,
		_T("선택된 단위 기준의 격자 간격을 설정합니다.")
	);
	pPropStep->SetData(2);
	pGroupGrid->AddSubItem(pPropStep);

	// 주 격자선 배율
	CMFCPropertyGridProperty* pPropMajor = new CMFCPropertyGridProperty(
		_T("주 격자선 배율"),
		(_variant_t)(long)GetApp()->GetGrid().majorEvery,
		_T("몇 칸마다 굵은 주 격자선을 표시할지 지정합니다.")
	);
	pPropMajor->EnableSpinControl(TRUE, 2, 20);
	pPropMajor->SetData(3);
	pGroupGrid->AddSubItem(pPropMajor);

	// LOD 최소 화면 픽셀
	CMFCPropertyGridProperty* pPropMinStep = new CMFCPropertyGridProperty(
		_T("LOD 최소 간격 (px)"),
		(_variant_t)(double)GetApp()->GetGrid().minStepPx,
		_T("줌아웃 시 격자 간격이 이 값보다 작아지면 표시 밀도를 낮춥니다.")
	);
	pPropMinStep->SetData(4);
	pGroupGrid->AddSubItem(pPropMinStep);

	m_wndPropList.AddProperty(pGroupGrid);

	// 2. 그리드 색상 그룹
	CMFCPropertyGridProperty* pGroupColor = new CMFCPropertyGridProperty(_T("그리드 색상"));

	COLORREF clrMinor = RGB(
		(BYTE)(GetApp()->GetGrid().minor.r * 255),
		(BYTE)(GetApp()->GetGrid().minor.g * 255),
		(BYTE)(GetApp()->GetGrid().minor.b * 255)
	);
	CMFCPropertyGridColorProperty* pPropClrMinor = new CMFCPropertyGridColorProperty(
		_T("보조 격자선 색상"),
		clrMinor,
		nullptr,
		_T("일반 보조 격자선의 색상을 설정합니다.")
	);
	pPropClrMinor->EnableOtherButton(_T("기타..."));
	pPropClrMinor->SetData(5);
	pGroupColor->AddSubItem(pPropClrMinor);

	COLORREF clrMajor = RGB(
		(BYTE)(GetApp()->GetGrid().major.r * 255),
		(BYTE)(GetApp()->GetGrid().major.g * 255),
		(BYTE)(GetApp()->GetGrid().major.b * 255)
	);
	CMFCPropertyGridColorProperty* pPropClrMajor = new CMFCPropertyGridColorProperty(
		_T("주 격자선 색상"),
		clrMajor,
		nullptr,
		_T("주 격자선의 색상을 설정합니다.")
	);
	pPropClrMajor->EnableOtherButton(_T("기타..."));
	pPropClrMajor->SetData(6);
	pGroupColor->AddSubItem(pPropClrMajor);

	m_wndPropList.AddProperty(pGroupColor);
}

LRESULT CPropertiesWnd::OnPropertyChanged(WPARAM /*wParam*/, LPARAM lParam)
{
	CMFCPropertyGridProperty* pProp = reinterpret_cast<CMFCPropertyGridProperty*>(lParam);
	if (!pProp)
		return 0;

	DWORD_PTR id = pProp->GetData();
	COleVariant val = pProp->GetValue();

	CString strLogMsg;
	switch (id)
	{
	case 1:
		val.ChangeType(VT_BOOL);
		GetApp()->GetGrid().enabled = (val.boolVal != VARIANT_FALSE);
		strLogMsg.Format(_T("[속성 변경] 그리드 표시: %s"), GetApp()->GetGrid().enabled ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	case 2:
		val.ChangeType(VT_R8);
		if (val.dblVal > 0.001)
		{
			GetApp()->GetGrid().stepInUnit = static_cast<float>(val.dblVal);
			strLogMsg.Format(_T("[속성 변경] 그리드 간격: %.2f %s (실제 %.2f px)"),
				GetApp()->GetGrid().stepInUnit,
				GetCanvasUnitSuffix(GetApp()->GetGrid().unit),
				GetApp()->GetGrid().GetStepPx());
			LogAppEvent(strLogMsg);
		}
		break;
	case 3:
		val.ChangeType(VT_I4);
		if (val.lVal >= 1)
		{
			GetApp()->GetGrid().majorEvery = static_cast<int>(val.lVal);
			strLogMsg.Format(_T("[속성 변경] 주 격자선 배율: %d 칸마다"), GetApp()->GetGrid().majorEvery);
			LogAppEvent(strLogMsg);
		}
		break;
	case 4:
		val.ChangeType(VT_R8);
		if (val.dblVal >= 2.0)
		{
			GetApp()->GetGrid().minStepPx = static_cast<float>(val.dblVal);
			strLogMsg.Format(_T("[속성 변경] LOD 최소 간격: %.1f px"), GetApp()->GetGrid().minStepPx);
			LogAppEvent(strLogMsg);
		}
		break;
	case 5:
	{
		CMFCPropertyGridColorProperty* pColor = dynamic_cast<CMFCPropertyGridColorProperty*>(pProp);
		if (pColor)
		{
			COLORREF c = pColor->GetColor();
			GetApp()->GetGrid().minor = D2D1::ColorF(
				GetRValue(c) / 255.0f,
				GetGValue(c) / 255.0f,
				GetBValue(c) / 255.0f,
				0.5f
			);
			strLogMsg.Format(_T("[속성 변경] 보조 격자선 색상: RGB(%d, %d, %d)"), GetRValue(c), GetGValue(c), GetBValue(c));
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case 6:
	{
		CMFCPropertyGridColorProperty* pColor = dynamic_cast<CMFCPropertyGridColorProperty*>(pProp);
		if (pColor)
		{
			COLORREF c = pColor->GetColor();
			GetApp()->GetGrid().major = D2D1::ColorF(
				GetRValue(c) / 255.0f,
				GetGValue(c) / 255.0f,
				GetBValue(c) / 255.0f,
				0.8f
			);
			strLogMsg.Format(_T("[속성 변경] 주 격자선 색상: RGB(%d, %d, %d)"), GetRValue(c), GetGValue(c), GetBValue(c));
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case 7: // 눈금자 표시
		val.ChangeType(VT_BOOL);
		GetApp()->GetRuler().enabled = (val.boolVal != VARIANT_FALSE);
		strLogMsg.Format(_T("[속성 변경] 눈금자 표시: %s"), GetApp()->GetRuler().enabled ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	case 8: // 측정 단위 선택
	{
		CString strUnit = pProp->GetValue().bstrVal;
		if (strUnit.Find(_T("밀리미터")) >= 0 || strUnit.Find(_T("mm")) >= 0)
			GetApp()->GetGrid().unit = CanvasUnit::MM;
		else if (strUnit.Find(_T("센티미터")) >= 0 || strUnit.Find(_T("cm")) >= 0)
			GetApp()->GetGrid().unit = CanvasUnit::CM;
		else if (strUnit.Find(_T("인치")) >= 0 || strUnit.Find(_T("in")) >= 0)
			GetApp()->GetGrid().unit = CanvasUnit::Inch;
		else if (strUnit.Find(_T("rem")) >= 0)
			GetApp()->GetGrid().unit = CanvasUnit::Rem;
		else
			GetApp()->GetGrid().unit = CanvasUnit::Pixel;

		strLogMsg.Format(_T("[속성 변경] 측정 단위: %s (1단위 = %.2f px)"),
			GetCanvasUnitName(GetApp()->GetGrid().unit),
			GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit));
		LogAppEvent(strLogMsg);
		break;
	}
	case 9: // 그리드 스냅 ON/OFF
		val.ChangeType(VT_BOOL);
		GetApp()->GetGrid().snapToGrid = (val.boolVal != VARIANT_FALSE);
		strLogMsg.Format(_T("[속성 변경] 그리드 스냅: %s"), GetApp()->GetGrid().snapToGrid ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	default:
		break;
	}

	// 모든 열린 프레임 및 뷰 강제 재도색 (Invalidate & UpdateWindow)
	CWinApp* pApp = AfxGetApp();
	if (pApp)
	{
		POSITION posDocTemplate = pApp->GetFirstDocTemplatePosition();
		while (posDocTemplate)
		{
			CDocTemplate* pDocTemplate = pApp->GetNextDocTemplate(posDocTemplate);
			POSITION posDoc = pDocTemplate->GetFirstDocPosition();
			while (posDoc)
			{
				CDocument* pDoc = pDocTemplate->GetNextDoc(posDoc);
				POSITION posView = pDoc->GetFirstViewPosition();
				while (posView)
				{
					CView* pView = pDoc->GetNextView(posView);
					if (pView && pView->GetSafeHwnd())
					{
						pView->Invalidate(FALSE);
						pView->UpdateWindow();
					}
				}
				pDoc->UpdateAllViews(nullptr);
			}
		}
	}

	return 0;
}

void CPropertiesWnd::SetPropListFont()
{
	::DeleteObject(m_fntPropList.Detach());

	LOGFONT lf;
	afxGlobalData.fontRegular.GetLogFont(&lf);

	NONCLIENTMETRICS info;
	info.cbSize = sizeof(NONCLIENTMETRICS);

	if (afxGlobalData.GetNonClientMetrics(info))
	{
		TRACE0("CPropertiesWnd::SetPropListFont failed\n");
		return;
	}

	m_fntPropList.CreateFontIndirect(&info.lfMenuFont);
	m_wndPropList.SetFont(&m_fntPropList);
	m_wndObjectCombo.SetFont(&m_fntPropList);
}

void CPropertiesWnd::OnSetFocus(CWnd* pOldWnd)
{
	CDockablePane::OnSetFocus(pOldWnd);
	m_wndPropList.SetFocus();
}

void CPropertiesWnd::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CDockablePane::OnSettingChange(uFlags, lpszSection);
	SetPropListFont();
}
