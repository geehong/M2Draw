#pragma once

#include "D2DGraphicEngine.h"
#include <string>

namespace ge {

// 아이콘 세트 내보내기 (Step 7.4.1 / 7.4.2)
// target 영역(없으면 문서 전체)을 가운데 정사각형으로 잘라 icon-16 ~ icon-512.png 를 만들고,
// icon.ico (16/32/48/256), favicon.ico (16/32/48), favicon-snippet.html (HTML 연결 태그) 을 같은 폴더에 쓴다.
// 실패하면 false 와 사유를 err 에 넣는다.
bool ExportIconSet(Document& doc, const std::wstring& folder, Node* target, std::wstring* err);

} // namespace ge
