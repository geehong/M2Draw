#include "pch.h"
#include "D2DGraphicEngine.h"

namespace ge {

// 테이블 격자선 그리기 (Step 4.5.7.1). 엔진 본체에 넣지 않고 테이블 전용 파일에 둔다.
// 칸 내용(자식 노드)은 DrawGroup 이 따로 그린다.
void D2DRenderer::DrawTableGrid(TableNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	if (!m_rt || !m_brush) return;
	Rect2 b;
	if (!node.GetLocalBounds(b)) return;

	const Color c = node.GetBorderColor();
	const float bw = (std::max)(node.GetBorderWidth(), 0.5f);
	m_rt->SetTransform((worldXf * view.ToMatrix()).ToD2D());
	m_brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));

	// 머리글 행 배경 (4.5.7.2)
	if (node.GetHeaderRows() > 0) {
		const int hr = (std::min)(node.GetHeaderRows(), node.Rows());
		const Rect2 last = node.CellRect(hr - 1, 0);
		m_brush->SetColor(D2D1::ColorF(0.93f, 0.94f, 0.96f, 1.0f));
		m_rt->FillRectangle(D2D1::RectF(b.left, b.top, b.right, last.bottom), m_brush.Get());
		m_brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a));
	}

	// 테두리 종류: 파선 {4,2}, 점선 {1,1} (선 굵기의 배수)
	ComPtr<ID2D1StrokeStyle> dashStyle;
	if (node.GetBorderStyle() != 0 && m_factory) {
		const float dashes[2] = { node.GetBorderStyle() == 1 ? 4.0f : 1.0f, node.GetBorderStyle() == 1 ? 2.0f : 1.0f };
		D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);
		m_factory->CreateStrokeStyle(sp, dashes, 2, &dashStyle);
	}
	ID2D1StrokeStyle* ss = dashStyle.Get();
	m_rt->DrawRectangle(D2D1::RectF(b.left, b.top, b.right, b.bottom), m_brush.Get(), bw, ss);

	// 안쪽 격자선: 병합된 칸 내부는 그리지 않음
	std::vector<TableNode::Seg> segs;
	node.GridSegments(segs);
	for (const auto& s : segs)
		m_rt->DrawLine(D2D1::Point2F(s.x0, s.y0), D2D1::Point2F(s.x1, s.y1), m_brush.Get(), bw, ss);

	// 편집 중인 칸 강조
	int ar, ac;
	if (node.GetActiveCell(ar, ac)) {
		const Rect2 cr = node.SpanRect(ar, ac);
		m_brush->SetColor(D2D1::ColorF(0.12f, 0.56f, 1.0f, 1.0f));
		m_rt->DrawRectangle(D2D1::RectF(cr.left + 1.0f, cr.top + 1.0f, cr.right - 1.0f, cr.bottom - 1.0f), m_brush.Get(), 2.0f);
	}

	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

} // namespace ge
