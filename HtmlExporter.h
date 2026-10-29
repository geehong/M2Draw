#pragma once

#include "D2DGraphicEngine.h"
#include "GeNodeVisitor.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

namespace ge {

// HTML 출력 옵션
struct HtmlExportOptions {
	bool singleFile = true;             // true: HTML 파일 1개에 CSS/이미지 인라인(base64) 포함, false: html + css + assets/ 분리
	bool extractCssClasses = true;      // true: 인라인 style 대신 클래스 기반 CSS 추출 및 중복 병합
	bool useRemUnits = false;           // true: px 대신 rem 단위 사용 (1rem = 16px 기준)
	float baseFontSize = 16.0f;         // rem 변환 기준 폰트 크기
	const FrameNode* targetArtboard = nullptr; // 특정 Artboard(최상위 Frame)만 내보내기 (nullptr 이면 전체 문서)
	std::string title = "M2Draw Export";
	bool tableAsGrid = false;           // true: 테이블을 <table> 대신 CSS Grid(div)로 출력
};

// HTML / CSS 내보내기 결과물
struct HtmlExportResult {
	std::string html;                   // <!DOCTYPE html>...
	std::string css;                    // 분리 모드일 때 별도 .css 내용
	std::vector<std::pair<std::string, std::vector<uint8_t>>> assets; // assets 파일 목록 (파일명, 바이트)
};

// ----------------------------------------------------------------------------
// HtmlExporter: INodeVisitor 기반 HTML/CSS 출력기
// FrameNode -> 시맨틱 컨테이너 / Flexbox
// TextNode  -> <p>, <h1~h6>, <span>
// ImageNode -> <img> (object-fit)
// PathNode  -> <div> (Rect/Ellipse) 또는 인라인 <svg>
// ----------------------------------------------------------------------------
class HtmlExporter : public INodeVisitor {
public:
	HtmlExporter();
	virtual ~HtmlExporter() = default;

	// Document 전체 또는 옵션에 지정된 Artboard 내보내기
	HtmlExportResult Export(Document& doc, const HtmlExportOptions& options = HtmlExportOptions());

	// 문자열(HTML 본문)만 간단히 얻기
	std::string ExportToString(Document& doc, const HtmlExportOptions& options = HtmlExportOptions());

	// 파일 또는 디렉토리에 저장
	bool ExportToFile(Document& doc, const std::wstring& htmlFilePath, const HtmlExportOptions& options = HtmlExportOptions());

	// INodeVisitor 구현
	void Visit(PathNode& node) override;
	void Visit(GroupNode& node) override;
	void Visit(LayerNode& node) override;
	void Visit(TextNode& node) override;
	void Visit(ImageNode& node) override;

private:
	std::string FormatUnit(float pxVal) const;
	std::string ColorToCss(const Color& c) const;
	std::string GenerateClassName(const Node& node, const std::string& prefix = "elem");
	std::string RegisterCssRule(const std::string& desiredPrefix, const std::string& decls, const Node* pNode = nullptr);
	std::string BuildCommonStyleDecls(const Node& node);
	void AppendIndent();
	void ExportTable(TableNode& table);   // <table>/<thead>/<tbody>/<tr>/<th>/<td> (또는 CSS Grid)
	std::string StyleOrClassAttr(const std::string& prefix, const std::string& decls, const Node* node);

	HtmlExportOptions m_options;
	HtmlExportResult  m_result;
	int               m_indent = 0;
	std::stringstream m_htmlStream;

	// CSS 클래스 중복 병합용: <선언 블록 문자열, 클래스명>
	std::unordered_map<std::string, std::string> m_styleToClass;
	// 생성된 CSS 룰 목록
	std::vector<std::pair<std::string, std::string>> m_cssRules; // <클래스명, 속성 목록>
	int m_classSeq = 1;
};

} // namespace ge
