#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ge {

// URL 캡처 결과 (7.1.6 / 7.1.7)
struct WebCaptureResult {
	std::vector<uint8_t> png;   // 전체 페이지 PNG (폭 = 요청한 폭, 높이 = 문서 전체 높이)
	unsigned pixelW = 0;
	unsigned pixelH = 0;
	std::string domJson;        // 주요 블록의 위치·크기·계산 스타일 JSON (에이전트가 해석한다. 프레임은 앱이 만들지 않음)
	std::string probeJson;      // 진단용: 사진·입력칸·버튼의 투명도·표시 상태 (캡처 직전 측정)
};

// URL 을 WebView2 로 열어 폭 width 의 전체 페이지를 PNG 로 찍고, DOM 블록 정보를 JSON 으로 뽑는다.
// 창은 화면에 보이지 않는 숨김 창을 쓴다. 로그인이 필요한 페이지는 로그인 화면이 찍힌다.
// 실패하면 false 와 사유를 err 에 넣는다.
bool CaptureWebPage(const std::wstring& url, unsigned width, unsigned timeoutMs, WebCaptureResult& out, std::wstring* err);

} // namespace ge
