#pragma once

#include <cstdint>
#include <string>
#include <vector>

// URL 캡처 (폴더 형식): 캡처 한 건은 폴더 하나다.
//   webmd_202611031311\
//     webmd_202611031311.m2svg   ← 목차 (주소, 만든 날짜, 그림 크기, 파일 목록)
//     reference.png              ← 페이지 그림
//     dom.json                   ← DOM 요약
// 예전 단일 파일 형식(m2:kind="capture" 한 파일)도 읽을 수 있다.
namespace ge {

struct CaptureFileData {
	std::string          url;       // 캡처한 주소
	std::string          created;   // 만든 날짜 (표시용 문자열)
	std::string          domJson;   // 주소의 DOM 요약 (UTF-8 JSON)
	std::vector<uint8_t> png;       // 페이지 그림 (PNG)
	int                  width = 0;
	int                  height = 0;
};

// manifestPath = 폴더\이름.m2svg (폴더는 없으면 만든다)
bool WriteCaptureFolder(const std::wstring& manifestPath, const CaptureFileData& data, std::wstring* err);
// 목차(.m2svg)를 읽고, 같은 폴더의 reference.png 와 dom.json 을 함께 읽는다
bool ReadCaptureFolder(const std::wstring& manifestPath, CaptureFileData& out, std::wstring* err);
// 예전 단일 파일 형식 읽기 (호환용)
bool ReadCaptureFile(const std::wstring& path, CaptureFileData& out, std::wstring* err);
// 목차인지 확인 (폴더 형식)
bool IsCaptureManifest(const std::string& xml);

} // namespace ge
