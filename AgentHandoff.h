#pragma once

#include "D2DGraphicEngine.h"
#include <string>

namespace ge {

// AI 에이전트 전달 패키지 (Step 7.3.1)
// 한 폴더에 에이전트가 읽을 파일 3개를 만든다.
//   scene.json  : 구조 데이터 (SceneJsonWriter, 7.2.2)
//   preview.png : 대상 영역의 그림 (RasterExport, 5.5)
//   prompt.md   : 지시문 초안 (사용자가 편집 가능, 7.3.2)
// 파일 목록과 지시문 형식은 에이전트가 달라도 같으므로, 어떤 에이전트든 같은 폴더를 읽으면 된다.

struct AgentHandoffOptions {
	Node* target = nullptr;   // 보통 프레임. nullptr = 문서 전체
};

// 지시문 초안을 만든다 (대상 이름, 크기, 지침 포함). 파일로 쓸 때는 UTF-8 로 변환한다
std::wstring BuildAgentPrompt(const Document& doc, const Node* target);

// 패키지를 folder 에 쓴다 (폴더가 없으면 만든다). 실패하면 false 와 사유를 err 에 넣는다
bool WriteAgentPackage(Document& doc, const std::wstring& folder, const AgentHandoffOptions& opt, std::wstring* err);

} // namespace ge
