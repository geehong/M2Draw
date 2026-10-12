#include "pch.h"
#include "GridRenderer.h"
#include <cmath>

void GridRenderer::Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                          D2D1_SIZE_F view, float zoom, D2D1_POINT_2F pan)
{
	float worldStepPx = GetStepPx();
	if (!enabled || !rt || !brush || worldStepPx <= 0.0f || zoom <= 0.0f)
		return;

	float dpiX, dpiY;
	rt->GetDpi(&dpiX, &dpiY);
	const float s = dpiX / 96.0f;

	// 1. LOD: 줌아웃 시 화면 픽셀 간격이 minStepPx 미만이 되면 간격을 2배씩 묶음
	float stepW = worldStepPx;
	while (stepW * zoom < minStepPx)
	{
		stepW *= 2.0f;
	}
	const float stepPx = stepW * zoom;

	auto snap = [s](float v) {
		return (floorf(v * s) + 0.5f) / s;
	};

	const float lineW = 1.0f / s;
	const D2D1_COLOR_F savedColor = brush->GetColor();

	// 2. 월드 인덱스 i 기준 격자선 그리기
	auto pass = [&](float panAxis, float extent, bool vertical) {
		int i0 = static_cast<int>(ceilf(-panAxis / stepPx));
		int i1 = static_cast<int>(floorf((extent - panAxis) / stepPx));

		for (int i = i0; i <= i1; ++i)
		{
			float p = snap(panAxis + i * stepPx);
			bool isMajor = (((i % majorEvery) + majorEvery) % majorEvery == 0);

			brush->SetColor(isMajor ? major : minor);

			if (vertical)
			{
				rt->DrawLine(D2D1::Point2F(p, 0.0f), D2D1::Point2F(p, view.height), brush, lineW);
			}
			else
			{
				rt->DrawLine(D2D1::Point2F(0.0f, p), D2D1::Point2F(view.width, p), brush, lineW);
			}
		}
	};

	pass(pan.x, view.width,  true);  // 세로선
	pass(pan.y, view.height, false); // 가로선

	brush->SetColor(savedColor);
}
