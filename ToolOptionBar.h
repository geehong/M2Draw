#pragma once
#include "ITool.h"
#include "ToolManager.h"

// 상단 도구 옵션바 컨트롤 ID (resource.h 와 충돌하지 않도록 높은 범위 사용)
#define ID_OPT_LABEL           41000
#define ID_OPT_X               41001
#define ID_OPT_Y               41002
#define ID_OPT_W               41003
#define ID_OPT_H               41004
#define ID_OPT_ROT             41005
#define ID_OPT_RX              41006
#define ID_OPT_RY              41007
#define ID_OPT_LINK_RATIO      41008  // 비율 링크 토글 [🔗]
#define ID_OPT_RESET_CORNER    41009  // 모서리 리셋 [⏢]
#define ID_OPT_EQUILATERAL     41010  // 정삼각형 고정 [정삼각형]
#define ID_OPT_PATH_CLOSED     41011  // 닫힌 패스 토글 [닫힌패스✓] / [열린패스]
#define ID_OPT_PATH_COMPLETE   41012  // 패스 완료 [완료✓]
#define ID_OPT_PATH_CANCEL     41013  // 패스 취소 [취소✕]
#define ID_OPT_SIDES           41014  // 정다각형 변의 개수 N (3~32)
#define ID_OPT_RADIUS          41015  // 외접 반경 R
#define ID_OPT_POINTS          41016  // 별 꼭짓점 수 N (3~32)
#define ID_OPT_INNER_RATIO     41017  // 별 내경 비율 % (5~95)
#define ID_OPT_TEETH           41018  // 기어 톱니 수 (4~48)
#define ID_OPT_DEPTH_RATIO     41019  // 기어 톱니 깊이 % (5~50)
#define ID_OPT_BORE_RATIO      41020  // 기어 중앙 구멍(Bore) 비율 % (5~80)
#define ID_OPT_TURNS           41021  // 나선 회전 수 (0.5~10.0)
#define ID_OPT_DRAW_CENTER     41022  // 중심 => 바깥쪽 그리기 토글 [From Center✓] / [From Corner]
#define ID_OPT_FIRST           ID_OPT_X
#define ID_OPT_LAST            ID_OPT_DRAW_CENTER

// ----------------------------------------------------------------------------
// 진하고 선명한 텍스트 라벨 버튼
// ----------------------------------------------------------------------------
class CToolBarLabel : public CMFCToolBarButton
{
	DECLARE_DYNCREATE(CToolBarLabel)
public:
	CToolBarLabel() : CMFCToolBarButton() { m_bText = TRUE; m_bImage = FALSE; }
	CToolBarLabel(UINT nID, LPCTSTR lpszText)
		: CMFCToolBarButton(nID, -1, lpszText)
	{
		m_bText = TRUE;
		m_bImage = FALSE;
	}

	virtual BOOL CanBeStored() const override { return FALSE; }
	virtual BOOL HaveHotBorder() const override { return FALSE; }
	virtual SIZE OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz) override;

	virtual void OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages,
		BOOL bHorz = TRUE, BOOL bCustomizeMode = FALSE,
		BOOL bHighlight = FALSE, BOOL bDrawBorder = FALSE,
		BOOL bGrayDisabledButtons = FALSE) override;
};

// ----------------------------------------------------------------------------
// 수치 입력용 스핀(▲▼) 내장 에디트 컨트롤
// ----------------------------------------------------------------------------
class CToolOptionBar;

class CSpinEditCtrl : public CEdit
{
	DECLARE_DYNAMIC(CSpinEditCtrl)
public:
	CSpinEditCtrl(CToolOptionBar* pBar = nullptr, UINT nFieldID = 0) noexcept;
	virtual ~CSpinEditCtrl();

	void InitSpin();

protected:
	CToolOptionBar* m_pBar = nullptr;
	UINT            m_nFieldID = 0;
	CSpinButtonCtrl m_spin;
	CFont           m_fontBold;

	void AdjustValue(double step);

	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDeltaposSpin(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
	afx_msg void OnEnChange();

	DECLARE_MESSAGE_MAP()
};

// ----------------------------------------------------------------------------
// 스핀 버튼이 내장된 툴바 에디트 박스 버튼
// ----------------------------------------------------------------------------
class COptionSpinEditButton : public CMFCToolBarEditBoxButton
{
	DECLARE_DYNCREATE(COptionSpinEditButton)
public:
	COptionSpinEditButton() : CMFCToolBarEditBoxButton() {}
	COptionSpinEditButton(UINT uiID, int iImage, DWORD dwStyle = ES_AUTOHSCROLL, int iWidth = 0)
		: CMFCToolBarEditBoxButton(uiID, iImage, dwStyle, iWidth)
	{
	}

	virtual CEdit* CreateEdit(CWnd* pWndParent, const CRect& rect) override;
};

// 도구 옵션바: 활성 도구에 따라 표시되는 수치 입력 컨트롤을 전환합니다.
// MFC 기본 컨트롤(CMFCToolBar + COptionSpinEditButton + CMFCToolBarButton)로 구현합니다.
class CToolOptionBar : public CMFCToolBar, public IToolChangeListener
{
	DECLARE_DYNAMIC(CToolOptionBar)
public:
	CToolOptionBar() noexcept;

	// 생성 직후 한 번 호출: ToolManager 리스너 등록 및 초기 구성
	void Attach();

	// IToolChangeListener
	void OnToolChanged(ToolType newTool) override;

	// 도구/모드에 맞게 컨트롤 재구성. editing=true 이면 "수정" 모드 레이블
	void SetMode(ToolType tool, bool editing);

	// 선택된 도형/문서 상태로부터 옵션바 수치 실시간 동기화
	void SyncSelection(CM2DrawDoc* pDoc, int selectedIndex);

	// 도형 생성(만들기) 중 마우스 이동에 따른 실시간 좌표/크기 동기화
	void SyncCreationCoordinates(float x, float y, float w, float h, bool hasFixedStart);

	// 문서 -> 컨트롤 값 갱신 (m_bUpdating 가드로 루프 방지)
	void SetFieldText(UINT id, const CString& text);
	bool IsUpdating() const noexcept { return m_bUpdating; }

	// 컨트롤 -> 문서: 편집 상자에서 Enter 또는 버튼 토글 시 호출 (MainFrame 에서 전달)
	void OnOptionCommit(UINT id);

	// 고정 높이 바: 세로 도킹 금지
	virtual BOOL CanBeRestored() const override { return FALSE; }
	virtual BOOL OnBeforeRemoveButton(CMFCToolBarButton*, DROPEFFECT) override { return FALSE; }
	virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz) override;

	// 도구에 따라 버튼 구성이 동적으로 바뀌므로 레지스트리 저장/복원은 하지 않습니다.
	virtual BOOL LoadState(LPCTSTR = nullptr, int = -1, UINT = (UINT)-1) override { return TRUE; }
	virtual BOOL SaveState(LPCTSTR = nullptr, int = -1, UINT = (UINT)-1) override { return TRUE; }

protected:
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()

private:
	void AddLabel(const CString& text);
	void AddEdit(UINT id, const CString& caption, int width = 72);
	void AddButton(UINT id, const CString& text);
	CString GetFieldText(UINT id);
	static CString GetModeLabel(ToolType tool, bool editing);

	ToolType    m_curTool  = ToolType::Select;
	bool        m_bEditing = false;
	bool        m_bUpdating = false;
	bool        m_bAttached = false;
	CM2DrawDoc* m_pDoc = nullptr;
	int         m_selectedIndex = -1;

	// 비율 링크 및 정삼각형/RxRy 링크 토글 상태
	bool        m_bLinkRatio = true;
	bool        m_bLinkRxRy = true;
	bool        m_bEquilateral = false;
	bool        m_bPathClosed = false;
	bool        m_bDrawFromCenter = false;
};
