#include "pch.h"
#include "TableComponent.h"
#include <algorithm>

namespace ge {
namespace TableComponent {

namespace {

// 자식 원점의 로컬 좌표 (자식 변환의 이동 성분)
Vec2 Origin(const Node& n) { return Vec2{ n.GetTransform().dx, n.GetTransform().dy }; }

void Shift(Node& n, float dx, float dy)
{
	Matrix2D m = n.GetTransform();
	m.dx += dx;
	m.dy += dy;
	n.SetTransform(m);
}

// 세로 위치 y 이상에 있는 자식을 dy 만큼 이동
void ShiftBelow(TableNode& t, float y, float dy)
{
	for (const auto& ch : t.GetChildren())
		if (Origin(*ch).y >= y) Shift(*ch, 0.0f, dy);
}

void ShiftRight(TableNode& t, float x, float dx)
{
	for (const auto& ch : t.GetChildren())
		if (Origin(*ch).x >= x) Shift(*ch, dx, 0.0f);
}

float RowTop(const TableNode& t, int r) { return t.CellRect(r, 0).top; }
float ColLeft(const TableNode& t, int c) { return t.CellRect(0, c).left; }

// 세로 구간 [y0,y1) 또는 가로 구간 [x0,x1) 안에 있는 자식을 삭제
void RemoveChildrenIn(TableNode& t, bool vertical, float lo, float hi)
{
	std::vector<Node*> kill;
	for (const auto& ch : t.GetChildren()) {
		const Vec2 o = Origin(*ch);
		const float v = vertical ? o.y : o.x;
		if (v >= lo && v < hi) kill.push_back(ch.get());
	}
	for (Node* n : kill) t.RemoveChild(n);
}

} // namespace

std::unique_ptr<TableNode> Create(int rows, int cols, float w, float h)
{
	auto t = std::make_unique<TableNode>();
	rows = (std::max)(1, (std::min)(50, rows));
	cols = (std::max)(1, (std::min)(50, cols));
	t->SetGrid(rows, cols, w / cols, h / rows);
	t->SetHeaderRows(1);
	t->SetName(L"테이블");
	return t;
}

bool InsertRow(TableNode& t, int at)
{
	if (at < 0 || at > t.Rows()) return false;
	const int ref = (std::min)(at, t.Rows() - 1);
	const float h = t.RowHeights()[ref];
	const float y = (at < t.Rows()) ? RowTop(t, at) : -1.0f;
	if (!t.InsertRowAt(at, h)) return false;
	if (y >= 0.0f) ShiftBelow(t, y, h);
	return true;
}

bool InsertCol(TableNode& t, int at)
{
	if (at < 0 || at > t.Cols()) return false;
	const int ref = (std::min)(at, t.Cols() - 1);
	const float w = t.ColWidths()[ref];
	const float x = (at < t.Cols()) ? ColLeft(t, at) : -1.0f;
	if (!t.InsertColAt(at, w)) return false;
	if (x >= 0.0f) ShiftRight(t, x, w);
	return true;
}

bool DeleteRow(TableNode& t, int at)
{
	if (at < 0 || at >= t.Rows() || t.Rows() <= 1) return false;
	const float top = RowTop(t, at), h = t.RowHeights()[at];
	if (!t.RemoveRowAt(at)) return false;
	RemoveChildrenIn(t, true, top, top + h);
	ShiftBelow(t, top + h, -h);
	return true;
}

bool DeleteCol(TableNode& t, int at)
{
	if (at < 0 || at >= t.Cols() || t.Cols() <= 1) return false;
	const float left = ColLeft(t, at), w = t.ColWidths()[at];
	if (!t.RemoveColAt(at)) return false;
	RemoveChildrenIn(t, false, left, left + w);
	ShiftRight(t, left + w, -w);
	return true;
}

bool ResizeRow(TableNode& t, int r, float h)
{
	if (r < 0 || r >= t.Rows()) return false;
	h = (std::max)(8.0f, h);
	const float old = t.RowHeights()[r], bottom = RowTop(t, r) + old;
	if (std::fabs(h - old) < 0.001f) return false;
	t.SetRowHeight(r, h);
	ShiftBelow(t, bottom, h - old);
	return true;
}

bool ResizeCol(TableNode& t, int c, float w)
{
	if (c < 0 || c >= t.Cols()) return false;
	w = (std::max)(8.0f, w);
	const float old = t.ColWidths()[c], right = ColLeft(t, c) + old;
	if (std::fabs(w - old) < 0.001f) return false;
	t.SetColWidth(c, w);
	ShiftRight(t, right, w - old);
	return true;
}

bool SetGridSize(TableNode& t, int rows, int cols)
{
	rows = (std::max)(1, (std::min)(50, rows));
	cols = (std::max)(1, (std::min)(50, cols));
	bool changed = false;
	while (t.Rows() < rows) changed |= InsertRow(t, t.Rows());
	while (t.Rows() > rows) changed |= DeleteRow(t, t.Rows() - 1);
	while (t.Cols() < cols) changed |= InsertCol(t, t.Cols());
	while (t.Cols() > cols) changed |= DeleteCol(t, t.Cols() - 1);
	return changed;
}

bool MergeCells(TableNode& t, int r0, int c0, int r1, int c1) { return t.MergeRange(r0, c0, r1, c1); }
bool SplitCell(TableNode& t, int r, int c) { return t.SplitAt(r, c); }

bool MergeNeighbor(TableNode& t, int r, int c, bool right)
{
	const TableNode::Span a = t.SpanOf(r, c);
	const int nr = right ? a.r : a.r + a.rs;
	const int nc = right ? a.c + a.cs : a.c;
	if (nr >= t.Rows() || nc >= t.Cols()) return false;
	const TableNode::Span b = t.SpanOf(nr, nc);
	// 합친 결과가 직사각형이려면 이웃의 행(열) 범위가 같아야 한다
	if (right && (b.r != a.r || b.rs != a.rs)) return false;
	if (!right && (b.c != a.c || b.cs != a.cs)) return false;
	const int r0 = (std::min)(a.r, b.r), c0 = (std::min)(a.c, b.c);
	const int r1 = (std::max)(a.r + a.rs, b.r + b.rs) - 1, c1 = (std::max)(a.c + a.cs, b.c + b.cs) - 1;
	const std::vector<TableNode::Span> before = t.Merges();
	t.SplitAt(a.r, a.c);
	t.SplitAt(b.r, b.c);
	if (t.MergeRange(r0, c0, r1, c1)) return true;
	t.SetMerges(before);   // 실패하면 원래대로
	return false;
}

bool ChildCell(const TableNode& t, const Node& child, int& r, int& c)
{
	return t.CellAt(Origin(child), r, c);
}

std::vector<TextNode*> CellTexts(const TableNode& t, int r, int c)
{
	std::vector<TextNode*> out;
	for (const auto& ch : t.GetChildren()) {
		if (ch->Kind() != NodeKind::Text) continue;
		int cr, cc;
		if (ChildCell(t, *ch, cr, cc) && cr == r && cc == c) out.push_back(static_cast<TextNode*>(ch.get()));
	}
	return out;
}

} // namespace TableComponent
} // namespace ge
