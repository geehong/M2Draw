#include "pch.h"
#include "ReferenceVerification.h"
#include "ReferenceImport.h"
#include "ClipboardImage.h"
#include "LogoVariants.h"
#include "HtmlExporter.h"
#include "WebCapture.h"
#include "AnnotationNote.h"
#include "SvgReader.h"
#include <algorithm>
#include <functional>
#include "AgentHandoff.h"
#include "IconSet.h"
#include "SceneJsonWriter.h"
#include "SvgWriter.h"
#include "ClipboardSvg.h"
#include "D2DLayer.h"
#include "CaptureFile.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ge {
namespace test {

namespace {

namespace fs = std::filesystem;

#define REF_CHECK(cond, text) do { if (!(cond)) { if (failure) *failure = (text); return false; } } while (0)

struct Px { unsigned char b, g, r, a; };

std::string Utf8(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}

bool ReadAll(const fs::path& p, std::vector<uint8_t>& out)
{
	std::ifstream ifs(p, std::ios::binary);
	if (!ifs) return false;
	out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	return !out.empty();
}

bool ReadText(const fs::path& p, std::string& out)
{
	std::vector<uint8_t> b;
	if (!ReadAll(p, b)) return false;
	out.assign(b.begin(), b.end());
	return true;
}

// 이미지 크기 (WIC)
bool ImageSize(const std::wstring& path, unsigned& w, unsigned& h)
{
	ComPtr<IWICImagingFactory> f;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
	ComPtr<IWICBitmapDecoder> dec;
	if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return false;
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(dec->GetFrame(0, &frame))) return false;
	return SUCCEEDED(frame->GetSize(&w, &h));
}

// 이미지의 (x,y) 한 픽셀 (BGRA, 직선 알파)
bool ReadPixel(const std::wstring& path, unsigned x, unsigned y, Px& out)
{
	ComPtr<IWICImagingFactory> f;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
	ComPtr<IWICBitmapDecoder> dec;
	if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return false;
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(dec->GetFrame(0, &frame))) return false;
	ComPtr<IWICFormatConverter> conv;
	if (FAILED(f->CreateFormatConverter(&conv))) return false;
	if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
	WICRect rc{ static_cast<INT>(x), static_cast<INT>(y), 1, 1 };
	return SUCCEEDED(conv->CopyPixels(&rc, 4, 4, reinterpret_cast<BYTE*>(&out)));
}

bool NearColor(const Px& a, const Px& b, int tol)
{
	return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}

uint32_t U16(const std::vector<uint8_t>& d, size_t at) { return d[at] | (d[at + 1] << 8); }

void AppendLog(std::wstring* log, const std::wstring& line)
{
	if (log) *log += L"[PASS] " + line + L"\r\n";
}

} // namespace

bool RunReferenceAgentVerification(std::wstring* failure, std::wstring* log)
{
	// 1. 샘플 이미지 로드
	std::wstring samplePath = L"samples\\테스트레퍼런스_티스토리.png";
	if (wchar_t* env = nullptr; _wdupenv_s(&env, nullptr, L"M2_SELFTEST_IMAGE") == 0 && env) {
		samplePath = env;
		std::free(env);
	}
	unsigned sw = 0, sh = 0;
	REF_CHECK(ImageSize(samplePath, sw, sh), L"샘플 이미지를 열 수 없음: " + samplePath);
	auto data = std::make_shared<ImageData>();
	REF_CHECK(ReadAll(samplePath, data->bytes), L"샘플 이미지 바이트를 읽을 수 없음");
	data->mime = "image/png";
	data->pixelW = static_cast<int>(sw);
	data->pixelH = static_cast<int>(sh);
	AppendLog(log, L"샘플 이미지 로드: " + std::to_wstring(sw) + L"x" + std::to_wstring(sh));

	// 2. 문서: 역할 없는 빨간 프레임 + 역할(header) 프레임
	Document doc;
	doc.Clear(true);
	auto redNode = std::make_unique<FrameNode>();
	redNode->SetFrameSize(100.0f, 100.0f);
	redNode->SetTransform(Matrix2D::Translation(20.0f, 20.0f));
	redNode->SetName(L"빨강");
	Style redStyle = redNode->GetStyle();
	redStyle.fill.type = PaintType::Solid;
	redStyle.fill.color = Color(1.0f, 0.0f, 0.0f, 1.0f);
	redNode->SetStyle(redStyle);
	Node* red = doc.AddShape(std::move(redNode));
	REF_CHECK(red != nullptr, L"프레임 추가 실패");

	auto headerNode = std::make_unique<FrameNode>();
	headerNode->SetFrameSize(100.0f, 100.0f);
	headerNode->SetTransform(Matrix2D::Translation(200.0f, 20.0f));
	headerNode->SetName(L"머리");
	headerNode->SetHtmlTag("header");
	Node* header = doc.AddShape(std::move(headerNode));
	REF_CHECK(header != nullptr, L"역할 프레임 추가 실패");

	// 3. 역할 없는 프레임 검사 (7.3.4)
	{
		const auto noRole = FindFramesWithoutRole(doc, nullptr);
		bool hasRed = false, hasHeader = false;
		for (const auto& n : noRole) {
			if (n == L"빨강") hasRed = true;
			if (n == L"머리") hasHeader = true;
		}
		REF_CHECK(hasRed, L"역할 없는 프레임(빨강)이 경고 목록에 없음");
		REF_CHECK(!hasHeader, L"역할(header)이 있는 프레임이 경고 목록에 들어감");
		AppendLog(log, L"역할 없는 프레임 검사: 빨강만 경고 대상");
	}

	// 4. 레퍼런스 추가 (7.1.1)
	ImageNode* ref = AddReferenceImage(doc, data, L"테스트레퍼런스", 1.0f);
	REF_CHECK(ref != nullptr, L"레퍼런스 추가 실패");
	REF_CHECK(FindReferenceLayer(doc) != nullptr, L"레퍼런스 레이어가 없음");
	REF_CHECK(ref->IsLocked(), L"레퍼런스가 잠기지 않음");
	REF_CHECK(ref->GetOpacity() > 0.49f && ref->GetOpacity() < 0.51f, L"레퍼런스 투명도가 50%가 아님");
	REF_CHECK(IsReferenceNode(*ref), L"IsReferenceNode 가 레퍼런스를 못 찾음");
	REF_CHECK(!IsReferenceNode(*red), L"일반 프레임이 레퍼런스로 잘못 판정됨");
	REF_CHECK(doc.GetActiveLayerIndex() == 0, L"레퍼런스 추가 후 활성 레이어가 바뀜 (이후 도형이 레퍼런스에 들어감)");
	AppendLog(log, L"레퍼런스 추가: 잠금, 50% 투명, 활성 레이어 유지");

	// 5. SVG 내보내기 제외 / 저장 유지
	{
		SvgWriter w;
		const std::string clean = w.WriteString(doc, SvgWriteOptions(SvgWriteMode::Clean));
		REF_CHECK(clean.find("<image") == std::string::npos, L"순수 SVG 내보내기에 레퍼런스(<image>)가 남아 있음");
		SvgWriter w2;
		const std::string file = w2.WriteString(doc, SvgWriteOptions(SvgWriteMode::File));
		REF_CHECK(file.find("<image") != std::string::npos, L"파일 저장(File)에서 레퍼런스가 빠짐");
		AppendLog(log, L"SVG: 순수 내보내기는 제외, 파일 저장은 유지");
	}

	// 6. scene.json: 레퍼런스 제외, 역할 포함
	{
		const std::string json = SerializeSceneJson(doc);
		REF_CHECK(json.find(Utf8(L"테스트레퍼런스")) == std::string::npos, L"scene.json 에 레퍼런스가 들어 있음");
		REF_CHECK(json.find("\"role\":\"header\"") != std::string::npos, L"scene.json 에 역할(header)이 없음");
		AppendLog(log, L"scene.json: 레퍼런스 제외, 역할(header) 포함");
	}

	// 7. AI 패키지 (문서 전체): preview 는 레퍼런스 없음, reference.png 는 원본 색
	const fs::path base = fs::temp_directory_path() / L"m2draw_selftest";
	std::error_code ec;
	fs::remove_all(base, ec);
	{
		AgentHandoffOptions opt;   // 대상 없음 = 문서 전체
		std::wstring err;
		REF_CHECK(WriteAgentPackage(doc, (base / L"pkg_all").wstring(), opt, &err), L"패키지 저장 실패(전체): " + err);
		const fs::path d = base / L"pkg_all";
		REF_CHECK(fs::exists(d / L"scene.json"), L"scene.json 없음");
		REF_CHECK(fs::exists(d / L"preview.png"), L"preview.png 없음");
		REF_CHECK(fs::exists(d / L"prompt.md"), L"prompt.md 없음");
		REF_CHECK(fs::exists(d / L"reference.png"), L"reference.png 없음 (레퍼런스가 있는데 생성되지 않음)");

		// preview 중앙: 레퍼런스가 빠졌으므로 투명 배경(alpha 0)이어야 한다
		Px p{};
		REF_CHECK(ReadPixel((d / L"preview.png").wstring(), 960, 540, p), L"preview.png 픽셀 읽기 실패");
		REF_CHECK(p.a == 0, L"preview.png 중앙에 레퍼런스가 보임 (제외되지 않음)");

		// reference.png 중앙: 원본 샘플의 같은 지점과 색이 같아야 한다 (투명도 50% 가 아니라 원본 색)
		unsigned rw = 0, rh = 0;
		REF_CHECK(ImageSize((d / L"reference.png").wstring(), rw, rh), L"reference.png 크기 읽기 실패");
		const double W = doc.width, H = doc.height;
		const double sc = (std::min)(1.0, (std::min)(W / sw, H / sh));
		const double ox = (W - sw * sc) * 0.5, oy = (H - sh * sc) * 0.5;
		const unsigned u = static_cast<unsigned>((std::max)(0.0, (std::min)(sw - 1.0, (960.5 - ox) / sc)));
		const unsigned v = static_cast<unsigned>((std::max)(0.0, (std::min)(sh - 1.0, (540.5 - oy) / sc)));
		Px src{}, out{};
		REF_CHECK(ReadPixel(samplePath, u, v, src), L"샘플 픽셀 읽기 실패");
		REF_CHECK(ReadPixel((d / L"reference.png").wstring(), 960, 540, out), L"reference.png 픽셀 읽기 실패");
		REF_CHECK(out.a == 255, L"reference.png 가 불투명하지 않음 (투명도 50% 가 남아 있음)");
		REF_CHECK(NearColor(out, src, 60), L"reference.png 색이 원본과 다름 (옅게 보임)");
		AppendLog(log, L"패키지(전체): scene/preview/prompt/reference 생성, preview 는 레퍼런스 없음, reference 는 원본 색");

		std::string prompt;
		REF_CHECK(ReadText(d / L"prompt.md", prompt), L"prompt.md 읽기 실패");
		REF_CHECK(prompt.find("reference.png") != std::string::npos, L"prompt.md 에 reference.png 안내가 없음");
	}

	// 8. AI 패키지 (프레임 대상): scene.json 은 그 프레임만, header 는 빠져야 함
	{
		AgentHandoffOptions opt;
		opt.target = red;
		std::wstring err;
		REF_CHECK(WriteAgentPackage(doc, (base / L"pkg_frame").wstring(), opt, &err), L"패키지 저장 실패(프레임): " + err);
		std::string json;
		REF_CHECK(ReadText(base / L"pkg_frame" / L"scene.json", json), L"프레임 패키지 scene.json 읽기 실패");
		REF_CHECK(json.find("\"role\":\"header\"") == std::string::npos, L"프레임 대상 scene.json 에 다른 프레임(header)이 들어감");
		REF_CHECK(json.find("\"name\":\"" + Utf8(L"빨강") + "\"") != std::string::npos, L"프레임 대상 scene.json 에 대상 프레임이 없음");
		AppendLog(log, L"패키지(프레임): 대상 프레임만 scene.json 에 포함");
	}

	// 9. 아이콘 세트 (빨간 프레임 대상)
	{
		std::wstring err;
		REF_CHECK(ExportIconSet(doc, (base / L"icons").wstring(), red, &err), L"아이콘 세트 저장 실패: " + err);
		const fs::path d = base / L"icons";
		const int sizes[] = { 16, 24, 32, 48, 64, 128, 180, 192, 256, 512 };
		for (int s : sizes) {
			const fs::path p = d / (L"icon-" + std::to_wstring(s) + L".png");
			unsigned w = 0, h = 0;
			REF_CHECK(fs::exists(p) && ImageSize(p.wstring(), w, h), L"아이콘 PNG 없음: " + p.filename().wstring());
			REF_CHECK(w == static_cast<unsigned>(s) && h == static_cast<unsigned>(s), L"아이콘 PNG 크기가 틀림: " + p.filename().wstring());
		}
		Px c{};
		REF_CHECK(ReadPixel((d / L"icon-16.png").wstring(), 8, 8, c), L"icon-16 픽셀 읽기 실패");
		REF_CHECK(c.a == 255 && c.r > 200 && c.g < 40 && c.b < 40, L"icon-16 중앙이 빨강이 아님 (16px 축소 결과 확인)");

		std::vector<uint8_t> ico, fav, snip;
		REF_CHECK(ReadAll(d / L"icon.ico", ico) && ReadAll(d / L"favicon.ico", fav), L"ico 파일 없음");
		REF_CHECK(U16(ico, 4) == 4, L"icon.ico 항목 수가 4가 아님");
		REF_CHECK(U16(fav, 4) == 3, L"favicon.ico 항목 수가 3이 아님");
		REF_CHECK(ico[6] == 16 && ico[6 + 3 * 16] == 0, L"icon.ico 의 16px / 256px 항목이 틀림");
		REF_CHECK(ReadAll(d / L"favicon-snippet.html", snip), L"favicon-snippet.html 없음");
		AppendLog(log, L"아이콘: PNG 10종 크기, icon.ico 4항목, favicon.ico 3항목, 16px 중앙 빨강");
	}

	// 10. SVG 가져오기 (7.2.3): 역할 프레임을 복사해 빨간 프레임 안에 넣는다
	{
		std::vector<Node*> srcNodes{ header };
		const std::string svg = SerializeNodesToSvg(doc, srcNodes);
		REF_CHECK(!svg.empty(), L"SVG 직렬화 실패");
		Document tmp;
		std::vector<const Node*> parsed;
		REF_CHECK(ParseSvgNodes(svg, tmp, parsed) && !parsed.empty(), L"SVG 파싱 실패");
		std::vector<Node*> imported = doc.ImportNodes(parsed);
		REF_CHECK(!imported.empty(), L"노드 가져오기 실패");
		int moved = 0;
		for (Node* n : imported)
			if (doc.MoveNodeTo(n, static_cast<GroupNode*>(red), -1)) ++moved;
		REF_CHECK(moved == static_cast<int>(imported.size()), L"가져온 노드를 프레임 안으로 옮기지 못함");
		REF_CHECK(imported.front()->GetParent() == red, L"가져온 노드의 부모가 대상 프레임이 아님");
		AppendLog(log, L"SVG 가져오기: 대상 프레임의 자식으로 들어감");
	}

	// 11. 클립보드 비트맵 붙여넣기 경로 (스크린샷 Ctrl+V): CF_BITMAP -> PNG -> 레퍼런스 (7.1)
	{
		CWnd* owner = CWnd::FromHandle(::GetDesktopWindow());
		HDC dc = ::GetDC(nullptr);
		HBITMAP hbm = ::CreateCompatibleBitmap(dc, 64, 48);
		HDC mem = ::CreateCompatibleDC(dc);
		HGDIOBJ old = ::SelectObject(mem, hbm);
		HBRUSH brush = ::CreateSolidBrush(RGB(0, 128, 255));
		RECT rc{ 0, 0, 64, 48 };
		::FillRect(mem, &rc, brush);
		::DeleteObject(brush);
		::SelectObject(mem, old);
		::DeleteDC(mem);
		::ReleaseDC(nullptr, dc);
		REF_CHECK(owner && owner->OpenClipboard(), L"클립보드를 열 수 없음");
		::EmptyClipboard();
		::SetClipboardData(CF_BITMAP, hbm);   // 이제 클립보드가 소유하므로 hbm 은 지우지 않는다
		::CloseClipboard();

		auto clip = ReadClipboardBitmapAsPng(owner);
		REF_CHECK(clip != nullptr, L"클립보드 비트맵을 읽지 못함");
		REF_CHECK(clip->pixelW == 64 && clip->pixelH == 48, L"클립보드 비트맵 크기가 틀림");
		REF_CHECK(clip->bytes.size() > 8 && clip->bytes[0] == 0x89 && clip->bytes[1] == 'P' && clip->bytes[2] == 'N' && clip->bytes[3] == 'G', L"클립보드 비트맵이 PNG 로 바뀌지 않음");
		ImageNode* pasted = AddReferenceImage(doc, clip, L"클립보드 이미지", 1.0f);
		REF_CHECK(pasted != nullptr && IsReferenceNode(*pasted) && pasted->IsLocked(), L"붙여넣은 비트맵이 레퍼런스 레이어에 잠긴 상태로 들어가지 않음");
		AppendLog(log, L"클립보드 비트맵: CF_BITMAP -> PNG -> 레퍼런스(잠금)");
	}
	// 12. 레퍼런스 색 추출 (7.1.2): 왼쪽 위 지점의 색은 샘플 이미지의 대응 픽셀과 같아야 한다 (중앙은 클립보드 이미지가 덮음)
	{
		const double W = doc.width, H = doc.height;
		const double sc = (std::min)(1.0, (std::min)(W / sw, H / sh));
		const double ox = (W - sw * sc) * 0.5, oy = (H - sh * sc) * 0.5;
		const unsigned u = static_cast<unsigned>((std::max)(0.0, (std::min)(sw - 1.0, (300.5 - ox) / sc)));
		const unsigned v = static_cast<unsigned>((std::max)(0.0, (std::min)(sh - 1.0, (300.5 - oy) / sc)));
		Px src{};
		REF_CHECK(ReadPixel(samplePath, u, v, src), L"샘플 픽셀 읽기 실패");
		Color c;
		REF_CHECK(SampleReferenceColor(doc, Vec2(300.5f, 300.5f), c), L"레퍼런스 색 추출 실패");
		if (!(std::abs(c.r * 255.0f - src.r) <= 6 && std::abs(c.g * 255.0f - src.g) <= 6 && std::abs(c.b * 255.0f - src.b) <= 6)) { if (failure) *failure = L"추출한 색이 원본 픽셀과 다름: 추출 " + std::to_wstring(static_cast<int>(c.r * 255)) + L"," + std::to_wstring(static_cast<int>(c.g * 255)) + L"," + std::to_wstring(static_cast<int>(c.b * 255)) + L" / 원본(" + std::to_wstring(u) + L"," + std::to_wstring(v) + L") " + std::to_wstring(src.r) + L"," + std::to_wstring(src.g) + L"," + std::to_wstring(src.b); return false; }
		REF_CHECK(!SampleReferenceColor(doc, Vec2(-500.0f, -500.0f), c), L"이미지 밖 점에서 색이 추출됨");
		AppendLog(log, L"레퍼런스 색 추출: 왼쪽 위 픽셀과 일치, 밖은 거부");
	}

	// 13. 로고 색 변형 (7.4.3): 검정 변형에는 원래 색이 없고, 레퍼런스는 없으며, 끝나면 원래 색으로 돌아온다
	{
		std::wstring err;
		const fs::path ld = base / L"logo";
		REF_CHECK(ExportLogoVariants(doc, ld.wstring(), &err), L"로고 변형 저장 실패: " + err);
		const char* names[] = { "logo-color", "logo-black", "logo-white" };
		for (const char* n : names) {
			const std::wstring w(n, n + std::strlen(n));
			REF_CHECK(fs::exists(ld / (w + L".svg")) && fs::exists(ld / (w + L".png")) && fs::exists(ld / (w + L"-on-white.png")) && fs::exists(ld / (w + L"-on-black.png")), L"로고 변형 파일이 빠짐: " + w);
		}
		auto lower = [](std::string s) { for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; };
		std::string colorSvg, blackSvg;
		REF_CHECK(ReadText(ld / L"logo-color.svg", colorSvg) && ReadText(ld / L"logo-black.svg", blackSvg), L"로고 SVG 읽기 실패");
		colorSvg = lower(colorSvg);
		blackSvg = lower(blackSvg);
		REF_CHECK(colorSvg.find("#ff0000") != std::string::npos, L"원본 로고에 빨강이 없음");
		REF_CHECK(blackSvg.find("#ff0000") != std::string::npos, L"프레임 채우기(빨강)가 유지되지 않음 (프레임은 채우기를 바꾸지 않아야 함)");
		REF_CHECK(blackSvg.find("<image") == std::string::npos, L"로고 SVG 에 레퍼런스가 들어감");
		const Style after = red->GetStyle();
		REF_CHECK(after.fill.color.r > 0.9f && after.fill.color.g < 0.1f, L"로고 내보내기 뒤 원래 색이 복원되지 않음");
		AppendLog(log, L"로고 변형: 원본/검정/흰색 SVG+PNG 생성, 프레임 채우기 유지, 레퍼런스 제외, 원래 색 복원");
	}
	// 14. SVG 최적화 (7.4.4): 소수 2자리 출력은 소수 넷째 자리까지 쓰지 않는다
	{
		auto frac = std::make_unique<FrameNode>();
		frac->SetFrameSize(10.0f, 10.0f);
		frac->SetTransform(Matrix2D::Translation(33.3333f, 44.4444f));
		frac->SetName(L"소수");
		Node* fracNode = doc.AddShape(std::move(frac));
		REF_CHECK(fracNode != nullptr, L"소수 좌표 프레임 추가 실패");
		SvgWriter w4;
		const std::string s4 = w4.WriteString(doc, SvgWriteOptions(SvgWriteMode::Clean));
		SvgWriteOptions o2(SvgWriteMode::Clean);
		o2.decimals = 2;
		o2.optimize = true;
		SvgWriter w2;
		const std::string s2 = w2.WriteString(doc, o2);
		REF_CHECK(s4.find("33.3333") != std::string::npos, L"기본 출력(4자리)에 소수 좌표가 없음");
		REF_CHECK(s2.find("33.33") != std::string::npos && s2.find("33.3333") == std::string::npos, L"최적화 출력(2자리)이 줄어들지 않음");
		REF_CHECK(s2.size() < s4.size(), L"최적화 출력이 기본보다 짧지 않음");
		REF_CHECK(s2.find("id=\"path_") == std::string::npos && s2.find("<g id=") == std::string::npos, L"최적화 출력에 식별자(id)가 남아 있음");
		AppendLog(log, L"SVG 최적화: 소수 2자리 출력이 4자리보다 짧음");
	}

	// 15. 웹 프로젝트 묶음 (7.4.5) + 웹폰트 <head> 추가 (7.1.4)
	{
		HtmlExporter he;
		HtmlExportOptions ho;
		ho.singleFile = false;
		ho.headExtra = "  <link rel=\"stylesheet\" href=\"https://example.com/font.css\">";
		const fs::path wd = base / L"web" / L"index.html";
		REF_CHECK(he.ExportToFile(doc, wd.wstring(), ho), L"웹 프로젝트 저장 실패");
		REF_CHECK(fs::exists(base / L"web" / L"styles.css"), L"styles.css 가 없음");
		std::string html;
		REF_CHECK(ReadText(wd, html), L"index.html 읽기 실패");
		REF_CHECK(html.find("font.css") != std::string::npos, L"웹폰트 링크가 <head> 에 없음");
		REF_CHECK(html.find("styles.css") != std::string::npos && html.find("<style>") == std::string::npos, L"분리 모드인데 CSS 가 인라인으로 들어감");
		AppendLog(log, L"웹 프로젝트: index.html + styles.css, 웹폰트 링크 삽입");
	}
	// 16. URL 캡처 (7.1.6 / 7.1.7): 로컬 HTML 을 WebView2 로 찍어 크기, 픽셀 색, DOM 블록을 검사한다
	{
		fs::create_directories(base, ec);
		const fs::path htmlPath = base / L"capture_test.html";
		const std::wstring html = L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>selftest</title><style>h1,p{margin:0}</style></head>"
			L"<body style=\"margin:0\"><header style=\"height:100px;background:#ff0000\"><h1>헤더</h1></header>"
			L"<section style=\"height:1900px;background:#00ff00\"><p>본문</p></section></body></html>";
		{
			const std::string bytes = Utf8(html);
			std::ofstream hf(htmlPath, std::ios::binary | std::ios::trunc);
			hf.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		}
		const std::wstring fileUrl = L"file:///" + htmlPath.generic_wstring();
		WebCaptureResult cap;
		std::wstring capErr;
		REF_CHECK(CaptureWebPage(fileUrl, 800, 30000, cap, &capErr), L"URL 캡처 실패: " + capErr);
		REF_CHECK(cap.pixelW == 800, L"캡처 폭이 800 이 아님");
		REF_CHECK(cap.pixelH >= 2000, L"캡처 높이가 페이지 높이(2000)보다 작음");
		REF_CHECK(cap.domJson.find("regions") != std::string::npos && cap.domJson.find("header") != std::string::npos, L"레이아웃 요약(regions/header)이 없음");
		const fs::path pngPath = base / L"capture_test.png";
		{
			std::ofstream pf(pngPath, std::ios::binary | std::ios::trunc);
			pf.write(reinterpret_cast<const char*>(cap.png.data()), static_cast<std::streamsize>(cap.png.size()));
		}
		Px hp{}, bp{};
		REF_CHECK(ReadPixel(pngPath.wstring(), 10, 10, hp) && hp.r > 200 && hp.g < 60 && hp.b < 60, L"캡처 그림의 헤더(빨강) 픽셀이 다름");
		REF_CHECK(ReadPixel(pngPath.wstring(), 10, 500, bp) && bp.g > 200 && bp.r < 60, L"캡처 그림의 본문(초록) 픽셀이 다름");
		AppendLog(log, L"URL 캡처(로컬 HTML): 800 x " + std::to_wstring(cap.pixelH) + L", 헤더/본문 색과 DOM 영역 확인");

		// 선택: 실제 사이트 (환경변수 M2_SELFTEST_URL). 결과만 기록하고 실패로 보지 않는다
		std::wstring liveUrl;
		if (wchar_t* e = nullptr; _wdupenv_s(&e, nullptr, L"M2_SELFTEST_URL") == 0 && e) {
			liveUrl = e;
			std::free(e);
		}
		if (!liveUrl.empty()) {
			WebCaptureResult live;
			std::wstring liveErr;
			if (CaptureWebPage(liveUrl, 1440, 60000, live, &liveErr)) {
				size_t blocks = 0;
				for (size_t at = live.domJson.find("\"role\""); at != std::string::npos; at = live.domJson.find("\"role\"", at + 1)) ++blocks;
				AppendLog(log, std::wstring(L"실제 사이트 캡처: ") + liveUrl + L" -> " + std::to_wstring(live.pixelW) + L" x " + std::to_wstring(live.pixelH) + L", 영역 " + std::to_wstring(blocks) + L"개");
			} else {
				AppendLog(log, std::wstring(L"실제 사이트 캡처 실패: ") + liveErr);
			}
		}
	}
	// 17. 주석 (7.3.3): scene.json 과 프롬프트에는 들어가고, 순수 SVG 에서는 빠진다. 파일 저장에는 남는다
	{
		const std::wstring noteText = L"테스트메모";
		REF_CHECK(AddAnnotation(doc, noteText, Vec2(300.0f, 300.0f)) != nullptr, L"주석 추가 실패");
		const auto notes = AnnotationTexts(doc);
		REF_CHECK(std::find(notes.begin(), notes.end(), noteText) != notes.end(), L"주석 목록에 메모가 없음");
		const std::string json = SerializeSceneJson(doc);
		REF_CHECK(json.find("\"annotations\"") != std::string::npos && json.find(Utf8(noteText)) != std::string::npos, L"scene.json 에 주석이 없음");
		SvgWriter wc;
		const std::string clean = wc.WriteString(doc, SvgWriteOptions(SvgWriteMode::Clean));
		REF_CHECK(clean.find(Utf8(noteText)) == std::string::npos, L"순수 SVG 에 주석이 들어감");
		SvgWriter wf;
		const std::string file = wf.WriteString(doc, SvgWriteOptions(SvgWriteMode::File));
		REF_CHECK(file.find(Utf8(noteText)) != std::string::npos, L"파일 저장에서 주석이 빠짐");
		AgentHandoffOptions opt;
		std::wstring err;
		REF_CHECK(WriteAgentPackage(doc, (base / L"pkg_note").wstring(), opt, &err), L"주석 포함 패키지 저장 실패: " + err);
		std::string prompt;
		REF_CHECK(ReadText(base / L"pkg_note" / L"prompt.md", prompt) && prompt.find(Utf8(noteText)) != std::string::npos, L"prompt.md 에 주석이 없음");
		AppendLog(log, L"주석: scene.json/프롬프트 포함, 순수 SVG 제외, 파일 저장 유지");
	}
	// 18. 웹폰트 링크 (7.1.4): 문서에 저장되어 SVG 저장 후 다시 열어도 남는다
	{
		doc.webFontHead = L"<link rel=\"stylesheet\" href=\"https://fonts.example/f.css\">";
		SvgWriter wf;
		const std::string svg = wf.WriteString(doc, SvgWriteOptions(SvgWriteMode::File));
		REF_CHECK(svg.find("m2:webFontHead") != std::string::npos, L"SVG 에 웹폰트 속성이 없음");
		Document back;
		SvgReader rd;
		REF_CHECK(rd.ReadFromString(svg, back), L"SVG 다시 읽기 실패");
		REF_CHECK(back.webFontHead == doc.webFontHead, L"웹폰트 링크가 SVG 왕복 후 사라짐");
		AppendLog(log, L"웹폰트 링크: 문서 속성으로 SVG 저장/다시 열기 유지");
	}
	// 19. 캡처 파일 (*.m2svg): 주소·DOM 요약·그림이 저장 후 그대로 돌아온다 (7.1.6 저장/불러오기)
	{
		const std::vector<uint8_t> tiny = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
			0, 0, 0, 7, 0, 0, 0, 3, 8, 6, 0, 0, 0, 1, 2, 3, 4, 5 };   // 7 x 3 크기 PNG 머리 (내용은 비교용 바이트)
		ge::CaptureFileData cap;
		cap.url = "https://example.com/?q=a&b=<c>";
		cap.domJson = "{\"title\":\"x\",\"note\":\"]]> 포함\"}";
		cap.png = tiny;
		cap.width = 7;
		cap.height = 3;
		const fs::path folder = fs::temp_directory_path() / L"m2draw_capture_check";
		const fs::path file = folder / L"m2draw_capture_check.m2svg";
		std::wstring cerr;
		REF_CHECK(ge::WriteCaptureFolder(file.wstring(), cap, &cerr), L"캡처 폴더 저장 실패");
		ge::CaptureFileData back;
		REF_CHECK(ge::ReadCaptureFolder(file.wstring(), back, &cerr), L"캡처 폴더 다시 읽기 실패");
		REF_CHECK(back.url == cap.url, L"캡처 주소가 왕복 후 달라짐");
		REF_CHECK(back.domJson == cap.domJson, L"DOM 요약이 왕복 후 달라짐 (]]> 분할 포함)");
		REF_CHECK(back.png == cap.png, L"그림 바이트가 왕복 후 달라짐");
		REF_CHECK(back.width == 7 && back.height == 3, L"그림 크기가 왕복 후 달라짐");
		std::error_code rmEc;
		fs::remove_all(folder, rmEc);
		AppendLog(log, L"캡처 폴더(목차 *.m2svg + reference.png + dom.json): 주소, DOM 요약, 그림 왕복 유지");
	}
	// 20. 레퍼런스 연결: 노드마다 연결 경로를 주면 그림 대신 경로가 남고, 다시 읽으면 경로만 남는다
	{
		auto img = std::make_shared<ge::ImageData>();
		img->bytes = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 7, 0, 0, 0, 3, 8, 6, 0, 0, 0 };
		img->mime = "image/png";
		img->pixelW = 7;
		img->pixelH = 3;
		Document linked;
		ImageNode* refNode = ge::AddReferenceImage(linked, img, L"a", 1.0f, false);
		REF_CHECK(refNode != nullptr, L"레퍼런스 노드 만들기 실패");
		SvgWriteOptions lo(SvgWriteMode::File);
		lo.linkReferenceImages = true;
		lo.linkedImagePaths[refNode] = "../references/a.png";
		SvgWriter lw;
		const std::string lsvg = lw.WriteString(linked, lo);
		REF_CHECK(lsvg.find("../references/a.png") != std::string::npos, L"연결 경로가 SVG 에 없음");
		REF_CHECK(lsvg.find("base64") == std::string::npos, L"연결 모드인데 그림 데이터가 들어감");
		Document lback;
		SvgReader lrd;
		REF_CHECK(lrd.ReadFromString(lsvg, lback), L"연결 SVG 다시 읽기 실패");
		bool linkedBack = false;
		std::function<void(const Node&)> walk = [&](const Node& n) {
			if (const auto* im = dynamic_cast<const ImageNode*>(&n))
				if (im->GetLinkHref() == "../references/a.png" && !im->GetData()) linkedBack = true;
			if (const auto* g = dynamic_cast<const GroupNode*>(&n))
				for (const auto& c : g->GetChildren()) walk(*c);
		};
		for (const auto& l : lback.GetLayers()) walk(*l);
		REF_CHECK(linkedBack, L"다시 읽은 레퍼런스에 연결 경로가 남지 않음");
		AppendLog(log, L"레퍼런스 연결: 페이지에는 경로만, 읽으면 경로만 남음");
	}	// 생성 결과는 남겨 둔다 (확인용): %TEMP%\m2draw_selftest 에 pkg_all, pkg_frame, icons 폴더
	return true;
}

} // namespace test
} // namespace ge
