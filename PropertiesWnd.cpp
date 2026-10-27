// PropertiesWnd.cpp: implementation of the CPropertiesWnd class
#include "pch.h"
#include "framework.h"

#include "PropertiesWnd.h"
#include "Resource.h"
#include "MainFrm.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

// 속성 항목 고유 ID 정의
enum PropId : DWORD_PTR
{
	// 캔버스 / 그리드 설정 (1 ~ 9)
	PROP_GRID_SHOW      = 1,
	PROP_GRID_STEP      = 2,
	PROP_GRID_MAJOR     = 3,
	PROP_GRID_MINSTEP   = 4,
	PROP_GRID_COLOR_MIN = 5,
	PROP_GRID_COLOR_MAJ = 6,
	PROP_RULER_SHOW     = 7,
	PROP_UNIT           = 8,
	PROP_GRID_SNAP      = 9,

	// 쉐이프 설정 (101 ~ 110)
	PROP_SHAPE_NAME     = 101,
	PROP_SHAPE_STROKE_W = 102,
	PROP_SHAPE_STROKE_C = 103,
	PROP_SHAPE_DASH     = 104,
	PROP_SHAPE_FILL_TYP = 105,
	PROP_SHAPE_FILL_CLR = 106,

	// GUI TEST (201 ~ 210)
	PROP_TEST_CHECK1    = 201,
	PROP_TEST_CHECK2    = 202,
	PROP_TEST_SLIDER    = 203,
	PROP_TEST_BUTTON    = 204,
	PROP_TEST_COLOR     = 205
};

CPropertiesWnd::CPropertiesWnd(CD2DPropertyView::PaneMode mode) noexcept
	: m_paneMode(mode)
	, m_wndD2DProp(mode)
	, m_nComboHeight(0)
	, m_pActiveDoc(nullptr)
{
}

CPropertiesWnd::~CPropertiesWnd()
{
}

BEGIN_MESSAGE_MAP(CPropertiesWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_GETMINMAXINFO()
	ON_COMMAND(ID_EXPAND_ALL, OnExpandAllProperties)
	ON_UPDATE_COMMAND_UI(ID_EXPAND_ALL, OnUpdateExpandAllProperties)
	ON_COMMAND(ID_SORTPROPERTIES, OnSortProperties)
	ON_UPDATE_COMMAND_UI(ID_SORTPROPERTIES, OnUpdateSortProperties)
	ON_COMMAND(ID_PROPERTIES1, OnProperties1)
	ON_UPDATE_COMMAND_UI(ID_PROPERTIES1, OnUpdateProperties1)
	ON_COMMAND(ID_PROPERTIES2, OnProperties2)
	ON_UPDATE_COMMAND_UI(ID_PROPERTIES2, OnUpdateProperties2)
	ON_CBN_SELCHANGE(1, OnComboSelectChanged)
	ON_REGISTERED_MESSAGE(AFX_WM_CHANGE_ACTIVE_TAB, OnTabChanged)
	ON_WM_SETFOCUS()
	ON_WM_SETTINGCHANGE()
	ON_REGISTERED_MESSAGE(AFX_WM_PROPERTY_CHANGED, OnPropertyChanged)
END_MESSAGE_MAP()

int CPropertiesWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	// 최소 도킹 크기 설정 (너비 240px, 높이 180px)
	SetMinSize(CSize(240, 180));

	CRect rectDummy;
	rectDummy.SetRectEmpty();

	// 내부 호환용 콤보박스 (숨김 모드로 생성하여 기존 데이터 동기화 유지)
	const DWORD dwViewStyle = WS_CHILD | CBS_DROPDOWNLIST | WS_BORDER | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	m_wndObjectCombo.Create(dwViewStyle, rectDummy, this, 1);
	m_wndObjectCombo.ShowWindow(SW_HIDE);

	if (!m_wndPropList.Create(WS_VISIBLE | WS_CHILD, rectDummy, this, 2))
	{
		TRACE0("속성 그리드를 만들지 못했습니다.\n");
		return -1;
	}

	// D2D Vector GUI 속성 뷰 생성 (전용 윈도우 클래스 등록)
	CString strD2DClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), (HBRUSH)::GetStockObject(BLACK_BRUSH), nullptr);
	if (!m_wndD2DProp.Create(strD2DClass, _T("D2DPropertyView"), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, rectDummy, this, 3))
	{
		TRACE0("D2D 속성 뷰를 만들지 못했습니다.\n");
	}

	m_wndD2DProp.SetPaneMode(m_paneMode);

	AdjustLayout();
	return 0;
}

void CPropertiesWnd::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CDockablePane::OnWindowPosChanging(lpwndpos);

	if (!(lpwndpos->flags & SWP_NOSIZE))
	{
		const int MIN_DOCK_WIDTH = 240;
		const int MAX_DOCK_WIDTH = 420;

		// 도킹되어 있거나 도킹 컨테이너 내부에 있을 때
		if (IsDocked())
		{
			// 우측에 도킹되어 있는 경우 너비가 변하면 x 좌표도 보정하여 스플리터 경계와 일치시킴
			DWORD dwAlignment = GetCurrentAlignment();
			if (dwAlignment == CBRS_ALIGN_RIGHT)
			{
				if (lpwndpos->cx > MAX_DOCK_WIDTH)
				{
					int diff = lpwndpos->cx - MAX_DOCK_WIDTH;
					lpwndpos->cx = MAX_DOCK_WIDTH;
					lpwndpos->x += diff;
				}
				else if (lpwndpos->cx < MIN_DOCK_WIDTH)
				{
					int diff = MIN_DOCK_WIDTH - lpwndpos->cx;
					lpwndpos->cx = MIN_DOCK_WIDTH;
					lpwndpos->x -= diff;
				}
			}
			else
			{
				if (lpwndpos->cx > MAX_DOCK_WIDTH)
				{
					lpwndpos->cx = MAX_DOCK_WIDTH;
				}
				else if (lpwndpos->cx < MIN_DOCK_WIDTH)
				{
					lpwndpos->cx = MIN_DOCK_WIDTH;
				}
			}
		}
	}
}

void CPropertiesWnd::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDockablePane::OnGetMinMaxInfo(lpMMI);
	lpMMI->ptMinTrackSize.x = 240; // 최소 너비 240px
	lpMMI->ptMinTrackSize.y = 180; // 최소 높이 180px
	lpMMI->ptMaxTrackSize.x = 420; // 최대 너비 420px
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

	int yContent = rectClient.top;
	int hContent = rectClient.Height();
	if (hContent < 0) hContent = 0;

	// 이전 클래식 PropertyGrid는 숨김
	if (m_wndPropList.GetSafeHwnd())
	{
		m_wndPropList.ShowWindow(SW_HIDE);
	}

	if (m_wndD2DProp.GetSafeHwnd())
	{
		m_wndD2DProp.SetPaneMode(m_paneMode);
		m_wndD2DProp.SetWindowPos(nullptr, rectClient.left, yContent, rectClient.Width(), hContent, SWP_NOACTIVATE | SWP_NOZORDER);
		m_wndD2DProp.ShowWindow(SW_SHOW);
		m_wndD2DProp.Invalidate();
	}
}

LRESULT CPropertiesWnd::OnTabChanged(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	AdjustLayout();
	return 0;
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
	// 탭 1 버튼: 캔버스 설정으로 전환
	m_wndObjectCombo.SetCurSel(0);
	OnComboSelectChanged();
}

void CPropertiesWnd::OnUpdateProperties1(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_wndObjectCombo.GetCurSel() == 0 ? 1 : 0);
}

void CPropertiesWnd::OnProperties2()
{
	// 탭 2 버튼: 쉐이프 설정으로 전환 (선택된 도형이 있을 때)
	if (m_wndObjectCombo.GetCount() > 1)
	{
		m_wndObjectCombo.SetCurSel(1);
		OnComboSelectChanged();
	}
}

void CPropertiesWnd::OnUpdateProperties2(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(m_wndObjectCombo.GetCount() > 1);
	pCmdUI->SetCheck(m_wndObjectCombo.GetCurSel() == 1 ? 1 : 0);
}

void CPropertiesWnd::OnComboSelectChanged()
{
	int sel = m_wndObjectCombo.GetCurSel();
	if (sel < 0) return;

	CString strText;
	m_wndObjectCombo.GetLBText(sel, strText);

	if (strText.Find(_T("격자 및 캔버스")) >= 0)
	{
		InitCanvasPropList();
	}
	else if (strText.Find(_T("속성 GUI TEST")) >= 0)
	{
		// D2D Vector View가 표시됩니다
	}
	else
	{
		InitShapePropList();
	}

	AdjustLayout();
}

ge::Node* CPropertiesWnd::GetSelectedShape() const
{
	return (m_pActiveDoc && m_selectedNodeId) ? m_pActiveDoc->GetDoc().FindById(m_selectedNodeId) : nullptr;
}

void CPropertiesWnd::SetSelectedShape(ge::Node* pShape, CM2DrawDoc* pDoc)
{
	if (m_wndObjectCombo.GetSafeHwnd() == nullptr)
		return;

	const uint64_t newId = (pDoc && pShape) ? pShape->GetId() : 0;

	// 이미 동일한 도형이 선택되어 있는 상태에서 수치만 바뀐 경우:
	// 콤보박스 재구축 및 SetPaneMode(컨트롤 전체 파괴/재생성)를 건너뛰고 초고속 Transform만 동기화!
	if (newId != 0 && m_selectedNodeId == newId && m_pActiveDoc == pDoc)
	{
		if (m_wndD2DProp.GetSafeHwnd())
		{
			m_wndD2DProp.SyncTransformOnly();
		}
		return;
	}

	m_selectedNodeId = newId;
	m_pActiveDoc = pDoc;

	// 기존 쉐이프 설정 항목(이름에 "설정"이 들어있고 "격자"나 "TEST"가 아닌 항목) 제거
	for (int i = m_wndObjectCombo.GetCount() - 1; i >= 0; --i)
	{
		CString s;
		m_wndObjectCombo.GetLBText(i, s);
		if (s.Find(_T("격자")) < 0 && s.Find(_T("TEST")) < 0)
		{
			m_wndObjectCombo.DeleteString(i);
		}
	}

	if (ge::Node* shape = GetSelectedShape())
	{
		CString strTitle;
		if (!shape->GetName().empty())
			strTitle.Format(_T("{%s} 오브젝트 설정"), shape->GetName().c_str());
		else
			strTitle.Format(_T("{%s} 오브젝트 설정"), shape->GetShapeTypeName());

		int inserted = m_wndObjectCombo.InsertString(1, strTitle);
		m_wndObjectCombo.SetCurSel(inserted);

		if (m_paneMode == CD2DPropertyView::MODE_SHAPE)
		{
			SetWindowText(strTitle);
		}

		if (m_wndD2DProp.GetSafeHwnd())
		{
			m_wndD2DProp.SetTargetShape(pDoc, shape);
			m_wndD2DProp.SetPaneMode(CD2DPropertyView::MODE_SHAPE);
		}
	}
	else
	{
		if (m_wndD2DProp.GetSafeHwnd())
		{
			m_wndD2DProp.SetTargetShape(nullptr, nullptr);
		}

		if (m_paneMode == CD2DPropertyView::MODE_SHAPE)
		{
			SetWindowText(_T("오브젝트 설정"));
		}

		// 선택된 도형이 없고 현재 쉐이프 설정이 선택되어 있었다면 캔버스 설정으로 복귀
		int cur = m_wndObjectCombo.GetCurSel();
		if (cur < 0)
		{
			m_wndObjectCombo.SetCurSel(0);
			InitCanvasPropList();
		}
		else
		{
			CString s;
			m_wndObjectCombo.GetLBText(cur, s);
			if (s.Find(_T("TEST")) >= 0)
			{
				// TEST 탭 보고 있던 중이면 유지
			}
			else
			{
				m_wndObjectCombo.SetCurSel(0);
				InitCanvasPropList();
			}
		}
	}
}

void CPropertiesWnd::InitCanvasPropList()
{
	SetPropListFont();
	m_wndPropList.RemoveAll();

	m_wndPropList.EnableHeaderCtrl(FALSE);
	m_wndPropList.EnableDescriptionArea();
	m_wndPropList.SetVSDotNetLook();
	m_wndPropList.MarkModifiedProperties();

	// 1. 그리드 및 눈금자 설정 그룹
	CMFCPropertyGridProperty* pGroupGrid = new CMFCPropertyGridProperty(_T("격자 및 캔버스 설정"));

	// 그리드 표시 ON/OFF
	CMFCPropertyGridProperty* pPropShow = new CMFCPropertyGridProperty(
		_T("그리드 표시"),
		(_variant_t)(GetApp()->GetGrid().enabled ? true : false),
		_T("화면에 작업 그리드 격자선을 표시합니다.")
	);
	pPropShow->SetData(PROP_GRID_SHOW);
	pGroupGrid->AddSubItem(pPropShow);

	// 그리드 스냅 ON/OFF
	CMFCPropertyGridProperty* pPropSnap = new CMFCPropertyGridProperty(
		_T("그리드 스냅"),
		(_variant_t)(GetApp()->GetGrid().snapToGrid ? true : false),
		_T("마우스 포인터와 도형 정점을 그리드 교차점에 자동으로 맞춥니다.")
	);
	pPropSnap->SetData(PROP_GRID_SNAP);
	pGroupGrid->AddSubItem(pPropSnap);

	// 눈금자 표시 ON/OFF
	CMFCPropertyGridProperty* pPropRulerShow = new CMFCPropertyGridProperty(
		_T("눈금자 표시"),
		(_variant_t)(GetApp()->GetRuler().enabled ? true : false),
		_T("상단 및 좌측에 치수 눈금자를 표시합니다.")
	);
	pPropRulerShow->SetData(PROP_RULER_SHOW);
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
	pPropUnit->SetData(PROP_UNIT);
	pGroupGrid->AddSubItem(pPropUnit);

	// 그리드 간격 (단위 기준)
	CMFCPropertyGridProperty* pPropStep = new CMFCPropertyGridProperty(
		_T("그리드 간격 (단위)"),
		(_variant_t)(double)GetApp()->GetGrid().stepInUnit,
		_T("선택된 단위 기준의 격자 간격을 설정합니다.")
	);
	pPropStep->SetData(PROP_GRID_STEP);
	pGroupGrid->AddSubItem(pPropStep);

	// 주 격자선 배율
	CMFCPropertyGridProperty* pPropMajor = new CMFCPropertyGridProperty(
		_T("주 격자선 배율"),
		(_variant_t)(long)GetApp()->GetGrid().majorEvery,
		_T("몇 칸마다 굵은 주 격자선을 표시할지 지정합니다.")
	);
	pPropMajor->EnableSpinControl(TRUE, 2, 20);
	pPropMajor->SetData(PROP_GRID_MAJOR);
	pGroupGrid->AddSubItem(pPropMajor);

	// LOD 최소 화면 픽셀
	CMFCPropertyGridProperty* pPropMinStep = new CMFCPropertyGridProperty(
		_T("LOD 최소 간격 (px)"),
		(_variant_t)(double)GetApp()->GetGrid().minStepPx,
		_T("줌아웃 시 격자 간격이 이 값보다 작아지면 표시 밀도를 낮춥니다.")
	);
	pPropMinStep->SetData(PROP_GRID_MINSTEP);
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
	pPropClrMinor->SetData(PROP_GRID_COLOR_MIN);
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
	pPropClrMajor->SetData(PROP_GRID_COLOR_MAJ);
	pGroupColor->AddSubItem(pPropClrMajor);

	m_wndPropList.AddProperty(pGroupColor);
}

void CPropertiesWnd::InitShapePropList()
{
	SetPropListFont();
	m_wndPropList.RemoveAll();

	m_wndPropList.EnableHeaderCtrl(FALSE);
	m_wndPropList.EnableDescriptionArea();
	m_wndPropList.SetVSDotNetLook();
	m_wndPropList.MarkModifiedProperties();

	if (!GetSelectedShape())
	{
		CMFCPropertyGridProperty* pEmptyGroup = new CMFCPropertyGridProperty(_T("선택된 쉐이프 없음"));
		m_wndPropList.AddProperty(pEmptyGroup);
		return;
	}

	ge::Node* shape = GetSelectedShape();

	// 타이틀: {쉐이프 이름} 설정
	CString strGroupTitle;
	if (!shape->GetName().empty())
		strGroupTitle.Format(_T("{%s} 설정"), shape->GetName().c_str());
	else
		strGroupTitle.Format(_T("{%s} 설정"), shape->GetShapeTypeName());

	CMFCPropertyGridProperty* pGroupShape = new CMFCPropertyGridProperty(strGroupTitle);

	// 1. 쉐이프 이름 편집
	CString strNameVal = shape->GetName().empty() ? CString(shape->GetShapeTypeName()) : CString(shape->GetName().c_str());
	CMFCPropertyGridProperty* pPropName = new CMFCPropertyGridProperty(
		_T("쉐이프 이름"),
		(_variant_t)(LPCTSTR)strNameVal,
		_T("현재 선택된 쉐이프의 이름을 지정합니다.")
	);
	pPropName->SetData(PROP_SHAPE_NAME);
	pGroupShape->AddSubItem(pPropName);

	// 2. 라인 굵기 (D2D 실시간 지원, 1 ~ 50 px)
	CMFCPropertyGridProperty* pPropWidth = new CMFCPropertyGridProperty(
		_T("라인 굵기 (px)"),
		(_variant_t)(long)shape->GetStyle().stroke.width,
		_T("선(외곽선)의 굵기를 설정합니다.")
	);
	pPropWidth->EnableSpinControl(TRUE, 1, 50);
	pPropWidth->SetData(PROP_SHAPE_STROKE_W);
	pGroupShape->AddSubItem(pPropWidth);

	// 3. 라인 색상 (COLORREF 변환 및 알파 반영)
	COLORREF clrStroke = RGB(
		(BYTE)(shape->GetStyle().stroke.color.r * 255.0f),
		(BYTE)(shape->GetStyle().stroke.color.g * 255.0f),
		(BYTE)(shape->GetStyle().stroke.color.b * 255.0f)
	);
	CMFCPropertyGridColorProperty* pPropClrStroke = new CMFCPropertyGridColorProperty(
		_T("라인 색상"),
		clrStroke,
		nullptr,
		_T("선(외곽선)의 색상을 설정합니다.")
	);
	pPropClrStroke->EnableOtherButton(_T("기타..."));
	pPropClrStroke->SetData(PROP_SHAPE_STROKE_C);
	pGroupShape->AddSubItem(pPropClrStroke);

	// 4. 라인 스타일 (Direct2D DashStyle 지원: 실선, 점선, 대시선 등)
	CMFCPropertyGridProperty* pPropDash = new CMFCPropertyGridProperty(
		_T("라인 스타일"),
		_T("실선 (Solid)"),
		_T("선 모양(Direct2D Dash Style)을 지정합니다.")
	);
	pPropDash->AddOption(_T("실선 (Solid)"));
	pPropDash->AddOption(_T("대시선 (Dash)"));
	pPropDash->AddOption(_T("점선 (Dot)"));
	pPropDash->AddOption(_T("대시-점선 (DashDot)"));
	pPropDash->AllowEdit(FALSE);

	switch (ge::ClassifyDashArray(shape->GetStyle().stroke.dashArray, shape->GetStyle().stroke.width))
	{
	case ge::DashStyle::Dash:    pPropDash->SetValue(_T("대시선 (Dash)")); break;
	case ge::DashStyle::Dot:     pPropDash->SetValue(_T("점선 (Dot)")); break;
	case ge::DashStyle::DashDot: pPropDash->SetValue(_T("대시-점선 (DashDot)")); break;
	default:                     pPropDash->SetValue(_T("실선 (Solid)")); break;   // 사용자 정의 대시는 이 목록에 없으므로 속성창(D2D)에서 편집
	}
	pPropDash->SetData(PROP_SHAPE_DASH);
	pGroupShape->AddSubItem(pPropDash);

	m_wndPropList.AddProperty(pGroupShape);

	// 5. 채움(Fill) 설정 그룹
	CMFCPropertyGridProperty* pGroupFill = new CMFCPropertyGridProperty(_T("채움(Fill) 설정"));

	// 채움 유형: 채움 없음(투명), 단색 채움 (그라디언트는 Step 2.3)
	CMFCPropertyGridProperty* pPropFillType = new CMFCPropertyGridProperty(
		_T("채움 유형"),
		_T("채움 없음 (투명)"),
		_T("도형 내부를 채우는 방식(투명, 단색)을 설정합니다.")
	);
	pPropFillType->AddOption(_T("채움 없음 (투명)"));
	pPropFillType->AddOption(_T("단색 채움 (Solid)"));
	pPropFillType->AllowEdit(FALSE);

	switch (shape->GetStyle().fill.type)
	{
	case ge::PaintType::Solid: pPropFillType->SetValue(_T("단색 채움 (Solid)")); break;
	default:                   pPropFillType->SetValue(_T("채움 없음 (투명)")); break;
	}
	pPropFillType->SetData(PROP_SHAPE_FILL_TYP);
	pGroupFill->AddSubItem(pPropFillType);

	// 채움 색상
	COLORREF clrFill = RGB(
		(BYTE)(shape->GetStyle().fill.color.r * 255.0f),
		(BYTE)(shape->GetStyle().fill.color.g * 255.0f),
		(BYTE)(shape->GetStyle().fill.color.b * 255.0f)
	);
	CMFCPropertyGridColorProperty* pPropClrFill = new CMFCPropertyGridColorProperty(
		_T("채움 색상"),
		clrFill,
		nullptr,
		_T("채움 색상(단색 또는 기본 패턴 색)을 설정합니다.")
	);
	pPropClrFill->EnableOtherButton(_T("기타..."));
	pPropClrFill->SetData(PROP_SHAPE_FILL_CLR);
	pGroupFill->AddSubItem(pPropClrFill);

	m_wndPropList.AddProperty(pGroupFill);
}

void CPropertiesWnd::InitTestPropList()
{
	SetPropListFont();
	m_wndPropList.RemoveAll();

	m_wndPropList.EnableHeaderCtrl(FALSE);
	m_wndPropList.EnableDescriptionArea();
	m_wndPropList.SetVSDotNetLook();
	m_wndPropList.MarkModifiedProperties();

	// -------------------------------------------------------------
	// 1. [체크박스 컨트롤] 테스트 그룹 (Inkscape 스냅 옵션 방식)
	// -------------------------------------------------------------
	CMFCPropertyGridProperty* pGroupCheck = new CMFCPropertyGridProperty(_T("1. 체크박스 / 토글 스위치"));

	CMFCPropertyGridProperty* pChk1 = new CMFCPropertyGridProperty(
		_T("스냅 활성화 (Snapping)"),
		(_variant_t)true,
		_T("[체크박스] Inkscape의 최상단 스냅 전역 ON/OFF 토글입니다.")
	);
	pChk1->SetData(PROP_TEST_CHECK1);
	pGroupCheck->AddSubItem(pChk1);

	CMFCPropertyGridProperty* pChk2 = new CMFCPropertyGridProperty(
		_T("경계 상자(BBox) 모서리 스냅"),
		(_variant_t)false,
		_T("[체크박스] 닫힌 도형 외곽 바운딩 모서리에 자동 스냅을 겁니다.")
	);
	pChk2->SetData(PROP_TEST_CHECK2);
	pGroupCheck->AddSubItem(pChk2);

	m_wndPropList.AddProperty(pGroupCheck);

	// -------------------------------------------------------------
	// 2. [슬라이더 / 스핀 바] 테스트 그룹
	// -------------------------------------------------------------
	CMFCPropertyGridProperty* pGroupBar = new CMFCPropertyGridProperty(_T("2. 슬라이더 & 숫자 바 (0~100)"));

	CMFCPropertyGridProperty* pSlider = new CMFCPropertyGridProperty(
		_T("불투명도 / 흐림 강도 (%)"),
		(_variant_t)(long)85,
		_T("[수치 조절 바] 마우스 드래그 또는 스핀 단추로 수치를 미세 조절합니다.")
	);
	pSlider->EnableSpinControl(TRUE, 0, 100);
	pSlider->SetData(PROP_TEST_SLIDER);
	pGroupBar->AddSubItem(pSlider);

	m_wndPropList.AddProperty(pGroupBar);

	// -------------------------------------------------------------
	// 3. [드롭다운 / 버튼 / 메뉴] 테스트 그룹
	// -------------------------------------------------------------
	CMFCPropertyGridProperty* pGroupBtn = new CMFCPropertyGridProperty(_T("3. 모드 선택 버튼 / 콤보"));

	CMFCPropertyGridProperty* pComboMode = new CMFCPropertyGridProperty(
		_T("혼합 모드 (Blend Mode)"),
		_T("정상 (Normal)"),
		_T("[선택 버튼/목록] 레이어 및 채움 블렌딩 모드를 선택합니다.")
	);
	pComboMode->AddOption(_T("정상 (Normal)"));
	pComboMode->AddOption(_T("곱하기 (Multiply)"));
	pComboMode->AddOption(_T("스크린 (Screen)"));
	pComboMode->AddOption(_T("오버레이 (Overlay)"));
	pComboMode->AllowEdit(FALSE);
	pComboMode->SetData(PROP_TEST_BUTTON);
	pGroupBtn->AddSubItem(pComboMode);

	m_wndPropList.AddProperty(pGroupBtn);

	// -------------------------------------------------------------
	// 4. [컬러피커 / 팔레트 버튼] 테스트 그룹
	// -------------------------------------------------------------
	CMFCPropertyGridProperty* pGroupColor = new CMFCPropertyGridProperty(_T("4. 컬러 피커 (HSL / RGB)"));

	CMFCPropertyGridColorProperty* pColorProp = new CMFCPropertyGridColorProperty(
		_T("대표 테마 색상"),
		RGB(255, 0, 128),
		nullptr,
		_T("[컬러 버튼] 클릭하면 색상 선택 팔레트 및 다이얼로그가 호출됩니다.")
	);
	pColorProp->EnableOtherButton(_T("Color Wheel / 고급 피커..."));
	pColorProp->SetData(PROP_TEST_COLOR);
	pGroupColor->AddSubItem(pColorProp);

	m_wndPropList.AddProperty(pGroupColor);

	// -------------------------------------------------------------
	// 5. [비교 요약 안내 그룹]
	// -------------------------------------------------------------
	CMFCPropertyGridProperty* pGroupCompare = new CMFCPropertyGridProperty(_T("5. GUI 방식 비교 요약"));
	pGroupCompare->AddSubItem(new CMFCPropertyGridProperty(
		_T("A. MFC Custom"),
		_T("현 방식 + 다크모드 (#2d2d2d) 튜닝"),
		_T("장점: 외부 라이브러리 0개, 즉시 사용 가능, 가볍고 안정적")
	));
	pGroupCompare->AddSubItem(new CMFCPropertyGridProperty(
		_T("B. ImGui Overlay"),
		_T("C++ 소스 5개 복사만으로 즉시 연동"),
		_T("장점: 질문하신 원형 ColorWheel 완벽 내장, 게임/블렌더급 다크 비주얼")
	));
	pGroupCompare->AddSubItem(new CMFCPropertyGridProperty(
		_T("C. Direct2D Custom"),
		_T("D2D Vector Engine으로 직접 렌더링"),
		_T("장점: 디자인/애니메이션 100% 자유도, 초경량, GPU 가속")
	));
	m_wndPropList.AddProperty(pGroupCompare);
}

LRESULT CPropertiesWnd::OnPropertyChanged(WPARAM /*wParam*/, LPARAM lParam)
{
	CMFCPropertyGridProperty* pProp = reinterpret_cast<CMFCPropertyGridProperty*>(lParam);
	if (!pProp)
		return 0;

	DWORD_PTR id = pProp->GetData();
	COleVariant val = pProp->GetValue();

	CString strLogMsg;

	// 1. 캔버스 / 그리드 속성 변경 처리
	switch (id)
	{
	case PROP_GRID_SHOW:
		val.ChangeType(VT_BOOL);
		GetApp()->GetGrid().enabled = (val.boolVal != VARIANT_FALSE);
		strLogMsg.Format(_T("[속성 변경] 그리드 표시: %s"), GetApp()->GetGrid().enabled ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	case PROP_GRID_STEP:
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
	case PROP_GRID_MAJOR:
		val.ChangeType(VT_I4);
		if (val.lVal >= 1)
		{
			GetApp()->GetGrid().majorEvery = static_cast<int>(val.lVal);
			strLogMsg.Format(_T("[속성 변경] 주 격자선 배율: %d 칸마다"), GetApp()->GetGrid().majorEvery);
			LogAppEvent(strLogMsg);
		}
		break;
	case PROP_GRID_MINSTEP:
		val.ChangeType(VT_R8);
		if (val.dblVal >= 2.0)
		{
			GetApp()->GetGrid().minStepPx = static_cast<float>(val.dblVal);
			strLogMsg.Format(_T("[속성 변경] LOD 최소 간격: %.1f px"), GetApp()->GetGrid().minStepPx);
			LogAppEvent(strLogMsg);
		}
		break;
	case PROP_GRID_COLOR_MIN:
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
	case PROP_GRID_COLOR_MAJ:
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
	case PROP_RULER_SHOW:
		val.ChangeType(VT_BOOL);
		GetApp()->GetRuler().enabled = (val.boolVal != VARIANT_FALSE);
		strLogMsg.Format(_T("[속성 변경] 눈금자 표시: %s"), GetApp()->GetRuler().enabled ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	case PROP_UNIT:
	{
		CString strUnit = pProp->GetValue().bstrVal;
		CanvasUnit newUnit = CanvasUnit::Pixel;
		if (strUnit.Find(_T("밀리미터")) >= 0 || strUnit.Find(_T("mm")) >= 0)
			newUnit = CanvasUnit::MM;
		else if (strUnit.Find(_T("센티미터")) >= 0 || strUnit.Find(_T("cm")) >= 0)
			newUnit = CanvasUnit::CM;
		else if (strUnit.Find(_T("인치")) >= 0 || strUnit.Find(_T("in")) >= 0)
			newUnit = CanvasUnit::Inch;
		else if (strUnit.Find(_T("rem")) >= 0)
			newUnit = CanvasUnit::Rem;
		else
			newUnit = CanvasUnit::Pixel;

		GetApp()->GetGrid().SetUnit(newUnit);

		// 간격 수치가 새 단위에 맞춰 환산되었으므로 프로퍼티 리스트의 PROP_GRID_STEP 값도 갱신
		CMFCPropertyGridProperty* pStepProp = m_wndPropList.FindItemByData(PROP_GRID_STEP);
		if (pStepProp)
		{
			pStepProp->SetValue((_variant_t)(double)GetApp()->GetGrid().stepInUnit);
		}

		strLogMsg.Format(_T("[속성 변경] 측정 단위: %s (간격: %.2f %s, 1단위 = %.2f px)"),
			GetCanvasUnitName(GetApp()->GetGrid().unit),
			GetApp()->GetGrid().stepInUnit,
			GetCanvasUnitSuffix(GetApp()->GetGrid().unit),
			GetPixelsPerCanvasUnit(GetApp()->GetGrid().unit));
		LogAppEvent(strLogMsg);
		break;
	}
	case PROP_GRID_SNAP:
	{
		val.ChangeType(VT_BOOL);
		bool bSnap = (val.boolVal != VARIANT_FALSE);
		GetApp()->GetGrid().snapToGrid = bSnap;
		GetApp()->GetSnapManager().EnableTarget(SnapTargets::Grid, bSnap);
		strLogMsg.Format(_T("[속성 변경] 그리드 스냅: %s"), bSnap ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	}

	// 2. 쉐이프 속성 변경 처리
	case PROP_SHAPE_NAME:
	{
		if (GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			shape->SetName(val.bstrVal ? val.bstrVal : L"");
			CString strTitle;
			strTitle.Format(_T("{%s} 설정"), shape->GetName().c_str());
			if (m_wndObjectCombo.GetCount() > 1)
			{
				m_wndObjectCombo.DeleteString(1);
				m_wndObjectCombo.InsertString(1, strTitle);
				m_wndObjectCombo.SetCurSel(1);
			}
			strLogMsg.Format(_T("[쉐이프 속성] 이름 변경: %s"), shape->GetName().c_str());
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case PROP_SHAPE_STROKE_W:
	{
		val.ChangeType(VT_R8);
		if (GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			shape->GetStyleRef().stroke.width = static_cast<float>(val.dblVal);
			shape->Invalidate();
			strLogMsg.Format(_T("[쉐이프 속성] 라인 굵기: %.1f px"), shape->GetStyle().stroke.width);
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case PROP_SHAPE_STROKE_C:
	{
		CMFCPropertyGridColorProperty* pColor = dynamic_cast<CMFCPropertyGridColorProperty*>(pProp);
		if (pColor && GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			COLORREF c = pColor->GetColor();
			shape->GetStyleRef().stroke.color = D2D1::ColorF(
				GetRValue(c) / 255.0f,
				GetGValue(c) / 255.0f,
				GetBValue(c) / 255.0f,
				1.0f
			);
			shape->Invalidate();
			strLogMsg.Format(_T("[쉐이프 속성] 라인 색상: RGB(%d, %d, %d)"), GetRValue(c), GetGValue(c), GetBValue(c));
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case PROP_SHAPE_DASH:
	{
		if (GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			CString strDash = val.bstrVal;
			ge::Stroke& stroke = shape->GetStyleRef().stroke;
			if (strDash.Find(_T("대시-점선")) >= 0)
				stroke.dashArray = ge::MakeDashArray(ge::DashStyle::DashDot, stroke.width);
			else if (strDash.Find(_T("대시선")) >= 0)
				stroke.dashArray = ge::MakeDashArray(ge::DashStyle::Dash, stroke.width);
			else if (strDash.Find(_T("점선")) >= 0)
				stroke.dashArray = ge::MakeDashArray(ge::DashStyle::Dot, stroke.width);
			else
				stroke.dashArray.clear();

			shape->Invalidate();
			strLogMsg.Format(_T("[쉐이프 속성] 라인 스타일: %s"), (LPCTSTR)strDash);
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case PROP_SHAPE_FILL_TYP:
	{
		if (GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			CString strFillType = val.bstrVal;
			if (strFillType.Find(_T("단색")) >= 0)
				shape->GetStyleRef().fill.type = ge::PaintType::Solid;
			else
				shape->GetStyleRef().fill.type = ge::PaintType::None;

			shape->Invalidate();
			strLogMsg.Format(_T("[쉐이프 속성] 채움 유형: %s"), (LPCTSTR)strFillType);
			LogAppEvent(strLogMsg);
		}
		break;
	}
	case PROP_SHAPE_FILL_CLR:
	{
		CMFCPropertyGridColorProperty* pColor = dynamic_cast<CMFCPropertyGridColorProperty*>(pProp);
		if (pColor && GetSelectedShape())
		{
			ge::Node* shape = GetSelectedShape();
			COLORREF c = pColor->GetColor();
			shape->GetStyleRef().fill.color = D2D1::ColorF(
				GetRValue(c) / 255.0f,
				GetGValue(c) / 255.0f,
				GetBValue(c) / 255.0f,
				1.0f
			);
			// 사용자가 채움 색을 선택했는데 채움 유형이 '투명'이었다면 자동으로 '단색 채움'으로 활성화
			if (shape->GetStyle().fill.type == ge::PaintType::None)
			{
				shape->GetStyleRef().fill.type = ge::PaintType::Solid;
			}
			shape->Invalidate();
			strLogMsg.Format(_T("[쉐이프 속성] 채움 색상: RGB(%d, %d, %d)"), GetRValue(c), GetGValue(c), GetBValue(c));
			LogAppEvent(strLogMsg);
		}
		break;
	}

	// 3. 속성 GUI TEST 변경 처리
	case PROP_TEST_CHECK1:
	case PROP_TEST_CHECK2:
	{
		val.ChangeType(VT_BOOL);
		bool bChecked = (val.boolVal != VARIANT_FALSE);
		strLogMsg.Format(_T("[GUI TEST] 체크박스 '%s': %s"), (LPCTSTR)pProp->GetName(), bChecked ? _T("ON") : _T("OFF"));
		LogAppEvent(strLogMsg);
		break;
	}
	case PROP_TEST_SLIDER:
	{
		val.ChangeType(VT_I4);
		strLogMsg.Format(_T("[GUI TEST] 슬라이더 바 값 변경: %d%%"), (int)val.lVal);
		LogAppEvent(strLogMsg);
		break;
	}
	case PROP_TEST_BUTTON:
	{
		strLogMsg.Format(_T("[GUI TEST] 선택 모드 변경: %s"), (LPCTSTR)val.bstrVal);
		LogAppEvent(strLogMsg);
		break;
	}
	case PROP_TEST_COLOR:
	{
		CMFCPropertyGridColorProperty* pColor = dynamic_cast<CMFCPropertyGridColorProperty*>(pProp);
		if (pColor)
		{
			COLORREF c = pColor->GetColor();
			strLogMsg.Format(_T("[GUI TEST] 컬러 피커 색상: RGB(%d, %d, %d)"), GetRValue(c), GetGValue(c), GetBValue(c));
			LogAppEvent(strLogMsg);
		}
		break;
	}
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
	if (m_wndD2DProp.GetSafeHwnd() && m_wndD2DProp.IsWindowVisible())
	{
		m_wndD2DProp.SetFocus();
	}
	else
	{
		m_wndPropList.SetFocus();
	}
}

void CPropertiesWnd::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CDockablePane::OnSettingChange(uFlags, lpszSection);
	SetPropListFont();
}