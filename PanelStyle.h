#pragma once
// 패널 공통 색 (8.9). 채팅·레이어·속성·프로젝트 트리 패널이 같은 값을 쓴다. 색을 바꾸려면 이 파일만 고친다
#include <windows.h>

namespace PanelStyle {

// 작업 상태 버튼: 실행 중 (파란색 깜빡임), 완료 (빨간색 잠깐)
inline constexpr COLORREF kRunFillOn = RGB(30, 110, 255);
inline constexpr COLORREF kRunFillOff = RGB(160, 200, 255);
inline constexpr COLORREF kRunTextOn = RGB(255, 255, 255);
inline constexpr COLORREF kRunTextOff = RGB(20, 50, 110);
inline constexpr COLORREF kDoneFill = RGB(200, 40, 40);
inline constexpr COLORREF kDoneText = RGB(255, 255, 255);

// 패널 바탕과 글자 (레이어·그리드·캔버스 패널과 같은 다크 테마, 레이어 패널 배경 #1e1e22)
inline constexpr COLORREF kPanelBg = RGB(0x1e, 0x1e, 0x22);
inline constexpr COLORREF kPanelText = RGB(0xdc, 0xdc, 0xe0);
inline constexpr COLORREF kPanelSelBg = RGB(0x2a, 0x5f, 0xd6);
inline constexpr COLORREF kPanelSelText = RGB(0xff, 0xff, 0xff);

} // namespace PanelStyle
