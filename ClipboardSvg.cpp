#include "pch.h"
#include "ClipboardSvg.h"
#include "SvgWriter.h"
#include "SvgReader.h"
#include <algorithm>

namespace ge {

std::string SerializeNodesToSvg(const Document& source, const std::vector<Node*>& nodes)
{
	Document tmp;
	tmp.width = source.width;
	tmp.height = source.height;
	LayerNode* layer = tmp.GetActiveLayer();
	if (!layer) return std::string();

	int count = 0;
	for (Node* n : nodes) {
		if (!n || n->Kind() == NodeKind::Layer) continue;
		// 조상이 함께 선택되어 있으면 건너뜀 (중복 복사 방지)
		bool ancestorSelected = false;
		for (GroupNode* p = n->GetParent(); p; p = p->GetParent())
			if (std::find(nodes.begin(), nodes.end(), static_cast<Node*>(p)) != nodes.end()) { ancestorSelected = true; break; }
		if (ancestorSelected) continue;

		std::unique_ptr<Node> copy = n->Clone();
		copy->SetTransform(n->GetWorldTransform());   // 부모 변환을 구워 레이어 직속으로
		Node* added = layer->AddChild(std::move(copy));
		if (added) { tmp.AssignIds(*added); ++count; }
	}
	if (count == 0) return std::string();

	SvgWriter writer;
	return writer.WriteString(tmp, SvgWriteOptions(SvgWriteMode::File));
}

bool LooksLikeSvg(const std::string& text)
{
	// 앞쪽 공백/BOM/XML 선언을 건너뛰고 <svg 가 나오는지
	const size_t limit = (std::min)(text.size(), static_cast<size_t>(4096));
	return text.substr(0, limit).find("<svg") != std::string::npos;
}

bool ParseSvgNodes(const std::string& svg, Document& tmp, std::vector<const Node*>& out)
{
	out.clear();
	if (!LooksLikeSvg(svg)) return false;
	SvgReader reader;
	if (!reader.ReadFromString(svg, tmp)) return false;
	for (const auto& layer : tmp.GetLayers()) {
		if (!layer) continue;
		for (const auto& c : layer->GetChildren())
			if (c) out.push_back(c.get());
	}
	return !out.empty();
}

} // namespace ge
