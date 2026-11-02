#include "pch.h"
#include "IconSet.h"
#include "RasterExport.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <vector>

namespace ge {

namespace {

namespace fs = std::filesystem;

struct PngEntry {
	int size = 0;
	std::vector<uint8_t> png;
};

bool ReadBytes(const fs::path& p, std::vector<uint8_t>& out)
{
	std::ifstream ifs(p, std::ios::binary);
	if (!ifs) return false;
	out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	return !out.empty();
}

bool WriteBytes(const fs::path& p, const void* data, size_t n, std::wstring* err)
{
	std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
	ofs.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
	if (!ofs) {
		if (err) *err = L"파일 쓰기 실패: " + p.wstring();
		return false;
	}
	return true;
}

// ICO 파일: 헤더 + 항목 목록 + PNG 데이터 (Windows Vista 이후 PNG 항목을 지원)
std::vector<uint8_t> BuildIco(const std::vector<PngEntry>& entries)
{
	std::vector<uint8_t> out;
	auto put16 = [&out](uint32_t v) {
		out.push_back(static_cast<uint8_t>(v & 0xFF));
		out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	};
	auto put32 = [&out](uint32_t v) {
		for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
	};

	put16(0);                                        // reserved
	put16(1);                                        // type = icon
	put16(static_cast<uint32_t>(entries.size()));    // count
	uint32_t offset = static_cast<uint32_t>(6 + 16 * entries.size());
	for (const auto& e : entries) {
		const uint8_t dim = e.size >= 256 ? 0 : static_cast<uint8_t>(e.size);   // 256 은 0 으로 적는다
		out.push_back(dim);
		out.push_back(dim);
		out.push_back(0);                            // 색 수 (0 = 제한 없음)
		out.push_back(0);                            // reserved
		put16(1);                                    // planes
		put16(32);                                   // bpp
		put32(static_cast<uint32_t>(e.png.size()));
		put32(offset);
		offset += static_cast<uint32_t>(e.png.size());
	}
	for (const auto& e : entries) out.insert(out.end(), e.png.begin(), e.png.end());
	return out;
}

// 대상 영역을 가운데 기준 정사각형으로 (아이콘은 정사각형이 기본)
Rect2 SquareAround(const Rect2& b)
{
	const float side = (std::max)(b.Width(), b.Height());
	const float cx = b.MidX(), cy = b.MidY();
	return Rect2{ cx - side * 0.5f, cy - side * 0.5f, cx + side * 0.5f, cy + side * 0.5f };
}

} // namespace

bool ExportIconSet(Document& doc, const std::wstring& folder, Node* target, std::wstring* err)
{
	std::error_code ec;
	const fs::path dir(folder);
	fs::create_directories(dir, ec);
	if (ec) {
		if (err) *err = L"폴더를 만들 수 없습니다: " + folder;
		return false;
	}

	Rect2 bounds{ 0.0f, 0.0f, doc.width, doc.height };
	if (target) Document::GetWorldBounds(*target, bounds);   // 실패하면 문서 전체
	const Rect2 square = SquareAround(bounds);
	const float side = square.Width();
	if (side <= 0.0f) {
		if (err) *err = L"내보낼 영역이 비어 있습니다.";
		return false;
	}

	// 1. 크기별 PNG (투명 배경, 레퍼런스 제외는 RasterExport 기본값)
	const int kPngSizes[] = { 16, 24, 32, 48, 64, 128, 180, 192, 256, 512 };
	std::map<int, std::vector<uint8_t>> png;
	for (int px : kPngSizes) {
		RasterExportOptions opt;
		// 픽셀 수는 ceil(영역 x 배율) 이므로, 부동소수 오차로 1px 커지지 않도록 배율을 살짝 낮춘다
		opt.scale = (static_cast<float>(px) - 0.001f) / side;
		const fs::path path = dir / (L"icon-" + std::to_wstring(px) + L".png");
		std::wstring rasterErr;
		if (!ExportRaster(doc, path.wstring(), opt, &square, &rasterErr)) {
			if (err) *err = L"icon-" + std::to_wstring(px) + L".png 저장 실패: " + rasterErr;
			return false;
		}
		std::vector<uint8_t> bytes;
		if (!ReadBytes(path, bytes)) {
			if (err) *err = L"파일을 다시 읽을 수 없습니다: " + path.wstring();
			return false;
		}
		png[px] = std::move(bytes);
	}

	// 2. ICO 묶음 (icon.ico: 앱/윈도우 아이콘, favicon.ico: 브라우저 탭 아이콘)
	auto writeIco = [&](const wchar_t* name, std::initializer_list<int> sizes) {
		std::vector<PngEntry> entries;
		for (int s : sizes) entries.push_back(PngEntry{ s, png[s] });
		const std::vector<uint8_t> data = BuildIco(entries);
		return WriteBytes(dir / name, data.data(), data.size(), err);
	};
	if (!writeIco(L"icon.ico", { 16, 32, 48, 256 })) return false;
	if (!writeIco(L"favicon.ico", { 16, 32, 48 })) return false;

	// 3. HTML 연결 태그 (그대로 <head> 에 붙여 넣는다)
	const std::string snippet =
		"<link rel=\"icon\" href=\"favicon.ico\" sizes=\"16x16 32x32 48x48\">\n"
		"<link rel=\"icon\" type=\"image/png\" sizes=\"192x192\" href=\"icon-192.png\">\n"
		"<link rel=\"apple-touch-icon\" sizes=\"180x180\" href=\"icon-180.png\">\n";
	return WriteBytes(dir / L"favicon-snippet.html", snippet.data(), snippet.size(), err);
}

} // namespace ge