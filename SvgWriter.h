#pragma once

#include "GeNodeVisitor.h"
#include <string>

namespace ge {

class Document;
class PathNode;
class GroupNode;
class LayerNode;

// M2Draw 확장 SVG 작성기 (Visitor 패턴 구현체)
// 표준 SVG 규격과 호환되면서, M2Draw 및 Inkscape 전용 메타데이터(파라메트릭 도형, 레이어, 노드 타입 등)를 보존
class SvgWriter : public INodeVisitor {
public:
	SvgWriter();
	virtual ~SvgWriter() = default;

	// 문서를 SVG 포맷 문자열(UTF-8)로 직렬화
	std::string WriteString(Document& doc);

	// 문서를 지정된 파일 경로에 SVG 파일로 저장 (UTF-8 인코딩)
	bool WriteToFile(Document& doc, const std::wstring& filePath);

	// INodeVisitor 인터페이스 구현
	void Visit(PathNode& node) override;
	void Visit(GroupNode& node) override;
	void Visit(LayerNode& node) override;

private:
	void AppendIndent();

	std::string m_output;
	int         m_indent = 0;
};

} // namespace ge
