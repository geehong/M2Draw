#include "pch.h"
#include "AnnotationNote.h"
#include "D2DLayer.h"

namespace ge {

LayerNode* EnsureAnnotationLayer(Document& doc)
{
	for (const auto& layer : doc.GetLayers()) {
		if (layer->GetName() == kAnnotationLayerName)
			return layer.get();
	}
	return doc.AddLayer(kAnnotationLayerName, -1);   // -1 = 최상단
}

TextNode* AddAnnotation(Document& doc, const std::wstring& text, Vec2 at)
{
	LayerNode* layer = EnsureAnnotationLayer(doc);
	if (!layer) return nullptr;

	auto note = std::make_unique<TextNode>();
	note->SetText(text);
	note->SetFontSize(18.0f);
	note->SetName(L"주석");
	Style s = note->GetStyle();
	s.fill.type = PaintType::Solid;
	s.fill.color = Color(0.9f, 0.1f, 0.1f, 1.0f);   // 주석은 빨간 글씨로 구분
	note->SetStyle(s);
	note->SetTransform(Matrix2D::Translation(at.x, at.y));

	// AddShape 는 활성 레이어에 넣으므로 주석 레이어 최상단으로 옮긴다 (화면상 위치는 MoveNodeTo 가 보정)
	Node* added = doc.AddShape(std::move(note));
	if (!added) return nullptr;
	doc.MoveNodeTo(added, layer, -1);
	return static_cast<TextNode*>(added);
}

std::vector<std::wstring> AnnotationTexts(const Document& doc)
{
	std::vector<std::wstring> out;
	for (const auto& layer : doc.GetLayers()) {
		if (layer->GetName() != kAnnotationLayerName) continue;
		for (const auto& child : layer->GetChildren()) {
			if (const auto* t = dynamic_cast<const TextNode*>(child.get()))
				out.push_back(t->GetText());
		}
	}
	return out;
}

} // namespace ge