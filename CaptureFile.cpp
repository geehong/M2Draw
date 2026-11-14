#include "pch.h"
#include "CoreUtil.h"
#include "CaptureFile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ge {

namespace {


int B64Value(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;   // '=' 와 줄바꿈 등
}

std::vector<uint8_t> Base64Decode(const std::string& s)
{
	std::vector<uint8_t> out;
	out.reserve(s.size() / 4 * 3);
	uint32_t acc = 0;
	int bits = 0;
	for (char c : s) {
		const int v = B64Value(c);
		if (v < 0) continue;
		acc = (acc << 6) | static_cast<uint32_t>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
		}
	}
	return out;
}

// XML 속성 값 이스케이프
std::string XmlAttr(const std::string& s)
{
	std::string out;
	for (char c : s) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		default: out += c;
		}
	}
	return out;
}

// CDATA 안에 ]]> 가 들어가면 끊기므로 나눠서 넣는다
std::string CDataText(const std::string& s)
{
	std::string out;
	size_t pos = 0;
	while (true) {
		const size_t hit = s.find("]]>", pos);
		if (hit == std::string::npos) {
			out += s.substr(pos);
			break;
		}
		out += s.substr(pos, hit + 2 - pos);   // "]]" 까지
		out += "]]><![CDATA[>";                   // ">" 를 다음 CDATA 로
		pos = hit + 3;
	}
	return out;
}

// 속성 값 읽기: name="값" (XML 이스케이프는 최소한만 되돌린다)
bool ReadAttr(const std::string& xml, const std::string& name, std::string& value)
{
	const std::string key = " " + name + "=\"";
	const size_t at = xml.find(key);
	if (at == std::string::npos) return false;
	const size_t begin = at + key.size();
	const size_t end = xml.find('"', begin);
	if (end == std::string::npos) return false;
	value.clear();
	for (size_t i = begin; i < end; ++i) {
		if (xml.compare(i, 5, "&amp;") == 0) { value += '&'; i += 4; }
		else if (xml.compare(i, 4, "&lt;") == 0) { value += '<'; i += 3; }
		else if (xml.compare(i, 4, "&gt;") == 0) { value += '>'; i += 3; }
		else if (xml.compare(i, 6, "&quot;") == 0) { value += '"'; i += 5; }
		else value += xml[i];
	}
	return true;
}

} // namespace

namespace {

bool ReadWholeFile(const std::filesystem::path& p, std::string& out)
{
	std::ifstream ifs(p, std::ios::binary);
	if (!ifs) return false;
	out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return true;
}

bool WriteWholeFile(const std::filesystem::path& p, const std::string& data)
{
	std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
	if (!ofs) return false;
	ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
	return static_cast<bool>(ofs);
}

// PNG 헤더의 너비·높이 (바이트 16~23, 빅엔디언)
void ReadPngSize(CaptureFileData& d)
{
	auto be32 = [&](size_t o) {
		return (static_cast<uint32_t>(d.png[o]) << 24) | (static_cast<uint32_t>(d.png[o + 1]) << 16) |
		       (static_cast<uint32_t>(d.png[o + 2]) << 8) | static_cast<uint32_t>(d.png[o + 3]);
	};
	if (d.png.size() >= 24) {
		d.width = static_cast<int>(be32(16));
		d.height = static_cast<int>(be32(20));
	}
}

} // namespace

bool IsCaptureManifest(const std::string& xml)
{
	// 캡처 목차(m2:capture) 또는 프로젝트 목차(m2:project) 는 같은 폴더 구조(reference.png, dom.json)를 쓴다
	return xml.find("<m2:capture") != std::string::npos || xml.find("<m2:project") != std::string::npos;
}

bool WriteCaptureFolder(const std::wstring& manifestPath, const CaptureFileData& data, std::wstring* err)
{
	namespace fs = std::filesystem;
	const fs::path manifest(manifestPath);
	const fs::path folder = manifest.parent_path();
	std::error_code ec;
	fs::create_directories(folder, ec);
	if (ec) {
		if (err) *err = L"폴더를 만들 수 없습니다: " + folder.wstring();
		return false;
	}

	const std::string png(data.png.begin(), data.png.end());
	if (!WriteWholeFile(folder / L"reference.png", png) || !WriteWholeFile(folder / L"dom.json", data.domJson)) {
		if (err) *err = L"캡처 파일을 쓰는 중 오류가 발생했습니다: " + folder.wstring();
		return false;
	}

	std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	xml += "<m2:capture xmlns:m2=\"https://m2draw/ns\" m2:version=\"1\"";
	xml += " m2:url=\"" + XmlAttr(data.url) + "\"";
	xml += " m2:created=\"" + XmlAttr(data.created) + "\"";
	xml += " m2:width=\"" + std::to_string(data.width) + "\"";
	xml += " m2:height=\"" + std::to_string(data.height) + "\">\n";
	xml += "  <m2:file path=\"reference.png\"/>\n";
	xml += "  <m2:file path=\"dom.json\"/>\n";
	xml += "</m2:capture>\n";
	if (!WriteWholeFile(manifest, xml)) {
		if (err) *err = L"목차 파일을 쓰는 중 오류가 발생했습니다: " + manifest.wstring();
		return false;
	}
	return true;
}

bool ReadCaptureFolder(const std::wstring& manifestPath, CaptureFileData& out, std::wstring* err)
{
	namespace fs = std::filesystem;
	const fs::path manifest(manifestPath);
	const fs::path folder = manifest.parent_path();

	std::string xml;
	if (!ReadWholeFile(manifest, xml) || !IsCaptureManifest(xml)) {
		if (err) *err = L"캡처 목차(.m2svg)를 읽을 수 없습니다.";
		return false;
	}
	if (!ReadAttr(xml, "m2:url", out.url))
		ReadAttr(xml, "m2:site", out.url);   // 프로젝트 목차는 m2:site 에 주소를 적는다
	ReadAttr(xml, "m2:created", out.created);

	std::string png;
	if (!ReadWholeFile(folder / L"reference.png", png) || png.empty()) {
		if (err) *err = L"폴더에 reference.png 가 없습니다.";
		return false;
	}
	out.png.assign(png.begin(), png.end());
	ReadPngSize(out);

	out.domJson.clear();
	ReadWholeFile(folder / L"dom.json", out.domJson);   // 없어도 된다 (주소와 그림만 있으면 불러온다)

	if (out.width <= 0 || out.height <= 0) {
		if (err) *err = L"그림 데이터를 읽지 못했습니다.";
		return false;
	}
	return true;
}

bool ReadCaptureFile(const std::wstring& path, CaptureFileData& out, std::wstring* err)
{
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		if (err) *err = L"파일을 열 수 없습니다: " + path;
		return false;
	}
	const std::string xml((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	if (xml.find("m2:kind=\"capture\"") == std::string::npos) {
		if (err) *err = L"캡처 파일(.m2svg)이 아닙니다.";
		return false;
	}

	// 주소
	const size_t capTag = xml.find("<m2:capture");
	if (capTag == std::string::npos || !ReadAttr(xml.substr(capTag, xml.find('>', capTag) - capTag + 1), "url", out.url)) {
		if (err) *err = L"캡처 파일의 주소 정보가 없습니다.";
		return false;
	}

	// DOM 요약 (CDATA, 나눠 넣은 ]]> 는 다시 합친다)
	// 쓰기 쪽은 "]]>" 바로 뒤에 "<![CDATA[" 를 이어 붙이므로, 연속된 CDATA 조각을 차례로 이어 붙인다
	out.domJson.clear();
	size_t p = xml.find("<![CDATA[", capTag);
	while (p != std::string::npos && xml.compare(p, 9, "<![CDATA[") == 0) {
		const size_t begin = p + 9;
		const size_t end = xml.find("]]>", begin);
		if (end == std::string::npos) break;
		out.domJson += xml.substr(begin, end - begin);
		p = end + 3;
	}

	// 그림 (data URI 의 base64)
	const size_t imgTag = xml.find("<image");
	const size_t dataAt = xml.find("base64,", imgTag == std::string::npos ? 0 : imgTag);
	if (dataAt == std::string::npos) {
		if (err) *err = L"캡처 파일에 그림이 없습니다.";
		return false;
	}
	const size_t dataEnd = xml.find('"', dataAt + 7);
	out.png = Base64Decode(xml.substr(dataAt + 7, dataEnd == std::string::npos ? std::string::npos : dataEnd - dataAt - 7));

	// 크기 (PNG 헤더, 빅엔디언 바이트 16~23)
	auto be32 = [&](size_t o) {
		return (static_cast<uint32_t>(out.png[o]) << 24) | (static_cast<uint32_t>(out.png[o + 1]) << 16) |
		       (static_cast<uint32_t>(out.png[o + 2]) << 8) | static_cast<uint32_t>(out.png[o + 3]);
	};
	if (out.png.size() >= 24) {
		out.width = static_cast<int>(be32(16));
		out.height = static_cast<int>(be32(20));
	}
	if (out.png.empty() || out.width <= 0 || out.height <= 0) {
		if (err) *err = L"그림 데이터를 읽지 못했습니다.";
		return false;
	}
	return true;
}

} // namespace ge
