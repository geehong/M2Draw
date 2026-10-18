#include "pch.h"
#include "RulerRenderer.h"
#include <cmath>
#include <cwchar>

RulerRenderer::RulerRenderer()
	: enabled(true)
	, thickness(22.0f)
	, bgColor(D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f))
	, borderColor(D2D1::ColorF(0.80f, 0.82f, 0.86f, 1.0f))
	, tickColor(D2D1::ColorF(0.55f, 0.58f, 0.65f, 1.0f))
	, textColor(D2D1::ColorF(0.25f, 0.28f, 0.35f, 1.0f))
	, m_pDWriteFactory(nullptr)
	, m_pTextFormat(nullptr)
{
	HRESULT hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
	);

	if (SUCCEEDED(hr) && m_pDWriteFactory)
	{
		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			9.0f, // 9pt
			L"ko-kr",
			&m_pTextFormat
		);

		if (m_pTextFormat)
		{
			m_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		}

		m_pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			DWRITE_FONT_WEIGHT_BOLD,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			10.0f, // 10pt Bold
			L"ko-kr",
			&m_pTextFormatBold
		);

		if (m_pTextFormatBold)
		{
			m_pTextFormatBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			m_pTextFormatBold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
		}
	}
}

RulerRenderer::~RulerRenderer()
{
	DiscardDeviceResources();
}

void RulerRenderer::DiscardDeviceResources()
{
	if (m_pTextFormatBold)
	{
		m_pTextFormatBold->Release();
		m_pTextFormatBold = nullptr;
	}
	if (m_pTextFormat)
	{
		m_pTextFormat->Release();
		m_pTextFormat = nullptr;
	}
	if (m_pDWriteFactory)
	{
		m_pDWriteFactory->Release();
		m_pDWriteFactory = nullptr;
	}
}

void RulerRenderer::Render(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                          D2D1_SIZE_F view, float zoom, D2D1_POINT_2F pan,
                          const GridRenderer& grid)
{
	if (!enabled || !rt || !brush || zoom <= 0.0f)
		return;

	float dpiX, dpiY;
	rt->GetDpi(&dpiX, &dpiY);
	const float s = dpiX / 96.0f;
	const float lineW = 1.0f / s;
	auto snap = [s](float v) { return (floorf(v * s) + 0.5f) / s; };

	const D2D1_COLOR_F savedColor = brush->GetColor();

	const float T = thickness;
	const float w = view.width;
	const float h = view.height;

	// 1. 상단/좌측 눈금자 배경 및 모서리 채우기
	brush->SetColor(bgColor);
	rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, w, T), brush); // 상단 자
	rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, T, h), brush); // 좌측 자

	// 좌상단 모서리 영역 살짝 진하게 구분
	brush->SetColor(D2D1::ColorF(0.92f, 0.93f, 0.95f, 1.0f));
	rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, T, T), brush);

	// 2. 테두리 구분선 그리기
	brush->SetColor(borderColor);
	float snapT = snap(T);
	rt->DrawLine(D2D1::Point2F(0.0f, snapT), D2D1::Point2F(w, snapT), brush, lineW); // 상단 하단선
	rt->DrawLine(D2D1::Point2F(snapT, 0.0f), D2D1::Point2F(snapT, h), brush, lineW); // 좌측 우측선

	// 단위 이름 모서리에 렌더링 (예: px, mm, in)
	if (m_pTextFormat)
	{
		const wchar_t* suffix = GetCanvasUnitSuffix(grid.unit);
		brush->SetColor(textColor);
		rt->DrawText(
			suffix,
			static_cast<UINT32>(wcslen(suffix)),
			m_pTextFormat,
			D2D1::RectF(3.0f, 3.0f, T, T),
			brush
		);
	}

	// 3. 눈금 및 수치 렌더링 (LOD 및 월드 좌표 동기화)
	float worldStepPx = grid.GetStepPx();
	if (worldStepPx <= 0.0f)
	{
		brush->SetColor(savedColor);
		return;
	}

	// LOD: 눈금 라벨 숫자가 겹치지 않도록 최소 간격 확보 (최소 50 DIP)
	float stepW = worldStepPx;
	float stepUnit = grid.stepInUnit;
	while (stepW * zoom < 50.0f)
	{
		stepW *= 2.0f;
		stepUnit *= 2.0f;
	}
	const float stepPx = stepW * zoom;

	// 상단 눈금자 렌더링 (X축)
	{
		float originScreenX = pan.x + grid.originWorld.x * zoom;
		int i0 = static_cast<int>(ceilf((T - originScreenX) / stepPx));
		int i1 = static_cast<int>(floorf((w - originScreenX) / stepPx));

		for (int i = i0; i <= i1; ++i)
		{
			float screenX = snap(originScreenX + i * stepPx);
			if (screenX < T || screenX > w) continue;

			// 메이저 눈금선
			brush->SetColor(tickColor);
			rt->DrawLine(D2D1::Point2F(screenX, T - 7.0f), D2D1::Point2F(screenX, T), brush, lineW);

			// 중간 서브 눈금 (반칸)
			float subX = snap(screenX + stepPx * 0.5f);
			if (subX >= T && subX <= w)
			{
				rt->DrawLine(D2D1::Point2F(subX, T - 4.0f), D2D1::Point2F(subX, T), brush, lineW);
			}

			// 수치 텍스트 표시
			if (m_pTextFormat)
			{
				wchar_t szNum[32];
				double val = i * stepUnit;
				if (std::abs(val - std::round(val)) < 0.01)
					swprintf_s(szNum, L"%d", static_cast<int>(std::round(val)));
				else
					swprintf_s(szNum, L"%.1f", val);

				brush->SetColor(textColor);
				rt->DrawText(
					szNum,
					static_cast<UINT32>(wcslen(szNum)),
					m_pTextFormat,
					D2D1::RectF(screenX + 2.0f, 2.0f, screenX + 60.0f, T - 2.0f),
					brush
				);
			}
		}
	}

	// 좌측 눈금자 렌더링 (Y축)
	{
		float originScreenY = pan.y + grid.originWorld.y * zoom;
		int i0 = static_cast<int>(ceilf((T - originScreenY) / stepPx));
		int i1 = static_cast<int>(floorf((h - originScreenY) / stepPx));

		for (int i = i0; i <= i1; ++i)
		{
			float screenY = snap(originScreenY + i * stepPx);
			if (screenY < T || screenY > h) continue;

			// 메이저 눈금선
			brush->SetColor(tickColor);
			rt->DrawLine(D2D1::Point2F(T - 7.0f, screenY), D2D1::Point2F(T, screenY), brush, lineW);

			// 중간 서브 눈금 (반칸)
			float subY = snap(screenY + stepPx * 0.5f);
			if (subY >= T && subY <= h)
			{
				rt->DrawLine(D2D1::Point2F(T - 4.0f, subY), D2D1::Point2F(T, subY), brush, lineW);
			}

			// 수치 텍스트 표시
			if (m_pTextFormat)
			{
				wchar_t szNum[32];
				double val = i * stepUnit;
				if (std::abs(val - std::round(val)) < 0.01)
					swprintf_s(szNum, L"%d", static_cast<int>(std::round(val)));
				else
					swprintf_s(szNum, L"%.1f", val);

				brush->SetColor(textColor);
				rt->DrawText(
					szNum,
					static_cast<UINT32>(wcslen(szNum)),
					m_pTextFormat,
					D2D1::RectF(2.0f, screenY + 2.0f, T - 2.0f, screenY + 20.0f),
					brush
				);
			}
		}
	}

	// 4. X, Y 기준축 인디케이터 (네모친 부분 표시: X축은 가로선의 우측 끝에 X, Y축은 세로선의 상단 끝에 Y)
	if (grid.showAxis && m_pTextFormatBold)
	{
		float originScreenX = snap(pan.x + grid.originWorld.x * zoom);
		float originScreenY = snap(pan.y + grid.originWorld.y * zoom);

		// (1) Y축 인디케이터: 상단 눈금자 위치(originScreenX) 상단에 'Y' 배지 박스 표시 (녹색 계열)
		if (originScreenX >= T && originScreenX <= w)
		{
			D2D1_RECT_F rYTag = D2D1::RectF(originScreenX - 9.0f, 1.0f, originScreenX + 9.0f, T - 2.0f);
			// 불투명 눈금자 배경 박스를 깔아서 뒤의 눈금선/숫자에 가려지지 않게 처리
			brush->SetColor(D2D1::ColorF(0.96f, 0.96f, 0.97f, 0.95f));
			rt->FillRoundedRectangle(D2D1::RoundedRect(rYTag, 2.0f, 2.0f), brush);
			brush->SetColor(grid.axisYColor);
			rt->DrawRoundedRectangle(D2D1::RoundedRect(rYTag, 2.0f, 2.0f), brush, 1.2f);
			rt->DrawText(L"Y", 1, m_pTextFormatBold, rYTag, brush);
		}

		// (2) X축 인디케이터: 화면 우측 끝 X축 기준선 위치(originScreenY)에 'X' 배지 박스 표시 (붉은색 계열)
		if (originScreenY >= T && originScreenY <= h)
		{
			D2D1_RECT_F rXTag = D2D1::RectF(w - 20.0f, originScreenY - 9.0f, w - 2.0f, originScreenY + 9.0f);
			// 불투명 배경 박스
			brush->SetColor(D2D1::ColorF(0.96f, 0.96f, 0.97f, 0.95f));
			rt->FillRoundedRectangle(D2D1::RoundedRect(rXTag, 2.0f, 2.0f), brush);
			brush->SetColor(grid.axisXColor);
			rt->DrawRoundedRectangle(D2D1::RoundedRect(rXTag, 2.0f, 2.0f), brush, 1.2f);
			rt->DrawText(L"X", 1, m_pTextFormatBold, rXTag, brush);
		}
	}

	brush->SetColor(savedColor);
}
