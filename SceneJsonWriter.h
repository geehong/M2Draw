#pragma once

#include "D2DGraphicEngine.h"
#include <string>

namespace ge {

// AI 에이전트용 씬 구조 JSON 출력 (Step 7.2.2)
// 레이어 > 노드 트리를 그대로 옮기며, 계층·레이아웃·역할(htmlTag)·크기·스타일·텍스트·월드 경계 같은 "사실"만 담는다.
// 의미 해석(이게 헤더인지 카드인지)은 에이전트가 맡는다. 파일 쓰기는 호출자(AgentHandoff 등)가 담당한다.

struct SceneJsonOptions {
	Node* root = nullptr;         // nullptr = 문서 전체(레이어별), 지정하면 그 노드와 자손만 출력
};

// UTF-8 JSON 문자열을 돌려준다.
// 형식: { "version": 1, "document": { width, height, background }, "layers": [ node... ] }  (root 지정 시 "root": node)
// 노드: { id, kind, name, visible, locked, opacity, bounds, role?, frame?, layout?, sizing?, text?, image?, style?, children? }
std::string SerializeSceneJson(const Document& doc, const SceneJsonOptions& options = SceneJsonOptions());

} // namespace ge
