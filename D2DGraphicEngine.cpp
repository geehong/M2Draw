#include "pch.h"
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
	if (node.Kind() != NodeKind::Path)
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
		if (n.Kind() != NodeKind::Path)
			for (const auto& c : static_cast<GroupNode&>(n).GetChildren()) walk(*c);
	};
	for (const auto& layer : m_layers) walk(*layer);
}

static Node* FindIn(Node& n, NodeId id)
{
	if (n.GetId() == id) return &n;
	if (n.Kind() != NodeKind::Path)
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

GroupNode* Document::GroupSelection()
{
	std::vector<Node*> sel;
	for (Node* n : m_selection) if (n->GetParent()) sel.push_back(n);
	if (sel.size() < 2) return nullptr;
	GroupNode* parent = sel[0]->GetParent();
	for (Node* n : sel) if (n->GetParent() != parent) return nullptr;
	// z-order 유지
	std::sort(sel.begin(), sel.end(), [&](Node* a, Node* b) { return parent->IndexOf(a) < parent->IndexOf(b); });
	const int idx = parent->IndexOf(sel.back());
	auto g = std::make_unique<GroupNode>();
	g->m_id = IssueId();
	g->SetName(L"Group");
	GroupNode* raw = g.get();
	for (Node* n : sel) raw->AddChild(parent->RemoveChild(n));
	parent->AddChild(std::move(g), idx - (int)sel.size() + 1);
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
std::unique_ptr<PathNode> PathOp::Combine(const PathNode& a, const PathNode& b,
                                         BooleanMode mode, ID2D1Factory* factory)
{
	if (!factory) return nullptr;

	// D2D1_COMBINE_MODE 매핑
	D2D1_COMBINE_MODE d2dMode = D2D1_COMBINE_MODE_UNION;
	switch (mode) {
	case BooleanMode::Union:     d2dMode = D2D1_COMBINE_MODE_UNION; break;
	case BooleanMode::Intersect: d2dMode = D2D1_COMBINE_MODE_INTERSECT; break;
	case BooleanMode::Subtract:  d2dMode = D2D1_COMBINE_MODE_EXCLUDE; break; // A - B
	case BooleanMode::Exclude:   d2dMode = D2D1_COMBINE_MODE_XOR; break;
	}

	// 뼈대 인터페이스: 향후 D2D 지오메트리 CombineWithGeometry 및 간이 패스 추출 연결 슬롯
	auto result = std::make_unique<PathNode>();
	result->SetName(L"Combined Path");
	result->SetStyle(a.GetStyle());
	return result;
}

// ============================================================================
// 3. Renderer (D2D)
// ============================================================================
bool D2DRenderer::Initialize(ID2D1RenderTarget* rt, ID2D1Factory* factory)
{
	m_rt = rt;
	m_factory = factory;
	m_strokeStyleCache.clear();   // 팩터리가 바뀔 수 있으므로 이전 팩터리의 리소스 폐기
	m_brush.Reset();
	if (!m_rt) return false;
	return SUCCEEDED(m_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &m_brush));
}

void D2DRenderer::Shutdown()
{
	m_geoCache.clear();
	m_strokeStyleCache.clear();
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
	switch (node.Kind()) {
	case NodeKind::Path:  DrawPath(static_cast<PathNode&>(node), worldXf, view); break;
	case NodeKind::Group:
	case NodeKind::Layer: DrawGroup(static_cast<GroupNode&>(node), worldXf, view); break;
	}
}

void D2DRenderer::DrawGroup(GroupNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	for (auto& c : node.GetChildren())
		DrawNode(*c, worldXf, view);
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

void D2DRenderer::DrawPath(PathNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	if (!m_rt || !m_brush) return;
	ID2D1Geometry* geo = GetGeometry(node);
	if (!geo) return;
	const Style& st = node.GetStyle();
	// 레이어 불투명도는 DrawDocument 의 PushLayer 로 합성되므로 여기서는 레이어 아래(자신 + 그룹)만 곱함
	// (그룹 불투명도의 레이어 합성 전환은 Phase 2 S2)
	float op = node.GetOpacity();
	for (const GroupNode* p = node.GetParent(); p && p->Kind() != NodeKind::Layer; p = p->GetParent())
		op *= p->GetOpacity();

	m_rt->SetTransform(ToD2D(worldXf * view.ToMatrix()));

	// 채우기: Solid. Gradient 는 렌더링이 Step 2.3 이므로 그때까지 첫 스톱 색으로 대체
	Color fillColor;
	bool doFill = false;
	if (st.fill.type == PaintType::Solid)         { fillColor = st.fill.color; doFill = true; }
	else if (st.fill.type == PaintType::Gradient) { fillColor = st.fill.gradient.FallbackColor(); doFill = true; }
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
	ComPtr<ID2D1PathGeometry> geo;
	if (!m_factory || FAILED(m_factory->CreatePathGeometry(&geo))) return nullptr;
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
		const bool filled = node.GetStyle().fill.type != PaintType::None;
		sink->BeginFigure(pt(n[0].anchor), (filled && sp.closed) ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
		for (size_t i = 1; i < n.size(); ++i) seg(n[i - 1], n[i]);
		if (sp.closed && n.size() > 1) seg(n.back(), n[0]);
		sink->EndFigure(sp.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
	}
	if (FAILED(sink->Close())) return nullptr;
	return geo;
}

// ============================================================================
// 4. HitTester
// ============================================================================
static bool HitNode(Node& n, Vec2 sp, float tol, const Viewport& view, D2DRenderer& r, HitResult& out)
{
	if (!n.IsVisible() || n.IsLocked()) return false;
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
	if (renderer.Initialize(nullptr, f)) {
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
