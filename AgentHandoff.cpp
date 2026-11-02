#include "pch.h"
#include "AgentHandoff.h"
#include "SceneJsonWriter.h"
#include "RasterExport.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace ge {

namespace {

std::string ToUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}

bool WriteUtf8File(const std::filesystem::path& path, const std::string& content, std::wstring* err)
{
	std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
	if (!ofs) {
		if (err) *err = L"파일을 쓸 수 없습니다: " + path.wstring();
		return false;
	}
	ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
	if (!ofs) {
		if (err) *err = L"파일 쓰기에 실패했습니다: " + path.wstring();
		return false;
	}
	return true;
}

std::wstring Px(float v)
{
	return std::to_wstring(static_cast<long long>(std::lround(v)));
}

} // namespace

std::wstring BuildAgentPrompt(const Document& doc, const Node* target)
{
	std::wstring name = L"문서 전체";
	float w = doc.width, h = doc.height;

	if (target) {
		name = target->GetName();
		if (const auto* frame = dynamic_cast<const FrameNode*>(target)) {
			w = frame->GetWidth();
			h = frame->GetHeight();
		}
	}

	std::wstring s;
	s += L"아래 폴더의 파일을 참고해 웹페이지를 만들어 주세요.\r\n\r\n";
	s += L"- preview.png : 디자인 그림 (화면 구성 확인용)\r\n";
	s += L"- scene.json : 요소 구조 (계층, 역할(role), 크기, 레이아웃, 색, 글꼴, 좌표)\r\n\r\n";
	s += L"대상: " + name + L" (" + Px(w) + L" x " + Px(h) + L" px)\r\n\r\n";
	s += L"지침\r\n";
	s += L"1. role 이 있는 요소는 그 HTML 태그를 사용하세요.\r\n";
	s += L"2. layout.mode 가 row 또는 column 이면 flex 로 구현하고 gap, padding 을 따르세요.\r\n";
	s += L"3. 색과 글꼴은 scene.json 의 값을 그대로 쓰고, 추측하지 마세요.\r\n";
	s += L"4. 결과는 index.html 과 styles.css 로 주세요.\r\n";
	return s;
}

bool WriteAgentPackage(Document& doc, const std::wstring& folder, const AgentHandoffOptions& opt, std::wstring* err)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir(folder);
	fs::create_directories(dir, ec);
	if (ec) {
		if (err) *err = L"폴더를 만들 수 없습니다: " + folder;
		return false;
	}

	// 1. 구조 데이터
	SceneJsonOptions jsonOpt;
	jsonOpt.root = opt.target;
	if (!WriteUtf8File(dir / L"scene.json", SerializeSceneJson(doc, jsonOpt), err))
		return false;

	// 2. 그림: 대상이 있으면 그 영역만 (월드 경계), 없으면 문서 전체
	Rect2 region;
	const Rect2* regionPtr = nullptr;
	if (opt.target && Document::GetWorldBounds(*opt.target, region))
		regionPtr = &region;

	RasterExportOptions rasterOpt;
	std::wstring rasterErr;
	if (!ExportRaster(doc, (dir / L"preview.png").wstring(), rasterOpt, regionPtr, &rasterErr)) {
		if (err) *err = L"preview.png 저장 실패: " + rasterErr;
		return false;
	}

	// 3. 지시문 초안 (사용자가 편집해 쓰도록 파일로 남김)
	if (!WriteUtf8File(dir / L"prompt.md", ToUtf8(BuildAgentPrompt(doc, opt.target)), err))
		return false;

	return true;
}

} // namespace ge
