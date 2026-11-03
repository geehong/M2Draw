#include "pch.h"
#include "AgentHandoff.h"
#include "SceneJsonWriter.h"
#include "RasterExport.h"
#include "ReferenceImport.h"
#include "AnnotationNote.h"
#include "AgentGuide.h"
#include "D2DLayer.h"

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

bool WriteUtf8File(const std::filesystem::path& path, const std::string& content, std::wstring* err);

std::wstring FromUtf8(const std::string& s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

// 기본 지침 (지침_템플릿.md 가 없을 때 만들어 두는 내용). 사용자가 파일에서 고칠 수 있다
const wchar_t* kDefaultRules =
	L"1. role 이 있는 요소는 그 HTML 태그를 사용하세요.\n"
	L"2. layout.mode 가 row 또는 column 이면 flex 로 구현하고 gap, padding 을 따르세요.\n"
	L"3. 색과 글꼴은 scene.json 의 값을 그대로 쓰고, 추측하지 마세요.\n"
	L"4. 결과는 result 폴더의 SVG 로 저장하세요 (앱에 바로 표시됩니다). index.html 과 styles.css 는 만들지 마세요.\n"
	L"5. 원본 글자(제목, 메뉴, 버튼, 기사 제목)는 reference.png 에서 읽은 그대로 옮기세요. 새로 만들거나 바꾸지 마세요.\n"
	L"6. 브랜드 이름과 로고 글자는 원본에 보이는 것만 씁니다. 없으면 로고 자리만 두세요.\n"
	L"7. 메뉴와 버튼 글자는 한 줄에 들어가도록 칸 폭을 넉넉히 잡으세요 (글자 하나당 약 9px 이상).\n"
	L"8. 원본 사진은 복사하지 말고, 같은 자리에 회색 상자와 \"사진\" 라벨로 두세요.\n"
	L"9. 대지 크기 안에는 원본의 첫 화면 범위만 담으세요 (상단 머리 바, 첫 번째 구획, 광고 자리). 그 아래 구획과 푸터는 만들지 마세요.\n";

// 지침 템플릿 읽기 (워크스페이스\지침_템플릿.md). 없으면 기본 지침으로 만들어 두고 그 내용을 쓴다. 줄바꿈은 \r\n
std::wstring LoadPromptRules()
{
	namespace fs = std::filesystem;
	const fs::path file = fs::path(ge::AgentWorkspaceRoot()) / L"지침_템플릿.md";
	std::ifstream ifs(file, std::ios::binary);
	std::wstring rules;
	if (ifs) {
		const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		rules = FromUtf8(text);
	} else {
		rules = kDefaultRules;
		std::error_code ec;
		fs::create_directories(file.parent_path(), ec);
		WriteUtf8File(file, ToUtf8(rules), nullptr);
	}
	std::wstring out;
	for (wchar_t c : rules) {
		if (c == L'\r') continue;
		if (c == L'\n') out += L"\r\n";
		else out += c;
	}
	if (!out.empty() && out.substr(out.size() - 2) != L"\r\n") out += L"\r\n";
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

void CollectFramesWithoutRole(const Node& node, std::vector<std::wstring>& out)
{
	if (dynamic_cast<const FrameNode*>(&node) && node.GetHtmlTag().empty())
		out.push_back(node.GetName());
	if (const auto* group = dynamic_cast<const GroupNode*>(&node)) {
		for (const auto& child : group->GetChildren())
			CollectFramesWithoutRole(*child, out);
	}
}

std::wstring Px(float v)
{
	return std::to_wstring(static_cast<long long>(std::lround(v)));
}

} // namespace

std::vector<std::wstring> FindFramesWithoutRole(const Document& doc, const Node* scope)
{
	std::vector<std::wstring> out;
	if (scope) {
		CollectFramesWithoutRole(*scope, out);
	} else {
		for (const auto& layer : doc.GetLayers())
			CollectFramesWithoutRole(*layer, out);
	}
	return out;
}

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
	s += L"- scene.json : 요소 구조 (계층, 역할(role), 크기, 레이아웃, 색, 글꼴, 좌표)\r\n";
	if (HasReferenceImage(doc))
		s += L"- reference.png : 목표 디자인 원본. preview.png 와 비교해 차이를 줄이세요\r\n";
	s += L"\r\n";
	s += L"대상: " + name + L" (" + Px(w) + L" x " + Px(h) + L" px)\r\n\r\n";
	s += L"지침\r\n";
	s += LoadPromptRules();
	const std::vector<std::wstring> notes = AnnotationTexts(doc);
	if (!notes.empty()) {
		s += L"\r\n주석 (사용자 메모. 해당 위치의 요소를 참고하세요)\r\n";
		for (const auto& note : notes)
			s += L"- " + note + L"\r\n";
	}
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

	// 3. 레퍼런스 원본 (있을 때만): 레퍼런스 레이어만 그리고 원본 색을 유지 (7.1.1)
	if (HasReferenceImage(doc)) {
		RasterExportOptions refOpt;
		refOpt.transparent = false;
		refOpt.layers = RenderLayerFilter::OnlyReference;
		if (!ExportRaster(doc, (dir / L"reference.png").wstring(), refOpt, nullptr, &rasterErr)) {
			if (err) *err = L"reference.png 저장 실패: " + rasterErr;
			return false;
		}
	}

	// 4. 지시문 초안 (사용자가 편집해 쓰도록 파일로 남김)
	if (!WriteUtf8File(dir / L"prompt.md", ToUtf8(BuildAgentPrompt(doc, opt.target)), err))
		return false;

	return true;
}

} // namespace ge
