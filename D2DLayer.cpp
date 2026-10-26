#include "pch.h"
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"

namespace ge {

LayerNode::LayerNode()
	: m_bVisible(true)
	, m_bLocked(false)
	, m_opacity(1.0f)
	, m_blendMode(LayerBlendMode::Normal)
	, m_bClippingMask(false)
{
	m_name = L"Layer 1";
}

LayerNode::LayerNode(const std::wstring& name)
	: m_bVisible(true)
	, m_bLocked(false)
	, m_opacity(1.0f)
	, m_blendMode(LayerBlendMode::Normal)
	, m_bClippingMask(false)
{
	m_name = name;
}

std::unique_ptr<Node> LayerNode::Clone() const
{
	auto copy = std::make_unique<LayerNode>(m_name);
	copy->CopyNodePropsFrom(*this);
	copy->m_bVisible      = m_bVisible;
	copy->m_bLocked       = m_bLocked;
	copy->m_opacity       = m_opacity;
	copy->m_blendMode     = m_blendMode;
	copy->m_bClippingMask = m_bClippingMask;

	for (const auto& child : m_children) {
		if (child) {
			copy->AddChild(child->Clone());
		}
	}
	return copy;
}

D2D1_LAYER_PARAMETERS LayerNode::GetD2DLayerParameters() const
{
	D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
	params.opacity = m_opacity;
	params.contentBounds = D2D1::InfiniteRect();
	return params;
}

} // namespace ge
