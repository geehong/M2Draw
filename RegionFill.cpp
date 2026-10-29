#include "pch.h"
#include "RegionFill.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace ge {

float PolygonArea(const std::vector<Vec2>& poly)
{
	double a = 0.0;
	for (size_t i = 0; i < poly.size(); ++i) {
		const Vec2& p = poly[i];
		const Vec2& q = poly[(i + 1) % poly.size()];
		a += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
	}
	return static_cast<float>(std::fabs(a) * 0.5);
}

namespace {

// 윤곽 후보: 보이는 PathNode / FrameNode (재귀)
void CollectOutlineNodes(const GroupNode& g, std::vector<Node*>& out)
{
	for (const auto& c : g.GetChildren()) {
		Node* n = c.get();
		if (!n || !n->IsVisible()) continue;
		if (n->Kind() == NodeKind::Path || dynamic_cast<FrameNode*>(n)) out.push_back(n);
		if (n->Kind() == NodeKind::Group && !dynamic_cast<TableNode*>(n))
			CollectOutlineNodes(*static_cast<GroupNode*>(n), out);
	}
}

// Ramer-Douglas-Peucker (열린 구간)
void Rdp(const std::vector<Vec2>& pts, size_t a, size_t b, float eps, std::vector<bool>& keep)
{
	if (b <= a + 1) return;
	const Vec2 A = pts[a], B = pts[b];
	const float dx = B.x - A.x, dy = B.y - A.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	float best = -1.0f;
	size_t idx = a;
	for (size_t i = a + 1; i < b; ++i) {
		float d;
		if (len < 1e-6f) d = std::hypot(pts[i].x - A.x, pts[i].y - A.y);
		else d = std::fabs((pts[i].x - A.x) * dy - (pts[i].y - A.y) * dx) / len;
		if (d > best) { best = d; idx = i; }
	}
	if (best > eps) {
		keep[idx] = true;
		Rdp(pts, a, idx, eps, keep);
		Rdp(pts, idx, b, eps, keep);
	}
}

std::vector<Vec2> SimplifyClosed(const std::vector<Vec2>& pts, float eps)
{
	const size_t n = pts.size();
	if (n < 4) return pts;
	// 가장 먼 두 점(첫 점과 가장 먼 점)을 기준으로 둘로 나눠 각각 RDP
	size_t farIdx = 0;
	float bestD = -1.0f;
	for (size_t i = 1; i < n; ++i) {
		const float d = std::hypot(pts[i].x - pts[0].x, pts[i].y - pts[0].y);
		if (d > bestD) { bestD = d; farIdx = i; }
	}
	std::vector<bool> keep(n, false);
	keep[0] = true;
	keep[farIdx] = true;
	Rdp(pts, 0, farIdx, eps, keep);
	std::vector<Vec2> second(pts.begin() + farIdx, pts.end());
	second.push_back(pts[0]);
	std::vector<bool> keep2(second.size(), false);
	keep2.front() = keep2.back() = true;
	Rdp(second, 0, second.size() - 1, eps, keep2);
	std::vector<Vec2> out;
	for (size_t i = 0; i <= farIdx; ++i) if (keep[i]) out.push_back(pts[i]);
	for (size_t i = 1; i + 1 < second.size(); ++i) if (keep2[i]) out.push_back(second[i]);
	return out;
}

} // namespace

bool FindEnclosedRegion(Document& doc, Vec2 click, std::vector<Vec2>& polygon, std::wstring* err, const RegionFillOptions& opt)
{
	polygon.clear();
	auto fail = [&](const wchar_t* m) { if (err) *err = m; return false; };

	std::vector<Node*> nodes;
	for (const auto& layer : doc.GetLayers())
		if (layer && layer->IsVisible()) CollectOutlineNodes(*layer, nodes);
	if (nodes.empty()) return fail(L"윤곽선으로 쓸 도형이 없습니다.");

	// 래스터 범위: 모든 윤곽의 월드 경계 + 클릭 점 (+여백)
	bool any = false;
	Rect2 bounds{};
	for (Node* n : nodes) {
		Rect2 lb;
		if (!n->GetLocalBounds(lb)) continue;
		const Rect2 wb = n->GetWorldTransform().TransformRect(lb);
		if (!any) { bounds = wb; any = true; } else bounds.Union(wb);
	}
	if (!any) return fail(L"윤곽선으로 쓸 도형의 경계를 구할 수 없습니다.");
	const float margin = 8.0f;
	bounds.left -= margin; bounds.top -= margin; bounds.right += margin; bounds.bottom += margin;
	if (click.x < bounds.left || click.x > bounds.right || click.y < bounds.top || click.y > bounds.bottom)
		return fail(L"클릭한 곳은 어떤 도형의 영역 안도 아닙니다 (열린 공간).");

	const float bw = bounds.right - bounds.left, bh = bounds.bottom - bounds.top;
	float s = opt.maxPixelsPerUnit;
	s = (std::min)(s, static_cast<float>(opt.maxSide) / (std::max)(bw, bh));
	s = (std::max)(s, 0.05f);
	const int W = (std::max)(2, static_cast<int>(std::ceil(bw * s)));
	const int H = (std::max)(2, static_cast<int>(std::ceil(bh * s)));
	if (W > opt.maxSide + 2 || H > opt.maxSide + 2) return fail(L"영역이 너무 커서 탐색할 수 없습니다.");

	ComPtr<IWICImagingFactory> wic;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return fail(L"WIC 초기화 실패");
	ComPtr<ID2D1Factory> d2d;
	if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return fail(L"Direct2D 초기화 실패");
	ComPtr<IWICBitmap> bmp;
	if (FAILED(wic->CreateBitmap(W, H, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) return fail(L"비트맵 생성 실패");
	ComPtr<ID2D1RenderTarget> rt;
	const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
	if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return fail(L"렌더 타깃 생성 실패");
	ComPtr<ID2D1SolidColorBrush> brush;
	rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush);

	// 윤곽선 그리기: 월드 -> 픽셀 변환을 지오메트리에 적용한 뒤 픽셀 단위 굵기로 그림
	const Matrix2D toPx = Matrix2D::Scale(s, s) * Matrix2D::Translation(-bounds.left * s, -bounds.top * s);
	rt->BeginDraw();
	rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
	for (Node* n : nodes) {
		ComPtr<ID2D1Geometry> geo;
		float strokeWorld = 0.0f;
		if (auto* pn = dynamic_cast<PathNode*>(n)) {
			geo = PathOp::BuildGeometry(*pn, d2d.Get());
			const Style& st = pn->GetStyle();
			if (st.stroke.width > 0.0f && st.stroke.color.a > 0.0f) strokeWorld = st.stroke.width;
		} else if (auto* fr = dynamic_cast<FrameNode*>(n)) {
			ComPtr<ID2D1RectangleGeometry> g;
			if (SUCCEEDED(d2d->CreateRectangleGeometry(D2D1::RectF(0.0f, 0.0f, fr->GetWidth(), fr->GetHeight()), &g))) geo = g;
			const Style& st = fr->GetStyle();
			if (st.stroke.width > 0.0f && st.stroke.color.a > 0.0f) strokeWorld = st.stroke.width;
		}
		if (!geo) continue;
		const D2D1_MATRIX_3X2_F m = (n->GetWorldTransform() * toPx).ToD2D();
		ComPtr<ID2D1TransformedGeometry> xg;
		if (FAILED(d2d->CreateTransformedGeometry(geo.Get(), &m, &xg))) continue;
		rt->DrawGeometry(xg.Get(), brush.Get(), (std::max)(opt.lineWidthPx, strokeWorld * s));
	}
	const HRESULT hr = rt->EndDraw();
	rt.Reset();
	if (FAILED(hr)) return fail(L"윤곽 그리기 실패");

	// 픽셀 읽기: 알파가 일정 이상이면 선(벽)
	const UINT stride = static_cast<UINT>(W) * 4;
	std::vector<unsigned char> buf(static_cast<size_t>(stride) * H);
	if (FAILED(bmp->CopyPixels(nullptr, stride, static_cast<UINT>(buf.size()), buf.data()))) return fail(L"픽셀 읽기 실패");
	auto wall = [&](int x, int y) { return buf[static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4 + 3] >= 64; };

	int sx = static_cast<int>((click.x - bounds.left) * s), sy = static_cast<int>((click.y - bounds.top) * s);
	sx = (std::max)(0, (std::min)(W - 1, sx));
	sy = (std::max)(0, (std::min)(H - 1, sy));
	if (wall(sx, sy)) return fail(L"선 위를 클릭했습니다. 선으로 둘러싸인 빈 공간을 클릭하세요.");

	// 플러드 필 (4방향). 비트맵 가장자리에 닿으면 열린 영역
	std::vector<unsigned char> mask(static_cast<size_t>(W) * H, 0);
	std::queue<std::pair<int, int>> q;
	q.push({ sx, sy });
	mask[static_cast<size_t>(sy) * W + sx] = 1;
	bool leaked = false;
	size_t filled = 0;
	while (!q.empty()) {
		const auto [x, y] = q.front();
		q.pop();
		++filled;
		if (x == 0 || y == 0 || x == W - 1 || y == H - 1) { leaked = true; break; }
		static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; ++k) {
			const int nx = x + dx[k], ny = y + dy[k];
			const size_t idx = static_cast<size_t>(ny) * W + nx;
			if (mask[idx] || wall(nx, ny)) continue;
			mask[idx] = 1;
			q.push({ nx, ny });
		}
	}
	if (leaked) return fail(L"닫힌 영역이 아닙니다. 선이 끊겨 있거나 영역이 열려 있습니다.");

	// 윤곽 추적: 채워진 픽셀의 경계 변을 방향(시계)대로 모아 하나로 이어 붙임
	auto key = [&](int x, int y) { return static_cast<long long>(y) * (W + 1) + x; };
	std::unordered_map<long long, std::vector<long long>> next;   // 시작 코너 -> 도착 코너 목록
	auto filledAt = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && mask[static_cast<size_t>(y) * W + x]; };
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) {
			if (!mask[static_cast<size_t>(y) * W + x]) continue;
			if (!filledAt(x, y - 1)) next[key(x, y)].push_back(key(x + 1, y));            // 위쪽 변
			if (!filledAt(x + 1, y)) next[key(x + 1, y)].push_back(key(x + 1, y + 1));    // 오른쪽 변
			if (!filledAt(x, y + 1)) next[key(x + 1, y + 1)].push_back(key(x, y + 1));    // 아래쪽 변
			if (!filledAt(x - 1, y)) next[key(x, y + 1)].push_back(key(x, y));            // 왼쪽 변
		}
	std::vector<std::vector<Vec2>> loops;
	for (auto& kv : next) {
		while (!kv.second.empty()) {
			std::vector<Vec2> loop;
			long long cur = kv.first;
			const long long startKey = cur;
			for (;;) {
				loop.push_back(Vec2{ static_cast<float>(cur % (W + 1)), static_cast<float>(cur / (W + 1)) });
				auto it = next.find(cur);
				if (it == next.end() || it->second.empty()) break;
				const long long nx = it->second.back();
				it->second.pop_back();
				cur = nx;
				if (cur == startKey) break;
				if (loop.size() > static_cast<size_t>(W + 1) * (H + 1)) break;   // 안전장치
			}
			if (loop.size() >= 4) loops.push_back(std::move(loop));
		}
	}
	if (loops.empty()) return fail(L"영역의 윤곽을 만들 수 없습니다.");
	// 가장 큰 고리 = 바깥 윤곽 (안쪽 섬/구멍은 무시)
	size_t bestLoop = 0;
	float bestArea = -1.0f;
	for (size_t i = 0; i < loops.size(); ++i) {
		const float a = PolygonArea(loops[i]);
		if (a > bestArea) { bestArea = a; bestLoop = i; }
	}
	std::vector<Vec2> simp = SimplifyClosed(loops[bestLoop], opt.simplifyPx);
	if (simp.size() < 3) return fail(L"영역이 너무 작습니다.");

	polygon.reserve(simp.size());
	for (const Vec2& p : simp) polygon.push_back(Vec2{ p.x / s + bounds.left, p.y / s + bounds.top });
	return true;
}

} // namespace ge
