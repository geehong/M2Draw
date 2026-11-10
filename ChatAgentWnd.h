#pragma once

#include <afxwin.h>
#include <afxrich.h>
#include <string>
#include <vector>

// 출력 창의 "ChatAgent" 탭 (7.2.6): 앱 안에서 Claude 에게 M2Draw 작업을 요청하고 대화한다.
// 메시지 한 건마다 Claude Code 를 print 모드로 실행하고, 같은 세션 id 로 대화를 이어 간다.
class CChatAgentWnd : public CWnd
{
public:
	CChatAgentWnd();
	virtual ~CChatAgentWnd();

	// 작업 패키지 폴더를 연결한다 (에이전트 작업 시작 때 호출). 바뀌면 대화 세션은 새로 시작한다
	// site 는 대상 사이트 주소 (지시문에 넣어 에이전트가 어떤 사이트인지 알게 한다). 비우면 넣지 않는다
	void SetPackageDir(const CString& dir, const CString& site = _T(""));

	// 활성 탭이 바뀔 때 호출: 그 문서의 에이전트 패키지로 연결한다 (없으면 없다고 안내)
	void UseDocument(const CString& dir, const CString& site);

	// Analyze 버튼과 같은 요청을 보낸다
	void RunAnalyze();
	// 선택 프레임 하나만 다루라는 요청을 보낸다 (7.2.11)
	void RunFrameRequest(const CString& frameName, float width, float height);
	// 자동 처리: 구조 파악 → 웹페이지 생성 (Get Ref 자동 선택 시, Auto 버튼)
	void RunAutoChain();
	void AppendNotice(const CString& line) { AppendLine(line); }   // 채팅 대화창에 안내 한 줄
	// 앱에서 고친 내용이 current/ 에 저장되면 탭에 ● 를 붙인다 (에이전트가 읽도록 요청을 보내면 지운다, 7.2.12)
	void SetChangedBadge(bool on);

protected:
	BOOL PreTranslateMessage(MSG* pMsg) override;
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR idEvent);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);   // 퀵 버튼 직접 그리기 (작업 상태 색)
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);         // 상태 줄 글자색
	afx_msg void OnSendClicked();
	afx_msg void OnQuickClicked(UINT id);   // 퀵 액션 버튼 (Analyze / Generate / Apply / New Chat / Start)
	afx_msg void OnFrameClicked();          // Frame 버튼: 선택한 프레임만 패키지로 보낸다 (7.2.11)
	afx_msg void OnAutoClicked();           // Auto 버튼: 시작 → 구조 파악 → 웹페이지 생성
	afx_msg LRESULT OnChatReply(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnChatVersion(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	void AppendLine(const CString& line);
	void AppendLabel(const CString& label);   // 화자 이름 (굵게)
	void RenderMarkdown(const CString& md);    // 마크다운을 서식(제목·굵게·코드·목록·표)으로 바꿔 대화 기록에 넣는다
	void SendToAgent();
	// 지시문을 에이전트에 보낸다. shown 은 대화 기록에 보일 문구, button 은 실행 중 깜빡일 버튼 번호(없으면 -1)
	bool SendText(const CString& prompt, const CString& shown, int button = -1);
	// 작업 상태: 실행 중인 작업 이름과 경과 시간을 상태 줄에 보이고, 해당 버튼을 깜빡인다
	void StartJob(int button, const CString& label);
	void FinishJob(bool ok);
	void UpdateJobUi();
	void SetAgentName(const CString& name);
	void UpdateTabLabel();   // 탭 이름 = 에이전트 이름 (+ 변경됨 표시)

	CRichEditCtrl m_transcript;   // 대화 기록 (읽기 전용, 마크다운 서식)
	CEdit   m_input;
	CButton m_send;
	CButton m_quick[7];       // 퀵 액션: [0]=Analyze [1]=Generate [2]=Apply [3]=New Chat [4]=Start [5]=Frame [6]=Auto
	CString m_quickBase[7];   // 버튼의 원래 글자 (깜빡일 때 바뀌므로 따로 보관)
	CStatic m_status;         // 작업 상태 줄
	int     m_jobButton = -1; // 마지막 작업의 버튼 번호 (-1 = 없음). 끝난 뒤에도 빨간색으로 남는다
	std::vector<int> m_chain; // 자동 처리의 다음 단계 (버튼 번호)
	bool    m_chainActive = false;   // 자동 처리 중이면 작업 깜빡임은 Auto 버튼에 나온다
	int     m_jobState = 0;   // 0 없음, 1 실행 중 (파란색 깜빡임), 2 끝 (빨간색 고정)
	COLORREF m_statusColor = RGB(60, 60, 60);
	CString m_jobLabel;       // 실행 중인 작업 이름
	DWORD   m_jobStart = 0;   // 작업 시작 시각 (GetTickCount)
	bool    m_blinkOn = false;
	CString m_site;           // 대상 사이트 주소
	CString m_packageDir;
	std::string m_sessionId;
	bool    m_busy = false;
	CString m_agentName = _T("Claude Agent");   // 버전을 읽으면 Claude <버전> Agent 로 바뀐다
	bool    m_changed = false;
};
