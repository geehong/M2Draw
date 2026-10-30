#pragma once

#include <string>
#include <memory>

// HTML 미리보기 창 (Step 4.6): Microsoft Edge WebView2 로 현재 문서의 HtmlExporter 결과를 실시간으로 보여준다.
//  - 편집이 멈추면(변경 카운터가 한 번 안정되면) 자동으로 HTML 을 다시 만들어 표시 (4.6.2)
//  - 폭 슬라이더/프리셋(모바일/태블릿/데스크톱)으로 뷰포트 폭을 바꿔 반응형을 확인 (4.6.3)
//  - WebView2 런타임이 없으면 임시 HTML 파일을 시스템 브라우저로 열고 2초마다 자동 새로고침 (4.6.0)
//  - PrintToPdf 로 PDF 저장 (4.6.5)
// 모델은 이 창을 모른다: 문서에서 HTML 문자열만 받아간다 (CM2DrawDoc::BuildPreviewHtml).

struct ICoreWebView2Controller;
struct ICoreWebView2;
class CM2DrawDoc;
namespace ge { class FrameNode; }

class CHtmlPreviewFrame : public CFrameWnd
{
	DECLARE_DYNAMIC(CHtmlPreviewFrame)

public:
	// 창을 열거나(없으면 생성) 앞으로 가져온다
	static void Show();

protected:
	CHtmlPreviewFrame();
	~CHtmlPreviewFrame() override;

	afx_msg int  OnCreate(LPCREATESTRUCT cs);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnHScroll(UINT code, UINT pos, CScrollBar* bar);
	afx_msg BOOL OnEraseBkgnd(CDC* dc);
	afx_msg LRESULT OnInitWebView(WPARAM, LPARAM);
	afx_msg void OnPreset(UINT id);
	afx_msg void OnRefresh();
	afx_msg void OnOpenBrowser();
	afx_msg void OnSavePdf();
	void PostNcDestroy() override;
	DECLARE_MESSAGE_MAP()

private:
	void Layout();
	void SetWidth(int w, bool updateSlider);
	void SetStatus(const CString& s);
	void InitWebView();
	void UseBrowserFallback(const CString& why);
	void Refresh();                       // 현재 문서 -> HTML -> WebView2 (또는 임시 파일)
	CM2DrawDoc* ActiveDoc() const;
	static std::wstring TempHtmlPath();
	bool WriteTempHtml(const std::string& html, bool autoReload);

	static CHtmlPreviewFrame* s_inst;

	CButton m_btnMobile, m_btnTablet, m_btnDesktop, m_btnRefresh, m_btnBrowser, m_btnPdf;
	CSliderCtrl m_slider;
	CStatic m_lblWidth, m_lblStatus;
	CFont m_font;

	ICoreWebView2Controller* m_controller = nullptr;
	ICoreWebView2*           m_webview = nullptr;
	std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);   // 비동기 콜백이 소멸된 창을 건드리지 않게

	int  m_width = 1280;
	bool m_ready = false;                 // WebView2 준비 완료 또는 폴백 결정됨
	bool m_fallback = false;              // 시스템 브라우저 사용
	bool m_browserOpened = false;
	bool m_dirty = true;
	unsigned long long m_seenVersion = 0;
	CM2DrawDoc* m_doc = nullptr;
	const ge::FrameNode* m_artboard = nullptr;
	std::string m_lastHtml;
};
