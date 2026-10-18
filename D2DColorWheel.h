#pragma once
#include <afxwin.h>
#include <d2d1.h>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Inkscape / Illustrator 스타일의 D2D 컬러휠 컴포넌트
// 외곽 Hue 원형 링 + 내부 채도/명도(Saturation/Lightness) 삼각형 피커
class CD2DColorWheel
{
public:
	using ColorChangedCallback = std::function<void(float hue, float sat, float light, bool isFinal)>;

	CD2DColorWheel();
	~CD2DColorWheel() = default;

	// HSL 색상 변환 유틸리티
	static D2D1_COLOR_F HSLToRGB(float h, float s, float l, float a = 1.0f);
	static void RGBToHSL(const D2D1_COLOR_F& clr, float& h, float& s, float& l);

	// 색상 설정 및 조회
	void SetColorHSL(float hue, float sat, float light);
	void SetColorRGB(const D2D1_COLOR_F& clr);
	float GetHue() const { return m_fHue; }
	float GetSat() const { return m_fSat; }
	float GetLight() const { return m_fLight; }
	D2D1_COLOR_F GetRGB(float alpha = 1.0f) const;

	// 변경 콜백 등록
	void SetOnColorChanged(ColorChangedCallback callback) { m_onColorChanged = callback; }

	// 렌더링
	void Draw(ID2D1RenderTarget* pRenderTarget, ID2D1SolidColorBrush* pBrush, ID2D1Factory* pD2DFactory,
		D2D1_POINT_2F center, float radiusOuter, float ringThickness = 44.0f);

	// 마우스 상호작용 (이벤트 처리 성공 시 true 반환)
	bool OnLButtonDown(CPoint point, D2D1_POINT_2F center, float radiusOuter, float ringThickness = 44.0f);
	bool OnMouseMove(CPoint point, D2D1_POINT_2F center, float radiusOuter, float ringThickness = 44.0f);
	bool OnLButtonUp();

	// 드래그 상태 조회 및 리셋
	bool IsDragging() const { return m_isDraggingWheel || m_isDraggingTriangle; }
	void ResetDrag() { m_isDraggingWheel = false; m_isDraggingTriangle = false; }

	// ===== 공통 D2D UI 위젯 헬퍼 =====
	// 1. 슬라이더 (불투명도, 선굵기, 그리드 간격 등)
	static void DrawSlider(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, IDWriteTextFormat* pTextFmt,
		const wchar_t* label, float left, float right, float& y, float valNorm, D2D1_COLOR_F accentClr);
	static bool HitTestSlider(CPoint pt, float left, float right, float y, float& outValNorm);

	// 2. 색상 스와치 바 (단일 색상 프리뷰)
	static void DrawColorBar(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		float left, float right, float& y, float height, D2D1_COLOR_F color);

	// 3. 세그먼트 버튼 그룹 (채움/선 탭, 단위 선택 버튼, 대시 스타일 버튼 등)
	static void DrawSegmentButtons(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush, IDWriteTextFormat* pTextFmt,
		const wchar_t* const labels[], int count, int selectedIdx, float left, float right, float& y, float height);
	static int HitTestSegmentButtons(CPoint pt, int count, float left, float right, float y, float height);

private:
	float m_fHue = 0.0f;    // 0.0 ~ 360.0
	float m_fSat = 1.0f;    // 0.0 ~ 1.0
	float m_fLight = 0.5f;  // 0.0 ~ 1.0

	bool m_isDraggingWheel = false;
	bool m_isDraggingTriangle = false;

	ColorChangedCallback m_onColorChanged;

	// 수학 헬퍼
	static void GetTriangleVertices(D2D1_POINT_2F center, float radius, float hue,
		D2D1_POINT_2F& ptHue, D2D1_POINT_2F& ptWhite, D2D1_POINT_2F& ptBlack);
	static void CalcBarycentric(D2D1_POINT_2F pt, D2D1_POINT_2F pA, D2D1_POINT_2F pB, D2D1_POINT_2F pC,
		float& wA, float& wB, float& wC);
	static void BarycentricToSL(float wHue, float wWhite, float wBlack, float& sat, float& light);
	static void SLToBarycentric(float sat, float light, float& wHue, float& wWhite, float& wBlack);
};
