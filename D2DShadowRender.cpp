#include "pch.h"
#include "D2DGraphicEngine.h"

namespace ge {

namespace {

// 블러 근사: ID2D1RenderTarget 에는 효과(Effect)가 없으므로, 윤곽을 바깥쪽으로 넓히며 겹쳐 그린다 (최대 12겹).
// 반경이 큰 겹일수록 옅게 그려 가장자리가 부드럽게 사라지는 모양을 만든다.
int ShadowLayerCount(float blur)
{
	return (std::max)(0, (std::min)(12, static_cast<int>(std::ceil(blur * 0.5f))));
}

// 노드 윤곽(로컬 좌표). 경로는 자신의 선 굵기와 채움 여부도 돌려준다
ComPtr<ID2D1Geometry> BuildShadowGeometry(ID2D1Factory* factory, Node& node, float& baseStroke, bool& fillShape)
{
	ComPtr<ID2D1Geometry> geo;
	baseStroke = 0.0f;
	fillShape = true;
	if (!factory) return geo;
	if (auto* pn = dynamic_cast<PathNode*>(&node)) {
		geo = PathOp::BuildGeometry(*pn, factory);
		const Style& st = pn->GetStyle();
		baseStroke = (st.stroke.width > 0.0f && st.stroke.color.a > 0.0f) ? st.stroke.width : 0.0f;
		fillShape = st.fill.type != PaintType::None;
	} else if (auto* fr = dynamic_cast<FrameNode*>(&node)) {
		const D2D1_RECT_F rc = D2D1::RectF(0.0f, 0.0f, fr->GetWidth(), fr->GetHeight());
		const CornerRadii cr = fr->ClampedRadii();
		if (cr.IsUniform() && cr.tl > 0.0f) {
			ComPtr<ID2D1RoundedRectangleGeometry> g;
			if (SUCCEEDED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rc, cr.tl, cr.tl), &g))) geo = g;
		} else {
			ComPtr<ID2D1RectangleGeometry> g;
			if (SUCCEEDED(factory->CreateRectangleGeometry(rc, &g))) geo = g;
		}
	} else {
		Rect2 lb;
		if (!node.GetLocalBounds(lb)) return geo;
		ComPtr<ID2D1RectangleGeometry> g;
		if (SUCCEEDED(factory->CreateRectangleGeometry(D2D1::RectF(lb.left, lb.top, lb.right, lb.bottom), &g))) geo = g;
	}
	return geo;
}

// 윤곽을 중심 기준으로 spread 만큼 키우거나(양수) 줄이는(음수) 변환. 양수는 선 굵기로 처리하므로 여기서는 음수만
Matrix2D ShrinkForNegativeSpread(ID2D1Geometry* geo, float spread)
{
	if (spread >= 0.0f) return Matrix2D();
	D2D1_RECT_F b{};
	if (FAILED(geo->GetBounds(nullptr, &b))) return Matrix2D();
	const float w = b.right - b.left, h = b.bottom - b.top;
	if (w < 1e-3f || h < 1e-3f) return Matrix2D();
	const float sx = (std::max)(0.02f, 1.0f - 2.0f * (-spread) / w);
	const float sy = (std::max)(0.02f, 1.0f - 2.0f * (-spread) / h);
	return Matrix2D::Scale(sx, sy, Vec2{ (b.left + b.right) * 0.5f, (b.top + b.bottom) * 0.5f });
}

ID2D1StrokeStyle* RoundStrokeStyle(ID2D1Factory* factory)
{
	static ComPtr<ID2D1StrokeStyle> s_round;
	static ID2D1Factory* s_factory = nullptr;
	if (!s_round || s_factory != factory) {
		s_round.Reset();
		D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
		factory->CreateStrokeStyle(sp, nullptr, 0, &s_round);
		s_factory = factory;
	}
	return s_round.Get();
}

} // namespace

void D2DRenderer::DrawNodeShadow(Node& node, const Matrix2D& worldXf, const Viewport& view)
{
	const Shadow& sh = node.GetShadow();
	if (!sh.enabled || sh.inset || sh.color.a <= 0.001f || !m_rt || !m_brush || !m_factory) return;
	if (node.Kind() == NodeKind::Text || node.Kind() == NodeKind::Layer) return;   // 글자는 DrawTextShadow, 레이어는 그림자 없음

	float baseStroke = 0.0f;
	bool fillShape = true;
	ComPtr<ID2D1Geometry> geo = BuildShadowGeometry(m_factory, node, baseStroke, fillShape);
	if (!geo) return;
	ID2D1StrokeStyle* round = RoundStrokeStyle(m_factory);

	// 음수 spread: 윤곽 축소. 양수 spread: 선 굵기로 확장
	const Matrix2D shrink = ShrinkForNegativeSpread(geo.Get(), sh.spread);
	const float spread = (std::max)(0.0f, sh.spread);

	// 그림자 위치: 월드 기준으로 (x, y) 만큼 이동 (노드가 회전해도 방향은 유지)
	const Matrix2D xf = shrink * worldXf * Matrix2D::Translation(sh.x, sh.y);
	m_rt->SetTransform((xf * view.ToMatrix()).ToD2D());

	const int N = ShadowLayerCount(sh.blur);
	const float R = sh.blur * 0.5f;
	const float a = sh.color.a;

	// 바깥쪽 겹부터 (옅은 것 -> 짙은 것)
	for (int i = N; i >= 1; --i) {
		const float t = static_cast<float>(i) / static_cast<float>(N);
		const float radius = spread + R * t;
		const float alpha = a * (std::min)(1.0f, 1.6f / static_cast<float>(N)) * (1.0f - 0.5f * t);
		m_brush->SetColor(D2D1::ColorF(sh.color.r, sh.color.g, sh.color.b, alpha));
		m_rt->DrawGeometry(geo.Get(), m_brush.Get(), baseStroke + 2.0f * radius, round);
	}

	// 본체 그림자 (채움 + 선 + 스프레드)
	m_brush->SetColor(D2D1::ColorF(sh.color.r, sh.color.g, sh.color.b, a * (N > 0 ? 0.7f : 1.0f)));
	if (fillShape) m_rt->FillGeometry(geo.Get(), m_brush.Get());
	if (baseStroke > 0.0f || spread > 0.0f) m_rt->DrawGeometry(geo.Get(), m_brush.Get(), baseStroke + 2.0f * spread, round);

	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

// 안쪽 그림자: 노드 윤곽 안으로 클립한 뒤, 윤곽을 (x,y) 만큼 옮긴 "구멍" 바깥을 그림자 색으로 채우고 구멍 경계를 흐리게 겹친다.
// spread 양수 = 그림자가 안쪽으로 더 퍼짐(구멍 축소), 음수 = 구멍 확대
void D2DRenderer::DrawNodeInsetShadow(Node& node, const Matrix2D& worldXf, const Viewport& view)
{
	const Shadow& sh = node.GetShadow();
	if (!sh.enabled || !sh.inset || sh.color.a <= 0.001f || !m_rt || !m_brush || !m_factory) return;
	if (node.Kind() == NodeKind::Text || node.Kind() == NodeKind::Layer) return;

	float baseStroke = 0.0f;
	bool fillShape = true;
	ComPtr<ID2D1Geometry> geo = BuildShadowGeometry(m_factory, node, baseStroke, fillShape);
	if (!geo) return;
	ID2D1StrokeStyle* round = RoundStrokeStyle(m_factory);

	const Matrix2D viewM = view.ToMatrix();
	const D2D1_MATRIX_3X2_F clipM = (worldXf * viewM).ToD2D();

	// 화면 좌표의 클립(노드 윤곽)과 구멍(이동 + 축소/확대된 윤곽)
	ComPtr<ID2D1TransformedGeometry> clipGeo, holeGeo;
	if (FAILED(m_factory->CreateTransformedGeometry(geo.Get(), &clipM, &clipGeo))) return;
	// spread 양수 = 구멍을 줄임 -> ShrinkForNegativeSpread 에 부호를 뒤집어 전달
	const Matrix2D holeShrink = ShrinkForNegativeSpread(geo.Get(), -sh.spread);
	const D2D1_MATRIX_3X2_F holeM = (holeShrink * worldXf * Matrix2D::Translation(sh.x, sh.y) * viewM).ToD2D();
	if (FAILED(m_factory->CreateTransformedGeometry(geo.Get(), &holeM, &holeGeo))) return;

	// 큰 사각형 - 구멍 = 그림자 영역
	D2D1_RECT_F cb{};
	if (FAILED(clipGeo->GetBounds(nullptr, &cb))) return;
	const float margin = (std::fabs(sh.x) + std::fabs(sh.y) + sh.blur + std::fabs(sh.spread) + 20.0f) * (std::max)(view.zoom, 0.01f);
	ComPtr<ID2D1RectangleGeometry> bigRect;
	if (FAILED(m_factory->CreateRectangleGeometry(D2D1::RectF(cb.left - margin, cb.top - margin, cb.right + margin, cb.bottom + margin), &bigRect))) return;
	ComPtr<ID2D1PathGeometry> ring;
	ComPtr<ID2D1GeometrySink> sink;
	if (FAILED(m_factory->CreatePathGeometry(&ring)) || FAILED(ring->Open(&sink))) return;
	const HRESULT hr = bigRect->CombineWithGeometry(holeGeo.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
	sink->Close();
	if (FAILED(hr)) return;

	D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters();
	lp.geometricMask = clipGeo.Get();
	lp.contentBounds = D2D1::InfiniteRect();
	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
	m_rt->PushLayer(lp, nullptr);

	// 구멍 경계를 안쪽으로 흐리게 (경계에서 멀어질수록 옅게). 선 굵기는 화면 픽셀 단위
	const int N = ShadowLayerCount(sh.blur);
	const float R = sh.blur * 0.5f * (std::max)(view.zoom, 0.01f);
	const float a = sh.color.a;
	for (int i = N; i >= 1; --i) {
		const float t = static_cast<float>(i) / static_cast<float>(N);
		const float alpha = a * (std::min)(1.0f, 1.6f / static_cast<float>(N)) * (1.0f - 0.5f * t);
		m_brush->SetColor(D2D1::ColorF(sh.color.r, sh.color.g, sh.color.b, alpha));
		m_rt->DrawGeometry(holeGeo.Get(), m_brush.Get(), 2.0f * R * t, round);
	}
	m_brush->SetColor(D2D1::ColorF(sh.color.r, sh.color.g, sh.color.b, a * (N > 0 ? 0.7f : 1.0f)));
	m_rt->FillGeometry(ring.Get(), m_brush.Get());

	m_rt->PopLayer();
}

void D2DRenderer::DrawTextShadow(IDWriteTextLayout* layout, float x0, const Shadow& sh, const Matrix2D& worldXf, const Viewport& view)
{
	if (!layout || !sh.enabled || sh.color.a <= 0.001f || !m_rt || !m_brush) return;

	// 글자 모양 그대로: 흐림 반경 안에서 여러 방향으로 어긋나게 겹쳐 그린다 (중심 + 8방향 x 최대 3겹)
	const float R = sh.blur * 0.5f;
	const int rings = (std::max)(0, (std::min)(3, static_cast<int>(std::ceil(R / 2.0f))));
	const Matrix2D base = worldXf * Matrix2D::Translation(sh.x, sh.y);
	m_rt->SetTransform((base * view.ToMatrix()).ToD2D());

	static const float kDirs[8][2] = { {1,0}, {0.7071f,0.7071f}, {0,1}, {-0.7071f,0.7071f}, {-1,0}, {-0.7071f,-0.7071f}, {0,-1}, {0.7071f,-0.7071f} };
	const float ringAlpha = sh.color.a * (rings > 0 ? 0.55f / static_cast<float>(rings) : 0.0f);
	for (int r = rings; r >= 1; --r) {
		const float dist = R * static_cast<float>(r) / static_cast<float>(rings);
		m_brush->SetColor(D2D1::ColorF(sh.color.r, sh.color.g, sh.color.b, ringAlpha));
		for (const auto& d : kDirs)
			m_rt->DrawTextLayout(D2D1::Point2F(x0 + d[0] * dist, d[1] * dist), layout, m_brush.Get());
	}
	m_brush->SetColor(D2D1::ColorF(sh.color.r, sh.color.g, sh.color.b, sh.color.a));
	m_rt->DrawTextLayout(D2D1::Point2F(x0, 0.0f), layout, m_brush.Get());
	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

} // namespace ge
