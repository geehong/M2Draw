#include "pch.h"
#include "M2DrawControl.h"
#include <algorithm>
#include <cmath>

// ============================================================================
// M2DrawControl 기본 클래스 구현
// ============================================================================
M2DrawControl::M2DrawControl()
{
}

void M2DrawControl::SetPosition(float x, float y)
{
	float w = GetWidth();
	float h = GetHeight();
	m_bounds = D2D1::RectF(x, y, x + w, y + h);
}

void M2DrawControl::SetSize(float width, float height)
{
	m_bounds.right = m_bounds.left + width;
	m_bounds.bottom = m_bounds.top + height;
}

bool M2DrawControl::HitTest(D2D1_POINT_2F pt) const
{
	if (!m_bVisible) return false;
	return (pt.x >= m_bounds.left && pt.x <= m_bounds.right &&
		pt.y >= m_bounds.top && pt.y <= m_bounds.bottom);
}

bool M2DrawControl::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;
	if (HitTest(pt))
	{
		m_bPressed = true;
		m_bFocused = true;
		return true;
	}
	m_bFocused = false;
	return false;
}

bool M2DrawControl::OnMouseMove(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;
	m_bHovered = HitTest(pt);
	return m_bHovered;
}

bool M2DrawControl::OnLButtonUp(D2D1_POINT_2F /*pt*/)
{
	if (m_bPressed)
	{
		m_bPressed = false;
		return true;
	}
	return false;
}

bool M2DrawControl::OnKeyDown(UINT /*nChar*/)
{
	return false;
}

bool M2DrawControl::OnChar(UINT /*nChar*/)
{
	return false;
}

void M2DrawControl::ResetState()
{
	m_bHovered = false;
	m_bPressed = false;
	m_bFocused = false;
}

// 1. 공통 프레임 그리기 (호버/클릭 상태 반응형 둥근 모서리 사각형)
void M2DrawControl::DrawControlFrame(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	const D2D1_RECT_F& rect,
	float cornerRadius,
	D2D1_COLOR_F customBaseColor)
{
	if (!pRT || !pBrush) return;

	D2D1_COLOR_F fillColor = customBaseColor;
	D2D1_COLOR_F borderColor = D2D1::ColorF(0.32f, 0.32f, 0.36f, 0.9f);

	if (!m_bEnabled)
	{
		fillColor = D2D1::ColorF(customBaseColor.r * 0.6f, customBaseColor.g * 0.6f, customBaseColor.b * 0.6f, 0.5f);
		borderColor = D2D1::ColorF(0.24f, 0.24f, 0.27f, 0.4f);
	}
	else if (m_bPressed)
	{
		fillColor = D2D1::ColorF(customBaseColor.r * 0.8f, customBaseColor.g * 0.8f, customBaseColor.b * 0.8f, 1.0f);
		borderColor = D2D1::ColorF(0.0f, 0.55f, 0.95f, 1.0f);
	}
	else if (m_bHovered)
	{
		fillColor = D2D1::ColorF(
			min(1.0f, customBaseColor.r * 1.25f + 0.05f),
			min(1.0f, customBaseColor.g * 1.25f + 0.05f),
			min(1.0f, customBaseColor.b * 1.25f + 0.05f),
			1.0f
		);
		borderColor = D2D1::ColorF(0.48f, 0.48f, 0.54f, 1.0f);
	}

	D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect, cornerRadius, cornerRadius);
	pBrush->SetColor(fillColor);
	pRT->FillRoundedRectangle(rr, pBrush);

	pBrush->SetColor(borderColor);
	pRT->DrawRoundedRectangle(rr, pBrush, 1.0f);

	if (m_bFocused && m_bEnabled)
	{
		DrawFocusRing(pRT, pBrush, rect, cornerRadius);
	}
}

// 2. 공통 텍스트 라벨 그리기
void M2DrawControl::DrawTextLabel(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteTextFormat* pFormat,
	const wchar_t* text,
	const D2D1_RECT_F& rect,
	DWRITE_TEXT_ALIGNMENT align,
	DWRITE_PARAGRAPH_ALIGNMENT pAlign,
	D2D1_COLOR_F textColor)
{
	if (!pRT || !pBrush || !pFormat || !text) return;

	pFormat->SetTextAlignment(align);
	pFormat->SetParagraphAlignment(pAlign);

	if (!m_bEnabled)
	{
		textColor = D2D1::ColorF(0.48f, 0.48f, 0.52f, 0.7f);
	}

	pBrush->SetColor(textColor);
	pRT->DrawText(text, static_cast<UINT32>(wcslen(text)), pFormat, rect, pBrush);
}

// 3. 공통 체크마크 (✓) 기호 그리기
void M2DrawControl::DrawCheckMark(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	const D2D1_RECT_F& boxRect,
	D2D1_COLOR_F checkColor)
{
	if (!pRT || !pBrush) return;

	float w = boxRect.right - boxRect.left;
	float h = boxRect.bottom - boxRect.top;

	D2D1_POINT_2F p1 = D2D1::Point2F(boxRect.left + w * 0.25f, boxRect.top + h * 0.50f);
	D2D1_POINT_2F p2 = D2D1::Point2F(boxRect.left + w * 0.45f, boxRect.top + h * 0.72f);
	D2D1_POINT_2F p3 = D2D1::Point2F(boxRect.left + w * 0.78f, boxRect.top + h * 0.28f);

	pBrush->SetColor(checkColor);
	pRT->DrawLine(p1, p2, pBrush, 2.0f);
	pRT->DrawLine(p2, p3, pBrush, 2.0f);
}

// 4. 공통 방향 화살표 기호 그리기
void M2DrawControl::DrawArrow(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	const D2D1_RECT_F& rect,
	ArrowDirection dir,
	float size,
	D2D1_COLOR_F color)
{
	if (!pRT || !pBrush) return;

	float cx = (rect.left + rect.right) * 0.5f;
	float cy = (rect.top + rect.bottom) * 0.5f;

	D2D1_POINT_2F pA, pB, pC;

	switch (dir)
	{
	case ArrowDirection::Up:
		pA = D2D1::Point2F(cx, cy - size * 0.7f);
		pB = D2D1::Point2F(cx - size, cy + size * 0.5f);
		pC = D2D1::Point2F(cx + size, cy + size * 0.5f);
		break;
	case ArrowDirection::Down:
		pA = D2D1::Point2F(cx, cy + size * 0.7f);
		pB = D2D1::Point2F(cx - size, cy - size * 0.5f);
		pC = D2D1::Point2F(cx + size, cy - size * 0.5f);
		break;
	case ArrowDirection::Left:
		pA = D2D1::Point2F(cx - size * 0.7f, cy);
		pB = D2D1::Point2F(cx + size * 0.5f, cy - size);
		pC = D2D1::Point2F(cx + size * 0.5f, cy + size);
		break;
	case ArrowDirection::Right:
	default:
		pA = D2D1::Point2F(cx + size * 0.7f, cy);
		pB = D2D1::Point2F(cx - size * 0.5f, cy - size);
		pC = D2D1::Point2F(cx - size * 0.5f, cy + size);
		break;
	}

	pBrush->SetColor(color);

	// 꽉 찬 삼각형 글리프 렌더링
	D2D1_POINT_2F topPt = (dir == ArrowDirection::Up) ? pA : ((dir == ArrowDirection::Down) ? pA : pA);
	// 외곽선뿐만 아니라 명확하게 보이도록 굵은 선 및 십자 교차
	pRT->DrawLine(pB, pA, pBrush, 1.8f);
	pRT->DrawLine(pA, pC, pBrush, 1.8f);
	pRT->DrawLine(pB, pC, pBrush, 1.2f);

	// 내부를 채우는 횡선 추가
	float innerMidY = (pA.y + pB.y) * 0.5f;
	float innerMidX1 = (pA.x + pB.x) * 0.5f;
	float innerMidX2 = (pA.x + pC.x) * 0.5f;
	pRT->DrawLine(D2D1::Point2F(innerMidX1, innerMidY), D2D1::Point2F(innerMidX2, innerMidY), pBrush, 1.5f);
}

// 5. 공통 포커스 링
void M2DrawControl::DrawFocusRing(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	const D2D1_RECT_F& rect,
	float cornerRadius,
	D2D1_COLOR_F ringColor)
{
	if (!pRT || !pBrush) return;
	D2D1_RECT_F fr = D2D1::RectF(rect.left - 1.5f, rect.top - 1.5f, rect.right + 1.5f, rect.bottom + 1.5f);
	D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(fr, cornerRadius + 1.5f, cornerRadius + 1.5f);
	pBrush->SetColor(ringColor);
	pRT->DrawRoundedRectangle(rr, pBrush, 1.5f);
}

// ============================================================================
// 1. M2DrawButton 구현
// ============================================================================
M2DrawButton::M2DrawButton(const std::wstring& text)
	: m_text(text)
{
}

void M2DrawButton::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	D2D1_COLOR_F baseColor = D2D1::ColorF(0.22f, 0.22f, 0.26f, 1.0f);
	if (m_bToggleMode && m_bChecked)
	{
		baseColor = m_bUseAccent ? m_accentColor : D2D1::ColorF(0.0f, 0.48f, 0.85f, 1.0f);
	}

	DrawControlFrame(pRT, pBrush, m_bounds, 4.0f, baseColor);

	D2D1_COLOR_F textColor = (m_bToggleMode && m_bChecked)
		? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
		: D2D1::ColorF(0.92f, 0.92f, 0.95f, 1.0f);

	DrawTextLabel(pRT, pBrush, pTextFormat, m_text.c_str(), m_bounds,
		DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, textColor);
}

bool M2DrawButton::OnLButtonDown(D2D1_POINT_2F pt)
{
	return M2DrawControl::OnLButtonDown(pt);
}

bool M2DrawButton::OnLButtonUp(D2D1_POINT_2F pt)
{
	if (m_bPressed)
	{
		m_bPressed = false;
		if (HitTest(pt))
		{
			if (m_bToggleMode)
			{
				m_bChecked = !m_bChecked;
			}
			if (m_onClick)
			{
				m_onClick(this);
			}
		}
		return true;
	}
	return false;
}

// ============================================================================
// 2. M2DrawCheckBox 구현
// ============================================================================
M2DrawCheckBox::M2DrawCheckBox(const std::wstring& text, bool bChecked)
	: m_text(text)
	, m_bChecked(bChecked)
{
}

void M2DrawCheckBox::SetChecked(bool b)
{
	if (m_bChecked != b)
	{
		m_bChecked = b;
		if (m_onChanged)
		{
			m_onChanged(this, m_bChecked);
		}
	}
}

void M2DrawCheckBox::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	float h = m_bounds.bottom - m_bounds.top;
	float boxSize = min(16.0f, h - 2.0f);
	float boxTop = m_bounds.top + (h - boxSize) * 0.5f;
	D2D1_RECT_F boxRect = D2D1::RectF(m_bounds.left, boxTop, m_bounds.left + boxSize, boxTop + boxSize);

	D2D1_COLOR_F baseBoxClr = m_bChecked
		? D2D1::ColorF(0.0f, 0.48f, 0.85f, 1.0f)
		: D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f);

	DrawControlFrame(pRT, pBrush, boxRect, 3.0f, baseBoxClr);

	if (m_bChecked)
	{
		DrawCheckMark(pRT, pBrush, boxRect, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	}

	D2D1_RECT_F textRect = D2D1::RectF(boxRect.right + 8.0f, m_bounds.top, m_bounds.right, m_bounds.bottom);
	DrawTextLabel(pRT, pBrush, pTextFormat, m_text.c_str(), textRect,
		DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

bool M2DrawCheckBox::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;
	if (HitTest(pt))
	{
		SetChecked(!m_bChecked);
		return true;
	}
	return false;
}

// ============================================================================
// 3. M2DrawSlider 구현
// ============================================================================
M2DrawSlider::M2DrawSlider(float minVal, float maxVal, float curVal)
	: m_minVal(minVal)
	, m_maxVal(maxVal)
	, m_val(curVal)
{
}

void M2DrawSlider::SetRange(float minVal, float maxVal)
{
	m_minVal = minVal;
	m_maxVal = maxVal;
	m_val = min(m_maxVal, max(m_minVal, m_val));
}

void M2DrawSlider::SetValue(float val)
{
	float clamped = min(m_maxVal, max(m_minVal, val));
	if (std::abs(m_val - clamped) > 0.0001f)
	{
		m_val = clamped;
		if (m_onChanged)
		{
			m_onChanged(this, m_val);
		}
	}
}

float M2DrawSlider::GetNormalizedValue() const
{
	if (std::abs(m_maxVal - m_minVal) < 0.0001f) return 0.0f;
	return (m_val - m_minVal) / (m_maxVal - m_minVal);
}

void M2DrawSlider::UpdateValueFromX(float mouseX)
{
	float norm = (mouseX - m_bounds.left) / GetWidth();
	norm = min(1.0f, max(0.0f, norm));
	float rawVal = m_minVal + norm * (m_maxVal - m_minVal);
	float roundedVal = std::round(rawVal * 10.0f) / 10.0f;
	SetValue(roundedVal);
}

void M2DrawSlider::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	float midY = (m_bounds.top + m_bounds.bottom) * 0.5f;
	float trackH = 4.0f;
	D2D1_RECT_F trackRect = D2D1::RectF(m_bounds.left, midY - trackH * 0.5f, m_bounds.right, midY + trackH * 0.5f);

	// 1. 트랙 배경
	pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
	pRT->FillRoundedRectangle(D2D1::RoundedRect(trackRect, 2.0f, 2.0f), pBrush);

	// 2. 채워진 트랙
	float norm = GetNormalizedValue();
	float thumbX = m_bounds.left + norm * GetWidth();
	D2D1_RECT_F fillRect = D2D1::RectF(m_bounds.left, trackRect.top, thumbX, trackRect.bottom);
	pBrush->SetColor(m_accentColor);
	pRT->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 2.0f, 2.0f), pBrush);

	// 3. 손잡이 (Thumb)
	float thumbR = (m_isDragging || m_bHovered) ? 7.0f : 5.5f;
	pBrush->SetColor(D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f));
	pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, midY), thumbR, thumbR), pBrush);

	pBrush->SetColor(m_accentColor);
	pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, midY), thumbR, thumbR), pBrush, 2.0f);
}

bool M2DrawSlider::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;
	if (HitTest(pt))
	{
		m_isDragging = true;
		m_bPressed = true;
		UpdateValueFromX(pt.x);
		return true;
	}
	return false;
}

bool M2DrawSlider::OnMouseMove(D2D1_POINT_2F pt)
{
	M2DrawControl::OnMouseMove(pt);
	if (m_isDragging)
	{
		UpdateValueFromX(pt.x);
		return true;
	}
	return m_bHovered;
}

bool M2DrawSlider::OnLButtonUp(D2D1_POINT_2F /*pt*/)
{
	if (m_isDragging)
	{
		m_isDragging = false;
		m_bPressed = false;
		return true;
	}
	return false;
}

// ============================================================================
// 4. M2DrawNumericInput (스핀 박스) 구현
// ============================================================================
M2DrawNumericInput::M2DrawNumericInput(double minVal, double maxVal, double curVal, double step)
	: m_minVal(minVal)
	, m_maxVal(maxVal)
	, m_val(curVal)
	, m_step(step)
{
}

void M2DrawNumericInput::SetValue(double val)
{
	double clamped = min(m_maxVal, max(m_minVal, val));
	if (std::abs(m_val - clamped) > 0.0001)
	{
		m_val = clamped;
		if (m_onChanged)
		{
			m_onChanged(this, m_val);
		}
	}
}

D2D1_RECT_F M2DrawNumericInput::GetUpButtonRect() const
{
	float btnW = 24.0f;
	float midY = (m_bounds.top + m_bounds.bottom) * 0.5f;
	return D2D1::RectF(m_bounds.right - btnW, m_bounds.top, m_bounds.right, midY);
}

D2D1_RECT_F M2DrawNumericInput::GetDownButtonRect() const
{
	float btnW = 24.0f;
	float midY = (m_bounds.top + m_bounds.bottom) * 0.5f;
	return D2D1::RectF(m_bounds.right - btnW, midY, m_bounds.right, m_bounds.bottom);
}

D2D1_RECT_F M2DrawNumericInput::GetTextRect() const
{
	return D2D1::RectF(m_bounds.left + 8.0f, m_bounds.top, m_bounds.right - 26.0f, m_bounds.bottom);
}

void M2DrawNumericInput::CommitEdit()
{
	if (!m_bEditing) return;
	m_bEditing = false;
	try
	{
		if (!m_editText.empty())
		{
			double v = std::stod(m_editText);
			SetValue(v);
		}
	}
	catch (...)
	{
	}
	m_editText.clear();
}

void M2DrawNumericInput::CancelEdit()
{
	m_bEditing = false;
	m_editText.clear();
}

void M2DrawNumericInput::ResetState()
{
	if (m_bEditing)
	{
		CommitEdit();
	}
	M2DrawControl::ResetState();
}

void M2DrawNumericInput::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	// 프레임 박스: 편집 중일 때는 포커스 강조 테두리 색상 사용
	if (m_bEditing)
	{
		DrawControlFrame(pRT, pBrush, m_bounds, 4.0f, D2D1::ColorF(0.12f, 0.12f, 0.16f, 1.0f));
		pBrush->SetColor(D2D1::ColorF(0.0f, 0.48f, 0.85f, 1.0f));
		pRT->DrawRoundedRectangle(D2D1::RoundedRect(m_bounds, 4.0f, 4.0f), pBrush, 1.5f);
	}
	else
	{
		DrawControlFrame(pRT, pBrush, m_bounds, 4.0f, D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));
	}

	D2D1_RECT_F textRect = GetTextRect();

	if (m_bEditing)
	{
		// 편집 중: 사용자가 입력 중인 텍스트 표시 + 깜빡이는 캐럿(커서) 표시
		std::wstring disp = m_editText;
		DrawTextLabel(pRT, pBrush, pTextFormat, disp.c_str(), textRect,
			DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));

		// 커서 렌더링 (500ms 깜빡임)
		if ((::GetTickCount64() / 500) % 2 == 0)
		{
			float charWidth = 7.0f; // 평균 문자 폭 추정
			float caretX = min(textRect.right - 4.0f, textRect.left + disp.length() * charWidth);
			pBrush->SetColor(D2D1::ColorF(0.0f, 0.68f, 1.0f, 1.0f));
			pRT->DrawLine(
				D2D1::Point2F(caretX, textRect.top + 5.0f),
				D2D1::Point2F(caretX, textRect.bottom - 5.0f),
				pBrush, 1.5f
			);
		}
	}
	else
	{
		// 기본 표시: 소수점 1자리(정수면 정수 표기)
		wchar_t numBuf[32];
		bool isInteger = (std::abs(m_val - std::round(m_val)) < 0.0001);
		if (m_decimals <= 0 || isInteger)
		{
			swprintf_s(numBuf, L"%.0f", m_val);
		}
		else
		{
			// 소수점 1자리 기본
			swprintf_s(numBuf, L"%.1f", m_val);
		}

		wchar_t buf[64];
		if (m_suffix.empty())
		{
			swprintf_s(buf, L"%s", numBuf);
		}
		else
		{
			swprintf_s(buf, L"%s %s", numBuf, m_suffix.c_str());
		}

		DrawTextLabel(pRT, pBrush, pTextFormat, buf, textRect,
			DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f));
	}

	// 증/감 단추 (▲, ▼)
	D2D1_RECT_F rUp = GetUpButtonRect();
	D2D1_RECT_F rDown = GetDownButtonRect();

	// 버튼 배경 및 호버/클릭 효과
	pBrush->SetColor(m_bHoverUp ? D2D1::ColorF(0.35f, 0.35f, 0.42f, 1.0f) : D2D1::ColorF(0.24f, 0.24f, 0.28f, 1.0f));
	pRT->FillRectangle(rUp, pBrush);

	pBrush->SetColor(m_bHoverDown ? D2D1::ColorF(0.35f, 0.35f, 0.42f, 1.0f) : D2D1::ColorF(0.24f, 0.24f, 0.28f, 1.0f));
	pRT->FillRectangle(rDown, pBrush);

	// 화살표 렌더링 (밝고 선명한 화이트 색상, 4.5f 크기)
	D2D1_COLOR_F arrowColor = D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f);
	DrawArrow(pRT, pBrush, rUp, ArrowDirection::Up, 4.5f, arrowColor);
	DrawArrow(pRT, pBrush, rDown, ArrowDirection::Down, 4.5f, arrowColor);

	// 버튼과 입력창 사이의 구분선
	pBrush->SetColor(D2D1::ColorF(0.35f, 0.35f, 0.40f, 1.0f));
	pRT->DrawLine(D2D1::Point2F(rUp.left, m_bounds.top), D2D1::Point2F(rUp.left, m_bounds.bottom), pBrush, 1.0f);
	pRT->DrawLine(D2D1::Point2F(rUp.left, rUp.bottom), D2D1::Point2F(rUp.right, rUp.bottom), pBrush, 1.0f);
}

bool M2DrawNumericInput::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;
	D2D1_RECT_F rUp = GetUpButtonRect();
	D2D1_RECT_F rDown = GetDownButtonRect();

	if (pt.x >= rUp.left && pt.x <= rUp.right)
	{
		if (m_bEditing) CommitEdit();

		if (pt.y >= rUp.top && pt.y <= rUp.bottom)
		{
			SetValue(m_val + m_step);
			m_bPressUp = true;
			return true;
		}
		if (pt.y >= rDown.top && pt.y <= rDown.bottom)
		{
			SetValue(m_val - m_step);
			m_bPressDown = true;
			return true;
		}
	}

	// 텍스트 영역 클릭 시 인라인 직접 입력 모드 활성화
	D2D1_RECT_F textRect = GetTextRect();
	if (pt.x >= textRect.left && pt.x <= textRect.right &&
		pt.y >= textRect.top && pt.y <= textRect.bottom)
	{
		m_bEditing = true;
		m_bFocused = true;
		// 현재 값을 편집 버퍼로 로드 (소수점 1자리 또는 정수)
		wchar_t buf[32];
		if (m_decimals <= 0 || (std::abs(m_val - std::round(m_val)) < 0.0001))
			swprintf_s(buf, L"%.0f", m_val);
		else
			swprintf_s(buf, L"%.1f", m_val);
		m_editText = buf;
		return true;
	}

	if (m_bEditing)
	{
		CommitEdit();
	}
	return M2DrawControl::OnLButtonDown(pt);
}

bool M2DrawNumericInput::OnKeyDown(UINT nChar)
{
	if (!m_bEditing) return false;

	if (nChar == VK_RETURN)
	{
		CommitEdit();
		return true;
	}
	else if (nChar == VK_ESCAPE)
	{
		CancelEdit();
		return true;
	}
	else if (nChar == VK_BACK)
	{
		if (!m_editText.empty())
		{
			m_editText.pop_back();
		}
		return true;
	}
	return false;
}

bool M2DrawNumericInput::OnChar(UINT nChar)
{
	if (!m_bEditing) return false;

	// 숫자(0~9), 마이너스(-), 소수점(.) 허용
	if ((nChar >= '0' && nChar <= '9') || nChar == '.' || nChar == '-')
	{
		if (nChar == '.' && m_editText.find(L'.') != std::wstring::npos)
		{
			return true; // 이미 소수점이 있으면 무시
		}
		if (nChar == '-' && !m_editText.empty())
		{
			return true; // 맨 앞에만 부호 허용
		}
		if (m_editText.length() < 16)
		{
			m_editText.push_back(static_cast<wchar_t>(nChar));
		}
		return true;
	}
	return false;
}

bool M2DrawNumericInput::OnMouseMove(D2D1_POINT_2F pt)
{
	M2DrawControl::OnMouseMove(pt);
	D2D1_RECT_F rUp = GetUpButtonRect();
	D2D1_RECT_F rDown = GetDownButtonRect();

	m_bHoverUp = (pt.x >= rUp.left && pt.x <= rUp.right && pt.y >= rUp.top && pt.y <= rUp.bottom);
	m_bHoverDown = (pt.x >= rDown.left && pt.x <= rDown.right && pt.y >= rDown.top && pt.y <= rDown.bottom);
	return m_bHovered;
}

bool M2DrawNumericInput::OnLButtonUp(D2D1_POINT_2F pt)
{
	m_bPressUp = false;
	m_bPressDown = false;
	return M2DrawControl::OnLButtonUp(pt);
}

// ============================================================================
// 5. M2DrawLabel 구현
// ============================================================================
M2DrawLabel::M2DrawLabel(const std::wstring& text)
	: m_text(text)
{
}

void M2DrawLabel::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;
	DrawTextLabel(pRT, pBrush, pTextFormat, m_text.c_str(), m_bounds, m_align,
		DWRITE_PARAGRAPH_ALIGNMENT_CENTER, m_textColor);
}

// ============================================================================
// 6. M2DrawColorWheel 구현
// ============================================================================
namespace
{
	D2D1_COLOR_F WheelHSLToRGB(float h, float s, float l, float a = 1.0f)
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
}

M2DrawColorWheel::M2DrawColorWheel()
{
}

void M2DrawColorWheel::SetColorHSL(float h, float s, float l)
{
	m_fHue = min(360.0f, max(0.0f, h));
	m_fSat = min(1.0f, max(0.0f, s));
	m_fLight = min(1.0f, max(0.0f, l));
}

void M2DrawColorWheel::SetColorRGB(const D2D1_COLOR_F& clr)
{
	float r = clr.r, g = clr.g, b = clr.b;
	float maxVal = max(r, max(g, b));
	float minVal = min(r, min(g, b));
	float delta = maxVal - minVal;

	m_fLight = (maxVal + minVal) * 0.5f;
	if (delta < 0.0001f)
	{
		m_fHue = 0.0f;
		m_fSat = 0.0f;
		return;
	}

	m_fSat = (m_fLight > 0.5f) ? (delta / (2.0f - maxVal - minVal)) : (delta / (maxVal + minVal));
	if (maxVal == r)
		m_fHue = 60.0f * std::fmod((g - b) / delta, 6.0f);
	else if (maxVal == g)
		m_fHue = 60.0f * (((b - r) / delta) + 2.0f);
	else
		m_fHue = 60.0f * (((r - g) / delta) + 4.0f);

	if (m_fHue < 0.0f) m_fHue += 360.0f;
}

D2D1_COLOR_F M2DrawColorWheel::GetRGB(float alpha) const
{
	return WheelHSLToRGB(m_fHue, m_fSat, m_fLight, alpha);
}

void M2DrawColorWheel::CalcBarycentric(D2D1_POINT_2F pt, D2D1_POINT_2F pA, D2D1_POINT_2F pB, D2D1_POINT_2F pC,
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

void M2DrawColorWheel::BarycentricToSL(float wHue, float wWhite, float wBlack, float& sat, float& light)
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

void M2DrawColorWheel::SLToBarycentric(float sat, float light, float& wHue, float& wWhite, float& wBlack)
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

void M2DrawColorWheel::GetMetrics(D2D1_POINT_2F& center, float& radiusOuter, float& radiusInner) const
{
	float w = GetWidth();
	float h = GetHeight();
	center = D2D1::Point2F(m_bounds.left + w * 0.5f, m_bounds.top + h * 0.5f);
	radiusOuter = min(w, h) * 0.495f; // 테두리 끝까지 꽉 차게 확장
	float ringThickness = 36.0f; // 시인성이 우수하고 쾌적한 굵은 링
	radiusInner = max(10.0f, radiusOuter - ringThickness);
}

void M2DrawColorWheel::GetTriangleVertices(D2D1_POINT_2F center, float radiusInner,
	D2D1_POINT_2F& ptHue, D2D1_POINT_2F& ptWhite, D2D1_POINT_2F& ptBlack) const
{
	float radHue = static_cast<float>(m_fHue * (M_PI / 180.0));
	float radWhite = radHue + static_cast<float>((2.0 * M_PI) / 3.0);
	float radBlack = radHue - static_cast<float>((2.0 * M_PI) / 3.0);

	ptHue = D2D1::Point2F(center.x + std::cos(radHue) * radiusInner, center.y + std::sin(radHue) * radiusInner);
	ptWhite = D2D1::Point2F(center.x + std::cos(radWhite) * radiusInner, center.y + std::sin(radWhite) * radiusInner);
	ptBlack = D2D1::Point2F(center.x + std::cos(radBlack) * radiusInner, center.y + std::sin(radBlack) * radiusInner);
}

void M2DrawColorWheel::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* /*pTextFormat*/)
{
	if (!m_bVisible || !pRT || !pBrush) return;

	D2D1_POINT_2F center;
	float radiusOuter, radiusInner;
	GetMetrics(center, radiusOuter, radiusInner);

	// 1. 외곽 Hue 링 렌더링
	const int segments = 180;
	const float stepRad = static_cast<float>((2.0 * M_PI) / segments);

	for (int i = 0; i < segments; ++i)
	{
		float a1 = i * stepRad;
		float a2 = (i + 1.2f) * stepRad;
		float hueMid = (i + 0.5f) * (360.0f / segments);

		float midR = (radiusOuter + radiusInner) * 0.5f;
		float thick = radiusOuter - radiusInner;
		float mx = center.x + std::cos(a1) * midR;
		float my = center.y + std::sin(a1) * midR;
		float nx = center.x + std::cos(a2) * midR;
		float ny = center.y + std::sin(a2) * midR;

		pBrush->SetColor(WheelHSLToRGB(hueMid, 1.0f, 0.5f));
		pRT->DrawLine(D2D1::Point2F(mx, my), D2D1::Point2F(nx, ny), pBrush, thick);
	}

	// 링 테두리
	pBrush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.9f));
	pRT->DrawEllipse(D2D1::Ellipse(center, radiusOuter, radiusOuter), pBrush, 1.0f);
	pRT->DrawEllipse(D2D1::Ellipse(center, radiusInner, radiusInner), pBrush, 1.0f);

	// 2. 내부 삼각형 지오메트리 & 리니어 그라데이션
	D2D1_POINT_2F ptHue, ptWhite, ptBlack;
	GetTriangleVertices(center, radiusInner, ptHue, ptWhite, ptBlack);

	ID2D1Factory* pFactory = nullptr;
	pRT->GetFactory(&pFactory);

	if (pFactory)
	{
		ID2D1PathGeometry* pTriGeo = nullptr;
		pFactory->CreatePathGeometry(&pTriGeo);
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
				stops[1].color = WheelHSLToRGB(m_fHue, 1.0f, 0.5f);

				pRT->CreateGradientStopCollection(stops, 2, &pStops);
				if (pStops)
				{
					ID2D1LinearGradientBrush* pLinBrush = nullptr;
					pRT->CreateLinearGradientBrush(
						D2D1::LinearGradientBrushProperties(ptBaseMid, ptHue),
						pStops,
						&pLinBrush
					);
					if (pLinBrush)
					{
						pRT->FillGeometry(pTriGeo, pLinBrush);
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

				pRT->CreateGradientStopCollection(stopsBW, 2, &pStopsBW);
				if (pStopsBW)
				{
					ID2D1LinearGradientBrush* pLinBrushBW = nullptr;
					pRT->CreateLinearGradientBrush(
						D2D1::LinearGradientBrushProperties(ptBlack, ptWhite),
						pStopsBW,
						&pLinBrushBW
					);
					if (pLinBrushBW)
					{
						pRT->FillGeometry(pTriGeo, pLinBrushBW);
						pLinBrushBW->Release();
					}
					pStopsBW->Release();
				}

				// 삼각형 외곽선
				pBrush->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.9f));
				pRT->DrawGeometry(pTriGeo, pBrush, 1.2f);
			}
			pTriGeo->Release();
		}
		pFactory->Release();
	}

	// 3. 삼각형 내부 채도/명도 피커 핸들 (적절한 크기: 6.5px 링 + 4.5px 채움)
	float wHue, wWhite, wBlack;
	SLToBarycentric(m_fSat, m_fLight, wHue, wWhite, wBlack);
	D2D1_POINT_2F triHandlePt = D2D1::Point2F(
		wHue * ptHue.x + wWhite * ptWhite.x + wBlack * ptBlack.x,
		wHue * ptHue.y + wWhite * ptWhite.y + wBlack * ptBlack.y
	);

	pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f));
	pRT->DrawEllipse(D2D1::Ellipse(triHandlePt, 7.0f, 7.0f), pBrush, 2.0f);
	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	pRT->DrawEllipse(D2D1::Ellipse(triHandlePt, 6.0f, 6.0f), pBrush, 1.5f);
	D2D1_COLOR_F curHandleClr = GetRGB();
	pBrush->SetColor(curHandleClr);
	pRT->FillEllipse(D2D1::Ellipse(triHandlePt, 4.5f, 4.5f), pBrush);

	// 4. Hue 마커 (외곽 링 위의 슬릿 및 원형 링 핸들)
	float radHue = static_cast<float>(m_fHue * (M_PI / 180.0));
	float midR = (radiusOuter + radiusInner) * 0.5f;
	D2D1_POINT_2F markerPt = D2D1::Point2F(center.x + std::cos(radHue) * midR, center.y + std::sin(radHue) * midR);

	D2D1_POINT_2F pOuter = D2D1::Point2F(center.x + std::cos(radHue) * (radiusOuter + 1.0f), center.y + std::sin(radHue) * (radiusOuter + 1.0f));
	D2D1_POINT_2F pInner = D2D1::Point2F(center.x + std::cos(radHue) * (radiusInner - 1.0f), center.y + std::sin(radHue) * (radiusInner - 1.0f));
	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f));
	pRT->DrawLine(pInner, pOuter, pBrush, 2.0f);

	pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
	pRT->DrawEllipse(D2D1::Ellipse(markerPt, 6.5f, 6.5f), pBrush, 2.0f);
	pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
	pRT->DrawEllipse(D2D1::Ellipse(markerPt, 5.5f, 5.5f), pBrush, 1.5f);
	pBrush->SetColor(WheelHSLToRGB(m_fHue, 1.0f, 0.5f));
	pRT->FillEllipse(D2D1::Ellipse(markerPt, 4.0f, 4.0f), pBrush);
}

bool M2DrawColorWheel::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;
	D2D1_POINT_2F center;
	float radiusOuter, radiusInner;
	GetMetrics(center, radiusOuter, radiusInner);

	float dx = pt.x - center.x;
	float dy = pt.y - center.y;
	float dist = std::sqrt(dx * dx + dy * dy);

	if (dist >= radiusInner && dist <= radiusOuter + 6.0f)
	{
		m_isDraggingRing = true;
		float deg = static_cast<float>(std::atan2(dy, dx) * (180.0 / M_PI));
		if (deg < 0.0f) deg += 360.0f;
		m_fHue = deg;
		if (m_onColorChanged) m_onColorChanged(m_fHue, m_fSat, m_fLight, false);
		return true;
	}
	else if (dist < radiusInner)
	{
		m_isDraggingTriangle = true;
		D2D1_POINT_2F ptHue, ptWhite, ptBlack;
		GetTriangleVertices(center, radiusInner, ptHue, ptWhite, ptBlack);

		float wA, wB, wC;
		CalcBarycentric(pt, ptHue, ptWhite, ptBlack, wA, wB, wC);
		BarycentricToSL(wA, wB, wC, m_fSat, m_fLight);

		if (m_onColorChanged) m_onColorChanged(m_fHue, m_fSat, m_fLight, false);
		return true;
	}
	return false;
}

bool M2DrawColorWheel::OnMouseMove(D2D1_POINT_2F pt)
{
	M2DrawControl::OnMouseMove(pt);
	D2D1_POINT_2F center;
	float radiusOuter, radiusInner;
	GetMetrics(center, radiusOuter, radiusInner);

	if (m_isDraggingRing)
	{
		float dx = pt.x - center.x;
		float dy = pt.y - center.y;
		float deg = static_cast<float>(std::atan2(dy, dx) * (180.0 / M_PI));
		if (deg < 0.0f) deg += 360.0f;
		m_fHue = deg;
		if (m_onColorChanged) m_onColorChanged(m_fHue, m_fSat, m_fLight, false);
		return true;
	}
	else if (m_isDraggingTriangle)
	{
		D2D1_POINT_2F ptHue, ptWhite, ptBlack;
		GetTriangleVertices(center, radiusInner, ptHue, ptWhite, ptBlack);

		float wA, wB, wC;
		CalcBarycentric(pt, ptHue, ptWhite, ptBlack, wA, wB, wC);
		BarycentricToSL(wA, wB, wC, m_fSat, m_fLight);

		if (m_onColorChanged) m_onColorChanged(m_fHue, m_fSat, m_fLight, false);
		return true;
	}
	return m_bHovered;
}

bool M2DrawColorWheel::OnLButtonUp(D2D1_POINT_2F /*pt*/)
{
	if (m_isDraggingRing || m_isDraggingTriangle)
	{
		m_isDraggingRing = false;
		m_isDraggingTriangle = false;
		if (m_onColorChanged) m_onColorChanged(m_fHue, m_fSat, m_fLight, true);
		return true;
	}
	return false;
}

// ============================================================================
// 7. M2DrawPanel 컨테이너 구현
// ============================================================================
M2DrawPanel::M2DrawPanel()
{
}

void M2DrawPanel::AddChild(std::shared_ptr<M2DrawControl> child)
{
	if (child)
	{
		m_children.push_back(child);
	}
}

void M2DrawPanel::ClearChildren()
{
	m_children.clear();
	m_capturedChild.reset();
}

void M2DrawPanel::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* pDWFactory,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	std::shared_ptr<M2DrawControl> droppedControl = nullptr;

	for (auto& child : m_children)
	{
		if (child && child->IsVisible())
		{
			auto combo = std::dynamic_pointer_cast<M2DrawComboBox>(child);
			if (combo && combo->IsDropped())
			{
				droppedControl = child;
				continue;
			}
			child->Draw(pRT, pBrush, pDWFactory, pTextFormat);
		}
	}

	// 드롭다운이 열린 콤보박스는 다른 컨트롤 위에 오버레이되도록 가장 마지막에 렌더링
	if (droppedControl)
	{
		droppedControl->Draw(pRT, pBrush, pDWFactory, pTextFormat);
	}
}

bool M2DrawPanel::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	// 1. 드롭다운이 열려 있는 콤보박스가 있다면 팝업 리스트 영역이 최상위 z-order이므로 최우선 처리
	for (auto& child : m_children)
	{
		if (child && child->IsVisible())
		{
			auto combo = std::dynamic_pointer_cast<M2DrawComboBox>(child);
			if (combo && combo->IsDropped())
			{
				if (combo->HitTest(pt))
				{
					if (combo->OnLButtonDown(pt))
					{
						m_capturedChild = combo;
						return true;
					}
				}
				else
				{
					// 팝업 밖을 클릭한 경우 드롭다운 닫기
					combo->OnLButtonDown(pt);
					return true;
				}
			}
		}
	}

	// 2. 최상위(나중에 추가된) 컨트롤부터 역순 히트 테스트
	for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
	{
		if ((*it)->IsVisible() && (*it)->HitTest(pt))
		{
			if ((*it)->OnLButtonDown(pt))
			{
				m_capturedChild = *it;
				return true;
			}
		}
	}
	return false;
}

bool M2DrawPanel::OnMouseMove(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	if (m_capturedChild)
	{
		return m_capturedChild->OnMouseMove(pt);
	}

	// 드롭다운이 열려 있는 콤보박스가 있다면 호버 이벤트를 최우선 처리
	for (auto& child : m_children)
	{
		if (child && child->IsVisible())
		{
			auto combo = std::dynamic_pointer_cast<M2DrawComboBox>(child);
			if (combo && combo->IsDropped() && combo->HitTest(pt))
			{
				return combo->OnMouseMove(pt);
			}
		}
	}

	bool anyHandled = false;
	for (auto& child : m_children)
	{
		if (child && child->IsVisible())
		{
			if (child->OnMouseMove(pt))
			{
				anyHandled = true;
			}
		}
	}
	return anyHandled;
}

bool M2DrawPanel::OnLButtonUp(D2D1_POINT_2F pt)
{
	if (m_capturedChild)
	{
		bool res = m_capturedChild->OnLButtonUp(pt);
		m_capturedChild.reset();
		return res;
	}

	for (auto& child : m_children)
	{
		if (child && child->IsVisible())
		{
			child->OnLButtonUp(pt);
		}
	}
	return false;
}

bool M2DrawPanel::OnKeyDown(UINT nChar)
{
	if (m_capturedChild)
	{
		if (m_capturedChild->OnKeyDown(nChar)) return true;
	}
	for (auto& child : m_children)
	{
		if (child && child->IsVisible() && child->OnKeyDown(nChar))
		{
			return true;
		}
	}
	return false;
}

bool M2DrawPanel::OnChar(UINT nChar)
{
	if (m_capturedChild)
	{
		if (m_capturedChild->OnChar(nChar)) return true;
	}
	for (auto& child : m_children)
	{
		if (child && child->IsVisible() && child->OnChar(nChar))
		{
			return true;
		}
	}
	return false;
}

// ============================================================================
// 8. M2DrawExpander (접기/펴기 그룹 컨테이너) 구현
// ============================================================================
M2DrawExpander::M2DrawExpander(const std::wstring& headerText, bool bExpanded)
	: m_headerText(headerText)
	, m_bExpanded(bExpanded)
{
}

void M2DrawExpander::SetExpanded(bool bExpanded)
{
	if (m_bExpanded != bExpanded)
	{
		m_bExpanded = bExpanded;
		if (m_onToggle)
		{
			m_onToggle(this, m_bExpanded);
		}
	}
}

void M2DrawExpander::AddChild(std::shared_ptr<M2DrawControl> child)
{
	if (child)
	{
		m_children.push_back(child);
	}
}

void M2DrawExpander::ClearChildren()
{
	m_children.clear();
	m_capturedChild.reset();
}

void M2DrawExpander::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* pDWFactory,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible || !pRT || !pBrush) return;

	// 1. 헤더 바 영역
	D2D1_RECT_F headerRect = D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + m_headerHeight);

	D2D1_COLOR_F headerBg = m_bHovered
		? D2D1::ColorF(0.25f, 0.25f, 0.30f, 1.0f)
		: D2D1::ColorF(0.19f, 0.19f, 0.23f, 1.0f);

	DrawControlFrame(pRT, pBrush, headerRect, 4.0f, headerBg);

	// 2. 화살표 (접힘: ▶, 펼침: ▼)
	D2D1_RECT_F arrowRect = D2D1::RectF(headerRect.left + 8.0f, headerRect.top, headerRect.left + 22.0f, headerRect.bottom);
	DrawArrow(pRT, pBrush, arrowRect, m_bExpanded ? ArrowDirection::Down : ArrowDirection::Right, 3.5f, D2D1::ColorF(0.85f, 0.85f, 0.90f, 1.0f));

	// 3. 헤더 텍스트
	D2D1_RECT_F textRect = D2D1::RectF(headerRect.left + 24.0f, headerRect.top, headerRect.right - 8.0f, headerRect.bottom);
	DrawTextLabel(pRT, pBrush, pTextFormat, m_headerText.c_str(), textRect,
		DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, D2D1::ColorF(0.92f, 0.92f, 0.95f, 1.0f));

	// 4. 펼쳐져 있을 때만 자식 컨트롤들 그리기
	if (m_bExpanded)
	{
		for (auto& child : m_children)
		{
			if (child && child->IsVisible())
			{
				child->Draw(pRT, pBrush, pDWFactory, pTextFormat);
			}
		}
	}
}

bool M2DrawExpander::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	D2D1_RECT_F headerRect = D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + m_headerHeight);
	if (pt.x >= headerRect.left && pt.x <= headerRect.right && pt.y >= headerRect.top && pt.y <= headerRect.bottom)
	{
		SetExpanded(!m_bExpanded);
		return true;
	}

	if (m_bExpanded)
	{
		for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
		{
			if ((*it)->IsVisible() && (*it)->HitTest(pt))
			{
				if ((*it)->OnLButtonDown(pt))
				{
					m_capturedChild = *it;
					return true;
				}
			}
		}
	}
	return false;
}

bool M2DrawExpander::OnMouseMove(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	D2D1_RECT_F headerRect = D2D1::RectF(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + m_headerHeight);
	m_bHovered = (pt.x >= headerRect.left && pt.x <= headerRect.right && pt.y >= headerRect.top && pt.y <= headerRect.bottom);

	if (m_capturedChild)
	{
		return m_capturedChild->OnMouseMove(pt);
	}

	bool anyHandled = m_bHovered;
	if (m_bExpanded)
	{
		for (auto& child : m_children)
		{
			if (child && child->IsVisible())
			{
				if (child->OnMouseMove(pt))
				{
					anyHandled = true;
				}
			}
		}
	}
	return anyHandled;
}

bool M2DrawExpander::OnLButtonUp(D2D1_POINT_2F pt)
{
	if (m_capturedChild)
	{
		bool res = m_capturedChild->OnLButtonUp(pt);
		m_capturedChild.reset();
		return res;
	}

	if (m_bExpanded)
	{
		for (auto& child : m_children)
		{
			if (child && child->IsVisible())
			{
				child->OnLButtonUp(pt);
			}
		}
	}
	return false;
}

bool M2DrawExpander::OnKeyDown(UINT nChar)
{
	if (m_capturedChild)
	{
		if (m_capturedChild->OnKeyDown(nChar)) return true;
	}
	if (m_bExpanded)
	{
		for (auto& child : m_children)
		{
			if (child && child->IsVisible() && child->OnKeyDown(nChar))
			{
				return true;
			}
		}
	}
	return false;
}

bool M2DrawExpander::OnChar(UINT nChar)
{
	if (m_capturedChild)
	{
		if (m_capturedChild->OnChar(nChar)) return true;
	}
	if (m_bExpanded)
	{
		for (auto& child : m_children)
		{
			if (child && child->IsVisible() && child->OnChar(nChar))
			{
				return true;
			}
		}
	}
	return false;
}

// ============================================================================
// 9. M2DrawColorPalette (16색 표준 팔레트 박스) 구현
// ============================================================================
M2DrawColorPalette::M2DrawColorPalette()
{
	// 16개 표준 색상 (Windows 그림판 / 표준 VGA 팔레트)
	// 행 1: 기본 원색 및 무채색 계열 (8색)
	m_colors.push_back(D2D1::ColorF(0.0f, 0.0f, 0.0f));       // 검정
	m_colors.push_back(D2D1::ColorF(0.5f, 0.5f, 0.5f));       // 회색
	m_colors.push_back(D2D1::ColorF(0.5f, 0.0f, 0.0f));       // 암적색
	m_colors.push_back(D2D1::ColorF(0.9f, 0.1f, 0.1f));       // 빨강
	m_colors.push_back(D2D1::ColorF(1.0f, 0.5f, 0.0f));       // 주황
	m_colors.push_back(D2D1::ColorF(1.0f, 0.85f, 0.0f));      // 노랑
	m_colors.push_back(D2D1::ColorF(0.1f, 0.75f, 0.2f));      // 녹색
	m_colors.push_back(D2D1::ColorF(0.1f, 0.5f, 0.95f));      // 파랑

	// 행 2: 파스텔 및 보조 계열 (8색)
	m_colors.push_back(D2D1::ColorF(1.0f, 1.0f, 1.0f));       // 흰색
	m_colors.push_back(D2D1::ColorF(0.75f, 0.75f, 0.75f));    // 밝은 회색
	m_colors.push_back(D2D1::ColorF(0.6f, 0.3f, 0.1f));       // 갈색
	m_colors.push_back(D2D1::ColorF(1.0f, 0.6f, 0.8f));       // 분홍
	m_colors.push_back(D2D1::ColorF(1.0f, 0.9f, 0.6f));       // 살구/연노랑
	m_colors.push_back(D2D1::ColorF(0.6f, 0.9f, 0.6f));       // 연두
	m_colors.push_back(D2D1::ColorF(0.5f, 0.85f, 1.0f));      // 하늘색
	m_colors.push_back(D2D1::ColorF(0.6f, 0.3f, 0.9f));       // 보라
}

D2D1_RECT_F M2DrawColorPalette::GetBoxRect(int index) const
{
	if (index < 0 || index >= 16) return D2D1::RectF(0, 0, 0, 0);

	int row = index / 8; // 0 또는 1
	int col = index % 8; // 0 ~ 7

	float totalW = GetWidth();
	float boxW = (totalW - (7.0f * 4.0f)) / 8.0f;
	float boxH = (GetHeight() - 4.0f) * 0.5f;

	float x1 = m_bounds.left + col * (boxW + 4.0f);
	float y1 = m_bounds.top + row * (boxH + 4.0f);

	return D2D1::RectF(x1, y1, x1 + boxW, y1 + boxH);
}

void M2DrawColorPalette::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* /*pTextFormat*/)
{
	if (!m_bVisible || !pRT || !pBrush) return;

	for (int i = 0; i < 16; ++i)
	{
		D2D1_RECT_F rBox = GetBoxRect(i);
		D2D1_COLOR_F clr = m_colors[i];

		// 색상 채움
		pBrush->SetColor(clr);
		pRT->FillRoundedRectangle(D2D1::RoundedRect(rBox, 3.0f, 3.0f), pBrush);

		// 호버 및 선택 상태 표시
		bool isSel = (std::abs(clr.r - m_selectedColor.r) < 0.02f &&
		              std::abs(clr.g - m_selectedColor.g) < 0.02f &&
		              std::abs(clr.b - m_selectedColor.b) < 0.02f);

		if (isSel)
		{
			// 흰색 + 검정 이중 테두리로 확실한 선택 표시
			pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
			pRT->DrawRoundedRectangle(D2D1::RoundedRect(rBox, 3.0f, 3.0f), pBrush, 2.0f);
			D2D1_RECT_F rInner = D2D1::RectF(rBox.left + 2.0f, rBox.top + 2.0f, rBox.right - 2.0f, rBox.bottom - 2.0f);
			pBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
			pRT->DrawRoundedRectangle(D2D1::RoundedRect(rInner, 2.0f, 2.0f), pBrush, 1.0f);
		}
		else if (i == m_hoverIndex)
		{
			pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.8f));
			pRT->DrawRoundedRectangle(D2D1::RoundedRect(rBox, 3.0f, 3.0f), pBrush, 1.5f);
		}
		else
		{
			// 은은한 다크 테두리
			pBrush->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.28f, 1.0f));
			pRT->DrawRoundedRectangle(D2D1::RoundedRect(rBox, 3.0f, 3.0f), pBrush, 1.0f);
		}
	}
}

bool M2DrawColorPalette::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	for (int i = 0; i < 16; ++i)
	{
		D2D1_RECT_F rBox = GetBoxRect(i);
		if (pt.x >= rBox.left && pt.x <= rBox.right && pt.y >= rBox.top && pt.y <= rBox.bottom)
		{
			m_selectedColor = m_colors[i];
			if (m_onColorSelected)
			{
				m_onColorSelected(this, m_selectedColor);
			}
			return true;
		}
	}
	return false;
}

bool M2DrawColorPalette::OnMouseMove(D2D1_POINT_2F pt)
{
	M2DrawControl::OnMouseMove(pt);
	int oldHover = m_hoverIndex;
	m_hoverIndex = -1;

	for (int i = 0; i < 16; ++i)
	{
		D2D1_RECT_F rBox = GetBoxRect(i);
		if (pt.x >= rBox.left && pt.x <= rBox.right && pt.y >= rBox.top && pt.y <= rBox.bottom)
		{
			m_hoverIndex = i;
			break;
		}
	}
	return (oldHover != m_hoverIndex);
}

// ============================================================================
// 10. M2DrawListBox 구현
// ============================================================================
M2DrawListBox::M2DrawListBox()
{
}

void M2DrawListBox::AddItem(const std::wstring& item)
{
	m_items.push_back(item);
}

void M2DrawListBox::ClearItems()
{
	m_items.clear();
	m_curSel = 0;
	m_hoverIndex = -1;
}

const std::wstring& M2DrawListBox::GetItemText(int index) const
{
	static const std::wstring emptyStr = L"";
	if (index >= 0 && index < static_cast<int>(m_items.size()))
		return m_items[index];
	return emptyStr;
}

void M2DrawListBox::SetCurSel(int index)
{
	if (m_curSel != index && index >= 0 && index < static_cast<int>(m_items.size()))
	{
		m_curSel = index;
		if (m_onSelectionChanged)
		{
			m_onSelectionChanged(this, m_curSel);
		}
	}
}

D2D1_RECT_F M2DrawListBox::GetItemRect(int index) const
{
	float top = m_bounds.top + 2.0f + index * m_itemHeight;
	return D2D1::RectF(m_bounds.left + 2.0f, top, m_bounds.right - 2.0f, top + m_itemHeight);
}

void M2DrawListBox::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	// 외부 프레임 테두리 및 딥 다크 배경
	DrawControlFrame(pRT, pBrush, m_bounds, 4.0f, D2D1::ColorF(0.14f, 0.14f, 0.18f, 1.0f));

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
	{
		D2D1_RECT_F rItem = GetItemRect(i);
		if (rItem.bottom > m_bounds.bottom - 2.0f) break;

		bool isSelected = (i == m_curSel);
		bool isHover = (i == m_hoverIndex);

		if (isSelected)
		{
			// 선택된 항목 배경 (파란 액센트)
			pBrush->SetColor(D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.85f));
			pRT->FillRoundedRectangle(D2D1::RoundedRect(rItem, 3.0f, 3.0f), pBrush);
		}
		else if (isHover)
		{
			// 호버 항목 배경
			pBrush->SetColor(D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.6f));
			pRT->FillRoundedRectangle(D2D1::RoundedRect(rItem, 3.0f, 3.0f), pBrush);
		}

		// 항목 텍스트
		D2D1_COLOR_F textColor = isSelected
			? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
			: (isHover ? D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f) : D2D1::ColorF(0.80f, 0.80f, 0.84f, 1.0f));

		D2D1_RECT_F textRect = D2D1::RectF(rItem.left + 8.0f, rItem.top, rItem.right - 8.0f, rItem.bottom);
		DrawTextLabel(pRT, pBrush, pTextFormat, m_items[i].c_str(), textRect,
			DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, textColor);
	}
}

bool M2DrawListBox::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	if (HitTest(pt))
	{
		for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
		{
			D2D1_RECT_F rItem = GetItemRect(i);
			if (pt.x >= rItem.left && pt.x <= rItem.right && pt.y >= rItem.top && pt.y <= rItem.bottom)
			{
				SetCurSel(i);
				return true;
			}
		}
		return true;
	}
	return false;
}

bool M2DrawListBox::OnMouseMove(D2D1_POINT_2F pt)
{
	M2DrawControl::OnMouseMove(pt);
	int oldHover = m_hoverIndex;
	m_hoverIndex = -1;

	if (HitTest(pt))
	{
		for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
		{
			D2D1_RECT_F rItem = GetItemRect(i);
			if (pt.x >= rItem.left && pt.x <= rItem.right && pt.y >= rItem.top && pt.y <= rItem.bottom)
			{
				m_hoverIndex = i;
				break;
			}
		}
	}
	return (oldHover != m_hoverIndex);
}

// ============================================================================
// 11. M2DrawComboBox 구현
// ============================================================================
M2DrawComboBox::M2DrawComboBox()
{
}

void M2DrawComboBox::AddItem(const std::wstring& item)
{
	m_items.push_back(item);
}

void M2DrawComboBox::ClearItems()
{
	m_items.clear();
	m_curSel = 0;
	m_hoverIndex = -1;
	m_bDropped = false;
}

const std::wstring& M2DrawComboBox::GetItemText(int index) const
{
	static const std::wstring emptyStr = L"";
	if (index >= 0 && index < static_cast<int>(m_items.size()))
		return m_items[index];
	return emptyStr;
}

void M2DrawComboBox::SetCurSel(int index)
{
	if (m_curSel != index && index >= 0 && index < static_cast<int>(m_items.size()))
	{
		m_curSel = index;
		if (m_onSelectionChanged)
		{
			m_onSelectionChanged(this, m_curSel);
		}
	}
}

D2D1_RECT_F M2DrawComboBox::GetDropDownRect() const
{
	float top = m_bounds.bottom + 2.0f;
	float totalH = static_cast<float>(m_items.size()) * m_itemHeight + 4.0f;
	return D2D1::RectF(m_bounds.left, top, m_bounds.right, top + totalH);
}

D2D1_RECT_F M2DrawComboBox::GetItemRect(int index) const
{
	float top = m_bounds.bottom + 4.0f + index * m_itemHeight;
	return D2D1::RectF(m_bounds.left + 2.0f, top, m_bounds.right - 2.0f, top + m_itemHeight);
}

bool M2DrawComboBox::HitTest(D2D1_POINT_2F pt) const
{
	if (!m_bVisible) return false;

	// 헤더 버튼 영역
	if (pt.x >= m_bounds.left && pt.x <= m_bounds.right && pt.y >= m_bounds.top && pt.y <= m_bounds.bottom)
		return true;

	// 드롭다운 열려있을 때 팝업 리스트 영역
	if (m_bDropped)
	{
		D2D1_RECT_F dropRect = GetDropDownRect();
		if (pt.x >= dropRect.left && pt.x <= dropRect.right && pt.y >= dropRect.top && pt.y <= dropRect.bottom)
			return true;
	}

	return false;
}

void M2DrawComboBox::Draw(
	ID2D1RenderTarget* pRT,
	ID2D1SolidColorBrush* pBrush,
	IDWriteFactory* /*pDWFactory*/,
	IDWriteTextFormat* pTextFormat)
{
	if (!m_bVisible) return;

	// 1. 헤더 (현재 선택된 텍스트 + 드롭다운 화살표 ▼)
	DrawControlFrame(pRT, pBrush, m_bounds, 4.0f, D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f));

	std::wstring curText = (m_curSel >= 0 && m_curSel < static_cast<int>(m_items.size()))
		? m_items[m_curSel] : L"";

	D2D1_RECT_F textRect = D2D1::RectF(m_bounds.left + 10.0f, m_bounds.top, m_bounds.right - 26.0f, m_bounds.bottom);
	DrawTextLabel(pRT, pBrush, pTextFormat, curText.c_str(), textRect,
		DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f));

	// 우측 드롭다운 화살표
	D2D1_RECT_F arrowRect = D2D1::RectF(m_bounds.right - 24.0f, m_bounds.top, m_bounds.right - 4.0f, m_bounds.bottom);
	DrawArrow(pRT, pBrush, arrowRect, m_bDropped ? ArrowDirection::Up : ArrowDirection::Down, 4.0f,
		D2D1::ColorF(0.85f, 0.85f, 0.90f, 1.0f));

	// 2. 드롭다운 팝업 리스트 (열려 있을 때)
	if (m_bDropped && !m_items.empty())
	{
		D2D1_RECT_F dropRect = GetDropDownRect();

		// 드롭다운 배경 (살짝 더 어두운 섀도우 다크 톤)
		DrawControlFrame(pRT, pBrush, dropRect, 4.0f, D2D1::ColorF(0.14f, 0.14f, 0.17f, 0.98f));

		for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
		{
			D2D1_RECT_F rItem = GetItemRect(i);
			bool isSelected = (i == m_curSel);
			bool isHover = (i == m_hoverIndex);

			if (isSelected)
			{
				pBrush->SetColor(D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.85f));
				pRT->FillRoundedRectangle(D2D1::RoundedRect(rItem, 3.0f, 3.0f), pBrush);
			}
			else if (isHover)
			{
				pBrush->SetColor(D2D1::ColorF(0.28f, 0.28f, 0.34f, 0.7f));
				pRT->FillRoundedRectangle(D2D1::RoundedRect(rItem, 3.0f, 3.0f), pBrush);
			}

			D2D1_COLOR_F textColor = isSelected
				? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
				: (isHover ? D2D1::ColorF(0.98f, 0.98f, 1.0f, 1.0f) : D2D1::ColorF(0.82f, 0.82f, 0.86f, 1.0f));

			D2D1_RECT_F itemTextRect = D2D1::RectF(rItem.left + 8.0f, rItem.top, rItem.right - 8.0f, rItem.bottom);
			DrawTextLabel(pRT, pBrush, pTextFormat, m_items[i].c_str(), itemTextRect,
				DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, textColor);
		}
	}
}

bool M2DrawComboBox::OnLButtonDown(D2D1_POINT_2F pt)
{
	if (!m_bVisible || !m_bEnabled) return false;

	// 1. 헤더 클릭 시 드롭다운 토글
	if (pt.x >= m_bounds.left && pt.x <= m_bounds.right && pt.y >= m_bounds.top && pt.y <= m_bounds.bottom)
	{
		m_bDropped = !m_bDropped;
		m_hoverIndex = -1;
		return true;
	}

	// 2. 드롭다운 목록 클릭 시 항목 선택 및 닫기
	if (m_bDropped)
	{
		for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
		{
			D2D1_RECT_F rItem = GetItemRect(i);
			if (pt.x >= rItem.left && pt.x <= rItem.right && pt.y >= rItem.top && pt.y <= rItem.bottom)
			{
				SetCurSel(i);
				m_bDropped = false;
				m_hoverIndex = -1;
				return true;
			}
		}
		// 목록 밖을 클릭하면 닫기
		m_bDropped = false;
		return true;
	}

	return false;
}

bool M2DrawComboBox::OnMouseMove(D2D1_POINT_2F pt)
{
	M2DrawControl::OnMouseMove(pt);
	int oldHover = m_hoverIndex;
	m_hoverIndex = -1;

	if (m_bDropped)
	{
		for (int i = 0; i < static_cast<int>(m_items.size()); ++i)
		{
			D2D1_RECT_F rItem = GetItemRect(i);
			if (pt.x >= rItem.left && pt.x <= rItem.right && pt.y >= rItem.top && pt.y <= rItem.bottom)
			{
				m_hoverIndex = i;
				break;
			}
		}
	}
	return (oldHover != m_hoverIndex);
}


