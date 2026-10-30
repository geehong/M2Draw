#include "pch.h"
#include "ITool.h"
#include "HtmlComponents/TableComponent.h"
#include "M2DrawDoc.h"
#include "M2DrawView.h"
#include "M2Draw.h"
#include "MainFrm.h"

// ============================================================================
// SelectTool 구현 (도형 선택, 이동, 스케일, 회전, 에디트 포인트 편집)
// ============================================================================

// 레이어 가시성 확인 (레이어 자체가 잠겨 있어도 잠기지 않은 하위 노드는 개별 선택/편집 가능)
static bool IsLayerInteractive(const ge::LayerNode* pLayer)
{
	return pLayer && pLayer->IsVisible();
}

// 노드 상호작용 가능 여부 (자신의 잠금 + 단일 컴포넌트/일반 그룹 잠금 및 가시성 체인)
static bool IsInteractive(const ge::Node* n)
{
	return n && n->IsEffectivelyVisible() && !n->IsEffectivelyLocked();
}

// 선택 가능한 최상위 후보를 위에서 아래로 순회 (위 레이어 -> 아래 레이어, 레이어 안에서도 위 -> 아래).
// 부모(레이어/일반 프레임)가 잠겨 있어도 그 안의 잠기지 않은 자식을 탐색할 수 있도록 컨테이너는 통과한다.
template <typename Fn>
static void ForEachSelectableTopDown(CM2DrawDoc* pDoc, Fn fn)
{
	// 격리 편집(5.3) 중에는 격리 그룹의 직속 자식만 선택 단위가 되고, 나머지는 선택/히트되지 않는다
	if (ge::GroupNode* iso = pDoc->GetDoc().GetIsolationGroup())
	{
		if (iso->IsEffectivelyVisible())
		{
			const auto& kids = iso->GetChildren();
			for (int i = static_cast<int>(kids.size()) - 1; i >= 0; --i)
			{
				ge::Node* n = kids[i].get();
				if (!n || !n->IsVisible()) continue;
				if (n->IsLocked() && !ge::AllowsChildSelection(n)) continue;
				if (fn(n)) return;
			}
		}
		return;
	}
	const auto& layers = pDoc->GetDoc().GetLayers();
	for (int li = static_cast<int>(layers.size()) - 1; li >= 0; --li)
	{
		const ge::LayerNode* pLayer = layers[li].get();
		if (!IsLayerInteractive(pLayer)) continue;
		const auto& children = pLayer->GetChildren();
		for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
		{
			ge::Node* n = children[i].get();
			if (!n || !n->IsVisible()) continue;
			if (n->IsLocked() && !ge::AllowsChildSelection(n)) continue;
			if (fn(n)) return;
		}
	}
}

// 개별 노드 자체(자식 제외)가 화면 좌표에 히트되는지 판정
static bool HitTestSingleNode(ge::Node* n, D2D1_POINT_2F screenPt, const ViewportTransform& view, ID2D1Factory* f)
{
	if (!n || !n->IsVisible() || n->IsLocked()) return false;

	if (auto* tb = dynamic_cast<ge::TableNode*>(n))
	{
		ge::Matrix2D inv = tb->GetWorldTransform();
		ge::Rect2 b;
		if (inv.Invert() && tb->GetLocalBounds(b))
		{
			const ge::Vec2 l = inv.TransformPoint(view.ScreenToWorld(ge::Vec2{ screenPt.x, screenPt.y }));
			if (l.x >= b.left && l.x <= b.right && l.y >= b.top && l.y <= b.bottom) return true;
		}
		return false;
	}

	if (auto* fr = dynamic_cast<ge::FrameNode*>(n))
	{
		ge::Matrix2D inv = fr->GetWorldTransform();
		if (inv.Invert())
		{
			const ge::Vec2 l = inv.TransformPoint(view.ScreenToWorld(ge::Vec2{ screenPt.x, screenPt.y }));
			if (l.x >= 0.0f && l.x <= fr->GetWidth() && l.y >= 0.0f && l.y <= fr->GetHeight()) return true;
		}
		return false;
	}

	if (n->Kind() == ge::NodeKind::Group)
	{
		return false;
	}

	if (n->Kind() == ge::NodeKind::Text || n->Kind() == ge::NodeKind::Image)
	{
		ge::Rect2 b;
		ge::Matrix2D inv = n->GetWorldTransform();
		if (!n->GetLocalBounds(b) || !inv.Invert()) return false;
		const ge::Vec2 l = inv.TransformPoint(view.ScreenToWorld(ge::Vec2{ screenPt.x, screenPt.y }));
		return l.x >= b.left && l.x <= b.right && l.y >= b.top && l.y <= b.bottom;
	}

	ge::PathNode* ps = dynamic_cast<ge::PathNode*>(n);
	return ps && ps->HitTestScreenSpace(screenPt, 8.0f, view, f).part != HitResult::Part::None;
}

// 씬 그래프 깊은 탐색: 가장 안쪽의 히트된 말단 노드 반환 (위 -> 아래)
// 일반 프레임(AllowsChildSelection)은 자기 자신이 잠겨 있어도 내부의 잠기지 않은 자식을 탐색할 수 있다.
static ge::Node* HitTestDeepNode(ge::Node* n, D2D1_POINT_2F screenPt, const ViewportTransform& view, ID2D1Factory* f)
{
	if (!n || !n->IsVisible()) return nullptr;
	if (n->IsLocked() && !ge::AllowsChildSelection(n)) return nullptr;

	if (auto* group = dynamic_cast<ge::GroupNode*>(n))
	{
		const auto& children = group->GetChildren();
		for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
		{
			if (ge::Node* hit = HitTestDeepNode(children[i].get(), screenPt, view, f))
				return hit;
		}
	}

	if (HitTestSingleNode(n, screenPt, view, f))
		return n;

	return nullptr;
}

// 레이어/프레임 계층을 고려한 선택 대상 노드 반환:
// - 컴포넌트(버튼 등)는 자식 텍스트와 프레임이 분리되지 않고 하나의 컴포넌트 오브젝트로 선택됨
// - 일반 프레임(FrameNode) 안의 자식 객체(버튼, 테이블, 텍스트, 도형 등)는 개별 선택 가능함
// - 일반 그룹(Ctrl+G)은 그룹 단위로 선택됨
static ge::Node* HitTestTopShape(CM2DrawDoc* pDoc, D2D1_POINT_2F screenPt, const ViewportTransform& view, ID2D1Factory* f)
{
	ge::Node* hitTarget = nullptr;
	ge::GroupNode* isoGroup = pDoc->GetDoc().GetIsolationGroup();
	ForEachSelectableTopDown(pDoc, [&](ge::Node* topNode) {
		ge::Node* deep = HitTestDeepNode(topNode, screenPt, view, f);
		if (!deep) return false;

		// 1) deep 노드가 "버튼" 컴포넌트의 자식 텍스트이거나 버튼 내부 요소인 경우, 버튼 컴포넌트 전체를 선택
		ge::Node* checkBtn = deep;
		while (checkBtn)
		{
			if (ge::IsAtomicComponent(checkBtn))
			{
				hitTarget = checkBtn;
				return true;
			}
			checkBtn = checkBtn->GetParent();
		}

		// 2) deep 노드에서 부모 방향으로 올라가면서, FrameNode의 직속 자식이면 그 자식 단위로 선택
		ge::Node* curr = deep;
		while (curr && curr->GetParent())
		{
			ge::GroupNode* parent = curr->GetParent();
			if (dynamic_cast<ge::FrameNode*>(parent) != nullptr)
			{
				// FrameNode의 직속 자식이면 이 curr 노드를 개별 선택 대상으로 확정!
				if (!curr->IsEffectivelyLocked())
					hitTarget = curr;
				return hitTarget != nullptr;
			}
			if (parent->Kind() == ge::NodeKind::Layer || parent == isoGroup)   // 격리 중에는 격리 그룹의 직속 자식 단위
			{
				if (!curr->IsEffectivelyLocked())
					hitTarget = curr;
				return hitTarget != nullptr;
			}
			curr = parent;
		}

		if (!topNode->IsEffectivelyLocked())
			hitTarget = topNode;
		return hitTarget != nullptr;
	});

	return hitTarget;
}

// ============================================================================
// 그라디언트 기즈모: 선택된 도형의 그라디언트 핸들 (좌표는 도형 지오메트리 경계 기준 비율)
// ============================================================================
namespace {

struct GradientGizmo
{
	ge::Rect2     bounds{};
	ge::Matrix2D  worldXf;
	bool          radial = false;
	D2D1_POINT_2F start{}, end{}, radius{};   // 화면 좌표. 방사형: start=중심, end=초점, radius=중심에서 +X 방향 반지름 지점

	// 박스 비율 좌표 -> 화면 좌표
	D2D1_POINT_2F ToScreen(float rx, float ry, const ViewportTransform& view) const
	{
		const D2D1_POINT_2F local = D2D1::Point2F(bounds.left + rx * (bounds.right - bounds.left),
		                                          bounds.top + ry * (bounds.bottom - bounds.top));
		return CoordinateSpace::LocalToScreen(local, worldXf, view);
	}
};

// 도형 또는 프레임이 그라디언트 채우기를 지원하는지 확인
bool SupportsGradientFill(const ge::Node* n)
{
	return n && (n->Kind() == ge::NodeKind::Path || dynamic_cast<const ge::FrameNode*>(n) != nullptr);
}

// 단일 도형/프레임의 그라디언트 기즈모 계산. 그라디언트 채우기가 아니거나 박스를 만들 수 없으면 false
bool ComputeGradientGizmo(ge::Node* n, const ViewportTransform& view, ID2D1Factory* /*f*/, GradientGizmo& out)
{
	if (!SupportsGradientFill(n) || n->GetStyle().fill.type != ge::PaintType::Gradient) return false;
	const ge::Gradient& g = n->GetStyle().fill.gradient;
	if (!g.IsRenderable()) return false;

	if (!n->GetLocalBounds(out.bounds)) return false;
	if (out.bounds.right - out.bounds.left < 1e-4f || out.bounds.bottom - out.bounds.top < 1e-4f) return false;

	out.worldXf = n->GetWorldTransform();
	out.radial = (g.kind == ge::Gradient::Kind::Radial);
	out.start = out.ToScreen(g.start.x, g.start.y, view);
	out.end = out.ToScreen(g.end.x, g.end.y, view);
	out.radius = out.ToScreen(g.start.x + g.radius, g.start.y, view);
	return true;
}

float DistSq(D2D1_POINT_2F a, D2D1_POINT_2F b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

} // namespace

// ============================================================================
// 그라디언트 공용 헬퍼 (선택 도구의 기즈모 편집과 그라디언트 도구가 함께 사용)
// ============================================================================
namespace {

// 기즈모 핸들 종류 (SelectTool::GradHandle 과 같은 값)
enum GradPartId { kGradNone = 0, kGradStart = 1, kGradEnd = 2, kGradRadius = 3 };

// 화면 좌표 -> 도형/프레임 지오메트리 박스 비율 좌표. 박스를 만들 수 없거나 변환이 불가능하면 false
bool ScreenToGradRatio(ge::Node* ps, const ge::Rect2& b, D2D1_POINT_2F screenPt, const ViewportTransform& view, float& rx, float& ry)
{
	const float w = b.right - b.left;
	const float h = b.bottom - b.top;
	if (!ps || w < 1e-4f || h < 1e-4f) return false;
	auto local = CoordinateSpace::ScreenToLocal(screenPt, ps->GetWorldTransform(), view);
	if (!local) return false;
	rx = (local->x - b.left) / w;
	ry = (local->y - b.top) / h;
	return true;
}

// 기즈모 핸들 드래그를 그라디언트 필드에 반영
void ApplyGradPartDrag(ge::Node* ps, const ge::Rect2& b, int part, D2D1_POINT_2F screenPt, const ViewportTransform& view)
{
	float rx = 0.0f, ry = 0.0f;
	if (!ScreenToGradRatio(ps, b, screenPt, view, rx, ry)) return;

	ge::Gradient& g = ps->GetStyleRef().fill.gradient;   // GetStyleRef: 지오메트리/브러시 캐시 무효화 + 변경 카운터
	switch (part)
	{
	case kGradStart:
	{
		const bool focusFollows = (g.kind == ge::Gradient::Kind::Radial) &&
			std::abs(g.end.x - g.start.x) < 1e-4f && std::abs(g.end.y - g.start.y) < 1e-4f;   // 초점이 중심과 같으면 함께 이동
		g.start = { rx, ry };
		if (focusFollows) g.end = g.start;
		break;
	}
	case kGradEnd:
		g.end = { rx, ry };
		break;
	case kGradRadius:
		g.radius = (std::max)(0.01f, std::abs(rx - g.start.x));
		break;
	default:
		break;
	}
}

// 그라디언트가 없으면 현재 채움 색 -> 같은 색의 투명으로 새로 만들고, 있으면 그대로 둔다
void EnsureGradient(ge::Node* ps, ge::Gradient::Kind kind)
{
	const ge::Style& cur = ps->GetStyle();
	if (cur.fill.type == ge::PaintType::Gradient && cur.fill.gradient.stops.size() >= 2) return;

	ge::Style& st = ps->GetStyleRef();
	ge::Color from = st.fill.color;
	if (st.fill.type != ge::PaintType::Solid) from.a = 1.0f;   // 채움이 없던 도형은 불투명 색에서 시작
	ge::Color to = from;
	to.a = 0.0f;
	st.fill.gradient = ge::Gradient::Make(kind, from, to);
	st.fill.type = ge::PaintType::Gradient;
}

// 도형/프레임의 기하 경계 (그라디언트 박스) - 채움 종류와 무관하게 계산
bool ComputePathBounds(ge::Node* ps, ID2D1Factory* /*f*/, ge::Rect2& out)
{
	if (!SupportsGradientFill(ps)) return false;
	return ps->GetLocalBounds(out) && (out.right - out.left) > 1e-4f && (out.bottom - out.top) > 1e-4f;
}

// 축(선형: 시작->끝, 방사형: 중심->반지름 지점) 위 한 점의 매개변수 t (0~1 로 제한)와 축까지의 거리 제곱
float ProjectOnAxis(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F p, float& distSqToSegment)
{
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float len2 = dx * dx + dy * dy;
	float t = len2 > 1e-6f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
	t = (std::max)(0.0f, (std::min)(1.0f, t));
	const D2D1_POINT_2F q = D2D1::Point2F(a.x + dx * t, a.y + dy * t);
	distSqToSegment = DistSq(p, q);
	return t;
}

// 마우스 아래의 최상단 그라디언트 대상 노드(PathNode 또는 FrameNode, 그룹/프레임 안쪽 포함, 숨김/잠금 제외)
ge::Node* HitTestGradNodeInNode(ge::Node* n, D2D1_POINT_2F screenPt, const ViewportTransform& view, ID2D1Factory* f)
{
	if (!n || !n->IsVisible()) return nullptr;
	if (n->IsLocked() && !ge::AllowsChildSelection(n)) return nullptr;
	if (auto* group = dynamic_cast<ge::GroupNode*>(n))
	{
		const auto& children = group->GetChildren();
		for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i)
			if (ge::Node* hit = HitTestGradNodeInNode(children[i].get(), screenPt, view, f)) return hit;
		if (auto* fr = dynamic_cast<ge::FrameNode*>(n))
		{
			if (!fr->IsLocked() && HitTestSingleNode(fr, screenPt, view, f)) return fr;
		}
		return nullptr;
	}
	if (n->IsLocked()) return nullptr;
	ge::PathNode* ps = dynamic_cast<ge::PathNode*>(n);
	return (ps && ps->HitTestScreenSpace(screenPt, 8.0f, view, f).part != HitResult::Part::None) ? ps : nullptr;
}

ge::Node* HitTestGradNodeDeep(CM2DrawDoc* pDoc, D2D1_POINT_2F screenPt, const ViewportTransform& view, ID2D1Factory* f)
{
	ge::Node* hit = nullptr;
	ForEachSelectableTopDown(pDoc, [&](ge::Node* n) {
		hit = HitTestGradNodeInNode(n, screenPt, view, f);
		return hit != nullptr;
	});
	return hit;
}

// 그라디언트 기즈모 그리기: 축/반지름 타원, 컬러 스톱 점, 핸들(시작 ■ / 끝·초점 ● / 반지름 ◆)
// activePart: 강조할 핸들(GradPartId), activeStop: 강조할 스톱 인덱스(-1 = 없음)
void DrawGradientGizmo(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const ViewportTransform& view, ID2D1Factory* f,
                       ge::Node* ps, int activePart, int activeStop)
{
	GradientGizmo gz;
	if (!ComputeGradientGizmo(ps, view, f, gz)) return;
	const ge::Gradient& grad = ps->GetStyle().fill.gradient;

	auto line = [&](D2D1_POINT_2F a, D2D1_POINT_2F b) {
		brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));
		rt->DrawLine(a, b, brush, 3.0f);
		brush->SetColor(D2D1::ColorF(0.10f, 0.30f, 0.70f, 1.0f));
		rt->DrawLine(a, b, brush, 1.3f);
	};
	auto dot = [&](D2D1_POINT_2F p, float r, D2D1_COLOR_F fill) {
		brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
		rt->FillEllipse(D2D1::Ellipse(p, r + 1.5f, r + 1.5f), brush);
		brush->SetColor(fill);
		rt->FillEllipse(D2D1::Ellipse(p, r, r), brush);
		brush->SetColor(D2D1::ColorF(0.10f, 0.30f, 0.70f, 1.0f));
		rt->DrawEllipse(D2D1::Ellipse(p, r + 1.5f, r + 1.5f), brush, 1.0f);
	};

	if (!gz.radial)
	{
		line(gz.start, gz.end);
	}
	else
	{
		// 반지름 타원 (박스 너비/높이에 비례) 을 64각형으로 근사해 화면 좌표로 그림
		const int kSeg = 64;
		D2D1_POINT_2F prev{};
		for (int i = 0; i <= kSeg; ++i)
		{
			const float t = static_cast<float>(i) / kSeg * 6.28318530718f;
			D2D1_POINT_2F p = gz.ToScreen(grad.start.x + grad.radius * std::cos(t), grad.start.y + grad.radius * std::sin(t), view);
			if (i > 0) line(prev, p);
			prev = p;
		}
		line(gz.start, gz.radius);   // 중심 -> 반지름 핸들 (스톱 위치 표시용 축)
	}

	// 컬러 스톱: 축 위에 스톱 색 점 (선형: 시작->끝, 방사형: 중심->반지름 지점). 조작 중인 스톱은 크게
	const D2D1_POINT_2F axisA = gz.start;
	const D2D1_POINT_2F axisB = gz.radial ? gz.radius : gz.end;
	for (int i = 0; i < static_cast<int>(grad.stops.size()); ++i)
	{
		const ge::GradientStop& st = grad.stops[i];
		const D2D1_POINT_2F p = D2D1::Point2F(axisA.x + (axisB.x - axisA.x) * st.offset, axisA.y + (axisB.y - axisA.y) * st.offset);
		dot(p, i == activeStop ? 5.5f : 4.0f, D2D1::ColorF(st.color.r, st.color.g, st.color.b, 1.0f));
	}

	auto square = [&](D2D1_POINT_2F p, bool active) {
		D2D1_RECT_F r = D2D1::RectF(p.x - 5.0f, p.y - 5.0f, p.x + 5.0f, p.y + 5.0f);
		brush->SetColor(active ? D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
		rt->FillRectangle(r, brush);
		brush->SetColor(D2D1::ColorF(0.10f, 0.30f, 0.70f, 1.0f));
		rt->DrawRectangle(r, brush, 1.5f);
	};
	square(gz.start, activePart == kGradStart);
	dot(gz.end, 5.5f, activePart == kGradEnd ? D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	if (gz.radial)
		dot(gz.radius, 5.5f, activePart == kGradRadius ? D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f) : D2D1::ColorF(1.0f, 0.85f, 0.2f, 1.0f));
}

} // namespace

// 노드가 속한 레이어를 활성 레이어로 (새 도형 생성/복제 대상 레이어와 일치시킴)
static void ActivateLayerOf(CM2DrawDoc* pDoc, ge::Node* node)
{
	if (!pDoc || !node) return;
	auto& doc = pDoc->GetDoc();
	// 그룹 안쪽 노드면 최상위 조상(레이어 직속 자식)의 부모 레이어를 찾음
	ge::GroupNode* parent = node->GetParent();
	while (parent && parent->GetParent())
		parent = parent->GetParent();

	const auto& layers = doc.GetLayers();
	for (size_t i = 0; i < layers.size(); ++i)
	{
		if (layers[i].get() == parent)
		{
			doc.SetActiveLayer(static_cast<int>(i));
			return;
		}
	}
}

static bool IsEditPart(const HitResult& hr)
{
	return hr.part == HitResult::Part::Anchor || hr.part == HitResult::Part::HandleIn || hr.part == HitResult::Part::HandleOut;
}

bool SelectTool::IsOpenPath(ge::Node* shape)
{
	ge::PathNode* ps = dynamic_cast<ge::PathNode*>(shape);
	if (!ps || ps->GetPaths().empty()) return false;
	for (const auto& sp : ps->GetPaths())
	{
		if (sp.closed) return false;
	}
	return true;
}

static bool IsCornerRotateZone(D2D1_POINT_2F screenPt, const D2D1_POINT_2F handles[9])
{
	const D2D1_POINT_2F center = D2D1::Point2F(
		(handles[0].x + handles[4].x) * 0.5f,
		(handles[0].y + handles[4].y) * 0.5f);
	const int cornerIndices[4] = { 0, 2, 4, 6 };
	const float minSq = 8.0f * 8.0f;
	const float maxSq = 18.0f * 18.0f;
	for (int idx : cornerIndices)
	{
		const D2D1_POINT_2F c = handles[idx];
		const float dx = screenPt.x - c.x;
		const float dy = screenPt.y - c.y;
		const float dSq = dx * dx + dy * dy;
		if (dSq > minSq && dSq <= maxSq)
		{
			const float vx = c.x - center.x;
			const float vy = c.y - center.y;
			if (dx * vx + dy * vy > 0.0f)
				return true;
		}
	}
	return false;
}

HCURSOR SelectTool::GetCursor() const
{
	if (m_mode == Mode::Moving || (m_mode == Mode::None && m_hoverOnShape))
	{
		return ::LoadCursor(nullptr, IDC_SIZEALL);
	}

	if (m_hoverIsNode || m_mode == Mode::EditNode || m_mode == Mode::GradientHandle)
	{
		return ::LoadCursor(nullptr, IDC_CROSS);
	}

	if (m_mode == Mode::TableResize)
		return ::LoadCursor(nullptr, m_tblCol ? IDC_SIZEWE : IDC_SIZENS);
	if (m_mode == Mode::Rotating || m_mode == Mode::MultiRotate || m_hoverCornerRotate)
		return ::LoadCursor(nullptr, IDC_CROSS);
	if (m_mode == Mode::Skewing || m_mode == Mode::MultiSkew)
	{
		const bool horizEdge = (m_handle == Handle::T || m_handle == Handle::B);
		return ::LoadCursor(nullptr, horizEdge ? IDC_SIZEWE : IDC_SIZENS);
	}
	if (m_mode == Mode::None && m_hoverTbl != 0)
		return ::LoadCursor(nullptr, m_hoverTbl == 1 ? IDC_SIZEWE : IDC_SIZENS);

	Handle activeH = (m_mode == Mode::Scaling || m_mode == Mode::MultiScale) ? m_handle : m_hoverHandle;
	if (activeH == Handle::Rotate)
		return ::LoadCursor(nullptr, IDC_CROSS);
	if (activeH == Handle::None)
		return ::LoadCursor(nullptr, IDC_ARROW);

	if (m_ctrlDown && (activeH == Handle::T || activeH == Handle::B || activeH == Handle::L || activeH == Handle::R))
	{
		const bool horizEdge = (activeH == Handle::T || activeH == Handle::B);
		return ::LoadCursor(nullptr, horizEdge ? IDC_SIZEWE : IDC_SIZENS);
	}

	// 도형 회전각(m_hoverAngleDeg)을 반영한 8방향 동적 크기 조절 커서 매핑 (6.14.2)
	float baseDeg = 0.0f;
	switch (activeH)
	{
	case Handle::L:
	case Handle::R:  baseDeg = 0.0f; break;
	case Handle::TL:
	case Handle::BR: baseDeg = 45.0f; break;
	case Handle::T:
	case Handle::B:  baseDeg = 90.0f; break;
	case Handle::TR:
	case Handle::BL: baseDeg = 135.0f; break;
	default: return ::LoadCursor(nullptr, IDC_ARROW);
	}
	float totalDeg = std::fmod(baseDeg + m_hoverAngleDeg, 180.0f);
	if (totalDeg < 0.0f) totalDeg += 180.0f;
	const int sector = static_cast<int>(std::floor((totalDeg + 22.5f) / 45.0f)) % 4;
	switch (sector)
	{
	case 0: return ::LoadCursor(nullptr, IDC_SIZEWE);
	case 1: return ::LoadCursor(nullptr, IDC_SIZENWSE);
	case 2: return ::LoadCursor(nullptr, IDC_SIZENS);
	default: return ::LoadCursor(nullptr, IDC_SIZENESW);
	}
}

SelectTool::Handle SelectTool::HitHandle(D2D1_POINT_2F screenPt, const D2D1_POINT_2F handles[9], float tolPx) const
{
	float tolSq = tolPx * tolPx;
	for (int i = 0; i < 9; ++i)
	{
		float dx = screenPt.x - handles[i].x;
		float dy = screenPt.y - handles[i].y;
		if (dx * dx + dy * dy <= tolSq)
		{
			return static_cast<Handle>(i + 1);
		}
	}
	return Handle::None;
}

bool SelectTool::GetHandleScreenPositions(ge::Node* shape, const ViewportTransform& view, ID2D1Factory* f,
                                          D2D1_POINT_2F outHandles[9], D2D1_RECT_F* outLocalBounds) const
{
	if (!shape || !f) return false;

	D2D1_RECT_F b;
	if (!shape->GetLocalBounds(f, b)) return false;

	if (outLocalBounds)
	{
		*outLocalBounds = b;
	}

	D2D1_POINT_2F localHandles[9];
	ge::Transformer::GetLocalHandlePoints(b, localHandles);

	for (int i = 0; i < 9; ++i)
	{
		outHandles[i] = CoordinateSpace::LocalToScreen(localHandles[i], shape->GetWorldTransform(), view);
	}
	return true;
}

bool SelectTool::MultiHandlePositions(const std::vector<ge::Node*>& sel, const ViewportTransform& view, D2D1_POINT_2F out[9], ge::Rect2& rect) const
{
	if (sel.size() < 2 || !ge::UnionWorldBounds(sel, rect)) return false;
	if (rect.right - rect.left < 1e-3f && rect.bottom - rect.top < 1e-3f) return false;
	const float cx = (rect.left + rect.right) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f;
	out[0] = view.WorldToScreen(D2D1::Point2F(rect.left, rect.top));
	out[1] = view.WorldToScreen(D2D1::Point2F(cx, rect.top));
	out[2] = view.WorldToScreen(D2D1::Point2F(rect.right, rect.top));
	out[3] = view.WorldToScreen(D2D1::Point2F(rect.right, cy));
	out[4] = view.WorldToScreen(D2D1::Point2F(rect.right, rect.bottom));
	out[5] = view.WorldToScreen(D2D1::Point2F(cx, rect.bottom));
	out[6] = view.WorldToScreen(D2D1::Point2F(rect.left, rect.bottom));
	out[7] = view.WorldToScreen(D2D1::Point2F(rect.left, cy));
	out[8] = D2D1::Point2F(out[1].x, out[1].y - 26.0f);   // 회전 핸들: 위쪽 가운데에서 26px 위
	return true;
}

void SelectTool::ApplyMultiTransform(const ToolEvent& e)
{
	if (m_multiNodes.empty()) return;
	const ge::Vec2 cur{ e.worldPt.x, e.worldPt.y };
	const ge::Vec2 start{ m_dragStartWorld.x, m_dragStartWorld.y };
	const ge::Rect2& rc = m_multiRect;
	const float cx = (rc.left + rc.right) * 0.5f, cy = (rc.top + rc.bottom) * 0.5f;
	const Handle h = m_multiHandle;
	const bool left = (h == Handle::TL || h == Handle::L || h == Handle::BL);
	const bool right = (h == Handle::TR || h == Handle::R || h == Handle::BR);
	const bool top = (h == Handle::TL || h == Handle::T || h == Handle::TR);
	const bool bottom = (h == Handle::BL || h == Handle::B || h == Handle::BR);
	m_hoverHandle = h;   // 커서 표시용

	ge::Matrix2D T = ge::Matrix2D::Identity();
	if (m_mode == Mode::MultiRotate)
	{
		const float a0 = std::atan2(start.y - cy, start.x - cx), a1 = std::atan2(cur.y - cy, cur.x - cx);
		float deg = (a1 - a0) * 180.0f / 3.14159265f;
		if (e.shiftDown) deg = std::round(deg / 15.0f) * 15.0f;   // Shift: 15도 단위
		T = ge::MakeWorldRotation(deg, ge::Vec2{ cx, cy });
	}
	else if (m_mode == Mode::MultiScale)
	{
		const float hx = left ? rc.left : right ? rc.right : cx;
		const float hy = top ? rc.top : bottom ? rc.bottom : cy;
		float ax = left ? rc.right : right ? rc.left : cx;
		float ay = top ? rc.bottom : bottom ? rc.top : cy;
		if (e.altDown) { ax = cx; ay = cy; }   // Alt: 가운데 기준
		float sx = 1.0f, sy = 1.0f;
		if ((left || right) && std::fabs(hx - ax) > 1e-3f) sx = (cur.x - ax) / (hx - ax);
		if ((top || bottom) && std::fabs(hy - ay) > 1e-3f) sy = (cur.y - ay) / (hy - ay);
		if (e.shiftDown && (left || right) && (top || bottom))   // Shift + 모서리: 비율 유지
		{
			const float s = (std::fabs(sx) > std::fabs(sy)) ? sx : sy;
			sx = sy = s;
		}
		auto clampS = [](float s) { return std::fabs(s) < 0.01f ? (s < 0.0f ? -0.01f : 0.01f) : s; };
		T = ge::MakeWorldScale(clampS(sx), clampS(sy), ge::Vec2{ ax, ay });
	}
	else if (m_mode == Mode::MultiSkew)
	{
		if (top || bottom)
		{
			const float ay = top ? rc.bottom : rc.top, hy = top ? rc.top : rc.bottom;
			const float d = hy - ay;
			if (std::fabs(d) > 1e-3f) T = ge::MakeWorldSkewX((cur.x - start.x) / d, ge::Vec2{ cx, ay });
		}
		else
		{
			const float ax = left ? rc.right : rc.left, hx = left ? rc.left : rc.right;
			const float d = hx - ax;
			if (std::fabs(d) > 1e-3f) T = ge::MakeWorldSkewY((cur.y - start.y) / d, ge::Vec2{ ax, cy });
		}
	}
	ge::ApplyWorldTransform(m_multiNodes, m_multiInitial, T);

	static DWORD s_lastTick = 0;
	const DWORD tick = ::GetTickCount();
	if (tick - s_lastTick >= 30 && e.pDoc)
	{
		s_lastTick = tick;
		UpdateShapePropertiesLight(m_multiNodes.front(), e.pDoc);
	}
}

bool SelectTool::HitTableLine(ge::TableNode* tb, D2D1_POINT_2F sp, const ViewportTransform& view, bool& isCol, int& idx)
{
	if (!tb) return false;
	const ge::Matrix2D w = tb->GetWorldTransform();
	ge::Rect2 b;
	if (!tb->GetLocalBounds(b)) return false;
	float best = 5.0f;
	bool found = false;
	auto distToSeg = [&](ge::Vec2 a, ge::Vec2 c) {
		const ge::Vec2 A = view.WorldToScreen(w.TransformPoint(a)), C = view.WorldToScreen(w.TransformPoint(c));
		const float vx = C.x - A.x, vy = C.y - A.y;
		const float len2 = vx * vx + vy * vy;
		float t = len2 > 0.0f ? ((sp.x - A.x) * vx + (sp.y - A.y) * vy) / len2 : 0.0f;
		t = (std::max)(0.0f, (std::min)(1.0f, t));
		const float dx = sp.x - (A.x + vx * t), dy = sp.y - (A.y + vy * t);
		return std::sqrt(dx * dx + dy * dy);
	};
	float x = 0.0f;
	for (int i = 0; i + 1 < tb->Cols(); ++i)
	{
		x += tb->ColWidths()[i];
		const float d = distToSeg(ge::Vec2{ x, b.top }, ge::Vec2{ x, b.bottom });
		if (d <= best) { best = d; isCol = true; idx = i; found = true; }
	}
	float y = 0.0f;
	for (int i = 0; i + 1 < tb->Rows(); ++i)
	{
		y += tb->RowHeights()[i];
		const float d = distToSeg(ge::Vec2{ b.left, y }, ge::Vec2{ b.right, y });
		if (d <= best) { best = d; isCol = false; idx = i; found = true; }
	}
	return found;
}

float SelectTool::TableLocalAxis(ge::TableNode* tb, D2D1_POINT_2F worldPt, bool isCol)
{
	ge::Matrix2D inv = tb->GetWorldTransform();
	if (!inv.Invert()) return 0.0f;
	const ge::Vec2 l = inv.TransformPoint(ge::Vec2{ worldPt.x, worldPt.y });
	return isCol ? l.x : l.y;
}

void SelectTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	// -1. 선택된 테이블의 안쪽 행/열 경계를 잡으면 크기 조절 (최우선)
	if (e.pDoc->GetDoc().GetSelection().size() == 1)
	{
		if (auto* tb = dynamic_cast<ge::TableNode*>(e.pDoc->GetPrimarySelection()))
		{
			bool isCol = false; int idx = -1;
			if (IsInteractive(tb) && HitTableLine(tb, D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y)), e.view, isCol, idx))
			{
				m_mode = Mode::TableResize;
				m_handle = Handle::None;
				m_tbl = tb;
				m_tblCol = isCol;
				m_tblIdx = idx;
				m_tblStartSize = isCol ? tb->ColWidths()[idx] : tb->RowHeights()[idx];
				m_tblStartLocal = TableLocalAxis(tb, e.worldPt, isCol);
				LogAppEvent(isCol ? _T("[테이블] 열 경계 드래그 시작") : _T("[테이블] 행 경계 드래그 시작"));
				return;
			}
		}
	}

	auto& doc = e.pDoc->GetDoc();
	const std::vector<ge::Node*> sel = doc.GetSelection();
	m_viewZoom = e.view.zoom;
	m_dragStartWorld = e.worldPt;
	m_dragCurrentWorld = e.worldPt;
	D2D1_POINT_2F screenPt = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));

	// 0. 단일 선택된 그라디언트 도형의 기즈모 핸들(시작/끝 또는 중심/초점/반지름) 최우선
	if (sel.size() == 1 && IsInteractive(sel[0]))
	{
		GradientGizmo gz;
		if (ComputeGradientGizmo(sel[0], e.view, e.pD2DFactory, gz))
		{
			const float tolSq = 8.0f * 8.0f;
			GradHandle hit = GradHandle::None;
			float best = tolSq;
			auto consider = [&](GradHandle h, D2D1_POINT_2F p) {
				const float d = DistSq(screenPt, p);
				if (d <= best) { best = d; hit = h; }
			};
			// 방사형에서 중심과 초점이 겹쳐 있으면 기본은 중심을 잡고, Shift 를 누르고 잡으면 초점을 움직임
			const bool coincident = gz.radial && DistSq(gz.start, gz.end) < 1.0f;
			if (coincident && e.shiftDown)
			{
				consider(GradHandle::End, gz.end);
			}
			else
			{
				consider(GradHandle::Start, gz.start);
				if (!coincident) consider(GradHandle::End, gz.end);
			}
			if (gz.radial) consider(GradHandle::Radius, gz.radius);
			if (hit != GradHandle::None)
			{
				m_mode = Mode::GradientHandle;
				m_gradShape = sel[0];
				m_gradHandle = hit;
				m_gradBounds = gz.bounds;
				m_handle = Handle::None;
				LogAppEvent(_T("[선택 도구] 그라디언트 핸들 드래그 시작"));
				return;
			}
		}
	}

	// 0-multi. 2개 이상 선택: 결합 경계 상자 기즈모 (스케일 / 회전 / Ctrl+가장자리 = 전단) (5.2)
	if (sel.size() >= 2)
	{
		bool allOk = true;
		for (ge::Node* n : sel) if (!IsInteractive(n)) { allOk = false; break; }
		D2D1_POINT_2F hp[9];
		ge::Rect2 rc;
		if (allOk && MultiHandlePositions(sel, e.view, hp, rc))
		{
			const Handle hit = HitHandle(screenPt, hp, 8.0f);
			if (hit != Handle::None)
			{
				m_multiNodes = sel;
				m_multiRect = rc;
				m_multiHandle = hit;
				m_handle = hit;
				m_multiInitial.clear();
				for (ge::Node* n : sel) m_multiInitial.push_back(n->GetTransform());
				const bool edge = (hit == Handle::T || hit == Handle::R || hit == Handle::B || hit == Handle::L);
				m_mode = (hit == Handle::Rotate) ? Mode::MultiRotate : ((e.ctrlDown && edge) ? Mode::MultiSkew : Mode::MultiScale);
				LogAppEvent(m_mode == Mode::MultiRotate ? _T("[선택 도구] 다중 선택 회전 시작") : m_mode == Mode::MultiSkew ? _T("[선택 도구] 다중 선택 전단(Skew) 시작") : _T("[선택 도구] 다중 선택 크기 조절 시작"));
				return;
			}
		}
	}
	// 다중 선택일 때는 개별 도형의 앵커/크기 핸들을 쓰지 않는다 (결합 기즈모만)
	const std::vector<ge::Node*> perNode = (sel.size() >= 2) ? std::vector<ge::Node*>() : sel;

	// 1. 이미 선택된 패스 도형의 노드(앵커, 핸들 In/Out) 클릭 여부 최우선 검사
	for (ge::Node* n : perNode)
	{
		ge::PathNode* ps = dynamic_cast<ge::PathNode*>(n);
		if (!ps || !IsInteractive(ps)) continue;
		HitResult hr = ps->HitTestScreenSpace(screenPt, 8.0f, e.view, e.pD2DFactory);
		if (IsEditPart(hr))
		{
			m_mode = Mode::EditNode;
			m_editShape = ps;
			m_editHit = hr;
			m_handle = Handle::None;
			m_initialXf = ps->GetTransform();

			CString partName = (hr.part == HitResult::Part::Anchor) ? _T("정점(Anchor)") :
			                   (hr.part == HitResult::Part::HandleIn) ? _T("핸들In") : _T("핸들Out");
			CString str;
			str.Format(_T("[선택 도구] %s 조절점 드래그 시작 (%s, 노드 #%d)"), (LPCTSTR)partName, ps->GetName().c_str(), hr.nodeIdx + 1);
			LogAppEvent(str);
			return;
		}
	}

	// 2. 이미 선택된 도형 중 닫힌 도형(열린 곡선/직선 제외)의 바운딩 박스 핸들 클릭 여부 검사
	for (ge::Node* n : perNode)
	{
		if (!IsInteractive(n) || IsOpenPath(n)) continue;
		D2D1_POINT_2F handles[9];
		D2D1_RECT_F localBounds;
		if (!GetHandleScreenPositions(n, e.view, e.pD2DFactory, handles, &localBounds)) continue;

		Handle hitH = HitHandle(screenPt, handles, 8.0f);
		const bool cornerRot = (hitH == Handle::None) && IsCornerRotateZone(screenPt, handles);
		if (hitH != Handle::None || cornerRot)
		{
			m_handle = cornerRot ? Handle::Rotate : hitH;
			m_xfTarget = n;
			m_initialXf = n->GetTransform();
			m_initialLocalBounds = localBounds;

			const bool isEdge = (hitH == Handle::T || hitH == Handle::R || hitH == Handle::B || hitH == Handle::L);
			if (cornerRot || hitH == Handle::Rotate)
			{
				m_mode = Mode::Rotating;
				LogAppEvent(_T("[선택 도구] 회전 핸들 드래그 시작"));
			}
			else if (e.ctrlDown && isEdge)
			{
				m_mode = Mode::Skewing;
				LogAppEvent(_T("[선택 도구] 변 핸들 전단(Skew) 드래그 시작"));
			}
			else
			{
				m_mode = Mode::Scaling;
				LogAppEvent(_T("[선택 도구] 크기 조절 핸들 드래그 시작"));
			}
			return;
		}
	}

	// 3. 새로운 도형 선택 히트 테스트 (모든 레이어, 최상단 도형부터)
	ge::Node* clicked = HitTestTopShape(e.pDoc, screenPt, e.view, e.pD2DFactory);
	if (clicked)
	{
		std::vector<ge::Node*> newSel = sel;
		auto it = std::find(newSel.begin(), newSel.end(), clicked);
		if (e.ctrlDown)
		{
			if (it != newSel.end()) newSel.erase(it);
			else newSel.push_back(clicked);
		}
		else if (it == newSel.end())
		{
			newSel = { clicked };
		}
		doc.SetSelection(newSel);
		ActivateLayerOf(e.pDoc, clicked);

		if (auto* tb = dynamic_cast<ge::TableNode*>(clicked))
		{
			ge::Matrix2D inv = tb->GetWorldTransform();
			int cr = -1, cc = -1;
			if (inv.Invert() && tb->CellAt(inv.TransformPoint(ge::Vec2{ e.worldPt.x, e.worldPt.y }), cr, cc)) tb->SetActiveCell(cr, cc);
		}

		m_mode = Mode::Moving;
		m_handle = Handle::None;
		m_initialXf = clicked->GetTransform();
		clicked->GetLocalBounds(e.pD2DFactory, m_initialLocalBounds);

		// 다중 선택된 모든 도형의 시작 Transform 캐싱
		m_initialXfs.clear();
		for (ge::Node* n : newSel)
			m_initialXfs[n] = n->GetTransform();

		CString str;
		str.Format(_T("[선택 도구] '%s' 선택됨 (현재 선택 수: %d개) | 이동 모드 시작"), clicked->GetName().c_str(), static_cast<int>(newSel.size()));
		LogAppEvent(str);

		UpdateShapePropertiesSelection(e.pDoc->GetPrimarySelection(), e.pDoc);
	}
	else
	{
		if (!e.ctrlDown)
		{
			doc.ClearSelection();
			UpdateShapePropertiesSelection(nullptr, e.pDoc);
		}
		m_mode = Mode::Marquee; // 빈 공간 드래그 시 마키(고무줄) 선택 모드 진입
		m_handle = Handle::None;
		LogAppEvent(_T("[선택 도구] 마키(Marquee) 영역 선택 시작"));
	}
}

void SelectTool::SelectNode(ge::Node* node, CM2DrawDoc* pDoc)
{
	if (!pDoc) return;
	if (node && !node->IsEffectivelyLocked() && node->IsEffectivelyVisible())
	{
		pDoc->GetDoc().SetSelection({ node });
		ActivateLayerOf(pDoc, node);
	}
	else if (!node)
	{
		pDoc->GetDoc().ClearSelection();
	}
	else
	{
		// 잠기거나 숨겨진 노드는 선택하지 않음
		return;
	}
	UpdateShapePropertiesSelection(node, pDoc);
}

void SelectTool::ClearSelection(CM2DrawDoc* pDoc)
{
	if (pDoc) pDoc->GetDoc().ClearSelection();
}

void SelectTool::OnMove(const ToolEvent& e)
{
	if (!e.pDoc) return;

	const std::vector<ge::Node*> sel = e.pDoc->GetDoc().GetSelection();
	m_viewZoom = e.view.zoom;
	m_ctrlDown = e.ctrlDown;
	m_dragCurrentWorld = e.worldPt;
	D2D1_POINT_2F screenPt = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));

	if (m_mode == Mode::None)
	{
		m_hoverIsNode = false;
		m_hoverHandle = Handle::None;
		m_hoverCornerRotate = false;
		m_hoverAngleDeg = 0.0f;
		m_hoverOnShape = false;
		m_hoverTbl = 0;
		if (sel.size() == 1)
		{
			bool isCol = false; int idx = -1;
			auto* tb = dynamic_cast<ge::TableNode*>(sel[0]);
			if (tb && IsInteractive(tb) && HitTableLine(tb, screenPt, e.view, isCol, idx))
			{
				m_hoverTbl = isCol ? 1 : 2;
				return;
			}
		}

		// 0) 그라디언트 기즈모 핸들 호버 -> 십자 커서
		if (sel.size() == 1 && IsInteractive(sel[0]))
		{
			GradientGizmo gz;
			if (ComputeGradientGizmo(sel[0], e.view, e.pD2DFactory, gz))
			{
				const float tolSq = 8.0f * 8.0f;
				if (DistSq(screenPt, gz.start) <= tolSq || DistSq(screenPt, gz.end) <= tolSq ||
				    (gz.radial && DistSq(screenPt, gz.radius) <= tolSq))
				{
					m_hoverIsNode = true;
					return;
				}
			}
		}

		// 0) 다중 선택: 결합 기즈모 핸들 호버 (5.2)
		if (sel.size() >= 2)
		{
			D2D1_POINT_2F hp[9];
			ge::Rect2 rc;
			if (MultiHandlePositions(sel, e.view, hp, rc))
			{
				const Handle hh = HitHandle(screenPt, hp, 8.0f);
				if (hh != Handle::None) { m_hoverHandle = hh; return; }
			}
		}
		const std::vector<ge::Node*> perNodeHover = (sel.size() >= 2) ? std::vector<ge::Node*>() : sel;

		// 1) 선택된 도형의 노드(앵커/핸들) 및 바운딩 박스 핸들 호버 검사
		for (ge::Node* n : perNodeHover)
		{
			if (!IsInteractive(n)) continue;
			ge::PathNode* ps = dynamic_cast<ge::PathNode*>(n);
			if (ps && IsEditPart(ps->HitTestScreenSpace(screenPt, 8.0f, e.view, e.pD2DFactory)))
			{
				m_hoverIsNode = true;
				return;
			}

			if (!IsOpenPath(n))
			{
				D2D1_POINT_2F handles[9];
				if (GetHandleScreenPositions(n, e.view, e.pD2DFactory, handles))
				{
					const ge::Matrix2D wxf = n->GetWorldTransform();
					m_hoverAngleDeg = std::atan2(wxf.m12, wxf.m11) * (180.0f / 3.14159265358979323846f);
					Handle hit = HitHandle(screenPt, handles, 8.0f);
					if (hit != Handle::None)
					{
						m_hoverHandle = hit;
						return;
					}
					if (IsCornerRotateZone(screenPt, handles))
					{
						m_hoverCornerRotate = true;
						return;
					}
				}
			}
		}

		// 2) 도형 스트로크 또는 면(채우기 내부) 호버 검사 -> 4방향 이동 커서(IDC_SIZEALL) 활성화
		m_hoverOnShape = HitTestTopShape(e.pDoc, screenPt, e.view, e.pD2DFactory) != nullptr;
		return;
	}

	if (m_mode == Mode::Marquee)
	{
		// 마키(영역 선택) 드래그 중: 화면 갱신 유도
		return;
	}

	if (m_mode == Mode::TableResize)
	{
		if (m_tbl)
		{
			const float delta = TableLocalAxis(m_tbl, e.worldPt, m_tblCol) - m_tblStartLocal;
			if (m_tblCol) ge::TableComponent::ResizeCol(*m_tbl, m_tblIdx, m_tblStartSize + delta);
			else ge::TableComponent::ResizeRow(*m_tbl, m_tblIdx, m_tblStartSize + delta);
		}
		return;
	}

	if (m_mode == Mode::GradientHandle)
	{
		ApplyGradientDrag(e);

		static DWORD s_lastGradTick = 0;
		const DWORD curTick = ::GetTickCount();
		if (curTick - s_lastGradTick >= 30)
		{
			s_lastGradTick = curTick;
			UpdateShapePropertiesSelection(m_gradShape, e.pDoc);   // 속성창 수치(시작/끝/반지름) 실시간 동기화
		}
		return;
	}

	if (m_mode == Mode::EditNode)
	{
		if (m_editShape)
		{
			ApplyNodeEdit(m_editShape, e);

			static DWORD s_lastNodeTick = 0;
			DWORD curTick = ::GetTickCount();
			if (curTick - s_lastNodeTick >= 30)
			{
				s_lastNodeTick = curTick;
				UpdateShapePropertiesSelection(m_editShape, e.pDoc);
			}
		}
		return;
	}

	if (m_mode == Mode::MultiScale || m_mode == Mode::MultiRotate || m_mode == Mode::MultiSkew)
	{
		ApplyMultiTransform(e);
		return;
	}

	if (sel.empty())
		return;

	switch (m_mode)
	{
	case Mode::Moving:
		ApplyMove(sel);
		break;
	case Mode::Scaling:
		ApplyScale(m_xfTarget);
		break;
	case Mode::Rotating:
		ApplyRotate(m_xfTarget);
		break;
	case Mode::Skewing:
		ApplySkew(m_xfTarget);
		break;
	default:
		break;
	}

	// 변형(이동, 크기 조절, 회전, 전단) 중 상단 옵션바 수치 실시간 동기화 (30ms 쓰로틀링으로 메시지 큐 지연 차단)
	if (m_mode == Mode::Moving || m_mode == Mode::Scaling || m_mode == Mode::Rotating || m_mode == Mode::Skewing)
	{
		static DWORD s_lastDragTick = 0;
		DWORD curTick = ::GetTickCount();
		if (curTick - s_lastDragTick >= 30)
		{
			s_lastDragTick = curTick;
			ge::Node* shown = (m_mode == Mode::Moving || !m_xfTarget) ? sel.front() : m_xfTarget;
			UpdateShapePropertiesLight(shown, e.pDoc);   // 드래그 중에는 값 동기화만 (레이어 패널/서브도구 바 재구성은 놓을 때)
		}
	}
}

// 기즈모 핸들 드래그: 화면 좌표 -> 도형 로컬 -> 지오메트리 박스 비율로 바꿔 그라디언트 필드에 반영
void SelectTool::ApplyGradientDrag(const ToolEvent& e)
{
	if (!m_gradShape) return;
	const D2D1_POINT_2F screenPt = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
	ApplyGradPartDrag(m_gradShape, m_gradBounds, static_cast<int>(m_gradHandle), screenPt, e.view);
}

// 엔진에 에디트 포인트 조작 위임!
void SelectTool::ApplyNodeEdit(ge::PathNode* shape, const ToolEvent& e)
{
	if (!shape) return;
	// Alt 키가 눌려있으면 대칭/스무스 구속을 깨고 한쪽 핸들만 독립적으로 꺾음 (일러스트레이터 방식)
	shape->MoveNodeOrHandle(m_editHit.part, m_editHit.subPath, m_editHit.nodeIdx, e.worldPt, e.altDown);
}

// 엔진에 평행 이동 위임! (선택된 모든 도형 일괄 이동)
void SelectTool::RefreshGuidesFor(ge::Node* node)
{
	m_guides.clear();
	if (!node) return;
	ge::Rect2 wb;
	if (!ge::GuideWorldBounds(*node, wb)) return;
	const ge::GuideTargets targets = ge::CollectGuideTargets({ node });
	m_guides = ge::SnapToGuides(wb, targets, 0.05f).lines;   // 이미 맞춰진 변에만 선을 표시
}

void SelectTool::ApplyMove(const std::vector<ge::Node*>& selection)
{
	float dx = m_dragCurrentWorld.x - m_dragStartWorld.x;
	float dy = m_dragCurrentWorld.y - m_dragStartWorld.y;

	const auto& grid = GetApp()->GetGrid();
	const auto& snapMgr = GetApp()->GetSnapManager();
	// 그리드 스냅: 모든 도구 공통 토글("Snap to Grid")을 따른다. 꺼져 있으면 그리드에 붙지 않음
	if (snapMgr.IsGridSnapActive(grid) && !selection.empty())
	{
		// 기준(대표) 노드의 기준점을 월드 좌표에서 격자에 맞춤: 패스는 첫 앵커, 그 외(프레임/표/글자/이미지)는 경계 좌상단
		ge::Node* primary = selection.front();
		auto itP = m_initialXfs.find(primary);
		const ge::Matrix2D initXf = (itP != m_initialXfs.end()) ? ge::Matrix2D(itP->second) : primary->GetTransform();
		ge::Matrix2D parentWorld;
		if (ge::GroupNode* par = primary->GetParent()) parentWorld = par->GetWorldTransform();

		ge::Vec2 localRef{ 0.0f, 0.0f };
		bool haveRef = false;
		if (auto* ps = dynamic_cast<ge::PathNode*>(primary); ps && !ps->GetPaths().empty() && !ps->GetPaths()[0].nodes.empty())
		{
			localRef = ps->GetPaths()[0].nodes[0].anchor;
			haveRef = true;
		}
		else
		{
			ge::Rect2 lb;
			if (primary->GetLocalBounds(lb)) { localRef = ge::Vec2{ lb.left, lb.top }; haveRef = true; }
		}
		if (haveRef)
		{
			const ge::Vec2 p0 = parentWorld.TransformPoint(initXf.TransformPoint(localRef));
			const D2D1_POINT_2F target = grid.SnapPoint(D2D1::Point2F(p0.x + dx, p0.y + dy));
			dx = target.x - p0.x;
			dy = target.y - p0.y;
		}
	}

	// 스마트 가이드: 부모 프레임/형제의 가장자리·가운데에 맞춤 (Alt 로 일시 해제, 마스터 스냅이 꺼져 있으면 사용 안 함)
	m_guides.clear();
	m_gaps.clear();
	if (snapMgr.IsSmartGuidesEnabled() && !selection.empty() && !(::GetKeyState(VK_MENU) & 0x8000))
	{
		bool any = false;
		ge::Rect2 mr{};
		for (ge::Node* n : selection)
		{
			auto it = m_initialXfs.find(n);
			const ge::Matrix2D ix = (it != m_initialXfs.end()) ? ge::Matrix2D(it->second) : n->GetTransform();
			ge::Rect2 lb;
			if (!n->GetLocalBounds(lb)) continue;
			ge::Matrix2D pw;
			if (ge::GroupNode* par = n->GetParent()) pw = par->GetWorldTransform();
			const ge::Rect2 wr = pw.TransformRect(ix.TransformRect(lb));
			if (!any) { mr = wr; any = true; } else mr.Union(wr);
		}
		if (any)
		{
			const ge::GuideTargets targets = ge::CollectGuideTargets(selection);
			const ge::Rect2 moved{ mr.left + dx, mr.top + dy, mr.right + dx, mr.bottom + dy };
			ge::GuideFlags gf;
			gf.equalSpacing = snapMgr.IsEqualSpacingEnabled();
			const ge::GuideResult gr = ge::SnapToGuides(moved, targets, 6.0f / (std::max)(m_viewZoom, 0.01f), gf);
			dx += gr.dx;
			dy += gr.dy;
			m_guides = gr.lines;
			m_gaps = gr.gaps;
		}
	}

	// 선택된 모든 도형에 동일한 이동량 적용
	for (ge::Node* n : selection)
	{
		auto it = m_initialXfs.find(n);
		ge::Matrix2D baseInitXf = (it != m_initialXfs.end()) ? it->second : n->GetTransform();

		// 부모가 있는 노드(예: FrameNode 안의 자식)인 경우 월드 이동량(dx, dy)을 부모 로컬 변위로 환산
		ge::GroupNode* parent = n->GetParent();
		if (parent && parent->Kind() != ge::NodeKind::Layer)
		{
			ge::Matrix2D pInv = parent->GetWorldTransform();
			if (pInv.Invert())
			{
				ge::Vec2 d0 = pInv.TransformPoint(ge::Vec2{ 0.0f, 0.0f });
				ge::Vec2 d1 = pInv.TransformPoint(ge::Vec2{ dx, dy });
				ge::Vec2 localDelta = { d1.x - d0.x, d1.y - d0.y };
				n->SetTransform(ge::Transformer::Translate(localDelta, baseInitXf));
			}
			else
			{
				n->SetTransform(ge::Transformer::Translate(ge::Vec2{ dx, dy }, baseInitXf));
			}
		}
		else
		{
			n->SetTransform(ge::Transformer::Translate(ge::Vec2{ dx, dy }, baseInitXf));
		}
		if (n->Kind() != ge::NodeKind::Text) n->Invalidate();   // 텍스트 배치 캐시 보존

			// pointer inside the parent frame: child cannot leave the parent's range (dragging the pointer out allows re-parenting)
			if (auto* pf = dynamic_cast<ge::FrameNode*>(n->GetParent()))
			{
				ge::Matrix2D inv = pf->GetWorldTransform();
				if (inv.Invert())
				{
					const ge::Vec2 lp = inv.TransformPoint(ge::Vec2{ m_dragCurrentWorld.x, m_dragCurrentWorld.y });
					if (lp.x >= 0.0f && lp.y >= 0.0f && lp.x <= pf->GetWidth() && lp.y <= pf->GetHeight())
						pf->ClampChild(n, false);
				}
			}
	}
}

// 둥근 사각형 경로 (원점 기준 w x h, 모서리 반경 rx/ry 는 변의 절반으로 제한). 크기를 바꿔도 반경이 찌그러지지 않게 새로 만든다 (Step 6.10.1)
static std::vector<ge::SubPath> BuildRoundedRectPaths(float w, float h, float rx, float ry)
{
	const float crx = (std::min)((std::max)(0.0f, rx), w * 0.5f);
	const float cry = (std::min)((std::max)(0.0f, ry), h * 0.5f);
	ge::SubPath sp;
	sp.closed = true;
	auto add = [&](float ax, float ay, float ix, float iy, float ox, float oy) {
		ge::BezierNode b; b.anchor = ge::Vec2{ ax, ay }; b.hIn = ge::Vec2{ ix, iy }; b.hOut = ge::Vec2{ ox, oy };
		sp.nodes.push_back(b);
	};
	if (crx > 0.001f || cry > 0.001f) {
		const float kx = crx * 0.55228475f, ky = cry * 0.55228475f;
		add(crx, 0.0f, -kx, 0.0f, 0.0f, 0.0f);       add(w - crx, 0.0f, 0.0f, 0.0f, kx, 0.0f);
		add(w, cry, 0.0f, -ky, 0.0f, 0.0f);          add(w, h - cry, 0.0f, 0.0f, 0.0f, ky);
		add(w - crx, h, kx, 0.0f, 0.0f, 0.0f);       add(crx, h, 0.0f, 0.0f, -kx, 0.0f);
		add(0.0f, h - cry, 0.0f, ky, 0.0f, 0.0f);   add(0.0f, cry, 0.0f, 0.0f, 0.0f, -ky);
	} else {
		add(0.0f, 0.0f, 0, 0, 0, 0); add(w, 0.0f, 0, 0, 0, 0); add(w, h, 0, 0, 0, 0); add(0.0f, h, 0, 0, 0, 0);
	}
	return { sp };
}

// 엔진에 스케일 변형 위임!
void SelectTool::ApplyScale(ge::Node* shape)
{
	if (!shape) return;
	// curWorld 를 부모 좌표계로 역변환하여 로컬/월드 일치 유지
	D2D1_POINT_2F curWorld = D2D1::Point2F(m_dragCurrentWorld.x, m_dragCurrentWorld.y);

	// 스마트 가이드: 움직이는 변만 부모 프레임/형제에 맞춤 (Alt 로 일시 해제)
	const bool guidesOn = GetApp()->GetSnapManager().IsSmartGuidesEnabled() && !(::GetKeyState(VK_MENU) & 0x8000);
	if (guidesOn)
	{
		ge::GuideFlags fl;
		fl.left = fl.centerX = fl.right = fl.top = fl.centerY = fl.bottom = false;
		switch (m_handle)
		{
		case Handle::TL: fl.left = fl.top = true; break;
		case Handle::T:  fl.top = true; break;
		case Handle::TR: fl.left = fl.top = true; break;
		case Handle::R:  fl.left = true; break;
		case Handle::BR: fl.left = fl.top = true; break;
		case Handle::B:  fl.top = true; break;
		case Handle::BL: fl.left = fl.top = true; break;
		case Handle::L:  fl.left = true; break;
		default: break;
		}
		if (fl.left || fl.top)
		{
			const ge::GuideTargets targets = ge::CollectGuideTargets({ shape });
			// 가로/세로 중 해당 핸들이 움직이는 축만 보정 (한 점이므로 left/top 하나로 대표)
			const ge::GuideResult gr = ge::SnapToGuides(ge::Rect2{ curWorld.x, curWorld.y, curWorld.x, curWorld.y }, targets, 6.0f / (std::max)(m_viewZoom, 0.01f), fl);
			if (m_handle != Handle::T && m_handle != Handle::B) curWorld.x += gr.dx;
			if (m_handle != Handle::L && m_handle != Handle::R) curWorld.y += gr.dy;
		}
	}
	struct GuideRefresh { SelectTool* tool; ge::Node* node; ~GuideRefresh() { tool->RefreshGuidesFor(node); } } guideRefresh{ this, guidesOn ? shape : nullptr };
	ge::GroupNode* parent = shape->GetParent();
	if (parent && parent->Kind() != ge::NodeKind::Layer)
	{
		ge::Matrix2D pInv = parent->GetWorldTransform();
		if (pInv.Invert())
		{
			ge::Vec2 cw = pInv.TransformPoint(ge::Vec2{ curWorld.x, curWorld.y });
			curWorld = D2D1::Point2F(cw.x, cw.y);
		}
	}
	auto newXf = ge::Transformer::ScaleByHandle(static_cast<int>(m_handle), curWorld, m_initialLocalBounds, m_initialXf);
	if (newXf)
	{
		// 프레임: 변환 스케일 대신 실제 프레임 크기를 바꿔 자식 제약(Constraints)/자동 배치가 반영되게 함 (4.5.11).
		// 이동/스케일만 있는(회전 없는) 프레임일 때만 적용하고, 그 외에는 기존처럼 변환으로 처리
		if (auto* fr = dynamic_cast<ge::FrameNode*>(shape))
		{
			const bool plainStart = std::fabs(m_initialXf._11 - 1.0f) < 1e-4f && std::fabs(m_initialXf._22 - 1.0f) < 1e-4f &&
			                        std::fabs(m_initialXf._12) < 1e-4f && std::fabs(m_initialXf._21) < 1e-4f;
			const bool plainEnd = std::fabs(newXf->m12) < 1e-4f && std::fabs(newXf->m21) < 1e-4f && newXf->m11 > 1e-4f && newXf->m22 > 1e-4f;
			if (plainStart && plainEnd)
			{
				const float w0 = m_initialLocalBounds.right - m_initialLocalBounds.left;
				const float h0 = m_initialLocalBounds.bottom - m_initialLocalBounds.top;
				fr->SetTransform(ge::Matrix2D::Translation(newXf->dx, newXf->dy));
				fr->ResizeFrame(w0 * newXf->m11, h0 * newXf->m22);
				ge::FrameNode::ClampNode(fr, true);
				return;
			}
		}
		// 파라메트릭 사각형: 변환 스케일 대신 경로를 새 크기로 다시 만들어 모서리 반경이 찌그러지지 않게 함 (6.10.1).
		// 이동/스케일만 있는(회전·전단·반전 없는) 경우에만 적용하고 그 외에는 기존처럼 변환으로 처리
		if (auto* rps = dynamic_cast<ge::PathNode*>(shape))
		{
			const ge::RectParams* rp = rps->GetParam<ge::RectParams>();
			const bool plainStart = std::fabs(m_initialXf._12) < 1e-4f && std::fabs(m_initialXf._21) < 1e-4f && m_initialXf._11 > 1e-4f && m_initialXf._22 > 1e-4f;
			const bool plainEnd = std::fabs(newXf->m12) < 1e-4f && std::fabs(newXf->m21) < 1e-4f && newXf->m11 > 1e-4f && newXf->m22 > 1e-4f;
			if (rps->GetShapeKind() == ge::ShapeKind::Rect && rp && plainStart && plainEnd)
			{
				const ge::Rect2 nb = newXf->TransformRect(ge::Rect2{ m_initialLocalBounds.left, m_initialLocalBounds.top, m_initialLocalBounds.right, m_initialLocalBounds.bottom });
				const float nw = nb.right - nb.left, nh = nb.bottom - nb.top;
				if (nw >= 1.0f && nh >= 1.0f)
				{
					rps->SetPaths(BuildRoundedRectPaths(nw, nh, rp->rx, rp->ry));
					rps->SetTransform(ge::Matrix2D::Translation(nb.left, nb.top));
					rps->Invalidate();
					ge::FrameNode::ClampNode(shape, true);
					return;
				}
			}
		}
		shape->SetTransform(*newXf);
		// 텍스트는 변환만 바뀌므로 Invalidate 하면 안 됨: 버전이 매 프레임 바뀌어 글자 배치 캐시가 계속 무효화됨 (크기 조절이 버벅거림)
		if (shape->Kind() != ge::NodeKind::Text) shape->Invalidate();
		ge::FrameNode::ClampNode(shape, true);   // clip overflowing side (opposite edge fixed)
	}
}

// 엔진에 회전 변형 위임!
void SelectTool::ApplyRotate(ge::Node* shape)
{
	if (!shape) return;
	D2D1_POINT_2F startWorld = D2D1::Point2F(m_dragStartWorld.x, m_dragStartWorld.y);
	D2D1_POINT_2F curWorld = D2D1::Point2F(m_dragCurrentWorld.x, m_dragCurrentWorld.y);
	ge::GroupNode* parent = shape->GetParent();
	if (parent && parent->Kind() != ge::NodeKind::Layer)
	{
		ge::Matrix2D pInv = parent->GetWorldTransform();
		if (pInv.Invert())
		{
			ge::Vec2 sw = pInv.TransformPoint(ge::Vec2{ startWorld.x, startWorld.y });
			ge::Vec2 cw = pInv.TransformPoint(ge::Vec2{ curWorld.x, curWorld.y });
			startWorld = D2D1::Point2F(sw.x, sw.y);
			curWorld = D2D1::Point2F(cw.x, cw.y);
		}
	}
	auto newXf = ge::Transformer::RotateAboutCenter(startWorld, curWorld, m_initialLocalBounds, m_initialXf);
	shape->SetTransform(newXf);
	if (shape->Kind() != ge::NodeKind::Text) shape->Invalidate();   // 텍스트 배치 캐시 보존
	ge::FrameNode::ClampNode(shape, false);
}

// 엔진에 전단(Skew/Shear) 변형 위임! (6.14.4)
void SelectTool::ApplySkew(ge::Node* shape)
{
	if (!shape) return;
	ge::Vec2 startWorld{ m_dragStartWorld.x, m_dragStartWorld.y };
	ge::Vec2 curWorld{ m_dragCurrentWorld.x, m_dragCurrentWorld.y };
	ge::GroupNode* parent = shape->GetParent();
	if (parent && parent->Kind() != ge::NodeKind::Layer)
	{
		ge::Matrix2D pInv = parent->GetWorldTransform();
		if (pInv.Invert())
		{
			startWorld = pInv.TransformPoint(startWorld);
			curWorld = pInv.TransformPoint(curWorld);
		}
	}
	const ge::Rect2 initBounds{ m_initialLocalBounds.left, m_initialLocalBounds.top, m_initialLocalBounds.right, m_initialLocalBounds.bottom };
	auto newXf = ge::Transformer::SkewByEdgeHandle(static_cast<int>(m_handle), startWorld, curWorld, initBounds, ge::Matrix2D(m_initialXf));
	if (newXf)
	{
		shape->SetTransform(*newXf);
		if (shape->Kind() != ge::NodeKind::Text) shape->Invalidate();
		ge::FrameNode::ClampNode(shape, false);
	}
}

void SelectTool::OnUp(const ToolEvent& e)
{
	if (m_mode == Mode::Marquee && e.pDoc)
	{
		// 드래그한 마키 사각형 영역 계산 (월드 좌표)
		float minX = (std::min)(m_dragStartWorld.x, m_dragCurrentWorld.x);
		float maxX = (std::max)(m_dragStartWorld.x, m_dragCurrentWorld.x);
		float minY = (std::min)(m_dragStartWorld.y, m_dragCurrentWorld.y);
		float maxY = (std::max)(m_dragStartWorld.y, m_dragCurrentWorld.y);

		// 단순 클릭이 아니라 일정 크기 이상 드래그되었을 때만 영역 선택 판정
		if ((maxX - minX) > 2.0f || (maxY - minY) > 2.0f)
		{
			auto& doc = e.pDoc->GetDoc();
			std::vector<ge::Node*> newSel;
			if (e.ctrlDown) newSel = doc.GetSelection();

			ID2D1Factory* f = e.pD2DFactory;
			std::function<void(ge::Node*)> testMarqueeNode = [&](ge::Node* n) {
				if (!n || !n->IsVisible()) return;
				// 일반 프레임(AllowsChildSelection)이 잠겨 있는 경우: 프레임 자체는 선택하지 않고 내부의 잠기지 않은 자식만 검사!
				if (ge::AllowsChildSelection(n) && n->IsLocked())
				{
					auto* grp = static_cast<ge::GroupNode*>(n);
					for (const auto& c : grp->GetChildren())
						testMarqueeNode(c.get());
					return;
				}
				if (n->IsEffectivelyLocked()) return;

				D2D1_RECT_F localB{};
				if (!n->GetLocalBounds(f, localB)) return;

				// 도형의 4개 코너를 월드 변환하여 AABB 산출 (중첩 프레임 내 자식도 정확하도록 GetWorldTransform 사용)
				const ge::Matrix2D xf = n->GetWorldTransform();
				D2D1_POINT_2F pts[4] = {
					xf.TransformPoint(D2D1::Point2F(localB.left, localB.top)),
					xf.TransformPoint(D2D1::Point2F(localB.right, localB.top)),
					xf.TransformPoint(D2D1::Point2F(localB.right, localB.bottom)),
					xf.TransformPoint(D2D1::Point2F(localB.left, localB.bottom))
				};

				float sMinX = pts[0].x, sMaxX = pts[0].x;
				float sMinY = pts[0].y, sMaxY = pts[0].y;
				for (int k = 1; k < 4; ++k)
				{
					sMinX = (std::min)(sMinX, pts[k].x);
					sMaxX = (std::max)(sMaxX, pts[k].x);
					sMinY = (std::min)(sMinY, pts[k].y);
					sMaxY = (std::max)(sMaxY, pts[k].y);
				}

				// 사각형 교차(Intersect) 판정
				bool intersects = !(sMaxX < minX || sMinX > maxX || sMaxY < minY || sMinY > maxY);
				if (intersects && std::find(newSel.begin(), newSel.end(), n) == newSel.end())
				{
					newSel.push_back(n);
				}
			};

			ForEachSelectableTopDown(e.pDoc, [&](ge::Node* n) {
				testMarqueeNode(n);
				return false;
			});

			doc.SetSelection(newSel);
			ActivateLayerOf(e.pDoc, e.pDoc->GetPrimarySelection());
			UpdateShapePropertiesSelection(e.pDoc->GetPrimarySelection(), e.pDoc);
			CString str;
			str.Format(_T("[선택 도구] 마키 영역 선택 완료 (선택된 도형: %d개)"), static_cast<int>(newSel.size()));
			LogAppEvent(str);
		}
	}
	else if (m_mode != Mode::None && e.pDoc)
	{
		// 끌어서 놓은 위치의 프레임으로 부모 변경 (프레임 안으로 넣거나 밖으로 빼기, 4.5.13)
		if (m_mode == Mode::Moving)
		{
			const float ddx = e.worldPt.x - m_dragStartWorld.x, ddy = e.worldPt.y - m_dragStartWorld.y;
			if (ddx * ddx + ddy * ddy > 4.0f)
			{
				auto& doc = e.pDoc->GetDoc();
				const std::vector<ge::Node*> sel = doc.GetSelection();
				bool any = false;
				for (ge::Node* n : sel)
					any |= doc.ReparentAt(n, ge::Vec2{ e.worldPt.x, e.worldPt.y });
				for (ge::Node* n : sel) ge::FrameNode::ClampNode(n, false);
				if (any) LogAppEvent(_T("[선택 도구] 부모 프레임 변경"));
			}
		}
		e.pDoc->SetModifiedFlag(TRUE);
		UpdateShapePropertiesSelection(e.pDoc->GetPrimarySelection(), e.pDoc);
		CString str;
		str.Format(_T("[선택 도구] 변형 완료 (모드: %d)"), static_cast<int>(m_mode));
		LogAppEvent(str);
	}

	m_mode = Mode::None;
	m_handle = Handle::None;
	m_editShape = nullptr;
	m_xfTarget = nullptr;
	m_gradShape = nullptr;
	m_gradHandle = GradHandle::None;
	m_tbl = nullptr;
	m_guides.clear();
	m_gaps.clear();
	m_multiNodes.clear();
	m_multiInitial.clear();
	m_initialXfs.clear();
}

ge::Vec2 SnapCreationPoint(const ToolEvent& e, ge::Vec2 p, std::vector<ge::GuideLine>& out)
{
	out.clear();
	if (!e.pDoc || e.altDown || !GetApp()->GetSnapManager().IsSmartGuidesEnabled()) return p;
	const ge::GuideTargets t = ge::CollectGuideTargetsAt(e.pDoc->GetDoc(), p);
	ge::GuideFlags fl;
	fl.centerX = fl.right = fl.centerY = fl.bottom = false;   // 한 점이므로 left/top 으로 대표
	const ge::GuideResult r = ge::SnapToGuides(ge::Rect2{ p.x, p.y, p.x, p.y }, t, 6.0f / (std::max)(e.view.zoom, 0.01f), fl);
	out = r.lines;
	return ge::Vec2{ p.x + r.dx, p.y + r.dy };
}

void DrawGuideLines(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const std::vector<ge::GuideLine>& guides, float zoom, D2D1_POINT_2F pan)
{
	if (!rt || !brush || guides.empty()) return;
	const ViewportTransform view = { zoom, { pan.x, pan.y } };
	brush->SetColor(D2D1::ColorF(1.0f, 0.27f, 0.20f, 1.0f));
	for (const ge::GuideLine& g : guides)
	{
		auto pt = [&](float along) { return g.vertical ? view.WorldToScreen(D2D1::Point2F(g.pos, along)) : view.WorldToScreen(D2D1::Point2F(along, g.pos)); };
		rt->DrawLine(pt(g.from), pt(g.to), brush, 1.0f);
		for (float mk : g.marks)
		{
			const D2D1_POINT_2F p = pt(mk);
			rt->DrawLine(D2D1::Point2F(p.x - 3.0f, p.y - 3.0f), D2D1::Point2F(p.x + 3.0f, p.y + 3.0f), brush, 1.0f);
			rt->DrawLine(D2D1::Point2F(p.x - 3.0f, p.y + 3.0f), D2D1::Point2F(p.x + 3.0f, p.y - 3.0f), brush, 1.0f);
		}
	}
}

void DrawGapMarks(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, const std::vector<ge::GapMark>& gaps, float zoom, D2D1_POINT_2F pan)
{
	if (!rt || !brush || gaps.empty()) return;
	const ViewportTransform view = { zoom, { pan.x, pan.y } };
	static IDWriteFactory* s_dw = nullptr;
	static IDWriteTextFormat* s_fmt = nullptr;
	if (!s_dw)
	{
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s_dw));
		if (s_dw)
		{
			s_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"ko-kr", &s_fmt);
			if (s_fmt)
			{
				s_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
				s_fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				s_fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			}
		}
	}
	for (const ge::GapMark& g : gaps)
	{
		const D2D1_POINT_2F A = g.horizontal ? view.WorldToScreen(D2D1::Point2F(g.a, g.cross)) : view.WorldToScreen(D2D1::Point2F(g.cross, g.a));
		const D2D1_POINT_2F B = g.horizontal ? view.WorldToScreen(D2D1::Point2F(g.b, g.cross)) : view.WorldToScreen(D2D1::Point2F(g.cross, g.b));
		brush->SetColor(D2D1::ColorF(1.0f, 0.27f, 0.20f, 1.0f));
		rt->DrawLine(A, B, brush, 1.0f);
		if (g.horizontal)
		{
			rt->DrawLine(D2D1::Point2F(A.x, A.y - 4.0f), D2D1::Point2F(A.x, A.y + 4.0f), brush, 1.0f);
			rt->DrawLine(D2D1::Point2F(B.x, B.y - 4.0f), D2D1::Point2F(B.x, B.y + 4.0f), brush, 1.0f);
		}
		else
		{
			rt->DrawLine(D2D1::Point2F(A.x - 4.0f, A.y), D2D1::Point2F(A.x + 4.0f, A.y), brush, 1.0f);
			rt->DrawLine(D2D1::Point2F(B.x - 4.0f, B.y), D2D1::Point2F(B.x + 4.0f, B.y), brush, 1.0f);
		}
		if (s_fmt)
		{
			wchar_t buf[24];
			swprintf_s(buf, L"%d", static_cast<int>(std::round(g.value)));
			const D2D1_POINT_2F c = D2D1::Point2F((A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f);
			const float tw = static_cast<float>(wcslen(buf)) * 6.4f + 10.0f;
			const D2D1_RECT_F pill = D2D1::RectF(c.x - tw * 0.5f, c.y - 8.0f, c.x + tw * 0.5f, c.y + 8.0f);
			brush->SetColor(D2D1::ColorF(1.0f, 0.27f, 0.20f, 1.0f));
			rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 3.0f, 3.0f), brush);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->DrawText(buf, static_cast<UINT32>(wcslen(buf)), s_fmt, pill, brush);
		}
	}
}

void SelectTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush || !pDoc) return;

	ViewportTransform view = { zoom, { pan.x, pan.y } };

	// 1. 마키(Marquee) 드래그 선택 사각형 렌더링
	if (m_mode == Mode::Marquee)
	{
		D2D1_POINT_2F p1 = view.WorldToScreen(m_dragStartWorld);
		D2D1_POINT_2F p2 = view.WorldToScreen(m_dragCurrentWorld);
		D2D1_RECT_F mr = D2D1::RectF(
			(std::min)(p1.x, p2.x), (std::min)(p1.y, p2.y),
			(std::max)(p1.x, p2.x), (std::max)(p1.y, p2.y)
		);

		// 반투명 파란색 채우기
		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.15f));
		rt->FillRectangle(mr, brush);

		// 점선/실선 테두리
		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.85f));
		rt->DrawRectangle(mr, brush, 1.2f);
	}

	const std::vector<ge::Node*>& sel = pDoc->GetDoc().GetSelection();
	if (sel.empty()) return;

	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	if (!f) return;

	for (ge::Node* shape : sel)
	{
		if (!shape || !shape->IsEffectivelyVisible()) continue;
		if (sel.size() >= 2)
		{
			// 다중 선택: 각 도형은 옅은 윤곽만, 핸들은 아래의 결합 기즈모 하나로
			ge::Rect2 wb;
			if (ge::GuideWorldBounds(*shape, wb))
			{
				const D2D1_POINT_2F a0 = view.WorldToScreen(D2D1::Point2F(wb.left, wb.top)), b0 = view.WorldToScreen(D2D1::Point2F(wb.right, wb.bottom));
				brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.45f));
				rt->DrawRectangle(D2D1::RectF(a0.x, a0.y, b0.x, b0.y), brush, 1.0f);
			}
			continue;
		}
		D2D1_POINT_2F handles[9];
		D2D1_RECT_F localBounds;
		if (!GetHandleScreenPositions(shape, view, f, handles, &localBounds))
			continue;

		if (!IsOpenPath(shape))
		{
			D2D1_POINT_2F pTL = handles[0];
			D2D1_POINT_2F pTR = handles[2];
			D2D1_POINT_2F pBR = handles[4];
			D2D1_POINT_2F pBL = handles[6];

			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.85f));
			rt->DrawLine(pTL, pTR, brush, 1.2f);
			rt->DrawLine(pTR, pBR, brush, 1.2f);
			rt->DrawLine(pBR, pBL, brush, 1.2f);
			rt->DrawLine(pBL, pTL, brush, 1.2f);

			D2D1_POINT_2F pT = handles[1];
			D2D1_POINT_2F pRot = handles[8];
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
			rt->DrawLine(pT, pRot, brush, 1.0f);

			bool isRotActive = (m_handle == Handle::Rotate);
			brush->SetColor(isRotActive ? D2D1::ColorF(0.12f, 0.55f, 0.95f, 1.0f) : D2D1::ColorF(D2D1::ColorF::White));
			rt->FillEllipse(D2D1::Ellipse(pRot, 4.5f, 4.5f), brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawEllipse(D2D1::Ellipse(pRot, 4.5f, 4.5f), brush, 1.5f);

			const float hSize = 3.5f;
			const D2D1_POINT_2F centerPt = D2D1::Point2F((pTL.x + pBR.x) * 0.5f, (pTL.y + pBR.y) * 0.5f);

			// 회전 기준점(Pivot Point ⊕) 표시 (6.14.5)
			brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f));
			rt->FillEllipse(D2D1::Ellipse(centerPt, 4.0f, 4.0f), brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.9f));
			rt->DrawEllipse(D2D1::Ellipse(centerPt, 4.0f, 4.0f), brush, 1.1f);
			rt->DrawLine(D2D1::Point2F(centerPt.x - 5.5f, centerPt.y), D2D1::Point2F(centerPt.x + 5.5f, centerPt.y), brush, 1.0f);
			rt->DrawLine(D2D1::Point2F(centerPt.x, centerPt.y - 5.5f), D2D1::Point2F(centerPt.x, centerPt.y + 5.5f), brush, 1.0f);

			// 양방향 변형 화살표(Bi-directional Transform Arrow Gizmo) 드로잉 헬퍼 (6.14.1)
			auto drawBiArrow = [&](D2D1_POINT_2F c, float ux, float uy, float halfLen, bool highlight) {
				const float px = -uy, py = ux;
				const D2D1_POINT_2F a = D2D1::Point2F(c.x - ux * halfLen, c.y - uy * halfLen);
				const D2D1_POINT_2F b = D2D1::Point2F(c.x + ux * halfLen, c.y + uy * halfLen);
				const float head = highlight ? 3.8f : 3.0f;
				brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, highlight ? 0.95f : 0.75f));
				rt->DrawLine(a, b, brush, highlight ? 2.6f : 2.0f);
				brush->SetColor(highlight ? D2D1::ColorF(0.06f, 0.36f, 0.90f, 1.0f) : D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.85f));
				rt->DrawLine(a, b, brush, highlight ? 1.5f : 1.1f);
				// 양끝 화살촉
				rt->DrawLine(a, D2D1::Point2F(a.x + ux * head + px * head * 0.7f, a.y + uy * head + py * head * 0.7f), brush, 1.3f);
				rt->DrawLine(a, D2D1::Point2F(a.x + ux * head - px * head * 0.7f, a.y + uy * head - py * head * 0.7f), brush, 1.3f);
				rt->DrawLine(b, D2D1::Point2F(b.x - ux * head + px * head * 0.7f, b.y - uy * head + py * head * 0.7f), brush, 1.3f);
				rt->DrawLine(b, D2D1::Point2F(b.x - ux * head - px * head * 0.7f, b.y - uy * head - py * head * 0.7f), brush, 1.3f);
			};

			for (int i = 0; i < 8; ++i)
			{
				Handle hType = static_cast<Handle>(i + 1);
				bool isSelectedHandle = (m_handle == hType);
				bool isHoveredHandle = (m_hoverHandle == hType);

				D2D1_RECT_F r = D2D1::RectF(
					handles[i].x - hSize, handles[i].y - hSize,
					handles[i].x + hSize, handles[i].y + hSize
				);

				if (isSelectedHandle || isHoveredHandle)
				{
					brush->SetColor(D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f));
					rt->FillRectangle(r, brush);
					brush->SetColor(D2D1::ColorF(0.05f, 0.25f, 0.70f, 1.0f));
					rt->DrawRectangle(r, brush, 1.6f);

					D2D1_RECT_F outerR = D2D1::RectF(r.left - 2.0f, r.top - 2.0f, r.right + 2.0f, r.bottom + 2.0f);
					brush->SetColor(D2D1::ColorF(0.10f, 0.50f, 0.95f, 0.45f));
					rt->DrawRectangle(outerR, brush, 1.0f);
				}
				else
				{
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
					rt->FillRectangle(r, brush);
					brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
					rt->DrawRectangle(r, brush, 1.2f);
				}

				// 모서리/변 핸들 바깥쪽에 양방향 스케일 또는 전단(Skew) 화살표 표시 (6.14.1)
				float vx = handles[i].x - centerPt.x;
				float vy = handles[i].y - centerPt.y;
				const float vLen = std::hypot(vx, vy);
				if (vLen > 8.0f)
				{
					vx /= vLen; vy /= vLen;
					const bool isEdge = (i % 2 == 1);
					const bool showSkew = isEdge && (m_ctrlDown || m_mode == Mode::Skewing);
					const D2D1_POINT_2F arrowCenter = D2D1::Point2F(handles[i].x + vx * 11.0f, handles[i].y + vy * 11.0f);
					if (showSkew)
					{
						// 접선 방향 전단(Shear) 화살표
						drawBiArrow(arrowCenter, -vy, vx, 7.0f, isSelectedHandle || isHoveredHandle);
					}
					else if (isSelectedHandle || isHoveredHandle)
					{
						drawBiArrow(arrowCenter, vx, vy, 7.5f, true);
					}
				}
			}

			// 회전 핸들 호버/드래그 또는 코너 회전 근접 시 곡선 회전 화살표 기즈모 표시
			if (isRotActive || m_hoverHandle == Handle::Rotate || m_hoverCornerRotate)
			{
				const float rArc = 8.5f;
				const int kArcSeg = 12;
				D2D1_POINT_2F prevPt{};
				brush->SetColor(D2D1::ColorF(0.06f, 0.36f, 0.90f, 0.95f));
				for (int s = 0; s <= kArcSeg; ++s)
				{
					const float a = -2.35f + (1.7f * static_cast<float>(s) / kArcSeg);
					const D2D1_POINT_2F curPt = D2D1::Point2F(pRot.x + rArc * std::cos(a), pRot.y + rArc * std::sin(a));
					if (s > 0) rt->DrawLine(prevPt, curPt, brush, 1.4f);
					prevPt = curPt;
				}
			}
		}

		// 개별 정점(Anchor) 및 베이지어 핸들(Handle) 에디트 포인트 표시
		ge::PathNode* pathShape = dynamic_cast<ge::PathNode*>(shape);
		if (pathShape)
		{
			for (const auto& sp : pathShape->GetPaths())
			{
				for (const auto& node : sp.nodes)
				{
					D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, pathShape->GetWorldTransform(), view);

					if (node.HasIn())
					{
						D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), pathShape->GetWorldTransform(), view);
						brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
						rt->DrawLine(aScreen, inScreen, brush, 1.0f);
						brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
						rt->FillEllipse(D2D1::Ellipse(inScreen, 3.0f, 3.0f), brush);
					}

					if (node.HasOut())
					{
						D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), pathShape->GetWorldTransform(), view);
						brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
						rt->DrawLine(aScreen, outScreen, brush, 1.0f);
						brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
						rt->FillEllipse(D2D1::Ellipse(outScreen, 3.0f, 3.0f), brush);
					}

					D2D1_RECT_F ar = D2D1::RectF(aScreen.x - 3.5f, aScreen.y - 3.5f, aScreen.x + 3.5f, aScreen.y + 3.5f);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
					rt->FillRectangle(ar, brush);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
					rt->DrawRectangle(ar, brush, 1.5f);
				}
			}
		}
	}

	// 다중 선택 결합 기즈모 (5.2): 경계 상자 + 8핸들 + 회전 핸들 + 크기 알약
	if (sel.size() >= 2)
	{
		D2D1_POINT_2F hp[9];
		ge::Rect2 rc;
		if (MultiHandlePositions(sel, view, hp, rc))
		{
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.95f));
			rt->DrawLine(hp[0], hp[2], brush, 1.2f);
			rt->DrawLine(hp[2], hp[4], brush, 1.2f);
			rt->DrawLine(hp[4], hp[6], brush, 1.2f);
			rt->DrawLine(hp[6], hp[0], brush, 1.2f);
			rt->DrawLine(hp[1], hp[8], brush, 1.0f);
			const bool rotActive = (m_mode == Mode::MultiRotate);
			brush->SetColor(rotActive ? D2D1::ColorF(0.12f, 0.55f, 0.95f, 1.0f) : D2D1::ColorF(D2D1::ColorF::White));
			rt->FillEllipse(D2D1::Ellipse(hp[8], 4.5f, 4.5f), brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawEllipse(D2D1::Ellipse(hp[8], 4.5f, 4.5f), brush, 1.5f);
			for (int i = 0; i < 8; ++i)
			{
				const bool active = (m_handle == static_cast<Handle>(i + 1)) && (m_mode == Mode::MultiScale || m_mode == Mode::MultiSkew);
				const D2D1_RECT_F r = D2D1::RectF(hp[i].x - 3.5f, hp[i].y - 3.5f, hp[i].x + 3.5f, hp[i].y + 3.5f);
				brush->SetColor(active ? D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f) : D2D1::ColorF(D2D1::ColorF::White));
				rt->FillRectangle(r, brush);
				brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
				rt->DrawRectangle(r, brush, 1.2f);
			}
			// 크기 알약
			static IDWriteFactory* s_dwM = nullptr;
			static IDWriteTextFormat* s_fmtM = nullptr;
			if (!s_dwM)
			{
				DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s_dwM));
				if (s_dwM)
				{
					s_dwM->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"ko-kr", &s_fmtM);
					if (s_fmtM)
					{
						s_fmtM->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
						s_fmtM->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
						s_fmtM->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
					}
				}
			}
			if (s_fmtM)
			{
				wchar_t buf[48];
				swprintf_s(buf, L"%d \u00D7 %d  (%d)", static_cast<int>(std::round(rc.right - rc.left)), static_cast<int>(std::round(rc.bottom - rc.top)), static_cast<int>(sel.size()));
				const float tw = static_cast<float>(wcslen(buf)) * 6.8f + 14.0f;
				const D2D1_RECT_F pill = D2D1::RectF(hp[5].x - tw * 0.5f, hp[5].y + 10.0f, hp[5].x + tw * 0.5f, hp[5].y + 28.0f);
				brush->SetColor(D2D1::ColorF(0.10f, 0.55f, 0.98f, 1.0f));
				rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 3.0f, 3.0f), brush);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->DrawText(buf, static_cast<UINT32>(wcslen(buf)), s_fmtM, pill, brush);
			}
		}
	}

	// 스마트 가이드: 붉은 선과 x 표시 (Figma 방식)
	DrawGuideLines(rt, brush, m_guides, zoom, pan);
	DrawGapMarks(rt, brush, m_gaps, zoom, pan);

	// 선택된 요소 아래 크기 알약 "W × H"
	if (sel.size() == 1 && m_mode != Mode::Marquee && sel[0] && sel[0]->IsEffectivelyVisible())
	{
		ge::Rect2 wb;
		if (ge::GuideWorldBounds(*sel[0], wb))
		{
			static IDWriteFactory* s_dw = nullptr;
			static IDWriteTextFormat* s_fmt = nullptr;
			if (!s_dw)
			{
				DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&s_dw));
				if (s_dw)
				{
					s_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
						DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"ko-kr", &s_fmt);
					if (s_fmt)
					{
						s_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
						s_fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
						s_fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
					}
				}
			}
			if (s_fmt)
			{
				wchar_t buf[64];
				if (m_mode == Mode::Rotating)
				{
					const ge::Matrix2D wxf = sel[0]->GetWorldTransform();
					const float deg = std::atan2(wxf.m12, wxf.m11) * (180.0f / 3.14159265358979323846f);
					swprintf_s(buf, L"%.1f\u00B0", deg);
				}
				else if (m_mode == Mode::Skewing)
				{
					const ge::Matrix2D& lxf = sel[0]->GetTransform();
					const float skDeg = std::atan2((m_handle == Handle::T || m_handle == Handle::B) ? lxf.m21 : lxf.m12, 1.0f) * (180.0f / 3.14159265358979323846f);
					swprintf_s(buf, L"Skew %.1f\u00B0", skDeg);
				}
				else
				{
					swprintf_s(buf, L"%d \u00D7 %d", static_cast<int>(std::round(wb.Width())), static_cast<int>(std::round(wb.Height())));
				}
				const D2D1_POINT_2F p = view.WorldToScreen(D2D1::Point2F((wb.left + wb.right) * 0.5f, wb.bottom));
				const float tw = static_cast<float>(wcslen(buf)) * 6.8f + 14.0f;
				const D2D1_RECT_F pill = D2D1::RectF(p.x - tw * 0.5f, p.y + 10.0f, p.x + tw * 0.5f, p.y + 28.0f);
				brush->SetColor(D2D1::ColorF(0.10f, 0.55f, 0.98f, 1.0f));
				rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 3.0f, 3.0f), brush);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->DrawText(buf, static_cast<UINT32>(wcslen(buf)), s_fmt, pill, brush);
			}
		}
	}

	// 그라디언트 기즈모: 단일 선택된 그라디언트 도형/프레임의 시작/끝(방사형: 중심/초점/반지름) 핸들과 컬러 스톱 위치
	if (sel.size() == 1 && SupportsGradientFill(sel[0]))
	{
		DrawGradientGizmo(rt, brush, view, f, sel[0], static_cast<int>(m_gradHandle), -1);
	}

	f->Release();
}

// ============================================================================
// BaseShapeTool 구현
// ============================================================================

void BaseShapeTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_previewShape)
	{
		UpdatePreviewGeometry(e.worldPt);
	}
}

void BaseShapeTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                float zoom, D2D1_POINT_2F pan, CM2DrawDoc* /*pDoc*/)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, { pan.x, pan.y } };

	// 1. 도형이 생성되어 드래그 중인 경우: 엔진의 Shape 렌더링 파이프라인 활용
	if (m_previewShape)
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);

		ge::D2DRenderer renderer;
		if (renderer.Initialize(rt, pFactory))
		{
			renderer.DrawNode(*m_previewShape, ge::Matrix2D::Identity(), view);
		}

		if (pFactory)
		{
			pFactory->Release();
		}
	}
	// 2. 첫 클릭 전 마우스 호버 상태인 경우: 십자선 & 앵커 마커 안내 표시
	else if (m_hasHover)
	{
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
		DrawCrosshairAndAnchor(rt, brush, sp);
	}
}

bool BaseShapeTool::CommitShape(CM2DrawDoc* pDoc)
{
	if (!pDoc || !m_previewShape) return false;

	// 미리보기 도형(ge::PathNode)을 그대로 문서 활성 레이어에 등록 (단일 모델)
	m_previewShape->SetStyle(m_setting.ToGeStyle());
	const std::wstring name = m_previewShape->GetName();
	ge::Node* pAdded = pDoc->GetDoc().AddShapeAuto(std::move(m_previewShape));   // 프레임 안에서 그렸으면 그 프레임의 자식
	m_previewShape = nullptr;
	if (!pAdded) return false;

	auto* pLayer = pDoc->GetDoc().GetActiveLayer();
	m_lastCommittedId = pAdded->GetId();

	CString strEngineLog;
	strEngineLog.Format(_T("[도형 생성] %s (NodeId: %llu, 활성레이어: %s)"),
		name.c_str(), pAdded->GetId(),
		pLayer ? pLayer->GetName().c_str() : L"None");
	LogAppEvent(strEngineLog);
	SetAppStatusText(strEngineLog);

	pDoc->SetModifiedFlag(TRUE);

	// 방금 등록된 도형 선택 + 정보 표시 (속성창 + 상단 옵션바 수정 모드)
	SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
	if (pSelect)
	{
		pSelect->SelectNode(pAdded, pDoc);
	}

	pDoc->UpdateAllViews(nullptr);
	return true;
}
void BaseShapeTool::CancelShape()
{
	m_previewShape = nullptr;
}

bool BaseShapeTool::OnDoubleClicked(CM2DrawDoc* pDoc)
{
	CommitShape(pDoc);
	return true; // "도형 완료 후 Select 툴로 전환" 신호
}

bool BaseShapeTool::OnRightClicked(CM2DrawDoc* pDoc)
{
	CommitShape(pDoc);
	return true; // "도형 완료 후 Select 툴로 전환" 신호
}

void BaseShapeTool::DrawCrosshairAndAnchor(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                           D2D1_POINT_2F screenPt)
{
	const float cross = 9.0f;
	brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
	rt->DrawLine(D2D1::Point2F(screenPt.x - cross, screenPt.y), D2D1::Point2F(screenPt.x + cross, screenPt.y), brush, 1.0f);
	rt->DrawLine(D2D1::Point2F(screenPt.x, screenPt.y - cross), D2D1::Point2F(screenPt.x, screenPt.y + cross), brush, 1.0f);

	D2D1_RECT_F hr = D2D1::RectF(screenPt.x - 3.5f, screenPt.y - 3.5f, screenPt.x + 3.5f, screenPt.y + 3.5f);
	brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
	rt->FillRectangle(hr, brush);
	brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
	rt->DrawRectangle(hr, brush, 1.5f);
}

void BaseShapeTool::DrawCommittedShapeAnchors(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                             const ViewportTransform& view, CM2DrawDoc* pDoc)
{
	if (!rt || !brush || !pDoc) return;
	ge::PathNode* ps = m_lastCommittedId ? dynamic_cast<ge::PathNode*>(pDoc->GetDoc().FindById(m_lastCommittedId)) : nullptr;
	if (!ps) return;

	for (const auto& sp : ps->GetPaths())
	{
		for (const auto& node : sp.nodes)
		{
			D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, ps->GetWorldTransform(), view);

			if (node.HasIn())
			{
				D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), ps->GetWorldTransform(), view);
				brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.6f));
				rt->DrawLine(aScreen, inScreen, brush, 1.0f);
				brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.8f));
				rt->FillEllipse(D2D1::Ellipse(inScreen, 2.5f, 2.5f), brush);
			}

			if (node.HasOut())
			{
				D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), ps->GetWorldTransform(), view);
				brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.6f));
				rt->DrawLine(aScreen, outScreen, brush, 1.0f);
				brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.8f));
				rt->FillEllipse(D2D1::Ellipse(outScreen, 2.5f, 2.5f), brush);
			}

			// 활성 작업 도형 앵커 마커 (화이트 사각 박스 + 딥블루 테두리)
			D2D1_RECT_F ar = D2D1::RectF(aScreen.x - 3.5f, aScreen.y - 3.5f, aScreen.x + 3.5f, aScreen.y + 3.5f);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillRectangle(ar, brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawRectangle(ar, brush, 1.4f);
		}
	}
}

// ============================================================================
// BoxShapeTool 구현 (단일 클래스로 사각/원/직선 등 모든 2포인트 도형 처리)
// ============================================================================

void BoxShapeTool::CalculateExtents(D2D1_POINT_2F curPt, bool shiftDown, bool altDown, D2D1_POINT_2F& outP0, D2D1_POINT_2F& outP1) const
{
	bool fromCenter = m_setting.bDrawFromCenter || altDown;

	if (m_kind == ShapeKind::Line)
	{
		if (fromCenter)
		{
			float dx = curPt.x - m_startPt.x;
			float dy = curPt.y - m_startPt.y;
			if (shiftDown)
			{
				float len = std::sqrt(dx * dx + dy * dy);
				float angle = std::atan2(dy, dx);
				const float snap = 3.14159265f / 4.0f;
				angle = std::round(angle / snap) * snap;
				dx = len * std::cos(angle);
				dy = len * std::sin(angle);
			}
			outP0 = D2D1::Point2F(m_startPt.x - dx, m_startPt.y - dy);
			outP1 = D2D1::Point2F(m_startPt.x + dx, m_startPt.y + dy);
		}
		else
		{
			outP0 = m_startPt;
			float dx = curPt.x - m_startPt.x;
			float dy = curPt.y - m_startPt.y;
			if (shiftDown)
			{
				float len = std::sqrt(dx * dx + dy * dy);
				float angle = std::atan2(dy, dx);
				const float snap = 3.14159265f / 4.0f;
				angle = std::round(angle / snap) * snap;
				dx = len * std::cos(angle);
				dy = len * std::sin(angle);
			}
			outP1 = D2D1::Point2F(m_startPt.x + dx, m_startPt.y + dy);
		}
		return;
	}

	float dx = std::abs(curPt.x - m_startPt.x);
	float dy = std::abs(curPt.y - m_startPt.y);

	bool forceSquare = shiftDown || 
	                   (m_kind == ShapeKind::RegularPoly ||
	                    m_kind == ShapeKind::Star ||
	                    m_kind == ShapeKind::Gear ||
	                    m_kind == ShapeKind::Spiral);

	if (forceSquare)
	{
		float maxD = (std::max)(dx, dy);
		dx = maxD;
		dy = maxD;
	}

	if (fromCenter)
	{
		outP0 = D2D1::Point2F(m_startPt.x - dx, m_startPt.y - dy);
		outP1 = D2D1::Point2F(m_startPt.x + dx, m_startPt.y + dy);
	}
	else
	{
		float signX = (curPt.x >= m_startPt.x) ? 1.0f : -1.0f;
		float signY = (curPt.y >= m_startPt.y) ? 1.0f : -1.0f;
		outP0 = m_startPt;
		outP1 = D2D1::Point2F(m_startPt.x + signX * dx, m_startPt.y + signY * dy);
	}
}

void BoxShapeTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	// 2. 이미 1번째 클릭을 마친 상태(클릭-이동-클릭 모드)인 경우:
	// 이번 2번째 클릭으로 도형 확정 완료!
	if (m_waitingSecondClick)
	{
		float dx = e.worldPt.x - m_startPt.x;
		float dy = e.worldPt.y - m_startPt.y;

		if ((dx * dx + dy * dy) > 16.0f)
		{
			if (m_previewShape)
			{
				D2D1_POINT_2F p0, p1;
				CalculateExtents(e.worldPt, e.shiftDown, e.altDown, p0, p1);
				UpdateGeometry(m_previewShape.get(), p0, p1);
			}
			CommitShape(e.pDoc);
		}
		else
		{
			CancelShape();
		}

		m_waitingSecondClick = false;
		m_isDragging = false;
		return;
	}

	// 1. 첫 번째 클릭(다운): 시작점 기록 및 미리보기 생성
	m_startPt = e.worldPt;
	m_isDragging = true;
	m_waitingSecondClick = false;

	// 상단 옵션바를 "만들기" 모드로 설정하고 시작점 고정
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetOptionBar().SetMode(m_toolType, false);
	}
	UpdateOptionBarCreationCoords(m_startPt.x, m_startPt.y, 0.0f, 0.0f, true);

	// 엔진 팩토리를 통해 도형 인스턴스 생성
	m_previewShape = CreateShapeInstance(m_startPt, m_startPt, e.pDoc);
	if (m_previewShape)
	{
		m_previewShape->SetStyle(m_setting.ToGeStyle());
		m_previewShape->Invalidate();
	}

}

void BoxShapeTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_isDragging || m_waitingSecondClick)
	{
		D2D1_POINT_2F p0, p1;
		CalculateExtents(e.worldPt, e.shiftDown, e.altDown, p0, p1);

		if (m_previewShape)
		{
			UpdateGeometry(m_previewShape.get(), p0, p1);
		}

		float left = (std::min)(p0.x, p1.x);
		float top = (std::min)(p0.y, p1.y);
		float w = std::abs(p1.x - p0.x);
		float h = std::abs(p1.y - p0.y);

		UpdateOptionBarCreationCoords(left, top, w, h, true);
	}
	else
	{
		// 첫 포인트 찍기 전: 마우스 위치를 X, Y에 실시간 표시 (하단 좌표와 동일)
		UpdateOptionBarCreationCoords(e.worldPt.x, e.worldPt.y, 0.0f, 0.0f, false);
	}
}

void BoxShapeTool::OnUp(const ToolEvent& e)
{
	if (!m_isDragging) return;
	m_isDragging = false;

	float dx = e.worldPt.x - m_startPt.x;
	float dy = e.worldPt.y - m_startPt.y;

	// 1. 드래그 방식: 마우스를 누른 채 충분히 이동(4px 초과)하여 뗐다면 즉시 도형 완성!
	if ((dx * dx + dy * dy) > 16.0f)
	{
		if (m_previewShape)
		{
			D2D1_POINT_2F p0, p1;
			CalculateExtents(e.worldPt, e.shiftDown, e.altDown, p0, p1);
			UpdateGeometry(m_previewShape.get(), p0, p1);
		}

		CommitShape(e.pDoc);
		m_waitingSecondClick = false;
		// 도구 유지: 연속 그리기 보장
	}
	// 2. 클릭-이동-클릭 방식: 제자리에서 다운 후 업(단순 클릭)한 경우
	// 도형을 취소하지 않고, 마우스 이동에 따라 미리보기를 보여주며 "2번째 클릭"을 대기함!
	else
	{
		m_waitingSecondClick = true;
		UpdateOptionBarCreationCoords(m_startPt.x, m_startPt.y, 0.0f, 0.0f, true);
	}
}

void BoxShapeTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (nChar == VK_ESCAPE)
	{
		CancelShape();
		m_isDragging = false;
		m_waitingSecondClick = false;
	}
}

void BoxShapeTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                               float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, { pan.x, pan.y } };

	// 1. 첫 번째 포인트가 찍혀서 미리보기가 생성된 상태 (드래그 중이거나 클릭-이동 대기 중)
	if (m_previewShape && (m_isDragging || m_waitingSecondClick))
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);

		if (m_previewShape)
		{
			// (1) 도형 본체 미리보기 렌더링 (엔진 기하학)
			ge::D2DRenderer renderer;
			if (renderer.Initialize(rt, pFactory))
			{
				renderer.DrawNode(*m_previewShape, ge::Matrix2D::Identity(), view);
			}
		}

		if (pFactory) pFactory->Release();

		// (2) 첫 번째 앵커 포인트 고정 표시 (주황/빨강 강조 마커)
		D2D1_POINT_2F p0Screen = view.WorldToScreen(m_startPt);
		const float sz0 = 4.5f;
		D2D1_RECT_F hr0 = D2D1::RectF(p0Screen.x - sz0, p0Screen.y - sz0, p0Screen.x + sz0, p0Screen.y + sz0);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr0, brush);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::OrangeRed));
		rt->DrawRectangle(hr0, brush, 2.0f);

		// 중심 그리기 모드일 때: 중심점(m_startPt)에 십자선 가이드 표시
		if (m_setting.bDrawFromCenter)
		{
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::OrangeRed, 0.85f));
			rt->DrawLine(D2D1::Point2F(p0Screen.x - 7.0f, p0Screen.y), D2D1::Point2F(p0Screen.x + 7.0f, p0Screen.y), brush, 1.2f);
			rt->DrawLine(D2D1::Point2F(p0Screen.x, p0Screen.y - 7.0f), D2D1::Point2F(p0Screen.x, p0Screen.y + 7.0f), brush, 1.2f);
		}

		// (3) 현재 마우스 위치(두 번째 앵커 포인트 미리보기)
		if (m_hasHover)
		{
			D2D1_POINT_2F p1Screen = view.WorldToScreen(m_hoverPt);
			const float sz1 = 3.5f;
			D2D1_RECT_F hr1 = D2D1::RectF(p1Screen.x - sz1, p1Screen.y - sz1, p1Screen.x + sz1, p1Screen.y + sz1);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillRectangle(hr1, brush);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
			rt->DrawRectangle(hr1, brush, 1.5f);

			// 직선 도구인 경우 시작점과 끝점을 잇는 대각 가이드선 강조
			if (m_kind == ShapeKind::Line)
			{
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
				rt->DrawLine(p0Screen, p1Screen, brush, 1.0f);
			}
		}
	}
	// 2. 첫 클릭 전 대기 상태 (동일 도형 연속 그리기 준비 상태)
	else
	{
		// 방금 완성된 직전 작업 도형의 앵커/핸들 표시
		DrawCommittedShapeAnchors(rt, brush, view, pDoc);

		// 마우스 위치에 십자선 & 앵커 마커 안내
		if (m_hasHover)
		{
			D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
			DrawCrosshairAndAnchor(rt, brush, sp);
		}
	}
}

void BoxShapeTool::UpdatePreviewGeometry(D2D1_POINT_2F curPt)
{
	if (m_previewShape)
	{
		D2D1_POINT_2F p0, p1;
		CalculateExtents(curPt, false, false, p0, p1);
		UpdateGeometry(m_previewShape.get(), p0, p1);
	}
}

std::unique_ptr<ge::PathNode> BoxShapeTool::CreateShapeInstance(D2D1_POINT_2F p0, D2D1_POINT_2F p1, CM2DrawDoc* pDoc)
{
	auto shape = std::make_unique<ge::PathNode>();

	// Inkscape 스타일 자동 명명 (rect1, ellipse1, star1 등)
	auto getAutoName = [&](ge::ShapeKind geKind, const wchar_t* fallback) -> std::wstring {
		if (pDoc) {
			return pDoc->GetDoc().GenerateNodeName(geKind);
		}
		return fallback;
	};

	if (m_kind == ShapeKind::Line)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Path, _T("line1")));
		shape->AddAnchor(p0);
		shape->AddAnchor(p1);
	}
	else if (m_kind == ShapeKind::Rect)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Rect, _T("rect1")));
		shape->SetKind(::ShapeKind::Rect);
		RectParams rp;
		rp.rx = m_setting.cornerRx;
		rp.ry = m_setting.cornerRy;
		rp.linkRxRy = m_setting.bLinkRxRy;
		shape->SetParams(rp);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->GetPathsRef().back().closed = true;
	}
	else if (m_kind == ShapeKind::Ellipse)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Ellipse, _T("ellipse1")));
		shape->SetKind(::ShapeKind::Ellipse);
		EllipseParams ep;
		ep.linkRxRy = m_setting.bLinkRxRy;
		shape->SetParams(ep);
		SubPath sp;
		for (int i = 0; i < 4; ++i)
		{
			BezierNode n;
			n.type = BezierNode::Type::Smooth;
			sp.nodes.push_back(n);
		}
		sp.closed = true;
		shape->GetPathsRef().push_back(sp);
	}
	else if (m_kind == ShapeKind::Triangle)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Triangle, _T("triangle1")));
		shape->SetKind(::ShapeKind::Triangle);
		TriangleParams tp;
		tp.equilateral = false;
		shape->SetParams(tp);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->GetPathsRef().back().closed = true;
	}
	else if (m_kind == ShapeKind::Star)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Star, _T("star1")));
		shape->SetKind(::ShapeKind::Star);
		StarParams sp;
		sp.points = (std::max)(3, (std::min)(32, m_setting.starPoints));
		sp.innerRatio = (std::max)(0.05f, (std::min)(0.95f, m_setting.starInnerRatio));
		shape->SetParams(sp);
		int n = sp.points;
		for (int i = 0; i < n * 2; ++i) shape->AddAnchor(p0);
		shape->GetPathsRef().back().closed = true;
	}
	else if (m_kind == ShapeKind::RegularPoly)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Polygon, _T("polygon1")));
		shape->SetKind(::ShapeKind::Polygon);
		PolygonParams pp;
		pp.sides = (std::max)(3, (std::min)(32, m_setting.polySides));
		shape->SetParams(pp);
		int n = pp.sides;
		for (int i = 0; i < n; ++i) shape->AddAnchor(p0);
		shape->GetPathsRef().back().closed = true;
	}
	else if (m_kind == ShapeKind::Gear)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Gear, _T("gear1")));
		shape->SetKind(::ShapeKind::Gear);
		GearParams gp;
		gp.teeth = (std::max)(4, (std::min)(48, m_setting.gearTeeth));
		gp.depthRatio = (std::max)(0.05f, (std::min)(0.50f, m_setting.gearDepthRatio));
		gp.boreRatio = (std::max)(0.05f, (std::min)(0.80f, m_setting.gearHoleRatio));
		shape->SetParams(gp);
		int n = gp.teeth;
		for (int i = 0; i < n * 4; ++i) shape->AddAnchor(p0);
		shape->GetPathsRef().back().closed = true;

		// 2번째 서브패스: 축 구멍 4개 노드
		shape->GetPathsRef().emplace_back();
		for (int i = 0; i < 4; ++i) {
			BezierNode bn; bn.type = BezierNode::Type::Smooth;
			shape->GetPathsRef().back().nodes.push_back(bn);
		}
		shape->GetPathsRef().back().closed = true;
	}
	else if (m_kind == ShapeKind::Spiral)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Spiral, _T("spiral1")));
		shape->SetKind(::ShapeKind::Spiral);
		SpiralParams sp;
		sp.turns = (std::max)(0.5f, (std::min)(10.0f, m_setting.spiralTurns));
		sp.innerRatio = 0.1f;
		shape->SetParams(sp);
		int segs = static_cast<int>(sp.turns * 4.0f);
		if (segs < 4) segs = 4;
		SubPath sub;
		for (int i = 0; i <= segs; ++i) {
			BezierNode n; n.type = BezierNode::Type::Smooth;
			sub.nodes.push_back(n);
		}
		sub.closed = false;
		shape->GetPathsRef().push_back(sub);
	}
	else if (m_kind == ShapeKind::Heart)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Heart, _T("heart1")));
		shape->SetKind(::ShapeKind::Heart);
		HeartParams hp;
		shape->SetParams(hp);
		SubPath sp;
		for (int i = 0; i < 4; ++i) {
			BezierNode n; n.type = BezierNode::Type::Smooth;
			sp.nodes.push_back(n);
		}
		sp.closed = true;
		shape->GetPathsRef().push_back(sp);
	}
	else if (m_kind == ShapeKind::Arc)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Arc, _T("arc1")));
		shape->SetKind(ge::ShapeKind::Arc);
		ge::ArcParams ap;
		ap.mode = m_setting.arcMode;
		ap.startAngle = m_setting.arcStartDeg;
		ap.sweepAngle = m_setting.arcSweepDeg;
		ap.innerRatio = m_setting.arcInnerRatio;
		shape->SetParams(ap);
		ge::PathNode::BuildArcSubPaths(ge::Rect2{ p0.x, p0.y, p1.x, p1.y }, ap, shape->GetPathsRef());
	}
	else if (m_kind == ShapeKind::BlockArrow)
	{
		shape->SetName(getAutoName(ge::ShapeKind::BlockArrow, _T("arrow1")));
		shape->SetKind(ge::ShapeKind::BlockArrow);
		ge::BlockArrowParams bp;
		shape->SetParams(bp);
		ge::PathNode::BuildBlockArrowSubPaths(ge::Rect2{ p0.x, p0.y, p1.x, p1.y }, bp, shape->GetPathsRef());
	}
	else if (m_kind == ShapeKind::Callout)
	{
		shape->SetName(getAutoName(ge::ShapeKind::Callout, _T("callout1")));
		shape->SetKind(ge::ShapeKind::Callout);
		ge::CalloutParams cp;
		shape->SetParams(cp);
		ge::PathNode::BuildCalloutSubPaths(ge::Rect2{ p0.x, p0.y, p1.x, p1.y }, cp, shape->GetPathsRef());
	}

	UpdateGeometry(shape.get(), p0, p1);
	return shape;
}

void BoxShapeTool::UpdateGeometry(ge::PathNode* pShape, D2D1_POINT_2F p0, D2D1_POINT_2F p1)
{
	if (!pShape || pShape->GetPaths().empty()) return;

	float left = min(p0.x, p1.x);
	float top = min(p0.y, p1.y);
	float right = max(p0.x, p1.x);
	float bottom = max(p0.y, p1.y);
	float w = max(1.0f, right - left);
	float h = max(1.0f, bottom - top);
	float cx = left + w * 0.5f;
	float cy = top + h * 0.5f;
	float rx = w * 0.5f;
	float ry = h * 0.5f;

	if (m_kind == ShapeKind::Line)
	{
		if (pShape->GetPathsRef()[0].nodes.size() < 2) return;
		pShape->GetPathsRef()[0].nodes[0].anchor = p0;
		pShape->GetPathsRef()[0].nodes[1].anchor = p1;
	}
	else if (m_kind == ShapeKind::Rect)
	{
		float crx = (std::min)(rx, (std::max)(0.0f, m_setting.cornerRx));
		float cry = (std::min)(ry, (std::max)(0.0f, m_setting.cornerRy));

		if (crx > 0.001f || cry > 0.001f)
		{
			// 둥근 사각형 (8개 앵커 + 베이지어 4분원)
			auto& nodes = pShape->GetPathsRef()[0].nodes;
			if (nodes.size() < 8) {
				nodes.clear();
				for (int i = 0; i < 8; ++i) {
					BezierNode bn; bn.type = BezierNode::Type::Corner;
					nodes.push_back(bn);
				}
			}
			const float kx = crx * 0.55228475f;
			const float ky = cry * 0.55228475f;

			// Top-Left to Top-Right
			nodes[0].anchor = D2D1::Point2F(left + crx, top);
			nodes[0].hIn = D2D1::Point2F(-kx, 0.0f); nodes[0].hOut = D2D1::Point2F(0, 0);

			nodes[1].anchor = D2D1::Point2F(right - crx, top);
			nodes[1].hIn = D2D1::Point2F(0, 0); nodes[1].hOut = D2D1::Point2F(kx, 0.0f);

			// Right edge
			nodes[2].anchor = D2D1::Point2F(right, top + cry);
			nodes[2].hIn = D2D1::Point2F(0.0f, -ky); nodes[2].hOut = D2D1::Point2F(0, 0);

			nodes[3].anchor = D2D1::Point2F(right, bottom - cry);
			nodes[3].hIn = D2D1::Point2F(0, 0); nodes[3].hOut = D2D1::Point2F(0.0f, ky);

			// Bottom edge
			nodes[4].anchor = D2D1::Point2F(right - crx, bottom);
			nodes[4].hIn = D2D1::Point2F(kx, 0.0f); nodes[4].hOut = D2D1::Point2F(0, 0);

			nodes[5].anchor = D2D1::Point2F(left + crx, bottom);
			nodes[5].hIn = D2D1::Point2F(0, 0); nodes[5].hOut = D2D1::Point2F(-kx, 0.0f);

			// Left edge
			nodes[6].anchor = D2D1::Point2F(left, bottom - cry);
			nodes[6].hIn = D2D1::Point2F(0.0f, ky); nodes[6].hOut = D2D1::Point2F(0, 0);

			nodes[7].anchor = D2D1::Point2F(left, top + cry);
			nodes[7].hIn = D2D1::Point2F(0, 0); nodes[7].hOut = D2D1::Point2F(0.0f, -ky);
		}
		else
		{
			// 일반 직각 사각형 (4개 정점)
			auto& nodes = pShape->GetPathsRef()[0].nodes;
			if (nodes.size() != 4) {
				nodes.clear();
				for (int i = 0; i < 4; ++i) nodes.emplace_back();
			}
			nodes[0].anchor = D2D1::Point2F(left, top);
			nodes[0].hIn = nodes[0].hOut = { 0, 0 };
			nodes[1].anchor = D2D1::Point2F(right, top);
			nodes[1].hIn = nodes[1].hOut = { 0, 0 };
			nodes[2].anchor = D2D1::Point2F(right, bottom);
			nodes[2].hIn = nodes[2].hOut = { 0, 0 };
			nodes[3].anchor = D2D1::Point2F(left, bottom);
			nodes[3].hIn = nodes[3].hOut = { 0, 0 };
		}
	}
	else if (m_kind == ShapeKind::Ellipse)
	{
		if (pShape->GetPathsRef()[0].nodes.size() < 4) return;
		const float kappa = 0.55228475f;
		float kx = rx * kappa;
		float ky = ry * kappa;

		auto& nodes = pShape->GetPathsRef()[0].nodes;
		nodes[0].anchor = D2D1::Point2F(cx, cy - ry);
		nodes[0].hIn = D2D1::Point2F(-kx, 0.0f); nodes[0].hOut = D2D1::Point2F(kx, 0.0f);

		nodes[1].anchor = D2D1::Point2F(cx + rx, cy);
		nodes[1].hIn = D2D1::Point2F(0.0f, -ky); nodes[1].hOut = D2D1::Point2F(0.0f, ky);

		nodes[2].anchor = D2D1::Point2F(cx, cy + ry);
		nodes[2].hIn = D2D1::Point2F(kx, 0.0f); nodes[2].hOut = D2D1::Point2F(-kx, 0.0f);

		nodes[3].anchor = D2D1::Point2F(cx - rx, cy);
		nodes[3].hIn = D2D1::Point2F(0.0f, ky); nodes[3].hOut = D2D1::Point2F(0.0f, -ky);
	}
	else if (m_kind == ShapeKind::Triangle)
	{
		// 이등변 삼각형: (상단 중앙, 우하단, 좌하단)
		auto& nodes = pShape->GetPathsRef()[0].nodes;
		if (nodes.size() < 3) return;
		nodes[0].anchor = D2D1::Point2F(cx, top);
		nodes[0].hIn = nodes[0].hOut = { 0, 0 };
		nodes[1].anchor = D2D1::Point2F(right, bottom);
		nodes[1].hIn = nodes[1].hOut = { 0, 0 };
		nodes[2].anchor = D2D1::Point2F(left, bottom);
		nodes[2].hIn = nodes[2].hOut = { 0, 0 };
	}
	else if (m_kind == ShapeKind::Star)
	{
		// 다각 별: 외경(R), 내경(r) 삼각함수 교차점
		StarParams* sp = pShape->GetParamRef<StarParams>();
		int n = sp ? sp->points : (std::max)(3, (std::min)(32, m_setting.starPoints));
		if (sp) { sp->radius = rx; sp->cx = cx; sp->cy = cy; }
		auto& nodes = pShape->GetPathsRef()[0].nodes;
		if (static_cast<int>(nodes.size()) != n * 2) {
			nodes.clear();
			for (int i = 0; i < n * 2; ++i) nodes.emplace_back();
		}
		const float PI = 3.14159265358979323846f;
		float innerRatio = sp ? sp->innerRatio : (std::max)(0.05f, (std::min)(0.95f, m_setting.starInnerRatio));
		for (int i = 0; i < n * 2; ++i) {
			float angle = -PI * 0.5f + (i * PI / n);
			float curR = (i % 2 == 0) ? 1.0f : innerRatio;
			nodes[i].anchor = D2D1::Point2F(cx + rx * curR * std::cos(angle), cy + ry * curR * std::sin(angle));
			nodes[i].hIn = nodes[i].hOut = { 0, 0 };
		}
	}
	else if (m_kind == ShapeKind::RegularPoly)
	{
		// 정다각형: 원주 상의 N개 정점
		PolygonParams* pp = pShape->GetParamRef<PolygonParams>();
		int n = pp ? pp->sides : (std::max)(3, (std::min)(32, m_setting.polySides));
		if (pp) { pp->radius = rx; pp->cx = cx; pp->cy = cy; }
		auto& nodes = pShape->GetPathsRef()[0].nodes;
		if (static_cast<int>(nodes.size()) != n) {
			nodes.clear();
			for (int i = 0; i < n; ++i) nodes.emplace_back();
		}
		const float PI = 3.14159265358979323846f;
		for (int i = 0; i < n; ++i) {
			float angle = -PI * 0.5f + (i * 2.0f * PI / n);
			nodes[i].anchor = D2D1::Point2F(cx + rx * std::cos(angle), cy + ry * std::sin(angle));
			nodes[i].hIn = nodes[i].hOut = { 0, 0 };
		}
	}
	else if (m_kind == ShapeKind::Gear)
	{
		// 기어: N개 톱니 (외경 톱니 상단 2점, 내경 홈 2점) + 중앙 축 구멍(Bore Hole)
		GearParams* gp = pShape->GetParamRef<GearParams>();
		int n = gp ? gp->teeth : (std::max)(4, (std::min)(48, m_setting.gearTeeth));
		if (gp) { gp->radius = rx; gp->cx = cx; gp->cy = cy; }
		if (pShape->GetPaths().empty()) {
			pShape->GetPathsRef().emplace_back();
		}
		auto& outerNodes = pShape->GetPathsRef()[0].nodes;
		if (static_cast<int>(outerNodes.size()) != n * 4) {
			outerNodes.clear();
			for (int i = 0; i < n * 4; ++i) outerNodes.emplace_back();
		}
		pShape->GetPathsRef()[0].closed = true;

		const float PI = 3.14159265358979323846f;
		float depth = gp ? gp->depthRatio : (std::max)(0.05f, (std::min)(0.5f, m_setting.gearDepthRatio));
		float rInner = 1.0f - depth;
		float boreRatio = gp ? gp->boreRatio : (std::max)(0.05f, (std::min)(0.80f, m_setting.gearHoleRatio));
		// 축 구멍 반경은 루트 반경보다 작아야 함 (Clamp)
		boreRatio = (std::min)(boreRatio, rInner - 0.05f);
		if (boreRatio < 0.05f) boreRatio = 0.05f;

		for (int i = 0; i < n * 4; ++i) {
			float angle = -PI * 0.5f + (i * 2.0f * PI / (n * 4));
			int mod = i % 4;
			float curR = (mod == 1 || mod == 2) ? 1.0f : rInner;
			outerNodes[i].anchor = D2D1::Point2F(cx + rx * curR * std::cos(angle), cy + ry * curR * std::sin(angle));
			outerNodes[i].hIn = outerNodes[i].hOut = { 0, 0 };
		}

		// 중앙 축 구멍(Bore Hole) 서브패스: 4-노드 3차 베지어 원 (κ = 0.55228475f)
		if (pShape->GetPaths().size() < 2) {
			pShape->GetPathsRef().emplace_back();
		}
		auto& holeNodes = pShape->GetPathsRef()[1].nodes;
		if (holeNodes.size() != 4) {
			holeNodes.clear();
			for (int i = 0; i < 4; ++i) {
				BezierNode bn;
				bn.type = BezierNode::Type::Smooth;
				holeNodes.push_back(bn);
			}
		}
		pShape->GetPathsRef()[1].closed = true;

		const float KAPPA = 0.5522847498f;
		float hx = rx * boreRatio;
		float hy = ry * boreRatio;
		float kx = hx * KAPPA;
		float ky = hy * KAPPA;

		// 4개 제어점: Top, Right, Bottom, Left
		holeNodes[0].anchor = D2D1::Point2F(cx, cy - hy);
		holeNodes[0].hIn    = D2D1::Point2F(-kx, 0.0f);
		holeNodes[0].hOut   = D2D1::Point2F(kx, 0.0f);

		holeNodes[1].anchor = D2D1::Point2F(cx + hx, cy);
		holeNodes[1].hIn    = D2D1::Point2F(0.0f, -ky);
		holeNodes[1].hOut   = D2D1::Point2F(0.0f, ky);

		holeNodes[2].anchor = D2D1::Point2F(cx, cy + hy);
		holeNodes[2].hIn    = D2D1::Point2F(kx, 0.0f);
		holeNodes[2].hOut   = D2D1::Point2F(-kx, 0.0f);

		holeNodes[3].anchor = D2D1::Point2F(cx - hx, cy);
		holeNodes[3].hIn    = D2D1::Point2F(0.0f, ky);
		holeNodes[3].hOut   = D2D1::Point2F(0.0f, -ky);
	}
	else if (m_kind == ShapeKind::Spiral)
	{
		// 아르키메데스 나선 (Open Path)
		SpiralParams* sp = pShape->GetParamRef<SpiralParams>();
		float turns = sp ? sp->turns : m_setting.spiralTurns;
		if (sp) { sp->radius = rx; sp->cx = cx; sp->cy = cy; }
		int segs = static_cast<int>(turns * 4.0f);
		if (segs < 4) segs = 4;
		auto& nodes = pShape->GetPathsRef()[0].nodes;
		if (static_cast<int>(nodes.size()) != segs + 1) {
			nodes.clear();
			for (int i = 0; i <= segs; ++i) {
				BezierNode bn; bn.type = BezierNode::Type::Smooth;
				nodes.push_back(bn);
			}
		}
		const float PI = 3.14159265358979323846f;
		const float kappa = 0.55228475f;
		for (int i = 0; i <= segs; ++i) {
			float t = static_cast<float>(i) / segs;
			float angle = t * turns * 2.0f * PI;
			float curR = t; // 중심에서 바깥으로 팽창
			nodes[i].anchor = D2D1::Point2F(cx + rx * curR * std::cos(angle), cy + ry * curR * std::sin(angle));
			// 접선 방향 베이지어 핸들
			float tanX = -std::sin(angle) * rx * curR * (PI * 0.5f / segs);
			float tanY = std::cos(angle) * ry * curR * (PI * 0.5f / segs);
			nodes[i].hIn = D2D1::Point2F(-tanX, -tanY);
			nodes[i].hOut = D2D1::Point2F(tanX, tanY);
		}
	}
	else if (m_kind == ShapeKind::Heart)
	{
		// 하트 (상단 골짜기, 우측 볼륨, 하단 뾰족점, 좌측 볼륨)
		auto& nodes = pShape->GetPathsRef()[0].nodes;
		if (nodes.size() < 4) {
			nodes.clear();
			for (int i = 0; i < 4; ++i) {
				BezierNode bn; bn.type = BezierNode::Type::Smooth;
				nodes.push_back(bn);
			}
		}
		// 0: 상단 중심 골짜기 (Corner)
		nodes[0].type = BezierNode::Type::Corner;
		nodes[0].anchor = D2D1::Point2F(cx, top + h * 0.28f);
		nodes[0].hIn = D2D1::Point2F(-rx * 0.5f, -h * 0.28f);
		nodes[0].hOut = D2D1::Point2F(rx * 0.5f, -h * 0.28f);

		// 1: 우측 볼륨
		nodes[1].type = BezierNode::Type::Smooth;
		nodes[1].anchor = D2D1::Point2F(right, top + h * 0.42f);
		nodes[1].hIn = D2D1::Point2F(0, -h * 0.2f);
		nodes[1].hOut = D2D1::Point2F(0, h * 0.25f);

		// 2: 하단 끝점 (Corner)
		nodes[2].type = BezierNode::Type::Corner;
		nodes[2].anchor = D2D1::Point2F(cx, bottom);
		nodes[2].hIn = D2D1::Point2F(rx * 0.4f, -h * 0.25f);
		nodes[2].hOut = D2D1::Point2F(-rx * 0.4f, -h * 0.25f);

		// 3: 좌측 볼륨
		nodes[3].type = BezierNode::Type::Smooth;
		nodes[3].anchor = D2D1::Point2F(left, top + h * 0.42f);
		nodes[3].hIn = D2D1::Point2F(0, h * 0.25f);
		nodes[3].hOut = D2D1::Point2F(0, -h * 0.2f);
	}
	else if (m_kind == ShapeKind::Arc)
	{
		ge::ArcParams* ap = pShape->GetParamRef<ge::ArcParams>();
		ge::ArcParams cur = ap ? *ap : ge::ArcParams{};
		cur.cx = cx; cur.cy = cy; cur.rx = rx; cur.ry = ry;
		if (ap) *ap = cur;
		ge::PathNode::BuildArcSubPaths(ge::Rect2{ left, top, right, bottom }, cur, pShape->GetPathsRef());
	}
	else if (m_kind == ShapeKind::BlockArrow)
	{
		ge::BlockArrowParams* bp = pShape->GetParamRef<ge::BlockArrowParams>();
		ge::BlockArrowParams cur = bp ? *bp : ge::BlockArrowParams{};
		ge::PathNode::BuildBlockArrowSubPaths(ge::Rect2{ left, top, right, bottom }, cur, pShape->GetPathsRef());
	}
	else if (m_kind == ShapeKind::Callout)
	{
		ge::CalloutParams* cp = pShape->GetParamRef<ge::CalloutParams>();
		ge::CalloutParams cur = cp ? *cp : ge::CalloutParams{};
		ge::PathNode::BuildCalloutSubPaths(ge::Rect2{ left, top, right, bottom }, cur, pShape->GetPathsRef());
	}

	pShape->Invalidate();
}

// ============================================================================
// MultiPointShapeTool 구현 (다각형, 열린 폴리라인 등 다중 클릭 생성)
// ============================================================================

void MultiPointShapeTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	if (!m_previewShape)
	{
		m_previewShape = std::make_unique<ge::PathNode>();
		m_previewShape->SetName(m_pName ? m_pName : L"");
		m_previewShape->SetStyle(m_setting.ToGeStyle());
	}

	// 3개 이상 정점이 있을 때 첫 정점(8px 이내) 스냅 클릭 시 폐합 완료
	if (!m_previewShape->GetPaths().empty() && !m_previewShape->GetPathsRef().back().nodes.empty())
	{
		const auto& firstNode = m_previewShape->GetPaths().back().nodes[0];
		D2D1_POINT_2F firstScreen = CoordinateSpace::LocalToScreen(firstNode.anchor, m_previewShape->GetTransform(), e.view);
		float dx = firstScreen.x - static_cast<float>(e.screenPt.x);
		float dy = firstScreen.y - static_cast<float>(e.screenPt.y);

		if (m_previewShape->GetPathsRef().back().nodes.size() >= 3 && (dx * dx + dy * dy) <= 64.0f)
		{
			// 첫 점 스냅 클릭: 폐합 완료하고 문서에 확정 (도구는 유지!)
			m_previewShape->GetPathsRef().back().closed = true;
			CommitShape(e.pDoc);
			LogAppEvent(_T("[다각형 폐합 완료] 첫 정점 스냅"));
			return;
		}
	}

	// 직전에 찍은 정점과 동일하거나 매우 가까우면(더블클릭의 두 번째 클릭 등) 중복 추가 방지
	if (!m_previewShape->GetPaths().empty() && !m_previewShape->GetPathsRef().back().nodes.empty())
	{
		const auto& lastNode = m_previewShape->GetPaths().back().nodes.back();
		float ddx = lastNode.anchor.x - e.worldPt.x;
		float ddy = lastNode.anchor.y - e.worldPt.y;
		if ((ddx * ddx + ddy * ddy) < 1.0f)
		{
			return; // 동일 위치 클릭 중복 추가 무시
		}
	}

	m_previewShape->AddAnchor(e.worldPt);
}

void MultiPointShapeTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (!pDoc || !m_previewShape) return;

	if (nChar == VK_RETURN)
	{
		// Enter 키로 완료 (설정에 따라 열림/닫힘 반영, 도구는 유지)
		if (!m_previewShape->GetPaths().empty() && m_previewShape->GetPathsRef().back().nodes.size() >= 2)
		{
			m_previewShape->GetPathsRef().back().closed = m_setting.bClosed;
			CommitShape(pDoc);
			LogAppEvent(_T("[다각형 생성 완료] (Enter)"));
		}
		else
		{
			CancelShape();
		}
	}
	else if (nChar == VK_ESCAPE)
	{
		CancelShape();
		LogAppEvent(_T("[다각형 작성 취소] (Escape)"));
	}
}

bool MultiPointShapeTool::OnDoubleClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return true;

	if (m_previewShape && !m_previewShape->GetPaths().empty())
	{
		auto& nodes = m_previewShape->GetPathsRef().back().nodes;

		// 1. 더블클릭 시 바로 직전 점과 동일한 좌표가 연속으로 들어갔다면 마지막 중복점 1개 제거
		if (nodes.size() >= 2)
		{
			const auto& last1 = nodes[nodes.size() - 1];
			const auto& last2 = nodes[nodes.size() - 2];
			float dx = last1.anchor.x - last2.anchor.x;
			float dy = last1.anchor.y - last2.anchor.y;
			if ((dx * dx + dy * dy) < 4.0f)
			{
				nodes.pop_back();
			}
		}

		// 2. 최소 2개 이상(폐곡선이면 3개 이상 권장) 점이 있으면 확정 등록
		if (nodes.size() >= 2)
		{
			m_previewShape->GetPathsRef().back().closed = m_setting.bClosed;
			CommitShape(pDoc);
			LogAppEvent(_T("[다각형 생성 완료] (더블클릭 -> 선택 도구 전환)"));
		}
		else
		{
			CancelShape();
		}
	}
	else
	{
		CancelShape();
	}
	return true; // Select 툴로 전환 신호
}

bool MultiPointShapeTool::OnRightClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return true;

	// 우클릭 시: 작성 중인 정점들이 있으면 도형을 확정하고, 없으면 취소 후 선택 도구 전환
	if (m_previewShape && !m_previewShape->GetPaths().empty() && m_previewShape->GetPathsRef().back().nodes.size() >= 2)
	{
		m_previewShape->GetPathsRef().back().closed = m_setting.bClosed;
		CommitShape(pDoc);
		LogAppEvent(_T("[다각형 생성 완료] (우클릭 -> 선택 도구 전환)"));
	}
	else
	{
		CancelShape();
	}
	return true; // Select 툴로 전환 신호
}

void MultiPointShapeTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                      float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, { pan.x, pan.y } };

	if (m_previewShape && !m_previewShape->GetPaths().empty())
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);
		ge::D2DRenderer renderer;
		if (renderer.Initialize(rt, pFactory))
		{
			renderer.DrawNode(*m_previewShape, ge::Matrix2D::Identity(), view);
		}
		if (pFactory) pFactory->Release();

		const auto& nodes = m_previewShape->GetPaths().back().nodes;
		if (!nodes.empty())
		{
			// 1. 확정 정점 선 연결
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
			for (size_t i = 1; i < nodes.size(); ++i)
			{
				D2D1_POINT_2F pA = CoordinateSpace::LocalToScreen(nodes[i - 1].anchor, m_previewShape->GetTransform(), view);
				D2D1_POINT_2F pB = CoordinateSpace::LocalToScreen(nodes[i].anchor, m_previewShape->GetTransform(), view);
				rt->DrawLine(pA, pB, brush, 1.5f);
			}

			// 2. 마지막 점에서 현재 마우스 호버 위치로 실시간 연결선
			if (m_hasHover)
			{
				D2D1_POINT_2F pLast = CoordinateSpace::LocalToScreen(nodes.back().anchor, m_previewShape->GetTransform(), view);
				D2D1_POINT_2F pNext = view.WorldToScreen(m_hoverPt);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
				rt->DrawLine(pLast, pNext, brush, 1.5f);

				// 2개 이상이면 첫 정점으로의 은은한 가상 폐합 가이드선
				if (nodes.size() >= 2)
				{
					D2D1_POINT_2F pFirst = CoordinateSpace::LocalToScreen(nodes.front().anchor, m_previewShape->GetTransform(), view);
					brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.45f));
					rt->DrawLine(pNext, pFirst, brush, 1.0f);
				}
			}

			// 3. 앵커 점들 표시 (첫 정점 강조)
			for (size_t i = 0; i < nodes.size(); ++i)
			{
				D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(nodes[i].anchor, m_previewShape->GetTransform(), view);
				float sz = (i == 0) ? 4.5f : 3.5f;
				D2D1_RECT_F hr = D2D1::RectF(aScreen.x - sz, aScreen.y - sz, aScreen.x + sz, aScreen.y + sz);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->FillRectangle(hr, brush);
				brush->SetColor((i == 0) ? D2D1::ColorF(D2D1::ColorF::OrangeRed) : D2D1::ColorF(D2D1::ColorF::DarkBlue));
				rt->DrawRectangle(hr, brush, (i == 0) ? 1.8f : 1.2f);
			}
		}
	}
	else
	{
		DrawCommittedShapeAnchors(rt, brush, view, pDoc);

		if (m_hasHover)
		{
			D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
			DrawCrosshairAndAnchor(rt, brush, sp);
		}
	}
}

// ============================================================================
// 5. 손 / 화면 이동 도구 (PanTool)
// ============================================================================
void PanTool::OnDown(const ToolEvent& e)
{
	m_isPanning = true;
	m_lastScreenPt = e.screenPt;
}

void PanTool::OnMove(const ToolEvent& e)
{
	if (!m_isPanning || !e.pView) return;

	float dx = static_cast<float>(e.screenPt.x - m_lastScreenPt.x);
	float dy = static_cast<float>(e.screenPt.y - m_lastScreenPt.y);
	m_lastScreenPt = e.screenPt;

	D2D1_POINT_2F curPan = e.pView->GetPan();
	curPan.x += dx;
	curPan.y += dy;
	e.pView->SetPan(curPan);
}

void PanTool::OnUp(const ToolEvent& e)
{
	m_isPanning = false;
}

// ============================================================================
// 6. 돋보기 도구 (ZoomInTool / ZoomOutTool)
// ============================================================================
// ---------------- 프레임 도구 ----------------
void FrameTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	// 2. 이미 1번째 클릭을 마친 상태(클릭-이동-클릭 모드)인 경우:
	// 이번 2번째 클릭으로 프레임 생성 완료!
	if (m_waitingSecondClick)
	{
		{ const ge::Vec2 sp_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides); m_curW = D2D1::Point2F(sp_.x, sp_.y); }
		m_curS = e.screenPt;
		const float x = (std::min)(m_startW.x, m_curW.x), y = (std::min)(m_startW.y, m_curW.y);
		const float w = std::fabs(m_curW.x - m_startW.x), h = std::fabs(m_curW.y - m_startW.y);

		if (w >= 4.0f && h >= 4.0f)
		{
			auto fn = std::make_unique<ge::FrameNode>();
			fn->SetFrameSize(w, h);
			fn->SetTransform(ge::Matrix2D::Translation(x, y));
			fn->SetName(e.pDoc->GetDoc().GenerateFrameName());
			ge::Node* added = e.pDoc->GetDoc().AddShapeAt(std::move(fn), ge::Vec2{ m_startW.x, m_startW.y });   // 시작점 아래 프레임의 자식
			if (added)
			{
				e.pDoc->GetDoc().SetSelection({ added });
				e.pDoc->SetModifiedFlag(TRUE);
				e.pDoc->CommitUndoPoint(L"프레임 추가");
				LogAppEvent(_T("[프레임 도구] 프레임 추가"));

				GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
				if (SelectTool* sel = GetApp()->GetToolManager().GetSelectTool()) sel->SelectNode(added, e.pDoc);
			}
		}
		m_waitingSecondClick = false;
		m_drag = false;
		e.pDoc->UpdateAllViews(nullptr);
		return;
	}

	// 1. 첫 번째 클릭(다운): 시작점 기록 및 드래그 시작
	m_drag = true;
	m_waitingSecondClick = false;
	{ const ge::Vec2 sp_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides); m_startW = m_curW = D2D1::Point2F(sp_.x, sp_.y); }
	m_startS = m_curS = e.screenPt;
}

void FrameTool::OnMove(const ToolEvent& e)
{
	const ge::Vec2 hv_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides);   // 호버 중에도 가이드 표시
	if (!m_drag && !m_waitingSecondClick) return;
	m_curW = D2D1::Point2F(hv_.x, hv_.y);
	m_curS = e.screenPt;
}

void FrameTool::OnUp(const ToolEvent& e)
{
	if (!m_drag) return;
	m_drag = false;
	{ const ge::Vec2 sp_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides); m_curW = D2D1::Point2F(sp_.x, sp_.y); }
	m_curS = e.screenPt;

	const float dx = m_curW.x - m_startW.x;
	const float dy = m_curW.y - m_startW.y;

	// 1. 드래그 방식: 마우스를 누른 채 충분히 이동(4px 초과)하여 뗐다면 즉시 완성!
	if ((dx * dx + dy * dy) > 16.0f)
	{
		const float x = (std::min)(m_startW.x, m_curW.x), y = (std::min)(m_startW.y, m_curW.y);
		const float w = std::fabs(m_curW.x - m_startW.x), h = std::fabs(m_curW.y - m_startW.y);
		if (!e.pDoc || w < 4.0f || h < 4.0f) return;

		auto fn = std::make_unique<ge::FrameNode>();
		fn->SetFrameSize(w, h);
		fn->SetTransform(ge::Matrix2D::Translation(x, y));
		fn->SetName(e.pDoc->GetDoc().GenerateFrameName());
		ge::Node* added = e.pDoc->GetDoc().AddShapeAt(std::move(fn), ge::Vec2{ m_startW.x, m_startW.y });   // 시작점 아래 프레임의 자식
		if (!added) return;
		e.pDoc->GetDoc().SetSelection({ added });
		e.pDoc->SetModifiedFlag(TRUE);
		e.pDoc->CommitUndoPoint(L"프레임 추가");
		LogAppEvent(_T("[프레임 도구] 프레임 추가"));

		CM2DrawDoc* pDoc = e.pDoc;
		GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
		if (SelectTool* sel = GetApp()->GetToolManager().GetSelectTool()) sel->SelectNode(added, pDoc);
		pDoc->UpdateAllViews(nullptr);
		m_waitingSecondClick = false;
	}
	// 2. 클릭-이동-클릭 방식: 단순 클릭(다운-업)한 경우 2번째 클릭 대기
	else
	{
		m_waitingSecondClick = true;
	}
}

void FrameTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (nChar == VK_ESCAPE)
	{
		m_drag = false;
		m_waitingSecondClick = false;
		m_guides.clear();
		if (pDoc) pDoc->UpdateAllViews(nullptr);
	}
}

void FrameTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* /*pDoc*/)
{
	if (!rt || !brush) return;
	DrawGuideLines(rt, brush, m_guides, zoom, pan);
	if (!m_drag && !m_waitingSecondClick) return;
	const D2D1_RECT_F rc = WorldRectToScreen(m_startW, m_curW, zoom, pan);
	brush->SetColor(D2D1::ColorF(0.20f, 0.45f, 0.85f, 0.12f));
	rt->FillRectangle(rc, brush);
	brush->SetColor(D2D1::ColorF(0.20f, 0.45f, 0.85f, 1.0f));
	rt->DrawRectangle(rc, brush, 1.0f);
}

// ---------------- 테이블 도구 ----------------
void TableTool::OnDown(const ToolEvent& e)
{
	m_drag = true;
	{ const ge::Vec2 sp_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides); m_startW = m_curW = D2D1::Point2F(sp_.x, sp_.y); }
	m_startS = m_curS = e.screenPt;
}

void TableTool::OnMove(const ToolEvent& e)
{
	const ge::Vec2 hv_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides);
	if (!m_drag) return;
	m_curW = D2D1::Point2F(hv_.x, hv_.y);
	m_curS = e.screenPt;
}

void TableTool::OnUp(const ToolEvent& e)
{
	if (!m_drag) return;
	m_drag = false;
	if (!e.pDoc) return;
	{ const ge::Vec2 sp_ = SnapCreationPoint(e, ge::Vec2{ e.worldPt.x, e.worldPt.y }, m_guides); m_curW = D2D1::Point2F(sp_.x, sp_.y); }

	float x = (std::min)(m_startW.x, m_curW.x), y = (std::min)(m_startW.y, m_curW.y);
	float w = std::fabs(m_curW.x - m_startW.x), h = std::fabs(m_curW.y - m_startW.y);
	if (w < 6.0f || h < 6.0f) { x = m_startW.x; y = m_startW.y; w = 300.0f; h = 150.0f; }   // 클릭만 하면 기본 크기

	auto tb = ge::TableComponent::Create(3, 3, w, h);
	tb->SetTransform(ge::Matrix2D::Translation(x, y));
	ge::Node* added = e.pDoc->GetDoc().AddShapeAt(std::move(tb), ge::Vec2{ m_startW.x, m_startW.y });
	if (!added) return;
	e.pDoc->GetDoc().SetSelection({ added });
	e.pDoc->SetModifiedFlag(TRUE);
	e.pDoc->CommitUndoPoint(L"테이블 추가");
	LogAppEvent(_T("[테이블 도구] 테이블 추가"));

	CM2DrawDoc* pDoc = e.pDoc;
	GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
	if (SelectTool* sel = GetApp()->GetToolManager().GetSelectTool()) sel->SelectNode(added, pDoc);
	pDoc->UpdateAllViews(nullptr);
}

void TableTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* /*pDoc*/)
{
	if (!rt || !brush) return;
	DrawGuideLines(rt, brush, m_guides, zoom, pan);
	if (!m_drag) return;
	const D2D1_RECT_F rc = WorldRectToScreen(m_startW, m_curW, zoom, pan);
	brush->SetColor(D2D1::ColorF(0.35f, 0.38f, 0.45f, 0.10f));
	rt->FillRectangle(rc, brush);
	brush->SetColor(D2D1::ColorF(0.35f, 0.38f, 0.45f, 1.0f));
	rt->DrawRectangle(rc, brush, 1.0f);
}

void ZoomInTool::OnDown(const ToolEvent& e)
{
	m_isDragging = true;
	m_dragStartScreen = e.screenPt;
	m_dragCurScreen = e.screenPt;
}

void ZoomInTool::OnMove(const ToolEvent& e)
{
	if (m_isDragging)
	{
		m_dragCurScreen = e.screenPt;
	}
}

void ZoomInTool::OnUp(const ToolEvent& e)
{
	if (!m_isDragging) return;
	m_isDragging = false;

	if (!e.pView) return;

	int dx = std::abs(m_dragCurScreen.x - m_dragStartScreen.x);
	int dy = std::abs(m_dragCurScreen.y - m_dragStartScreen.y);

	// 5px 미만 클릭인 경우: 마우스 휠과 동일하게 클릭 위치 기준 1.25배 확대
	if (dx < 5 && dy < 5)
	{
		e.pView->ZoomAtPoint(e.screenPt, 1.25f);
	}
	else
	{
		// 드래그 영역(Marquee) 줌: 드래그한 사각형 영역을 뷰포트에 맞춤 확대
		CRect marqueeRc(
			min(m_dragStartScreen.x, m_dragCurScreen.x),
			min(m_dragStartScreen.y, m_dragCurScreen.y),
			max(m_dragStartScreen.x, m_dragCurScreen.x),
			max(m_dragStartScreen.y, m_dragCurScreen.y)
		);
		e.pView->ZoomAtRect(marqueeRc);
	}
}

void ZoomInTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                             float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!m_isDragging || !rt || !brush) return;

	float left = static_cast<float>(min(m_dragStartScreen.x, m_dragCurScreen.x));
	float top = static_cast<float>(min(m_dragStartScreen.y, m_dragCurScreen.y));
	float right = static_cast<float>(max(m_dragStartScreen.x, m_dragCurScreen.x));
	float bottom = static_cast<float>(max(m_dragStartScreen.y, m_dragCurScreen.y));

	if (right - left > 2.0f && bottom - top > 2.0f)
	{
		D2D1_RECT_F r = D2D1::RectF(left, top, right, bottom);

		// 반투명 파란색 채우기 박스
		brush->SetColor(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.15f));
		rt->FillRectangle(r, brush);

		// 외곽선
		brush->SetColor(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.9f));
		rt->DrawRectangle(r, brush, 1.2f);
	}
}

void ZoomOutTool::OnDown(const ToolEvent& e)
{
	if (!e.pView) return;

	// 마우스 휠과 동일하게 클릭 지점을 기준으로 0.8배 축소
	e.pView->ZoomAtPoint(e.screenPt, 0.8f);
}

void ZoomOutTool::OnMove(const ToolEvent& e)
{
}

void ZoomOutTool::OnUp(const ToolEvent& e)
{
}


// ============================================================================
// GradientTool 구현 (전통적인 그라디언트 도구)
// ============================================================================
void GradientTool::OnDown(const ToolEvent& e)
{
	m_drag = Drag::None;
	m_moved = false;
	if (!e.pDoc || !e.pD2DFactory) return;

	auto& doc = e.pDoc->GetDoc();
	const D2D1_POINT_2F screenPt = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
	m_downScreen = m_curScreen = screenPt;

	const std::vector<ge::Node*> sel = doc.GetSelection();
	ge::Node* cur = (sel.size() == 1 && SupportsGradientFill(sel[0])) ? sel[0] : nullptr;
	if (cur && !IsInteractive(cur)) cur = nullptr;

	// 1~3. 선택된 그라디언트 도형/프레임의 기즈모: 핸들 -> 컬러 스톱 점 -> 축 클릭(스톱 추가) 순으로 우선 처리
	GradientGizmo gz;
	if (cur && cur->GetStyle().fill.type == ge::PaintType::Gradient && ComputeGradientGizmo(cur, e.view, e.pD2DFactory, gz))
	{
		// 1) 핸들 (방사형에서 중심과 초점이 겹쳐 있으면 중심, Shift 를 누르면 초점)
		int hitPart = kGradNone;
		float best = 8.0f * 8.0f;
		auto consider = [&](int part, D2D1_POINT_2F p) {
			const float d = DistSq(screenPt, p);
			if (d <= best) { best = d; hitPart = part; }
		};
		const bool coincident = gz.radial && DistSq(gz.start, gz.end) < 1.0f;
		if (coincident && e.shiftDown)
		{
			consider(kGradEnd, gz.end);
		}
		else
		{
			consider(kGradStart, gz.start);
			if (!coincident) consider(kGradEnd, gz.end);
		}
		if (gz.radial) consider(kGradRadius, gz.radius);
		if (hitPart != kGradNone)
		{
			m_drag = Drag::Part;
			m_shapeId = cur->GetId();
			m_part = hitPart;
			m_bounds = gz.bounds;
			return;
		}

		// 2) 컬러 스톱 점 (양 끝 스톱은 시작/끝 핸들과 겹치므로 안쪽 스톱만): 드래그로 이동, Alt+클릭으로 삭제
		m_axisA = gz.start;
		m_axisB = gz.radial ? gz.radius : gz.end;
		ge::Gradient& g = cur->GetStyleRef().fill.gradient;
		for (int i = 0; i < static_cast<int>(g.stops.size()); ++i)
		{
			const float off = g.stops[i].offset;
			if (off <= 0.001f || off >= 0.999f) continue;
			const D2D1_POINT_2F p = D2D1::Point2F(m_axisA.x + (m_axisB.x - m_axisA.x) * off, m_axisA.y + (m_axisB.y - m_axisA.y) * off);
			if (DistSq(screenPt, p) > 7.0f * 7.0f) continue;

			if (e.altDown)
			{
				if (g.stops.size() > 2)
				{
					g.stops.erase(g.stops.begin() + i);
					m_activeStop = -1;
					LogAppEvent(_T("[그라디언트 도구] 컬러 스톱 삭제"));
					UpdateShapePropertiesSelection(cur, e.pDoc);
				}
				return;
			}
			m_drag = Drag::Stop;
			m_shapeId = cur->GetId();
			m_stop = m_activeStop = i;
			m_bounds = gz.bounds;
			return;
		}

		// 3) 축(선) 위 클릭: 그 위치에 새 컬러 스톱 추가 (현재 그라디언트에서 보간한 색) 후 바로 드래그 가능
		float d2 = 0.0f;
		const float t = ProjectOnAxis(m_axisA, m_axisB, screenPt, d2);
		if (d2 <= 6.0f * 6.0f && t > 0.01f && t < 0.99f && g.stops.size() < 16)
		{
			ge::Gradient sorted = g;
			sorted.NormalizeStops();
			const ge::Color c = sorted.SampleColor(t);
			g.stops = sorted.stops;
			g.stops.push_back({ t, c });
			g.NormalizeStops();
			for (int i = 0; i < static_cast<int>(g.stops.size()); ++i)
				if (g.stops[i].offset == t) { m_stop = m_activeStop = i; break; }
			m_drag = Drag::Stop;
			m_shapeId = cur->GetId();
			m_bounds = gz.bounds;
			LogAppEvent(_T("[그라디언트 도구] 컬러 스톱 추가"));
			UpdateShapePropertiesSelection(cur, e.pDoc);
			return;
		}
	}

	// 4. 도형/프레임 집기 + 드래그로 그라디언트 만들기: 클릭한 도형/프레임을 선택하거나, 선택된 대상에 대해 어디서든 드래그
	ge::Node* hit = HitTestGradNodeDeep(e.pDoc, screenPt, e.view, e.pD2DFactory);
	ge::Node* target = hit ? hit : cur;
	if (hit && hit != cur)
	{
		if (SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool())
			pSelect->SelectNode(hit, e.pDoc);
		m_activeStop = -1;
	}
	if (!target)
	{
		doc.ClearSelection();
		UpdateShapePropertiesSelection(nullptr, e.pDoc);
		return;
	}
	if (!ComputePathBounds(target, e.pD2DFactory, m_bounds)) return;
	m_drag = Drag::Create;
	m_shapeId = target->GetId();
}

void GradientTool::OnMove(const ToolEvent& e)
{
	m_curScreen = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
	if (m_drag == Drag::None || !e.pDoc) return;

	ge::Node* ps = e.pDoc->GetDoc().FindById(m_shapeId);
	if (!SupportsGradientFill(ps)) { m_drag = Drag::None; return; }

	if (DistSq(m_downScreen, m_curScreen) > 3.0f * 3.0f) m_moved = true;

	switch (m_drag)
	{
	case Drag::Part:
		ApplyGradPartDrag(ps, m_bounds, m_part, m_curScreen, e.view);
		break;

	case Drag::Stop:
	{
		float d2 = 0.0f;
		const float t = ProjectOnAxis(m_axisA, m_axisB, m_curScreen, d2);
		ge::Gradient& g = ps->GetStyleRef().fill.gradient;
		if (m_stop >= 0 && m_stop < static_cast<int>(g.stops.size()))
			g.stops[m_stop].offset = t;   // 드래그 중에는 정렬하지 않음 (렌더러가 정렬된 복사본을 사용)
		break;
	}

	case Drag::Create:
	default:
		break;   // 미리보기는 DrawOverlay 가 m_downScreen -> m_curScreen 선으로 그림
	}

	if (m_drag == Drag::Part || m_drag == Drag::Stop)
	{
		static DWORD s_lastTick = 0;
		const DWORD now = ::GetTickCount();
		if (now - s_lastTick >= 30)
		{
			s_lastTick = now;
			UpdateShapePropertiesSelection(ps, e.pDoc);   // 속성창 수치/스톱 바 실시간 동기화
		}
	}
}

void GradientTool::OnUp(const ToolEvent& e)
{
	const Drag drag = m_drag;
	m_drag = Drag::None;
	if (drag == Drag::None || !e.pDoc) return;

	ge::Node* ps = e.pDoc->GetDoc().FindById(m_shapeId);
	if (!SupportsGradientFill(ps)) return;

	if (drag == Drag::Create)
	{
		if (!m_moved) return;   // 단순 클릭은 선택만
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		const D2D1_POINT_2F upScreen = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
		if (!ScreenToGradRatio(ps, m_bounds, m_downScreen, e.view, x0, y0) || !ScreenToGradRatio(ps, m_bounds, upScreen, e.view, x1, y1)) return;

		EnsureGradient(ps, m_kind);
		ge::Gradient& g = ps->GetStyleRef().fill.gradient;
		if (g.kind != m_kind) g.SetKind(m_kind);   // 도구의 Linear/Radial 선택이 기존 그라디언트 종류보다 우선
		g.start = { x0, y0 };
		if (m_kind == ge::Gradient::Kind::Linear)
		{
			g.end = { x1, y1 };
		}
		else
		{
			// 방사형: 드래그 시작점이 중심, 드래그 거리가 반지름 (박스 너비 기준 비율), 초점은 중심과 같게
			const float w = m_bounds.right - m_bounds.left;
			const float h = m_bounds.bottom - m_bounds.top;
			const float dx = (x1 - x0) * w, dy = (y1 - y0) * h;
			g.end = g.start;
			g.radius = (std::max)(0.01f, std::sqrt(dx * dx + dy * dy) / w);
		}
		LogAppEvent(m_kind == ge::Gradient::Kind::Linear ? _T("[그라디언트 도구] 선형 그라디언트 방향 지정") : _T("[그라디언트 도구] 방사형 그라디언트 중심/반지름 지정"));
	}
	else if (drag == Drag::Stop)
	{
		// 드래그가 끝나면 offset 순으로 정렬하고, 같은 스톱을 계속 강조
		ge::Gradient& g = ps->GetStyleRef().fill.gradient;
		if (m_stop >= 0 && m_stop < static_cast<int>(g.stops.size()))
		{
			const float off = g.stops[m_stop].offset;
			g.NormalizeStops();
			for (int i = 0; i < static_cast<int>(g.stops.size()); ++i)
				if (g.stops[i].offset == off) { m_activeStop = i; break; }
		}
	}

	UpdateShapePropertiesSelection(ps, e.pDoc);
}

bool GradientTool::OnDoubleClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return false;
	const std::vector<ge::Node*>& sel = pDoc->GetDoc().GetSelection();
	ge::Node* ps = (sel.size() == 1 && SupportsGradientFill(sel[0])) ? sel[0] : nullptr;
	if (!ps || !IsInteractive(ps)) return false;

	// 기본 그라디언트: 없으면 만들고, 있으면 방향/반지름만 종류별 기본값으로 되돌림
	EnsureGradient(ps, m_kind);
	ge::Gradient& g = ps->GetStyleRef().fill.gradient;
	if (g.kind != m_kind) g.SetKind(m_kind);
	g.ResetGeometry();
	LogAppEvent(_T("[그라디언트 도구] 기본 그라디언트 적용"));
	UpdateShapePropertiesSelection(ps, pDoc);
	return false;   // 도구는 유지
}

void GradientTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush || !pDoc) return;
	const ViewportTransform view = { zoom, { pan.x, pan.y } };

	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	if (!f) return;

	const std::vector<ge::Node*>& sel = pDoc->GetDoc().GetSelection();
	ge::Node* ps = (sel.size() == 1 && SupportsGradientFill(sel[0])) ? sel[0] : nullptr;
	if (ps && IsInteractive(ps))
	{
		// 선택된 도형/프레임의 경계 상자 (그라디언트 박스 = 비율 좌표의 기준) 를 얇게 표시
		ge::Rect2 b{};
		if (ComputePathBounds(ps, f, b))
		{
			const ge::Matrix2D wx = ps->GetWorldTransform();
			const D2D1_POINT_2F c[4] = {
				CoordinateSpace::LocalToScreen(D2D1::Point2F(b.left, b.top), wx, view),
				CoordinateSpace::LocalToScreen(D2D1::Point2F(b.right, b.top), wx, view),
				CoordinateSpace::LocalToScreen(D2D1::Point2F(b.right, b.bottom), wx, view),
				CoordinateSpace::LocalToScreen(D2D1::Point2F(b.left, b.bottom), wx, view) };
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.75f));
			for (int i = 0; i < 4; ++i)
				rt->DrawLine(c[i], c[(i + 1) % 4], brush, 1.0f);
		}

		if (ps->GetStyle().fill.type == ge::PaintType::Gradient)
			DrawGradientGizmo(rt, brush, view, f, ps, m_drag == Drag::Part ? m_part : 0, m_activeStop);
	}

	// 새 그라디언트를 드래그로 지정하는 동안의 미리보기 (시작 ■ -> 현재 위치 ●)
	if (m_drag == Drag::Create && m_moved)
	{
		brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));
		rt->DrawLine(m_downScreen, m_curScreen, brush, 3.0f);
		brush->SetColor(D2D1::ColorF(0.10f, 0.30f, 0.70f, 1.0f));
		rt->DrawLine(m_downScreen, m_curScreen, brush, 1.3f);
		const D2D1_RECT_F sq = D2D1::RectF(m_downScreen.x - 5.0f, m_downScreen.y - 5.0f, m_downScreen.x + 5.0f, m_downScreen.y + 5.0f);
		brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
		rt->FillRectangle(sq, brush);
		brush->SetColor(D2D1::ColorF(0.10f, 0.30f, 0.70f, 1.0f));
		rt->DrawRectangle(sq, brush, 1.5f);
		brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
		rt->FillEllipse(D2D1::Ellipse(m_curScreen, 5.5f, 5.5f), brush);
		brush->SetColor(D2D1::ColorF(0.10f, 0.30f, 0.70f, 1.0f));
		rt->DrawEllipse(D2D1::Ellipse(m_curScreen, 5.5f, 5.5f), brush, 1.5f);
	}

	f->Release();
}
