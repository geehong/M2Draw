#pragma once

#include "D2DGraphicEngine.h"
#include <memory>
#include <string>

namespace ge {

// 레퍼런스(트레이싱) 이미지 입력 (Step 7.1.1 / 7.1.x 붙여넣기)
// 파일 열기, 드래그 앤 드롭, 클립보드 붙여넣기는 모두 이 클래스의 AddReferenceImage 한 곳으로 들어온다.
// 레퍼런스 레이어의 이미지는 잠금 + 반투명으로 배치된다. 내보내기 제외 필터는 아직 없다 (7.1.1 잔여).
// Direct2D/MFC 에 의존하지 않는다. 파일 읽기와 픽셀 크기 계산은 호출자(CM2DrawDoc)가 담당한다.

// 레퍼런스 레이어 이름 (사용자가 이름을 바꾸면 인식되지 않으므로 레이어 속성 플래그 도입 전까지의 임시 식별자)
inline constexpr const wchar_t* kReferenceLayerName = L"레퍼런스";
inline constexpr float kReferenceOpacity = 0.5f;

// 레퍼런스 레이어를 찾는다. 없으면 nullptr
LayerNode* FindReferenceLayer(Document& doc);

// 레퍼런스 레이어를 찾거나 최상단에 새로 만든다
LayerNode* EnsureReferenceLayer(Document& doc);

// 이미지를 레퍼런스 레이어에 놓는다.
//  - 원본 픽셀 크기를 문서 크기의 maxRatio 이내로 맞추고 문서 중앙에 배치 (pixelW/H 가 0이면 100x100 으로 둔다)
//  - 잠금 + 반투명 (kReferenceOpacity)
// 실패하면 nullptr
// fitWidth: 문서 폭에만 맞추고 위쪽에 붙인다 (긴 웹 페이지 캡처용). 높이는 문서보다 길어도 된다.
ImageNode* AddReferenceImage(Document& doc, std::shared_ptr<const ImageData> data, const std::wstring& name, float maxRatio = 1.0f, bool fitWidth = false);

// 문서에 레퍼런스 이미지가 있는지 (레퍼런스 레이어에 자식이 하나라도 있으면 true)
inline bool HasReferenceImage(const Document& doc)
{
	for (const auto& layer : doc.GetLayers()) {
		if (layer->GetName() == kReferenceLayerName && !layer->GetChildren().empty())
			return true;
	}
	return false;
}

// 노드가 레퍼런스 레이어 안에 있는지 (조상 체인을 따라 레이어까지 확인). 내보내기 제외 판정에 쓴다
inline bool IsReferenceNode(const Node& node)
{
	const Node* cur = &node;
	while (cur) {
		if (cur->Kind() == NodeKind::Layer)
			return cur->GetName() == kReferenceLayerName;
		cur = cur->GetParent();
	}
	return false;
}

// 레퍼런스 이미지에서 색 추출 (7.1.2): 월드 점 아래의 가장 위 레퍼런스 이미지 픽셀 색 (알파 무시).
// 맞춤 방식은 늘이기(Stretch)로 계산한다 (Fit/Fill 은 근사).
bool SampleReferenceColor(Document& doc, Vec2 worldPt, Color& out);

// 이미지 바이트의 (x,y) 픽셀 색 (WIC 디코딩). 실패하면 false
bool DecodeImagePixel(const ImageData& data, unsigned x, unsigned y, Color& out);

} // namespace ge
