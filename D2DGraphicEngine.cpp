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
	const float left = (m_align == TextAlign::Center) ? -w * 0.5f : (m_align == TextAlign::Right) ? -w : 0.0f;
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
	bool any = false;
	Rect2 box{ 1e9f, 1e9f, -1e9f, -1e9f };
	auto add = [&](Vec2 p) {
		box.left = (std::min)(box.left, p.x);   box.right  = (std::max)(box.right, p.x);
		box.top  = (std::min)(box.top, p.y);    box.bottom = (std::max)(box.bottom, p.y);
		any = true;
	};
	for (const auto& sp : m_paths)
		for (const auto& n : sp.nodes) {
			add(n.anchor);
			if (n.HasIn())  add(n.AbsIn());
			if (n.HasOut()) add(n.AbsOut());
		}
	if (any) {
		if (box.right - box.left < 2.0f) { box.left -= 1.0f; box.right += 1.0f; }
		if (box.bottom - box.top < 2.0f) { box.top -= 1.0f; box.bottom += 1.0f; }
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
		if (p->IsLocked()) return true;
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
	copy->m_w = m_w; copy->m_h = m_h; copy->m_clip = m_clip; copy->m_radius = m_radius;
	for (const auto& c : m_children) copy->AddChild(c->Clone());
	return copy;
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

Node* Document::AddShape(std::unique_ptr<Node> node)
{
	LayerNode* layer = GetActiveLayer();
	if (!layer || !node) return nullptr;
	AssignIds(*node);
	return layer->AddChild(std::move(node));
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
	case ShapeKind::Gear:     prefix = L"gear"; break;
	case ShapeKind::Spiral:   prefix = L"spiral"; break;
	case ShapeKind::Heart:    prefix = L"heart"; break;
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

// ============================================================================
// 2.6 Path Operations / Pathfinder (불린 연산 뼈대 구현)
// ============================================================================
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
	ComPtr<IDWriteFactory> dw;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf())))) return;
	ComPtr<IDWriteTextFormat> fmt;
	if (FAILED(dw->CreateTextFormat(text.GetFontFamily().c_str(), nullptr,
		text.IsBold() ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
		text.IsItalic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, text.GetFontSize(), L"ko-kr", &fmt))) return;
	fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, text.GetFontSize() * TextNode::kLineHeightRatio, text.GetFontSize() * TextNode::kAscentRatio);
	fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	const std::wstring& txt = text.GetText();
	ComPtr<IDWriteTextLayout> layout;
	if (FAILED(dw->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()), fmt.Get(), 100000.0f, 100000.0f, &layout))) return;
	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	text.SetMeasured((std::max)(tm.widthIncludingTrailingWhitespace, 1.0f), (std::max)(tm.height, 1.0f));
}

std::unique_ptr<PathNode> PathOp::TextToPath(const TextNode& text, ID2D1Factory* factory)
{
	if (!factory || text.GetText().empty()) return nullptr;

	ComPtr<IDWriteFactory> dw;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf())))) return nullptr;
	ComPtr<IDWriteTextFormat> fmt;
	if (FAILED(dw->CreateTextFormat(text.GetFontFamily().c_str(), nullptr,
		text.IsBold() ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
		text.IsItalic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, text.GetFontSize(), L"ko-kr", &fmt))) return nullptr;
	fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, text.GetFontSize() * TextNode::kLineHeightRatio, text.GetFontSize() * TextNode::kAscentRatio);
	fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

	const std::wstring& txt = text.GetText();
	ComPtr<IDWriteTextLayout> layout;
	if (FAILED(dw->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()), fmt.Get(), 100000.0f, 100000.0f, &layout))) return nullptr;
	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	const float w = (std::max)(tm.widthIncludingTrailingWhitespace, 1.0f);
	if (text.GetAlign() != TextAlign::Left) {
		layout->SetMaxWidth(w);
		layout->SetTextAlignment(text.GetAlign() == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
	}
	const float x0 = (text.GetAlign() == TextAlign::Center) ? -w * 0.5f : (text.GetAlign() == TextAlign::Right) ? -w : 0.0f;

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

	switch (node.Kind()) {
	case NodeKind::Path:  DrawPath(static_cast<PathNode&>(node), worldXf, view); break;
	case NodeKind::Text:  DrawTextNode(static_cast<TextNode&>(node), worldXf, view); break;
	case NodeKind::Image: DrawImageNode(static_cast<ImageNode&>(node), worldXf, view); break;
	case NodeKind::Group:
	case NodeKind::Layer: DrawGroup(static_cast<GroupNode&>(node), worldXf, view); break;
	}

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

void D2DRenderer::DrawTextNode(TextNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	if (!m_rt || !m_brush || node.GetText().empty()) return;

	static ComPtr<IDWriteFactory> s_dw;
	if (!s_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(s_dw.GetAddressOf())))) return;

	// 레이아웃 캐시: 노드 버전은 프로세스 전역에서 유일하고 글자/글꼴이 바뀔 때만 바뀐다 (이동/크기 조절은 버전 불변).
	// 매 프레임 포맷/레이아웃을 새로 만들면 크기 조절처럼 자주 다시 그리는 동안 버벅거리므로 재사용
	struct TextCache { ComPtr<IDWriteTextLayout> layout; float w = 0, h = 0; };
	static std::unordered_map<uint32_t, TextCache> s_cache;
	auto hit = s_cache.find(node.GetVersion());
	if (hit == s_cache.end()) {
		ComPtr<IDWriteTextFormat> fmt;
		if (FAILED(s_dw->CreateTextFormat(node.GetFontFamily().c_str(), nullptr,
			node.IsBold() ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
			node.IsItalic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, node.GetFontSize(), L"ko-kr", &fmt))) return;
		fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, node.GetFontSize() * TextNode::kLineHeightRatio, node.GetFontSize() * TextNode::kAscentRatio);
		fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

		const std::wstring& txt = node.GetText();
		TextCache entry;
		if (FAILED(s_dw->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()), fmt.Get(), 100000.0f, 100000.0f, &entry.layout))) return;
		DWRITE_TEXT_METRICS tm{};
		entry.layout->GetMetrics(&tm);
		entry.w = (std::max)(tm.widthIncludingTrailingWhitespace, 1.0f);
		entry.h = (std::max)(tm.height, 1.0f);
		// 가운데/오른쪽 정렬: 각 줄을 가장 긴 줄의 폭 안에서 정렬 (SVG text-anchor 와 같은 모양)
		if (node.GetAlign() != TextAlign::Left) {
			entry.layout->SetMaxWidth(entry.w);
			entry.layout->SetTextAlignment(node.GetAlign() == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
		}
		if (s_cache.size() > 512) s_cache.clear();
		hit = s_cache.emplace(node.GetVersion(), std::move(entry)).first;
	}
	IDWriteTextLayout* layout = hit->second.layout.Get();
	if (node.MeasuredWidth() != hit->second.w || node.MeasuredHeight() != hit->second.h) node.SetMeasured(hit->second.w, hit->second.h);
	DWRITE_TEXT_METRICS tm{};
	tm.widthIncludingTrailingWhitespace = hit->second.w;

	// 정렬 기준점(로컬 원점)에 맞춰 왼쪽 시작 위치 결정
	float x0 = 0.0f;
	if (node.GetAlign() == TextAlign::Center) x0 = -tm.widthIncludingTrailingWhitespace * 0.5f;
	else if (node.GetAlign() == TextAlign::Right) x0 = -tm.widthIncludingTrailingWhitespace;

	const Style& st = node.GetStyle();
	const Color col = (st.fill.type == PaintType::None) ? Color(0, 0, 0, 0) : st.fill.color;
	m_brush->SetColor(D2D1::ColorF(col.r, col.g, col.b, col.a));
	m_rt->SetTransform((worldXf * view.ToMatrix()).ToD2D());
	m_rt->DrawTextLayout(D2D1::Point2F(x0, 0.0f), layout, m_brush.Get());
	m_rt->SetTransform(D2D1::Matrix3x2F::Identity());   // 이후 그리기(눈금자/가이드)가 이 변환을 물려받지 않도록 복원
}

void D2DRenderer::DrawGroup(GroupNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	const bool isClipMask = node.IsClippingMask();
	Node* maskNode = isClipMask ? node.GetMaskNode() : nullptr;

	ComPtr<ID2D1Geometry> maskGeo;

	// 프레임: 배경/테두리를 먼저 그리고, 자식은 프레임 상자로 클리핑
	const auto* frame = (node.Kind() == NodeKind::Group) ? dynamic_cast<const FrameNode*>(&node) : nullptr;
	if (frame && m_rt && m_brush) {
		const D2D1_MATRIX_3X2_F fm = (worldXf * view.ToMatrix()).ToD2D();
		const Style& fst = frame->GetStyle();
		const D2D1_RECT_F rc = D2D1::RectF(0.0f, 0.0f, frame->GetWidth(), frame->GetHeight());
		const float rr = (std::min)(frame->GetCornerRadius(), (std::min)(frame->GetWidth(), frame->GetHeight()) * 0.5f);
		m_rt->SetTransform(fm);
		if (fst.fill.type == PaintType::Solid && fst.fill.color.a > 0.0f) {
			m_brush->SetColor(D2D1::ColorF(fst.fill.color.r, fst.fill.color.g, fst.fill.color.b, fst.fill.color.a));
			if (rr > 0.0f) m_rt->FillRoundedRectangle(D2D1::RoundedRect(rc, rr, rr), m_brush.Get());
			else m_rt->FillRectangle(rc, m_brush.Get());
		}
		if (fst.stroke.width > 0.0f && fst.stroke.color.a > 0.0f) {
			m_brush->SetColor(D2D1::ColorF(fst.stroke.color.r, fst.stroke.color.g, fst.stroke.color.b, fst.stroke.color.a));
			if (rr > 0.0f) m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rc, rr, rr), m_brush.Get(), fst.stroke.width);
			else m_rt->DrawRectangle(rc, m_brush.Get(), fst.stroke.width);
		}
		m_rt->SetTransform(D2D1::Matrix3x2F::Identity());

		if (frame->IsClip() && m_factory) {
			ComPtr<ID2D1RoundedRectangleGeometry> rg;
			if (SUCCEEDED(m_factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rc, rr, rr), &rg))) {
				ComPtr<ID2D1TransformedGeometry> xg;
				if (SUCCEEDED(m_factory->CreateTransformedGeometry(rg.Get(), &fm, &xg))) maskGeo = xg;
			}
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

ID2D1Brush* D2DRenderer::GetGradientBrush(PathNode& node, const Rect2& bounds)
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
		c.geo = BuildGeometry(node);
		c.version = node.GetVersion();
	}
	return c.geo.Get();
}

ComPtr<ID2D1PathGeometry> D2DRenderer::BuildGeometry(const PathNode& node)
{
	return PathOp::BuildGeometry(node, m_factory);
}

// ============================================================================
// 4. HitTester
// ============================================================================
static bool HitNode(Node& n, Vec2 sp, float tol, const Viewport& view, D2DRenderer& r, HitResult& out)
{
	if (!n.IsVisible() || n.IsLocked()) return false;
	if (n.Kind() == NodeKind::Text || n.Kind() == NodeKind::Image) return false;   // 텍스트/이미지 히트는 선택 도구가 상자로 처리
	if (n.Kind() != NodeKind::Path) {
		const auto& ch = static_cast<GroupNode&>(n).GetChildren();
		for (int i = (int)ch.size() - 1; i >= 0; --i)
			if (HitNode(*ch[i], sp, tol, view, r, out)) return true;
		return false;
	}
	PathNode& p = static_cast<PathNode&>(n);
	ID2D1PathGeometry* geo = static_cast<ID2D1PathGeometry*>(r.GetGeometry(p));
	if (!geo) return false;
	const D2D1_MATRIX_3X2_F m = ToD2D(p.GetWorldTransform() * view.ToMatrix());
	const D2D1_POINT_2F pt = D2D1::Point2F(sp.x, sp.y);
	const Style& st = p.GetStyle();
	BOOL hit = FALSE;
	const float w = (std::max)(st.stroke.width * view.zoom, 1.0f) + tol * 2.0f;
	// 스트로크: 화면 좌표에서 검사하도록 변환 적용한 지오메트리 대신 로컈 폭으로 환산
	const float lw = w / (std::max)(view.zoom, 0.0001f);
	if (SUCCEEDED(geo->StrokeContainsPoint(pt, lw, nullptr, &m, &hit)) && hit) {
		out.part = HitPart::Stroke; out.node = &p; return true;
	}
	if (st.fill.type != PaintType::None && SUCCEEDED(geo->FillContainsPoint(pt, &m, &hit)) && hit) {
		out.part = HitPart::Fill; out.node = &p; return true;
	}
	return false;
}

HitResult HitTester::HitTest(Document& doc, Vec2 screenPt, float tolPx,
							 const Viewport& view, D2DRenderer& renderer)
{
	HitResult res;
	// 1) 선택된 패스의 앵커/핸들
	auto isNear = [&](Vec2 world) {
		const Vec2 s = view.WorldToScreen(world);
		const float dx = s.x - screenPt.x, dy = s.y - screenPt.y;
		return dx * dx + dy * dy <= tolPx * tolPx;
	};
	for (Node* sel : doc.GetSelection()) {
		if (!sel || sel->Kind() != NodeKind::Path || sel->IsEffectivelyLocked() || !sel->IsEffectivelyVisible()) continue;
		PathNode& p = static_cast<PathNode&>(*sel);
		const Matrix2D w = p.GetWorldTransform();
		const auto& paths = p.GetPaths();
		for (int si = 0; si < (int)paths.size(); ++si)
			for (int ni = (int)paths[si].nodes.size() - 1; ni >= 0; --ni) {
				const BezierNode& b = paths[si].nodes[ni];
				auto set = [&](HitPart part) { res.part = part; res.node = &p; res.subPath = si; res.nodeIdx = ni; };
				if (b.HasOut() && isNear(w.TransformPoint(b.AbsOut()))) { set(HitPart::HandleOut); return res; }
				if (b.HasIn()  && isNear(w.TransformPoint(b.AbsIn())))  { set(HitPart::HandleIn);  return res; }
				if (isNear(w.TransformPoint(b.anchor)))                 { set(HitPart::Anchor);    return res; }
			}
	}
	// 2) 위 레이어부터 역순
	const auto& layers = doc.GetLayers();
	for (int i = (int)layers.size() - 1; i >= 0; --i)
		if (HitNode(*layers[i], screenPt, tolPx, view, renderer, res)) return res;
	return {};
}

Node* HitTester::FindSelectable(Node* hit)
{
	while (hit && hit->GetParent() && hit->GetParent()->Kind() != NodeKind::Layer)
		hit = hit->GetParent();
	return hit;
}

// ============================================================================
// 5. Command
// ============================================================================
void CommandStack::Execute(std::unique_ptr<ICommand> cmd, Document& doc)
{
	if (!cmd) return;
	cmd->Execute(doc);
	m_undo.push_back(std::move(cmd));
	m_redo.clear();
}

void CommandStack::Undo(Document& doc)
{
	if (m_undo.empty()) return;
	auto cmd = std::move(m_undo.back());
	m_undo.pop_back();
	cmd->Undo(doc);
	m_redo.push_back(std::move(cmd));
}

void CommandStack::Redo(Document& doc)
{
	if (m_redo.empty()) return;
	auto cmd = std::move(m_redo.back());
	m_redo.pop_back();
	cmd->Execute(doc);
	m_undo.push_back(std::move(cmd));
}

void CommandStack::Clear()
{
	m_undo.clear();
	m_redo.clear();
}

HitResult PathNode::HitTestScreenSpace(D2D1_POINT_2F screenPt, float tolPx, const Viewport& view, ID2D1Factory* f)
{
	const float tolSq = tolPx * tolPx;
	const Matrix2D worldXf = GetWorldTransform();   // 그룹 안쪽 도형도 정확히 판정

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

	// 2순위: 스트로크 및 채우기
	// D2D 변환 행렬(월드 Transform * 뷰포트 행렬)을 StrokeContainsPoint / FillContainsPoint에 직접 전달하여 정확한 화면 판정 수행
	D2DRenderer renderer;
	renderer.Initialize(nullptr, f);   // 렌더 타깃 없이 팩터리만 사용 (반환값 false 는 정상)
	{
		auto* g = renderer.GetGeometry(*this);
		if (g) {
			const D2D1_MATRIX_3X2_F m = ToD2D(GetWorldTransform() * view.ToMatrix());
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
		}
	}

	// 3순위: Direct2D 기하 판정 실패 시 수학적 선분/곡선 거리 계산 (100% 신뢰성 보장)
	// 화면상의 점을 월드 및 로컬 좌표계로 역변환하여 패스의 모든 베이지어/직선 선분과의 최근접 거리를 직접 검사
	Matrix2D invWorld = GetWorldTransform();
	if (invWorld.Invert()) {
		Vec2 worldPt = view.ScreenToWorld(Vec2{ screenPt.x, screenPt.y });
		Vec2 localPt = invWorld.TransformPoint(worldPt);
		int spIdx = -1, segIdx = -1;
		float tVal = 0.0f, distSq = 1e30f;
		if (FindClosestSegment(localPt, spIdx, segIdx, tVal, distSq)) {
			// 로컬 거리를 화면 픽셀 거리로 환산
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
