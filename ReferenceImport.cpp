#include "pch.h"
#include "ReferenceImport.h"
#include "D2DLayer.h"

#include <algorithm>

namespace ge {

LayerNode* FindReferenceLayer(Document& doc)
{
	for (const auto& layer : doc.GetLayers()) {
		if (layer->GetName() == kReferenceLayerName)
			return layer.get();
	}
	return nullptr;
}

LayerNode* EnsureReferenceLayer(Document& doc)
{
	if (LayerNode* existing = FindReferenceLayer(doc))
		return existing;
	return doc.AddLayer(kReferenceLayerName, -1);   // -1 = 최상단
}

ImageNode* AddReferenceImage(Document& doc, std::shared_ptr<const ImageData> data, const std::wstring& name, float maxRatio)
{
	if (!data) return nullptr;
	LayerNode* layer = EnsureReferenceLayer(doc);
	if (!layer) return nullptr;

	// 원본 픽셀 크기를 알 수 없으면 기본 상자로 둔다
	float w = data->pixelW > 0 ? static_cast<float>(data->pixelW) : 100.0f;
	float h = data->pixelH > 0 ? static_cast<float>(data->pixelH) : 100.0f;
	const float ratio = (std::max)(0.01f, maxRatio);
	const float sc = (std::min)(1.0f, (std::min)(doc.width * ratio / w, doc.height * ratio / h));
	w *= sc;
	h *= sc;

	auto im = std::make_unique<ImageNode>();
	im->SetData(std::move(data));
	im->SetSize(w, h);
	im->SetName(name);
	im->SetTransform(Matrix2D::Translation((doc.width - w) * 0.5f, (doc.height - h) * 0.5f));
	im->SetLocked(true);                       // 실수로 옮기거나 지우지 않도록 잠금
	im->SetOpacity(kReferenceOpacity);         // 아래 도형이 비쳐 보이도록 반투명

	// AddShape 는 활성 레이어에 넣으므로, 레퍼런스 레이어 최상단으로 옮긴다 (화면상 위치는 MoveNodeTo 가 보정)
	Node* added = doc.AddShape(std::move(im));
	if (!added) return nullptr;
	doc.MoveNodeTo(added, layer, -1);
	return static_cast<ImageNode*>(added);
}

bool IsReferenceNode(const Node& node)
{
	const Node* cur = &node;
	while (cur) {
		if (cur->Kind() == NodeKind::Layer)
			return cur->GetName() == kReferenceLayerName;
		cur = cur->GetParent();
	}
	return false;
}

} // namespace ge
