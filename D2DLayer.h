#pragma once

namespace ge {

// ---- 잉크스케이프 스타일 레이어 노드 (씬 루트 컨테이너) ----------------------
// 가시성/잠금/불투명도/혼합 모드는 Node 공통 필드를 그대로 사용 (레이어 전용 필드 없음)
class LayerNode : public GroupNode {
public:
	LayerNode();
	explicit LayerNode(const std::wstring& name);
	virtual ~LayerNode() = default;

	NodeKind Kind() const override { return NodeKind::Layer; }
	std::unique_ptr<Node> Clone() const override;
	void Accept(INodeVisitor& visitor) override;
};

} // namespace ge
