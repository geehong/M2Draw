#include "pch.h"
#include "SmartGuides.h"
#include <algorithm>
#include <cmath>

namespace ge {

bool GuideWorldBounds(const Node& n, Rect2& out)
{
	Rect2 lb;
	if (!const_cast<Node&>(n).GetLocalBounds(lb)) return false;
	out = n.GetWorldTransform().TransformRect(lb);
	return true;
}

namespace {

// 그룹 아래의 모든 보이는 노드 상자를 모은다 (계층 무관). exclude 와 그 후손은 건너뜀
void CollectRects(const GroupNode& g, const std::vector<Node*>& exclude, std::vector<Rect2>& out)
{
	for (const auto& c : g.GetChildren()) {
		Node* n = c.get();
		if (!n || !n->IsVisible()) continue;
		if (std::find(exclude.begin(), exclude.end(), n) != exclude.end()) continue;
		if (out.size() >= 800) return;   // 성능 상한
		Rect2 b;
		if (GuideWorldBounds(*n, b)) out.push_back(b);
		if (dynamic_cast<TableNode*>(n)) continue;   // 표 칸 내용은 후보로 쓰지 않음
		if (auto* gc = dynamic_cast<GroupNode*>(n)) CollectRects(*gc, exclude, out);
	}
}

} // namespace

GuideTargets CollectGuideTargets(const std::vector<Node*>& moving)
{
	GuideTargets t;
	if (moving.empty() || !moving.front()) return t;
	GroupNode* parent = moving.front()->GetParent();
	if (!parent) return t;

	if (auto* fr = dynamic_cast<FrameNode*>(parent)) {
		t.parent = fr->GetWorldTransform().TransformRect(Rect2{ 0.0f, 0.0f, fr->GetWidth(), fr->GetHeight() });
		t.hasParent = true;
	}
	GroupNode* root = parent;
	while (root && root->Kind() != NodeKind::Layer && root->GetParent()) root = root->GetParent();
	CollectRects(*root, moving, t.rects);
	return t;
}

GuideTargets CollectGuideTargetsAt(const Document& doc, Vec2 worldPt)
{
	GuideTargets t;
	if (FrameNode* fr = doc.FindFrameAt(worldPt)) {
		t.parent = fr->GetWorldTransform().TransformRect(Rect2{ 0.0f, 0.0f, fr->GetWidth(), fr->GetHeight() });
		t.hasParent = true;
	}
	if (LayerNode* layer = doc.GetActiveLayer()) CollectRects(*layer, {}, t.rects);
	return t;
}

namespace {

struct Cand { float v; Rect2 r; };

void AddUnique(std::vector<float>& v, float x)
{
	for (float e : v) if (std::fabs(e - x) < 0.01f) return;
	v.push_back(x);
}

GuideLine* FindLine(std::vector<GuideLine>& lines, bool vertical, float pos)
{
	for (auto& l : lines) if (l.vertical == vertical && std::fabs(l.pos - pos) < 0.01f) return &l;
	return nullptr;
}

// 세로선(vertical): 축 방향 범위는 y. 두 상자의 y 범위를 합쳐 선을 만들고 표시점은 각 상자의 위/가운데/아래
void AddLine(std::vector<GuideLine>& lines, bool vertical, float pos, const Rect2& a, const Rect2& b)
{
	const float a0 = vertical ? a.top : a.left, a1 = vertical ? a.bottom : a.right;
	const float b0 = vertical ? b.top : b.left, b1 = vertical ? b.bottom : b.right;
	GuideLine* l = FindLine(lines, vertical, pos);
	if (!l) {
		lines.push_back(GuideLine{});
		l = &lines.back();
		l->vertical = vertical;
		l->pos = pos;
		l->from = (std::min)(a0, b0);
		l->to = (std::max)(a1, b1);
	} else {
		l->from = (std::min)(l->from, (std::min)(a0, b0));
		l->to = (std::max)(l->to, (std::max)(a1, b1));
	}
	for (float m : { a0, (a0 + a1) * 0.5f, a1, b0, (b0 + b1) * 0.5f, b1 }) AddUnique(l->marks, m);
}

} // namespace

namespace {

// 같은 간격 맞춤 (5.1.3) 한 축. horizontal=true: x 축으로 나란히 놓인 요소들 사이의 간격.
// 같은 줄(수직 방향으로 겹치는)에 있고 이동 요소와 주축에서 겹치지 않는 요소만 대상으로 한다.
// 후보: (1) 이웃 간격과 같은 간격으로 이동 요소를 오른쪽/아래에 붙임 (2) 왼쪽/위에 붙임 (3) 두 요소 사이에서 양쪽 간격이 같게 가운데 놓음
bool EqualSpacingAxis(const Rect2& m, const std::vector<Rect2>& others, bool horizontal, float tol, float& delta, std::vector<GapMark>& marks)
{
	auto lo = [&](const Rect2& r) { return horizontal ? r.left : r.top; };
	auto hi = [&](const Rect2& r) { return horizontal ? r.right : r.bottom; };
	auto plo = [&](const Rect2& r) { return horizontal ? r.top : r.left; };
	auto phi = [&](const Rect2& r) { return horizontal ? r.bottom : r.right; };

	std::vector<Rect2> items;
	for (const Rect2& r : others) {
		const float overlapPerp = (std::min)(phi(r), phi(m)) - (std::max)(plo(r), plo(m));
		if (overlapPerp <= 0.5f) continue;                                      // 같은 줄이 아님
		if ((std::min)(hi(r), hi(m)) - (std::max)(lo(r), lo(m)) > 0.5f) continue;   // 주축에서 겹침 (이웃이 아님, 부모 프레임 등)
		items.push_back(r);
	}
	if (items.empty()) return false;
	std::sort(items.begin(), items.end(), [&](const Rect2& x, const Rect2& y) { return lo(x) < lo(y); });

	struct Gap { float g; size_t i; };
	std::vector<Gap> gaps;   // 이웃한 두 요소 사이 간격
	for (size_t i = 0; i + 1 < items.size(); ++i) {
		const float g = lo(items[i + 1]) - hi(items[i]);
		if (g > 0.5f) gaps.push_back({ g, i });
	}

	struct Cand { float delta; int kind; size_t idx; float g; };   // kind 1: 오른쪽에 붙임(idx=기준 요소), 2: 왼쪽에 붙임, 3: 사이(idx=왼쪽 요소)
	Cand best{ 0.0f, 0, 0, 0.0f };
	float bestAbs = tol + 1e-4f;
	const float size = hi(m) - lo(m);
	auto consider = [&](float delta, int kind, size_t idx, float g) {
		if (std::fabs(delta) < bestAbs) { bestAbs = std::fabs(delta); best = { delta, kind, idx, g }; }
	};
	for (size_t b = 0; b < items.size(); ++b)
		for (const Gap& gp : gaps) {
			consider((hi(items[b]) + gp.g) - lo(m), 1, b, gp.g);
			consider((lo(items[b]) - gp.g - size) - lo(m), 2, b, gp.g);
		}
	for (size_t i = 0; i + 1 < items.size(); ++i) {
		const float space = lo(items[i + 1]) - hi(items[i]) - size;
		if (space >= 0.5f) consider((hi(items[i]) + space * 0.5f) - lo(m), 3, i, space * 0.5f);
	}
	if (best.kind == 0) return false;

	delta = best.delta;
	const float mlo = lo(m) + delta, mhi = hi(m) + delta;
	const float crossM = (plo(m) + phi(m)) * 0.5f;
	auto addMark = [&](float a0, float a1, float cross, float v) {
		GapMark gm; gm.horizontal = horizontal; gm.a = a0; gm.b = a1; gm.cross = cross; gm.value = v;
		marks.push_back(gm);
	};
	auto crossOf = [&](const Rect2& r) { return ((std::max)(plo(r), plo(m)) + (std::min)(phi(r), phi(m))) * 0.5f; };
	if (best.kind == 1) {
		addMark(hi(items[best.idx]), mlo, crossOf(items[best.idx]), best.g);
	} else if (best.kind == 2) {
		addMark(mhi, lo(items[best.idx]), crossOf(items[best.idx]), best.g);
	} else {
		addMark(hi(items[best.idx]), mlo, crossOf(items[best.idx]), best.g);
		addMark(mhi, lo(items[best.idx + 1]), crossOf(items[best.idx + 1]), best.g);
	}
	if (best.kind != 3) {
		// 기준이 된 같은 크기의 이웃 간격들도 함께 표시
		for (const Gap& gp : gaps)
			if (std::fabs(gp.g - best.g) < 0.5f) addMark(hi(items[gp.i]), lo(items[gp.i + 1]), crossOf(items[gp.i]), gp.g);
	}
	(void)crossM;
	return true;
}

} // namespace

GuideResult SnapToGuides(const Rect2& m, const GuideTargets& t, float tolWorld, const GuideFlags& fl)
{
	GuideResult res;

	std::vector<Cand> xs, ys;
	auto addRect = [&](const Rect2& r) {
		xs.push_back({ r.left, r }); xs.push_back({ (r.left + r.right) * 0.5f, r }); xs.push_back({ r.right, r });
		ys.push_back({ r.top, r }); ys.push_back({ (r.top + r.bottom) * 0.5f, r }); ys.push_back({ r.bottom, r });
	};
	if (t.hasParent) addRect(t.parent);
	for (const Rect2& r : t.rects) addRect(r);
	if (xs.empty()) return res;

	const float mx[3] = { m.left, (m.left + m.right) * 0.5f, m.right };
	const bool  ex[3] = { fl.left, fl.centerX, fl.right };
	const float my[3] = { m.top, (m.top + m.bottom) * 0.5f, m.bottom };
	const bool  ey[3] = { fl.top, fl.centerY, fl.bottom };

	float bestX = tolWorld + 1e-4f, bestY = tolWorld + 1e-4f;
	for (int i = 0; i < 3; ++i) {
		if (ex[i]) for (const Cand& c : xs) {
			const float d = c.v - mx[i];
			if (std::fabs(d) < bestX) { bestX = std::fabs(d); res.dx = d; res.snappedX = true; }
		}
		if (ey[i]) for (const Cand& c : ys) {
			const float d = c.v - my[i];
			if (std::fabs(d) < bestY) { bestY = std::fabs(d); res.dy = d; res.snappedY = true; }
		}
	}

	// 같은 간격 (5.1.3): 변 맞춤이 없거나, 변 맞춤보다 더 가까우면 간격 맞춤을 사용
	if (fl.equalSpacing) {
		std::vector<Rect2> items = t.rects;
		float d = 0.0f;
		std::vector<GapMark> mk;
		if (EqualSpacingAxis(m, items, true, tolWorld, d, mk) && (!res.snappedX || std::fabs(d) < std::fabs(res.dx))) {
			res.dx = d; res.snappedX = true;
			for (const auto& g : mk) res.gaps.push_back(g);
		}
		mk.clear();
		if (EqualSpacingAxis(m, items, false, tolWorld, d, mk) && (!res.snappedY || std::fabs(d) < std::fabs(res.dy))) {
			res.dy = d; res.snappedY = true;
			for (const auto& g : mk) res.gaps.push_back(g);
		}
	}

	// 맞춰진 위치 기준으로 일치하는 모든 변에 가이드선 생성
	const Rect2 s{ m.left + res.dx, m.top + res.dy, m.right + res.dx, m.bottom + res.dy };
	const float sx[3] = { s.left, (s.left + s.right) * 0.5f, s.right };
	const float sy[3] = { s.top, (s.top + s.bottom) * 0.5f, s.bottom };
	if (res.snappedX)
		for (int i = 0; i < 3; ++i) if (ex[i])
			for (const Cand& c : xs) if (std::fabs(c.v - sx[i]) < 0.01f) AddLine(res.lines, true, c.v, s, c.r);
	if (res.snappedY)
		for (int i = 0; i < 3; ++i) if (ey[i])
			for (const Cand& c : ys) if (std::fabs(c.v - sy[i]) < 0.01f) AddLine(res.lines, false, c.v, s, c.r);
	return res;
}

} // namespace ge
