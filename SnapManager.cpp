#include "pch.h"
#include "SnapManager.h"
#include <algorithm>

SnapResult SnapManager::FindSnapPoint(
	D2D1_POINT_2F mouseWorld,
	float zoom,
	const GridRenderer& grid,
	const ge::Document* pDoc,
	const ge::Node* excludeNode) const
{
	SnapResult result;
	result.snapped = false;
	result.pt = mouseWorld;
	result.targetType = SnapTargets::None;

	if (!m_enabled || zoom <= 0.0001f)
	{
		return result;
	}

	float tolWorld = m_tolerancePx / zoom;
	float bestDistSq = tolWorld * tolWorld;

	auto tryCandidate = [&](D2D1_POINT_2F candPt, SnapTargets type, const wchar_t* desc) {
		float dx = candPt.x - mouseWorld.x;
		float dy = candPt.y - mouseWorld.y;
		float distSq = dx * dx + dy * dy;
		if (distSq <= bestDistSq)
		{
			bestDistSq = distSq;
			result.snapped = true;
			result.pt = candPt;
			result.targetType = type;
			result.description = desc;
		}
	};

	// 1. 오브젝트 스냅 (Anchor, Midpoint, Center) - ge::Document 노드 순회
	if (pDoc)
	{
		std::function<void(const ge::Node*)> checkNode = [&](const ge::Node* node) {
			if (!node || !node->IsVisible() || node == excludeNode) return;

			if (node->Kind() == ge::NodeKind::Group || node->Kind() == ge::NodeKind::Layer)
			{
				const auto* g = static_cast<const ge::GroupNode*>(node);
				for (const auto& child : g->GetChildren())
				{
					checkNode(child.get());
				}
				return;
			}

			if (node->Kind() == ge::NodeKind::Path)
			{
				const auto* pn = static_cast<const ge::PathNode*>(node);
				ge::Matrix2D worldXf = pn->GetWorldTransform();

				for (const auto& sp : pn->GetPaths())
				{
					for (size_t i = 0; i < sp.nodes.size(); ++i)
					{
						const auto& bn = sp.nodes[i];
						ge::Vec2 aWorld = worldXf.TransformPoint(bn.anchor);
						D2D1_POINT_2F aPt = aWorld.ToD2D();

						// (1) 앵커/정점 스냅
						if (HasSnapTarget(m_targets, SnapTargets::Anchor))
						{
							tryCandidate(aPt, SnapTargets::Anchor, L"정점 (Anchor)");
						}

						// (2) 선분 중간점 (Midpoint) 스냅
						if (HasSnapTarget(m_targets, SnapTargets::Midpoint))
						{
							size_t nextIdx = i + 1;
							if (nextIdx >= sp.nodes.size() && sp.closed)
								nextIdx = 0;

							if (nextIdx < sp.nodes.size())
							{
								const auto& nextBn = sp.nodes[nextIdx];
								ge::Vec2 midLocal = {
									(bn.anchor.x + nextBn.anchor.x) * 0.5f,
									(bn.anchor.y + nextBn.anchor.y) * 0.5f
								};
								ge::Vec2 midWorld = worldXf.TransformPoint(midLocal);
								tryCandidate(midWorld.ToD2D(), SnapTargets::Midpoint, L"중간점 (Midpoint)");
							}
						}
					}
				}

				// (3) 도형 중심점 (Center) 스냅
				if (HasSnapTarget(m_targets, SnapTargets::Center))
				{
					ge::Rect2 lb;
					if (const_cast<ge::PathNode*>(pn)->GetLocalBounds(lb))
					{
						ge::Vec2 centerLocal = { lb.MidX(), lb.MidY() };
						ge::Vec2 centerWorld = worldXf.TransformPoint(centerLocal);
						tryCandidate(centerWorld.ToD2D(), SnapTargets::Center, L"중심 (Center)");
					}
				}
			}
		};

		for (const auto& layer : pDoc->GetLayers())
		{
			checkNode(layer.get());
		}
	}

	// 2. 기준 좌표축 (Origin Axis) 스냅
	bool isBottomLeft = (pDoc && pDoc->originMode == ge::CanvasOriginMode::BottomLeft);
	float originWorldY = (isBottomLeft && pDoc) ? pDoc->height : grid.originWorld.y;

	if (HasSnapTarget(m_targets, SnapTargets::OriginAxis) && grid.showAxis)
	{
		// X축 (Y = originWorldY)
		float dy = std::abs(mouseWorld.y - originWorldY);
		if (dy <= tolWorld)
		{
			tryCandidate(D2D1::Point2F(mouseWorld.x, originWorldY), SnapTargets::OriginAxis, L"X 기준축");
		}
		// Y축 (X = grid.originWorld.x)
		float dx = std::abs(mouseWorld.x - grid.originWorld.x);
		if (dx <= tolWorld)
		{
			tryCandidate(D2D1::Point2F(grid.originWorld.x, mouseWorld.y), SnapTargets::OriginAxis, L"Y 기준축");
		}
		// 원점 교차점 (grid.originWorld.x, originWorldY)
		tryCandidate(D2D1::Point2F(grid.originWorld.x, originWorldY), SnapTargets::OriginAxis, L"원점 (0, 0)");
	}

	// 3. 월드 그리드 (Grid) 격자점 스냅 (그리드 표시 및 스냅 활성화 여부 동시 검사)
	if (HasSnapTarget(m_targets, SnapTargets::Grid) && grid.enabled && grid.snapToGrid)
	{
		float step = grid.GetStepPx();
		if (step > 0.001f)
		{
			float relX = mouseWorld.x - grid.originWorld.x;
			float relY = mouseWorld.y - originWorldY;
			float gx = grid.originWorld.x + static_cast<float>(std::round(relX / step)) * step;
			float gy = originWorldY + static_cast<float>(std::round(relY / step)) * step;
			tryCandidate(D2D1::Point2F(gx, gy), SnapTargets::Grid, L"격자 (Grid)");
		}
	}

	return result;
}

void SnapManager::DrawSnapIndicator(
	ID2D1RenderTarget* rt,
	ID2D1SolidColorBrush* brush,
	const SnapResult& snap,
	float zoom,
	D2D1_POINT_2F pan) const
{
	if (!rt || !brush || !snap.snapped) return;

	D2D1_POINT_2F sp = D2D1::Point2F(snap.pt.x * zoom + pan.x, snap.pt.y * zoom + pan.y);

	const D2D1_COLOR_F savedColor = brush->GetColor();

	switch (snap.targetType)
	{
	case SnapTargets::Anchor:
		// 정점 스냅: 청록색(Cyan) 자석 다이아몬드 마커
		{
			brush->SetColor(D2D1::ColorF(0.0f, 0.85f, 0.95f, 0.9f));
			const float r = 5.5f;
			rt->DrawLine(D2D1::Point2F(sp.x - r, sp.y), D2D1::Point2F(sp.x, sp.y - r), brush, 1.8f);
			rt->DrawLine(D2D1::Point2F(sp.x, sp.y - r), D2D1::Point2F(sp.x + r, sp.y), brush, 1.8f);
			rt->DrawLine(D2D1::Point2F(sp.x + r, sp.y), D2D1::Point2F(sp.x, sp.y + r), brush, 1.8f);
			rt->DrawLine(D2D1::Point2F(sp.x, sp.y + r), D2D1::Point2F(sp.x - r, sp.y), brush, 1.8f);
		}
		break;

	case SnapTargets::Midpoint:
		// 중간점 스냅: 보라색(Magenta) 삼각형 마커
		{
			brush->SetColor(D2D1::ColorF(0.85f, 0.25f, 0.95f, 0.9f));
			const float r = 5.0f;
			rt->DrawLine(D2D1::Point2F(sp.x - r, sp.y + r), D2D1::Point2F(sp.x, sp.y - r), brush, 1.8f);
			rt->DrawLine(D2D1::Point2F(sp.x, sp.y - r), D2D1::Point2F(sp.x + r, sp.y + r), brush, 1.8f);
			rt->DrawLine(D2D1::Point2F(sp.x + r, sp.y + r), D2D1::Point2F(sp.x - r, sp.y + r), brush, 1.8f);
		}
		break;

	case SnapTargets::Center:
		// 중심점 스냅: 노란색(Gold) 원형 동심 링
		{
			brush->SetColor(D2D1::ColorF(1.0f, 0.80f, 0.15f, 0.9f));
			rt->DrawEllipse(D2D1::Ellipse(sp, 5.5f, 5.5f), brush, 1.8f);
		}
		break;

	case SnapTargets::OriginAxis:
		// 기준축/원점 스냅: 주황색 십자선 마커
		{
			brush->SetColor(D2D1::ColorF(1.0f, 0.45f, 0.15f, 0.9f));
			rt->DrawLine(D2D1::Point2F(sp.x - 7.0f, sp.y), D2D1::Point2F(sp.x + 7.0f, sp.y), brush, 1.5f);
			rt->DrawLine(D2D1::Point2F(sp.x, sp.y - 7.0f), D2D1::Point2F(sp.x, sp.y + 7.0f), brush, 1.5f);
		}
		break;

	case SnapTargets::Grid:
	default:
		// 그리드 스냅: 은은한 닷(Dot) 링 피드백
		{
			brush->SetColor(D2D1::ColorF(0.18f, 0.55f, 0.95f, 0.85f));
			rt->DrawEllipse(D2D1::Ellipse(sp, 4.0f, 4.0f), brush, 1.4f);
		}
		break;
	}

	brush->SetColor(savedColor);
}
