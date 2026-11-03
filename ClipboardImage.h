#pragma once

#include "D2DGraphicEngine.h"
#include <memory>

// 클립보드 비트맵 읽기 (Step 7.1 붙여넣기): 스크린샷 등 CF_BITMAP 을 PNG 바이트로 바꿔 ge::ImageData 로 돌려준다.
// 실제 클립보드 열기/닫기는 이 함수가 담당한다 (ClipboardSvg 의 텍스트 읽기와 같은 방식).
// 비트맵이 없거나 변환에 실패하면 nullptr.
std::shared_ptr<ge::ImageData> ReadClipboardBitmapAsPng(CWnd* owner);

// 클립보드의 유니코드 텍스트 (없으면 빈 문자열). URL 가져오기에 쓴다 (7.1.6)
std::wstring ReadClipboardText(CWnd* owner);
