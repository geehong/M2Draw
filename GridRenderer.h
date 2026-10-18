#pragma once
#include <d2d1.h>
#include <cmath>
#pragma comment(lib, "d2d1.lib")

enum class CanvasUnit
{
	Pixel = 0, // px / pt (1 pt = 1.0 px @ 96 DPI)
	MM,        // 1 mm = 3.7795 px (96 / 25.4)
	CM,        // 1 cm = 37.795 px (10 mm)
	Inch,      // 1 in = 96.0 px (25.4 mm)
	Rem        // 1 rem = 16.0 px (웹 표준 기본 폰트 크기 기준)
};

inline const wchar_t* GetCanvasUnitName(CanvasUnit unit)
{
	switch (unit)
	{
	case CanvasUnit::Pixel: return L"픽셀 / 포인트 (px/pt)";
	case CanvasUnit::MM:    return L"밀리미터 (mm)";
	case CanvasUnit::CM:    return L"센티미터 (cm)";
	case CanvasUnit::Inch:  return L"인치 (in)";
	case CanvasUnit::Rem:   return L"웹 rem (16px)";
	default:                return L"픽셀 / 포인트 (px/pt)";
	}
}

inline const wchar_t* GetCanvasUnitSuffix(CanvasUnit unit)
{
	switch (unit)
	{
	case CanvasUnit::Pixel: return L"px";
	case CanvasUnit::MM:    return L"mm";
	case CanvasUnit::CM:    return L"cm";
	case CanvasUnit::Inch:  return L"in";
	case CanvasUnit::Rem:   return L"rem";
	default:                return L"px";
	}
}

inline float GetPixelsPerCanvasUnit(CanvasUnit unit)
{
	switch (unit)
	{
	case CanvasUnit::Pixel: return 1.0f;
	case CanvasUnit::MM:    return 3.779527559f; // 96.0 / 25.4
	case CanvasUnit::CM:    return 37.79527559f; // 10 mm
	case CanvasUnit::Inch:  return 96.0f;        // 96 DPI
	case CanvasUnit::Rem:   return 16.0f;        // 기본 16px
	default:                return 1.0f;
	}
}

struct GridOptions
{
	bool          enabled        = true;
	bool          snapToGrid     = false;
	CanvasUnit    unit           = CanvasUnit::Pixel;
	float         stepInUnit     = 20.0f;
	int           majorEvery     = 5;
	float         minStepPx      = 8.0f;
	D2D1_COLOR_F  minor          = D2D1::ColorF(0.75f, 0.78f, 0.88f, 0.5f);
	D2D1_COLOR_F  major          = D2D1::ColorF(0.60f, 0.64f, 0.80f, 0.8f);

	// X, Y 기준 좌표축 (Origin Axis)
	bool          showAxis       = true;
	D2D1_POINT_2F originWorld    = { 0.0f, 0.0f };
	float         axisThickness  = 1.5f;
	float         axisOpacity    = 0.85f;
	D2D1_COLOR_F  axisXColor     = D2D1::ColorF(0.95f, 0.35f, 0.35f); // Red
	D2D1_COLOR_F  axisYColor     = D2D1::ColorF(0.35f, 0.80f, 0.45f); // Green
};

class GridRenderer
{
public:
	bool          enabled;
	bool          snapToGrid;   // 그리드 스냅 활성화 여부
	CanvasUnit    unit;
	float         stepInUnit;   // 선택한 단위 기준의 간격 (예: 20px, 10mm, 1in, 1rem)
	int           majorEvery;   // N번째마다 주(Major) 격자선
	float         minStepPx;    // LOD 화면 최소 픽셀 (기본 8.0px)
	D2D1_COLOR_F  minor;
	D2D1_COLOR_F  major;

	// X, Y 기준 좌표축
	bool          showAxis;
	D2D1_POINT_2F originWorld;
	float         axisThickness;
	float         axisOpacity;
	D2D1_COLOR_F  axisXColor;
	D2D1_COLOR_F  axisYColor;

	GridRenderer()
		: GridRenderer(GridOptions{})
	{
	}

	GridRenderer(const GridOptions& opt)
		: enabled(opt.enabled)
		, snapToGrid(opt.snapToGrid)
		, unit(opt.unit)
		, stepInUnit(opt.stepInUnit)
		, majorEvery(opt.majorEvery)
		, minStepPx(opt.minStepPx)
		, minor(opt.minor)
		, major(opt.major)
		, showAxis(opt.showAxis)
		, originWorld(opt.originWorld)
		, axisThickness(opt.axisThickness)
		, axisOpacity(opt.axisOpacity)
		, axisXColor(opt.axisXColor)
		, axisYColor(opt.axisYColor)
	{
	}

	// 단위 변경 시 기존 물리적 화면 픽셀 거리를 보존하며 stepInUnit 자동 환산 동기화
	void SetUnit(CanvasUnit newUnit)
	{
		if (unit == newUnit) return;
		float currentPx = GetStepPx();
		unit = newUnit;
		float pxPerUnit = GetPixelsPerCanvasUnit(newUnit);
		if (pxPerUnit > 0.0001f)
		{
			stepInUnit = currentPx / pxPerUnit;
		}
	}

	// 실제 월드 px 단위 간격 계산
	float GetStepPx() const
	{
		return stepInUnit * GetPixelsPerCanvasUnit(unit);
	}

	// 월드 좌표를 그리드 간격에 맞게 스냅 처리 (기준 원점 오프셋 반영)
	D2D1_POINT_2F SnapPoint(D2D1_POINT_2F worldPt) const
	{
		if (!snapToGrid) return worldPt;
		float step = GetStepPx();
		if (step <= 0.001f) return worldPt;

		float relX = worldPt.x - originWorld.x;
		float relY = worldPt.y - originWorld.y;
		float sx = originWorld.x + static_cast<float>(std::round(relX / step)) * step;
		float sy = originWorld.y + static_cast<float>(std::round(relY / step)) * step;
		return D2D1::Point2F(sx, sy);
	}

	void Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	            D2D1_SIZE_F view, float zoom, D2D1_POINT_2F pan);
};
