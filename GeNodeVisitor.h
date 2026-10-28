#pragma once

namespace ge {

class PathNode;
class GroupNode;
class LayerNode;
class TextNode;
class ImageNode;

// 씬 그래프 노드 방문자 인터페이스 (Visitor Pattern)
// SvgWriter, SvgReader, HtmlExporter 등이 공통으로 사용
class INodeVisitor {
public:
	virtual ~INodeVisitor() = default;

	virtual void Visit(PathNode& node) = 0;
	virtual void Visit(GroupNode& node) = 0;
	virtual void Visit(LayerNode& node) = 0;
	virtual void Visit(TextNode&) {}   // 텍스트를 지원하지 않는 방문자는 무시
	virtual void Visit(ImageNode&) {}  // 이미지를 지원하지 않는 방문자는 무시
};

} // namespace ge
