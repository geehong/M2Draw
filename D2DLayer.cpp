#include "pch.h"
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"
#include "GeNodeVisitor.h"

namespace ge {

void LayerNode::Accept(INodeVisitor& visitor)
{
	visitor.Visit(*this);
}

LayerNode::LayerNode()
{
	m_name = L"Layer 1";
}

LayerNode::LayerNode(const std::wstring& name)
{
	m_name = name;
}

std::unique_ptr<Node> LayerNode::Clone() const
{
	auto copy = std::make_unique<LayerNode>(m_name);
	copy->CopyNodePropsFrom(*this);
	copy->m_bClippingMask = m_bClippingMask;

	for (const auto& child : m_children) {
		if (child) {
			copy->AddChild(child->Clone());
		}
	}
	return copy;
}

} // namespace ge
