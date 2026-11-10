#include "pch.h"
#include "WebCapture.h"

#include <wrl.h>
#include <WebView2.h>
#include <functional>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace ge {

namespace {

// 화면 밖 숨김 창 + WebView2 로 URL 을 열고 PNG 와 DOM 정보를 얻는 한 번짜리 작업
class WebCaptureJob {
public:
	bool Run(const std::wstring& url, unsigned width, unsigned timeoutMs, WebCaptureResult& out, std::wstring* err)
	{
		m_width = width;
		m_timeoutMs = timeoutMs;
		auto fail = [&](const std::wstring& why) { if (err) *err = why; Cleanup(); return false; };

		m_hwnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, static_cast<int>(width), 800,
			nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
		if (!m_hwnd) return fail(L"캡처용 창을 만들 수 없습니다");
		::SetLayeredWindowAttributes(m_hwnd, 0, 1, LWA_ALPHA);
		::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

		wchar_t tmp[MAX_PATH] = {};
		::GetTempPathW(MAX_PATH, tmp);
		const std::wstring dataDir = std::wstring(tmp) + L"M2Draw_WebCapture";

		// 1. 환경
		bool envDone = false;
		HRESULT envHr = E_FAIL;
		HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), nullptr,
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[&](HRESULT r, ICoreWebView2Environment* e) -> HRESULT {
					envHr = r;
					m_env = e;
					envDone = true;
					return S_OK;
				}).Get());
		if (FAILED(hr)) return fail(L"WebView2 런타임을 찾을 수 없습니다 (Evergreen 런타임 설치 필요)");
		if (!Pump([&] { return envDone; }) || FAILED(envHr) || !m_env) return fail(L"WebView2 환경 생성 시간 초과 또는 실패");

		// 2. 컨트롤러
		bool ctlDone = false;
		HRESULT ctlHr = E_FAIL;
		hr = m_env->CreateCoreWebView2Controller(m_hwnd,
			Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
				[&](HRESULT r, ICoreWebView2Controller* c) -> HRESULT {
					ctlHr = r;
					m_ctl = c;
					ctlDone = true;
					return S_OK;
				}).Get());
		if (FAILED(hr)) return fail(L"WebView2 컨트롤러 생성 실패");
		if (!Pump([&] { return ctlDone; }) || FAILED(ctlHr) || !m_ctl) return fail(L"WebView2 컨트롤러 생성 시간 초과 또는 실패");
		m_ctl->get_CoreWebView2(&m_web);
		if (!m_web) return fail(L"WebView2 핵심 객체를 얻을 수 없습니다");
		SetBounds(800);

		// 3. 이동 (완료 대기)
		bool navDone = false, navOk = false;
		EventRegistrationToken navToken{};
		m_web->add_NavigationCompleted(
			Callback<ICoreWebView2NavigationCompletedEventHandler>(
				[&](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
					BOOL ok = FALSE;
					if (args) args->get_IsSuccess(&ok);
					navOk = ok != FALSE;
					navDone = true;
					return S_OK;
				}).Get(), &navToken);
		m_web->Navigate(url.c_str());
		if (!Pump([&] { return navDone; }) || !navOk) return fail(L"페이지를 열지 못했습니다 (주소·네트워크 확인)");
		// 고정 시간 대신: 로딩과 애니메이션이 끝날 때까지 기다린다 (상한 8초)
		const ULONGLONG settleStartMs = ::GetTickCount64();
		while (::GetTickCount64() - settleStartMs < 8000) {
			std::wstring readyJson;
			RunScript(L"(window.scrollY === 0 && document.readyState === 'complete' && Array.from(document.images).every(function (i) { return i.complete; }) && !(function () { var el = document.elementFromPoint(innerWidth / 2, innerHeight / 2); while (el) { var r = el.getBoundingClientRect(); if (getComputedStyle(el).position === 'fixed' && r.width >= innerWidth * 0.8 && r.height >= innerHeight * 0.8) return true; el = el.parentElement; } return false; })() && (!document.getAnimations || document.getAnimations().every(function (a) { return a.playState !== 'running'; }))) ? 1 : 0", readyJson);
			if (readyJson.find(L'1') != std::wstring::npos) break;
			PumpFor(200);
		}
		// 지연 로딩 내용 끌어오기: 맨 아래까지 내렸다가 다시 위로
		std::wstring scrollJson;
		RunScript(L"document.documentElement.style.scrollBehavior='auto';window.scrollTo({top:document.documentElement.scrollHeight,behavior:'instant'});", scrollJson);
		PumpFor(400);
		RunScript(L"document.documentElement.style.scrollBehavior='auto';window.scrollTo({top:0,behavior:'instant'});", scrollJson);
		// 스크롤로 나타난 사진과 효과가 다 그려질 때까지 한 번 더 기다린다 (위로 올린 뒤 다시 확인)
		const ULONGLONG rescrollStartMs = ::GetTickCount64();
		while (::GetTickCount64() - rescrollStartMs < 8000) {
			PumpFor(200);
			std::wstring rescrollJson;
			RunScript(L"(window.scrollY === 0 && document.readyState === 'complete' && Array.from(document.images).every(function (i) { return i.complete; }) && !(function () { var el = document.elementFromPoint(innerWidth / 2, innerHeight / 2); while (el) { var r = el.getBoundingClientRect(); if (getComputedStyle(el).position === 'fixed' && r.width >= innerWidth * 0.8 && r.height >= innerHeight * 0.8) return true; el = el.parentElement; } return false; })() && (!document.getAnimations || document.getAnimations().every(function (a) { return a.playState !== 'running'; }))) ? 1 : 0", rescrollJson);
			if (rescrollJson.find(L'1') != std::wstring::npos) break;
		}
		// 화면에 붙는 고정 바는 캡처에서 뺀다 (페이지 중간을 가리지 않도록)
		std::wstring fixedJson;
		RunScript(L"(function(){document.querySelectorAll('*').forEach(function (el) { if (getComputedStyle(el).position === 'fixed' && el.getBoundingClientRect().height < 150) el.style.setProperty('display', 'none', 'important'); }); return 1;})()", fixedJson);

		// 4. 스크롤바 숨김 (캡처 폭에 스크롤바가 끼지 않게) 후 전체 높이
		std::wstring hideJson;
		RunScript(L"(function(){var s=document.createElement('style');s.textContent='html,body{height:auto!important;overflow:visible!important}html::-webkit-scrollbar,body::-webkit-scrollbar,*::-webkit-scrollbar{display:none!important;width:0!important}';document.head.appendChild(s);return 1;})()", hideJson);
		std::wstring expandJson;
		RunScript(kExpandScript, expandJson);
		// 펼친 뒤: 전체 높이가 3번 연속 같아지면 안정된 것으로 본다 (상한 8초)
		long stableLastH = -1;
		int stableCount = 0;
		while (::GetTickCount64() - settleStartMs < 8000) {
			std::wstring stableJson;
			RunScript(L"document.documentElement.scrollHeight", stableJson);
			const long stableH = _wtol(stableJson.c_str());
			if (stableH == stableLastH) {
				if (++stableCount >= 8) break;
			} else {
				stableLastH = stableH;
				stableCount = 0;
			}
			PumpFor(200);
		}
		std::wstring heightJson;
		if (!RunScript(L"document.documentElement.scrollHeight", heightJson)) return fail(L"페이지 높이를 읽지 못했습니다");
		const long pageH = _wtol(heightJson.c_str());
		const unsigned capH = static_cast<unsigned>((std::max)(100L, (std::min)(pageH, 3240L)));   // 기본 1920x1080 의 세 배 (3240) 까지만 찍는다. 넘는 페이지는 잘린다
		SetBounds(capH);
		// 크기를 바꾼 뒤 다시 그리기를 강제로 일으킨다 (그리지 않은 영역이 남는 문제): resize 알림 + 1px 스크롤
		RunScript(L"window.dispatchEvent(new Event('resize'));window.scrollBy(0,1);window.scrollBy(0,-1);", heightJson);
		PumpFor(300);
		// 크기를 바꾸면 화면에 들어온 것으로 보고 효과가 새로 시작된다: 다 끝날 때까지 다시 기다린다 (상한 8초)
		const ULONGLONG resizeStartMs = ::GetTickCount64();
		while (::GetTickCount64() - resizeStartMs < 8000) {
			PumpFor(200);
			std::wstring resizeJson;
			RunScript(L"(document.readyState === 'complete' && Array.from(document.images).every(function (i) { return i.complete; }) && (!document.getAnimations || document.getAnimations().every(function (a) { return a.playState !== 'running'; }))) ? 1 : 0", resizeJson);
			if (resizeJson.find(L'1') != std::wstring::npos) break;
		}

		// 5. DOM 블록 정보
		std::wstring dom;
		if (!RunScript(kDomScript, dom)) return fail(L"DOM 정보를 읽지 못했습니다");
		out.domJson = ToUtf8(dom);
		// 진단: 사진·입력칸·버튼·효과가 걸린 요소의 투명도와 표시 상태 (안 보이는 원인 확인용)
		std::wstring probe;
		RunScript(L"(function(){var out=[];var sel=document.querySelectorAll('img,input,button,textarea,select,[class*=reveal],[class*=fade],[class*=anim]');for(var i=0;i<sel.length&&out.length<120;i++){var el=sel[i];var cs=getComputedStyle(el);var r=el.getBoundingClientRect();out.push({t:el.tagName,c:String(el.className).slice(0,60),op:cs.opacity,vis:cs.visibility,disp:cs.display,w:Math.round(r.width),h:Math.round(r.height),y:Math.round(r.top+scrollY)});}return JSON.stringify(out);})()", probe);
		out.probeJson = ToUtf8(probe);

		// 6. 전체 페이지 PNG (WebView2 CapturePreview)
		ComPtr<IStream> stream;
		if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return fail(L"메모리 스트림을 만들 수 없습니다");
		bool capDone = false;
		HRESULT capHr = E_FAIL;
		hr = m_web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
			Callback<ICoreWebView2CapturePreviewCompletedHandler>(
				[&](HRESULT r) -> HRESULT { capHr = r; capDone = true; return S_OK; }).Get());
		if (FAILED(hr)) return fail(L"CapturePreview 호출 실패: " + std::to_wstring(static_cast<long>(hr)));
		if (!Pump([&] { return capDone; })) return fail(L"CapturePreview 응답 시간 초과");
		if (FAILED(capHr)) return fail(L"CapturePreview 실패: " + std::to_wstring(static_cast<long>(capHr)));

		STATSTG st{};
		if (FAILED(stream->Stat(&st, STATFLAG_NONAME)) || st.cbSize.QuadPart == 0) return fail(L"캡처 결과가 비어 있습니다");
		LARGE_INTEGER zero{};
		stream->Seek(zero, STREAM_SEEK_SET, nullptr);
		out.png.resize(static_cast<size_t>(st.cbSize.QuadPart));
		ULONG got = 0;
		if (FAILED(stream->Read(out.png.data(), static_cast<ULONG>(out.png.size()), &got)) || got != out.png.size())
			return fail(L"캡처 결과를 읽지 못했습니다");

		// PNG IHDR: 시그니처 8바이트 뒤 길이 4 + 'IHDR' 4, 그다음 폭·높이 (빅엔디언)
		if (out.png.size() < 24) return fail(L"PNG 형식이 아닙니다");
		auto be32 = [&](size_t at) { return (uint32_t(out.png[at]) << 24) | (uint32_t(out.png[at + 1]) << 16) | (uint32_t(out.png[at + 2]) << 8) | uint32_t(out.png[at + 3]); };
		out.pixelW = be32(16);
		out.pixelH = be32(20);

		Cleanup();
		return true;
	}

private:
	static constexpr const wchar_t* kDomScript = LR"JS(
(function () {
  const W = Math.max(document.documentElement.scrollWidth, innerWidth);
  const H = Math.max(document.documentElement.scrollHeight, innerHeight);
  const roleOf = (el) => {
    const t = el.tagName.toLowerCase(), r = el.getAttribute('role') || '';
    if (t === 'header' || r === 'banner') return 'header';
    if (t === 'footer' || r === 'contentinfo') return 'footer';
    if (t === 'nav' || r === 'navigation') return 'nav';
    if (t === 'aside' || r === 'complementary') return 'sidebar';
    if (t === 'main' || r === 'main') return 'main';
    return 'section';
  };
  const count = (el, sel) => el.querySelectorAll(sel).length;
  const els = document.querySelectorAll('header,nav,aside,main,footer,section,article,[role=banner],[role=navigation],[role=complementary],[role=main],[role=contentinfo]');
  const regions = [];
  for (const el of els) {
    const r = el.getBoundingClientRect();
    if (r.width < 20 || r.height < 10) continue;
    const x = r.left + scrollX, y = r.top + scrollY;
    regions.push({
      role: roleOf(el),
      xPct: Math.round(x / W * 1000) / 10, yPct: Math.round(y / H * 1000) / 10,
      wPct: Math.round(r.width / W * 1000) / 10, hPct: Math.round(r.height / H * 1000) / 10,
      hPx: Math.round(r.height),
      links: count(el, 'a'),
      buttons: count(el, 'button,[role=button],input[type=button],input[type=submit]'),
      inputs: count(el, 'input:not([type=button]):not([type=submit]),textarea,select'),
      images: count(el, 'img,picture,svg'),
      listItems: count(el, 'li')
    });
    if (regions.length >= 200) break;
  }
  return { url: location.href, title: document.title, pageWidth: W, pageHeight: H, regions: regions };
})()
)JS";

	// 안쪽 스크롤 영역(예: 티스토리 본문)을 내용 높이만큼 펼쳐서 전체가 찍히게 한다
	static constexpr const wchar_t* kExpandScript = LR"JS(
(function () {
  let n = 0;
  for (const el of document.querySelectorAll('*')) {
    const cs = getComputedStyle(el);
    if (!/(auto|scroll)/.test(cs.overflowY)) continue;
    if (el.scrollHeight <= el.clientHeight + 40) continue;
    if (el.getBoundingClientRect().height < 200) continue;
    el.style.setProperty('height', el.scrollHeight + 'px', 'important');
    el.style.setProperty('max-height', 'none', 'important');
    el.style.setProperty('overflow', 'visible', 'important');
    if (++n >= 30) break;
  }
  return n;
})()
)JS";
	bool Pump(const std::function<bool()>& done)
	{
		const DWORD t0 = ::GetTickCount();
		while (!done()) {
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			if (::GetTickCount() - t0 > m_timeoutMs) return false;
			::Sleep(5);
		}
		return true;
	}

	void PumpFor(DWORD ms)
	{
		const DWORD t0 = ::GetTickCount();
		while (::GetTickCount() - t0 < ms) {
			MSG msg;
			while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			}
			::Sleep(5);
		}
	}

	bool RunScript(const wchar_t* script, std::wstring& resultJson)
	{
		bool done = false;
		HRESULT hr = m_web->ExecuteScript(script,
			Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
				[&](HRESULT r, LPCWSTR json) -> HRESULT {
					if (SUCCEEDED(r) && json) resultJson = json;
					done = true;
					return S_OK;
				}).Get());
		return SUCCEEDED(hr) && Pump([&] { return done; }) && !resultJson.empty();
	}

	void SetBounds(unsigned h)
	{
		if (m_ctl) {
			RECT rc{ 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(h) };
			m_ctl->put_Bounds(rc);
		}
		if (m_hwnd) ::SetWindowPos(m_hwnd, nullptr, 0, 0, static_cast<int>(m_width), static_cast<int>(h), SWP_NOZORDER | SWP_NOACTIVATE);
	}

	static std::string ToUtf8(const std::wstring& w)
	{
		if (w.empty()) return {};
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<size_t>(n), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
		return out;
	}

	void Cleanup()
	{
		if (m_ctl) { m_ctl->Close(); m_ctl.Reset(); }
		m_web.Reset();
		m_env.Reset();
		if (m_hwnd) { ::DestroyWindow(m_hwnd); m_hwnd = nullptr; }
	}

	HWND m_hwnd = nullptr;
	unsigned m_width = 1440;
	unsigned m_timeoutMs = 30000;
	ComPtr<ICoreWebView2Environment> m_env;
	ComPtr<ICoreWebView2Controller> m_ctl;
	ComPtr<ICoreWebView2> m_web;
};

} // namespace

bool CaptureWebPage(const std::wstring& url, unsigned width, unsigned timeoutMs, WebCaptureResult& out, std::wstring* err)
{
	WebCaptureJob job;
	return job.Run(url, width, timeoutMs, out, err);
}

} // namespace ge
