#pragma once

#include "D2DGraphicEngine.h"
#include <string>

namespace ge {

// 로고 색 변형 내보내기 (Step 7.4.3)
// 문서 전체를 원본 / 검정 단색 / 흰색 단색 세 가지로 저장한다.
// 각 변형마다 SVG(순수 SVG), PNG(투명 배경, 2x), PNG(흰 배경), PNG(검정 배경)을 쓴다.
// 단색 변형은 도형·글자·프레임의 채우기와 선 색을 바꾸고, 레퍼런스와 이미지 노드는 바꾸지 않는다.
// 저장이 끝나면 원래 스타일과 배경색으로 되돌린다.
bool ExportLogoVariants(Document& doc, const std::wstring& folder, std::wstring* err);

} // namespace ge
