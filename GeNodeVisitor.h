#pragma once

namespace ge {

class PathNode;
class GroupNode;
class LayerNode;

// 씬 그래프 노드 방문자 인터페이스 (Visitor Pattern)
// SvgWriter, SvgReader, HtmlExporter 등이 공통으로 사용
class INodeVisitor {
public:
	virtual ~INodeVisitor() = default;

	virtual void Visit(PathNode& node) = 0;
	virtual void Visit(GroupNode& node) = 0;
	virtual void Visit(LayerNode& node) = 0;
};

} // namespace ge
