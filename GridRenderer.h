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

class GridRenderer
{
public:
	bool         enabled;
	bool         snapToGrid;   // 그리드 스냅 활성화 여부
	CanvasUnit   unit;
	float        stepInUnit;   // 선택한 단위 기준의 간격 (예: 20px, 10mm, 1in, 1rem)
	int          majorEvery;   // N번째마다 주(Major) 격자선
	float        minStepPx;    // LOD 화면 최소 픽셀 (기본 8.0px)
	D2D1_COLOR_F minor;
	D2D1_COLOR_F major;

	GridRenderer()
		: enabled(true)
		, snapToGrid(false)
		, unit(CanvasUnit::Pixel)
		, stepInUnit(20.0f)
		, majorEvery(5)
		, minStepPx(8.0f)
		, minor(D2D1::ColorF(0.75f, 0.78f, 0.88f, 0.5f))
		, major(D2D1::ColorF(0.60f, 0.64f, 0.80f, 0.8f))
	{
	}

	// 실제 월드 px 단위 간격 계산
	float GetStepPx() const
	{
		return stepInUnit * GetPixelsPerCanvasUnit(unit);
	}

	// 월드 좌표를 그리드 간격에 맞게 스냅 처리
	D2D1_POINT_2F SnapPoint(D2D1_POINT_2F worldPt) const
	{
		if (!snapToGrid) return worldPt;
		float step = GetStepPx();
		if (step <= 0.001f) return worldPt;

		float sx = static_cast<float>(std::round(worldPt.x / step)) * step;
		float sy = static_cast<float>(std::round(worldPt.y / step)) * step;
		return D2D1::Point2F(sx, sy);
	}

	void Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	            D2D1_SIZE_F view, float zoom, D2D1_POINT_2F pan);
};
