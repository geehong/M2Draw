#include "pch.h"
#include "D2DTestView.h"
#include "M2DrawDoc.h"
#include "M2Draw.h"
#include "D2DVectorEngine.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
	// HSL(0~360, 0~1, 0~1)을 RGB로 변환하는 함수
	D2D1_COLOR_F HSLtoRGB(float h, float s, float l, float a = 1.0f)
	{
		float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
		float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
		float m = l - c / 2.0f;
		float r = 0, g = 0, b = 0;

		if (h >= 0 && h < 60)       { r = c; g = x; b = 0; }
		else if (h >= 60 && h < 120) { r = x; g = c; b = 0; }
		else if (h >= 120 && h < 180){ r = 0; g = c; b = x; }
		else if (h >= 180 && h < 240){ r = 0; g = x; b = c; }
		else if (h >= 240 && h < 300){ r = x; g = 0; b = c; }
		else                         { r = c; g = 0; b = x; }

		return D2D1::ColorF(r + m, g + m, b + m, a);
	}

	void RGBtoHSL(const D2D1_COLOR_F& clr, float& h, float& s, float& l)
	{
		float r = clr.r, g = clr.g, b = clr.b;
		float maxVal = max(r, max(g, b));
		float minVal = min(r, min(g, b));
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

	float RGBtoHue(const D2D1_COLOR_F& clr)
	{
		float h, s, l;
		RGBtoHSL(clr, h, s, l);
		return h;
	}

	// 삼각형 꼭짓점 계산: 꼭짓점(ptHue), ptWhite, ptBlack
	void GetTriangleVertices(D2D1_POINT_2F center, float radiusInner, float activeHue,
		D2D1_POINT_2F& ptHue, D2D1_POINT_2F& ptWhite, D2D1_POINT_2F& ptBlack)
	{
		float triR = radiusInner - 4.0f;
		float radHue = static_cast<float>(activeHue * (M_PI / 180.0));
		ptHue = D2D1::Point2F(center.x + std::cos(radHue) * triR, center.y + std::sin(radHue) * triR);
		ptWhite = D2D1::Point2F(center.x + std::cos(radHue + 2.0943951f) * triR, center.y + std::sin(radHue + 2.0943951f) * triR);
		ptBlack = D2D1::Point2F(center.x + std::cos(radHue + 4.1887902f) * triR, center.y + std::sin(radHue + 4.1887902f) * triR);
	}

	// 점 pt에서 삼각형 가중치(wHue, wWhite, wBlack) 계산 (클램프 지원)
	void CalcBarycentric(D2D1_POINT_2F pt, D2D1_POINT_2F pA, D2D1_POINT_2F pB, D2D1_POINT_2F pC,
		float& wA, float& wB, float& wC)
	{
		float det = (pB.y - pC.y) * (pA.x - pC.x) + (pC.x - pB.x) * (pA.y - pC.y);
		if (std::abs(det) < 0.0001f) { wA = 0.33f; wB = 0.33f; wC = 0.34f; return; }

		wA = ((pB.y - pC.y) * (pt.x - pC.x) + (pC.x - pB.x) * (pt.y - pC.y)) / det;
		wB = ((pC.y - pA.y) * (pt.x - pC.x) + (pA.x - pC.x) * (pt.y - pC.y)) / det;
		wC = 1.0f - wA - wB;

		// 삼각형 내부로 클램프
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

	// wHue, wWhite, wBlack 가중치로부터 Saturation과 Lightness 역계산
	void BarycentricToSL(float wHue, float wWhite, float wBlack, float& sat, float& light)
	{
		// ptWhite: L=1.0, S=0.0
		// ptBlack: L=0.0, S=0.0
		// ptHue:   L=0.5, S=1.0
		light = wWhite * 1.0f + wHue * 0.5f + wBlack * 0.0f;
		if (light < 0.0f) light = 0.0f;
		if (light > 1.0f) light = 1.0f;

		// 순수 Hue 비중에 따른 채도
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

	// Saturation과 Lightness로부터 삼각형 가중치 wHue, wWhite, wBlack 계산
	void SLToBarycentric(float sat, float light, float& wHue, float& wWhite, float& wBlack)
	{
		// C = (1 - |2L - 1|) * S
		float c = (1.0f - std::abs(2.0f * light - 1.0f)) * sat;
		wHue = c;
		if (wHue < 0.0f) wHue = 0.0f;
		if (wHue > 1.0f) wHue = 1.0f;

		// L = wWhite + 0.5 * wHue
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
}

BEGIN_MESSAGE_MAP(CD2DTestView, CWnd)
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

CD2DTestView::CD2DTestView()
{
}

void CD2DTestView::SetTargetShape(CM2DrawDoc* pDoc, int shapeIndex)
{
	m_pActiveDoc = pDoc;
	m_selectedShapeIndex = shapeIndex;

	if (pDoc && shapeIndex >= 0 && shapeIndex < static_cast<int>(pDoc->GetShapes().size()))
	{
		Shape* shape = pDoc->GetShapes()[shapeIndex].get();
		m_strShapeType = shape->GetShapeTypeName();
		m_strShapeName = shape->name.IsEmpty() ? m_strShapeType : shape->name;

		// 1. 채움 여부 및 채움 색상
		m_bFillEnabled = (shape->style.fillType != FillType::None);
		m_fillColor = shape->style.fill;
		RGBtoHSL(m_fillColor, m_fFillHue, m_fFillSat, m_fFillLight);

		// 2. 선 굵기 및 선 색상
		m_fStrokeWidth = shape->style.strokeWidth;
		m_strokeColor = shape->style.stroke;
		RGBtoHSL(m_strokeColor, m_fStrokeHue, m_fStrokeSat, m_fStrokeLight);

		// 기본 불투명도 및 포커스 색상
		m_fOpacity = (m_colorTarget == 0 ? m_strokeColor.a : m_fillColor.a) * 100.0f;

		// 3. 대시 스타일
		switch (shape->style.dashStyle)
		{
		case D2D1_DASH_STYLE_DASH:      m_selectedDash = 1; break;
		case D2D1_DASH_STYLE_DOT:       m_selectedDash = 2; break;
		case D2D1_DASH_STYLE_DASH_DOT:  m_selectedDash = 3; break;
		default:                         m_selectedDash = 0; break;
		}
	}
	else
	{
		m_strShapeName = _T("선택된 도형 없음");
		m_strShapeType.Empty();
	}

	Invalidate(FALSE);
}

void CD2DTestView::ApplyToShape()
{
	if (!m_pActiveDoc || m_selectedShapeIndex < 0 || m_selectedShapeIndex >= static_cast<int>(m_pActiveDoc->GetShapes().size()))
		return;

	Shape* shape = m_pActiveDoc->GetShapes()[m_selectedShapeIndex].get();

	// 1. 채움 적용
	if (m_bFillEnabled)
	{
		shape->style.fillType = FillType::Solid;
		shape->style.fill = m_fillColor;
	}
	else
	{
		shape->style.fillType = FillType::None;
	}

	// 2. 선 색상 및 선 굵기 적용
	shape->style.stroke = m_strokeColor;
	shape->style.strokeWidth = m_fStrokeWidth;

	// 3. 선 대시 스타일 적용
	switch (m_selectedDash)
	{
	case 1: shape->style.dashStyle = D2D1_DASH_STYLE_DASH; break;
	case 2: shape->style.dashStyle = D2D1_DASH_STYLE_DOT; break;
	case 3: shape->style.dashStyle = D2D1_DASH_STYLE_DASH_DOT; break;
	default: shape->style.dashStyle = D2D1_DASH_STYLE_SOLID; break;
	}

	shape->Invalidate();
	m_pActiveDoc->UpdateAllViews(nullptr);
}

void CD2DTestView::ApplyCanvasSettings()
{
	CWinApp* pApp = AfxGetApp();
	if (pApp)
	{
		POSITION posDocTemplate = pApp->GetFirstDocTemplatePosition();
		while (posDocTemplate)
		{
			CDocTemplate* pDocTemplate = pApp->GetNextDocTemplate(posDocTemplate);
			if (pDocTemplate)
			{
				POSITION posDoc = pDocTemplate->GetFirstDocPosition();
				while (posDoc)
				{
					CDocument* pDoc = pDocTemplate->GetNextDoc(posDoc);
					if (pDoc)
					{
						pDoc->UpdateAllViews(nullptr);
					}
				}
			}
		}
	}
}

CD2DTestView::~CD2DTestView()
{
	DiscardDeviceResources();
	if (m_pTextFormatCenter) m_pTextFormatCenter->Release();
	if (m_pTextFormatSmall) m_pTextFormatSmall->Release();
	if (m_pTextFormatBold) m_pTextFormatBold->Release();
	if (m_pTextFormat) m_pTextFormat->Release();
	if (m_pDWriteFactory) m_pDWriteFactory->Release();
	if (m_pD2DFactory) m_pD2DFactory->Release();
}

BOOL CD2DTestView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE; // 깜빡임 방지
}

HRESULT CD2DTestView::CreateDeviceResources()
{
	HRESULT hr = S_OK;

	if (!m_pD2DFactory)
	{
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
	}

	if (SUCCEEDED(hr) && !m_pDWriteFactory)
	{
		hr = DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormat && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			12.0f,
			L"ko-kr",
			&m_pTextFormat
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormatBold && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_BOLD,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			13.0f,
			L"ko-kr",
			&m_pTextFormatBold
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormatSmall && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			11.0f,
			L"ko-kr",
			&m_pTextFormatSmall
		);
	}

	if (SUCCEEDED(hr) && !m_pTextFormatCenter && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			11.0f,
			L"ko-kr",
			&m_pTextFormatCenter
		);
		if (m_pTextFormatCenter)
		{
			m_pTextFormatCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			m_pTextFormatCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}

	if (SUCCEEDED(hr) && !m_pRenderTarget)
	{
		CRect rc;
		GetClientRect(&rc);
		D2D1_SIZE_U size = D2D1::SizeU(max(1, rc.Width()), max(1, rc.Height()));

		D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(),
			96.0f, 96.0f
		);

		hr = m_pD2DFactory->CreateHwndRenderTarget(
			rtProps,
			D2D1::HwndRenderTargetProperties(m_hWnd, size),
			&m_pRenderTarget
		);

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pBrush);
		}
	}

	return hr;
}

void CD2DTestView::DiscardDeviceResources()
{
	if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
	if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
}

void CD2DTestView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	if (m_pRenderTarget)
	{
		m_pRenderTarget->Resize(D2D1::SizeU(cx, cy));
	}
	Invalidate(FALSE);
}

void CD2DTestView::DrawColorWheel(D2D1_POINT_2F center, float radiusOuter, float radiusInner, float activeHue, float activeSat, float activeLight)
{
	if (!m_pRenderTarget || !m_pBrush) return;

	// 1. 외곽 Hue 링 (360개 1도 단위 초미세 렌더링으로 끊김 없는 매끄러운 그라데이션)
	const int segments = 360;
	const float stepRad = static_cast<float>((2.0 * M_PI) / segments);

	for (int i = 0; i < segments; ++i)
	{
		float a1 = i * stepRad;
		float a2 = (i + 1.15f) * stepRad; // 틈새 없이 안티에일리어싱 매끄럽게 연결
		float hueMid = (i + 0.5f) * (360.0f / segments);

		D2D1_POINT_2F p1 = D2D1::Point2F(center.x + std::cos(a1) * radiusOuter, center.y + std::sin(a1) * radiusOuter);
		D2D1_POINT_2F p2 = D2D1::Point2F(center.x + std::cos(a2) * radiusOuter, center.y + std::sin(a2) * radiusOuter);
		D2D1_POINT_2F p3 = D2D1::Point2F(center.x + std::cos(a2) * radiusInner, center.y + std::sin(a2) * radiusInner);
		D2D1_POINT_2F p4 = D2D1::Point2F(center.x + std::cos(a1) * radiusInner, center.y + std::sin(a1) * radiusInner);

		ID2D1PathGeometry* pGeo = nullptr;
		m_pD2DFactory->CreatePathGeometry(&pGeo);
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

				m_pBrush->SetColor(HSLtoRGB(hueMid, 1.0f, 0.5f));
				m_pRenderTarget->FillGeometry(pGeo, m_pBrush);
			}
			pGeo->Release();
		}
	}

	// 외곽/내곽 링 다듬기 테두리선
	m_pBrush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.9f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(center, radiusOuter, radiusOuter), m_pBrush, 1.0f);
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(center, radiusInner, radiusInner), m_pBrush, 1.0f);

	// 2. 내부 삼각형: 꼭짓점(선택된 Hue 원색) -> 밑변(흰색과 검은색) 그라데이션
	D2D1_POINT_2F ptHue, ptWhite, ptBlack;
	GetTriangleVertices(center, radiusInner, activeHue, ptHue, ptWhite, ptBlack);

	ID2D1PathGeometry* pTriGeo = nullptr;
	m_pD2DFactory->CreatePathGeometry(&pTriGeo);
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

			// 밑변 중심 (White와 Black의 중간)
			D2D1_POINT_2F ptBaseMid = D2D1::Point2F((ptWhite.x + ptBlack.x) * 0.5f, (ptWhite.y + ptBlack.y) * 0.5f);

			// 1차 리니어 그라데이션: 밑변(중간 회색) -> 꼭짓점(Hue 원색)
			ID2D1GradientStopCollection* pStops = nullptr;
			D2D1_GRADIENT_STOP stops[2];
			stops[0].position = 0.0f;
			stops[0].color = D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f);
			stops[1].position = 1.0f;
			stops[1].color = HSLtoRGB(activeHue, 1.0f, 0.5f);

			m_pRenderTarget->CreateGradientStopCollection(stops, 2, &pStops);
			if (pStops)
			{
				ID2D1LinearGradientBrush* pLinBrush = nullptr;
				m_pRenderTarget->CreateLinearGradientBrush(
					D2D1::LinearGradientBrushProperties(ptBaseMid, ptHue),
					pStops,
					&pLinBrush
				);
				if (pLinBrush)
				{
					m_pRenderTarget->FillGeometry(pTriGeo, pLinBrush);
					pLinBrush->Release();
				}
				pStops->Release();
			}

			// 2차 리니어 그라데이션 (Black -> White 명암 블렌딩)
			ID2D1GradientStopCollection* pStopsBW = nullptr;
			D2D1_GRADIENT_STOP stopsBW[2];
			stopsBW[0].position = 0.0f;
			stopsBW[0].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f);
			stopsBW[1].position = 1.0f;
			stopsBW[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.85f);

			m_pRenderTarget->CreateGradientStopCollection(stopsBW, 2, &pStopsBW);
			if (pStopsBW)
			{
				ID2D1LinearGradientBrush* pLinBrushBW = nullptr;
				m_pRenderTarget->CreateLinearGradientBrush(
					D2D1::LinearGradientBrushProperties(ptBlack, ptWhite),
					pStopsBW,
					&pLinBrushBW
				);
				if (pLinBrushBW)
				{
					m_pRenderTarget->FillGeometry(pTriGeo, pLinBrushBW);
					pLinBrushBW->Release();
				}
				pStopsBW->Release();
			}

			// 삼각형 외곽선
			m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.9f));
			m_pRenderTarget->DrawGeometry(pTriGeo, m_pBrush, 1.2f);
		}
		pTriGeo->Release();
	}

	// 3. 내부 삼각형 채도/명도 선택 핸들 (피커 역활의 동그라미)
	float wHue, wWhite, wBlack;
	SLToBarycentric(activeSat, activeLight, wHue, wWhite, wBlack);
	D2D1_POINT_2F triHandlePt = D2D1::Point2F(
		wHue * ptHue.x + wWhite * ptWhite.x + wBlack * ptBlack.x,
		wHue * ptHue.y + wWhite * ptWhite.y + wBlack * ptBlack.y
	);

	// 바깥 그림자 테두리 (블랙 외곽)
	m_pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(triHandlePt, 7.5f, 7.5f), m_pBrush, 2.0f);

	// 메인 흰색 링
	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(triHandlePt, 6.5f, 6.5f), m_pBrush, 1.5f);

	// 중심 현재 색상 채움 원
	D2D1_COLOR_F curHandleColor = HSLtoRGB(activeHue, activeSat, activeLight);
	m_pBrush->SetColor(curHandleColor);
	m_pRenderTarget->FillEllipse(D2D1::Ellipse(triHandlePt, 5.0f, 5.0f), m_pBrush);

	// 4. 외곽 링 위의 현재 선택 Hue 핸들 (피커 동그라미)
	float radTarget = static_cast<float>(activeHue * (M_PI / 180.0));
	float midR = (radiusOuter + radiusInner) * 0.5f;
	D2D1_POINT_2F handlePt = D2D1::Point2F(center.x + std::cos(radTarget) * midR, center.y + std::sin(radTarget) * midR);

	// 링을 관통하는 슬릿 라인
	D2D1_POINT_2F pOuter = D2D1::Point2F(center.x + std::cos(radTarget) * (radiusOuter + 1.0f), center.y + std::sin(radTarget) * (radiusOuter + 1.0f));
	D2D1_POINT_2F pInner = D2D1::Point2F(center.x + std::cos(radTarget) * (radiusInner - 1.0f), center.y + std::sin(radTarget) * (radiusInner - 1.0f));
	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f));
	m_pRenderTarget->DrawLine(pInner, pOuter, m_pBrush, 2.0f);

	// 외곽 링 원형 핀
	m_pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(handlePt, 7.5f, 7.5f), m_pBrush, 2.0f);
	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(handlePt, 6.5f, 6.5f), m_pBrush, 1.5f);
	m_pBrush->SetColor(HSLtoRGB(activeHue, 1.0f, 0.5f));
	m_pRenderTarget->FillEllipse(D2D1::Ellipse(handlePt, 5.0f, 5.0f), m_pBrush);
}

void CD2DTestView::DrawTabHeaders(float width, float& y)
{
	// 상단 탭 버튼 3개 (D2D Custom / ImGui Dark / MFC Style)
	float tabW = (width - 20.0f) / 3.0f;
	const wchar_t* tabTitles[] = { L"1. D2D Custom", L"2. ImGui Dark", L"3. MFC Classic" };

	for (int i = 0; i < 3; ++i)
	{
		D2D1_RECT_F tabRect = D2D1::RectF(10.0f + i * tabW, y, 10.0f + (i + 1) * tabW - 3.0f, y + 26.0f);
		bool isActive = (m_currentTab == i);

		if (isActive)
		{
			// 활성 탭: 하이라이트 색상 (D2D: 퍼플/블루, ImGui: 오렌지/다크차콜, MFC: 윈도우 블루)
			D2D1_COLOR_F activeClr = (i == 0) ? D2D1::ColorF(0.18f, 0.52f, 0.95f) :
			                         (i == 1) ? D2D1::ColorF(0.92f, 0.45f, 0.12f) :
			                                    D2D1::ColorF(0.0f, 0.47f, 0.84f);
			m_pBrush->SetColor(activeClr);
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tabRect, 4.0f, 4.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
			m_pRenderTarget->DrawText(tabTitles[i], (UINT32)wcslen(tabTitles[i]), m_pTextFormatBold, tabRect, m_pBrush);
		}
		else
		{
			// 비활성 탭
			m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.23f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tabRect, 4.0f, 4.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.38f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(tabRect, 4.0f, 4.0f), m_pBrush, 1.0f);
			m_pBrush->SetColor(D2D1::ColorF(0.65f, 0.65f, 0.68f));
			m_pRenderTarget->DrawText(tabTitles[i], (UINT32)wcslen(tabTitles[i]), m_pTextFormatSmall, tabRect, m_pBrush);
		}
	}
	y += 34.0f;
}

// -------------------------------------------------------------
// [1. Direct2D 탭] Inkscape/Illustrator 스타일 미려한 벡터 UI
// -------------------------------------------------------------
void CD2DTestView::DrawD2DContent(float width, float y)
{
	// 1. 배지 / 선택된 쉐이프 이름 및 상태
	CString strHeader;
	if (m_pActiveDoc && m_selectedShapeIndex >= 0)
	{
		strHeader.Format(_T("● 도형 연결: [%s]"), (LPCTSTR)m_strShapeName);
		m_pBrush->SetColor(D2D1::ColorF(0.2f, 0.85f, 0.4f));
	}
	else
	{
		strHeader = _T("● 도형 미선택 (프리뷰 모드)");
		m_pBrush->SetColor(D2D1::ColorF(0.7f, 0.7f, 0.7f));
	}
	m_pRenderTarget->DrawText(strHeader, (UINT32)strHeader.GetLength(), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 22.0f;

	// 2. Inkscape 스타일 상단 탭: [채움(Fill)] 이 먼저, [윤곽선(Stroke)] 이 뒤로
	// 잉크스케이프처럼 탭 하단 인디케이터 라인 포함
	float halfW = (width - 24.0f) * 0.5f;
	D2D1_RECT_F btnFill = D2D1::RectF(12.0f, y, 12.0f + halfW - 2.0f, y + 26.0f);
	D2D1_RECT_F btnStroke = D2D1::RectF(12.0f + halfW + 2.0f, y, width - 12.0f, y + 26.0f);

	bool isFillActive = (m_colorTarget == 0);

	// [채움(Fill)] 탭 버튼
	m_pBrush->SetColor(isFillActive ? D2D1::ColorF(0.18f, 0.52f, 0.95f) : D2D1::ColorF(0.20f, 0.20f, 0.23f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnFill, 4.0f, 4.0f), m_pBrush);
	m_pBrush->SetColor(isFillActive ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.7f, 0.7f, 0.7f));
	m_pRenderTarget->DrawText(L"■ 채움(Fill)", 9, m_pTextFormatCenter, btnFill, m_pBrush);

	// [윤곽선(Stroke)] 탭 버튼
	m_pBrush->SetColor(!isFillActive ? D2D1::ColorF(0.18f, 0.52f, 0.95f) : D2D1::ColorF(0.20f, 0.20f, 0.23f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnStroke, 4.0f, 4.0f), m_pBrush);
	m_pBrush->SetColor(!isFillActive ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.7f, 0.7f, 0.7f));
	m_pRenderTarget->DrawText(L"□ 윤곽선(Stroke)", 12, m_pTextFormatCenter, btnStroke, m_pBrush);
	y += 32.0f;

	// 3. 채움 탭 활성화 시: Inkscape 스타일 채우기 모드 툴바 5개 버튼 표시
	// [X] (채우기 없음) | [■] (단색) | [선형 그라디언트] | [방사형 그라디언트] | [패턴]
	if (isFillActive)
	{
		const int numModes = 5;
		float modeBtnW = 28.0f;
		float modeSpacing = 6.0f;
		float totalModeW = numModes * modeBtnW + (numModes - 1) * modeSpacing;
		float startX = 14.0f;

		for (int i = 0; i < numModes; ++i)
		{
			D2D1_RECT_F rBtn = D2D1::RectF(startX + i * (modeBtnW + modeSpacing), y, startX + i * (modeBtnW + modeSpacing) + modeBtnW, y + modeBtnW);
			bool isSelectedMode = (static_cast<int>(m_fillMode) == i);

			// 버튼 배경
			m_pBrush->SetColor(isSelectedMode ? D2D1::ColorF(0.12f, 0.35f, 0.65f) : D2D1::ColorF(0.20f, 0.20f, 0.23f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rBtn, 3.0f, 3.0f), m_pBrush);
			m_pBrush->SetColor(isSelectedMode ? D2D1::ColorF(0.25f, 0.65f, 1.0f) : D2D1::ColorF(0.32f, 0.32f, 0.35f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rBtn, 3.0f, 3.0f), m_pBrush, isSelectedMode ? 1.5f : 1.0f);

			// 각 모드별 아이콘 렌더링
			if (i == 0) // [X] 채우기 없음
			{
				m_pBrush->SetColor(D2D1::ColorF(0.9f, 0.3f, 0.3f));
				m_pRenderTarget->DrawLine(D2D1::Point2F(rBtn.left + 7.0f, rBtn.top + 7.0f), D2D1::Point2F(rBtn.right - 7.0f, rBtn.bottom - 7.0f), m_pBrush, 2.0f);
				m_pRenderTarget->DrawLine(D2D1::Point2F(rBtn.right - 7.0f, rBtn.top + 7.0f), D2D1::Point2F(rBtn.left + 7.0f, rBtn.bottom - 7.0f), m_pBrush, 2.0f);
			}
			else if (i == 1) // [■] 단색 채움 (현재 채움색 표시)
			{
				D2D1_RECT_F rSolid = D2D1::RectF(rBtn.left + 6.0f, rBtn.top + 6.0f, rBtn.right - 6.0f, rBtn.bottom - 6.0f);
				m_pBrush->SetColor(m_fillColor);
				m_pRenderTarget->FillRectangle(rSolid, m_pBrush);
				m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.8f));
				m_pRenderTarget->DrawRectangle(rSolid, m_pBrush, 1.0f);
			}
			else if (i == 2) // [선형 그라디언트]
			{
				D2D1_RECT_F rGrad = D2D1::RectF(rBtn.left + 6.0f, rBtn.top + 6.0f, rBtn.right - 6.0f, rBtn.bottom - 6.0f);
				ID2D1GradientStopCollection* pGradStops = nullptr;
				D2D1_GRADIENT_STOP gStops[2];
				gStops[0].position = 0.0f;
				gStops[0].color = m_fillColor;
				gStops[1].position = 1.0f;
				gStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f);
				if (SUCCEEDED(m_pRenderTarget->CreateGradientStopCollection(gStops, 2, &pGradStops)))
				{
					ID2D1LinearGradientBrush* pLinB = nullptr;
					m_pRenderTarget->CreateLinearGradientBrush(
						D2D1::LinearGradientBrushProperties(D2D1::Point2F(rGrad.left, rGrad.top), D2D1::Point2F(rGrad.right, rGrad.top)),
						pGradStops,
						&pLinB
					);
					if (pLinB)
					{
						m_pRenderTarget->FillRectangle(rGrad, pLinB);
						pLinB->Release();
					}
					pGradStops->Release();
				}
				m_pBrush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f));
				m_pRenderTarget->DrawRectangle(rGrad, m_pBrush, 1.0f);
			}
			else if (i == 3) // [방사형 그라디언트]
			{
				D2D1_RECT_F rRad = D2D1::RectF(rBtn.left + 6.0f, rBtn.top + 6.0f, rBtn.right - 6.0f, rBtn.bottom - 6.0f);
				D2D1_POINT_2F ptC = D2D1::Point2F((rRad.left + rRad.right) * 0.5f, (rRad.top + rRad.bottom) * 0.5f);
				ID2D1GradientStopCollection* pRadStops = nullptr;
				D2D1_GRADIENT_STOP gStops[2];
				gStops[0].position = 0.0f;
				gStops[0].color = m_fillColor;
				gStops[1].position = 1.0f;
				gStops[1].color = D2D1::ColorF(0.1f, 0.1f, 0.1f, 1.0f);
				if (SUCCEEDED(m_pRenderTarget->CreateGradientStopCollection(gStops, 2, &pRadStops)))
				{
					ID2D1RadialGradientBrush* pRadB = nullptr;
					m_pRenderTarget->CreateRadialGradientBrush(
						D2D1::RadialGradientBrushProperties(ptC, D2D1::Point2F(0, 0), 8.0f, 8.0f),
						pRadStops,
						&pRadB
					);
					if (pRadB)
					{
						m_pRenderTarget->FillRectangle(rRad, pRadB);
						pRadB->Release();
					}
					pRadStops->Release();
				}
				m_pBrush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f));
				m_pRenderTarget->DrawRectangle(rRad, m_pBrush, 1.0f);
			}
			else if (i == 4) // [패턴 / 바둑판]
			{
				D2D1_RECT_F rPat = D2D1::RectF(rBtn.left + 6.0f, rBtn.top + 6.0f, rBtn.right - 6.0f, rBtn.bottom - 6.0f);
				m_pBrush->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.28f));
				m_pRenderTarget->FillRectangle(rPat, m_pBrush);
				m_pBrush->SetColor(D2D1::ColorF(0.7f, 0.7f, 0.75f));
				float midX = (rPat.left + rPat.right) * 0.5f;
				float midY = (rPat.top + rPat.bottom) * 0.5f;
				m_pRenderTarget->FillRectangle(D2D1::RectF(rPat.left, rPat.top, midX, midY), m_pBrush);
				m_pRenderTarget->FillRectangle(D2D1::RectF(midX, midY, rPat.right, rPat.bottom), m_pBrush);
				m_pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
				m_pRenderTarget->DrawRectangle(rPat, m_pBrush, 1.0f);
			}
		}
		y += 36.0f;
	}

	// 4. Inkscape 스타일의 대형 컬러휠 (너비에 맞춰 좌우 여백 12px만 남기고 꽉 차게 확장)
	// 요구사항 2: 중앙의 색선택(삼각 원)을 잉크스케이프와 같이 탭메뉴 좌우로 꽉차게(약간의 여백) 해줘
	float wheelRadius = max(60.0f, (width - 24.0f) * 0.5f);
	D2D1_POINT_2F wheelCenter = D2D1::Point2F(width * 0.5f, y + wheelRadius);
	float curHue = isFillActive ? m_fFillHue : m_fStrokeHue;
	float curSat = isFillActive ? m_fFillSat : m_fStrokeSat;
	float curLight = isFillActive ? m_fFillLight : m_fStrokeLight;
	float ringThickness = 44.0f; // 기존(22.0f)의 2배 두께
	DrawColorWheel(wheelCenter, wheelRadius, wheelRadius - ringThickness, curHue, curSat, curLight);
	y += wheelRadius * 2.0f + 14.0f;

	// 5. 활성 색상 스와치 프리뷰 바 (단일 와이드 바)
	D2D1_RECT_F swatchBar = D2D1::RectF(14.0f, y, width - 14.0f, y + 20.0f);
	D2D1_COLOR_F activeClr = isFillActive ? m_fillColor : m_strokeColor;
	m_pBrush->SetColor(activeClr);
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(swatchBar, 3.0f, 3.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
	m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(swatchBar, 3.0f, 3.0f), m_pBrush, 1.0f);
	y += 26.0f;

	// 6. 불투명도(Opacity) 슬라이더 (0% ~ 100%) - 채움과 선 공통 지원
	wchar_t szSliderText[64];
	swprintf_s(szSliderText, L"● 불투명도 조절 바: %.0f%%", m_fOpacity);
	m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.85f));
	m_pRenderTarget->DrawText(szSliderText, (UINT32)wcslen(szSliderText), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	float sliderLeft = 14.0f;
	float sliderRight = width - 14.0f;
	float sliderW = sliderRight - sliderLeft;
	D2D1_RECT_F trackRectO = D2D1::RectF(sliderLeft, y + 3.0f, sliderRight, y + 9.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.22f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackRectO, 3.0f, 3.0f), m_pBrush);

	float fillXO = sliderLeft + (m_fOpacity / 100.0f) * sliderW;
	D2D1_RECT_F fillRectO = D2D1::RectF(sliderLeft, y + 3.0f, fillXO, y + 9.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillRectO, 3.0f, 3.0f), m_pBrush);

	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillXO, y + 6.0f), 7.0f, 7.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillXO, y + 6.0f), 7.0f, 7.0f), m_pBrush, 1.5f);
	y += 26.0f;

	// 요구사항 3: 채움을 선택하면 선굵기와 선 종류 아이콘이 안나오게 함!
	// 선(Stroke)을 선택했을 때만 선 굵기 및 선 종류 아이콘 컨트롤 노출
	if (!isFillActive)
	{
		// 7. 선 굵기(Stroke Width) 슬라이더 (0.5px ~ 20.0px)
		wchar_t szWidthText[64];
		swprintf_s(szWidthText, L"● 선 굵기 (Stroke Width): %.1f px", m_fStrokeWidth);
		m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.85f));
		m_pRenderTarget->DrawText(szWidthText, (UINT32)wcslen(szWidthText), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
		y += 20.0f;

		D2D1_RECT_F trackRectW = D2D1::RectF(sliderLeft, y + 3.0f, sliderRight, y + 9.0f);
		m_pBrush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.22f));
		m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackRectW, 3.0f, 3.0f), m_pBrush);

		float wNorm = (m_fStrokeWidth - 0.5f) / 19.5f;
		if (wNorm < 0.0f) wNorm = 0.0f;
		if (wNorm > 1.0f) wNorm = 1.0f;
		float fillXW = sliderLeft + wNorm * sliderW;
		D2D1_RECT_F fillRectW = D2D1::RectF(sliderLeft, y + 3.0f, fillXW, y + 9.0f);
		m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
		m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillRectW, 3.0f, 3.0f), m_pBrush);

		m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
		m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillXW, y + 6.0f), 7.0f, 7.0f), m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
		m_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(fillXW, y + 6.0f), 7.0f, 7.0f), m_pBrush, 1.5f);
		y += 26.0f;

		// 8. 선 대시 스타일 버튼 4구 (텍스트가 아닌 실제 벡터 선 모양 아이콘: —, - -, ···, — · —)
		float btnW = (width - 28.0f) / 4.0f;
		for (int i = 0; i < 4; ++i)
		{
			D2D1_RECT_F btnRect = D2D1::RectF(14.0f + i * btnW, y, 14.0f + (i + 1) * btnW - 3.0f, y + 26.0f);
			bool isSel = (m_selectedDash == i);

			// 버튼 배경
			m_pBrush->SetColor(isSel ? D2D1::ColorF(0.18f, 0.52f, 0.95f) : D2D1::ColorF(0.20f, 0.20f, 0.23f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnRect, 3.0f, 3.0f), m_pBrush);
			m_pBrush->SetColor(isSel ? D2D1::ColorF(0.3f, 0.7f, 1.0f) : D2D1::ColorF(0.35f, 0.35f, 0.38f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(btnRect, 3.0f, 3.0f), m_pBrush, isSel ? 1.5f : 1.0f);

			// 버튼 내부 선 아이콘 렌더링
			float lineY = (btnRect.top + btnRect.bottom) * 0.5f;
			float lLeft = btnRect.left + 6.0f;
			float lRight = btnRect.right - 6.0f;
			float lWidth = lRight - lLeft;

			m_pBrush->SetColor(isSel ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.75f, 0.75f, 0.78f));

			if (i == 0) // 실선: —
			{
				m_pRenderTarget->DrawLine(D2D1::Point2F(lLeft, lineY), D2D1::Point2F(lRight, lineY), m_pBrush, 2.0f);
			}
			else if (i == 1) // 대시선: - - -
			{
				float seg = lWidth / 5.0f;
				m_pRenderTarget->DrawLine(D2D1::Point2F(lLeft, lineY), D2D1::Point2F(lLeft + seg * 1.5f, lineY), m_pBrush, 2.0f);
				m_pRenderTarget->DrawLine(D2D1::Point2F(lLeft + seg * 2.5f, lineY), D2D1::Point2F(lLeft + seg * 4.0f, lineY), m_pBrush, 2.0f);
			}
			else if (i == 2) // 점선: · · · ·
			{
				float step = lWidth / 4.0f;
				for (int d = 0; d < 4; ++d)
				{
					m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lLeft + d * step + 2.0f, lineY), 1.5f, 1.5f), m_pBrush);
				}
			}
			else if (i == 3) // 대시점선: — · —
			{
				float seg = lWidth / 7.0f;
				m_pRenderTarget->DrawLine(D2D1::Point2F(lLeft, lineY), D2D1::Point2F(lLeft + seg * 2.2f, lineY), m_pBrush, 2.0f);
				m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lLeft + seg * 3.5f, lineY), 1.5f, 1.5f), m_pBrush);
				m_pRenderTarget->DrawLine(D2D1::Point2F(lLeft + seg * 4.8f, lineY), D2D1::Point2F(lRight, lineY), m_pBrush, 2.0f);
			}
		}
	}
}

// -------------------------------------------------------------
// [2. Dear ImGui 탭] 블렌더 / 언리얼 엔진 / 게임 툴 스타일
// -------------------------------------------------------------
void CD2DTestView::DrawImGuiContent(float width, float y)
{
	// 배지 / 설명
	m_pBrush->SetColor(D2D1::ColorF(0.95f, 0.55f, 0.15f));
	m_pRenderTarget->DrawText(L"[Dear ImGui 스타일] Dark 테마 & 슬림 UI", 26, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;
	m_pBrush->SetColor(D2D1::ColorF(0.65f, 0.65f, 0.68f));
	m_pRenderTarget->DrawText(L"특징: 컴팩트한 사각 위젯, ColorEdit4, SliderFloat", 29, m_pTextFormatSmall, D2D1::RectF(14.0f, y, width - 14.0f, y + 16.0f), m_pBrush);
	y += 22.0f;

	m_pBrush->SetColor(D2D1::ColorF(0.28f, 0.28f, 0.30f));
	m_pRenderTarget->DrawLine(D2D1::Point2F(14.0f, y), D2D1::Point2F(width - 14.0f, y), m_pBrush, 1.0f);
	y += 12.0f;

	// 1. ImGui의 ColorEdit4 버튼 & 사각 컬러 팔레트
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● ImGui::ColorPicker3 (RGB / Hue 휠)", 26, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	// ImGui 사각 컬러 스와치 & RGB 값 입력 박스
	D2D1_RECT_F imColorBtn = D2D1::RectF(16.0f, y, 46.0f, y + 26.0f);
	m_pBrush->SetColor(HSLtoRGB(m_fHue, 1.0f, 0.5f));
	m_pRenderTarget->FillRectangle(imColorBtn, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.45f, 0.45f, 0.48f));
	m_pRenderTarget->DrawRectangle(imColorBtn, m_pBrush, 1.0f);

	D2D1_RECT_F imColorInput = D2D1::RectF(52.0f, y, width - 16.0f, y + 26.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f));
	m_pRenderTarget->FillRectangle(imColorInput, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.38f));
	m_pRenderTarget->DrawRectangle(imColorInput, m_pBrush, 1.0f);

	wchar_t szRgbText[64];
	D2D1_COLOR_F rgb = HSLtoRGB(m_fHue, 1.0f, 0.5f);
	swprintf_s(szRgbText, L"R:%.2f  G:%.2f  B:%.2f", rgb.r, rgb.g, rgb.b);
	m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.85f));
	m_pRenderTarget->DrawText(szRgbText, (UINT32)wcslen(szRgbText), m_pTextFormatSmall, D2D1::RectF(58.0f, y + 4.0f, width - 20.0f, y + 24.0f), m_pBrush);
	y += 32.0f;

	// ImGui 스타일 컴팩트 컬러 바 (가로 Hue 바)
	float hueBarW = width - 32.0f;
	for (int i = 0; i < 30; ++i)
	{
		float segX1 = 16.0f + (i / 30.0f) * hueBarW;
		float segX2 = 16.0f + ((i + 1) / 30.0f) * hueBarW;
		m_pBrush->SetColor(HSLtoRGB(i * 12.0f, 1.0f, 0.5f));
		m_pRenderTarget->FillRectangle(D2D1::RectF(segX1, y, segX2, y + 14.0f), m_pBrush);
	}
	m_pBrush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.3f));
	m_pRenderTarget->DrawRectangle(D2D1::RectF(16.0f, y, width - 16.0f, y + 14.0f), m_pBrush, 1.0f);
	// 선택 포인터
	float selX = 16.0f + (m_fHue / 360.0f) * hueBarW;
	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	m_pRenderTarget->DrawRectangle(D2D1::RectF(selX - 2.0f, y - 2.0f, selX + 2.0f, y + 16.0f), m_pBrush, 2.0f);
	y += 26.0f;

	// 2. ImGui 스타일 사각 체크박스
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● ImGui::Checkbox (클릭 토글)", 19, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	auto drawImChk = [&](float topY, bool checked, const wchar_t* label)
	{
		D2D1_RECT_F box = D2D1::RectF(16.0f, topY, 30.0f, topY + 14.0f);
		m_pBrush->SetColor(D2D1::ColorF(0.16f, 0.16f, 0.18f));
		m_pRenderTarget->FillRectangle(box, m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.45f));
		m_pRenderTarget->DrawRectangle(box, m_pBrush, 1.0f);

		if (checked)
		{
			// ImGui 특유의 X자 또는 사각 체크마크
			m_pBrush->SetColor(D2D1::ColorF(0.92f, 0.45f, 0.12f)); // 오렌지 테마
			m_pRenderTarget->FillRectangle(D2D1::RectF(19.0f, topY + 3.0f, 27.0f, topY + 11.0f), m_pBrush);
		}
		m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.85f));
		m_pRenderTarget->DrawText(label, (UINT32)wcslen(label), m_pTextFormat, D2D1::RectF(36.0f, topY - 1.0f, width - 20.0f, topY + 16.0f), m_pBrush);
	};

	drawImChk(y, m_bSnapAll, L"ImGui::SnapAll"); y += 20.0f;
	drawImChk(y, m_bSnapBBox, L"ImGui::SnapBBox"); y += 20.0f;
	drawImChk(y, m_bSnapNode, L"ImGui::SnapNode"); y += 24.0f;

	// 3. ImGui::SliderFloat (내부에 숫자 텍스트가 들어가는 일체형 바)
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● ImGui::SliderFloat (드래그 조절)", 22, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	D2D1_RECT_F imSlider = D2D1::RectF(16.0f, y, width - 16.0f, y + 22.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.14f, 0.14f, 0.16f));
	m_pRenderTarget->FillRectangle(imSlider, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.38f));
	m_pRenderTarget->DrawRectangle(imSlider, m_pBrush, 1.0f);

	float fillW = (m_fOpacity / 100.0f) * (width - 32.0f);
	D2D1_RECT_F imFill = D2D1::RectF(16.0f, y, 16.0f + fillW, y + 22.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.92f, 0.45f, 0.12f, 0.75f)); // ImGui 오렌지 그랩
	m_pRenderTarget->FillRectangle(imFill, m_pBrush);

	wchar_t szImSliderText[64];
	swprintf_s(szImSliderText, L"Opacity: %.1f%%", m_fOpacity);
	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	m_pRenderTarget->DrawText(szImSliderText, (UINT32)wcslen(szImSliderText), m_pTextFormatBold, imSlider, m_pBrush);
	y += 32.0f;

	// 4. ImGui::Combo 드롭다운 스타일 버튼
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● ImGui::Combo (드롭다운)", 16, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	const wchar_t* blendModes[] = { L"Normal", L"Multiply", L"Screen", L"Overlay" };
	D2D1_RECT_F comboRect = D2D1::RectF(16.0f, y, width - 16.0f, y + 22.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.23f));
	m_pRenderTarget->FillRectangle(comboRect, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.40f, 0.40f, 0.44f));
	m_pRenderTarget->DrawRectangle(comboRect, m_pBrush, 1.0f);

	m_pBrush->SetColor(D2D1::ColorF(0.9f, 0.9f, 0.9f));
	m_pRenderTarget->DrawText(blendModes[m_selectedBlend], (UINT32)wcslen(blendModes[m_selectedBlend]), m_pTextFormat, D2D1::RectF(24.0f, y + 2.0f, width - 40.0f, y + 22.0f), m_pBrush);
	// 화살표 v
	m_pBrush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f));
	m_pRenderTarget->DrawText(L"▼", 1, m_pTextFormatSmall, D2D1::RectF(width - 32.0f, y + 3.0f, width - 16.0f, y + 22.0f), m_pBrush);
}

// -------------------------------------------------------------
// [3. MFC Classic 탭] 윈도우 기본 대화상자 컨트롤 스타일
// -------------------------------------------------------------
void CD2DTestView::DrawMFCContent(float width, float y)
{
	// 배지 / 설명
	m_pBrush->SetColor(D2D1::ColorF(0.3f, 0.7f, 0.9f));
	m_pRenderTarget->DrawText(L"[MFC Classic 스타일] Windows 공용 컨트롤", 28, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;
	m_pBrush->SetColor(D2D1::ColorF(0.65f, 0.65f, 0.68f));
	m_pRenderTarget->DrawText(L"특징: 친숙한 Win32 OS 표준 대화상자 & 프로퍼티 그리드", 32, m_pTextFormatSmall, D2D1::RectF(14.0f, y, width - 14.0f, y + 16.0f), m_pBrush);
	y += 22.0f;

	m_pBrush->SetColor(D2D1::ColorF(0.28f, 0.28f, 0.30f));
	m_pRenderTarget->DrawLine(D2D1::Point2F(14.0f, y), D2D1::Point2F(width - 14.0f, y), m_pBrush, 1.0f);
	y += 12.0f;

	// 1. MFC CColorDialog 스타일 컬러 박스 & [기타...] 푸시버튼
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● CColorDialog / CMFCColorButton", 23, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	D2D1_RECT_F mfcColorBtn = D2D1::RectF(16.0f, y, width - 90.0f, y + 24.0f);
	m_pBrush->SetColor(HSLtoRGB(m_fHue, 1.0f, 0.5f));
	m_pRenderTarget->FillRectangle(mfcColorBtn, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.4f));
	m_pRenderTarget->DrawRectangle(mfcColorBtn, m_pBrush, 1.0f);

	D2D1_RECT_F mfcBtnOther = D2D1::RectF(width - 85.0f, y, width - 16.0f, y + 24.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.28f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(mfcBtnOther, 2.0f, 2.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.45f, 0.45f, 0.48f));
	m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(mfcBtnOther, 2.0f, 2.0f), m_pBrush, 1.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.9f, 0.9f, 0.9f));
	m_pRenderTarget->DrawText(L"선택...", 3, m_pTextFormatCenter, mfcBtnOther, m_pBrush);
	y += 32.0f;

	// 2. MFC BS_AUTOCHECKBOX 스타일 체크박스 (흰 배경에 파란 V체크)
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● CButton (BS_AUTOCHECKBOX)", 20, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	auto drawMfcChk = [&](float topY, bool checked, const wchar_t* label)
	{
		D2D1_RECT_F box = D2D1::RectF(18.0f, topY, 31.0f, topY + 13.0f);
		m_pBrush->SetColor(D2D1::ColorF(0.95f, 0.95f, 0.95f)); // 윈도우 스타일 흰색 박스
		m_pRenderTarget->FillRectangle(box, m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.38f));
		m_pRenderTarget->DrawRectangle(box, m_pBrush, 1.0f);

		if (checked)
		{
			m_pBrush->SetColor(D2D1::ColorF(0.0f, 0.45f, 0.85f));
			m_pRenderTarget->DrawLine(D2D1::Point2F(20.5f, topY + 6.5f), D2D1::Point2F(23.5f, topY + 10.0f), m_pBrush, 2.0f);
			m_pRenderTarget->DrawLine(D2D1::Point2F(23.5f, topY + 10.0f), D2D1::Point2F(28.5f, topY + 3.5f), m_pBrush, 2.0f);
		}
		m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.85f));
		m_pRenderTarget->DrawText(label, (UINT32)wcslen(label), m_pTextFormat, D2D1::RectF(38.0f, topY - 1.0f, width - 20.0f, topY + 16.0f), m_pBrush);
	};

	drawMfcChk(y, m_bSnapAll, L"스냅 사용 (&S)"); y += 20.0f;
	drawMfcChk(y, m_bSnapBBox, L"경계 상자 맞춤 (&B)"); y += 20.0f;
	drawMfcChk(y, m_bSnapNode, L"노드 맞춤 (&N)"); y += 24.0f;

	// 3. MFC CSliderCtrl (트랙바 + 눈금 스타일)
	wchar_t szMfcSlider[64];
	swprintf_s(szMfcSlider, L"● CSliderCtrl (위치: %d)", (int)m_fOpacity);
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(szMfcSlider, (UINT32)wcslen(szMfcSlider), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	// 트랙바 선
	float sLeft = 20.0f;
	float sRight = width - 20.0f;
	m_pBrush->SetColor(D2D1::ColorF(0.4f, 0.4f, 0.42f));
	m_pRenderTarget->DrawLine(D2D1::Point2F(sLeft, y + 8.0f), D2D1::Point2F(sRight, y + 8.0f), m_pBrush, 2.0f);

	// 눈금 5개
	for (int i = 0; i <= 4; ++i)
	{
		float tickX = sLeft + (i / 4.0f) * (sRight - sLeft);
		m_pRenderTarget->DrawLine(D2D1::Point2F(tickX, y + 12.0f), D2D1::Point2F(tickX, y + 16.0f), m_pBrush, 1.0f);
	}

	// 윈도우 스타일 삼각 썸
	float thumbX = sLeft + (m_fOpacity / 100.0f) * (sRight - sLeft);
	D2D1_RECT_F thumbRect = D2D1::RectF(thumbX - 5.0f, y + 1.0f, thumbX + 5.0f, y + 14.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.8f, 0.8f, 0.82f));
	m_pRenderTarget->FillRectangle(thumbRect, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.32f));
	m_pRenderTarget->DrawRectangle(thumbRect, m_pBrush, 1.0f);
	y += 28.0f;

	// 4. MFC CComboBox 스타일
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.88f));
	m_pRenderTarget->DrawText(L"● CComboBox (CBS_DROPDOWNLIST)", 22, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	const wchar_t* mfcModes[] = { L"정상 (Normal)", L"곱하기 (Multiply)", L"스크린 (Screen)", L"오버레이 (Overlay)" };
	D2D1_RECT_F mfcCombo = D2D1::RectF(16.0f, y, width - 16.0f, y + 24.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.95f, 0.95f, 0.95f));
	m_pRenderTarget->FillRectangle(mfcCombo, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.45f, 0.45f, 0.48f));
	m_pRenderTarget->DrawRectangle(mfcCombo, m_pBrush, 1.0f);

	m_pBrush->SetColor(D2D1::ColorF(0.1f, 0.1f, 0.1f)); // 윈도우 기본 콤보 텍스트 (어두운 글자)
	m_pRenderTarget->DrawText(mfcModes[m_selectedBlend], (UINT32)wcslen(mfcModes[m_selectedBlend]), m_pTextFormat, D2D1::RectF(24.0f, y + 3.0f, width - 40.0f, y + 24.0f), m_pBrush);

	// 드롭다운 버튼 영역
	D2D1_RECT_F dropBtn = D2D1::RectF(width - 36.0f, y, width - 16.0f, y + 24.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.88f, 0.88f, 0.90f));
	m_pRenderTarget->FillRectangle(dropBtn, m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
	m_pRenderTarget->DrawRectangle(dropBtn, m_pBrush, 1.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.2f));
	m_pRenderTarget->DrawText(L"▼", 1, m_pTextFormatSmall, dropBtn, m_pBrush);
}

// -------------------------------------------------------------
// [캔버스 설정 탭] Inkscape 스타일 Direct2D 커스텀 설정 뷰
// -------------------------------------------------------------
void CD2DTestView::DrawCanvasContent(float width, float y)
{
	GridRenderer& grid = GetApp()->GetGrid();
	RulerRenderer& ruler = GetApp()->GetRuler();

	// 1. 섹션 헤더: "격자 및 캔버스 설정"
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->DrawText(L"■ 격자 및 캔버스 설정", 13, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 20.0f), m_pBrush);
	y += 24.0f;

	// 2. 3개 토글 스위치 버튼: [그리드 표시], [그리드 스냅], [눈금자 표시]
	struct ToggleBtn {
		const wchar_t* label;
		bool state;
	} toggles[3] = {
		{ L"격자 표시", grid.enabled },
		{ L"격자 스냅", grid.snapToGrid },
		{ L"눈금자 표시", ruler.enabled }
	};

	float btnW = (width - 28.0f - 12.0f) / 3.0f;
	for (int i = 0; i < 3; ++i)
	{
		float bx1 = 14.0f + i * (btnW + 6.0f);
		float bx2 = bx1 + btnW;
		D2D1_RECT_F rBtn = D2D1::RectF(bx1, y, bx2, y + 28.0f);

		if (toggles[i].state)
		{
			// ON 상태: 액센트 배경
			m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f, 0.25f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rBtn, 4.0f, 4.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.25f, 0.65f, 1.0f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rBtn, 4.0f, 4.0f), m_pBrush, 1.5f);

			// LED 인디케이터 녹색
			m_pBrush->SetColor(D2D1::ColorF(0.2f, 0.9f, 0.4f));
			m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx1 + 8.0f, y + 14.0f), 3.0f, 3.0f), m_pBrush);

			m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
			m_pRenderTarget->DrawText(toggles[i].label, (UINT32)wcslen(toggles[i].label), m_pTextFormatSmall,
				D2D1::RectF(bx1 + 14.0f, y + 5.0f, bx2 - 2.0f, y + 24.0f), m_pBrush);
		}
		else
		{
			// OFF 상태: 짙은 회색 배경
			m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rBtn, 4.0f, 4.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.32f, 0.32f, 0.36f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rBtn, 4.0f, 4.0f), m_pBrush, 1.0f);

			// LED 인디케이터 오프(회색)
			m_pBrush->SetColor(D2D1::ColorF(0.45f, 0.45f, 0.50f));
			m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx1 + 8.0f, y + 14.0f), 3.0f, 3.0f), m_pBrush);

			m_pBrush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.65f));
			m_pRenderTarget->DrawText(toggles[i].label, (UINT32)wcslen(toggles[i].label), m_pTextFormatSmall,
				D2D1::RectF(bx1 + 14.0f, y + 5.0f, bx2 - 2.0f, y + 24.0f), m_pBrush);
		}
	}
	y += 36.0f;

	// 3. 측정 단위 선택 세그먼트 (px, mm, cm, in, rem)
	m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.88f));
	m_pRenderTarget->DrawText(L"● 측정 단위 (Unit)", 13, m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	const wchar_t* unitLabels[] = { L"px/pt", L"mm", L"cm", L"in", L"rem" };
	float unitBtnW = (width - 28.0f - 16.0f) / 5.0f;
	int curUnitIdx = static_cast<int>(grid.unit);

	for (int i = 0; i < 5; ++i)
	{
		float ux1 = 14.0f + i * (unitBtnW + 4.0f);
		float ux2 = ux1 + unitBtnW;
		D2D1_RECT_F rUnit = D2D1::RectF(ux1, y, ux2, y + 24.0f);

		bool isSelected = (curUnitIdx == i);
		if (isSelected)
		{
			m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rUnit, 3.0f, 3.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
			m_pRenderTarget->DrawText(unitLabels[i], (UINT32)wcslen(unitLabels[i]), m_pTextFormatCenter, rUnit, m_pBrush);
		}
		else
		{
			m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f));
			m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rUnit, 3.0f, 3.0f), m_pBrush);
			m_pBrush->SetColor(D2D1::ColorF(0.32f, 0.32f, 0.36f));
			m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rUnit, 3.0f, 3.0f), m_pBrush, 1.0f);
			m_pBrush->SetColor(D2D1::ColorF(0.70f, 0.70f, 0.75f));
			m_pRenderTarget->DrawText(unitLabels[i], (UINT32)wcslen(unitLabels[i]), m_pTextFormatCenter, rUnit, m_pBrush);
		}
	}
	y += 32.0f;

	// 4. 격자 간격 슬라이더 (5 ~ 100 stepInUnit)
	wchar_t szStepText[64];
	swprintf_s(szStepText, L"● 그리드 간격: %.1f %s", grid.stepInUnit, GetCanvasUnitSuffix(grid.unit));
	m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.88f));
	m_pRenderTarget->DrawText(szStepText, (UINT32)wcslen(szStepText), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	float sliderLeft = 14.0f;
	float sliderRight = width - 14.0f;
	float sliderW = sliderRight - sliderLeft;

	// 슬라이더 배경 트랙
	D2D1_RECT_F trackStep = D2D1::RectF(sliderLeft, y + 6.0f, sliderRight, y + 10.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackStep, 2.0f, 2.0f), m_pBrush);

	// 슬라이더 채움 트랙
	float fracStep = (grid.stepInUnit - 5.0f) / 95.0f;
	if (fracStep < 0.0f) fracStep = 0.0f;
	if (fracStep > 1.0f) fracStep = 1.0f;
	float thumbStepX = sliderLeft + fracStep * sliderW;

	D2D1_RECT_F fillStep = D2D1::RectF(sliderLeft, y + 6.0f, thumbStepX, y + 10.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillStep, 2.0f, 2.0f), m_pBrush);

	// 썸
	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbStepX, y + 8.0f), 7.0f, 7.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbStepX, y + 8.0f), 7.0f, 7.0f), m_pBrush, 1.5f);
	y += 24.0f;

	// 5. 주 격자선 배율 슬라이더 (2 ~ 20)
	wchar_t szMajorText[64];
	swprintf_s(szMajorText, L"● 주 격자선 배율: %d칸마다 (LOD 최소: %.0fpx)", grid.majorEvery, grid.minStepPx);
	m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.88f));
	m_pRenderTarget->DrawText(szMajorText, (UINT32)wcslen(szMajorText), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	D2D1_RECT_F trackMajor = D2D1::RectF(sliderLeft, y + 6.0f, sliderRight, y + 10.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackMajor, 2.0f, 2.0f), m_pBrush);

	float fracMajor = (static_cast<float>(grid.majorEvery) - 2.0f) / 18.0f;
	if (fracMajor < 0.0f) fracMajor = 0.0f;
	if (fracMajor > 1.0f) fracMajor = 1.0f;
	float thumbMajorX = sliderLeft + fracMajor * sliderW;

	D2D1_RECT_F fillMajor = D2D1::RectF(sliderLeft, y + 6.0f, thumbMajorX, y + 10.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.95f, 0.45f, 0.18f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillMajor, 2.0f, 2.0f), m_pBrush);

	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbMajorX, y + 8.0f), 7.0f, 7.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.95f, 0.45f, 0.18f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbMajorX, y + 8.0f), 7.0f, 7.0f), m_pBrush, 1.5f);
	y += 28.0f;

	// 6. 격자 색상 설정 탭: [보조 격자선 색상] vs [주 격자선 색상]
	float tabHalfW = (width - 28.0f) * 0.5f;
	D2D1_RECT_F rTabMinor = D2D1::RectF(14.0f, y, 14.0f + tabHalfW - 2.0f, y + 26.0f);
	D2D1_RECT_F rTabMajor = D2D1::RectF(14.0f + tabHalfW + 2.0f, y, width - 14.0f, y + 26.0f);

	bool isMinorActive = (m_canvasColorTarget == 0);

	// 보조선 탭
	if (isMinorActive)
	{
		m_pBrush->SetColor(D2D1::ColorF(0.22f, 0.22f, 0.28f));
		m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rTabMinor, 4.0f, 4.0f), m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.2f, 0.6f, 1.0f));
		m_pRenderTarget->DrawLine(D2D1::Point2F(rTabMinor.left + 6.0f, rTabMinor.bottom - 2.0f),
			D2D1::Point2F(rTabMinor.right - 6.0f, rTabMinor.bottom - 2.0f), m_pBrush, 2.5f);
		m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	}
	else
	{
		m_pBrush->SetColor(D2D1::ColorF(0.15f, 0.15f, 0.18f));
		m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rTabMinor, 4.0f, 4.0f), m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f));
	}
	m_pRenderTarget->DrawText(L"보조선 색상", 6, m_pTextFormatCenter, rTabMinor, m_pBrush);

	// 보조선 색상 미리보기 작은 칩
	m_pBrush->SetColor(grid.minor);
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rTabMinor.left + 6.0f, rTabMinor.top + 6.0f, rTabMinor.left + 20.0f, rTabMinor.bottom - 6.0f), 2.0f, 2.0f), m_pBrush);

	// 주선 탭
	if (!isMinorActive)
	{
		m_pBrush->SetColor(D2D1::ColorF(0.22f, 0.22f, 0.28f));
		m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rTabMajor, 4.0f, 4.0f), m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.95f, 0.45f, 0.18f));
		m_pRenderTarget->DrawLine(D2D1::Point2F(rTabMajor.left + 6.0f, rTabMajor.bottom - 2.0f),
			D2D1::Point2F(rTabMajor.right - 6.0f, rTabMajor.bottom - 2.0f), m_pBrush, 2.5f);
		m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	}
	else
	{
		m_pBrush->SetColor(D2D1::ColorF(0.15f, 0.15f, 0.18f));
		m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rTabMajor, 4.0f, 4.0f), m_pBrush);
		m_pBrush->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.60f));
	}
	m_pRenderTarget->DrawText(L"주선 색상", 5, m_pTextFormatCenter, rTabMajor, m_pBrush);

	// 주선 색상 미리보기 작은 칩
	m_pBrush->SetColor(grid.major);
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rTabMajor.left + 6.0f, rTabMajor.top + 6.0f, rTabMajor.left + 20.0f, rTabMajor.bottom - 6.0f), 2.0f, 2.0f), m_pBrush);

	y += 34.0f;

	// 7. 대형 컬러 휠 (보조선 또는 주선 색상 조절)
	float wheelRadius = max(60.0f, (width - 24.0f) * 0.5f);
	D2D1_POINT_2F wheelCenter = D2D1::Point2F(width * 0.5f, y + wheelRadius);
	float activeHue = isMinorActive ? m_fMinorHue : m_fMajorHue;
	float activeSat = isMinorActive ? m_fMinorSat : m_fMajorSat;
	float activeLight = isMinorActive ? m_fMinorLight : m_fMajorLight;
	float ringThickness = 44.0f; // 기존(22.0f)의 2배 두께
	DrawColorWheel(wheelCenter, wheelRadius, wheelRadius - ringThickness, activeHue, activeSat, activeLight);
	y += wheelRadius * 2.0f + 14.0f;

	// 8. 현재 선택된 색상 와이드 바 프리뷰
	D2D1_RECT_F colorBar = D2D1::RectF(14.0f, y, width - 14.0f, y + 20.0f);
	m_pBrush->SetColor(isMinorActive ? grid.minor : grid.major);
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(colorBar, 3.0f, 3.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f));
	m_pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(colorBar, 3.0f, 3.0f), m_pBrush, 1.0f);
	y += 26.0f;

	// 9. 그리드선 투명도(Opacity) 슬라이더 (0% ~ 100%)
	float curAlpha = (isMinorActive ? grid.minor.a : grid.major.a) * 100.0f;
	wchar_t szGridOpacityText[64];
	swprintf_s(szGridOpacityText, L"● 그리드 투명도 (Opacity): %.0f%%", curAlpha);
	m_pBrush->SetColor(D2D1::ColorF(0.85f, 0.85f, 0.88f));
	m_pRenderTarget->DrawText(szGridOpacityText, (UINT32)wcslen(szGridOpacityText), m_pTextFormatBold, D2D1::RectF(14.0f, y, width - 14.0f, y + 18.0f), m_pBrush);
	y += 20.0f;

	D2D1_RECT_F trackAlpha = D2D1::RectF(sliderLeft, y + 6.0f, sliderRight, y + 10.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.20f, 0.20f, 0.24f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackAlpha, 2.0f, 2.0f), m_pBrush);

	float fracAlpha = curAlpha / 100.0f;
	if (fracAlpha < 0.0f) fracAlpha = 0.0f;
	if (fracAlpha > 1.0f) fracAlpha = 1.0f;
	float thumbAlphaX = sliderLeft + fracAlpha * sliderW;

	D2D1_RECT_F fillAlpha = D2D1::RectF(sliderLeft, y + 6.0f, thumbAlphaX, y + 10.0f);
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillAlpha, 2.0f, 2.0f), m_pBrush);

	m_pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
	m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbAlphaX, y + 8.0f), 7.0f, 7.0f), m_pBrush);
	m_pBrush->SetColor(D2D1::ColorF(0.18f, 0.52f, 0.95f));
	m_pRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbAlphaX, y + 8.0f), 7.0f, 7.0f), m_pBrush, 1.5f);
	y += 24.0f;
}

void CD2DTestView::OnPaint()
{
	CPaintDC dc(this);

	if (FAILED(CreateDeviceResources()) || !m_pRenderTarget)
		return;

	m_pRenderTarget->BeginDraw();

	// 다크 테마 배경 (#1f1f23)
	m_pRenderTarget->Clear(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f));

	CRect rc;
	GetClientRect(&rc);
	float width = static_cast<float>(rc.Width());

	float y = 10.0f;

	if (m_paneMode == MODE_CANVAS)
	{
		// 캔버스 설정 모드
		DrawCanvasContent(width, y);
	}
	else
	{
		// 쉐이프 / GUI TEST 모드
		// 최상단 3개 탭 헤더 그리기
		DrawTabHeaders(width, y);

		// 선택된 탭에 따라 각각 다른 스타일로 렌더링
		switch (m_currentTab)
		{
		case TAB_D2D:
			DrawD2DContent(width, y);
			break;
		case TAB_IMGUI:
			DrawImGuiContent(width, y);
			break;
		case TAB_MFC:
			DrawMFCContent(width, y);
			break;
		}
	}

	HRESULT hr = m_pRenderTarget->EndDraw();
	if (hr == D2DERR_RECREATE_TARGET)
	{
		DiscardDeviceResources();
	}
}

void CD2DTestView::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	CRect rc;
	GetClientRect(&rc);
	float width = static_cast<float>(rc.Width());

	// -------------------------------------------------------------
	// 캔버스 설정 모드 (MODE_CANVAS) 상호작용
	// -------------------------------------------------------------
	if (m_paneMode == MODE_CANVAS)
	{
		GridRenderer& grid = GetApp()->GetGrid();
		RulerRenderer& ruler = GetApp()->GetRuler();
		float y = 10.0f + 24.0f; // 34.0f (헤더 아래)

		// 1. 토글 버튼 3구 (y: 34 ~ 62)
		float btnW = (width - 28.0f - 12.0f) / 3.0f;
		if (point.y >= y && point.y <= y + 28.0f)
		{
			for (int i = 0; i < 3; ++i)
			{
				float bx1 = 14.0f + i * (btnW + 6.0f);
				float bx2 = bx1 + btnW;
				if (point.x >= bx1 && point.x <= bx2)
				{
					if (i == 0) grid.enabled = !grid.enabled;
					else if (i == 1) grid.snapToGrid = !grid.snapToGrid;
					else if (i == 2) ruler.enabled = !ruler.enabled;

					ApplyCanvasSettings();
					Invalidate(FALSE);
					return;
				}
			}
		}
		y += 36.0f; // 70.0f

		// 2. 측정 단위 세그먼트 (px, mm, cm, in, rem) (y: 70 + 20 = 90 ~ 114)
		y += 20.0f; // 90.0f
		if (point.y >= y && point.y <= y + 24.0f)
		{
			float unitBtnW = (width - 28.0f - 16.0f) / 5.0f;
			for (int i = 0; i < 5; ++i)
			{
				float ux1 = 14.0f + i * (unitBtnW + 4.0f);
				float ux2 = ux1 + unitBtnW;
				if (point.x >= ux1 && point.x <= ux2)
				{
					grid.unit = static_cast<CanvasUnit>(i);
					ApplyCanvasSettings();
					Invalidate(FALSE);
					return;
				}
			}
		}
		y += 32.0f; // 122.0f

		// 3. 그리드 간격 슬라이더 (5 ~ 100) (y: 122 + 20 = 142 ~ 162)
		y += 20.0f; // 142.0f
		float sliderLeft = 14.0f;
		float sliderRight = width - 14.0f;
		float sliderW = sliderRight - sliderLeft;

		if (point.y >= y - 5.0f && point.y <= y + 20.0f)
		{
			m_isDraggingStepSlider = true;
			float frac = ((float)point.x - sliderLeft) / sliderW;
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			grid.stepInUnit = 5.0f + frac * 95.0f;
			SetCapture();
			ApplyCanvasSettings();
			Invalidate(FALSE);
			return;
		}
		y += 24.0f; // 166.0f

		// 4. 주 격자선 배율 슬라이더 (2 ~ 20) (y: 166 + 20 = 186 ~ 206)
		y += 20.0f; // 186.0f
		if (point.y >= y - 5.0f && point.y <= y + 20.0f)
		{
			m_isDraggingMajorSlider = true;
			float frac = ((float)point.x - sliderLeft) / sliderW;
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			grid.majorEvery = 2 + static_cast<int>(std::round(frac * 18.0f));
			SetCapture();
			ApplyCanvasSettings();
			Invalidate(FALSE);
			return;
		}
		y += 28.0f; // 214.0f

		// 5. 격자 색상 탭: [보조선 색상] vs [주선 색상] (y: 214 ~ 240)
		float tabHalfW = (width - 28.0f) * 0.5f;
		if (point.y >= y && point.y <= y + 26.0f)
		{
			if (point.x >= 14.0f && point.x <= 14.0f + tabHalfW)
			{
				m_canvasColorTarget = 0; // 보조선
				Invalidate(FALSE);
				return;
			}
			else if (point.x >= 14.0f + tabHalfW && point.x <= width - 14.0f)
			{
				m_canvasColorTarget = 1; // 주선
				Invalidate(FALSE);
				return;
			}
		}
		y += 34.0f; // 248.0f

		// 6. Inkscape 대형 컬러휠 클릭 (보조/주 격자선 색상 조절)
		float wheelRadius = max(60.0f, (width - 24.0f) * 0.5f);
		D2D1_POINT_2F wheelCenter = D2D1::Point2F(width * 0.5f, y + wheelRadius);
		float dx = point.x - wheelCenter.x;
		float dy = point.y - wheelCenter.y;
		float dist = std::sqrt(dx * dx + dy * dy);
		float radiusInner = wheelRadius - 44.0f;

		// 6-1. 외곽 Hue 링 클릭 및 드래그 시작
		if (dist >= radiusInner && dist <= wheelRadius + 8.0f)
		{
			m_isDraggingWheel = true;
			float angle = std::atan2(dy, dx) * (180.0f / 3.14159265f);
			if (angle < 0.0f) angle += 360.0f;

			if (m_canvasColorTarget == 0)
			{
				m_fMinorHue = angle;
				grid.minor = HSLtoRGB(m_fMinorHue, m_fMinorSat, m_fMinorLight, 0.7f);
			}
			else
			{
				m_fMajorHue = angle;
				grid.major = HSLtoRGB(m_fMajorHue, m_fMajorSat, m_fMajorLight, 0.9f);
			}

			SetCapture();
			ApplyCanvasSettings();
			Invalidate(FALSE);
			return;
		}
		// 6-2. 내부 삼각형 클릭 및 드래그 시작 (요구사항 1: 세모 안 색상 선택)
		else if (dist < radiusInner)
		{
			m_isDraggingTriangle = true;
			float activeHue = (m_canvasColorTarget == 0) ? m_fMinorHue : m_fMajorHue;
			D2D1_POINT_2F ptHue, ptWhite, ptBlack;
			GetTriangleVertices(wheelCenter, radiusInner, activeHue, ptHue, ptWhite, ptBlack);

			float wA, wB, wC;
			CalcBarycentric(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)), ptHue, ptWhite, ptBlack, wA, wB, wC);

			float sat, light;
			BarycentricToSL(wA, wB, wC, sat, light);

			if (m_canvasColorTarget == 0)
			{
				m_fMinorSat = sat;
				m_fMinorLight = light;
				grid.minor = HSLtoRGB(m_fMinorHue, m_fMinorSat, m_fMinorLight, 0.7f);
			}
			else
			{
				m_fMajorSat = sat;
				m_fMajorLight = light;
				grid.major = HSLtoRGB(m_fMajorHue, m_fMajorSat, m_fMajorLight, 0.9f);
			}

			SetCapture();
			ApplyCanvasSettings();
			Invalidate(FALSE);
			return;
		}

		// 7. 그리드선 투명도(Opacity) 슬라이더 클릭 (y: 248 + wheelRadius*2 + 14 + 26 + 20)
		y += wheelRadius * 2.0f + 14.0f + 26.0f + 20.0f;
		if (point.y >= y - 5.0f && point.y <= y + 20.0f)
		{
			m_isDraggingCanvasOpacity = true;
			float frac = ((float)point.x - sliderLeft) / sliderW;
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;

			if (m_canvasColorTarget == 0)
			{
				grid.minor.a = frac;
			}
			else
			{
				grid.major.a = frac;
			}

			SetCapture();
			ApplyCanvasSettings();
			Invalidate(FALSE);
			return;
		}

		return;
	}

	// 1. 최상단 3개 탭 클릭 판정 (y: 10 ~ 36)
	if (point.y >= 10 && point.y <= 36)
	{
		float tabW = (width - 20.0f) / 3.0f;
		int clickedTab = static_cast<int>((point.x - 10.0f) / tabW);
		if (clickedTab >= 0 && clickedTab < 3)
		{
			m_currentTab = static_cast<GuiTab>(clickedTab);
			Invalidate(FALSE);
			return;
		}
	}

	// 2. 탭별 컨트롤 상호작용
	if (m_currentTab == TAB_D2D)
	{
		float yCur = 10.0f + 34.0f; // 44.0f
		yCur += 22.0f; // 66.0f (도형 이름 헤더 아래)

		// 1. [채움(Fill)] vs [윤곽선(Stroke)] 상단 탭 버튼 클릭 (y: yCur ~ yCur + 26)
		float halfW = (width - 24.0f) * 0.5f;
		if (point.y >= yCur && point.y <= yCur + 26.0f)
		{
			if (point.x >= 12.0f && point.x <= 12.0f + halfW)
			{
				m_colorTarget = 0; // 채움(Fill)
				m_fOpacity = m_fillColor.a * 100.0f;
				Invalidate(FALSE);
				return;
			}
			else if (point.x >= 12.0f + halfW && point.x <= width - 12.0f)
			{
				m_colorTarget = 1; // 선(Stroke)
				m_fOpacity = m_strokeColor.a * 100.0f;
				Invalidate(FALSE);
				return;
			}
		}
		yCur += 32.0f; // 98.0f

		bool isFillActive = (m_colorTarget == 0);

		// 2. 채움 모드일 때: 5개 모드 버튼 클릭 (None, Solid, Linear, Radial, Pattern)
		if (isFillActive)
		{
			if (point.y >= yCur && point.y <= yCur + 28.0f)
			{
				float modeBtnW = 28.0f;
				float modeSpacing = 6.0f;
				float startX = 14.0f;
				for (int i = 0; i < 5; ++i)
				{
					float bx1 = startX + i * (modeBtnW + modeSpacing);
					float bx2 = bx1 + modeBtnW;
					if (point.x >= bx1 && point.x <= bx2)
					{
						m_fillMode = static_cast<FillMode>(i);
						if (m_fillMode == FILL_NONE)
						{
							m_bFillEnabled = false;
						}
						else
						{
							m_bFillEnabled = true;
						}
						ApplyToShape();
						Invalidate(FALSE);
						return;
					}
				}
			}
			yCur += 36.0f;
		}

		// 3. Inkscape 대형 컬러휠 클릭 및 드래그 시작
		float wheelRadius = max(60.0f, (width - 24.0f) * 0.5f);
		D2D1_POINT_2F wheelCenter = D2D1::Point2F(width * 0.5f, yCur + wheelRadius);
		float dx = point.x - wheelCenter.x;
		float dy = point.y - wheelCenter.y;
		float dist = std::sqrt(dx * dx + dy * dy);
		float radiusInner = wheelRadius - 44.0f;

		// 3-1. 외곽 링 클릭
		if (dist >= radiusInner && dist <= wheelRadius + 8.0f)
		{
			m_isDraggingWheel = true;
			float angle = std::atan2(dy, dx) * (180.0f / 3.14159265f);
			if (angle < 0.0f) angle += 360.0f;
			
			if (m_colorTarget == 0) // 채움
			{
				m_fFillHue = angle;
				m_fillColor = HSLtoRGB(m_fFillHue, m_fFillSat, m_fFillLight, m_fOpacity / 100.0f);
			}
			else // 선
			{
				m_fStrokeHue = angle;
				m_strokeColor = HSLtoRGB(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight, m_fOpacity / 100.0f);
			}
			SetCapture();
			ApplyToShape();
			Invalidate(FALSE);
			return;
		}
		// 3-2. 내부 삼각형 클릭 (요구사항 1: 세모 안 색상 선택)
		else if (dist < radiusInner)
		{
			m_isDraggingTriangle = true;
			float activeHue = (m_colorTarget == 0) ? m_fFillHue : m_fStrokeHue;
			D2D1_POINT_2F ptHue, ptWhite, ptBlack;
			GetTriangleVertices(wheelCenter, radiusInner, activeHue, ptHue, ptWhite, ptBlack);

			float wA, wB, wC;
			CalcBarycentric(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)), ptHue, ptWhite, ptBlack, wA, wB, wC);

			float sat, light;
			BarycentricToSL(wA, wB, wC, sat, light);

			if (m_colorTarget == 0) // 채움
			{
				m_fFillSat = sat;
				m_fFillLight = light;
				m_fillColor = HSLtoRGB(m_fFillHue, m_fFillSat, m_fFillLight, m_fOpacity / 100.0f);
			}
			else // 선
			{
				m_fStrokeSat = sat;
				m_fStrokeLight = light;
				m_strokeColor = HSLtoRGB(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight, m_fOpacity / 100.0f);
			}

			SetCapture();
			ApplyToShape();
			Invalidate(FALSE);
			return;
		}
		yCur += wheelRadius * 2.0f + 14.0f; // 스와치 바 위치

		// 4. 스와치 바
		yCur += 26.0f;

		// 5. 불투명도(Opacity) 슬라이더 (0% ~ 100%)
		float sliderLeft = 14.0f;
		float sliderRight = width - 14.0f;
		float sliderW = sliderRight - sliderLeft;
		float opacitySliderY = yCur + 20.0f;
		if (point.y >= opacitySliderY - 5.0f && point.y <= opacitySliderY + 18.0f)
		{
			m_isDraggingOpacitySlider = true;
			float val = ((float)point.x - sliderLeft) / sliderW;
			if (val < 0.0f) val = 0.0f;
			if (val > 1.0f) val = 1.0f;
			m_fOpacity = val * 100.0f;
			if (m_colorTarget == 0)
				m_fillColor = HSLtoRGB(m_fFillHue, 1.0f, 0.5f, m_fOpacity / 100.0f);
			else
				m_strokeColor = HSLtoRGB(m_fStrokeHue, 1.0f, 0.5f, m_fOpacity / 100.0f);

			SetCapture();
			ApplyToShape();
			Invalidate(FALSE);
			return;
		}
		yCur = opacitySliderY + 26.0f;

		// 6. 선(Stroke) 탭일 때만 선 굵기 및 선 종류 아이콘 버튼 상호작용
		if (!isFillActive)
		{
			// 선 굵기(Stroke Width) 슬라이더 (0.5 ~ 20.0px)
			float widthSliderY = yCur + 20.0f;
			if (point.y >= widthSliderY - 5.0f && point.y <= widthSliderY + 18.0f)
			{
				m_isDraggingStrokeWidthSlider = true;
				float frac = ((float)point.x - sliderLeft) / sliderW;
				if (frac < 0.0f) frac = 0.0f;
				if (frac > 1.0f) frac = 1.0f;
				m_fStrokeWidth = 0.5f + frac * 19.5f;
				SetCapture();
				ApplyToShape();
				Invalidate(FALSE);
				return;
			}
			yCur = widthSliderY + 26.0f;

			// 선 스타일 세그먼트 버튼 4구 (실선, 대시선, 점선, 대시점선)
			if (point.y >= yCur && point.y <= yCur + 28.0f)
			{
				float btnW = (width - 28.0f) / 4.0f;
				int idx = static_cast<int>((point.x - 14.0f) / btnW);
				if (idx >= 0 && idx < 4)
				{
					m_selectedDash = idx;
					ApplyToShape();
					Invalidate(FALSE);
					return;
				}
			}
		}
	}
	else if (m_currentTab == TAB_IMGUI)
	{
		// ImGui 가로 Hue 바 클릭 (y: 44 + 20 + 22 + 12 + 20 + 32 = 150)
		float hueBarY = 44.0f + 20.0f + 22.0f + 12.0f + 20.0f + 32.0f;
		if (point.y >= hueBarY && point.y <= hueBarY + 16.0f)
		{
			float hueBarW = width - 32.0f;
			float frac = ((float)point.x - 16.0f) / hueBarW;
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			m_fHue = frac * 360.0f;
			Invalidate(FALSE);
			return;
		}

		// ImGui 체크박스
		float imChkY = hueBarY + 26.0f + 20.0f;
		if (point.x >= 14 && point.x <= width - 14)
		{
			if (point.y >= imChkY && point.y <= imChkY + 18.0f)
			{
				m_bSnapAll = !m_bSnapAll;
				Invalidate(FALSE);
				return;
			}
			if (point.y >= imChkY + 20.0f && point.y <= imChkY + 38.0f)
			{
				m_bSnapBBox = !m_bSnapBBox;
				Invalidate(FALSE);
				return;
			}
			if (point.y >= imChkY + 40.0f && point.y <= imChkY + 58.0f)
			{
				m_bSnapNode = !m_bSnapNode;
				Invalidate(FALSE);
				return;
			}
		}

		// ImGui SliderFloat
		float imSliderY = imChkY + 24.0f + 20.0f + 20.0f + 20.0f;
		if (point.y >= imSliderY && point.y <= imSliderY + 22.0f)
		{
			m_isDraggingSlider = true;
			float val = ((float)point.x - 16.0f) / (width - 32.0f);
			if (val < 0.0f) val = 0.0f;
			if (val > 1.0f) val = 1.0f;
			m_fOpacity = val * 100.0f;
			SetCapture();
			Invalidate(FALSE);
			return;
		}

		// ImGui Combo 클릭 순환
		float comboY = imSliderY + 32.0f + 20.0f;
		if (point.y >= comboY && point.y <= comboY + 24.0f)
		{
			m_selectedBlend = (m_selectedBlend + 1) % 4;
			Invalidate(FALSE);
			return;
		}
	}
	else if (m_currentTab == TAB_MFC)
	{
		// MFC 색상 선택 버튼 클릭 시 Hue 순환
		float mfcColorY = 44.0f + 20.0f + 22.0f + 12.0f + 20.0f;
		if (point.y >= mfcColorY && point.y <= mfcColorY + 24.0f)
		{
			m_fHue = std::fmod(m_fHue + 45.0f, 360.0f);
			Invalidate(FALSE);
			return;
		}

		// MFC 체크박스
		float mfcChkY = mfcColorY + 32.0f + 20.0f;
		if (point.x >= 14 && point.x <= width - 14)
		{
			if (point.y >= mfcChkY && point.y <= mfcChkY + 18.0f)
			{
				m_bSnapAll = !m_bSnapAll;
				Invalidate(FALSE);
				return;
			}
			if (point.y >= mfcChkY + 20.0f && point.y <= mfcChkY + 38.0f)
			{
				m_bSnapBBox = !m_bSnapBBox;
				Invalidate(FALSE);
				return;
			}
			if (point.y >= mfcChkY + 40.0f && point.y <= mfcChkY + 58.0f)
			{
				m_bSnapNode = !m_bSnapNode;
				Invalidate(FALSE);
				return;
			}
		}

		// MFC CSliderCtrl 드래그
		float mfcSliderY = mfcChkY + 24.0f + 20.0f + 20.0f + 20.0f;
		if (point.y >= mfcSliderY && point.y <= mfcSliderY + 22.0f)
		{
			m_isDraggingSlider = true;
			float sLeft = 20.0f;
			float sRight = width - 20.0f;
			float val = ((float)point.x - sLeft) / (sRight - sLeft);
			if (val < 0.0f) val = 0.0f;
			if (val > 1.0f) val = 1.0f;
			m_fOpacity = val * 100.0f;
			SetCapture();
			Invalidate(FALSE);
			return;
		}

		// MFC CComboBox 클릭 순환
		float mfcComboY = mfcSliderY + 28.0f + 20.0f;
		if (point.y >= mfcComboY && point.y <= mfcComboY + 24.0f)
		{
			m_selectedBlend = (m_selectedBlend + 1) % 4;
			Invalidate(FALSE);
			return;
		}
	}
}

void CD2DTestView::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	CRect rc;
	GetClientRect(&rc);
	float width = static_cast<float>(rc.Width());

	if (m_paneMode == MODE_CANVAS)
	{
		GridRenderer& grid = GetApp()->GetGrid();
		float yCur = 248.0f;
		float wheelRadius = max(60.0f, (width - 24.0f) * 0.5f);
		D2D1_POINT_2F wheelCenter = D2D1::Point2F(width * 0.5f, yCur + wheelRadius);
		float radiusInner = wheelRadius - 44.0f;

		if (m_isDraggingWheel)
		{
			float dx = point.x - wheelCenter.x;
			float dy = point.y - wheelCenter.y;
			float angle = std::atan2(dy, dx) * (180.0f / 3.14159265f);
			if (angle < 0.0f) angle += 360.0f;

			if (m_canvasColorTarget == 0)
			{
				m_fMinorHue = angle;
				grid.minor = HSLtoRGB(m_fMinorHue, m_fMinorSat, m_fMinorLight, 0.7f);
			}
			else
			{
				m_fMajorHue = angle;
				grid.major = HSLtoRGB(m_fMajorHue, m_fMajorSat, m_fMajorLight, 0.9f);
			}

			ApplyCanvasSettings();
			Invalidate(FALSE);
		}
		else if (m_isDraggingTriangle)
		{
			float activeHue = (m_canvasColorTarget == 0) ? m_fMinorHue : m_fMajorHue;
			D2D1_POINT_2F ptHue, ptWhite, ptBlack;
			GetTriangleVertices(wheelCenter, radiusInner, activeHue, ptHue, ptWhite, ptBlack);

			float wA, wB, wC;
			CalcBarycentric(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)), ptHue, ptWhite, ptBlack, wA, wB, wC);

			float sat, light;
			BarycentricToSL(wA, wB, wC, sat, light);

			if (m_canvasColorTarget == 0)
			{
				m_fMinorSat = sat;
				m_fMinorLight = light;
				grid.minor = HSLtoRGB(m_fMinorHue, m_fMinorSat, m_fMinorLight, 0.7f);
			}
			else
			{
				m_fMajorSat = sat;
				m_fMajorLight = light;
				grid.major = HSLtoRGB(m_fMajorHue, m_fMajorSat, m_fMajorLight, 0.9f);
			}

			ApplyCanvasSettings();
			Invalidate(FALSE);
		}
		else if (m_isDraggingStepSlider)
		{
			float sliderLeft = 14.0f;
			float sliderRight = width - 14.0f;
			float frac = ((float)point.x - sliderLeft) / (sliderRight - sliderLeft);
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			grid.stepInUnit = 5.0f + frac * 95.0f;
			ApplyCanvasSettings();
			Invalidate(FALSE);
		}
		else if (m_isDraggingMajorSlider)
		{
			float sliderLeft = 14.0f;
			float sliderRight = width - 14.0f;
			float frac = ((float)point.x - sliderLeft) / (sliderRight - sliderLeft);
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			grid.majorEvery = 2 + static_cast<int>(std::round(frac * 18.0f));
			ApplyCanvasSettings();
			Invalidate(FALSE);
		}
		else if (m_isDraggingCanvasOpacity)
		{
			float sliderLeft = 14.0f;
			float sliderRight = width - 14.0f;
			float frac = ((float)point.x - sliderLeft) / (sliderRight - sliderLeft);
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;

			if (m_canvasColorTarget == 0)
			{
				grid.minor.a = frac;
			}
			else
			{
				grid.major.a = frac;
			}

			ApplyCanvasSettings();
			Invalidate(FALSE);
		}
		return;
	}

	if (m_currentTab == TAB_D2D)
	{
		float yCur = 44.0f + 22.0f + 32.0f;
		if (m_colorTarget == 0) // 채움 모드일 때 모드 툴바 36px 추가
		{
			yCur += 36.0f;
		}
		float wheelRadius = max(60.0f, (width - 24.0f) * 0.5f);
		D2D1_POINT_2F wheelCenter = D2D1::Point2F(width * 0.5f, yCur + wheelRadius);
		float radiusInner = wheelRadius - 44.0f;

		if (m_isDraggingWheel)
		{
			float dx = point.x - wheelCenter.x;
			float dy = point.y - wheelCenter.y;
			float angle = std::atan2(dy, dx) * (180.0f / 3.14159265f);
			if (angle < 0.0f) angle += 360.0f;

			if (m_colorTarget == 0) // 채움
			{
				m_fFillHue = angle;
				m_fillColor = HSLtoRGB(m_fFillHue, m_fFillSat, m_fFillLight, m_fOpacity / 100.0f);
			}
			else // 선
			{
				m_fStrokeHue = angle;
				m_strokeColor = HSLtoRGB(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight, m_fOpacity / 100.0f);
			}
			ApplyToShape();
			Invalidate(FALSE);
		}
		else if (m_isDraggingTriangle)
		{
			float activeHue = (m_colorTarget == 0) ? m_fFillHue : m_fStrokeHue;
			D2D1_POINT_2F ptHue, ptWhite, ptBlack;
			GetTriangleVertices(wheelCenter, radiusInner, activeHue, ptHue, ptWhite, ptBlack);

			float wA, wB, wC;
			CalcBarycentric(D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)), ptHue, ptWhite, ptBlack, wA, wB, wC);

			float sat, light;
			BarycentricToSL(wA, wB, wC, sat, light);

			if (m_colorTarget == 0) // 채움
			{
				m_fFillSat = sat;
				m_fFillLight = light;
				m_fillColor = HSLtoRGB(m_fFillHue, m_fFillSat, m_fFillLight, m_fOpacity / 100.0f);
			}
			else // 선
			{
				m_fStrokeSat = sat;
				m_fStrokeLight = light;
				m_strokeColor = HSLtoRGB(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight, m_fOpacity / 100.0f);
			}

			ApplyToShape();
			Invalidate(FALSE);
		}
		else if (m_isDraggingStrokeWidthSlider)
		{
			float sliderLeft = 14.0f;
			float sliderRight = width - 14.0f;
			float frac = ((float)point.x - sliderLeft) / (sliderRight - sliderLeft);
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			m_fStrokeWidth = 0.5f + frac * 19.5f;
			ApplyToShape();
			Invalidate(FALSE);
		}
		else if (m_isDraggingOpacitySlider)
		{
			float sliderLeft = 14.0f;
			float sliderRight = width - 14.0f;
			float val = ((float)point.x - sliderLeft) / (sliderRight - sliderLeft);
			if (val < 0.0f) val = 0.0f;
			if (val > 1.0f) val = 1.0f;
			m_fOpacity = val * 100.0f;
			if (m_colorTarget == 0)
				m_fillColor = HSLtoRGB(m_fFillHue, m_fFillSat, m_fFillLight, m_fOpacity / 100.0f);
			else
				m_strokeColor = HSLtoRGB(m_fStrokeHue, m_fStrokeSat, m_fStrokeLight, m_fOpacity / 100.0f);

			ApplyToShape();
			Invalidate(FALSE);
		}
	}
	else if (m_isDraggingSlider || m_isDraggingOpacitySlider)
	{
		float sliderLeft = (m_currentTab == TAB_MFC) ? 20.0f : 14.0f;
		float sliderRight = (m_currentTab == TAB_MFC) ? (width - 20.0f) : (width - 14.0f);
		float val = ((float)point.x - sliderLeft) / (sliderRight - sliderLeft);
		if (val < 0.0f) val = 0.0f;
		if (val > 1.0f) val = 1.0f;
		m_fOpacity = val * 100.0f;
		Invalidate(FALSE);
	}
}

void CD2DTestView::OnLButtonUp(UINT /*nFlags*/, CPoint /*point*/)
{
	if (m_isDraggingWheel || m_isDraggingTriangle || m_isDraggingOpacitySlider || m_isDraggingStrokeWidthSlider || m_isDraggingSlider ||
		m_isDraggingStepSlider || m_isDraggingMajorSlider || m_isDraggingCanvasOpacity)
	{
		m_isDraggingWheel = false;
		m_isDraggingTriangle = false;
		m_isDraggingOpacitySlider = false;
		m_isDraggingStrokeWidthSlider = false;
		m_isDraggingSlider = false;
		m_isDraggingStepSlider = false;
		m_isDraggingMajorSlider = false;
		m_isDraggingCanvasOpacity = false;
		ReleaseCapture();
	}
}
