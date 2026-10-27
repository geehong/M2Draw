#pragma once

#include <string>

namespace ge {

class Document;

// Windows 내장 XmlLite 기반의 SVG 파서 (Round-trip 역직렬화기)
// SvgWriter 가 출력한 M2Draw 확장 SVG 및 기본 SVG 파일을 Document 로 복원
class SvgReader {
public:
	SvgReader() = default;
	~SvgReader() = default;

	// SVG 문자열(UTF-8)로부터 문서를 파싱하여 복원
	bool ReadFromString(const std::string& svgContent, Document& outDoc);

	// SVG 파일로부터 문서를 파싱하여 복원
	bool ReadFromFile(const std::wstring& filePath, Document& outDoc);
};

} // namespace ge
