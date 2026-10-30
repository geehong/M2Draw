#include "pch.h"
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#include "D2DGraphicEngine.h"
#include "GeNodeVisitor.h"

namespace ge {

void PathNode::Accept(INodeVisitor& visitor)
{
	visitor.Visit(*this);
}

void GroupNode::Accept(INodeVisitor& visitor)
{
	visitor.Visit(*this);
}

// ============================================================================
// 1. Math
// ============================================================================
void Rect2::Union(const Rect2& o)
{
	left   = (std::min)(left, o.left);
	top    = (std::min)(top, o.top);
	right  = (std::max)(right, o.right);
	bottom = (std::max)(bottom, o.bottom);
}

Matrix2D Matrix2D::Translation(float x, float y)
{
	Matrix2D m; m.dx = x; m.dy = y; return m;
}

Matrix2D Matrix2D::Scale(float sx, float sy, Vec2 c)
{
	Matrix2D m;
	m.m11 = sx; m.m22 = sy;
	m.dx = c.x - c.x * sx;
	m.dy = c.y - c.y * sy;
	return m;
}

Matrix2D Matrix2D::Rotation(float degree, Vec2 c)
{
	const float rad = degree * 3.14159265f / 180.0f;
	const float cs = std::cos(rad), sn = std::sin(rad);
	Matrix2D m;
	m.m11 = cs;  m.m12 = sn;
	m.m21 = -sn; m.m22 = cs;
	m.dx = c.x - c.x * cs + c.y * sn;
	m.dy = c.y - c.x * sn - c.y * cs;
	return m;
}

Matrix2D Matrix2D::operator*(const Matrix2D& b) const
{
	Matrix2D r;
	r.m11 = m11 * b.m11 + m12 * b.m21;
	r.m12 = m11 * b.m12 + m12 * b.m22;
	r.m21 = m21 * b.m11 + m22 * b.m21;
	r.m22 = m21 * b.m12 + m22 * b.m22;
	r.dx  = dx  * b.m11 + dy  * b.m21 + b.dx;
	r.dy  = dx  * b.m12 + dy  * b.m22 + b.dy;
	return r;
}

bool Matrix2D::Invert()
{
	const float det = m11 * m22 - m12 * m21;
	if (std::abs(det) < 1e-8f) return false;
	const float inv = 1.0f / det;
	Matrix2D r;
	r.m11 =  m22 * inv;
	r.m12 = -m12 * inv;
	r.m21 = -m21 * inv;
	r.m22 =  m11 * inv;
	r.dx  = (m21 * dy - m22 * dx) * inv;
	r.dy  = (m12 * dx - m11 * dy) * inv;
	*this = r;
	return true;
}

Vec2 Matrix2D::TransformPoint(Vec2 p) const
{
	return { p.x * m11 + p.y * m21 + dx, p.x * m12 + p.y * m22 + dy };
}

Rect2 Matrix2D::TransformRect(const Rect2& r) const
{
	Vec2 p[4] = {
		TransformPoint(Vec2(r.left,  r.top)),    TransformPoint(Vec2(r.right, r.top)),
		TransformPoint(Vec2(r.right, r.bottom)), TransformPoint(Vec2(r.left,  r.bottom))
	};
	Rect2 o{ p[0].x, p[0].y, p[0].x, p[0].y };
	for (int i = 1; i < 4; ++i) {
		o.left   = (std::min)(o.left, p[i].x);   o.right  = (std::max)(o.right, p[i].x);
		o.top    = (std::min)(o.top, p[i].y);    o.bottom = (std::max)(o.bottom, p[i].y);
	}
	return o;
}

Matrix2D Viewport::ToMatrix() const
{
	return Matrix2D::Scale(zoom, zoom) * Matrix2D::Translation(pan.x, pan.y);
}

Vec2 Viewport::ScreenToWorld(Vec2 s) const
{
	if (zoom <= 0.0001f) return s;
	return { (s.x - pan.x) / zoom, (s.y - pan.y) / zoom };
}

Vec2 Viewport::WorldToScreen(Vec2 w) const
{
	return { w.x * zoom + pan.x, w.y * zoom + pan.y };
}

// ============================================================================
// 2. Model
// ============================================================================
Matrix2D Node::GetWorldTransform() const
{
	Matrix2D xf = m_xf;
	for (const GroupNode* p = m_parent; p; p = p->GetParent())
		xf = xf * p->GetTransform();
	return xf;
}

// ---- PathNode ---------------------------------------------------------------
static bool BuildTextLayout(const TextNode& t, ComPtr<IDWriteTextLayout>& layout, float& w, float& h, float& x0);   // 아래(렌더러 부근)에 정의

// ---------------- ImageNode ----------------
std::unique_ptr<Node> ImageNode::Clone() const
{
	auto copy = std::make_unique<ImageNode>(*this);
	copy->ResetIdentity();
	return copy;
}

void ImageNode::Accept(INodeVisitor& visitor)
{
	visitor.Visit(*this);
}

bool ImageNode::GetLocalBounds(Rect2& out)
{
	out = Rect2{ 0.0f, 0.0f, m_w, m_h };
	return true;
}

// ---------------- TextNode ----------------
std::unique_ptr<Node> TextNode::Clone() const
{
	auto copy = std::make_unique<TextNode>(*this);
	copy->ResetIdentity();
	return copy;
}

void TextNode::Accept(INodeVisitor& visitor)
{
	visitor.Visit(*this);
}

int TextNode::LineCount() const
{
	int n = 1;
	for (wchar_t c : m_text) if (c == L'\n') ++n;
	return n;
}

float TextNode::MeasuredWidth() const
{
	if (m_boxW > 0.0f) return m_boxW;   // 영역 텍스트는 상자 폭
	if (m_mw > 0.0f) return m_mw;
	size_t longest = 0, cur = 0;   // 렌더 전 추정: 글자 수 x 글꼴 크기 x 0.6 (전각 문자는 1.0)
	float w = 0.0f, best = 0.0f;
	for (wchar_t c : m_text) {
		if (c == L'\n') { best = (std::max)(best, w); w = 0.0f; continue; }
		w += m_size * (c > 0x2E80 ? 1.0f : 0.6f);
	}
	(void)longest; (void)cur;
	return (std::max)(best, w);
}

float TextNode::MeasuredHeight() const
{
	return m_mh > 0.0f ? m_mh : m_size * kLineHeightRatio * LineCount();
}

bool TextNode::GetLocalBounds(Rect2& out)
{
	const float w = MeasuredWidth(), h = MeasuredHeight();
	const float left = IsAreaText() ? 0.0f : (m_align == TextAlign::Center) ? -w * 0.5f : (m_align == TextAlign::Right) ? -w : 0.0f;
	out = Rect2{ left, 0.0f, left + w, h };
	return true;
}

std::unique_ptr<Node> PathNode::Clone() const
{
	auto copy = std::make_unique<PathNode>(*this);
	copy->ResetIdentity();
	return copy;
}

bool PathNode::GetLocalBounds(Rect2& out)
{
	if (m_paths.empty()) return false;
	if (m_boundsValid && m_boundsVersion == m_version) {
		out = m_cachedBounds;
		return true;
	}

	bool any = false;
	Rect2 box{ 1e9f, 1e9f, -1e9f, -1e9f };
	auto add = [&](Vec2 p) {
		box.left = (std::min)(box.left, p.x);   box.right  = (std::max)(box.right, p.x);
		box.top  = (std::min)(box.top, p.y);    box.bottom = (std::max)(box.bottom, p.y);
		any = true;
	};

	// 1차원 3차 베지어 도함수 B'(t) = 3(a*t^2 + 2*b*t + c) = 0 의 해 t in (0, 1) 를 찾아 극값 포함 (Step 6.8)
	auto addCubicExtrema = [&](const BezierNode& n0, const BezierNode& n1) {
		if (!n0.HasOut() && !n1.HasIn()) return;
		const Vec2 p0 = n0.anchor, p1 = n0.AbsOut(), p2 = n1.AbsIn(), p3 = n1.anchor;
		auto solveAxis = [&](float v0, float v1, float v2, float v3) {
			const float a = -v0 + 3.0f * v1 - 3.0f * v2 + v3;
			const float b = v0 - 2.0f * v1 + v2;
			const float c = v1 - v0;
			if (std::fabs(a) < 1e-6f) {
				if (std::fabs(b) > 1e-6f) {
					const float t = -c / (2.0f * b);
					if (t > 0.0f && t < 1.0f) add(EvalSegment(n0, n1, t));
				}
				return;
			}
			const float disc = b * b - a * c;
			if (disc < 0.0f) return;
			const float sq = std::sqrt(disc);
			const float t1 = (-b - sq) / a;
			const float t2 = (-b + sq) / a;
			if (t1 > 0.0f && t1 < 1.0f) add(EvalSegment(n0, n1, t1));
			if (t2 > 0.0f && t2 < 1.0f) add(EvalSegment(n0, n1, t2));
		};
		solveAxis(p0.x, p1.x, p2.x, p3.x);
		solveAxis(p0.y, p1.y, p2.y, p3.y);
	};

	for (const auto& sp : m_paths) {
		const size_t cnt = sp.nodes.size();
		for (size_t i = 0; i < cnt; ++i) {
			add(sp.nodes[i].anchor);
			if (i + 1 < cnt) {
				addCubicExtrema(sp.nodes[i], sp.nodes[i + 1]);
			} else if (sp.closed && cnt > 1) {
				addCubicExtrema(sp.nodes[i], sp.nodes[0]);
			}
		}
	}
	if (any) {
		// 1D 수평/수직 직선이나 단일 점인 경우에만 최소 두께(2px)를 부여하고, 일반 도형은 정확한 타이트 경계를 유지
		if (box.right - box.left < 1e-3f) { box.left -= 1.0f; box.right += 1.0f; }
		if (box.bottom - box.top < 1e-3f) { box.top -= 1.0f; box.bottom += 1.0f; }
		m_cachedBounds = box;
		m_boundsVersion = m_version;
		m_boundsValid = true;
		out = box;
	}
	return any;
}

void PathNode::AddAnchor(Vec2 localPt, bool forceNewSubPath)
{
	if (m_paths.empty() || forceNewSubPath || m_paths.back().closed)
		m_paths.emplace_back();
	BezierNode n;
	n.anchor = localPt;
	m_paths.back().nodes.push_back(n);
	Invalidate();
}

static Vec2 Lerp2(Vec2 a, Vec2 b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

Vec2 PathNode::EvalSegment(const BezierNode& a, const BezierNode& b, float t)
{
	const Vec2 p0 = a.anchor, p1 = a.AbsOut(), p2 = b.AbsIn(), p3 = b.anchor;
	const Vec2 q0 = Lerp2(p0, p1, t), q1 = Lerp2(p1, p2, t), q2 = Lerp2(p2, p3, t);
	return Lerp2(Lerp2(q0, q1, t), Lerp2(q1, q2, t), t);
}

void PathNode::BuildArcSubPaths(const Rect2& box, const ArcParams& p, std::vector<SubPath>& out)
{
	out.clear();
	const float PI = 3.14159265358979323846f;
	const float cx = (p.rx > 0.0f) ? p.cx : box.MidX();
	const float cy = (p.ry > 0.0f) ? p.cy : box.MidY();
	const float rx = (p.rx > 0.0f) ? p.rx : (std::max)(0.5f, box.Width() * 0.5f);
	const float ry = (p.ry > 0.0f) ? p.ry : (std::max)(0.5f, box.Height() * 0.5f);

	float sweepDeg = p.sweepAngle;
	if (std::fabs(sweepDeg) < 0.1f) sweepDeg = 270.0f;
	sweepDeg = (std::max)(-360.0f, (std::min)(360.0f, sweepDeg));
	const bool fullCircle = std::fabs(sweepDeg) >= 359.9f;

	auto buildEllipticalArcNodes = [&](float radX, float radY, float startDeg, float deltaDeg, std::vector<BezierNode>& nodes) {
		nodes.clear();
		const float totalRad = deltaDeg * (PI / 180.0f);
		const int segs = (std::max)(1, static_cast<int>(std::ceil(std::fabs(totalRad) / (PI * 0.5f))));
		const float dTheta = totalRad / static_cast<float>(segs);
		const float k = (4.0f / 3.0f) * std::tan(dTheta * 0.25f);
		const float startRad = startDeg * (PI / 180.0f);

		for (int i = 0; i <= segs; ++i) {
			const float th = startRad + static_cast<float>(i) * dTheta;
			const float cs = std::cos(th), sn = std::sin(th);
			BezierNode bn;
			bn.type = BezierNode::Type::Smooth;
			bn.anchor = { cx + radX * cs, cy + radY * sn };
			const Vec2 tanVec{ -radX * sn * k, radY * cs * k };
			bn.hIn  = (i == 0)    ? Vec2{ 0.0f, 0.0f } : Vec2{ -tanVec.x, -tanVec.y };
			bn.hOut = (i == segs) ? Vec2{ 0.0f, 0.0f } : tanVec;
			nodes.push_back(bn);
		}
	};

	if (p.mode == ArcMode::Donut) {
		const float ir = (std::max)(0.05f, (std::min)(0.95f, p.innerRatio));
		if (fullCircle) {
			SubPath outerSp;
			buildEllipticalArcNodes(rx, ry, p.startAngle, 360.0f, outerSp.nodes);
			if (outerSp.nodes.size() > 1) {
				outerSp.nodes.front().hIn = outerSp.nodes.back().hIn;
				outerSp.nodes.pop_back();
			}
			outerSp.closed = true;
			out.push_back(std::move(outerSp));

			SubPath innerSp;
			buildEllipticalArcNodes(rx * ir, ry * ir, p.startAngle + 360.0f, -360.0f, innerSp.nodes);
			if (innerSp.nodes.size() > 1) {
				innerSp.nodes.front().hIn = innerSp.nodes.back().hIn;
				innerSp.nodes.pop_back();
			}
			innerSp.closed = true;
			out.push_back(std::move(innerSp));
			return;
		}
		SubPath sp;
		std::vector<BezierNode> outerNodes, innerNodes;
		buildEllipticalArcNodes(rx, ry, p.startAngle, sweepDeg, outerNodes);
		buildEllipticalArcNodes(rx * ir, ry * ir, p.startAngle + sweepDeg, -sweepDeg, innerNodes);
		if (!outerNodes.empty()) { outerNodes.front().type = BezierNode::Type::Corner; outerNodes.back().type = BezierNode::Type::Corner; }
		if (!innerNodes.empty()) { innerNodes.front().type = BezierNode::Type::Corner; innerNodes.back().type = BezierNode::Type::Corner; }
		sp.nodes.insert(sp.nodes.end(), outerNodes.begin(), outerNodes.end());
		sp.nodes.insert(sp.nodes.end(), innerNodes.begin(), innerNodes.end());
		sp.closed = true;
		out.push_back(std::move(sp));
		return;
	}

	SubPath sp;
	buildEllipticalArcNodes(rx, ry, p.startAngle, sweepDeg, sp.nodes);
	if (p.mode == ArcMode::Open) {
		sp.closed = false;
	} else if (p.mode == ArcMode::Chord || fullCircle) {
		if (fullCircle && sp.nodes.size() > 1) {
			sp.nodes.front().hIn = sp.nodes.back().hIn;
			sp.nodes.pop_back();
		} else if (!sp.nodes.empty()) {
			sp.nodes.front().type = BezierNode::Type::Corner;
			sp.nodes.back().type = BezierNode::Type::Corner;
		}
		sp.closed = true;
	} else {
		// ArcMode::Pie (부채꼴)
		if (!sp.nodes.empty()) {
			sp.nodes.front().type = BezierNode::Type::Corner;
			sp.nodes.back().type = BezierNode::Type::Corner;
		}
		BezierNode centerNode;
		centerNode.type = BezierNode::Type::Corner;
		centerNode.anchor = { cx, cy };
		sp.nodes.push_back(centerNode);
		sp.closed = true;
	}
	out.push_back(std::move(sp));
}

void PathNode::BuildBlockArrowSubPaths(const Rect2& box, const BlockArrowParams& p, std::vector<SubPath>& out)
{
	out.clear();
	const float left = box.left, top = box.top, right = box.right, bottom = box.bottom;
	const float w = (std::max)(1.0f, right - left);
	const float h = (std::max)(1.0f, bottom - top);
	const float cy = top + h * 0.5f;
	const float headLen = w * (std::max)(0.1f, (std::min)(0.85f, p.headLengthRatio));
	const float shaftHalfH = (h * 0.5f) * (std::max)(0.1f, (std::min)(0.9f, p.shaftWidthRatio));
	const float headX = right - headLen;

	SubPath sp;
	sp.closed = true;
	const Vec2 pts[7] = {
		{ left,  cy - shaftHalfH },
		{ headX, cy - shaftHalfH },
		{ headX, top },
		{ right, cy },
		{ headX, bottom },
		{ headX, cy + shaftHalfH },
		{ left,  cy + shaftHalfH }
	};
	for (const Vec2& pt : pts) {
		BezierNode bn;
		bn.type = BezierNode::Type::Corner;
		bn.anchor = pt;
		sp.nodes.push_back(bn);
	}
	out.push_back(std::move(sp));
}

void PathNode::BuildCalloutSubPaths(const Rect2& box, const CalloutParams& p, std::vector<SubPath>& out)
{
	out.clear();
	const float left = box.left, top = box.top, right = box.right, bottom = box.bottom;
	const float w = (std::max)(1.0f, right - left);
	const float h = (std::max)(1.0f, bottom - top);
	const float tailH = h * (std::max)(0.08f, (std::min)(0.5f, p.tailHeightRatio));
	const float bodyBottom = bottom - tailH;
	const float bodyH = (std::max)(1.0f, bodyBottom - top);
	const float tailW = w * (std::max)(0.06f, (std::min)(0.4f, p.tailWidthRatio));
	const float tailCenter = left + w * (std::max)(0.15f, (std::min)(0.85f, p.tailPosRatio));
	const float tailL = (std::max)(left + 2.0f, tailCenter - tailW * 0.5f);
	const float tailR = (std::min)(right - 2.0f, tailCenter + tailW * 0.5f);
	const float tipX  = left + w * (std::max)(0.05f, (std::min)(0.95f, p.tailPosRatio - 0.08f));
	(void)bodyH;

	SubPath sp;
	sp.closed = true;
	const Vec2 pts[7] = {
		{ left,  top },
		{ right, top },
		{ right, bodyBottom },
		{ tailR, bodyBottom },
		{ tipX,  bottom },
		{ tailL, bodyBottom },
		{ left,  bodyBottom }
	};
	for (const Vec2& pt : pts) {
		BezierNode bn;
		bn.type = BezierNode::Type::Corner;
		bn.anchor = pt;
		sp.nodes.push_back(bn);
	}
	out.push_back(std::move(sp));
}

void PathNode::MoveNodeOrHandle(HitPart part, int sp, int ni, Vec2 worldPt, bool breakTangent)
{
	if (sp < 0 || sp >= (int)m_paths.size()) return;
	auto& nodes = m_paths[sp].nodes;
	if (ni < 0 || ni >= (int)nodes.size()) return;
	Matrix2D inv = GetWorldTransform();
	if (!inv.Invert()) return;
	const Vec2 lp = inv.TransformPoint(worldPt);
	BezierNode& n = nodes[ni];
	switch (part) {
	case HitPart::Anchor:    n.anchor = lp; break;
	case HitPart::HandleIn: {
		Vec2 off = { lp.x - n.anchor.x, lp.y - n.anchor.y };
		if (breakTangent) n.hIn = off; else n.SetHandleIn(off);
		break; }
	case HitPart::HandleOut: {
		Vec2 off = { lp.x - n.anchor.x, lp.y - n.anchor.y };
		if (breakTangent) n.hOut = off; else n.SetHandleOut(off);
		break; }
	default: return;
	}
	m_kind = ShapeKind::Path;
	m_params = std::monostate{};
	Invalidate();
}

int PathNode::InsertNodeOnSegment(int sp, int seg, float t)
{
	if (sp < 0 || sp >= (int)m_paths.size()) return -1;
	SubPath& s = m_paths[sp];
	const int n = (int)s.nodes.size();
	const int segCount = s.closed ? n : n - 1;
	if (n < 2 || seg < 0 || seg >= segCount) return -1;
	t = (std::min)(1.0f, (std::max)(0.0f, t));
	const int bi = (seg + 1) % n;
	BezierNode& a = s.nodes[seg];
	BezierNode& b = s.nodes[bi];
	const Vec2 p0 = a.anchor, p1 = a.AbsOut(), p2 = b.AbsIn(), p3 = b.anchor;
	const Vec2 q0 = Lerp2(p0, p1, t), q1 = Lerp2(p1, p2, t), q2 = Lerp2(p2, p3, t);
	const Vec2 r0 = Lerp2(q0, q1, t), r1 = Lerp2(q1, q2, t);
	const Vec2 sp3 = Lerp2(r0, r1, t);
	BezierNode nn;
	nn.anchor = sp3;
	nn.hIn  = { r0.x - sp3.x, r0.y - sp3.y };
	nn.hOut = { r1.x - sp3.x, r1.y - sp3.y };
	nn.type = BezierNode::Type::Smooth;
	a.hOut = { q0.x - p0.x, q0.y - p0.y };
	b.hIn  = { q2.x - p3.x, q2.y - p3.y };
	const int at = seg + 1;
	s.nodes.insert(s.nodes.begin() + at, nn);
	m_kind = ShapeKind::Path;
	m_params = std::monostate{};
	Invalidate();
	return at;
}

bool PathNode::RemoveNode(int sp, int ni)
{
	if (sp < 0 || sp >= (int)m_paths.size()) return false;
	auto& nodes = m_paths[sp].nodes;
	if (ni < 0 || ni >= (int)nodes.size()) return false;
	nodes.erase(nodes.begin() + ni);
	if (nodes.empty()) m_paths.erase(m_paths.begin() + sp);
	m_kind = ShapeKind::Path;
	m_params = std::monostate{};
	Invalidate();
	return true;
}

bool PathNode::FindClosestSegment(Vec2 lp, int& spIdx, int& segIdx, float& t, float& distSq) const
{
	bool found = false;
	distSq = 1e30f;
	const int STEPS = 24;
	for (int si = 0; si < (int)m_paths.size(); ++si) {
		const SubPath& s = m_paths[si];
		const int n = (int)s.nodes.size();
		const int segCount = s.closed ? n : n - 1;
		for (int g = 0; g < segCount; ++g) {
			const BezierNode& a = s.nodes[g];
			const BezierNode& b = s.nodes[(g + 1) % n];
			for (int k = 0; k <= STEPS; ++k) {
				const float tt = (float)k / STEPS;
				const Vec2 p = EvalSegment(a, b, tt);
				const float dx = p.x - lp.x, dy = p.y - lp.y;
				const float d = dx * dx + dy * dy;
				if (d < distSq) { distSq = d; spIdx = si; segIdx = g; t = tt; found = true; }
			}
		}
	}
	return found;
}

float Node::GetEffectiveOpacity() const
{
	float o = m_opacity;
	for (const GroupNode* p = m_parent; p; p = p->GetParent()) o *= p->GetOpacity();
	return o;
}

bool Node::IsEffectivelyVisible() const
{
	if (!m_visible) return false;
	for (const GroupNode* p = m_parent; p; p = p->GetParent())
		if (!p->IsVisible()) return false;
	return true;
}

bool Node::IsEffectivelyLocked() const
{
	if (m_locked) return true;
	for (const GroupNode* p = m_parent; p; p = p->GetParent())
	{
		// 단일 부품(버튼/표준 UI 컴포넌트)이 잠겨 있으면 그 내부 자식 요소도 함께 잠김
		if (IsAtomicComponent(p) && p->IsLocked())
			return true;
		// 일반 그룹(Ctrl+G, FrameNode·TableNode·LayerNode 제외)이 잠겨 있으면 그룹 단위 선택이므로 함께 잠김
		if (p->Kind() == NodeKind::Group &&
		    dynamic_cast<const FrameNode*>(p) == nullptr &&
		    dynamic_cast<const TableNode*>(p) == nullptr &&
		    p->IsLocked())
		{
			return true;
		}
	}
	return false;
}

// ---- GroupNode --------------------------------------------------------------
std::unique_ptr<Node> GroupNode::Clone() const
{
	auto copy = std::make_unique<GroupNode>();
	copy->CopyNodePropsFrom(*this);
	copy->m_bClippingMask = m_bClippingMask;
	for (const auto& c : m_children) copy->AddChild(c->Clone());
	return copy;
}

std::unique_ptr<Node> FrameNode::Clone() const
{
	auto copy = std::make_unique<FrameNode>();
	copy->CopyNodePropsFrom(*this);
	copy->m_w = m_w; copy->m_h = m_h; copy->m_clip = m_clip; copy->m_radii = m_radii; copy->m_borderSides = m_borderSides;
	copy->m_layoutMode = m_layoutMode; copy->m_gap = m_gap; copy->m_pad = m_pad;
	copy->m_mainAlign = m_mainAlign; copy->m_crossAlign = m_crossAlign;
	copy->m_sizingW = m_sizingW; copy->m_sizingH = m_sizingH;
	copy->m_uiKind = m_uiKind; copy->m_uiProps = m_uiProps;
	for (const auto& c : m_children) copy->AddChild(c->Clone());
	return copy;
}

// ---------------- TableNode ----------------
std::unique_ptr<Node> TableNode::Clone() const
{
	auto copy = std::make_unique<TableNode>();
	copy->CopyNodePropsFrom(*this);
	copy->m_colW = m_colW;
	copy->m_rowH = m_rowH;
	copy->m_border = m_border;
	copy->m_pad = m_pad;
	copy->m_borderColor = m_borderColor;
	copy->m_merges = m_merges;
	copy->m_headerRows = m_headerRows;
	copy->m_cellAlign = m_cellAlign;
	copy->m_borderStyle = m_borderStyle;
	for (const auto& c : m_children) copy->AddChild(c->Clone());
	return copy;
}

bool TableNode::GetLocalBounds(Rect2& out)
{
	float w = 0.0f, h = 0.0f;
	for (float v : m_colW) w += v;
	for (float v : m_rowH) h += v;
	out = Rect2{ 0.0f, 0.0f, w, h };
	return true;
}

void TableNode::SetGrid(int rows, int cols, float cellW, float cellH)
{
	rows = (std::max)(1, (std::min)(50, rows));
	cols = (std::max)(1, (std::min)(50, cols));
	m_rowH.assign(rows, (std::max)(1.0f, cellH));
	m_colW.assign(cols, (std::max)(1.0f, cellW));
	m_merges.clear();
	m_headerRows = (std::min)(m_headerRows, rows);
	Invalidate();
}

void TableNode::SetSizes(const std::vector<float>& colW, const std::vector<float>& rowH)
{
	if (colW.empty() || rowH.empty()) return;
	m_colW = colW;
	m_rowH = rowH;
	for (auto& v : m_colW) v = (std::max)(1.0f, v);
	for (auto& v : m_rowH) v = (std::max)(1.0f, v);
	Invalidate();
}

void TableNode::SetColWidth(int c, float w)
{
	if (c < 0 || c >= Cols()) return;
	m_colW[c] = (std::max)(1.0f, w);
	Invalidate();
}

void TableNode::SetRowHeight(int r, float h)
{
	if (r < 0 || r >= Rows()) return;
	m_rowH[r] = (std::max)(1.0f, h);
	Invalidate();
}

Rect2 TableNode::CellRect(int r, int c) const
{
	if (r < 0 || r >= Rows() || c < 0 || c >= Cols()) return Rect2{};
	float x = 0.0f, y = 0.0f;
	for (int i = 0; i < c; ++i) x += m_colW[i];
	for (int i = 0; i < r; ++i) y += m_rowH[i];
	return Rect2{ x, y, x + m_colW[c], y + m_rowH[r] };
}

bool TableNode::CellAt(Vec2 p, int& r, int& c) const
{
	r = c = -1;
	float acc = 0.0f;
	int cc = -1, rr = -1;
	for (int i = 0; i < Cols(); ++i) {
		if (p.x >= acc && p.x < acc + m_colW[i]) { cc = i; break; }
		acc += m_colW[i];
	}
	acc = 0.0f;
	for (int i = 0; i < Rows(); ++i) {
		if (p.y >= acc && p.y < acc + m_rowH[i]) { rr = i; break; }
		acc += m_rowH[i];
	}
	if (cc < 0 || rr < 0) return false;
	const Span sp = SpanOf(rr, cc);
	r = sp.r;
	c = sp.c;
	return true;
}

// ---- 병합 / 격자선 / 행·열 편집 (4.5.7.2) ----
void TableNode::SetMerges(const std::vector<Span>& v)
{
	m_merges.clear();
	for (const Span& s : v) MergeRange(s.r, s.c, s.r + s.rs - 1, s.c + s.cs - 1);
}

TableNode::Span TableNode::SpanOf(int r, int c) const
{
	for (const Span& s : m_merges)
		if (r >= s.r && r < s.r + s.rs && c >= s.c && c < s.c + s.cs) return s;
	Span one; one.r = r; one.c = c;
	return one;
}

bool TableNode::MergeRange(int r0, int c0, int r1, int c1)
{
	if (r0 > r1) std::swap(r0, r1);
	if (c0 > c1) std::swap(c0, c1);
	if (r0 < 0 || c0 < 0 || r1 >= Rows() || c1 >= Cols()) return false;
	if (r0 == r1 && c0 == c1) return false;
	for (const Span& s : m_merges)
		if (!(r1 < s.r || r0 >= s.r + s.rs || c1 < s.c || c0 >= s.c + s.cs)) return false;   // 기존 병합과 겹침
	Span n; n.r = r0; n.c = c0; n.rs = r1 - r0 + 1; n.cs = c1 - c0 + 1;
	m_merges.push_back(n);
	Invalidate();
	return true;
}

bool TableNode::SplitAt(int r, int c)
{
	for (size_t i = 0; i < m_merges.size(); ++i) {
		const Span& s = m_merges[i];
		if (r >= s.r && r < s.r + s.rs && c >= s.c && c < s.c + s.cs) {
			m_merges.erase(m_merges.begin() + i);
			Invalidate();
			return true;
		}
	}
	return false;
}

Rect2 TableNode::SpanRect(int r, int c) const
{
	const Span s = SpanOf(r, c);
	const Rect2 a = CellRect(s.r, s.c);
	const Rect2 b = CellRect(s.r + s.rs - 1, s.c + s.cs - 1);
	return Rect2{ a.left, a.top, b.right, b.bottom };
}

void TableNode::GridSegments(std::vector<Seg>& out) const
{
	out.clear();
	const int R = Rows(), C = Cols();
	float totalW = 0.0f, totalH = 0.0f;
	for (float v : m_colW) totalW += v;
	for (float v : m_rowH) totalH += v;

	// 세로선: 열 i-1 / i 사이. 같은 병합 안에 있는 행 구간은 건너뜀
	float x = 0.0f;
	for (int i = 1; i < C; ++i) {
		x += m_colW[i - 1];
		float y = 0.0f, runStart = -1.0f;
		for (int r = 0; r < R; ++r) {
			const Span a = SpanOf(r, i - 1), b = SpanOf(r, i);
			const bool inside = (a.r == b.r && a.c == b.c);
			if (!inside && runStart < 0.0f) runStart = y;
			if (inside && runStart >= 0.0f) { out.push_back(Seg{ x, runStart, x, y }); runStart = -1.0f; }
			y += m_rowH[r];
		}
		if (runStart >= 0.0f) out.push_back(Seg{ x, runStart, x, totalH });
	}
	// 가로선: 행 i-1 / i 사이
	float y = 0.0f;
	for (int i = 1; i < R; ++i) {
		y += m_rowH[i - 1];
		float xx = 0.0f, runStart = -1.0f;
		for (int c = 0; c < C; ++c) {
			const Span a = SpanOf(i - 1, c), b = SpanOf(i, c);
			const bool inside = (a.r == b.r && a.c == b.c);
			if (!inside && runStart < 0.0f) runStart = xx;
			if (inside && runStart >= 0.0f) { out.push_back(Seg{ runStart, y, xx, y }); runStart = -1.0f; }
			xx += m_colW[c];
		}
		if (runStart >= 0.0f) out.push_back(Seg{ runStart, y, totalW, y });
	}
}

bool TableNode::InsertRowAt(int at, float h)
{
	if (at < 0 || at > Rows() || Rows() >= 50) return false;
	m_rowH.insert(m_rowH.begin() + at, (std::max)(1.0f, h));
	for (Span& s : m_merges) {
		if (s.r >= at) ++s.r;
		else if (s.r + s.rs > at) ++s.rs;
	}
	if (at < m_headerRows) ++m_headerRows;
	Invalidate();
	return true;
}

bool TableNode::RemoveRowAt(int at)
{
	if (at < 0 || at >= Rows() || Rows() <= 1) return false;
	m_rowH.erase(m_rowH.begin() + at);
	for (size_t i = 0; i < m_merges.size();) {
		Span& s = m_merges[i];
		if (at < s.r) --s.r;
		else if (at < s.r + s.rs) --s.rs;
		if (s.rs <= 1 && s.cs <= 1) m_merges.erase(m_merges.begin() + i); else ++i;
	}
	if (at < m_headerRows) --m_headerRows;
	if (m_actR >= Rows()) m_actR = Rows() - 1;
	Invalidate();
	return true;
}

bool TableNode::InsertColAt(int at, float w)
{
	if (at < 0 || at > Cols() || Cols() >= 50) return false;
	m_colW.insert(m_colW.begin() + at, (std::max)(1.0f, w));
	for (Span& s : m_merges) {
		if (s.c >= at) ++s.c;
		else if (s.c + s.cs > at) ++s.cs;
	}
	Invalidate();
	return true;
}

bool TableNode::RemoveColAt(int at)
{
	if (at < 0 || at >= Cols() || Cols() <= 1) return false;
	m_colW.erase(m_colW.begin() + at);
	for (size_t i = 0; i < m_merges.size();) {
		Span& s = m_merges[i];
		if (at < s.c) --s.c;
		else if (at < s.c + s.cs) --s.cs;
		if (s.rs <= 1 && s.cs <= 1) m_merges.erase(m_merges.begin() + i); else ++i;
	}
	if (m_actC >= Cols()) m_actC = Cols() - 1;
	Invalidate();
	return true;
}

CornerRadii FrameNode::ClampedRadii() const
{
	CornerRadii r = m_radii;
	// 각 변에서 두 모서리 반경의 합이 변 길이를 넘으면 같은 비율로 줄임
	float k = 1.0f;
	auto fit = [&](float a, float b, float len) { const float s = a + b; if (s > len && s > 0.0f) k = (std::min)(k, len / s); };
	fit(r.tl, r.tr, m_w); fit(r.bl, r.br, m_w);
	fit(r.tl, r.bl, m_h); fit(r.tr, r.br, m_h);
	r.tl *= k; r.tr *= k; r.br *= k; r.bl *= k;
	return r;
}

bool FrameNode::ApplyLayout()
{
	if (m_layoutMode == FrameLayoutMode::None) return false;
	bool changed = false;

	const bool isRow = (m_layoutMode == FrameLayoutMode::Row);
	// 주축/교차축 기준 안쪽 여백 (Row: 주축=가로, Column: 주축=세로)
	const float mS = isRow ? m_pad.left : m_pad.top;
	const float mE = isRow ? m_pad.right : m_pad.bottom;
	const float cS = isRow ? m_pad.top : m_pad.left;
	const float cE = isRow ? m_pad.bottom : m_pad.right;

	// 1) 가시적 자식들의 바운딩 박스 수집
	struct ChildLayoutInfo {
		Node* node = nullptr;
		Rect2 bounds{};
		float mainSize = 0.0f;
		float crossSize = 0.0f;
	};

	// 0) Column 모드 등에서 TextNode 가 영역 텍스트인 경우, 프레임 가용 폭에 맞춰 자동 측정/줄바꿈 갱신
	for (const auto& c : m_children) {
		if (!c || !c->IsVisible()) continue;
		if (auto* tn = dynamic_cast<TextNode*>(c.get())) {
			if (!isRow && tn->IsAreaText()) {
				const float availW = (std::max)(20.0f, m_w - m_pad.left - m_pad.right);
				if (std::fabs(tn->GetBoxWidth() - availW) > 1.0f) {
					tn->SetBoxWidth(availW);
					changed = true;
				}
			}
			PathOp::MeasureText(*tn);
		}
	}

	// 0.5) Fill 크기 규칙 (4.5.9): 자식 프레임이 부모의 가용 공간을 채움.
	//      부모가 해당 축에서 Hug 이면 순환을 피하려고 자식의 현재 크기(Fixed 취급)를 유지한다.
	{
		const bool hugMain = isRow ? (m_sizingW == FrameSizingMode::Hug) : (m_sizingH == FrameSizingMode::Hug);
		const bool hugCross = isRow ? (m_sizingH == FrameSizingMode::Hug) : (m_sizingW == FrameSizingMode::Hug);
		std::vector<FrameNode*> fillMain;
		float fixedMain = 0.0f;
		int visCount = 0;
		for (const auto& c : m_children) {
			if (!c || !c->IsVisible()) continue;
			Rect2 lb;
			if (!c->GetLocalBounds(lb)) continue;
			++visCount;
			auto* fr = dynamic_cast<FrameNode*>(c.get());
			const Rect2 b = c->GetTransform().TransformRect(lb);
			const FrameSizingMode sMain = fr ? (isRow ? fr->GetSizingW() : fr->GetSizingH()) : FrameSizingMode::Fixed;
			if (fr && sMain == FrameSizingMode::Fill && !hugMain) fillMain.push_back(fr);
			else fixedMain += isRow ? b.Width() : b.Height();
		}
		const float availMain = (isRow ? m_w : m_h) - mS - mE;
		const float availCross = (isRow ? m_h : m_w) - cS - cE;
		const float share = fillMain.empty() ? 0.0f
			: (std::max)(1.0f, (availMain - fixedMain - (visCount > 0 ? (visCount - 1) * m_gap : 0.0f)) / static_cast<float>(fillMain.size()));

		for (const auto& c : m_children) {
			auto* fr = dynamic_cast<FrameNode*>(c.get());
			if (!fr || !fr->IsVisible()) continue;
			const FrameSizingMode sMain = isRow ? fr->GetSizingW() : fr->GetSizingH();
			const FrameSizingMode sCross = isRow ? fr->GetSizingH() : fr->GetSizingW();
			float nw = fr->GetWidth(), nh = fr->GetHeight();
			if (sMain == FrameSizingMode::Fill && !hugMain) (isRow ? nw : nh) = share;
			if (sCross == FrameSizingMode::Fill && !hugCross) (isRow ? nh : nw) = (std::max)(1.0f, availCross);
			const bool involved = (sMain == FrameSizingMode::Fill && !hugMain) || (sCross == FrameSizingMode::Fill && !hugCross);
			if (!involved) continue;
			if (std::fabs(nw - fr->GetWidth()) > 1e-3f || std::fabs(nh - fr->GetHeight()) > 1e-3f) {
				fr->ResizeFrame(nw, nh);   // 자식의 자식도 다시 배치/보정
				changed = true;
			} else if (fr->ApplyLayout()) {
				changed = true;
			}
		}
	}

	std::vector<ChildLayoutInfo> visibleChildren;
	float totalChildrenMain = 0.0f;
	float maxCross = 0.0f;

	for (const auto& c : m_children) {
		if (!c || !c->IsVisible()) continue;
		Rect2 lb;
		if (!c->GetLocalBounds(lb)) continue;
		const Rect2 b = c->GetTransform().TransformRect(lb);
		ChildLayoutInfo info;
		info.node = c.get();
		info.bounds = b;
		info.mainSize = isRow ? b.Width() : b.Height();
		info.crossSize = isRow ? b.Height() : b.Width();
		totalChildrenMain += info.mainSize;
		if (info.crossSize > maxCross) maxCross = info.crossSize;
		visibleChildren.push_back(info);
	}

	// 2) Hug 크기 모드 계산: 내용물에 맞춰 프레임 크기 자동 변경
	const int count = static_cast<int>(visibleChildren.size());
	const float contentMain = (count > 0) ? (totalChildrenMain + (count - 1) * m_gap) : 0.0f;
	const float hugW = isRow ? (contentMain + mS + mE) : (maxCross + cS + cE);
	const float hugH = isRow ? (maxCross + cS + cE) : (contentMain + mS + mE);

	if (m_sizingW == FrameSizingMode::Hug) {
		const float newW = (std::max)(1.0f, hugW);
		if (std::fabs(m_w - newW) > 1e-3f) {
			m_w = newW;
			changed = true;
		}
	}
	if (m_sizingH == FrameSizingMode::Hug) {
		const float newH = (std::max)(1.0f, hugH);
		if (std::fabs(m_h - newH) > 1e-3f) {
			m_h = newH;
			changed = true;
		}
	}

	if (count == 0) return changed;

	// 3) 주축(Main Axis) 가용 공간 및 간격 계산
	const float frameMain = isRow ? m_w : m_h;
	const float availableMain = frameMain - mS - mE;
	const float remainingMain = availableMain - totalChildrenMain;

	float startMain = mS;
	float actualGap = m_gap;

	switch (m_mainAlign) {
	case LayoutMainAlign::Start:
		startMain = mS;
		actualGap = m_gap;
		break;
	case LayoutMainAlign::Center:
		if (count > 0) {
			const float totalSpan = totalChildrenMain + (count - 1) * m_gap;
			startMain = mS + (std::max)(0.0f, (availableMain - totalSpan) * 0.5f);
		}
		actualGap = m_gap;
		break;
	case LayoutMainAlign::End:
		if (count > 0) {
			const float totalSpan = totalChildrenMain + (count - 1) * m_gap;
			startMain = mS + (std::max)(0.0f, availableMain - totalSpan);
		}
		actualGap = m_gap;
		break;
	case LayoutMainAlign::SpaceBetween:
		startMain = mS;
		if (count > 1) {
			actualGap = (std::max)(0.0f, remainingMain / static_cast<float>(count - 1));
		} else {
			actualGap = m_gap;
		}
		break;
	}

	// 4) 각 자식의 배치 위치 계산 및 변환 행렬 적용
	const float frameCross = isRow ? m_h : m_w;
	const float availableCross = frameCross - cS - cE;

	float cursor = startMain;
	for (const auto& item : visibleChildren) {
		// 교차축(Cross Axis) 정렬 기준 위치
		float crossPos = cS;
		switch (m_crossAlign) {
		case LayoutCrossAlign::Start:
			crossPos = cS;
			break;
		case LayoutCrossAlign::Center:
			crossPos = cS + (std::max)(0.0f, (availableCross - item.crossSize) * 0.5f);
			break;
		case LayoutCrossAlign::End:
			crossPos = cS + (std::max)(0.0f, availableCross - item.crossSize);
			break;
		}

		float targetX = 0.0f, targetY = 0.0f;
		if (isRow) {
			targetX = cursor;
			targetY = crossPos;
		} else {
			targetX = crossPos;
			targetY = cursor;
		}

		const float dx = targetX - item.bounds.left;
		const float dy = targetY - item.bounds.top;
		if (std::fabs(dx) > 1e-4f || std::fabs(dy) > 1e-4f) {
			item.node->SetTransform(item.node->GetTransform() * Matrix2D::Translation(dx, dy));
			changed = true;
		}

		cursor += item.mainSize + actualGap;
	}

	return changed;
}

void (*FrameNode::s_uiRebuildHook)(FrameNode&) = nullptr;

void FrameNode::ResizeFrame(float w, float h)
{
	w = (std::max)(1.0f, w);
	h = (std::max)(1.0f, h);
	if (IsUiComponent() && s_uiRebuildHook) {
		m_w = w;
		m_h = h;
		s_uiRebuildHook(*this);
		Invalidate();
		return;   // 표준 UI 컴포넌트는 새 크기에 맞춰 내부 자식을 통째로 재빌드하므로 자유 배치 자식 스케일을 건너뜀
	}
	const float dw = w - m_w, dh = h - m_h;
	const bool free = (m_layoutMode == FrameLayoutMode::None);
	if (free && (std::fabs(dw) > 1e-4f || std::fabs(dh) > 1e-4f)) {
		for (const auto& c : m_children) {
			if (!c) continue;
			Rect2 lb;
			if (!c->GetLocalBounds(lb)) continue;
			Rect2 b = c->GetTransform().TransformRect(lb);
			Matrix2D xf = c->GetTransform();
			float shiftX = 0.0f, shiftY = 0.0f;
			float newCW = b.Width(), newCH = b.Height();
			bool stretchW = false, stretchH = false;

			switch (c->GetConstraintH()) {
			case ConstraintH::Left: break;
			case ConstraintH::Right: shiftX = dw; break;
			case ConstraintH::Center: shiftX = dw * 0.5f; break;
			case ConstraintH::LeftRight: stretchW = true; newCW = (std::max)(1.0f, b.Width() + dw); break;
			}
			switch (c->GetConstraintV()) {
			case ConstraintV::Top: break;
			case ConstraintV::Bottom: shiftY = dh; break;
			case ConstraintV::Center: shiftY = dh * 0.5f; break;
			case ConstraintV::TopBottom: stretchH = true; newCH = (std::max)(1.0f, b.Height() + dh); break;
			}

			// 글자/표는 늘리지 않고 위치만 보정 (왼쪽/위 기준)
			if (c->Kind() == NodeKind::Text || dynamic_cast<TableNode*>(c.get())) { stretchW = stretchH = false; }

			if (auto* child = dynamic_cast<FrameNode*>(c.get())) {
				if (stretchW || stretchH) {
					child->ResizeFrame(stretchW ? newCW : child->GetWidth(), stretchH ? newCH : child->GetHeight());
				}
			} else if ((stretchW && b.Width() > 1e-3f) || (stretchH && b.Height() > 1e-3f)) {
				const float sx = (stretchW && b.Width() > 1e-3f) ? newCW / b.Width() : 1.0f;
				const float sy = (stretchH && b.Height() > 1e-3f) ? newCH / b.Height() : 1.0f;
				xf = xf * Matrix2D::Scale(sx, sy, Vec2{ b.left, b.top });
			}
			if (shiftX != 0.0f || shiftY != 0.0f) xf = xf * Matrix2D::Translation(shiftX, shiftY);
			c->SetTransform(xf);
			if (c->Kind() != NodeKind::Text) c->Invalidate();
		}
	}
	m_w = w;
	m_h = h;
	if (free) ClampChildren();   // 부모가 줄어들면 자식도 범위 안으로
	Invalidate();
	ApplyLayout();
}

bool FrameNode::ClampChild(Node* c, bool shrink)
{
	if (!c || m_layoutMode != FrameLayoutMode::None) return false;
	Rect2 lb;
	if (!c->GetLocalBounds(lb)) return false;
	Matrix2D xf = c->GetTransform();
	const Rect2 b = xf.TransformRect(lb);
	const float bw = b.Width(), bh = b.Height();
	const float eps = 0.01f;
	if (b.left >= -eps && b.top >= -eps && b.right <= m_w + eps && b.bottom <= m_h + eps) return false;

	const bool isText = (c->Kind() == NodeKind::Text) || dynamic_cast<TableNode*>(c) != nullptr;
	auto* fr = dynamic_cast<FrameNode*>(c);
	const bool plainFrame = fr && std::fabs(xf.m11 - 1.0f) < 1e-4f && std::fabs(xf.m22 - 1.0f) < 1e-4f && std::fabs(xf.m12) < 1e-4f && std::fabs(xf.m21) < 1e-4f;

	// 목표 상자: shrink 모드는 넘친 부분만 잘라냄, 그 외에는 부모보다 클 때만 부모 크기로 줄이고 위치를 안으로 옮김
	float nl = b.left, nt = b.top, nw = bw, nh = bh;
	if (!isText && shrink) {
		nl = (std::max)(b.left, 0.0f); nt = (std::max)(b.top, 0.0f);
		nw = (std::max)(1.0f, (std::min)(b.right, m_w) - nl);
		nh = (std::max)(1.0f, (std::min)(b.bottom, m_h) - nt);
	} else if (!isText) {
		nw = (std::min)(bw, m_w); nh = (std::min)(bh, m_h);
	}
	// 위치를 부모 안으로
	if (nw > m_w) nl = 0.0f; else nl = (std::max)(0.0f, (std::min)(nl, m_w - nw));
	if (nh > m_h) nt = 0.0f; else nt = (std::max)(0.0f, (std::min)(nt, m_h - nh));

	if (!isText && (std::fabs(nw - bw) > eps || std::fabs(nh - bh) > eps)) {
		if (plainFrame) {
			xf.dx += nl - b.left; xf.dy += nt - b.top;
			c->SetTransform(xf);
			fr->ResizeFrame(nw, nh);
			return true;
		}
		const float sx = bw > 1e-3f ? nw / bw : 1.0f, sy = bh > 1e-3f ? nh / bh : 1.0f;
		xf = xf * Matrix2D::Scale(sx, sy, Vec2{ b.left, b.top });
	}
	// 크기 조정 후의 위치 보정 (조정 후 좌상단은 b.left/b.top 그대로이므로 이동량 = 목표 - 현재)
	xf = xf * Matrix2D::Translation(nl - b.left, nt - b.top);
	c->SetTransform(xf);
	if (c->Kind() != NodeKind::Text) c->Invalidate();
	return true;
}

void FrameNode::ClampChildren()
{
	if (m_layoutMode != FrameLayoutMode::None) return;
	for (const auto& c : m_children) ClampChild(c.get(), false);
}

bool FrameNode::ClampNode(Node* child, bool shrink)
{
	if (!child) return false;
	auto* pf = dynamic_cast<FrameNode*>(child->GetParent());
	return pf ? pf->ClampChild(child, shrink) : false;
}

bool GroupNode::GetLocalBounds(Rect2& out)
{
	bool any = false;
	Rect2 box{};
	for (auto& c : m_children) {
		Rect2 b;
		if (!c->GetLocalBounds(b)) continue;
		b = c->GetTransform().TransformRect(b);   // 자식의 m_xf 적용 -> 그룹 로컬
		if (!any) { box = b; any = true; } else box.Union(b);
	}
	if (any) out = box;
	return any;
}

Node* GroupNode::AddChild(std::unique_ptr<Node> child, int index)
{
	if (!child) return nullptr;
	ChangeTracker::Bump();
	child->m_parent = this;
	Node* raw = child.get();
	if (index < 0 || index >= static_cast<int>(m_children.size()))
		m_children.push_back(std::move(child));
	else
		m_children.insert(m_children.begin() + index, std::move(child));
	return raw;
}

std::unique_ptr<Node> GroupNode::RemoveChild(Node* child)
{
	ChangeTracker::Bump();
	int i = IndexOf(child);
	if (i < 0) return nullptr;
	auto owned = std::move(m_children[i]);
	m_children.erase(m_children.begin() + i);
	owned->m_parent = nullptr;
	return owned;
}

int GroupNode::IndexOf(const Node* child) const
{
	for (int i = 0; i < static_cast<int>(m_children.size()); ++i)
		if (m_children[i].get() == child) return i;
	return -1;
}

void GroupNode::BringToFront(Node* child)
{
	ChangeTracker::Bump();
	int i = IndexOf(child);
	if (i < 0 || i == static_cast<int>(m_children.size()) - 1) return;
	std::rotate(m_children.begin() + i, m_children.begin() + i + 1, m_children.end());
}

void GroupNode::SendToBack(Node* child)
{
	ChangeTracker::Bump();
	int i = IndexOf(child);
	if (i <= 0) return;
	std::rotate(m_children.begin(), m_children.begin() + i, m_children.begin() + i + 1);
}

void GroupNode::BringForward(Node* child)
{
	ChangeTracker::Bump();
	int i = IndexOf(child);
	if (i < 0 || i + 1 >= static_cast<int>(m_children.size())) return;
	std::swap(m_children[i], m_children[i + 1]);
}

void GroupNode::SendBackward(Node* child)
{
	ChangeTracker::Bump();
	int i = IndexOf(child);
	if (i <= 0) return;
	std::swap(m_children[i], m_children[i - 1]);
}

// ---- Document ---------------------------------------------------------------
Document::Document()
{
	AddLayer(L"Layer 1");
}

void Document::Clear(bool addDefaultLayer)
{
	ChangeTracker::Bump();
	m_selection.clear();
	m_layers.clear();
	m_activeLayer = 0;
	m_nextId = 0;
	if (addDefaultLayer) {
		AddLayer(L"Layer 1");
	}
}

LayerNode* Document::AddLayer(const std::wstring& name, int index)
{
	ChangeTracker::Bump();
	auto layer = std::make_unique<LayerNode>();
	layer->m_id = IssueId();
	layer->SetName(name);
	LayerNode* raw = layer.get();
	if (index < 0 || index >= static_cast<int>(m_layers.size()))
		m_layers.push_back(std::move(layer));
	else
		m_layers.insert(m_layers.begin() + index, std::move(layer));
	return raw;
}

bool Document::RemoveLayer(LayerNode* layer)
{
	if (m_layers.size() <= 1) return false;   // 최소 1개 유지
	for (size_t i = 0; i < m_layers.size(); ++i) {
		if (m_layers[i].get() != layer) continue;
		ChangeTracker::Bump();
		m_layers.erase(m_layers.begin() + i);
		if (m_activeLayer >= static_cast<int>(m_layers.size()))
			m_activeLayer = static_cast<int>(m_layers.size()) - 1;
		ClearSelection();
		return true;
	}
	return false;
}

void Document::MoveLayer(LayerNode* layer, int newIndex)
{
	int from = -1;
	for (int i = 0; i < (int)m_layers.size(); ++i) if (m_layers[i].get() == layer) from = i;
	if (from < 0) return;
	ChangeTracker::Bump();
	LayerNode* active = GetActiveLayer();
	newIndex = (std::max)(0, (std::min)(newIndex, (int)m_layers.size() - 1));
	auto owned = std::move(m_layers[from]);
	m_layers.erase(m_layers.begin() + from);
	m_layers.insert(m_layers.begin() + newIndex, std::move(owned));
	for (int i = 0; i < (int)m_layers.size(); ++i) if (m_layers[i].get() == active) m_activeLayer = i;
}

bool Document::MoveNodeTo(Node* node, GroupNode* newParent, int index)
{
	if (!node || !newParent || node->Kind() == NodeKind::Layer) return false;
	GroupNode* oldParent = node->GetParent();
	if (!oldParent) return false;
	for (const GroupNode* p = newParent; p; p = p->GetParent())
		if (p == node) return false;   // 자기 자신/후손 안으로는 이동 불가

	// 마스크 도형 자체를 클리핑 그룹 밖으로 빼면 마스크가 없어지므로 클리핑 해제 (다른 자식이 마스크가 되는 것을 방지)
	if (oldParent->IsClippingMask() && oldParent->GetMaskNode() == node && oldParent != newParent)
		oldParent->SetClippingMask(false);
	Node* pinnedMask = (newParent->IsClippingMask() && newParent->GetMaskNode() != node) ? newParent->GetMaskNode() : nullptr;

	const Matrix2D world = node->GetWorldTransform();
	const int from = oldParent->IndexOf(node);
	if (index >= 0 && oldParent == newParent && from < index) --index;   // 제거로 밀리는 만큼 보정

	auto owned = oldParent->RemoveChild(node);
	if (!owned) return false;
	newParent->AddChild(std::move(owned), index);
	// 클리핑 그룹 안으로 들어온 노드가 마스크 자리(맨 위)를 차지하지 않도록 기존 마스크를 맨 위로 고정
	if (pinnedMask && newParent->GetChildren().back().get() != pinnedMask)
		newParent->BringToFront(pinnedMask);

	if (oldParent != newParent) {
		Matrix2D parentInv = newParent->GetWorldTransform();
		if (parentInv.Invert()) node->SetTransform(world * parentInv);   // local = world * inverse(새 부모 월드)
	}
	return true;
}

LayerNode* Document::GetActiveLayer() const
{
	if (m_activeLayer < 0 || m_activeLayer >= static_cast<int>(m_layers.size())) return nullptr;
	return m_layers[m_activeLayer].get();
}

void Document::SetActiveLayer(int index)
{
	if (index >= 0 && index < static_cast<int>(m_layers.size())) m_activeLayer = index;
}

std::vector<FrameNode*> Document::GetArtboards() const
{
	std::vector<FrameNode*> artboards;
	for (const auto& layer : m_layers)
	{
		if (!layer) continue;
		for (const auto& child : layer->GetChildren())
		{
			if (child && child->Kind() == NodeKind::Group)
			{
				if (auto* frame = dynamic_cast<FrameNode*>(child.get()))
				{
					if (frame->IsUiComponent()) continue;   // UI 컴포넌트는 아트보드가 아님
					artboards.push_back(frame);
				}
			}
		}
	}
	return artboards;
}

bool Document::CanIsolate(const Node* n)
{
	if (!n || n->Kind() != NodeKind::Group) return false;
	if (dynamic_cast<const FrameNode*>(n) || dynamic_cast<const TableNode*>(n)) return false;
	return !static_cast<const GroupNode*>(n)->IsClippingMask();
}

GroupNode* Document::GetIsolationGroup()
{
	if (m_isolationId == 0) return nullptr;
	Node* n = FindById(m_isolationId);
	if (!n || !CanIsolate(n)) { m_isolationId = 0; return nullptr; }
	return static_cast<GroupNode*>(n);
}

bool Document::EnterIsolation(GroupNode* g)
{
	if (!CanIsolate(g)) return false;
	m_isolationId = g->GetId();
	m_selection.clear();
	return true;
}

void Document::ExitIsolation()
{
	GroupNode* g = GetIsolationGroup();
	if (!g) return;
	GroupNode* p = g->GetParent();
	// 상위에 격리 가능한 그룹이 있으면 한 단계만 올라감
	while (p && p->Kind() != NodeKind::Layer && !CanIsolate(p)) p = p->GetParent();
	m_isolationId = (p && p->Kind() != NodeKind::Layer) ? p->GetId() : 0;
	m_selection.clear();
}

std::vector<GroupNode*> Document::IsolationPath()
{
	std::vector<GroupNode*> path;
	for (GroupNode* p = GetIsolationGroup(); p; p = p->GetParent()) path.push_back(p);
	std::reverse(path.begin(), path.end());
	return path;
}

Node* Document::AddShape(std::unique_ptr<Node> node)
{
	LayerNode* layer = GetActiveLayer();
	if (!layer || !node) return nullptr;
	AssignIds(*node);
	return layer->AddChild(std::move(node));
}

static FrameNode* DeepestFrameAt(const GroupNode& g, Vec2 w, const Node* exclude)
{
	const auto& kids = g.GetChildren();
	for (size_t i = kids.size(); i-- > 0;) {
		Node* c = kids[i].get();
		if (!c || c == exclude || !c->IsVisible()) continue;
		auto* gc = dynamic_cast<GroupNode*>(c);
		if (!gc || dynamic_cast<TableNode*>(c) || gc->IsClippingMask()) continue;
		if (IsAtomicComponent(c)) continue;   // 버튼/UI 컴포넌트는 컨테이너가 아님 (안으로 흡수되면 재구성 때 사라짐)
		if (auto* fr = dynamic_cast<FrameNode*>(gc)) {
			Matrix2D inv = fr->GetWorldTransform();
			if (!inv.Invert()) continue;
			const Vec2 l = inv.TransformPoint(w);
			if (l.x < 0.0f || l.y < 0.0f || l.x > fr->GetWidth() || l.y > fr->GetHeight()) continue;
			if (FrameNode* deeper = DeepestFrameAt(*fr, w, exclude)) return deeper;
			return fr;
		}
		if (FrameNode* d = DeepestFrameAt(*gc, w, exclude)) return d;   // 일반 그룹은 통과
	}
	return nullptr;
}

FrameNode* Document::FindFrameAt(Vec2 worldPt, const Node* exclude) const
{
	for (size_t i = m_layers.size(); i-- > 0;) {
		const LayerNode* layer = m_layers[i].get();
		if (!layer || !layer->IsVisible()) continue;
		if (FrameNode* f = DeepestFrameAt(*layer, worldPt, exclude)) return f;
	}
	return nullptr;
}

void Document::RelayoutFrameChain(GroupNode* from)
{
	for (GroupNode* p = from; p; p = p->GetParent())
		if (auto* f = dynamic_cast<FrameNode*>(p)) f->ApplyLayout();
}

Node* Document::AddShapeAt(std::unique_ptr<Node> node, Vec2 worldPt)
{
	if (!node) return nullptr;
	FrameNode* parent = FindFrameAt(worldPt);
	if (!parent) return AddShape(std::move(node));
	AssignIds(*node);
	const Matrix2D world = node->GetTransform();   // 아직 부모가 없으므로 월드 좌표 기준
	Matrix2D inv = parent->GetWorldTransform();
	if (inv.Invert()) node->SetTransform(world * inv);
	Node* added = parent->AddChild(std::move(node));
	parent->ClampChild(added, false);   // 부모 범위를 넘지 않게
	RelayoutFrameChain(parent);
	return added;
}

Node* Document::AddShapeAuto(std::unique_ptr<Node> node)
{
	if (!node) return nullptr;
	Rect2 lb;
	Vec2 c{ 0.0f, 0.0f };
	if (node->GetLocalBounds(lb)) {
		const Rect2 b = node->GetTransform().TransformRect(lb);
		c = Vec2{ (b.left + b.right) * 0.5f, (b.top + b.bottom) * 0.5f };
	} else {
		c = Vec2{ node->GetTransform().dx, node->GetTransform().dy };
	}
	return AddShapeAt(std::move(node), c);
}

bool Document::ReparentAt(Node* node, Vec2 worldPt)
{
	if (!node || node->Kind() == NodeKind::Layer) return false;
	GroupNode* oldParent = node->GetParent();
	if (!oldParent) return false;
	// 그룹/표/클리핑 그룹 안의 자식은 구조를 유지 (레이어 직속이거나 프레임 안의 노드만 대상)
	if (oldParent->Kind() != NodeKind::Layer && !dynamic_cast<FrameNode*>(oldParent)) return false;

	FrameNode* target = FindFrameAt(worldPt, node);
	GroupNode* dest = target;
	if (!dest) {
		if (oldParent->Kind() == NodeKind::Layer) return false;   // 이미 레이어 직속
		GroupNode* p = oldParent;
		while (p && p->Kind() != NodeKind::Layer) p = p->GetParent();
		dest = p;
	}
	if (!dest || dest == oldParent) return false;
	if (!MoveNodeTo(node, dest, -1)) return false;
	FrameNode::ClampNode(node, false);
	RelayoutFrameChain(oldParent);
	RelayoutFrameChain(dest);
	return true;
}

std::wstring Document::GenerateNodeName(ShapeKind kind) const
{
	std::wstring prefix = L"path";
	switch (kind)
	{
	case ShapeKind::Rect:     prefix = L"rect"; break;
	case ShapeKind::Ellipse:  prefix = L"ellipse"; break;
	case ShapeKind::Triangle: prefix = L"triangle"; break;
	case ShapeKind::Polygon:  prefix = L"polygon"; break;
	case ShapeKind::Star:     prefix = L"star"; break;
	case ShapeKind::Gear:       prefix = L"gear"; break;
	case ShapeKind::Spiral:     prefix = L"spiral"; break;
	case ShapeKind::Heart:      prefix = L"heart"; break;
	case ShapeKind::Arc:        prefix = L"arc"; break;
	case ShapeKind::BlockArrow: prefix = L"arrow"; break;
	case ShapeKind::Callout:    prefix = L"callout"; break;
	case ShapeKind::Path:
	default:
		prefix = L"path";
		break;
	}

	return GenerateNameWithPrefix(prefix);
}

// "<prefix><N>" 형식의 이름 중 문서 전체에서 N 이 가장 큰 값 + 1 로 새 이름 생성 (rect1, group2 ...)
std::wstring Document::GenerateNameWithPrefix(const std::wstring& prefix) const
{
	// 문서 내 모든 레이어의 자식 노드들을 검색하여 기존 번호 최대값 + 1 결정
	int maxIdx = 0;
	std::function<void(const Node*)> scanNode = [&](const Node* n) {
		if (!n) return;
		const std::wstring& name = n->GetName();
		if (name.rfind(prefix, 0) == 0) // prefix로 시작
		{
			std::wstring numPart = name.substr(prefix.length());
			if (!numPart.empty())
			{
				wchar_t* endPtr = nullptr;
				long val = std::wcstol(numPart.c_str(), &endPtr, 10);
				if (endPtr && *endPtr == L'\0' && val > maxIdx)
				{
					maxIdx = static_cast<int>(val);
				}
			}
		}

		if (n->Kind() == NodeKind::Group || n->Kind() == NodeKind::Layer)
		{
			const auto* g = static_cast<const GroupNode*>(n);
			for (const auto& child : g->GetChildren())
			{
				scanNode(child.get());
			}
		}
	};

	for (const auto& layer : m_layers)
	{
		scanNode(layer.get());
	}

	return prefix + std::to_wstring(maxIdx + 1);
}

std::wstring Document::GenerateGroupName() const
{
	return GenerateNameWithPrefix(L"group");
}

std::wstring Document::GenerateFrameName() const
{
	return GenerateNameWithPrefix(L"Frame ");
}

std::wstring Document::GenerateLayerName() const
{
	const std::wstring prefix = L"Layer ";
	int maxIdx = 0;
	for (const auto& layer : m_layers)
	{
		const std::wstring& name = layer->GetName();
		if (name.rfind(prefix, 0) == 0)
		{
			std::wstring numPart = name.substr(prefix.length());
			if (!numPart.empty())
			{
				wchar_t* endPtr = nullptr;
				long val = std::wcstol(numPart.c_str(), &endPtr, 10);
				if (endPtr && *endPtr == L'\0' && val > maxIdx)
				{
					maxIdx = static_cast<int>(val);
				}
			}
		}
	}
	return prefix + std::to_wstring(maxIdx + 1);
}


void Document::AssignIds(Node& node)
{
	if (node.m_id == 0) { node.m_id = IssueId(); ChangeTracker::Bump(); }
	if (node.Kind() == NodeKind::Group || node.Kind() == NodeKind::Layer)
		for (const auto& c : static_cast<GroupNode&>(node).GetChildren()) AssignIds(*c);
}

void Document::RestoreNodeId(Node& node, NodeId id)
{
	if (id == 0) return;
	node.m_id = id;
	ChangeTracker::Bump();
	if (id > m_nextId) m_nextId = id;
}

void Document::EnsureUniqueIds()
{
	ChangeTracker::Bump();
	std::unordered_set<NodeId> used;
	std::function<void(Node&)> walk = [&](Node& n) {
		if (n.m_id == 0 || !used.insert(n.m_id).second) {
			n.m_id = IssueId();                // 0 또는 중복 -> 새 ID (카운터는 항상 기존 최대값보다 큼)
			used.insert(n.m_id);
		}
		if (n.Kind() == NodeKind::Group || n.Kind() == NodeKind::Layer)
			for (const auto& c : static_cast<GroupNode&>(n).GetChildren()) walk(*c);
	};
	for (const auto& layer : m_layers) walk(*layer);
}

static Node* FindIn(Node& n, NodeId id)
{
	if (n.GetId() == id) return &n;
	if (n.Kind() == NodeKind::Group || n.Kind() == NodeKind::Layer)
		for (const auto& c : static_cast<GroupNode&>(n).GetChildren())
			if (Node* r = FindIn(*c, id)) return r;
	return nullptr;
}

Node* Document::FindById(NodeId id) const
{
	for (const auto& l : m_layers) if (Node* r = FindIn(*l, id)) return r;
	return nullptr;
}

std::unique_ptr<Node> Document::RemoveNode(Node* node)
{
	if (!node || !node->GetParent()) return nullptr;
	m_selection.erase(std::remove(m_selection.begin(), m_selection.end(), node), m_selection.end());
	return node->GetParent()->RemoveChild(node);
}

void Document::ArrangeSelection(ZOrder order)
{
	if (m_selection.empty()) return;

	// 부모별로 선택 노드들을 그룹화
	std::unordered_map<GroupNode*, std::vector<Node*>> groupedByParent;
	for (Node* n : m_selection) {
		if (n && n->GetParent()) {
			groupedByParent[n->GetParent()].push_back(n);
		}
	}

	for (auto& [parent, nodes] : groupedByParent) {
		if (!parent || nodes.empty()) continue;

		// 인덱스 순 정렬 (0 = 맨 아래, N-1 = 맨 위)
		std::sort(nodes.begin(), nodes.end(), [&](Node* a, Node* b) {
			return parent->IndexOf(a) < parent->IndexOf(b);
		});

		auto isNodeSelected = [&](Node* n) {
			return std::find(nodes.begin(), nodes.end(), n) != nodes.end();
		};

		// 클리핑 그룹의 마스크 = 마지막(최상단) 자식. 다른 자식이 맨 위로 올라가도 마스크가 바뀌지 않게 끝나고 다시 맨 위로 고정
		Node* pinnedMask = parent->IsClippingMask() ? parent->GetMaskNode() : nullptr;

		switch (order) {
		case ZOrder::Front:
			// Front: 아래에 있던 것부터 순서대로 Front로 보내면 원래 상대 순서 유지
			for (Node* n : nodes) {
				parent->BringToFront(n);
			}
			break;
		case ZOrder::Back:
			// Back: 위에 있던 것부터 순서대로 Back으로 보내면 원래 상대 순서 유지
			for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
				parent->SendToBack(*it);
			}
			break;
		case ZOrder::Forward:
			// Forward: 맨 위(인덱스 큰 쪽)부터 처리. 단 바로 위 이웃도 선택된 상태면 swap하지 않음
			for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
				Node* n = *it;
				int idx = parent->IndexOf(n);
				if (idx >= 0 && idx + 1 < (int)parent->GetChildren().size()) {
					Node* nextNeighbor = parent->GetChildren()[idx + 1].get();
					if (!isNodeSelected(nextNeighbor)) {
						parent->BringForward(n);
					}
				}
			}
			break;
		case ZOrder::Backward:
			// Backward: 맨 아래(인덱스 작은 쪽)부터 처리. 단 바로 아래 이웃도 선택된 상태면 swap하지 않음
			for (Node* n : nodes) {
				int idx = parent->IndexOf(n);
				if (idx > 0) {
					Node* prevNeighbor = parent->GetChildren()[idx - 1].get();
					if (!isNodeSelected(prevNeighbor)) {
						parent->SendBackward(n);
					}
				}
			}
			break;
		}

		if (pinnedMask && !parent->GetChildren().empty() && parent->GetChildren().back().get() != pinnedMask)
			parent->BringToFront(pinnedMask);
	}
}

bool Document::GetWorldBounds(Node& node, Rect2& out)
{
	Rect2 b;
	if (!node.GetLocalBounds(b)) return false;
	out = node.GetWorldTransform().TransformRect(b);
	return true;
}

bool Document::GetVisualWorldBounds(Node& node, Rect2& out)
{
	if (!GetWorldBounds(node, out)) return false;
	const Style& st = node.GetStyle();
	if (st.stroke.width > 0.0f && st.stroke.color.a > 0.0f) {
		const Matrix2D wxf = node.GetWorldTransform();
		const float sx = std::sqrt(wxf.m11 * wxf.m11 + wxf.m12 * wxf.m12);
		const float sy = std::sqrt(wxf.m21 * wxf.m21 + wxf.m22 * wxf.m22);
		const float halfStroke = st.stroke.width * 0.5f * (std::max)(sx, sy);
		out.left   -= halfStroke;
		out.top    -= halfStroke;
		out.right  += halfStroke;
		out.bottom += halfStroke;
	}
	return true;
}

bool Document::GetSelectionBounds(Rect2& out) const
{
	bool any = false;
	for (Node* n : m_selection) {
		Rect2 b;
		if (!GetWorldBounds(*n, b)) continue;
		if (!any) { out = b; any = true; } else out.Union(b);
	}
	return any;
}

// 그룹화 대상(레이어/부모 없는 노드 제외, 조상이 함께 선택된 노드 제외)이 2개 이상인지
bool Document::CanGroupSelection() const
{
	std::vector<const Node*> sel;
	for (const Node* n : m_selection)
		if (n && n->GetParent() && n->Kind() != NodeKind::Layer) sel.push_back(n);
	int count = 0;
	for (const Node* n : sel) {
		bool ancestorSelected = false;
		for (const GroupNode* p = n->GetParent(); p && !ancestorSelected; p = p->GetParent())
			if (std::find(sel.begin(), sel.end(), static_cast<const Node*>(p)) != sel.end()) ancestorSelected = true;
		if (!ancestorSelected) ++count;
	}
	return count >= 2;
}

// 프레임으로 감쌀 수 있는 유효 대상(레이어/부모 없는 노드 제외)이 1개 이상인지
bool Document::CanWrapInFrame() const
{
	std::vector<const Node*> sel;
	for (const Node* n : m_selection)
		if (n && n->GetParent() && n->Kind() != NodeKind::Layer) sel.push_back(n);
	int count = 0;
	for (const Node* n : sel) {
		bool ancestorSelected = false;
		for (const GroupNode* p = n->GetParent(); p && !ancestorSelected; p = p->GetParent())
			if (std::find(sel.begin(), sel.end(), static_cast<const Node*>(p)) != sel.end()) ancestorSelected = true;
		if (!ancestorSelected) ++count;
	}
	return count >= 1;
}

// 순서를 바꿀 수 있는 선택(레이어 제외, 부모가 있는 노드)이 하나라도 있는지
bool Document::CanArrangeSelection() const
{
	for (const Node* n : m_selection)
		if (n && n->GetParent() && n->Kind() != NodeKind::Layer) return true;
	return false;
}

// 선택에 그룹(레이어 제외)이 하나라도 있는지
bool Document::CanUngroupSelection() const
{
	for (const Node* n : m_selection)
		if (n && n->Kind() == NodeKind::Group && n->GetParent()) return true;
	return false;
}

bool Document::CanBooleanOperation() const
{
	int pathCount = 0;
	for (const Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path && n->GetParent()) {
			++pathCount;
		}
	}
	return pathCount >= 2;
}

#ifndef GE_NO_D2D
PathNode* Document::BooleanOperation(BooleanMode mode, ID2D1Factory* factory)
{
	if (!factory || !CanBooleanOperation()) return nullptr;

	// 1) 대상 PathNode 수집 (부모가 있고 유효한 PathNode)
	std::vector<PathNode*> paths;
	for (Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path && n->GetParent()) {
			paths.push_back(static_cast<PathNode*>(n));
		}
	}
	if (paths.size() < 2) return nullptr;

	// 2) 문서 Z-순서(레이어 -> 자식 인덱스 경로)로 정렬: 최하단(0) -> 최상단(N-1)
	struct OrderHelper {
		static std::vector<int> GetKey(Node* n, const std::vector<std::unique_ptr<LayerNode>>& layers) {
			std::vector<int> key;
			Node* c = n;
			while (c->GetParent()) { key.push_back(c->GetParent()->IndexOf(c)); c = c->GetParent(); }
			int layerIdx = 0;
			for (size_t i = 0; i < layers.size(); ++i) if (layers[i].get() == c) layerIdx = static_cast<int>(i);
			key.push_back(layerIdx);
			std::reverse(key.begin(), key.end());
			return key;
		}
	};
	std::stable_sort(paths.begin(), paths.end(), [&](PathNode* a, PathNode* b) {
		return OrderHelper::GetKey(a, m_layers) < OrderHelper::GetKey(b, m_layers);
	});

	// 최하단 도형(paths[0])이 스타일 기준
	// 연산 순차 적용: acc = Combine(paths[0], paths[1]), acc = Combine(acc, paths[2]), ...
	std::unique_ptr<PathNode> acc = PathOp::Combine(*paths[0], *paths[1], mode, factory);
	if (!acc) return nullptr;

	for (size_t i = 2; i < paths.size(); ++i) {
		acc = PathOp::Combine(*acc, *paths[i], mode, factory);
		if (!acc) return nullptr;
	}

	// 3) 새 PathNode 는 최하단 도형이 있던 부모와 위치에 삽입
	PathNode* baseNode = paths[0];
	GroupNode* targetParent = baseNode->GetParent();
	int insertIndex = targetParent->IndexOf(baseNode);

	// ID 발급 및 이름 생성
	acc->m_id = this->IssueId();
	acc->SetName(this->GenerateNodeName(ShapeKind::Path));

	// 월드 좌표계로 생성된 결과를 대상 부모의 로컬 좌표계로 역변환
	Matrix2D parentInv = targetParent->GetWorldTransform();
	if (parentInv.Invert()) {
		acc->SetTransform(parentInv);
	} else {
		acc->SetTransform(Matrix2D::Identity());
	}

	// 4) 원본 도형들 문서에서 제거
	for (PathNode* p : paths) {
		GroupNode* pParent = p->GetParent();
		if (pParent) {
			pParent->RemoveChild(p);
		}
	}

	// 5) 새 도형 삽입 및 선택 갱신
	// insertIndex 범위 보정
	if (insertIndex < 0 || insertIndex > static_cast<int>(targetParent->GetChildren().size())) {
		insertIndex = static_cast<int>(targetParent->GetChildren().size());
	}
	Node* inserted = targetParent->AddChild(std::move(acc), insertIndex);
	PathNode* resultPath = static_cast<PathNode*>(inserted);

	m_selection = std::vector<Node*>{ resultPath };
	return resultPath;
}
#endif

GroupNode* Document::GroupSelection()
{
	// 1) 대상 정리: 레이어/부모 없는 노드 제외, 조상이 함께 선택된 노드는 조상 그룹에 이미 포함되므로 제외
	std::vector<Node*> sel;
	for (Node* n : m_selection)
		if (n && n->GetParent() && n->Kind() != NodeKind::Layer) sel.push_back(n);
	auto hasSelectedAncestor = [&](Node* n) {
		for (GroupNode* p = n->GetParent(); p; p = p->GetParent())
			if (std::find(sel.begin(), sel.end(), p) != sel.end()) return true;
		return false;
	};
	sel.erase(std::remove_if(sel.begin(), sel.end(), hasSelectedAncestor), sel.end());
	if (sel.size() < 2) return nullptr;

	// 2) 문서 순서(레이어 -> 자식 인덱스 경로)로 정렬: 맨 아래 -> 맨 위. 부모가 서로 달라도(다른 레이어/그룹) 그룹화 가능
	auto orderKey = [&](Node* n) {
		std::vector<int> key;
		Node* c = n;
		while (c->GetParent()) { key.push_back(c->GetParent()->IndexOf(c)); c = c->GetParent(); }
		int layerIdx = 0;
		for (size_t i = 0; i < m_layers.size(); ++i) if (m_layers[i].get() == c) layerIdx = static_cast<int>(i);
		key.push_back(layerIdx);
		std::reverse(key.begin(), key.end());
		return key;
	};
	std::stable_sort(sel.begin(), sel.end(), [&](Node* a, Node* b) { return orderKey(a) < orderKey(b); });

	// 3) 새 그룹은 "맨 위 노드가 있던 부모/위치"에 생성 (그 부모에서 빠져나가는 앞쪽 형제 수만큼 인덱스 보정)
	Node* top = sel.back();
	GroupNode* parent = top->GetParent();
	int insertAt = parent->IndexOf(top);
	for (Node* n : sel)
		if (n != top && n->GetParent() == parent && parent->IndexOf(n) < parent->IndexOf(top)) --insertAt;

	// 4) 월드 변환을 기억한 채 분리 -> 새 그룹(항등 변환)에 넣고, 부모가 달라져도 화면 위치가 변하지 않게 로컬 변환 보정
	struct Item { std::unique_ptr<Node> node; Matrix2D world; };
	std::vector<Item> items;
	for (Node* n : sel) {
		Matrix2D w = n->GetWorldTransform();
		items.push_back({ n->GetParent()->RemoveChild(n), w });
	}

	auto g = std::make_unique<GroupNode>();
	g->m_id = IssueId();
	g->SetName(GenerateGroupName());
	GroupNode* raw = g.get();
	parent->AddChild(std::move(g), insertAt);

	Matrix2D parentInv = raw->GetWorldTransform();   // 새 그룹은 항등 변환이므로 부모 월드 변환과 같음
	const bool invertible = parentInv.Invert();
	for (Item& it : items) {
		if (invertible) it.node->SetTransform(it.world * parentInv);   // local = world * inverse(부모 월드)
		raw->AddChild(std::move(it.node));
	}

	m_selection = { raw };
	return raw;
}

FrameNode* Document::WrapSelectionInFrame()
{
	if (!CanWrapInFrame()) return nullptr;

	// 1) 대상 정리
	std::vector<Node*> sel;
	for (Node* n : m_selection)
		if (n && n->GetParent() && n->Kind() != NodeKind::Layer) sel.push_back(n);
	auto hasSelectedAncestor = [&](Node* n) {
		for (GroupNode* p = n->GetParent(); p; p = p->GetParent())
			if (std::find(sel.begin(), sel.end(), p) != sel.end()) return true;
		return false;
	};
	sel.erase(std::remove_if(sel.begin(), sel.end(), hasSelectedAncestor), sel.end());
	if (sel.empty()) return nullptr;

	// 2) 선택 노드들의 전체 월드 바운딩 박스 계산
	Rect2 unionBounds{};
	bool hasBounds = false;
	for (Node* n : sel) {
		Rect2 b{};
		if (GetWorldBounds(*n, b)) {
			if (!hasBounds) { unionBounds = b; hasBounds = true; }
			else {
				unionBounds.left   = (std::min)(unionBounds.left, b.left);
				unionBounds.top    = (std::min)(unionBounds.top, b.top);
				unionBounds.right  = (std::max)(unionBounds.right, b.right);
				unionBounds.bottom = (std::max)(unionBounds.bottom, b.bottom);
			}
		}
	}
	if (!hasBounds) return nullptr;

	const float frameW = (std::max)(1.0f, unionBounds.right - unionBounds.left);
	const float frameH = (std::max)(1.0f, unionBounds.bottom - unionBounds.top);

	// 3) 문서 순서 정렬
	auto orderKey = [&](Node* n) {
		std::vector<int> key;
		Node* c = n;
		while (c->GetParent()) { key.push_back(c->GetParent()->IndexOf(c)); c = c->GetParent(); }
		int layerIdx = 0;
		for (size_t i = 0; i < m_layers.size(); ++i) if (m_layers[i].get() == c) layerIdx = static_cast<int>(i);
		key.push_back(layerIdx);
		std::reverse(key.begin(), key.end());
		return key;
	};
	std::stable_sort(sel.begin(), sel.end(), [&](Node* a, Node* b) { return orderKey(a) < orderKey(b); });

	// 4) 새 프레임 생성 위치 (최상단 노드가 있던 부모/인덱스)
	Node* top = sel.back();
	GroupNode* parent = top->GetParent();
	int insertAt = parent->IndexOf(top);
	for (Node* n : sel)
		if (n != top && n->GetParent() == parent && parent->IndexOf(n) < parent->IndexOf(top)) --insertAt;

	// 5) 자식 노드 분리
	struct Item { std::unique_ptr<Node> node; Matrix2D world; };
	std::vector<Item> items;
	for (Node* n : sel) {
		Matrix2D w = n->GetWorldTransform();
		items.push_back({ n->GetParent()->RemoveChild(n), w });
	}

	// 6) FrameNode 생성 (바운딩 박스의 좌상단 위치로 변환, 크기 설정)
	auto fn = std::make_unique<FrameNode>();
	fn->m_id = IssueId();
	fn->SetName(GenerateFrameName());
	fn->SetFrameSize(frameW, frameH);

	// 프레임의 월드 변환 = 평행 이동(unionBounds.left, unionBounds.top)
	// parent 내부 로컬 좌표로 변환: frameLocal = frameWorld * inverse(parentWorld)
	Matrix2D parentWorld = parent->GetWorldTransform();
	Matrix2D parentInv = parentWorld;
	Matrix2D frameWorld = Matrix2D::Translation(unionBounds.left, unionBounds.top);
	if (parentInv.Invert()) {
		fn->SetTransform(frameWorld * parentInv);
	} else {
		fn->SetTransform(frameWorld);
	}

	FrameNode* raw = fn.get();
	parent->AddChild(std::move(fn), insertAt);

	// 7) 자식들을 새 프레임 내부로 삽입 (프레임 원점 (unionBounds.left, unionBounds.top) 기준 로컬 좌표로 보정)
	Matrix2D newFrameWorld = raw->GetWorldTransform();
	Matrix2D newFrameInv = newFrameWorld;
	const bool invertible = newFrameInv.Invert();
	for (Item& it : items) {
		if (invertible) it.node->SetTransform(it.world * newFrameInv);
		raw->AddChild(std::move(it.node));
	}

	m_selection = { raw };
	return raw;
}

void Document::UngroupSelection()
{
	std::vector<Node*> result;
	for (Node* n : m_selection) {
		if (n->Kind() != NodeKind::Group || !n->GetParent()) { result.push_back(n); continue; }
		GroupNode* g = static_cast<GroupNode*>(n);
		GroupNode* parent = g->GetParent();
		int at = parent->IndexOf(g);
		const Matrix2D gx = g->GetTransform();
		const float groupOpacity = g->GetOpacity();
		std::vector<Node*> kids;
		for (const auto& c : g->GetChildren()) kids.push_back(c.get());
		auto owned = parent->RemoveChild(g);
		for (Node* k : kids) {
			auto o = g->RemoveChild(k);
			o->SetTransform(o->GetTransform() * gx);
			o->SetOpacity(o->GetOpacity() * groupOpacity);
			Node* added = parent->AddChild(std::move(o), at++);
			result.push_back(added);
		}
	}
	m_selection = result;
}

bool Document::CanMakeClipMask() const
{
	// 2개 이상의 노드가 선택되어 있고, 그 중 최상단 노드가 PathNode(마스크 도형)일 때
	return CanGroupSelection();
}

bool Document::CanReleaseClipMask() const
{
	// 선택 항목 중에 클리핑 마스크 그룹이 있거나, 클리핑 마스크 그룹의 자식이 선택되어 있는 경우
	for (const Node* n : m_selection) {
		if (!n) continue;
		if (n->Kind() == NodeKind::Group && static_cast<const GroupNode*>(n)->IsClippingMask())
			return true;
		const GroupNode* p = n->GetParent();
		if (p && p->IsClippingMask())
			return true;
	}
	return false;
}

GroupNode* Document::MakeClipMask()
{
	// 1) 2개 이상의 선택 항목을 하나의 그룹으로 묶음
	GroupNode* g = GroupSelection();
	if (!g) return nullptr;

	// 2) 그룹을 클리핑 마스크로 설정
	g->SetClippingMask(true);
	ChangeTracker::Bump();
	return g;
}

void Document::ReleaseClipMask()
{
	std::vector<GroupNode*> maskGroups;
	for (Node* n : m_selection) {
		if (!n) continue;
		if (n->Kind() == NodeKind::Group && static_cast<GroupNode*>(n)->IsClippingMask()) {
			if (std::find(maskGroups.begin(), maskGroups.end(), static_cast<GroupNode*>(n)) == maskGroups.end())
				maskGroups.push_back(static_cast<GroupNode*>(n));
		}
		GroupNode* p = n->GetParent();
		if (p && p->IsClippingMask()) {
			if (std::find(maskGroups.begin(), maskGroups.end(), p) == maskGroups.end())
				maskGroups.push_back(p);
		}
	}

	for (GroupNode* g : maskGroups) {
		g->SetClippingMask(false);
	}

	if (!maskGroups.empty()) {
		ChangeTracker::Bump();
	}
}

bool Document::CanOutlineStroke() const
{
	for (const Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path && n->GetParent()) {
			const PathNode* p = static_cast<const PathNode*>(n);
			if (p->GetStyle().stroke.width > 0.0f && p->GetStyle().stroke.color.a > 0.0f)
				return true;
		}
	}
	return false;
}

bool Document::CanSimplify() const
{
	for (const Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path) return true;
	}
	return false;
}

bool Document::CanReversePath() const
{
	for (const Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path) return true;
	}
	return false;
}

bool Document::CanJoinPaths() const
{
	int pathCount = 0;
	for (const Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path && n->GetParent()) {
			++pathCount;
		}
	}
	return pathCount >= 2;
}

#ifndef GE_NO_D2D
std::vector<PathNode*> Document::OutlineStrokeSelection(ID2D1Factory* factory, float widthOverride)
{
	if (!factory || !CanOutlineStroke()) return {};

	std::vector<PathNode*> targets;
	for (Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path && n->GetParent()) {
			targets.push_back(static_cast<PathNode*>(n));
		}
	}

	std::vector<PathNode*> results;
	for (PathNode* src : targets) {
		GroupNode* parent = src->GetParent();
		if (!parent) continue;

		int idx = parent->IndexOf(src);
		auto outlined = PathOp::OutlineStroke(*src, factory, D2D1_DEFAULT_FLATTENING_TOLERANCE, widthOverride);
		if (!outlined) continue;

		outlined->m_id = IssueId();
		outlined->SetName(src->GetName() + L" Outline");

		// 보이는 채우기가 있으면 원본은 "채우기만" 남기고(선 제거) 외곽선 면을 그 위에 추가 (일러스트의 외곽선 만들기와 같은 동작)
		const Style& srcStyle = src->GetStyle();
		const bool hasFill = srcStyle.fill.type != PaintType::None && (srcStyle.fill.type != PaintType::Solid || srcStyle.fill.color.a > 0.0f);
		int insertAt = idx;
		if (hasFill) {
			Style fillOnly = srcStyle;
			fillOnly.stroke.width = 0.0f;
			src->SetStyle(fillOnly);
			insertAt = idx + 1;
		} else {
			parent->RemoveChild(src);
		}
		Node* inserted = parent->AddChild(std::move(outlined), insertAt);
		results.push_back(static_cast<PathNode*>(inserted));
	}

	if (!results.empty()) {
		m_selection.clear();
		for (PathNode* p : results) m_selection.push_back(p);
		ChangeTracker::Bump();
	}
	return results;
}

bool Document::CanConvertTextToPath() const
{
	for (const Node* n : m_selection)
		if (n && n->Kind() == NodeKind::Text && n->GetParent()) return true;
	return false;
}

std::vector<PathNode*> Document::TextToPathSelection(ID2D1Factory* factory)
{
	std::vector<PathNode*> results;
	if (!factory) return results;
	std::vector<TextNode*> targets;
	for (Node* n : m_selection)
		if (n && n->Kind() == NodeKind::Text && n->GetParent()) targets.push_back(static_cast<TextNode*>(n));

	for (TextNode* src : targets) {
		GroupNode* parent = src->GetParent();
		auto path = PathOp::TextToPath(*src, factory);
		if (!path) continue;
		path->m_id = IssueId();
		path->SetName(src->GetName().empty() ? std::wstring(L"Text Outline") : src->GetName() + L" Outline");
		const int idx = parent->IndexOf(src);
		parent->RemoveChild(src);
		results.push_back(static_cast<PathNode*>(parent->AddChild(std::move(path), idx)));
	}
	if (!results.empty()) {
		m_selection.clear();
		for (PathNode* p : results) m_selection.push_back(p);
		ChangeTracker::Bump();
	}
	return results;
}

std::vector<PathNode*> Document::SmoothSelection(float tolerance)
{
	std::vector<PathNode*> results;
	for (Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path) {
			PathNode* p = static_cast<PathNode*>(n);
			if (PathOp::Smooth(*p, tolerance)) results.push_back(p);
		}
	}
	if (!results.empty()) ChangeTracker::Bump();
	return results;
}

std::vector<PathNode*> Document::SimplifySelection(float tolerance)
{
	std::vector<PathNode*> results;
	for (Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path) {
			PathNode* p = static_cast<PathNode*>(n);
			if (PathOp::Simplify(*p, tolerance)) {
				results.push_back(p);
			}
		}
	}
	if (!results.empty()) {
		ChangeTracker::Bump();
	}
	return results;
}

std::vector<PathNode*> Document::ReversePathSelection()
{
	std::vector<PathNode*> results;
	for (Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path) {
			PathNode* p = static_cast<PathNode*>(n);
			PathOp::ReversePath(*p);
			results.push_back(p);
		}
	}
	if (!results.empty()) {
		ChangeTracker::Bump();
	}
	return results;
}

PathNode* Document::JoinSelection()
{
	if (!CanJoinPaths()) return nullptr;

	std::vector<PathNode*> paths;
	for (Node* n : m_selection) {
		if (n && n->Kind() == NodeKind::Path && n->GetParent()) {
			paths.push_back(static_cast<PathNode*>(n));
		}
	}
	if (paths.size() < 2) return nullptr;

	auto acc = PathOp::Join(*paths[0], *paths[1]);
	if (!acc) return nullptr;

	for (size_t i = 2; i < paths.size(); ++i) {
		acc = PathOp::Join(*acc, *paths[i]);
		if (!acc) return nullptr;
	}

	GroupNode* parent = paths[0]->GetParent();
	int insertIdx = parent->IndexOf(paths[0]);

	acc->m_id = IssueId();
	acc->SetName(paths[0]->GetName() + L" Joined");

	for (PathNode* p : paths) {
		GroupNode* pParent = p->GetParent();
		if (pParent) pParent->RemoveChild(p);
	}

	if (insertIdx < 0 || insertIdx > static_cast<int>(parent->GetChildren().size())) {
		insertIdx = static_cast<int>(parent->GetChildren().size());
	}
	Node* inserted = parent->AddChild(std::move(acc), insertIdx);
	PathNode* resultPath = static_cast<PathNode*>(inserted);

	m_selection = { resultPath };
	ChangeTracker::Bump();
	return resultPath;
}
#endif

std::vector<Node*> Document::DuplicateSelection()
{
	auto isSelected = [&](const Node* n) {
		return std::find(m_selection.begin(), m_selection.end(), n) != m_selection.end();
	};
	std::vector<Node*> result;
	for (Node* n : m_selection) {
		GroupNode* parent = n ? n->GetParent() : nullptr;
		if (!parent) continue;
		// 조상이 함께 선택된 경우 조상 복제에 포함되므로 건너뜀
		bool ancestorSelected = false;
		for (const GroupNode* p = parent; p; p = p->GetParent())
			if (isSelected(p)) { ancestorSelected = true; break; }
		if (ancestorSelected) continue;

		auto copy = n->Clone();
		AssignIds(*copy);
		if (copy->Kind() == NodeKind::Path)
			copy->SetName(GenerateNodeName(static_cast<PathNode&>(*copy).GetKind()));
		result.push_back(parent->AddChild(std::move(copy), parent->IndexOf(n) + 1));
	}
	if (!result.empty()) m_selection = result;
	return result;
}

std::vector<Node*> Document::ImportNodes(const std::vector<const Node*>& sources, GroupNode* target, int index)
{
	std::vector<Node*> result;

	// target 은 이 문서의 노드여야 함 (조상 체인의 루트가 이 문서의 레이어)
	if (target) {
		const GroupNode* root = target;
		while (root->GetParent()) root = root->GetParent();
		bool owned = false;
		for (const auto& l : m_layers) if (l.get() == root) { owned = true; break; }
		if (!owned) return result;
	}
	GroupNode* dest = target ? target : GetActiveLayer();

	// 1) 먼저 전부 복제: 자기 문서에서 가져오거나 target 이 소스의 후손이어도 삽입 중 소스가 바뀌지 않도록
	std::vector<std::unique_ptr<Node>> copies;
	for (const Node* src : sources) {
		if (!src) continue;
		if (src->Kind() == NodeKind::Layer && target) {
			// 레이어를 그룹/레이어 안으로 넣을 수는 없으므로 자식들만 펼침
			for (const auto& c : static_cast<const GroupNode*>(src)->GetChildren())
				if (c) copies.push_back(c->Clone());
		} else {
			copies.push_back(src->Clone());
		}
	}

	// 2) 새 ID 발급 후 삽입. AssignIds 는 id==0 인 노드에만 발급하므로 "Clone 은 ID 를 비운다"는 규약에 의존 (검증은 GeModelPureCheck)
	int at = index;
	for (auto& copy : copies) {
		AssignIds(*copy);
		if (copy->Kind() == NodeKind::Layer) {
			// target == nullptr 일 때만 도달: 새 레이어로 추가
			LayerNode* layer = static_cast<LayerNode*>(copy.release());
			ChangeTracker::Bump();
			if (at < 0 || at >= static_cast<int>(m_layers.size()))
				m_layers.emplace_back(layer);
			else
				m_layers.insert(m_layers.begin() + at, std::unique_ptr<LayerNode>(layer));
			result.push_back(layer);
		} else {
			if (!dest) continue;
			result.push_back(dest->AddChild(std::move(copy), at));
		}
		if (at >= 0) ++at;
	}
	return result;
}

void Document::AlignSelection(AlignMode mode, bool toCanvas)
{
	Rect2 ref;
	if (toCanvas) ref = { 0, 0, width, height };
	else if (m_selection.size() < 2 || !GetSelectionBounds(ref)) return;
	for (Node* n : m_selection) {
		Rect2 b;
		if (!GetWorldBounds(*n, b)) continue;
		float dx = 0, dy = 0;
		switch (mode) {
		case AlignMode::Left:    dx = ref.left - b.left; break;
		case AlignMode::CenterH: dx = ref.MidX() - b.MidX(); break;
		case AlignMode::Right:   dx = ref.right - b.right; break;
		case AlignMode::Top:     dy = ref.top - b.top; break;
		case AlignMode::CenterV: dy = ref.MidY() - b.MidY(); break;
		case AlignMode::Bottom:  dy = ref.bottom - b.bottom; break;
		}
		n->SetTransform(n->GetTransform() * Matrix2D::Translation(dx, dy));
	}
}

void Document::DistributeSelection(bool horizontal)
{
	if (m_selection.size() < 3) return;
	struct Item { Node* n; Rect2 b; };
	std::vector<Item> items;
	for (Node* n : m_selection) { Rect2 b; if (GetWorldBounds(*n, b)) items.push_back({ n, b }); }
	if (items.size() < 3) return;
	std::sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
		return horizontal ? a.b.MidX() < b.b.MidX() : a.b.MidY() < b.b.MidY(); });
	const float first = horizontal ? items.front().b.MidX() : items.front().b.MidY();
	const float last  = horizontal ? items.back().b.MidX()  : items.back().b.MidY();
	const float step = (last - first) / (items.size() - 1);
	for (size_t i = 1; i + 1 < items.size(); ++i) {
		const float cur = horizontal ? items[i].b.MidX() : items[i].b.MidY();
		const float d = first + step * i - cur;
		items[i].n->SetTransform(items[i].n->GetTransform() *
			(horizontal ? Matrix2D::Translation(d, 0) : Matrix2D::Translation(0, d)));
	}
}

// ============================================================================
// 2.5 Geometry Transform
// ============================================================================
Vec2 CoordinateSpace::LocalToScreen(Vec2 localPt, const Matrix2D& worldXf, const Viewport& view)
{
    return view.WorldToScreen(worldXf.TransformPoint(localPt));
}

std::optional<Vec2> CoordinateSpace::ScreenToLocal(Vec2 screenPt, const Matrix2D& worldXf, const Viewport& view)
{
    Matrix2D inv = worldXf;
    if (!inv.Invert()) return std::nullopt;
    return inv.TransformPoint(view.ScreenToWorld(screenPt));
}

Matrix2D Transformer::Translate(Vec2 d, const Matrix2D& initialXf)
{
    return initialXf * Matrix2D::Translation(d.x, d.y);
}

std::optional<Matrix2D> Transformer::ScaleByHandle(int handleIdx, Vec2 curWorld,
                                                   const Rect2& b, const Matrix2D& initialXf)
{
    Matrix2D inv = initialXf;
    if (!inv.Invert()) return std::nullopt;

    const Vec2 cur = inv.TransformPoint(curWorld);
    const float w = b.Width(), h = b.Height();
    if (w <= 0.001f || h <= 0.001f) return std::nullopt;

    Vec2 anchor{};
    float sx = 1.0f, sy = 1.0f;
    switch (handleIdx) {
    case 5: anchor = { b.left,  b.top };    sx = (cur.x - anchor.x) / w; sy = (cur.y - anchor.y) / h; break; // BR
    case 1: anchor = { b.right, b.bottom }; sx = (anchor.x - cur.x) / w; sy = (anchor.y - cur.y) / h; break; // TL
    case 3: anchor = { b.left,  b.bottom }; sx = (cur.x - anchor.x) / w; sy = (anchor.y - cur.y) / h; break; // TR
    case 7: anchor = { b.right, b.top };    sx = (anchor.x - cur.x) / w; sy = (cur.y - anchor.y) / h; break; // BL
    case 4: anchor = { b.left,  b.MidY() }; sx = (cur.x - anchor.x) / w; break;                           // R
    case 8: anchor = { b.right, b.MidY() }; sx = (anchor.x - cur.x) / w; break;                           // L
    case 6: anchor = { b.MidX(), b.top };   sy = (cur.y - anchor.y) / h; break;                           // B
    case 2: anchor = { b.MidX(), b.bottom };sy = (anchor.y - cur.y) / h; break;                           // T
    default: return std::nullopt;
    }

    if (std::abs(sx) < 0.01f) sx = 0.01f;
    if (std::abs(sy) < 0.01f) sy = 0.01f;
    return Matrix2D::Scale(sx, sy, anchor) * initialXf;
}

Matrix2D Transformer::RotateAboutCenter(Vec2 startWorld, Vec2 curWorld,
                                        const Rect2& b, const Matrix2D& initialXf)
{
    const Vec2 centerWorld = initialXf.TransformPoint(Vec2(b.MidX(), b.MidY()));
    const float a0 = std::atan2(startWorld.y - centerWorld.y, startWorld.x - centerWorld.x);
    const float a1 = std::atan2(curWorld.y - centerWorld.y, curWorld.x - centerWorld.x);
    const float deg = (a1 - a0) * (180.0f / 3.14159265f);
    return initialXf * Matrix2D::Rotation(deg, centerWorld);
}

void Transformer::GetLocalHandlePoints(const Rect2& b, Vec2 out[9])
{
    const float mx = b.MidX(), my = b.MidY();
    out[0] = { b.left,  b.top };     // TL
    out[1] = { mx,      b.top };     // T
    out[2] = { b.right, b.top };     // TR
    out[3] = { b.right, my };        // R
    out[4] = { b.right, b.bottom };  // BR
    out[5] = { mx,      b.bottom };  // B
    out[6] = { b.left,  b.bottom };  // BL
    out[7] = { b.left,  my };        // L
    out[8] = { mx,      b.top - 20.0f }; // Rotate
}

std::optional<Matrix2D> Transformer::SkewByEdgeHandle(int handleIdx, Vec2 startWorld, Vec2 curWorld,
                                                      const Rect2& b, const Matrix2D& initialXf)
{
    Matrix2D inv = initialXf;
    if (!inv.Invert()) return std::nullopt;
    const Vec2 s = inv.TransformPoint(startWorld);
    const Vec2 c = inv.TransformPoint(curWorld);
    const float w = b.Width(), h = b.Height();
    if (w <= 0.001f || h <= 0.001f) return std::nullopt;

    Vec2 anchor{};
    float shx = 0.0f, shy = 0.0f;
    switch (handleIdx) {
    case 2: // T (하단 고정, 수평 전단)
        anchor = { b.MidX(), b.bottom };
        shx = (c.x - s.x) / (-h);
        break;
    case 6: // B (상단 고정, 수평 전단)
        anchor = { b.MidX(), b.top };
        shx = (c.x - s.x) / h;
        break;
    case 4: // R (좌측 고정, 수직 전단)
        anchor = { b.left, b.MidY() };
        shy = (c.y - s.y) / w;
        break;
    case 8: // L (우측 고정, 수직 전단)
        anchor = { b.right, b.MidY() };
        shy = (c.y - s.y) / (-w);
        break;
    default:
        return std::nullopt;
    }
    shx = (std::max)(-4.0f, (std::min)(4.0f, shx));
    shy = (std::max)(-4.0f, (std::min)(4.0f, shy));
    const Matrix2D shear{ 1.0f, shy, shx, 1.0f, 0.0f, 0.0f };
    return Matrix2D::Translation(-anchor.x, -anchor.y) * shear * Matrix2D::Translation(anchor.x, anchor.y) * initialXf;
}

// ============================================================================
// 2.6 Path Operations / Pathfinder (불린 연산 뼈대 구현)
// ============================================================================
ComPtr<ID2D1PathGeometry> PathOp::GetCachedGeometry(const PathNode& node, ID2D1Factory* factory)
{
	if (!factory) return nullptr;
	struct CacheEntry {
		uint32_t version = 0;
		ID2D1Factory* factory = nullptr;
		ComPtr<ID2D1PathGeometry> geo;
	};
	static std::unordered_map<uint64_t, CacheEntry> s_cache;
	// ID 가 발급된 노드는 NodeId 기반, 미리보기 등 ID==0 노드는 포인터 기반 상위 비트 키 사용
	const uint64_t key = (node.GetId() != 0)
		? node.GetId()
		: (0x8000000000000000ULL | (reinterpret_cast<uintptr_t>(&node) & 0x7FFFFFFFFFFFFFFFULL));
	auto it = s_cache.find(key);
	if (it != s_cache.end() && it->second.version == node.GetVersion() && it->second.factory == factory && it->second.geo) {
		return it->second.geo;
	}
	if (s_cache.size() > 2048) s_cache.clear();
	ComPtr<ID2D1PathGeometry> built = BuildGeometry(node, factory);
	if (built) {
		s_cache[key] = CacheEntry{ node.GetVersion(), factory, built };
	}
	return built;
}

ComPtr<ID2D1PathGeometry> PathOp::BuildGeometry(const PathNode& node, ID2D1Factory* factory)
{
	if (!factory) return nullptr;
	ComPtr<ID2D1PathGeometry> geo;
	if (FAILED(factory->CreatePathGeometry(&geo))) return nullptr;
	ComPtr<ID2D1GeometrySink> sink;
	if (FAILED(geo->Open(&sink))) return nullptr;
	sink->SetFillMode(node.GetStyle().fillMode == FillMode::Winding ? D2D1_FILL_MODE_WINDING : D2D1_FILL_MODE_ALTERNATE);

	auto pt = [](Vec2 v) { return D2D1::Point2F(v.x, v.y); };
	auto seg = [&](const BezierNode& a, const BezierNode& b) {
		if (!a.HasOut() && !b.HasIn())
			sink->AddLine(pt(b.anchor));
		else
			sink->AddBezier(D2D1::BezierSegment(pt(a.AbsOut()), pt(b.AbsIn()), pt(b.anchor)));
	};
	for (const SubPath& sp : node.GetPaths()) {
		const auto& n = sp.nodes;
		if (n.empty()) continue;
		sink->BeginFigure(pt(n[0].anchor), D2D1_FIGURE_BEGIN_FILLED);
		for (size_t i = 1; i < n.size(); ++i) seg(n[i - 1], n[i]);
		if (sp.closed && n.size() > 1) seg(n.back(), n[0]);
		sink->EndFigure(sp.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
	}
	if (FAILED(sink->Close())) return nullptr;
	return geo;
}

ComPtr<ID2D1Geometry> PathOp::BuildWorldGeometry(const PathNode& node, ID2D1Factory* factory)
{
	if (!factory) return nullptr;
	ComPtr<ID2D1PathGeometry> localGeo = BuildGeometry(node, factory);
	if (!localGeo) return nullptr;

	const Matrix2D worldXf = node.GetWorldTransform();
	if (worldXf.IsIdentity()) {
		return localGeo;
	}

	D2D1_MATRIX_3X2_F d2dXf = worldXf.ToD2D();
	ComPtr<ID2D1TransformedGeometry> transformedGeo;
	if (FAILED(factory->CreateTransformedGeometry(localGeo.Get(), &d2dXf, &transformedGeo))) {
		return localGeo;
	}
	return transformedGeo;
}

// ID2D1SimplifiedGeometrySink 구현체: D2D 지오메트리 데이터를 SubPath/BezierNode 목록으로 복원
class SubPathGeometrySink : public ID2D1SimplifiedGeometrySink {
public:
	SubPathGeometrySink(std::vector<SubPath>& outPaths, D2D1_FILL_MODE* outFillMode)
		: m_paths(outPaths), m_pFillMode(outFillMode), m_refCount(1) {}

	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1SimplifiedGeometrySink)) {
			*ppv = static_cast<ID2D1SimplifiedGeometrySink*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refCount); }
	IFACEMETHODIMP_(ULONG) Release() override {
		ULONG r = InterlockedDecrement(&m_refCount);
		if (r == 0) delete this;
		return r;
	}

	// ID2D1SimplifiedGeometrySink
	IFACEMETHODIMP_(void) SetFillMode(D2D1_FILL_MODE fillMode) override {
		if (m_pFillMode) *m_pFillMode = fillMode;
	}
	IFACEMETHODIMP_(void) SetSegmentFlags(D2D1_PATH_SEGMENT /*vertexFlags*/) override {}

	IFACEMETHODIMP_(void) BeginFigure(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN /*figureBegin*/) override {
		m_paths.emplace_back();
		m_currentPath = &m_paths.back();
		m_currentPath->closed = false;

		BezierNode startNode;
		startNode.anchor = { startPoint.x, startPoint.y };
		startNode.hIn = { 0.0f, 0.0f };
		startNode.hOut = { 0.0f, 0.0f };
		startNode.type = BezierNode::Type::Corner;
		m_currentPath->nodes.push_back(startNode);
	}

	IFACEMETHODIMP_(void) AddLines(const D2D1_POINT_2F* points, UINT32 pointsCount) override {
		if (!m_currentPath || !points || pointsCount == 0) return;
		for (UINT32 i = 0; i < pointsCount; ++i) {
			BezierNode node;
			node.anchor = { points[i].x, points[i].y };
			node.hIn = { 0.0f, 0.0f };
			node.hOut = { 0.0f, 0.0f };
			node.type = BezierNode::Type::Corner;
			m_currentPath->nodes.push_back(node);
		}
	}

	IFACEMETHODIMP_(void) AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 beziersCount) override {
		if (!m_currentPath || !beziers || beziersCount == 0) return;
		for (UINT32 i = 0; i < beziersCount; ++i) {
			const auto& seg = beziers[i];
			// 이전 노드의 hOut 은 seg.point1 과 이전 anchor 의 상대 벡터
			if (!m_currentPath->nodes.empty()) {
				auto& prev = m_currentPath->nodes.back();
				prev.hOut = { seg.point1.x - prev.anchor.x, seg.point1.y - prev.anchor.y };
			}
			BezierNode node;
			node.anchor = { seg.point3.x, seg.point3.y };
			// seg.point2 는 새 anchor 기준 상대 벡터
			node.hIn = { seg.point2.x - node.anchor.x, seg.point2.y - node.anchor.y };
			node.hOut = { 0.0f, 0.0f };
			node.type = BezierNode::Type::Corner;
			m_currentPath->nodes.push_back(node);
		}
	}

	IFACEMETHODIMP_(void) EndFigure(D2D1_FIGURE_END figureEnd) override {
		if (!m_currentPath) return;
		m_currentPath->closed = (figureEnd == D2D1_FIGURE_END_CLOSED);
		// 폐곡선이고 마지막 정점이 시작점과 완전히 일치하면 병합
		if (m_currentPath->closed && m_currentPath->nodes.size() > 1) {
			const auto& first = m_currentPath->nodes.front();
			const auto& last = m_currentPath->nodes.back();
			float dx = first.anchor.x - last.anchor.x;
			float dy = first.anchor.y - last.anchor.y;
			if ((dx * dx + dy * dy) < 0.0001f) {
				m_currentPath->nodes.front().hIn = last.hIn;
				m_currentPath->nodes.pop_back();
			}
		}
		m_currentPath = nullptr;
	}

	IFACEMETHODIMP Close() override { return S_OK; }

private:
	std::vector<SubPath>& m_paths;
	D2D1_FILL_MODE*       m_pFillMode = nullptr;
	SubPath*              m_currentPath = nullptr;
	LONG                  m_refCount = 1;
};

bool PathOp::ExtractSubPaths(ID2D1Geometry* geometry, std::vector<SubPath>& outPaths,
                            D2D1_FILL_MODE* outFillMode, float flatteningTolerance)
{
	if (!geometry) return false;
	outPaths.clear();

	ComPtr<SubPathGeometrySink> sink = new SubPathGeometrySink(outPaths, outFillMode);
	// ID2D1Geometry::Simplify 를 통해 선 및 베지에 커브 세그먼트로 분해 수신
	HRESULT hr = geometry->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES,
	                                nullptr, flatteningTolerance, sink.Get());
	return SUCCEEDED(hr);
}

std::unique_ptr<PathNode> PathOp::Combine(const PathNode& a, const PathNode& b,
                                         BooleanMode mode, ID2D1Factory* factory,
                                         float flatteningTolerance)
{
	if (!factory) return nullptr;

	ComPtr<ID2D1Geometry> geoA = BuildWorldGeometry(a, factory);
	ComPtr<ID2D1Geometry> geoB = BuildWorldGeometry(b, factory);
	if (!geoA || !geoB) return nullptr;

	// D2D1_COMBINE_MODE 매핑
	D2D1_COMBINE_MODE d2dMode = D2D1_COMBINE_MODE_UNION;
	switch (mode) {
	case BooleanMode::Union:     d2dMode = D2D1_COMBINE_MODE_UNION; break;
	case BooleanMode::Intersect: d2dMode = D2D1_COMBINE_MODE_INTERSECT; break;
	case BooleanMode::Subtract:  d2dMode = D2D1_COMBINE_MODE_EXCLUDE; break; // A - B
	case BooleanMode::Exclude:   d2dMode = D2D1_COMBINE_MODE_XOR; break;
	}

	ComPtr<ID2D1PathGeometry> combinedGeo;
	if (FAILED(factory->CreatePathGeometry(&combinedGeo))) return nullptr;

	ComPtr<ID2D1GeometrySink> sink;
	if (FAILED(combinedGeo->Open(&sink))) return nullptr;

	// D2D CombineWithGeometry 실행
	HRESULT hr = geoA->CombineWithGeometry(geoB.Get(), d2dMode, nullptr, flatteningTolerance, sink.Get());
	if (FAILED(hr)) return nullptr;
	if (FAILED(sink->Close())) return nullptr;

	// 추출된 지오메트리로부터 SubPath/BezierNode 복원
	std::vector<SubPath> subPaths;
	D2D1_FILL_MODE d2dFillMode = D2D1_FILL_MODE_ALTERNATE;
	if (!ExtractSubPaths(combinedGeo.Get(), subPaths, &d2dFillMode, flatteningTolerance) || subPaths.empty()) {
		// 불리언 연산 결과 빈 도형(예: 교집합이 없는 경우)이면 nullptr 반환
		return nullptr;
	}

	auto result = std::make_unique<PathNode>();
	result->SetName(L"Combined Path");
	// 스타일은 첫 번째(최하단) 도형 스타일 승계
	Style combinedStyle = a.GetStyle();
	combinedStyle.fillMode = (d2dFillMode == D2D1_FILL_MODE_WINDING) ? FillMode::Winding : FillMode::Alternate;
	result->SetStyle(combinedStyle);
	result->SetPaths(std::move(subPaths));
	// 월드 지오메트리 기준으로 추출되었으므로 로컬 트랜스폼은 항등 변환
	result->SetTransform(Matrix2D::Identity());
	return result;
}

// 1) 외곽선 확장: 선(Stroke)을 닫힌 면(Fill) 패스로 변환 (Widen)
std::unique_ptr<PathNode> PathOp::OutlineStroke(const PathNode& node, ID2D1Factory* factory, float flatteningTolerance, float widthOverride)
{
	if (!factory) return nullptr;

	// 원본의 로컬 지오메트리 빌드
	ComPtr<ID2D1PathGeometry> localGeo = BuildGeometry(node, factory);
	if (!localGeo) return nullptr;

	// 선 스타일 및 두께 확인
	const Style& style = node.GetStyle();
	float strokeWidth = style.stroke.width;
	if (widthOverride > 0.0f) strokeWidth = widthOverride;
	if (strokeWidth < 0.1f) strokeWidth = 1.0f;

	// 스트로크 스타일: 화면 렌더링(GetStrokeStyle)과 같은 선 끝/접합/미터 한계/대시를 사용 (외곽선 확장 결과가 화면의 선과 같은 모양이어야 함)
	const Stroke& st = style.stroke;
	D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT;
	if (st.cap == LineCap::Round) cap = D2D1_CAP_STYLE_ROUND;
	else if (st.cap == LineCap::Square) cap = D2D1_CAP_STYLE_SQUARE;
	D2D1_LINE_JOIN joinStyle = D2D1_LINE_JOIN_MITER_OR_BEVEL;
	if (st.join == LineJoin::Round) joinStyle = D2D1_LINE_JOIN_ROUND;
	else if (st.join == LineJoin::Bevel) joinStyle = D2D1_LINE_JOIN_BEVEL;
	std::vector<float> dashes;
	if (IsValidDashArray(st.dashArray))
		for (float v : st.dashArray) dashes.push_back(v / strokeWidth);   // D2D 대시는 선 두께의 배수
	D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
		cap, cap, cap, joinStyle, (std::max)(st.miterLimit, 1.0f),
		dashes.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM, dashes.empty() ? 0.0f : st.dashOffset / strokeWidth);
	ComPtr<ID2D1StrokeStyle> strokeStyle;
	factory->CreateStrokeStyle(&sp, dashes.empty() ? nullptr : dashes.data(), static_cast<UINT32>(dashes.size()), &strokeStyle);

	// Widen 대상을 담을 새 PathGeometry
	ComPtr<ID2D1PathGeometry> widenedGeo;
	if (FAILED(factory->CreatePathGeometry(&widenedGeo))) return nullptr;

	ComPtr<ID2D1GeometrySink> sink;
	if (FAILED(widenedGeo->Open(&sink))) return nullptr;

	// Widen 수행
	HRESULT hr = localGeo->Widen(
		strokeWidth,
		strokeStyle.Get(),
		nullptr, // identity matrix (로컬 좌표계 유지)
		flatteningTolerance,
		sink.Get()
	);
	sink->Close();

	if (FAILED(hr)) return nullptr;

	// 추출
	std::vector<SubPath> subPaths;
	D2D1_FILL_MODE d2dFillMode = D2D1_FILL_MODE_WINDING;
	if (!ExtractSubPaths(widenedGeo.Get(), subPaths, &d2dFillMode, flatteningTolerance) || subPaths.empty()) {
		return nullptr;
	}

	auto result = std::make_unique<PathNode>();
	result->SetName(node.GetName() + L" Outline");

	// 외곽선 확장 결과는 "면(Fill)"이 원본의 선 색상/페인트가 되고, 선은 없음
	Style outStyle;
	outStyle.fill.type = PaintType::Solid;
	outStyle.fill.color = style.stroke.color;
	outStyle.stroke.width = 0.0f;
	outStyle.stroke.color = Color(0.0f, 0.0f, 0.0f, 0.0f);
	outStyle.fillMode = (d2dFillMode == D2D1_FILL_MODE_WINDING) ? FillMode::Winding : FillMode::Alternate;
	result->SetStyle(outStyle);

	result->SetPaths(std::move(subPaths));
	result->SetTransform(node.GetTransform());
	return result;
}

// RDP(Ramer-Douglas-Peucker) 보조 함수: 점과 선분 사이의 수직 거리 계산
static float PerpendicularDistance(Vec2 pt, Vec2 lineStart, Vec2 lineEnd)
{
	float dx = lineEnd.x - lineStart.x;
	float dy = lineEnd.y - lineStart.y;
	float lenSq = dx * dx + dy * dy;
	if (lenSq < 0.000001f) {
		float px = pt.x - lineStart.x;
		float py = pt.y - lineStart.y;
		return std::sqrt(px * px + py * py);
	}
	float t = ((pt.x - lineStart.x) * dx + (pt.y - lineStart.y) * dy) / lenSq;
	if (t < 0.0f) t = 0.0f;
	else if (t > 1.0f) t = 1.0f;
	float projX = lineStart.x + t * dx;
	float projY = lineStart.y + t * dy;
	float distSq = (pt.x - projX) * (pt.x - projX) + (pt.y - projY) * (pt.y - projY);
	return std::sqrt(distSq);
}

// RDP 재귀 단순화
static void RDPRecursive(const std::vector<BezierNode>& nodes, size_t start, size_t end, float epsilon, std::vector<bool>& keep)
{
	if (end <= start + 1) return;

	float maxDist = 0.0f;
	size_t maxIdx = start;

	for (size_t i = start + 1; i < end; ++i) {
		float d = PerpendicularDistance(nodes[i].anchor, nodes[start].anchor, nodes[end].anchor);
		if (d > maxDist) {
			maxDist = d;
			maxIdx = i;
		}
	}

	if (maxDist > epsilon) {
		keep[maxIdx] = true;
		RDPRecursive(nodes, start, maxIdx, epsilon, keep);
		RDPRecursive(nodes, maxIdx, end, epsilon, keep);
	}
}

// 2) 패스 단순화: RDP 알고리즘 적용
bool PathOp::Simplify(PathNode& node, float tolerance)
{
	bool modified = false;
	auto paths = node.GetPaths();

	for (auto& sp : paths) {
		if (sp.nodes.size() <= 2) continue;

		const size_t n = sp.nodes.size();
		std::vector<bool> keep(n, false);
		keep.front() = true;
		keep.back() = true;

		RDPRecursive(sp.nodes, 0, n - 1, tolerance, keep);

		std::vector<BezierNode> simplified;
		for (size_t i = 0; i < n; ++i) {
			if (keep[i]) {
				simplified.push_back(sp.nodes[i]);
			}
		}

		if (simplified.size() < sp.nodes.size()) {
			sp.nodes = std::move(simplified);
			modified = true;
		}
	}

	if (modified) {
		node.SetPaths(std::move(paths));
		node.SetShapeKind(ShapeKind::Path);
	}
	return modified;
}

// 2b) 부드럽게: 점 줄이기(RDP) + Catmull-Rom 핸들 부여
bool PathOp::Smooth(PathNode& node, float tolerance)
{
	if (tolerance > 0.0f) Simplify(node, tolerance);

	auto paths = node.GetPaths();
	bool modified = false;
	for (auto& sp : paths) {
		const int n = static_cast<int>(sp.nodes.size());
		if (n < 2) continue;
		for (int i = 0; i < n; ++i) {
			const bool hasPrev = (i > 0) || (sp.closed && n > 2);
			const bool hasNext = (i < n - 1) || (sp.closed && n > 2);
			const Vec2 a = sp.nodes[i].anchor;
			const Vec2 pr = hasPrev ? sp.nodes[(i + n - 1) % n].anchor : a;
			const Vec2 nx = hasNext ? sp.nodes[(i + 1) % n].anchor : a;
			BezierNode& bn = sp.nodes[i];
			if (hasPrev && hasNext) {
				const Vec2 d{ (nx.x - pr.x) / 6.0f, (nx.y - pr.y) / 6.0f };
				bn.hOut = d;
				bn.hIn = Vec2{ -d.x, -d.y };
			} else if (hasNext) {
				bn.hIn = Vec2{};
				bn.hOut = Vec2{ (nx.x - a.x) / 3.0f, (nx.y - a.y) / 3.0f };
			} else {
				bn.hOut = Vec2{};
				bn.hIn = Vec2{ (pr.x - a.x) / 3.0f, (pr.y - a.y) / 3.0f };
			}
			bn.type = BezierNode::Type::Smooth;
		}
		modified = true;
	}
	if (modified) {
		node.SetPaths(std::move(paths));
		node.SetShapeKind(ShapeKind::Path);
	}
	return modified;
}

// 텍스트 -> 윤곽선: IDWriteTextRenderer 로 글리프 런을 수집해 IDWriteFontFace::GetGlyphRunOutline 으로 패스화
namespace {
class GlyphOutlineCollector : public IDWriteTextRenderer
{
public:
	GlyphOutlineCollector(ID2D1Factory* f, float dx) : m_factory(f), m_dx(dx) {}

	std::vector<SubPath> paths;

	// IUnknown
	STDMETHOD(QueryInterface)(REFIID iid, void** obj) override
	{
		if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) || iid == __uuidof(IDWriteTextRenderer)) { *obj = this; AddRef(); return S_OK; }
		*obj = nullptr; return E_NOINTERFACE;
	}
	STDMETHOD_(ULONG, AddRef)() override { return ++m_ref; }
	STDMETHOD_(ULONG, Release)() override { const ULONG r = --m_ref; if (r == 0) delete this; return r; }

	// IDWritePixelSnapping
	STDMETHOD(IsPixelSnappingDisabled)(void*, BOOL* disabled) override { *disabled = TRUE; return S_OK; }
	STDMETHOD(GetCurrentTransform)(void*, DWRITE_MATRIX* m) override { *m = DWRITE_MATRIX{ 1, 0, 0, 1, 0, 0 }; return S_OK; }
	STDMETHOD(GetPixelsPerDip)(void*, FLOAT* p) override { *p = 1.0f; return S_OK; }

	// IDWriteTextRenderer
	STDMETHOD(DrawGlyphRun)(void*, FLOAT ox, FLOAT oy, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override
	{
		if (!run || !run->fontFace || run->glyphCount == 0) return S_OK;
		ComPtr<ID2D1PathGeometry> geo;
		if (FAILED(m_factory->CreatePathGeometry(&geo))) return S_OK;
		ComPtr<ID2D1GeometrySink> sink;
		if (FAILED(geo->Open(&sink))) return S_OK;
		const HRESULT hr = run->fontFace->GetGlyphRunOutline(run->fontEmSize, run->glyphIndices, run->glyphAdvances, run->glyphOffsets,
			run->glyphCount, run->isSideways, (run->bidiLevel & 1) != 0, sink.Get());
		sink->Close();
		if (FAILED(hr)) return S_OK;

		// 런의 기준선 원점(+ 정렬 이동)으로 옮겨서 추출
		const D2D1_MATRIX_3X2_F tr = D2D1::Matrix3x2F::Translation(ox + m_dx, oy);
		ComPtr<ID2D1TransformedGeometry> xg;
		if (FAILED(m_factory->CreateTransformedGeometry(geo.Get(), &tr, &xg))) return S_OK;
		std::vector<SubPath> sub;
		if (PathOp::ExtractSubPaths(xg.Get(), sub))
			for (auto& sp : sub) paths.push_back(std::move(sp));
		return S_OK;
	}
	STDMETHOD(DrawUnderline)(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
	STDMETHOD(DrawStrikethrough)(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
	STDMETHOD(DrawInlineObject)(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }

private:
	ID2D1Factory* m_factory;
	float m_dx;
	ULONG m_ref = 1;
};
} // namespace

void PathOp::MeasureText(TextNode& text)
{
	if (text.GetText().empty()) return;
	ComPtr<IDWriteTextLayout> layout;
	float w = 0, h = 0, x0 = 0;
	if (BuildTextLayout(text, layout, w, h, x0)) text.SetMeasured(w, h);
}

std::vector<std::wstring> PathOp::WrapLines(const TextNode& text)
{
	std::vector<std::wstring> lines;
	const std::wstring& txt = text.GetText();
	ComPtr<IDWriteTextLayout> layout;
	float w = 0, h = 0, x0 = 0;
	if (!txt.empty() && BuildTextLayout(text, layout, w, h, x0)) {
		UINT32 count = 0;
		layout->GetLineMetrics(nullptr, 0, &count);
		std::vector<DWRITE_LINE_METRICS> lm(count);
		if (count > 0 && SUCCEEDED(layout->GetLineMetrics(lm.data(), count, &count))) {
			size_t pos = 0;
			for (UINT32 i = 0; i < count && pos <= txt.size(); ++i) {
				std::wstring ln = txt.substr(pos, lm[i].length);
				pos += lm[i].length;
				while (!ln.empty() && (ln.back() == L'\n' || ln.back() == L'\r' || ln.back() == L' ')) ln.pop_back();
				lines.push_back(ln);
			}
			return lines;
		}
	}
	// 대체: 개행 기준
	size_t p = 0;
	while (p <= txt.size()) {
		size_t e = txt.find(L'\n', p);
		if (e == std::wstring::npos) e = txt.size();
		lines.push_back(txt.substr(p, e - p));
		p = e + 1;
		if (e == txt.size()) break;
	}
	return lines;
}

std::unique_ptr<PathNode> PathOp::TextToPath(const TextNode& text, ID2D1Factory* factory)
{
	if (!factory || text.GetText().empty()) return nullptr;
	ComPtr<IDWriteTextLayout> layout;
	float w = 0, h = 0, x0 = 0;
	if (!BuildTextLayout(text, layout, w, h, x0)) return nullptr;

	ComPtr<GlyphOutlineCollector> collector;
	collector.Attach(new GlyphOutlineCollector(factory, x0));
	if (FAILED(layout->Draw(nullptr, collector.Get(), 0.0f, 0.0f)) || collector->paths.empty()) return nullptr;

	auto result = std::make_unique<PathNode>();
	Style st;
	st.fill = text.GetStyle().fill;
	if (st.fill.type == PaintType::None) { st.fill.type = PaintType::Solid; st.fill.color = Color(0, 0, 0, 1); }
	st.stroke.width = 0.0f;
	st.fill.color = text.GetStyle().fill.color;
	st.fillMode = FillMode::Winding;   // 글리프 윤곽: 안쪽 구멍(ㅇ, O 등)은 반대 방향 서브패스로 표현됨
	result->SetStyle(st);
	result->SetPaths(std::move(collector->paths));
	result->SetTransform(text.GetTransform());
	result->SetOpacity(text.GetOpacity());
	return result;
}

// 3) 패스 방향 반전
void PathOp::ReversePath(PathNode& node)
{
	auto paths = node.GetPaths();
	for (auto& sp : paths) {
		if (sp.nodes.empty()) continue;

		std::reverse(sp.nodes.begin(), sp.nodes.end());
		// 정점이 뒤집히면 In 핸들과 Out 핸들이 서로 맞바뀌어야 함
		for (auto& bn : sp.nodes) {
			std::swap(bn.hIn, bn.hOut);
		}
	}
	node.SetPaths(std::move(paths));
}

// 4) 패스 결합: 두 PathNode의 서브패스를 연결하거나 단일 패스로 결합
std::unique_ptr<PathNode> PathOp::Join(const PathNode& a, const PathNode& b, float maxDistance)
{
	auto res = std::make_unique<PathNode>();
	res->SetName(a.GetName() + L" Joined");
	res->SetStyle(a.GetStyle());

	// a 와 b 의 모든 서브패스를 a의 로컬 좌표계로 통일하여 수집
	// b의 월드 변환 -> a의 로컬 역변환
	Matrix2D aInv = a.GetTransform();
	aInv.Invert();
	Matrix2D bToA = b.GetTransform() * aInv;

	std::vector<SubPath> combinedPaths = a.GetPaths();
	for (const auto& spB : b.GetPaths()) {
		SubPath spCopy = spB;
		for (auto& bn : spCopy.nodes) {
			// 핸들은 앵커 기준 상대값 -> 원래 앵커 기준으로 절대 위치를 변환한 뒤 새 앵커에 대한 상대값으로 되돌림
			const Vec2 oldAnchor = bn.anchor;
			const Vec2 newAnchor = bToA.TransformPoint(oldAnchor);
			const Vec2 absIn = bToA.TransformPoint(oldAnchor + bn.hIn);
			const Vec2 absOut = bToA.TransformPoint(oldAnchor + bn.hOut);
			bn.anchor = newAnchor;
			bn.hIn = absIn - newAnchor;
			bn.hOut = absOut - newAnchor;
		}
		combinedPaths.push_back(std::move(spCopy));
	}

	// 열린 서브패스들 중 끝점과 시작점이 인접(maxDistance 이내)한 경우 하나로 연결
	bool mergedAny = true;
	while (mergedAny) {
		mergedAny = false;
		for (size_t i = 0; i < combinedPaths.size(); ++i) {
			if (combinedPaths[i].closed || combinedPaths[i].nodes.empty()) continue;

			for (size_t j = 0; j < combinedPaths.size(); ++j) {
				if (i == j || combinedPaths[j].closed || combinedPaths[j].nodes.empty()) continue;

				Vec2 endA = combinedPaths[i].nodes.back().anchor;
				Vec2 startB = combinedPaths[j].nodes.front().anchor;

				float dist = (endA - startB).Length();
				if (dist <= maxDistance) {
					// j 를 i 의 뒤에 이어붙임. 끝점이 일치하면 두 점을 하나로 합치되, 이어지는 쪽(j)의 Out 핸들을 보존
					if (dist < 0.001f) combinedPaths[i].nodes.back().hOut = combinedPaths[j].nodes.front().hOut;
					for (size_t k = (dist < 0.001f ? 1 : 0); k < combinedPaths[j].nodes.size(); ++k) {
						combinedPaths[i].nodes.push_back(combinedPaths[j].nodes[k]);
					}
					combinedPaths.erase(combinedPaths.begin() + j);
					mergedAny = true;
					break;
				}
			}
			if (mergedAny) break;
		}
	}

	res->SetPaths(std::move(combinedPaths));
	res->SetTransform(a.GetTransform());
	return res;
}

// ============================================================================
// 3. Renderer (D2D)
// ============================================================================
bool D2DRenderer::Initialize(ID2D1RenderTarget* rt, ID2D1Factory* factory)
{
	m_rt = rt;
	m_factory = factory;
	m_strokeStyleCache.clear();   // 팩터리/렌더 타깃이 바뀔 수 있으므로 이전 리소스 폐기
	m_gradientCache.clear();
	m_brush.Reset();
	m_imageCache.clear();   // 비트맵은 렌더 타깃에 종속
	if (!m_rt) return false;
	return SUCCEEDED(m_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &m_brush));
}

void D2DRenderer::Shutdown()
{
	m_geoCache.clear();
	m_strokeStyleCache.clear();
	m_gradientCache.clear();
	m_brush.Reset();
	m_rt = nullptr;
	m_factory = nullptr;
}

void D2DRenderer::BeginDraw() { if (m_rt) m_rt->BeginDraw(); }
void D2DRenderer::EndDraw()   { if (m_rt) m_rt->EndDraw(); }

void D2DRenderer::DrawDocument(Document& doc, const Viewport& view)
{
	// 레이어 0 -> N-1 (Painter's Algorithm)
	for (auto& layer : doc.GetLayers()) {
		if (!layer->IsVisible()) continue;

		const bool needLayer = layer->GetOpacity() < 0.999f;   // 레이어 불투명도 합성 전용 (클리핑은 2.4 의 GroupNode::clipPath 로 구현)
		if (needLayer && m_rt) {
			D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
			params.opacity = layer->GetOpacity();
			params.contentBounds = D2D1::InfiniteRect();
			m_rt->PushLayer(params, nullptr);
		}

		DrawNode(*layer, Matrix2D::Identity(), view);

		if (needLayer && m_rt) {
			m_rt->PopLayer();
		}
	}
}

void D2DRenderer::DrawNode(Node& node, const Matrix2D& parentXf, const Viewport& view)
{
	if (!node.IsVisible()) return;
	const Matrix2D worldXf = node.GetTransform() * parentXf;

	// SVG opacity 의미: 도형(채움+선) / 그룹(모든 자식)을 먼저 하나로 합성한 뒤 불투명도를 한 번만 적용.
	// 자식마다 알파를 곱하면 겹친 부분이 진해지는 문제가 생김. 레이어는 DrawDocument 가 같은 방식으로 처리.
	const bool needLayer = m_rt && node.Kind() != NodeKind::Layer && node.GetOpacity() < 0.999f;
	if (needLayer) {
		D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
		params.opacity = node.GetOpacity();
		params.contentBounds = D2D1::InfiniteRect();
		m_rt->PushLayer(params, nullptr);
	}

	if (node.GetShadow().enabled && !node.GetShadow().inset) DrawNodeShadow(node, worldXf, view);   // 공통 그림자 (D2DShadowRender.cpp)

	switch (node.Kind()) {
	case NodeKind::Path:  DrawPath(static_cast<PathNode&>(node), worldXf, view); break;
	case NodeKind::Text:  DrawTextNode(static_cast<TextNode&>(node), worldXf, view); break;
	case NodeKind::Image: DrawImageNode(static_cast<ImageNode&>(node), worldXf, view); break;
	case NodeKind::Group:
	case NodeKind::Layer: DrawGroup(static_cast<GroupNode&>(node), worldXf, view); break;
	}

	// 안쪽 그림자는 노드 위에 그린다. 프레임은 자식 아래/배경 위여야 해서 DrawGroup 안에서 처리
	if (node.GetShadow().enabled && node.GetShadow().inset && node.Kind() != NodeKind::Text && node.Kind() != NodeKind::Layer &&
		dynamic_cast<FrameNode*>(&node) == nullptr)
		DrawNodeInsetShadow(node, worldXf, view);

	if (needLayer) m_rt->PopLayer();
}

// WIC 로 이미지 바이트를 디코딩 (공통 헬퍼)
static ComPtr<IWICBitmapSource> DecodeImageBytes(const std::vector<uint8_t>& bytes, int* outW, int* outH)
{
	if (bytes.empty()) return nullptr;
	ComPtr<IWICImagingFactory> wic;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return nullptr;
	ComPtr<IWICStream> stream;
	if (FAILED(wic->CreateStream(&stream))) return nullptr;
	if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))) return nullptr;
	ComPtr<IWICBitmapDecoder> decoder;
	if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) return nullptr;
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;
	UINT w = 0, h = 0;
	frame->GetSize(&w, &h);
	if (outW) *outW = static_cast<int>(w);
	if (outH) *outH = static_cast<int>(h);
	ComPtr<IWICFormatConverter> conv;
	if (FAILED(wic->CreateFormatConverter(&conv))) return nullptr;
	if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) return nullptr;
	ComPtr<IWICBitmapSource> src;
	conv.As(&src);
	return src;
}

bool PathOp::DecodeImageInfo(const std::vector<uint8_t>& bytes, int& w, int& h)
{
	w = h = 0;
	return DecodeImageBytes(bytes, &w, &h) != nullptr && w > 0 && h > 0;
}

void D2DRenderer::DrawImageNode(ImageNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	if (!m_rt || !node.GetData()) return;
	const ImageData* key = node.GetData().get();
	auto it = m_imageCache.find(key);
	if (it == m_imageCache.end()) {
		ImageCacheEntry e;
		e.keep = node.GetData();
		if (ComPtr<IWICBitmapSource> src = DecodeImageBytes(key->bytes, nullptr, nullptr))
			m_rt->CreateBitmapFromWicBitmap(src.Get(), nullptr, &e.bitmap);
		if (m_imageCache.size() > 128) m_imageCache.clear();
		it = m_imageCache.emplace(key, std::move(e)).first;
	}
	if (!it->second.bitmap) return;   // 디코딩 실패: 아무것도 그리지 않음

	// 맞춤: Stretch 는 상자를 그대로 채움, Fit 은 비율 유지로 전체가 들어가게(여백), Fill 은 비율 유지로 상자를 채우고 원본의 넘치는 부분을 잘라냄
	ID2D1Bitmap* bmp = it->second.bitmap.Get();
	const D2D1_SIZE_F sz = bmp->GetSize();
	const float W = node.GetWidth(), H = node.GetHeight();
	D2D1_RECT_F dst = D2D1::RectF(0.0f, 0.0f, W, H);
	D2D1_RECT_F src = D2D1::RectF(0.0f, 0.0f, sz.width, sz.height);
	if (sz.width > 0.0f && sz.height > 0.0f) {
		if (node.GetFit() == ImageFit::Fit) {
			const float s = (std::min)(W / sz.width, H / sz.height);
			const float dw = sz.width * s, dh = sz.height * s;
			dst = D2D1::RectF((W - dw) * 0.5f, (H - dh) * 0.5f, (W + dw) * 0.5f, (H + dh) * 0.5f);
		} else if (node.GetFit() == ImageFit::Fill) {
			const float s = (std::max)(W / sz.width, H / sz.height);
			const float sw = W / s, sh = H / s;
			src = D2D1::RectF((sz.width - sw) * 0.5f, (sz.height - sh) * 0.5f, (sz.width + sw) * 0.5f, (sz.height + sh) * 0.5f);
		}
	}
	m_rt->SetTransform((worldXf * view.ToMatrix()).ToD2D());
	m_rt->DrawBitmap(bmp, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

// 텍스트 레이아웃 공통 빌더 (그리기/측정/윤곽선/줄바꿈 공용). w,h = 상자 크기, x0 = 레이아웃을 그릴 왼쪽 시작 위치(로컬)
static bool BuildTextLayout(const TextNode& t, ComPtr<IDWriteTextLayout>& layout, float& w, float& h, float& x0)
{
	static ComPtr<IDWriteFactory> s_dw;
	if (!s_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dw.GetAddressOf())))) return false;
	ComPtr<IDWriteTextFormat> fmt;
	if (FAILED(s_dw->CreateTextFormat(t.GetFontFamily().c_str(), nullptr,
		t.IsBold() ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
		t.IsItalic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, t.GetFontSize(), L"ko-kr", &fmt))) return false;
	fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, t.GetFontSize() * TextNode::kLineHeightRatio, t.GetFontSize() * TextNode::kAscentRatio);
	const bool area = t.IsAreaText();
	fmt->SetWordWrapping(area ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);

	const std::wstring& txt = t.GetText();
	if (FAILED(s_dw->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()), fmt.Get(), area ? t.GetBoxWidth() : 100000.0f, 100000.0f, &layout))) return false;
	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	const DWRITE_TEXT_ALIGNMENT al = t.GetAlign() == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
		: t.GetAlign() == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
	h = (std::max)(tm.height, 1.0f);
	if (area) {
		// 영역 텍스트: 상자 폭 안에서 정렬, 원점은 상자 왼쪽 위
		w = t.GetBoxWidth();
		x0 = 0.0f;
		layout->SetTextAlignment(al);
	} else {
		// 점 텍스트: 가운데/오른쪽은 각 줄을 가장 긴 줄의 폭 안에서 정렬하고, 기준점이 그 폭의 가운데/오른쪽 끝에 오게 함
		w = (std::max)(tm.widthIncludingTrailingWhitespace, 1.0f);
		x0 = (t.GetAlign() == TextAlign::Center) ? -w * 0.5f : (t.GetAlign() == TextAlign::Right) ? -w : 0.0f;
		if (t.GetAlign() != TextAlign::Left) { layout->SetMaxWidth(w); layout->SetTextAlignment(al); }
	}
	return true;
}

void D2DRenderer::DrawTextNode(TextNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	if (!m_rt || !m_brush || node.GetText().empty()) return;

	// 레이아웃 캐시: 노드 버전은 프로세스 전역에서 유일하고 글자/글꼴/상자 폭이 바뀔 때만 바뀐다 (이동/크기 조절은 버전 불변).
	struct TextCache { ComPtr<IDWriteTextLayout> layout; float w = 0, h = 0, x0 = 0; };
	static std::unordered_map<uint32_t, TextCache> s_cache;
	auto hit = s_cache.find(node.GetVersion());
	if (hit == s_cache.end()) {
		TextCache entry;
		if (!BuildTextLayout(node, entry.layout, entry.w, entry.h, entry.x0)) return;
		if (s_cache.size() > 512) s_cache.clear();
		hit = s_cache.emplace(node.GetVersion(), std::move(entry)).first;
	}
	IDWriteTextLayout* layout = hit->second.layout.Get();
	if (node.MeasuredWidth() != hit->second.w || node.MeasuredHeight() != hit->second.h) node.SetMeasured(hit->second.w, hit->second.h);

	const Style& st = node.GetStyle();
	const Color col = (st.fill.type == PaintType::None) ? Color(0, 0, 0, 0) : st.fill.color;
	if (node.GetShadow().enabled) DrawTextShadow(layout, hit->second.x0, node.GetShadow(), worldXf, view);   // 글자 그림자는 글자 아래에
	m_brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, col.a));
	m_rt->SetTransform((worldXf * view.ToMatrix()).ToD2D());
	m_rt->DrawTextLayout(D2D1::Point2F(hit->second.x0, 0.0f), layout, m_brush.Get());
	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());   // 이후 그리기(눈금자/가이드)가 이 변환을 물려받지 않도록 복원
}

void D2DRenderer::DrawGroup(GroupNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	// 테이블이면 격자선을 먼저 그린다 (칸 내용은 아래 자식 루프에서 위에 그려짐)
	if (auto* tb = dynamic_cast<TableNode*>(&node)) DrawTableGrid(*tb, worldXf, view);
	const bool isClipMask = node.IsClippingMask();
	Node* maskNode = isClipMask ? node.GetMaskNode() : nullptr;

	ComPtr<ID2D1Geometry> maskGeo;

	// 프레임: 배경/테두리를 먼저 그리고, 자식은 프레임 상자로 클리핑
	const auto* frame = (node.Kind() == NodeKind::Group) ? dynamic_cast<const FrameNode*>(&node) : nullptr;
	if (frame && m_rt && m_brush) {
		const D2D1_MATRIX_3X2_F fm = (worldXf * view.ToMatrix()).ToD2D();
		const Style& fst = frame->GetStyle();
		const float fw = frame->GetWidth(), fh = frame->GetHeight();
		const D2D1_RECT_F rc = D2D1::RectF(0.0f, 0.0f, fw, fh);
		const CornerRadii cr = frame->ClampedRadii();
		const uint8_t sides = frame->GetBorderSides();

		// 모서리 반경이 4개 모두 같으면 사각형/둥근 사각형, 다르면 경로 지오메트리 (4.5.10)
		ComPtr<ID2D1Geometry> frameGeo;
		if (m_factory) {
			if (cr.IsUniform()) {
				if (cr.tl > 0.0f) {
					ComPtr<ID2D1RoundedRectangleGeometry> rg;
					if (SUCCEEDED(m_factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rc, cr.tl, cr.tl), &rg))) frameGeo = rg;
				} else {
					ComPtr<ID2D1RectangleGeometry> rg;
					if (SUCCEEDED(m_factory->CreateRectangleGeometry(rc, &rg))) frameGeo = rg;
				}
			} else {
				ComPtr<ID2D1PathGeometry> pg;
				ComPtr<ID2D1GeometrySink> sink;
				if (SUCCEEDED(m_factory->CreatePathGeometry(&pg)) && SUCCEEDED(pg->Open(&sink))) {
					auto arc = [&](float x, float y, float r) {
						if (r > 0.0f) sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
					};
					sink->BeginFigure(D2D1::Point2F(cr.tl, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
					sink->AddLine(D2D1::Point2F(fw - cr.tr, 0.0f));
					arc(fw, cr.tr, cr.tr);
					sink->AddLine(D2D1::Point2F(fw, fh - cr.br));
					arc(fw - cr.br, fh, cr.br);
					sink->AddLine(D2D1::Point2F(cr.bl, fh));
					arc(0.0f, fh - cr.bl, cr.bl);
					sink->AddLine(D2D1::Point2F(0.0f, cr.tl));
					arc(cr.tl, 0.0f, cr.tl);
					sink->EndFigure(D2D1_FIGURE_END_CLOSED);
					if (SUCCEEDED(sink->Close())) frameGeo = pg;
				}
			}
		}

		m_rt->SetTransform(fm);
		if (fst.fill.type == PaintType::Solid && fst.fill.color.a > 0.0f) {
			m_brush->SetColor(D2D1::ColorF(fst.fill.color.r, fst.fill.color.g, fst.fill.color.b, fst.fill.color.a));
			if (frameGeo) m_rt->FillGeometry(frameGeo.Get(), m_brush.Get());
			else m_rt->FillRectangle(rc, m_brush.Get());
		} else if (fst.fill.type == PaintType::Gradient) {
			const Rect2 fbounds{ 0.0f, 0.0f, fw, fh };
			ID2D1Brush* gb = GetGradientBrush(*frame, fbounds);
			if (gb) {
				if (frameGeo) m_rt->FillGeometry(frameGeo.Get(), gb);
				else m_rt->FillRectangle(rc, gb);
			} else {
				const Color fc = fst.fill.gradient.FallbackColor();
				if (fc.a > 0.0f) {
					m_brush->SetColor(D2D1::ColorF(fc.r, fc.g, fc.b, fc.a));
					if (frameGeo) m_rt->FillGeometry(frameGeo.Get(), m_brush.Get());
					else m_rt->FillRectangle(rc, m_brush.Get());
				}
			}
		}
		if (fst.stroke.width > 0.0f && fst.stroke.color.a > 0.0f && sides != 0) {
			m_brush->SetColor(D2D1::ColorF(fst.stroke.color.r, fst.stroke.color.g, fst.stroke.color.b, fst.stroke.color.a));
			if (sides == BorderAll) {
				if (frameGeo) m_rt->DrawGeometry(frameGeo.Get(), m_brush.Get(), fst.stroke.width);
				else m_rt->DrawRectangle(rc, m_brush.Get(), fst.stroke.width);
			} else {
				// 일부 변만: 각 변을 직선으로 (둥근 모서리 호는 그리지 않음)
				const float sw = fst.stroke.width;
				if (sides & BorderTop)    m_rt->DrawLine(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(fw, 0.0f), m_brush.Get(), sw);
				if (sides & BorderRight)  m_rt->DrawLine(D2D1::Point2F(fw, 0.0f), D2D1::Point2F(fw, fh), m_brush.Get(), sw);
				if (sides & BorderBottom) m_rt->DrawLine(D2D1::Point2F(0.0f, fh), D2D1::Point2F(fw, fh), m_brush.Get(), sw);
				if (sides & BorderLeft)   m_rt->DrawLine(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, fh), m_brush.Get(), sw);
			}
		}
		m_rt->SetTransform(D2D1::Matrix3x2F::Identity());

		// 프레임의 안쪽 그림자: 배경 위, 자식 아래 (4.5.18.1)
		if (frame->GetShadow().enabled && frame->GetShadow().inset) DrawNodeInsetShadow(const_cast<GroupNode&>(node), worldXf, view);

		if (frame->IsClip() && m_factory && frameGeo) {
			ComPtr<ID2D1TransformedGeometry> xg;
			if (SUCCEEDED(m_factory->CreateTransformedGeometry(frameGeo.Get(), &fm, &xg))) maskGeo = xg;
		}
	}

	if (isClipMask && maskNode && maskNode->Kind() == NodeKind::Path && m_rt && m_factory) {
		PathNode& pathMask = static_cast<PathNode&>(*maskNode);
		// 마스크 노드의 화면 변환 = maskNode 로컬 변환 * 그룹 월드 변환 * 뷰포트 (DrawPath 와 동일하게 뷰 변환까지 포함해야 패닝/줌에서 마스크가 도형과 같이 움직임)
		Matrix2D maskWorldXf = pathMask.GetTransform() * worldXf * view.ToMatrix();
		ComPtr<ID2D1PathGeometry> localGeo = PathOp::BuildGeometry(pathMask, m_factory);
		if (localGeo) {
			if (maskWorldXf.IsIdentity()) {
				maskGeo = localGeo;
			} else {
				D2D1_MATRIX_3X2_F d2dXf = maskWorldXf.ToD2D();
				ComPtr<ID2D1TransformedGeometry> xfGeo;
				if (SUCCEEDED(m_factory->CreateTransformedGeometry(localGeo.Get(), &d2dXf, &xfGeo))) {
					maskGeo = xfGeo;
				}
			}
		}
	}

	const bool pushMaskLayer = (maskGeo != nullptr);
	if (pushMaskLayer) {
		D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
		params.geometricMask = maskGeo.Get();
		params.maskTransform = D2D1::IdentityMatrix();
		params.contentBounds = D2D1::InfiniteRect();
		m_rt->PushLayer(params, nullptr);
	}

	for (auto& c : node.GetChildren()) {
		// 클리핑 마스크 그룹일 경우, 마스크로 사용되는 최상단 도형 자체는 오리기 형태(geometry)로만 쓰이고 직접 그려지지 않음
		if (isClipMask && c.get() == maskNode)
			continue;
		DrawNode(*c, worldXf, view);
	}

	if (pushMaskLayer) {
		m_rt->PopLayer();
	}

	// 역할(시맨틱 태그) 표시용 이름: div/빈 값은 역할 없음 (4.5.8.3)
	std::string roleTag;
	if (frame) { roleTag = frame->GetHtmlTag(); if (roleTag == "div") roleTag.clear(); }
	const bool isTopFrame = frame && (!node.GetParent() || node.GetParent()->Kind() == NodeKind::Layer);
	const bool uiComp = frame && frame->IsUiComponent();   // UI 컴포넌트는 아트보드 이름표/내부 프레임 윤곽선 표시 대상이 아님

	// 내부 컨테이너 프레임: 역할이 지정된 경우 좌상단 안쪽에 작은 배지 [header] 이름 표시
	if (frame && !uiComp && !isTopFrame && m_rt && m_brush) {   // 내부 프레임 윤곽선: 흰 배경 위에서도 범위가 보이는 편집용 표시 (저장/내보내기에는 들어가지 않음)
		const D2D1_MATRIX_3X2_F fmo = (worldXf * view.ToMatrix()).ToD2D();
		m_rt->SetTransform(fmo);
		m_brush->SetColor(D2D1::ColorF(0.30f, 0.50f, 0.90f, 0.75f));
		m_rt->DrawRectangle(D2D1::RectF(0.0f, 0.0f, frame->GetWidth(), frame->GetHeight()), m_brush.Get(), 1.0f / (std::max)(view.zoom, 0.01f));
		m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
	}
	// 내부 프레임 배지: [역할] 이름  W × H. 배지가 프레임보다 길거나(너비/높이) 안에 자식이 있으면 안쪽이 아니라 프레임 위쪽에 표시 (4.5.8.3)
	if (frame && !uiComp && !isTopFrame && m_rt && m_brush) {
		static ComPtr<IDWriteFactory> s_dwBadge;
		static ComPtr<IDWriteTextFormat> s_fmtBadge;
		if (!s_dwBadge) {
			DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dwBadge.GetAddressOf()));
			if (s_dwBadge) {
				s_dwBadge->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
					DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"ko-kr", &s_fmtBadge);
				if (s_fmtBadge) {
					s_fmtBadge->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
					s_fmtBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				}
			}
		}
		if (s_fmtBadge) {
			const D2D1_MATRIX_3X2_F fm = (worldXf * view.ToMatrix()).ToD2D();
			std::wstring txt;
			if (!roleTag.empty()) txt = L"[" + std::wstring(roleTag.begin(), roleTag.end()) + L"] ";
			if (!frame->GetName().empty()) txt += frame->GetName() + L"  ";
			wchar_t dimBuf2[48];
			swprintf_s(dimBuf2, L"%d \u00D7 %d", static_cast<int>(std::round(frame->GetWidth())), static_cast<int>(std::round(frame->GetHeight())));
			txt += dimBuf2;

			// 실제 글자 폭 측정 (한글 이름도 정확히)
			float textW = static_cast<float>(txt.length()) * 6.4f;
			ComPtr<IDWriteTextLayout> lay;
			if (SUCCEEDED(s_dwBadge->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.length()), s_fmtBadge.Get(), 2000.0f, 20.0f, &lay)) && lay) {
				DWRITE_TEXT_METRICS tm{};
				if (SUCCEEDED(lay->GetMetrics(&tm))) textW = tm.widthIncludingTrailingWhitespace;
			}
			const float bh = 15.0f;
			const float bw = textW + 12.0f;

			const float sw = frame->GetWidth() * std::sqrt(fm._11 * fm._11 + fm._12 * fm._12);
			const float sh = frame->GetHeight() * std::sqrt(fm._21 * fm._21 + fm._22 * fm._22);
			const bool fitsInside = (bw + 6.0f <= sw) && (bh + 6.0f <= sh) && node.GetChildren().empty();
			const D2D1_RECT_F bg = fitsInside
				? D2D1::RectF(fm.dx + 2.0f, fm.dy + 2.0f, fm.dx + 2.0f + bw, fm.dy + 2.0f + bh)
				: D2D1::RectF(fm.dx, fm.dy - bh - 3.0f, fm.dx + bw, fm.dy - 3.0f);   // 프레임 위쪽 바깥
			m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
			m_brush->SetColor(D2D1::ColorF(0.12f, 0.56f, 1.0f, 0.88f));
			m_rt->FillRoundedRectangle(D2D1::RoundedRect(bg, 3.0f, 3.0f), m_brush.Get());
			m_brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
			m_rt->DrawText(txt.c_str(), static_cast<UINT32>(txt.length()), s_fmtBadge.Get(), D2D1::RectF(bg.left + 6.0f, bg.top, bg.right - 2.0f, bg.bottom), m_brush.Get());
		}
	}

	// 최상위 프레임(Artboard): 캔버스 위에 Figma 스타일로 상단 이름 라벨 표시 (예: "Frame 1  1920 × 1080")
	if (isTopFrame && !uiComp && m_rt && m_brush) {
		static ComPtr<IDWriteFactory> s_dwLabel;
		static ComPtr<IDWriteTextFormat> s_fmtLabel;
		if (!s_dwLabel) {
			DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dwLabel.GetAddressOf()));
			if (s_dwLabel) {
				s_dwLabel->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
					DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"ko-kr", &s_fmtLabel);
				if (s_fmtLabel) {
					s_fmtLabel->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
					s_fmtLabel->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				}
			}
		}

		if (s_fmtLabel) {
			const D2D1_MATRIX_3X2_F fm = (worldXf * view.ToMatrix()).ToD2D();
			// 화면상 프레임의 좌상단 위치 계산 (로컬 (0, 0) 변환)
			const D2D1_POINT_2F screenTL = D2D1::Point2F(fm.dx, fm.dy);

			// 변환 행렬(스케일)이 반영된 실제 월드 가로/세로 크기 계산
			const Matrix2D& xf = frame->GetTransform();
			const float scaleX = std::sqrt(xf.m11 * xf.m11 + xf.m12 * xf.m12);
			const float scaleY = std::sqrt(xf.m21 * xf.m21 + xf.m22 * xf.m22);
			const float effectiveW = frame->GetWidth() * (scaleX > 0.0001f ? scaleX : 1.0f);
			const float effectiveH = frame->GetHeight() * (scaleY > 0.0001f ? scaleY : 1.0f);

			std::wstring labelText = frame->GetName();
			if (labelText.empty()) labelText = L"Artboard";
			if (!roleTag.empty()) labelText = L"[" + std::wstring(roleTag.begin(), roleTag.end()) + L"] " + labelText;
			wchar_t dimBuf[64];
			swprintf_s(dimBuf, L"  %d × %d", static_cast<int>(std::round(effectiveW)), static_cast<int>(std::round(effectiveH)));
			labelText += dimBuf;

			const float labelH = 18.0f;
			const float labelW = static_cast<float>(labelText.length()) * 7.5f + 16.0f;
			const D2D1_RECT_F labelBg = D2D1::RectF(screenTL.x, screenTL.y - labelH - 3.0f, screenTL.x + labelW, screenTL.y - 3.0f);

			m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
			// 반투명 다크 톤 라벨 배경
			m_brush->SetColor(D2D1::ColorF(0.16f, 0.17f, 0.20f, 0.88f));
			m_rt->FillRoundedRectangle(D2D1::RoundedRect(labelBg, 3.0f, 3.0f), m_brush.Get());
			// 미세 테두리
			m_brush->SetColor(D2D1::ColorF(0.35f, 0.38f, 0.44f, 0.6f));
			m_rt->DrawRoundedRectangle(D2D1::RoundedRect(labelBg, 3.0f, 3.0f), m_brush.Get(), 0.8f);
			// 텍스트 라벨
			m_brush->SetColor(D2D1::ColorF(0.88f, 0.90f, 0.94f, 1.0f));
			const D2D1_RECT_F textRect = D2D1::RectF(labelBg.left + 8.0f, labelBg.top, labelBg.right - 4.0f, labelBg.bottom);
			m_rt->DrawText(labelText.c_str(), static_cast<UINT32>(labelText.length()), s_fmtLabel.Get(), textRect, m_brush.Get());
		}
	}
}

static D2D1_COLOR_F ToD2D(const Color& c, float opacity)
{
	return D2D1::ColorF(c.r, c.g, c.b, c.a * opacity);
}

static D2D1_MATRIX_3X2_F ToD2D(const Matrix2D& m)
{
	return D2D1::Matrix3x2F(m.m11, m.m12, m.m21, m.m22, m.dx, m.dy);
}

ID2D1StrokeStyle* D2DRenderer::GetStrokeStyle(const Stroke& stroke)
{
	const bool hasDash = IsValidDashArray(stroke.dashArray);
	if (!m_factory || stroke.width <= 0.0f) return nullptr;
	if (!hasDash && stroke.cap == LineCap::Butt && stroke.join == LineJoin::Miter &&
	    std::abs(stroke.miterLimit - 4.0f) < 0.0001f)
		return nullptr;   // 기본 스타일이면 D2D 기본값 사용 (miterLimit 4 는 SVG 기본값과 같음)

	// D2D 사용자 정의 대시는 "선 두께의 배수" -> 절대값(문서 단위) dashArray 를 두께로 나눠 넘김
	std::vector<float> dashes;
	float offset = 0.0f;
	if (hasDash) {
		dashes.reserve(stroke.dashArray.size());
		for (float v : stroke.dashArray) dashes.push_back(v / stroke.width);
		offset = stroke.dashOffset / stroke.width;
	}

	std::string key = std::to_string(static_cast<int>(stroke.cap)) + "|" +
	                  std::to_string(static_cast<int>(stroke.join)) + "|" +
	                  std::to_string(stroke.miterLimit) + "|" + std::to_string(offset);
	for (float d : dashes) key += "|" + std::to_string(d);

	auto it = m_strokeStyleCache.find(key);
	if (it != m_strokeStyleCache.end()) return it->second.Get();

	D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT;
	switch (stroke.cap) {
	case LineCap::Round:  cap = D2D1_CAP_STYLE_ROUND;  break;
	case LineCap::Square: cap = D2D1_CAP_STYLE_SQUARE; break;
	default: break;
	}
	D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER_OR_BEVEL;   // SVG: 미터 한계를 넘으면 베벨
	switch (stroke.join) {
	case LineJoin::Round: join = D2D1_LINE_JOIN_ROUND; break;
	case LineJoin::Bevel: join = D2D1_LINE_JOIN_BEVEL; break;
	default: break;
	}

	D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
		cap, cap, cap, join, (std::max)(stroke.miterLimit, 1.0f),
		hasDash ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID, offset);

	ComPtr<ID2D1StrokeStyle> style;
	if (FAILED(m_factory->CreateStrokeStyle(props, hasDash ? dashes.data() : nullptr,
	                                         static_cast<UINT32>(dashes.size()), &style)))
		return nullptr;

	if (m_strokeStyleCache.size() > 256) m_strokeStyleCache.clear();   // 사용자 정의 대시가 무한히 쌓이지 않도록
	ID2D1StrokeStyle* raw = style.Get();
	m_strokeStyleCache[key] = std::move(style);
	return raw;
}

bool D2DRenderer::GetFillBounds(PathNode& node, Rect2& out)
{
	ID2D1Geometry* geo = GetGeometry(node);
	if (!geo) return false;
	D2D1_RECT_F r{};
	if (FAILED(geo->GetBounds(nullptr, &r))) return false;
	out = Rect2{ r.left, r.top, r.right, r.bottom };
	return true;
}

ID2D1Brush* D2DRenderer::GetGradientBrush(const Node& node, const Rect2& bounds)
{
	const Gradient& g = node.GetStyle().fill.gradient;
	const float w = bounds.right - bounds.left;
	const float h = bounds.bottom - bounds.top;
	if (!m_rt || !g.IsRenderable() || w < 1e-4f || h < 1e-4f) return nullptr;   // 박스가 0 이면 그라디언트 정의 불가

	GradientCache& c = m_gradientCache[node.GetId()];
	if (c.brush && c.version == node.GetVersion()) return c.brush.Get();

	// 스톱: offset 정렬/범위 제한은 모델이 보장하지 않을 수 있으므로 복사본을 정규화해서 사용
	Gradient norm = g;
	norm.NormalizeStops();
	std::vector<D2D1_GRADIENT_STOP> stops;
	stops.reserve(norm.stops.size());
	for (const GradientStop& s : norm.stops)
		stops.push_back(D2D1::GradientStop(s.offset, D2D1::ColorF(s.color.r, s.color.g, s.color.b, s.color.a)));

	ComPtr<ID2D1GradientStopCollection> coll;
	if (FAILED(m_rt->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()),
	                                               D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coll)))
		return nullptr;

	// 박스 비율 좌표 -> 도형 로컬 좌표 (브러시는 도형을 그리는 로컬 좌표계에서 해석됨)
	auto toLocal = [&](Vec2 p) { return D2D1::Point2F(bounds.left + p.x * w, bounds.top + p.y * h); };

	ComPtr<ID2D1Brush> brush;
	if (norm.kind == Gradient::Kind::Linear) {
		ComPtr<ID2D1LinearGradientBrush> lin;
		if (FAILED(m_rt->CreateLinearGradientBrush(
				D2D1::LinearGradientBrushProperties(toLocal(norm.start), toLocal(norm.end)), coll.Get(), &lin)))
			return nullptr;
		brush = lin;
	} else {
		const D2D1_POINT_2F center = toLocal(norm.start);
		const D2D1_POINT_2F focus = toLocal(norm.end);
		ComPtr<ID2D1RadialGradientBrush> rad;
		if (FAILED(m_rt->CreateRadialGradientBrush(
				D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(focus.x - center.x, focus.y - center.y),
				                                    (std::max)(norm.radius, 0.0001f) * w, (std::max)(norm.radius, 0.0001f) * h),
				coll.Get(), &rad)))
			return nullptr;
		brush = rad;
	}

	c.version = node.GetVersion();
	c.brush = brush;
	return c.brush.Get();
}

void D2DRenderer::DrawPath(PathNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	if (!m_rt || !m_brush) return;
	ID2D1Geometry* geo = GetGeometry(node);
	if (!geo) return;
	const Style& st = node.GetStyle();
	// 노드/그룹/레이어 불투명도는 DrawNode/DrawDocument 의 PushLayer 로 합성되므로 여기서는 색상 알파만 사용
	const float op = 1.0f;

	m_rt->SetTransform(ToD2D(worldXf * view.ToMatrix()));

	// 채우기: Solid 또는 Gradient. 그라디언트를 만들 수 없는 경우(스톱 2개 미만, 박스 크기 0)는 첫 스톱 색으로 대체
	Color fillColor;
	bool doFill = false;
	if (st.fill.type == PaintType::Solid) {
		fillColor = st.fill.color; doFill = true;
	} else if (st.fill.type == PaintType::Gradient) {
		Rect2 bounds{};
		ID2D1Brush* gb = GetFillBounds(node, bounds) ? GetGradientBrush(node, bounds) : nullptr;
		if (gb) {
			m_rt->FillGeometry(geo, gb);
		} else {
			fillColor = st.fill.gradient.FallbackColor(); doFill = true;
		}
	}
	if (doFill && fillColor.a > 0.0f) {
		m_brush->SetColor(ToD2D(fillColor, op));
		m_rt->FillGeometry(geo, m_brush.Get());
	}

	if (st.stroke.width > 0.0f && st.stroke.color.a > 0.0f) {
		m_brush->SetColor(ToD2D(st.stroke.color, op));
		m_rt->DrawGeometry(geo, m_brush.Get(), st.stroke.width, GetStrokeStyle(st.stroke));
	}
	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
}

ID2D1Geometry* D2DRenderer::GetGeometry(PathNode& node)
{
	if (!m_factory) return nullptr;
	GeoCache& c = m_geoCache[node.GetId()];
	if (!c.geo || c.version != node.GetVersion()) {
		c.geo = PathOp::GetCachedGeometry(node, m_factory);
		c.version = node.GetVersion();
	}
	return c.geo.Get();
}

ComPtr<ID2D1PathGeometry> D2DRenderer::BuildGeometry(const PathNode& node)
{
	return PathOp::GetCachedGeometry(node, m_factory);
}

HitResult PathNode::HitTestScreenSpace(D2D1_POINT_2F screenPt, float tolPx, const Viewport& view, ID2D1Factory* f)
{
	const float tolSq = tolPx * tolPx;
	const Matrix2D worldXf = GetWorldTransform();   // 그룹 안쪽 도형도 정확히 판정

	// Step 6.6: 월드 AABB 사전 기각 (Early-Out) — 화면 점을 월드 좌표로 바꾼 뒤 도형 월드 경계 밖이면 즉시 반환
	Rect2 lb{};
	if (GetLocalBounds(lb)) {
		const Rect2 wb = worldXf.TransformRect(lb);
		const Vec2 worldPt = view.ScreenToWorld(Vec2{ screenPt.x, screenPt.y });
		const float sx = std::sqrt(worldXf.m11 * worldXf.m11 + worldXf.m12 * worldXf.m12);
		const float sy = std::sqrt(worldXf.m21 * worldXf.m21 + worldXf.m22 * worldXf.m22);
		const float halfStrokeWorld = (m_style.stroke.width * 0.5f) * (std::max)(sx, sy);
		// 컨트롤 핸들이 밖으로 뻗어 있을 수 있으므로 핸들 여유분을 고려해 넉넉히 기각
		const float tolWorld = (tolPx + 12.0f) / (std::max)(view.zoom, 0.0001f) + halfStrokeWorld + 4.0f;
		if (worldPt.x < wb.left - tolWorld || worldPt.x > wb.right + tolWorld ||
		    worldPt.y < wb.top  - tolWorld || worldPt.y > wb.bottom + tolWorld) {
			// 단, 열린 핸들이 박스 밖으로 멀리 나가 있는 경우를 위해 앵커/핸들만 빠르게 확인
			bool nearHandle = false;
			for (const auto& sp : m_paths) {
				for (const auto& n : sp.nodes) {
					if (n.HasIn() || n.HasOut()) { nearHandle = true; break; }
				}
				if (nearHandle) break;
			}
			if (!nearHandle) return {};
		}
	}

	// 1순위: 앵커 및 핸들
	for (int spIdx = 0; spIdx < static_cast<int>(m_paths.size()); ++spIdx) {
		const auto& sp = m_paths[spIdx];
		for (int nIdx = 0; nIdx < static_cast<int>(sp.nodes.size()); ++nIdx) {
			const auto& node = sp.nodes[nIdx];
			D2D1_POINT_2F anchorScreen = CoordinateSpace::LocalToScreen(node.anchor.ToD2D(), worldXf, view);
			float dx = screenPt.x - anchorScreen.x;
			float dy = screenPt.y - anchorScreen.y;
			if ((dx * dx + dy * dy) <= tolSq) return { HitPart::Anchor, spIdx, nIdx, this };

			if (node.HasOut()) {
				D2D1_POINT_2F hScreen = CoordinateSpace::LocalToScreen(node.AbsOut().ToD2D(), worldXf, view);
				if (((screenPt.x - hScreen.x)*(screenPt.x - hScreen.x) + (screenPt.y - hScreen.y)*(screenPt.y - hScreen.y)) <= tolSq)
					return { HitPart::HandleOut, spIdx, nIdx, this };
			}
			if (node.HasIn()) {
				D2D1_POINT_2F hScreen = CoordinateSpace::LocalToScreen(node.AbsIn().ToD2D(), worldXf, view);
				if (((screenPt.x - hScreen.x)*(screenPt.x - hScreen.x) + (screenPt.y - hScreen.y)*(screenPt.y - hScreen.y)) <= tolSq)
					return { HitPart::HandleIn, spIdx, nIdx, this };
			}
		}
	}

	// 2순위: 스트로크 및 채우기 (Step 6.6: 임시 D2DRenderer 생성 없이 프로세스 공용 지오메트리 캐시 직접 조회)
	ComPtr<ID2D1PathGeometry> g = PathOp::GetCachedGeometry(*this, f);
	if (g) {
		const D2D1_MATRIX_3X2_F m = ToD2D(worldXf * view.ToMatrix());
		const float w = (std::max)(m_style.stroke.width * view.zoom, 1.0f) + tolPx * 2.0f;
		const float lw = w / (std::max)(view.zoom, 0.0001f);

		BOOL hit = FALSE;
		if (SUCCEEDED(g->StrokeContainsPoint(screenPt, lw, nullptr, &m, 0.25f, &hit)) && hit) {
			return { HitPart::Stroke, -1, -1, this };
		}

		// 닫힌 도형이거나 채우기 스타일이 지정된 경우 내부 클릭 히트 허용
		bool isClosed = false;
		for (const auto& sp : m_paths) {
			if (sp.closed) { isClosed = true; break; }
		}
		const bool checkFill = (m_style.fill.type != PaintType::None) || (m_style.fill.color.a > 0.0f) || isClosed;
		if (checkFill) {
			hit = FALSE;
			if (SUCCEEDED(g->FillContainsPoint(screenPt, &m, 0.25f, &hit)) && hit) {
				return { HitPart::Fill, -1, -1, this };
			}
		}
		return {};
	}

	// 3순위: D2D 팩터리가 없는 순수 CPU 환경 폴백 (g == nullptr 일 때만 수행)
	Matrix2D invWorld = worldXf;
	if (invWorld.Invert()) {
		Vec2 worldPt = view.ScreenToWorld(Vec2{ screenPt.x, screenPt.y });
		Vec2 localPt = invWorld.TransformPoint(worldPt);
		int spIdx = -1, segIdx = -1;
		float tVal = 0.0f, distSq = 1e30f;
		if (FindClosestSegment(localPt, spIdx, segIdx, tVal, distSq)) {
			float distScreen = std::sqrt(distSq) * view.zoom;
			float halfStrokePx = (m_style.stroke.width * 0.5f) * view.zoom;
			if (distScreen <= (halfStrokePx + tolPx)) {
				return { HitPart::Stroke, -1, -1, this };
			}
		}
	}

	return {};
}

} // namespace ge
