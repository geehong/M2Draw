#pragma once

#include "D2DGraphicEngine.h"
#include <string>
#include <vector>

namespace ge {

// SVG 클립보드 교환 (Step 5.4): 렌더링 API/OS 클립보드에 의존하지 않는 순수 변환 부분.
// 실제 Windows 클립보드 읽기/쓰기는 CM2DrawDoc 가 담당한다.

// 선택한 노드들을 월드 변환으로 구워 SVG 문자열(M2 메타 포함 File 형식, 이미지 임베드)로 직렬화한다.
// 조상이 함께 선택된 노드는 조상이 이미 포함하므로 제외한다. 결과가 없으면 빈 문자열
std::string SerializeNodesToSvg(const Document& source, const std::vector<Node*>& nodes);

// SVG 문자열(M2 출력/Illustrator/Inkscape/Figma/브라우저)을 임시 문서 tmp 로 읽고, 붙여넣을 노드(레이어 직속 자식)들을 돌려준다.
// 문자열이 SVG 가 아니거나 읽기에 실패하면 false
bool ParseSvgNodes(const std::string& svg, Document& tmp, std::vector<const Node*>& out);

// 클립보드 텍스트가 SVG 처럼 보이는지 (붙여넣기 가능 여부를 가볍게 판정)
bool LooksLikeSvg(const std::string& text);

} // namespace ge
