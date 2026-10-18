#include "pch.h"
#include "D2DColorWheel.h"
#include <cmath>
#include <algorithm>

CD2DColorWheel::CD2DColorWheel()
	: m_fHue(0.0f)
	, m_fSat(1.0f)
	, m_fLight(0.5f)
	, m_isDraggingWheel(false)
	, m_isDraggingTriangle(false)
{
}

D2D1_COLOR_F CD2DColorWheel::HSLToRGB(float h, float s, float l, float a)
{
	float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
	float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
	float m = l - c / 2.0f;
	float r = 0, g = 0, b = 0;

	if (h >= 0 && h < 60)        { r = c; g = x; b = 0; }
	else if (h >= 60 && h < 120) { r = x; g = c; b = 0; }
	else if (h >= 120 && h < 180){ r = 0; g = c; b = x; }
	else if (h >= 180 && h < 240){ r = 0; g = x; b = c; }
	else if (h >= 240 && h < 300){ r = x; g = 0; b = c; }
	else                         { r = c; g = 0; b = x; }

	return D2D1::ColorF(r + m, g + m, b + m, a);
}

void CD2DColorWheel::RGBToHSL(const D2D1_COLOR_F& clr, float& h, float& s, float& l)
{
	float r = clr.r, g = clr.g, b = clr.b;
	float maxVal = (std::max)({ r, g, b });
	float minVal = (std::min)({ r, g, b });
	float delta = maxVal - minVal;

	l = (maxVal + minVal) * 0.5f;

	if (delta < 0.0001f)
	{
		h = 0.0f;
		s = 0.0f;
		return;
	}

	s = (l > 0.5f) ? (delta / (2.0f - maxVal - minVal)) : (delta / (maxVal + minVal));

	if (maxVal == r)
		h = 60.0f * std::fmod((g - b) / delta, 6.0f);
	else if (maxVal == g)
		h = 60.0f * (((b - r) / delta) + 2.0f);
	else
		h = 60.0f * (((r - g) / delta) + 4.0f);

	if (h < 0.0f) h += 360.0f;
}

void CD2DColorWheel::SetColorHSL(float hue, float sat, float light)
{
	m_fHue = std::fmod(hue, 360.0f);
	if (m_fHue < 0.0f) m_fHue += 360.0f;
	m_fSat = (std::max)(0.0f, (std::min)(1.0f, sat));
	m_fLight = (std::max)(0.0f, (std::min)(1.0f, light));
}

void CD2DColorWheel::SetColorRGB(const D2D1_COLOR_F& clr)
{
	RGBToHSL(clr, m_fHue, m_fSat, m_fLight);
}

D2D1_COLOR_F CD2DColorWheel::GetRGB(float alpha) const
{
	return HSLToRGB(m_fHue, m_fSat, m_fLight, alpha);
}

void CD2DColorWheel::GetTriangleVertices(D2D1_POINT_2F center, float radius, float hue,
	D2D1_POINT_2F& ptHue, D2D1_POINT_2F& ptWhite, D2D1_POINT_2F& ptBlack)
{
	float radHue = static_cast<float>(hue * (M_PI / 180.0));
	float radWhite = radHue + static_cast<float>((2.0 * M_PI) / 3.0);
	float radBlack = radHue - static_cast<float>((2.0 * M_PI) / 3.0);

	ptHue = D2D1::Point2F(center.x + std::cos(radHue) * radius, center.y + std::sin(radHue) * radius);
	ptWhite = D2D1::Point2F(center.x + std::cos(radWhite) * radius, center.y + std::sin(radWhite) * radius);
	ptBlack = D2D1::Point2F(center.x + std::cos(radBlack) * radius, center.y + std::sin(radBlack) * radius);
}

void CD2DColorWheel::CalcBarycentric(D2D1_POINT_2F pt, D2D1_POINT_2F pA, D2D1_POINT_2F pB, D2D1_POINT_2F pC,
	float& wA, float& wB, float& wC)
{
	float det = (pB.y - pC.y) * (pA.x - pC.x) + (pC.x - pB.x) * (pA.y - pC.y);
	if (std::abs(det) < 0.0001f) { wA = 0.33f; wB = 0.33f; wC = 0.34f; return; }

	wA = ((pB.y - pC.y) * (pt.x - pC.x) + (pC.x - pB.x) * (pt.y - pC.y)) / det;
	wB = ((pC.y - pA.y) * (pt.x - pC.x) + (pA.x - pC.x) * (pt.y - pC.y)) / det;
	wC = 1.0f - wA - wB;

	if (wA < 0.0f) wA = 0.0f;
	if (wB < 0.0f) wB = 0.0f;
	if (wC < 0.0f) wC = 0.0f;
	float sum = wA + wB + wC;
	if (sum > 0.0001f)
	{
		wA /= sum;
		wB /= sum;
		wC /= sum;
	}
}

void CD2DColorWheel::BarycentricToSL(float wHue, float wWhite, float wBlack, float& sat, float& light)
{
	light = wWhite * 1.0f + wHue * 0.5f + wBlack * 0.0f;
	if (light < 0.0f) light = 0.0f;
	if (light > 1.0f) light = 1.0f;

	float maxSatLight = 1.0f - std::abs(2.0f * light - 1.0f);
	if (maxSatLight > 0.0001f)
	{
		sat = wHue / maxSatLight;
		if (sat < 0.0f) sat = 0.0f;
		if (sat > 1.0f) sat = 1.0f;
	}
	else
	{
		sat = 0.0f;
	}
}

void CD2DColorWheel::SLToBarycentric(float sat, float light, float& wHue, float& wWhite, float& wBlack)
{
	float c = (1.0f - std::abs(2.0f * light - 1.0f)) * sat;
	wHue = c;
	if (wHue < 0.0f) wHue = 0.0f;
	if (wHue > 1.0f) wHue = 1.0f;

	wWhite = light - 0.5f * wHue;
	if (wWhite < 0.0f) wWhite = 0.0f;
	if (wWhite > 1.0f) wWhite = 1.0f;

	wBlack = 1.0f - wHue - wWhite;
	if (wBlack < 0.0f) wBlack = 0.0f;
	if (wBlack > 1.0f) wBlack = 1.0f;

	float sum = wHue + wWhite + wBlack;
	if (sum > 0.0001f)
	{
		wHue /= sum;
		wWhite /= sum;
		wBlack /= sum;
	}
}

void CD2DColorWheel::Draw(ID2D1RenderTarget* pRenderTarget, ID2D1SolidColorBrush* pBrush, ID2D1Factory* pD2DFactory,
	D2D1_POINT_2F center, float radiusOuter, float ringThickness)
{
	if (!pRenderTarget || !pBrush || !pD2DFactory) return;

	float radiusInner = radiusOuter - ringThickness;
	if (radiusInner <= 10.0f) radiusInner = 10.0f;

	// 1. 외곽 Hue 링 (360개 1도 단위 초미세 렌더링)
	const int segments = 360;
	const float stepRad = static_cast<float>((2.0 * M_PI) / segments);

	for (int i = 0; i < segments; ++i)
	{
		float a1 = i * stepRad;
		float a2 = (i + 1.15f) * stepRad;
		float hueMid = (i + 0.5f) * (360.0f / segments);

		D2D1_POINT_2F p1 = D2D1::Point2F(center.x + std::cos(a1) * radiusOuter, center.y + std::sin(a1) * radiusOuter);
		D2D1_POINT_2F p2 = D2D1::Point2F(center.x + std::cos(a2) * radiusOuter, center.y + std::sin(a2) * radiusOuter);
		D2D1_POINT_2F p3 = D2D1::Point2F(center.x + std::cos(a2) * radiusInner, center.y + std::sin(a2) * radiusInner);
		D2D1_POINT_2F p4 = D2D1::Point2F(center.x + std::cos(a1) * radiusInner, center.y + std::sin(a1) * radiusInner);

		ID2D1PathGeometry* pGeo = nullptr;
		pD2DFactory->CreatePathGeometry(&pGeo);
		if (pGeo)
		{
			ID2D1GeometrySink* pSink = nullptr;
			pGeo->Open(&pSink);
			if (pSink)
			{
				pSink->BeginFigure(p1, D2D1_FIGURE_BEGIN_FILLED);
				pSink->AddLine(p2);
				pSink->AddLine(p3);
				pSink->AddLine(p4);
				pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
				pSink->Close();
				pSink->Release();

				pBrush->SetColor(HSLToRGB(hueMid, 1.0f, 0.5f));
				pRenderTarget->FillGeometry(pGeo, pBrush);
			}
			pGeo->Release();
		}
	}

	// 외곽/내곽 링 테두리선
	pBrush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.9f));
	pRenderTarget->DrawEllipse(D2D1::Ellipse(center, radiusOuter, radiusOuter), pBrush, 1.0f);
	pRenderTarget->DrawEllipse(D2D1::Ellipse(center, radiusInner, radiusInner), pBrush, 1.0f);

	// 2. 내부 삼각형
	D2D1_POINT_2F ptHue, ptWhite, ptBlack;
	GetTriangleVertices(center, radiusInner, m_fHue, ptHue, ptWhite, ptBlack);

	ID2D1PathGeometry* pTriGeo = nullptr;
	pD2DFactory->CreatePathGeometry(&pTriGeo);
	if (pTriGeo)
	{
		ID2D1GeometrySink* pSink = nullptr;
		pTriGeo->Open(&pSink);
		if (pSink)
		{
			pSink->BeginFigure(ptHue, D2D1_FIGURE_BEGIN_FILLED);
			pSink->AddLine(ptWhite);
			pSink->AddLine(ptBlack);
			pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
			pSink->Close();
			pSink->Release();

			D2D1_POINT_2F ptBaseMid = D2D1::Point2F((ptWhite.x + ptBlack.x) * 0.5f, (ptWhite.y + ptBlack.y) * 0.5f);

			// 1차 리니어 그라데이션: Base -> Hue
			ID2D1GradientStopCollection* pStops = nullptr;
			D2D1_GRADIENT_STOP stops[2];
			stops[0].position = 0.0f;
			stops[0].color = D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f);
			stops[1].position = 1.0f;
			stops[1].color = HSLToRGB(m_fHue, 1.0f, 0.5f);

			pRenderTarget->CreateGradientStopCollection(stops, 2, &pStops);
			if (pStops)
			{
				ID2D1LinearGradientBrush* pLinBrush = nullptr;
				pRenderTarget->CreateLinearGradientBrush(
					D2D1::LinearGradientBrushProperties(ptBaseMid, ptHue),
					pStops,
					&pLinBrush
				);
				if (pLinBrush)
				{
					pRenderTarget->FillGeometry(pTriGeo, pLinBrush);
					pLinBrush->Release();
				}
				pStops->Release();
			}

			// 2차 리니어 그라데이션: Black -> White
			ID2D1GradientStopCollection* pStopsBW = nullptr;
			D2D1_GRADIENT_STOP stopsBW[2];
			stopsBW[0].position = 0.0f;
			stopsBW[0].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f);
			stopsBW[1].position = 1.0f;
			stopsBW[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f);

			pRenderTarget->CreateGradientStopCollection(stopsBW, 2, &pStopsBW);
			if (pStopsBW)
			{
				ID2D1LinearGradientBrush* pLinBrushBW = nullptr;
				pRenderTarget->CreateLinearGradientBrush(
					D2D1::LinearGradientBrushProperties(ptBlack, ptWhite),
					pStopsBW,
					&pLinBrushBW
				);
				if (pLinBrushBW)
				{
					pRenderTarget->FillGeometry(pTriGeo, pLinBrushBW);
					pLinBrushBW->Release();
				}
				pStopsBW->Release();
			}

			// 삼각형 외곽선
			pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.9f));
			pRenderTarget->DrawGeometry(pTriGeo, pBrush, 1.2f);
		}
		pTriGeo->Release();
	}

	// 3. 삼각형 내부 채도/명도 핸들
	float wHue, wWhite, wBlack;
	SLToBarycentric(m_fSat, m_fLight, wHue, wWhite, wBlack);
	D2D1_POINT_2F triHandlePt = D2D1::Point2F(
		wHue * ptHue.x + wWhite * ptWhite.x + wBlack * ptBlack.x,
		wHue * ptHue.y + wWhite * ptWhite.y + wBlack * ptBlack.y
	);

	pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
	pRenderTarget->DrawEllipse(D2D1::Ellipse(triHandlePt, 7.5f, 7.5f), pBrush, 2.0f);

	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	pRenderTarget->DrawEllipse(D2D1::Ellipse(triHandlePt, 6.5f, 6.5f), pBrush, 1.5f);

	D2D1_COLOR_F curHandleColor = HSLToRGB(m_fHue, m_fSat, m_fLight);
	pBrush->SetColor(curHandleColor);
	pRenderTarget->FillEllipse(D2D1::Ellipse(triHandlePt, 5.0f, 5.0f), pBrush);

	// 4. 외곽 링 Hue 핸들
	float radTarget = static_cast<float>(m_fHue * (M_PI / 180.0));
	float midR = (radiusOuter + radiusInner) * 0.5f;
	D2D1_POINT_2F handlePt = D2D1::Point2F(center.x + std::cos(radTarget) * midR, center.y + std::sin(radTarget) * midR);

	D2D1_POINT_2F pOuter = D2D1::Point2F(center.x + std::cos(radTarget) * (radiusOuter + 1.0f), center.y + std::sin(radTarget) * (radiusOuter + 1.0f));
	D2D1_POINT_2F pInner = D2D1::Point2F(center.x + std::cos(radTarget) * (radiusInner - 1.0f), center.y + std::sin(radTarget) * (radiusInner - 1.0f));
	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f));
	pRenderTarget->DrawLine(pInner, pOuter, pBrush, 2.0f);

	pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
	pRenderTarget->DrawEllipse(D2D1::Ellipse(handlePt, 7.5f, 7.5f), pBrush, 2.0f);
	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	pRenderTarget->DrawEllipse(D2D1::Ellipse(handlePt, 6.5f, 6.5f), pBrush, 1.5f);
	pBrush->SetColor(HSLToRGB(m_fHue, 1.0f, 0.5f));
	pRenderTarget->FillEllipse(D2D1::Ellipse(handlePt, 5.0f, 5.0f), pBrush);
}

bool CD2DColorWheel::OnLButtonDown(CPoint point, D2D1_POINT_2F center, float radiusOuter, float ringThickness)
{
	float dx = point.x - center.x;
	float dy = point.y - center.y;
	float dist = std::sqrt(dx * dx + dy * dy);
	float radiusInner = radiusOuter - ringThickness;

	// Hue 링 클릭
	if (dist >= radiusInner && dist <= radiusOuter + 8.0f)
	{
		m_isDraggingWheel = true;
		float angle = std::atan2(dy, dx) * (180.0f / 3.14159265f);
		if (angle < 0.0f) angle += 360.0f;
		m_fHue = angle;

		if (m_onColorChanged)
			m_onColorChanged(m_fHue, m_fSat, m_fLight, false);

		return true;
	}
	// 내부 삼각형 클릭
	else if (dist < radiusInner)
	{
		m_isDraggingTriangle = true;
		D2D1_POINT_2F ptHue, ptWhite, ptBlack;
		GetTriangleVertices(center, radiusInner, m_fHue, ptHue, ptWhite, ptBlack);

		float wA, wB, wC;
		CalcBarycentric(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)), ptHue, ptWhite, ptBlack, wA, wB, wC);

		BarycentricToSL(wA, wB, wC, m_fSat, m_fLight);

		if (m_onColorChanged)
			m_onColorChanged(m_fHue, m_fSat, m_fLight, false);

		return true;
	}

	return false;
}

bool CD2DColorWheel::OnMouseMove(CPoint point, D2D1_POINT_2F center, float radiusOuter, float ringThickness)
{
	if (!m_isDraggingWheel && !m_isDraggingTriangle)
		return false;

	float radiusInner = radiusOuter - ringThickness;

	if (m_isDraggingWheel)
	{
		float dx = point.x - center.x;
		float dy = point.y - center.y;
		float angle = std::atan2(dy, dx) * (180.0f / 3.14159265f);
		if (angle < 0.0f) angle += 360.0f;
		m_fHue = angle;

		if (m_onColorChanged)
			m_onColorChanged(m_fHue, m_fSat, m_fLight, false);

		return true;
	}
	else if (m_isDraggingTriangle)
	{
		D2D1_POINT_2F ptHue, ptWhite, ptBlack;
		GetTriangleVertices(center, radiusInner, m_fHue, ptHue, ptWhite, ptBlack);

		float wA, wB, wC;
		CalcBarycentric(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)), ptHue, ptWhite, ptBlack, wA, wB, wC);

		BarycentricToSL(wA, wB, wC, m_fSat, m_fLight);

		if (m_onColorChanged)
			m_onColorChanged(m_fHue, m_fSat, m_fLight, false);

		return true;
	}

	return false;
}

bool CD2DColorWheel::OnLButtonUp()
{
	if (m_isDraggingWheel || m_isDraggingTriangle)
	{
		m_isDraggingWheel = false;
		m_isDraggingTriangle = false;

		if (m_onColorChanged)
			m_onColorChanged(m_fHue, m_fSat, m_fLight, true);

		return true;
	}
	return false;
}

void CD2DColorWheel::DrawSlider(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, IDWriteTextFormat* pTextFmt,
	const wchar_t* label, float left, float right, float& y, float valNorm, D2D1_COLOR_F accentClr)
{
	if (!pRT || !pBrush) return;

	valNorm = (std::max)(0.0f, (std::min)(1.0f, valNorm));

	if (label && pTextFmt)
	{
		pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.88f));
		pRT->DrawText(label, (UINT32)wcslen(label), pTextFmt, D2D1::RectF(left, y, right, y + 18.0f), pBrush);
		y += 20.0f;
	}

	float sliderW = right - left;
	D2D1_RECT_F trackRect = D2D1::RectF(left, y + 3.0f, right, y + 9.0f);
	pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f));
	pRT->FillRoundedRectangle(D2D1::RoundedRect(trackRect, 3.0f, 3.0f), pBrush);

	float fillX = left + valNorm * sliderW;
	D2D1_RECT_F fillRect = D2D1::RectF(left, y + 3.0f, fillX, y + 9.0f);
	pBrush->SetColor(accentClr);
	pRT->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 3.0f, 3.0f), pBrush);

	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, y + 6.0f), 7.0f, 7.0f), pBrush);
	pBrush->SetColor(accentClr);
	pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, y + 6.0f), 7.0f, 7.0f), pBrush, 1.5f);
	y += 26.0f;
}

bool CD2DColorWheel::HitTestSlider(CPoint pt, float left, float right, float y, float& outValNorm)
{
	if (pt.y >= y - 5.0f && pt.y <= y + 20.0f)
	{
		float sliderW = right - left;
		if (sliderW <= 0.0f) return false;
		float val = (static_cast<float>(pt.x) - left) / sliderW;
		outValNorm = (std::max)(0.0f, (std::min)(1.0f, val));
		return true;
	}
	return false;
}

void CD2DColorWheel::DrawColorBar(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
	float left, float right, float& y, float height, D2D1_COLOR_F color)
{
	if (!pRT || !pBrush) return;

	D2D1_RECT_F rect = D2D1::RectF(left, y, right, y + height);
	pBrush->SetColor(color);
	pRT->FillRoundedRectangle(D2D1::RoundedRect(rect, 3.0f, 3.0f), pBrush);
	pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
	pRT->DrawRoundedRectangle(D2D1::RoundedRect(rect, 3.0f, 3.0f), pBrush, 1.0f);
	y += height + 6.0f;
}

void CD2DColorWheel::DrawSegmentButtons(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, IDWriteTextFormat* pTextFmt,
	const wchar_t* const labels[], int count, int selectedIdx, float left, float right, float& y, float height)
{
	if (!pRT || !pBrush || count <= 0) return;

	float spacing = 4.0f;
	float totalW = right - left;
	float btnW = (totalW - (count - 1) * spacing) / static_cast<float>(count);

	for (int i = 0; i < count; ++i)
	{
		float bx1 = left + i * (btnW + spacing);
		float bx2 = bx1 + btnW;
		D2D1_RECT_F rBtn = D2D1::RectF(bx1, y, bx2, y + height);

		bool isSel = (selectedIdx == i);
		pBrush->SetColor(isSel ? D2D1::ColorF(0.18f, 0.52f, 0.95f) : D2D1::ColorF(0.18f, 0.18f, 0.22f));
		pRT->FillRoundedRectangle(D2D1::RoundedRect(rBtn, 3.0f, 3.0f), pBrush);

		pBrush->SetColor(isSel ? D2D1::ColorF(0.3f, 0.7f, 1.0f) : D2D1::ColorF(0.32f, 0.32f, 0.36f));
		pRT->DrawRoundedRectangle(D2D1::RoundedRect(rBtn, 3.0f, 3.0f), pBrush, isSel ? 1.5f : 1.0f);

		if (labels && labels[i] && pTextFmt)
		{
			pBrush->SetColor(isSel ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.70f, 0.70f, 0.75f));
			pRT->DrawText(labels[i], (UINT32)wcslen(labels[i]), pTextFmt, rBtn, pBrush);
		}
	}
	y += height + 8.0f;
}

int CD2DColorWheel::HitTestSegmentButtons(CPoint pt, int count, float left, float right, float y, float height)
{
	if (count <= 0) return -1;
	if (pt.y < y || pt.y > y + height) return -1;

	float spacing = 4.0f;
	float totalW = right - left;
	float btnW = (totalW - (count - 1) * spacing) / static_cast<float>(count);

	for (int i = 0; i < count; ++i)
	{
		float bx1 = left + i * (btnW + spacing);
		float bx2 = bx1 + btnW;
		if (pt.x >= bx1 && pt.x <= bx2)
			return i;
	}
	return -1;
}
