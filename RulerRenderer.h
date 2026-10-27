#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include "GridRenderer.h"
#pragma comment(lib, "dwrite.lib")

class RulerRenderer
{
public:
	bool         enabled;
	float        thickness;   // 눈금자 두께 (기본 22.0 DIP)
	D2D1_COLOR_F bgColor;     // 눈금자 배경색
	D2D1_COLOR_F borderColor; // 테두리 구분선 색상
	D2D1_COLOR_F tickColor;   // 눈금선 색상
	D2D1_COLOR_F textColor;   // 텍스트 숫자 색상

private:
	IDWriteFactory*    m_pDWriteFactory;
	IDWriteTextFormat* m_pTextFormat;
	IDWriteTextFormat* m_pTextFormatBold;

public:
	RulerRenderer();
	~RulerRenderer();

	void DiscardDeviceResources();

	void Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
	            D2D1_SIZE_F view, float zoom, D2D1_POINT_2F pan,
	            const GridRenderer& grid, float docH = 0.0f, bool isBottomLeft = false);
};
