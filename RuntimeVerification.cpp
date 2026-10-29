#include "pch.h"
#include "RasterExport.h"
#include "ClipboardSvg.h"
#include "SnapManager.h"
#include "RegionFill.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>

using Microsoft::WRL::ComPtr;

// Debug 시작 시 실행되는 런타임 자체 검증 (D2D/WIC 가 필요한 것들). GeModelPureCheck 는 D2D 없는 순수 모델만 검사하므로 따로 둔다.
namespace ge {

namespace {

struct Px { unsigned char b, g, r, a; };

// 이미지 파일을 WIC 로 읽어 크기와 한 픽셀(BGRA)을 돌려준다
bool ReadPixel(const std::wstring& path, unsigned& w, unsigned& h, unsigned x, unsigned y, Px& out)
{
	ComPtr<IWICImagingFactory> wic;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
	ComPtr<IWICBitmapDecoder> dec;
	if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) return false;
	ComPtr<IWICBitmapFrameDecode> fr;
	if (FAILED(dec->GetFrame(0, &fr))) return false;
	fr->GetSize(&w, &h);
	if (x >= w || y >= h) return false;
	ComPtr<IWICFormatConverter> conv;
	if (FAILED(wic->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
	WICRect rc{ static_cast<INT>(x), static_cast<INT>(y), 1, 1 };
	return SUCCEEDED(conv->CopyPixels(&rc, 4, sizeof(Px), reinterpret_cast<BYTE*>(&out)));
}

std::unique_ptr<PathNode> MakeRect(float x, float y, float w, float h, Color fill)
{
	auto p = std::make_unique<PathNode>();
	p->SetShapeKind(ShapeKind::Rect);
	p->SetParams(RectParams{ 0.0f, 0.0f, true });
	BezierNode a, b, c, d;
	a.anchor = Vec2{ 0, 0 }; b.anchor = Vec2{ w, 0 }; c.anchor = Vec2{ w, h }; d.anchor = Vec2{ 0, h };
	SubPath sp; sp.nodes = { a, b, c, d }; sp.closed = true;
	p->SetPaths(std::vector<SubPath>{ sp });
	Style st; st.fill.type = PaintType::Solid; st.fill.color = fill; st.stroke.width = 0.0f;
	p->SetStyle(st);
	p->SetTransform(Matrix2D::Translation(x, y));
	return p;
}

std::unique_ptr<PathNode> MakeLine(float x1, float y1, float x2, float y2)
{
	auto p = std::make_unique<PathNode>();
	BezierNode a, b;
	a.anchor = Vec2{ x1, y1 }; b.anchor = Vec2{ x2, y2 };
	SubPath sp; sp.nodes = { a, b }; sp.closed = false;
	p->SetPaths(std::vector<SubPath>{ sp });
	Style st; st.fill.type = PaintType::None; st.stroke.width = 2.0f;
	p->SetStyle(st);
	return p;
}

// 윤곽선만 있는 닫힌 사각 경로 (채움 없음, 선 2px)
std::unique_ptr<PathNode> MakeBoxOutline(float x, float y, float w, float h)
{
	auto p = std::make_unique<PathNode>();
	BezierNode a, b, c, d;
	a.anchor = Vec2{ 0, 0 }; b.anchor = Vec2{ w, 0 }; c.anchor = Vec2{ w, h }; d.anchor = Vec2{ 0, h };
	SubPath sp; sp.nodes = { a, b, c, d }; sp.closed = true;
	p->SetPaths(std::vector<SubPath>{ sp });
	Style st; st.fill.type = PaintType::None; st.stroke.width = 2.0f;
	p->SetStyle(st);
	p->SetTransform(Matrix2D::Translation(x, y));
	return p;
}

} // namespace

bool RunRuntimeVerification(std::wstring* failure)
{
#define RT_CHECK(cond, text) do { if (!(cond)) { if (failure) *failure = (text); CoUninitialize(); return false; } } while (0)
	const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) { if (failure) *failure = L"COM 초기화 실패"; return false; }

	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path();
	const std::wstring png = (dir / L"m2_verify_export.png").wstring();
	const std::wstring jpg = (dir / L"m2_verify_export.jpg").wstring();

	// ---- 1. 래스터 출력 (5.5): 100x50 문서에 빨간 사각형(0,0,50x50), 2배 배율 ----
	{
		Document d;
		d.width = 100.0f; d.height = 50.0f;
		d.AddShape(MakeRect(0.0f, 0.0f, 50.0f, 50.0f, Color(1.0f, 0.0f, 0.0f, 1.0f)));
		std::wstring err;
		RasterExportOptions o;
		o.scale = 2.0f; o.dpi = 144.0f; o.transparent = true;
		RT_CHECK(ExportRaster(d, png, o, nullptr, &err), L"PNG 내보내기 실패: " + err);
		unsigned w = 0, h = 0; Px p{};
		RT_CHECK(ReadPixel(png, w, h, 50, 50, p), L"PNG 읽기 실패");
		RT_CHECK(w == 200 && h == 100, L"PNG 크기가 배율과 다름 (200x100 이어야 함)");
		RT_CHECK(p.r > 240 && p.g < 15 && p.b < 15 && p.a == 255, L"PNG 빨간 사각형 픽셀 색이 틀림");
		RT_CHECK(ReadPixel(png, w, h, 150, 50, p) && p.a == 0, L"PNG 투명 배경이 투명하지 않음");

		o.jpeg = true; o.dpi = 96.0f; o.scale = 1.0f;
		RT_CHECK(ExportRaster(d, jpg, o, nullptr, &err), L"JPG 내보내기 실패: " + err);
		RT_CHECK(ReadPixel(jpg, w, h, 75, 25, p), L"JPG 읽기 실패");
		RT_CHECK(w == 100 && h == 50, L"JPG 크기가 틀림");
		RT_CHECK(p.r > 240 && p.g > 240 && p.b > 240 && p.a == 255, L"JPG 배경이 불투명 흰색이 아님");
		RT_CHECK(ReadPixel(jpg, w, h, 25, 25, p) && p.r > 220 && p.g < 40 && p.b < 40, L"JPG 빨간 사각형 색이 틀림");

		// 영역 지정(선택 영역): (0,0)~(50,50) 만 -> 50x50, 전부 빨강
		Rect2 region{ 0.0f, 0.0f, 50.0f, 50.0f };
		o.jpeg = false;
		RT_CHECK(ExportRaster(d, png, o, &region, &err), L"영역 PNG 내보내기 실패: " + err);
		RT_CHECK(ReadPixel(png, w, h, 49, 49, p) && w == 50 && h == 50 && p.r > 240, L"영역 내보내기 크기/색이 틀림");

		// 너무 큰 출력은 거부
		o.scale = 32.0f;
		Document big; big.width = 4000.0f; big.height = 4000.0f;
		RT_CHECK(!ExportRaster(big, png, o, nullptr, &err), L"과대 출력이 거부되지 않음");
	}

	// ---- 2. 클립보드 SVG 직렬화 왕복 (5.4): 부모 프레임 안의 노드가 월드 위치로 구워짐 ----
	{
		Document d;
		auto fp = std::make_unique<FrameNode>();
		fp->SetFrameSize(200.0f, 100.0f);
		fp->SetTransform(Matrix2D::Translation(100.0f, 50.0f));
		FrameNode* fr = static_cast<FrameNode*>(d.AddShape(std::move(fp)));
		Node* child = fr->AddChild(MakeRect(10.0f, 20.0f, 30.0f, 40.0f, Color(0.0f, 0.0f, 1.0f, 1.0f)));
		d.AssignIds(*fr);
		const std::string svg = SerializeNodesToSvg(d, { child, fr });   // 조상(fr)이 함께 선택됐으므로 child 는 중복 제외
		RT_CHECK(!svg.empty() && LooksLikeSvg(svg), L"클립보드 SVG 직렬화 실패");
		Document tmp;
		std::vector<const Node*> src;
		RT_CHECK(ParseSvgNodes(svg, tmp, src) && src.size() == 1, L"클립보드 SVG 파싱 실패 (조상과 함께 선택된 노드는 한 번만)");
		Document dst;
		std::vector<Node*> imp = dst.ImportNodes(src);
		RT_CHECK(imp.size() == 1 && dynamic_cast<FrameNode*>(imp[0]) != nullptr, L"붙여넣기 결과가 프레임이 아님");
		RT_CHECK(std::fabs(imp[0]->GetWorldTransform().dx - 100.0f) < 0.01f, L"프레임 월드 위치가 보존되지 않음");

		// 자식만 선택하면 월드 변환으로 구워져 (110,70) 에 놓임
		const std::string svg2 = SerializeNodesToSvg(d, { child });
		Document tmp2; std::vector<const Node*> src2;
		RT_CHECK(ParseSvgNodes(svg2, tmp2, src2) && src2.size() == 1, L"자식 단독 복사 파싱 실패");
		Rect2 lb, wb;
		const_cast<Node*>(src2[0])->GetLocalBounds(lb);
		wb = src2[0]->GetWorldTransform().TransformRect(lb);
		RT_CHECK(std::fabs(wb.left - 110.0f) < 0.1f && std::fabs(wb.top - 70.0f) < 0.1f, L"자식 단독 복사가 월드 위치로 구워지지 않음");

		// SVG 가 아닌 텍스트는 거부
		Document tmp3; std::vector<const Node*> src3;
		RT_CHECK(!ParseSvgNodes("hello world", tmp3, src3), L"SVG 가 아닌 텍스트가 거부되지 않음");
	}

	// ---- 3. 오브젝트 스냅: 선 위(Edge) / 교차점(Intersection) (5.1.0) ----
	{
		Document d;
		d.AddShape(MakeLine(0.0f, 0.0f, 100.0f, 100.0f));    // 대각선
		d.AddShape(MakeLine(0.0f, 100.0f, 100.0f, 0.0f));    // 반대 대각선 (교차점 (50,50))
		GridRenderer grid;
		SnapManager sm;
		sm.SetTargets(SnapTargets::None);

		// 끄면 맞추지 않음
		SnapResult r = sm.FindSnapPoint(D2D1::Point2F(52.0f, 53.0f), 1.0f, grid, &d);
		RT_CHECK(!r.snapped, L"스냅 대상을 모두 껐는데 맞춰짐");

		// Edge: 대각선 위 (30,30) 근처 -> 선 위의 가장 가까운 점
		sm.SetTargets(SnapTargets::Edge);
		r = sm.FindSnapPoint(D2D1::Point2F(33.0f, 28.0f), 1.0f, grid, &d);
		RT_CHECK(r.snapped && r.targetType == SnapTargets::Edge && std::fabs(r.pt.x - 30.5f) < 0.6f && std::fabs(r.pt.y - 30.5f) < 0.6f, L"선 위 스냅이 가장 가까운 점으로 맞춰지지 않음");
		r = sm.FindSnapPoint(D2D1::Point2F(30.0f, 60.0f), 1.0f, grid, &d);
		RT_CHECK(!r.snapped, L"선에서 먼 점이 선 위 스냅됨");

		// Intersection: (50,50) 근처
		sm.SetTargets(SnapTargets::Intersection);
		r = sm.FindSnapPoint(D2D1::Point2F(53.0f, 48.0f), 1.0f, grid, &d);
		RT_CHECK(r.snapped && r.targetType == SnapTargets::Intersection && std::fabs(r.pt.x - 50.0f) < 0.1f && std::fabs(r.pt.y - 50.0f) < 0.1f, L"교차점 스냅이 맞춰지지 않음");
		r = sm.FindSnapPoint(D2D1::Point2F(20.0f, 20.0f), 1.0f, grid, &d);
		RT_CHECK(!r.snapped, L"교차점에서 먼 점이 교차점 스냅됨");

		// 둘 다: 교차점이 선 위보다 우선(교차점 부근)
		sm.SetTargets(SnapTargets::Edge | SnapTargets::Intersection);
		r = sm.FindSnapPoint(D2D1::Point2F(52.0f, 51.0f), 1.0f, grid, &d);
		RT_CHECK(r.snapped && r.targetType == SnapTargets::Intersection, L"교차점 부근에서 교차점이 선 위 스냅보다 우선하지 않음");

		// 마스터 스냅 꺼짐 / 스마트 가이드 토글
		sm.SetEnabled(false);
		RT_CHECK(!sm.IsSmartGuidesEnabled() && !sm.IsEqualSpacingEnabled() && !sm.FindSnapPoint(D2D1::Point2F(50.0f, 50.0f), 1.0f, grid, &d).snapped, L"마스터 스냅이 꺼져도 동작함");
		sm.SetEnabled(true);
		sm.SetSmartGuides(false);
		RT_CHECK(!sm.IsSmartGuidesEnabled() && !sm.IsEqualSpacingEnabled(), L"스마트 가이드를 끄면 같은 간격도 꺼져야 함");
		sm.SetSmartGuides(true); sm.SetEqualSpacing(false);
		RT_CHECK(sm.IsSmartGuidesEnabled() && !sm.IsEqualSpacingEnabled(), L"같은 간격만 끄기가 동작하지 않음");
	}

	// ---- 4. 페인트통: 닫힌 영역 탐지 (5.6) ----
	{
		Document d;
		d.AddShape(MakeBoxOutline(50.0f, 50.0f, 100.0f, 100.0f));   // 상자 (50,50)~(150,150)
		std::vector<Vec2> poly;
		std::wstring err;

		// 상자 안쪽 한 점 -> 상자 안쪽 전체 (대략 98x98)
		RT_CHECK(FindEnclosedRegion(d, Vec2{ 100.0f, 100.0f }, poly, &err), L"닫힌 상자 안쪽 영역을 찾지 못함: " + err);
		const float a1 = PolygonArea(poly);
		RT_CHECK(a1 > 9000.0f && a1 < 10200.0f, L"상자 영역 면적이 틀림 (약 9600 이어야 함)");
		float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
		for (const Vec2& p : poly) { minx = (std::min)(minx, p.x); miny = (std::min)(miny, p.y); maxx = (std::max)(maxx, p.x); maxy = (std::max)(maxy, p.y); }
		RT_CHECK(minx > 49.0f && maxx < 151.0f && miny > 49.0f && maxy < 151.0f && maxx - minx > 95.0f, L"상자 영역 경계가 상자와 맞지 않음");
		RT_CHECK(poly.size() <= 16, L"사각형 윤곽이 충분히 단순화되지 않음");

		// 선 위를 클릭하면 실패, 상자 밖(도형 경계 안쪽 여백)은 열린 영역이라 실패
		RT_CHECK(!FindEnclosedRegion(d, Vec2{ 50.0f, 100.0f }, poly, &err), L"선 위 클릭이 성공함");
		RT_CHECK(!FindEnclosedRegion(d, Vec2{ 45.0f, 45.0f }, poly, &err), L"상자 밖 열린 공간이 영역으로 판정됨");
		RT_CHECK(!FindEnclosedRegion(d, Vec2{ 500.0f, 500.0f }, poly, &err), L"도형에서 멀리 떨어진 점이 영역으로 판정됨");

		// 대각선 두 개로 4등분 -> 위쪽 삼각형 (50,50),(150,50),(100,100): 면적 약 2500
		d.AddShape(MakeLine(50.0f, 50.0f, 150.0f, 150.0f));
		d.AddShape(MakeLine(50.0f, 150.0f, 150.0f, 50.0f));
		RT_CHECK(FindEnclosedRegion(d, Vec2{ 100.0f, 65.0f }, poly, &err), L"대각선으로 나뉜 위쪽 삼각형을 찾지 못함: " + err);
		const float a2 = PolygonArea(poly);
		RT_CHECK(a2 > 1800.0f && a2 < 2700.0f, L"삼각형 영역 면적이 틀림 (약 2300~2500)");
		RT_CHECK(FindEnclosedRegion(d, Vec2{ 65.0f, 100.0f }, poly, &err) && PolygonArea(poly) > 1800.0f && PolygonArea(poly) < 2700.0f, L"왼쪽 삼각형 영역이 틀림");

		// 이동된 도형(변환)도 월드 좌표로 처리
		Document d2;
		auto moved = MakeBoxOutline(0.0f, 0.0f, 80.0f, 60.0f);
		moved->SetTransform(Matrix2D::Translation(300.0f, 200.0f));
		d2.AddShape(std::move(moved));
		RT_CHECK(FindEnclosedRegion(d2, Vec2{ 340.0f, 230.0f }, poly, &err) && PolygonArea(poly) > 4300.0f && PolygonArea(poly) < 4800.0f, L"이동된 상자 영역이 틀림");

		// 열린 U 자 (위쪽이 열림): 안쪽 클릭 -> 열린 영역이라 실패
		Document d3;
		d3.AddShape(MakeLine(0.0f, 0.0f, 0.0f, 100.0f));
		d3.AddShape(MakeLine(0.0f, 100.0f, 100.0f, 100.0f));
		d3.AddShape(MakeLine(100.0f, 100.0f, 100.0f, 0.0f));
		RT_CHECK(!FindEnclosedRegion(d3, Vec2{ 50.0f, 60.0f }, poly, &err), L"열린 U 자 안쪽이 닫힌 영역으로 판정됨");
		// 위를 선으로 막으면 닫힘
		d3.AddShape(MakeLine(0.0f, 0.0f, 100.0f, 0.0f));
		RT_CHECK(FindEnclosedRegion(d3, Vec2{ 50.0f, 60.0f }, poly, &err) && PolygonArea(poly) > 9000.0f, L"선으로 막은 뒤에도 닫힌 영역을 찾지 못함");
	}

	std::error_code ec;
	fs::remove(png, ec); fs::remove(jpg, ec);
	CoUninitialize();
	return true;
#undef RT_CHECK
}

} // namespace ge
