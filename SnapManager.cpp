#include "pch.h"
#include "SnapManager.h"
#include <algorithm>

SnapResult SnapManager::FindSnapPoint(
	D2D1_POINT_2F mouseWorld,
	float zoom,
	const GridRenderer& grid,
	const std::vector<std::unique_ptr<Shape>>* pShapes,
	const Shape* excludeShape) const
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

	// 1. 오브젝트 스냅 (Anchor, Midpoint, Center) - 우선순위 최고
	if (pShapes)
	{
		for (const auto& spPtr : *pShapes)
		{
			Shape* shape = spPtr.get();
			if (!shape || shape == excludeShape) continue;

			PathShape* ps = dynamic_cast<PathShape*>(shape);
			if (ps)
			{
				for (const auto& sp : ps->paths)
				{
					for (size_t i = 0; i < sp.nodes.size(); ++i)
					{
						const auto& node = sp.nodes[i];
						D2D1_POINT_2F aWorld = ps->xf.TransformPoint(node.anchor);

						// (1) 앵커/정점 스냅
						if (HasSnapTarget(m_targets, SnapTargets::Anchor))
						{
							tryCandidate(aWorld, SnapTargets::Anchor, L"정점 (Anchor)");
						}

						// (2) 선분 중간점 (Midpoint) 스냅
						if (HasSnapTarget(m_targets, SnapTargets::Midpoint))
						{
							size_t nextIdx = i + 1;
							if (nextIdx >= sp.nodes.size() && sp.closed)
								nextIdx = 0;

							if (nextIdx < sp.nodes.size())
							{
								const auto& nextNode = sp.nodes[nextIdx];
								D2D1_POINT_2F midLocal = D2D1::Point2F(
									(node.anchor.x + nextNode.anchor.x) * 0.5f,
									(node.anchor.y + nextNode.anchor.y) * 0.5f
								);
								D2D1_POINT_2F midWorld = ps->xf.TransformPoint(midLocal);
								tryCandidate(midWorld, SnapTargets::Midpoint, L"중간점 (Midpoint)");
							}
						}
					}
				}
			}

			// (3) 도형 중심점 (Center) 스냅
			if (HasSnapTarget(m_targets, SnapTargets::Center))
			{
				ID2D1Factory* pFactory = nullptr;
				// 도형 로컬 바운딩의 중심점 산출
				D2D1_RECT_F b;
				// 간단한 정점 평균 중심
				if (ps && !ps->paths.empty())
				{
					float sumX = 0.0f, sumY = 0.0f;
					int count = 0;
					for (const auto& path : ps->paths)
					{
						for (const auto& n : path.nodes)
						{
							sumX += n.anchor.x;
							sumY += n.anchor.y;
							count++;
						}
					}
					if (count > 0)
					{
						D2D1_POINT_2F centerLocal = D2D1::Point2F(sumX / count, sumY / count);
						D2D1_POINT_2F centerWorld = ps->xf.TransformPoint(centerLocal);
						tryCandidate(centerWorld, SnapTargets::Center, L"중심 (Center)");
					}
				}
			}
		}
	}

	// 2. 기준 좌표축 (Origin Axis) 스냅
	if (HasSnapTarget(m_targets, SnapTargets::OriginAxis) && grid.showAxis)
	{
		// X축 (Y = originWorld.y)
		float dy = std::abs(mouseWorld.y - grid.originWorld.y);
		if (dy <= tolWorld)
		{
			tryCandidate(D2D1::Point2F(mouseWorld.x, grid.originWorld.y), SnapTargets::OriginAxis, L"X 기준축");
		}
		// Y축 (X = originWorld.x)
		float dx = std::abs(mouseWorld.x - grid.originWorld.x);
		if (dx <= tolWorld)
		{
			tryCandidate(D2D1::Point2F(grid.originWorld.x, mouseWorld.y), SnapTargets::OriginAxis, L"Y 기준축");
		}
		// 원점 교차점 (originWorld)
		tryCandidate(grid.originWorld, SnapTargets::OriginAxis, L"원점 (0, 0)");
	}

	// 3. 월드 그리드 (Grid) 격자점 스냅
	if (HasSnapTarget(m_targets, SnapTargets::Grid) && grid.enabled)
	{
		float step = grid.GetStepPx();
		if (step > 0.001f)
		{
			float relX = mouseWorld.x - grid.originWorld.x;
			float relY = mouseWorld.y - grid.originWorld.y;
			float gx = grid.originWorld.x + static_cast<float>(std::round(relX / step)) * step;
			float gy = grid.originWorld.y + static_cast<float>(std::round(relY / step)) * step;
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
