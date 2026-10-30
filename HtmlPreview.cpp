#include "pch.h"
#include "HtmlPreview.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"
#include "D2DGraphicEngine.h"
#include <wrl.h>
#include <WebView2.h>
#include <shellapi.h>
#include <fstream>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
enum { ID_BTN_MOBILE = 3001, ID_BTN_TABLET, ID_BTN_DESKTOP, ID_BTN_REFRESH, ID_BTN_BROWSER, ID_BTN_PDF, ID_SLIDER, ID_LBL_WIDTH, ID_LBL_STATUS };
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTickMs = 500;
constexpr UINT WM_INIT_WEBVIEW = WM_APP + 41;
constexpr size_t kMaxNavigateToString = 1800 * 1024;   // NavigateToString 은 2MB 제한 -> 넘으면 임시 파일로 이동
}

CHtmlPreviewFrame* CHtmlPreviewFrame::s_inst = nullptr;

IMPLEMENT_DYNAMIC(CHtmlPreviewFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CHtmlPreviewFrame, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_HSCROLL()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_INIT_WEBVIEW, &CHtmlPreviewFrame::OnInitWebView)
	ON_COMMAND_RANGE(ID_BTN_MOBILE, ID_BTN_DESKTOP, &CHtmlPreviewFrame::OnPreset)
	ON_COMMAND(ID_BTN_REFRESH, &CHtmlPreviewFrame::OnRefresh)
	ON_COMMAND(ID_BTN_BROWSER, &CHtmlPreviewFrame::OnOpenBrowser)
	ON_COMMAND(ID_BTN_PDF, &CHtmlPreviewFrame::OnSavePdf)
END_MESSAGE_MAP()

CHtmlPreviewFrame::CHtmlPreviewFrame() {}
CHtmlPreviewFrame::~CHtmlPreviewFrame() {}

void CHtmlPreviewFrame::Show()
{
	if (s_inst && ::IsWindow(s_inst->m_hWnd)) {
		if (s_inst->IsIconic()) s_inst->ShowWindow(SW_RESTORE);
		s_inst->SetForegroundWindow();
		s_inst->m_dirty = true;
		return;
	}
	auto* f = new CHtmlPreviewFrame();
	s_inst = f;
	CRect rc(0, 0, 1100, 800);
	if (!f->Create(nullptr, _T("HTML 미리보기 (WebView2)"), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CRect(120, 120, 1220, 920), AfxGetMainWnd())) {
		s_inst = nullptr;
		delete f;
		return;
	}
	f->ShowWindow(SW_SHOW);
	f->UpdateWindow();
}

void CHtmlPreviewFrame::PostNcDestroy()
{
	if (s_inst == this) s_inst = nullptr;
	delete this;
}

int CHtmlPreviewFrame::OnCreate(LPCREATESTRUCT cs)
{
	if (CFrameWnd::OnCreate(cs) == -1) return -1;
	m_font.CreateStockObject(DEFAULT_GUI_FONT);
	CRect z(0, 0, 10, 10);
	m_btnMobile.Create(_T("모바일 375"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, z, this, ID_BTN_MOBILE);
	m_btnTablet.Create(_T("태블릿 768"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, z, this, ID_BTN_TABLET);
	m_btnDesktop.Create(_T("데스크톱 1280"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, z, this, ID_BTN_DESKTOP);
	m_slider.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, z, this, ID_SLIDER);
	m_slider.SetRange(320, 1920, TRUE);
	m_slider.SetPos(m_width);
	m_lblWidth.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, z, this, ID_LBL_WIDTH);
	m_btnRefresh.Create(_T("새로고침"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, z, this, ID_BTN_REFRESH);
	m_btnBrowser.Create(_T("브라우저로 열기"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, z, this, ID_BTN_BROWSER);
	m_btnPdf.Create(_T("PDF 저장"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, z, this, ID_BTN_PDF);
	m_lblStatus.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE | SS_ENDELLIPSIS, z, this, ID_LBL_STATUS);
	for (CWnd* w : { (CWnd*)&m_btnMobile, (CWnd*)&m_btnTablet, (CWnd*)&m_btnDesktop, (CWnd*)&m_lblWidth, (CWnd*)&m_btnRefresh, (CWnd*)&m_btnBrowser, (CWnd*)&m_btnPdf, (CWnd*)&m_lblStatus })
		w->SetFont(&m_font);
	SetWidth(m_width, false);
	SetStatus(_T("WebView2 초기화 중..."));
	SetTimer(kTimerId, kTickMs, nullptr);
	PostMessage(WM_INIT_WEBVIEW);
	return 0;
}

void CHtmlPreviewFrame::OnDestroy()
{
	KillTimer(kTimerId);
	*m_alive = false;
	if (m_controller) { m_controller->Close(); m_controller->Release(); m_controller = nullptr; }
	if (m_webview) { m_webview->Release(); m_webview = nullptr; }
	CFrameWnd::OnDestroy();
}

BOOL CHtmlPreviewFrame::OnEraseBkgnd(CDC* dc)
{
	CRect rc; GetClientRect(&rc);
	dc->FillSolidRect(rc, RGB(120, 124, 132));   // 뷰포트 폭보다 창이 넓을 때 양옆 여백
	return TRUE;
}

void CHtmlPreviewFrame::OnSize(UINT type, int cx, int cy)
{
	CFrameWnd::OnSize(type, cx, cy);
	Layout();
}

void CHtmlPreviewFrame::Layout()
{
	if (!m_btnMobile.GetSafeHwnd()) return;
	CRect rc; GetClientRect(&rc);
	const double dpi = ::GetDpiForWindow(m_hWnd) / 96.0;
	auto S = [&](int v) { return static_cast<int>(v * dpi + 0.5); };
	const int barH = S(44), pad = S(8), h = S(28), y = (barH - h) / 2;
	int x = pad;
	auto put = [&](CWnd& w, int width) { w.MoveWindow(x, y, width, h); x += width + S(6); };
	put(m_btnMobile, S(92)); put(m_btnTablet, S(92)); put(m_btnDesktop, S(110));
	put(m_slider, S(200)); put(m_lblWidth, S(64));
	put(m_btnRefresh, S(80)); put(m_btnBrowser, S(110)); put(m_btnPdf, S(80));
	m_lblStatus.MoveWindow(x, y, (std::max)(S(40), rc.Width() - x - pad), h);

	if (m_controller) {
		const int availW = rc.Width(), availH = (std::max)(0, rc.Height() - barH);
		const int w = (std::min)(static_cast<int>(m_width * dpi), availW);   // CSS px = 물리 px / dpi
		RECT b{ (availW - w) / 2, barH, (availW - w) / 2 + w, barH + availH };
		m_controller->put_Bounds(b);
	}
	Invalidate(FALSE);
}

void CHtmlPreviewFrame::SetWidth(int w, bool updateSlider)
{
	m_width = (std::max)(320, (std::min)(1920, w));
	if (updateSlider && m_slider.GetSafeHwnd()) m_slider.SetPos(m_width);
	CString s; s.Format(_T("%d px"), m_width);
	m_lblWidth.SetWindowText(s);
	Layout();
}

void CHtmlPreviewFrame::SetStatus(const CString& s) { if (m_lblStatus.GetSafeHwnd()) m_lblStatus.SetWindowText(s); }

void CHtmlPreviewFrame::OnHScroll(UINT code, UINT pos, CScrollBar* bar)
{
	if (reinterpret_cast<CWnd*>(bar) == &m_slider || (bar == nullptr && false)) SetWidth(m_slider.GetPos(), false);
	else CFrameWnd::OnHScroll(code, pos, bar);
}

void CHtmlPreviewFrame::OnPreset(UINT id)
{
	SetWidth(id == ID_BTN_MOBILE ? 375 : id == ID_BTN_TABLET ? 768 : 1280, true);
}

LRESULT CHtmlPreviewFrame::OnInitWebView(WPARAM, LPARAM) { InitWebView(); return 0; }

void CHtmlPreviewFrame::UseBrowserFallback(const CString& why)
{
	m_fallback = true;
	m_ready = true;
	m_dirty = true;
	m_slider.EnableWindow(FALSE);
	for (CWnd* w : { (CWnd*)&m_btnMobile, (CWnd*)&m_btnTablet, (CWnd*)&m_btnDesktop }) w->EnableWindow(FALSE);
	m_btnPdf.EnableWindow(FALSE);
	SetStatus(why + _T(" - 시스템 브라우저로 표시 (2초마다 자동 새로고침)"));
}

void CHtmlPreviewFrame::InitWebView()
{
	PWSTR ver = nullptr;
	const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver);
	const bool present = SUCCEEDED(hr) && ver != nullptr;
	if (ver) ::CoTaskMemFree(ver);
	if (!present) { UseBrowserFallback(_T("WebView2 런타임 없음")); return; }

	wchar_t tmp[MAX_PATH] = {};
	::GetTempPathW(MAX_PATH, tmp);
	const std::wstring dataDir = std::wstring(tmp) + L"M2Draw_WebView2";
	std::shared_ptr<bool> alive = m_alive;

	const HRESULT hr2 = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[this, alive](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
				if (!*alive) return S_OK;
				if (FAILED(r) || !env) { UseBrowserFallback(_T("WebView2 환경 생성 실패")); return S_OK; }
				env->CreateCoreWebView2Controller(m_hWnd,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[this, alive](HRESULT r2, ICoreWebView2Controller* ctl) -> HRESULT {
							if (!*alive) return S_OK;
							if (FAILED(r2) || !ctl) { UseBrowserFallback(_T("WebView2 컨트롤 생성 실패")); return S_OK; }
							m_controller = ctl; m_controller->AddRef();
							m_controller->get_CoreWebView2(&m_webview);
							if (m_webview) {
								ComPtr<ICoreWebView2Settings> st;
								if (SUCCEEDED(m_webview->get_Settings(&st)) && st) {
									st->put_AreDevToolsEnabled(TRUE);
									st->put_AreDefaultContextMenusEnabled(TRUE);
								}
							}
							m_ready = true;
							m_dirty = true;
							SetStatus(_T("미리보기 준비됨 - 편집을 멈추면 자동으로 갱신됩니다"));
							Layout();
							return S_OK;
						}).Get());
				return S_OK;
			}).Get());
	if (FAILED(hr2)) UseBrowserFallback(_T("WebView2 초기화 실패"));
}

CM2DrawDoc* CHtmlPreviewFrame::ActiveDoc() const
{
	auto* mf = dynamic_cast<CMDIFrameWnd*>(AfxGetMainWnd());
	CFrameWnd* af = mf ? mf->GetActiveFrame() : nullptr;
	return af ? dynamic_cast<CM2DrawDoc*>(af->GetActiveDocument()) : nullptr;
}

void CHtmlPreviewFrame::OnTimer(UINT_PTR id)
{
	if (id != kTimerId) { CFrameWnd::OnTimer(id); return; }
	if (!m_ready) return;
	CM2DrawDoc* doc = ActiveDoc();
	if (!doc) return;
	const ge::FrameNode* ab = doc->GetSelectedTopArtboard();
	if (doc != m_doc || ab != m_artboard) { m_doc = doc; m_artboard = ab; m_dirty = true; }
	const unsigned long long v = ge::ChangeTracker::Value();
	if (v != m_seenVersion) { m_seenVersion = v; m_dirty = true; return; }   // 아직 편집 중: 한 틱 안정되면 갱신
	if (m_dirty) Refresh();
}

std::wstring CHtmlPreviewFrame::TempHtmlPath()
{
	wchar_t tmp[MAX_PATH] = {};
	::GetTempPathW(MAX_PATH, tmp);
	return std::wstring(tmp) + L"M2Draw_preview.html";
}

bool CHtmlPreviewFrame::WriteTempHtml(const std::string& html, bool autoReload)
{
	std::string out = html;
	if (autoReload) {
		const size_t p = out.find("<head>");
		if (p != std::string::npos) out.insert(p + 6, "<meta http-equiv=\"refresh\" content=\"2\">");
	}
	std::ofstream f(TempHtmlPath(), std::ios::binary);
	if (!f) return false;
	f.write(out.data(), static_cast<std::streamsize>(out.size()));
	return f.good();
}

void CHtmlPreviewFrame::Refresh()
{
	CM2DrawDoc* doc = ActiveDoc();
	if (!doc || !m_ready) return;
	m_doc = doc;
	m_artboard = doc->GetSelectedTopArtboard();
	const std::string html = doc->BuildPreviewHtml();
	m_seenVersion = ge::ChangeTracker::Value();   // 내보내기 중 생긴 카운터 변화는 무시 (무한 갱신 방지)
	m_dirty = false;
	if (html == m_lastHtml && !m_fallback) return;
	m_lastHtml = html;

	if (m_fallback) {
		if (!WriteTempHtml(html, true)) { SetStatus(_T("임시 HTML 파일을 쓰지 못했습니다")); return; }
		if (!m_browserOpened) {
			m_browserOpened = true;
			::ShellExecuteW(m_hWnd, L"open", TempHtmlPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		return;
	}
	if (!m_webview) return;
	if (html.size() < kMaxNavigateToString) {
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, html.c_str(), static_cast<int>(html.size()), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, html.c_str(), static_cast<int>(html.size()), &w[0], n);
		m_webview->NavigateToString(w.c_str());
	} else if (WriteTempHtml(html, false)) {
		const std::wstring url = L"file:///" + TempHtmlPath();
		m_webview->Navigate(url.c_str());
	}
	CString s; s.Format(_T("갱신됨 (%d KB)"), static_cast<int>(html.size() / 1024));
	SetStatus(s);
}

void CHtmlPreviewFrame::OnRefresh() { m_lastHtml.clear(); m_dirty = true; Refresh(); }

void CHtmlPreviewFrame::OnOpenBrowser()
{
	CM2DrawDoc* doc = ActiveDoc();
	if (!doc) return;
	if (!WriteTempHtml(doc->BuildPreviewHtml(), false)) { SetStatus(_T("임시 HTML 파일을 쓰지 못했습니다")); return; }
	::ShellExecuteW(m_hWnd, L"open", TempHtmlPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void CHtmlPreviewFrame::OnSavePdf()
{
	if (!m_webview) { SetStatus(_T("WebView2 가 준비되지 않아 PDF 로 저장할 수 없습니다")); return; }
	ComPtr<ICoreWebView2_7> wv7;
	if (FAILED(m_webview->QueryInterface(IID_PPV_ARGS(&wv7))) || !wv7) { SetStatus(_T("이 WebView2 런타임은 PDF 저장을 지원하지 않습니다")); return; }
	CFileDialog dlg(FALSE, _T("pdf"), _T("preview.pdf"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("PDF 파일 (*.pdf)|*.pdf||"), this);
	if (dlg.DoModal() != IDOK) return;
	std::shared_ptr<bool> alive = m_alive;
	const HRESULT hr = wv7->PrintToPdf(dlg.GetPathName(), nullptr,
		Callback<ICoreWebView2PrintToPdfCompletedHandler>([this, alive](HRESULT r, BOOL ok) -> HRESULT {
			if (!*alive) return S_OK;
			SetStatus(SUCCEEDED(r) && ok ? _T("PDF 저장 완료") : _T("PDF 저장 실패"));
			return S_OK;
		}).Get());
	if (FAILED(hr)) SetStatus(_T("PDF 저장을 시작하지 못했습니다"));
}
