#pragma once

#include "D2DGraphicEngine.h"
#include <string>
#include <vector>

namespace ge {

// 주석(메모) 레이어 (Step 7.3.3): 레이어 이름으로 식별한다. 순수 SVG/HTML/PNG 내보내기에서는 빠지고,
// 파일 저장과 AI 에이전트 패키지(scene.json 의 annotations, prompt.md)에는 들어간다.
inline constexpr const wchar_t* kAnnotationLayerName = L"주석";

// 노드가 주석 레이어 안에 있는지 (조상 체인을 따라 레이어까지 확인)
inline bool IsAnnotationNode(const Node& node)
{
	const Node* cur = &node;
	while (cur) {
		if (cur->Kind() == NodeKind::Layer)
			return cur->GetName() == kAnnotationLayerName;
		cur = cur->GetParent();
	}
	return false;
}

// 주석 레이어를 찾거나 최상단에 새로 만든다
LayerNode* EnsureAnnotationLayer(Document& doc);

// 주석(메모 텍스트)을 주석 레이어에 놓는다. at 은 문서 좌표의 왼쪽 위
TextNode* AddAnnotation(Document& doc, const std::wstring& text, Vec2 at);

// 주석 텍스트 목록 (주석 레이어의 글자 노드)
std::vector<std::wstring> AnnotationTexts(const Document& doc);

} // namespace ge
