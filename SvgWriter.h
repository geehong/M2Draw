#pragma once

#include "GeNodeVisitor.h"
#include <string>
#include <unordered_map>

namespace ge {

class Document;
class PathNode;
class GroupNode;
class LayerNode;
class TextNode;
class ImageNode;

// SVG 출력 용도
//  - Snapshot: Undo/Redo 용 내부 스냅샷. 모든 M2 메타데이터 포함, 이미지는 리소스 참조 ID 만 기록 (이미지 중복 보관 방지)
//  - File    : 저장 파일/클립보드. 모든 M2 메타데이터 포함, 이미지는 base64 로 임베드
//  - Clean   : 순수 표준 SVG 내보내기. m2:* / inkscape:* / sodipodi:* 속성·네임스페이스·namedview 제거
// (이미지 노드는 Step 3.4 에서 추가되며, 그때 Snapshot 과 File/Clean 의 이미지 처리가 갈라진다)
enum class SvgWriteMode { Snapshot, File, Clean };

struct SvgWriteOptions {
	SvgWriteMode mode = SvgWriteMode::File;
	// 파라메트릭 도형(Rect/Ellipse)을 <rect rx ry>/<ellipse>/<circle> 네이티브 태그로 출력. Clean 출력의 기본값 (File/Snapshot 은 M2 메타데이터를 읽는 <path> 유지)
	bool nativeShapes = false;

	SvgWriteOptions() = default;
	explicit SvgWriteOptions(SvgWriteMode m) : mode(m), nativeShapes(m == SvgWriteMode::Clean) {}

	bool IncludeMetadata() const { return mode != SvgWriteMode::Clean; }      // m2:/inkscape:/sodipodi: 포함 여부
	bool EmbedImages() const     { return true; }  // 이미지는 항상 base64 로 임베드 (Undo 스냅샷도: 복원된 문서가 이미지 바이트를 다시 얻어야 하므로)
};

// M2Draw 확장 SVG 작성기 (Visitor 패턴 구현체)
// 표준 SVG 규격과 호환되면서, M2Draw 및 Inkscape 전용 메타데이터(파라메트릭 도형, 레이어, 노드 타입 등)를 보존
class SvgWriter : public INodeVisitor {
public:
	SvgWriter();
	virtual ~SvgWriter() = default;

	// 문서를 SVG 포맷 문자열(UTF-8)로 직렬화
	std::string WriteString(Document& doc, const SvgWriteOptions& options = SvgWriteOptions());

	// 문서를 지정된 파일 경로에 SVG 파일로 저장 (UTF-8 인코딩)
	bool WriteToFile(Document& doc, const std::wstring& filePath, const SvgWriteOptions& options = SvgWriteOptions());

	// INodeVisitor 인터페이스 구현
	void Visit(PathNode& node) override;
	void Visit(GroupNode& node) override;
	void Visit(LayerNode& node) override;
	void Visit(TextNode& node) override;
	void Visit(ImageNode& node) override;

private:
	void AppendIndent();
	void WriteGradientDefs(Document& doc);
	void WriteClipPathDefs(Document& doc);

	// 같은 내용의 그라디언트는 <defs> 에 한 번만 정의하고 여러 도형이 참조 (도형 -> 사용할 id)
	std::unordered_map<const PathNode*, std::string> m_gradIds;
	std::string GradientIdFor(const PathNode& p) const;

	std::string m_output;
	int         m_indent = 0;
	SvgWriteOptions m_options;

	bool Meta() const { return m_options.IncludeMetadata(); }
};

} // namespace ge
