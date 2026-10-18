#pragma once
#include <d2d1.h>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdint>
#include "D2DVectorEngine.h"
#include "GridRenderer.h"

// 활성화할 스냅 타깃 플래그 (비트마스크)
enum class SnapTargets : uint32_t
{
	None       = 0,
	Grid       = 1 << 0, // 그리드 격자점 스냅
	OriginAxis = 1 << 1, // X/Y 기준 좌표축 스냅
	Anchor     = 1 << 2, // 도형의 앵커/정점 스냅
	Midpoint   = 1 << 3, // 선분/에지의 중간점 스냅
	Center     = 1 << 4, // 도형 바운딩 중심점 스냅
	All        = 0xFFFFFFFF
};

inline SnapTargets operator|(SnapTargets a, SnapTargets b)
{
	return static_cast<SnapTargets>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline SnapTargets operator&(SnapTargets a, SnapTargets b)
{
	return static_cast<SnapTargets>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasSnapTarget(SnapTargets flags, SnapTargets target)
{
	return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(target)) != 0;
}

// 스냅 판정 결과 구조체
struct SnapResult
{
	bool          snapped = false;
	D2D1_POINT_2F pt{ 0.0f, 0.0f };             // 스냅된 최종 월드 좌표
	SnapTargets   targetType = SnapTargets::None;// 걸린 스냅 유형
	const wchar_t* description = L"";            // UI 툴팁/안내 ("격자", "정점", "중심점" 등)
};

// ============================================================================
// SnapManager: 월드(그리드, 기준축) 및 오브젝트(정점, 중점, 중심) 스냅 총괄 매니저
// ============================================================================
class SnapManager
{
public:
	SnapManager()
		: m_enabled(true)
		, m_targets(SnapTargets::Grid | SnapTargets::OriginAxis | SnapTargets::Anchor)
		, m_tolerancePx(8.0f) // 마우스 기준 8px 이내 근접 시 자석 스냅
	{
	}

	void SetEnabled(bool b) { m_enabled = b; }
	bool IsEnabled() const { return m_enabled; }

	void SetTargets(SnapTargets t) { m_targets = t; }
	SnapTargets GetTargets() const { return m_targets; }
	void EnableTarget(SnapTargets t, bool enable)
	{
		if (enable)
			m_targets = m_targets | t;
		else
			m_targets = static_cast<SnapTargets>(static_cast<uint32_t>(m_targets) & ~static_cast<uint32_t>(t));
	}

	void SetTolerancePx(float px) { m_tolerancePx = px; }
	float GetTolerancePx() const { return m_tolerancePx; }

	// 핵심 스냅 판정 함수:
	// 마우스 월드 좌표, 그리드 설정, 줌 레벨, 그리고 도큐먼트의 도형 목록을 고려하여
	// 허용 반경 내에서 가장 가깝고 우선순위가 높은 스냅 포인트를 산출
	SnapResult FindSnapPoint(
		D2D1_POINT_2F mouseWorld,
		float zoom,
		const GridRenderer& grid,
		const std::vector<std::unique_ptr<Shape>>* pShapes = nullptr,
		const Shape* excludeShape = nullptr
	) const;

	// 스냅이 걸렸을 때 시각적 피드백(자석 원형 링, 자석 다이아몬드, 스냅 가이드) 렌더링
	void DrawSnapIndicator(
		ID2D1RenderTarget* rt,
		ID2D1SolidColorBrush* brush,
		const SnapResult& snap,
		float zoom,
		D2D1_POINT_2F pan
	) const;

private:
	bool        m_enabled;
	SnapTargets m_targets;
	float       m_tolerancePx;
};
