#include "pch.h"
#include "D2DGraphicEngine.h"

namespace ge {

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
		TransformPoint({ r.left,  r.top }),    TransformPoint({ r.right, r.top }),
		TransformPoint({ r.right, r.bottom }), TransformPoint({ r.left,  r.bottom })
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
	copy->m_parent = nullptr;
	copy->m_dirty = true;
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
	if (any) out = box;   // TODO: 곡선 실제 극값 기반 정밀 바운딩
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

// ---- GroupNode --------------------------------------------------------------
std::unique_ptr<Node> GroupNode::Clone() const
{
	auto copy = std::make_unique<GroupNode>();
	copy->m_name = m_name; copy->m_xf = m_xf; copy->m_style = m_style;
	copy->m_visible = m_visible; copy->m_locked = m_locked; copy->m_opacity = m_opacity;
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
	int i = IndexOf(child);
	if (i < 0 || i == static_cast<int>(m_children.size()) - 1) return;
	std::rotate(m_children.begin() + i, m_children.begin() + i + 1, m_children.end());
}

void GroupNode::SendToBack(Node* child)
{
	int i = IndexOf(child);
	if (i <= 0) return;
	std::rotate(m_children.begin(), m_children.begin() + i, m_children.begin() + i + 1);
}

void GroupNode::BringForward(Node* child)
{
	int i = IndexOf(child);
	if (i < 0 || i + 1 >= static_cast<int>(m_children.size())) return;
	std::swap(m_children[i], m_children[i + 1]);
}

void GroupNode::SendBackward(Node* child)
{
	int i = IndexOf(child);
	if (i <= 0) return;
	std::swap(m_children[i], m_children[i - 1]);
}

// ---- LayerNode --------------------------------------------------------------
std::unique_ptr<Node> LayerNode::Clone() const
{
	auto copy = std::make_unique<LayerNode>();
	copy->m_name = m_name; copy->m_xf = m_xf; copy->m_style = m_style;
	copy->m_visible = m_visible; copy->m_locked = m_locked; copy->m_opacity = m_opacity;
	for (const auto& c : m_children) copy->AddChild(c->Clone());
	return copy;
}

// ---- Document ---------------------------------------------------------------
Document::Document()
{
	AddLayer(L"Layer 1");
}

LayerNode* Document::AddLayer(const std::wstring& name, int index)
{
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
	// TODO: 레이어 순서 변경 (m_activeLayer 보정 포함)
	(void)layer; (void)newIndex;
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
	node->m_id = IssueId();
	return layer->AddChild(std::move(node));
}

GroupNode* Document::GroupSelection()   { /* TODO: 선택 노드 -> GroupNode 로 이동 */ return nullptr; }
void Document::UngroupSelection()       { /* TODO: 그룹 자식을 월드 변환 적용해 부모로 복원 */ }
void Document::AlignSelection(AlignMode, bool) { /* TODO */ }
void Document::DistributeSelection(bool)       { /* TODO */ }

// ============================================================================
// 3. Renderer (D2D)
// ============================================================================
bool D2DRenderer::Initialize(ID2D1RenderTarget* rt, ID2D1Factory* factory)
{
	m_rt = rt;
	m_factory = factory;
	m_brush.Reset();
	if (!m_rt) return false;
	return SUCCEEDED(m_rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &m_brush));
}

void D2DRenderer::Shutdown()
{
	m_geoCache.clear();
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
		DrawNode(*layer, Matrix2D::Identity(), view);
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

void D2DRenderer::DrawPath(PathNode& node, const Matrix2D& worldXf, const Viewport& view)
{
	// TODO: GetGeometry -> SetTransform(worldXf * view) -> Fill / Stroke (dash 포함)
	(void)node; (void)worldXf; (void)view;
}

ID2D1Geometry* D2DRenderer::GetGeometry(PathNode& node)
{
	// TODO: m_geoCache 에서 node.GetId() 검색, dirty 면 BuildGeometry 로 재생성
	(void)node;
	return nullptr;
}

ComPtr<ID2D1PathGeometry> D2DRenderer::BuildGeometry(const PathNode& node)
{
	// TODO: 기존 PathShape::Rebuild 이식 (AddLine / AddBezier, closed 처리)
	(void)node;
	return nullptr;
}

// ============================================================================
// 4. HitTester
// ============================================================================
HitResult HitTester::HitTest(Document& doc, Vec2 screenPt, float tolPx,
							 const Viewport& view, ID2D1Factory* factory)
{
	// TODO: 레이어/자식을 역순 순회, !visible || locked 제외,
	//       앵커 -> 핸들 -> 스트로크 -> 채움 순 판정 (기존 HitTestScreenSpace 이식)
	(void)doc; (void)screenPt; (void)tolPx; (void)view; (void)factory;
	return {};
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

} // namespace ge
