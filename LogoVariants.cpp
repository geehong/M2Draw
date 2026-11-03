#include "pch.h"
#include "LogoVariants.h"
#include "SvgWriter.h"
#include "RasterExport.h"
#include "ReferenceImport.h"

#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

namespace ge {

namespace {

namespace fs = std::filesystem;

using SavedStyles = std::vector<std::pair<Node*, Style>>;

// 색을 바꿀 노드와 원래 스타일 목록 (레이어 자체, 레퍼런스, 이미지 노드는 제외)
void CollectRecolorable(GroupNode& group, SavedStyles& out)
{
	for (const auto& child : group.GetChildren()) {
		Node* n = child.get();
		if (!n || IsReferenceNode(*n)) continue;
		if (n->Kind() != NodeKind::Image)
			out.push_back({ n, n->GetStyle() });
		if (auto* sub = dynamic_cast<GroupNode*>(n))
			CollectRecolorable(*sub, out);
	}
}

// 채우기(있으면)와 선(두께가 있으면)을 단색으로 바꾼다. 알파는 원래 값을 유지
Style MonoStyle(const Style& orig, const Color& mono)
{
	Style s = orig;
	if (s.fill.type != PaintType::None) {
		s.fill.type = PaintType::Solid;
		s.fill.color = Color(mono.r, mono.g, mono.b, orig.fill.color.a);
	}
	if (s.stroke.width > 0.0f)
		s.stroke.color = Color(mono.r, mono.g, mono.b, orig.stroke.color.a);
	return s;
}

bool WriteBytes(const fs::path& p, const void* data, size_t n, std::wstring* err)
{
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, p.c_str(), L"wb") != 0 || !fp) {
		if (err) *err = L"파일을 쓸 수 없습니다: " + p.wstring();
		return false;
	}
	const size_t written = fwrite(data, 1, n, fp);
	fclose(fp);
	if (written != n) {
		if (err) *err = L"파일 쓰기에 실패했습니다: " + p.wstring();
		return false;
	}
	return true;
}

// 현재 색 상태로 한 변형의 파일 4개를 쓴다 (SVG 1 + PNG 3)
bool WriteVariant(Document& doc, const fs::path& dir, const std::wstring& name, std::wstring* err)
{
	SvgWriter writer;
	const std::string svg = writer.WriteString(doc, SvgWriteOptions(SvgWriteMode::Clean));
	if (!WriteBytes(dir / (name + L".svg"), svg.data(), svg.size(), err))
		return false;

	RasterExportOptions opt;
	opt.scale = 2.0f;

	std::wstring rerr;
	opt.transparent = true;
	if (!ExportRaster(doc, (dir / (name + L".png")).wstring(), opt, nullptr, &rerr)) {
		if (err) *err = name + L".png 저장 실패: " + rerr;
		return false;
	}

	// 배경이 있는 PNG: 문서 배경색을 잠시 바꿔서 쓴다 (알파 1 = 불투명)
	const Color saved = doc.backgroundColor;
	opt.transparent = false;

	doc.backgroundColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
	const bool onWhite = ExportRaster(doc, (dir / (name + L"-on-white.png")).wstring(), opt, nullptr, &rerr);
	doc.backgroundColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
	const bool onBlack = onWhite && ExportRaster(doc, (dir / (name + L"-on-black.png")).wstring(), opt, nullptr, &rerr);
	doc.backgroundColor = saved;

	if (!onWhite || !onBlack) {
		if (err) *err = name + L" 배경 PNG 저장 실패: " + rerr;
		return false;
	}
	return true;
}

} // namespace

bool ExportLogoVariants(Document& doc, const std::wstring& folder, std::wstring* err)
{
	std::error_code ec;
	const fs::path dir(folder);
	fs::create_directories(dir, ec);
	if (ec) {
		if (err) *err = L"폴더를 만들 수 없습니다: " + folder;
		return false;
	}

	SavedStyles saved;
	for (const auto& layer : doc.GetLayers()) {
		if (IsReferenceNode(*layer)) continue;
		CollectRecolorable(*layer, saved);
	}
	const Color savedBg = doc.backgroundColor;

	struct Variant { const wchar_t* name; bool mono; Color color; };
	const Variant variants[] = {
		{ L"logo-color", false, Color() },
		{ L"logo-black", true, Color(0.0f, 0.0f, 0.0f, 1.0f) },
		{ L"logo-white", true, Color(1.0f, 1.0f, 1.0f, 1.0f) },
	};

	bool ok = true;
	std::wstring localErr;
	for (const Variant& v : variants) {
		if (v.mono) {
			for (auto& [node, orig] : saved) {
				Style s = MonoStyle(orig, v.color);
				// 프레임은 배경 컨테이너이므로 채우기는 그대로 두고 선만 단색으로 바꾼다
				if (dynamic_cast<FrameNode*>(node)) s.fill = orig.fill;
				node->SetStyle(s);
			}
		} else {
			for (auto& [node, orig] : saved)
				node->SetStyle(orig);
		}
		ok = WriteVariant(doc, dir, v.name, &localErr);
		if (!ok) break;
	}

	// 실패해도 원래 상태로 되돌린다
	for (auto& [node, orig] : saved)
		node->SetStyle(orig);
	doc.backgroundColor = savedBg;

	if (!ok && err) *err = localErr;
	return ok;
}

} // namespace ge
