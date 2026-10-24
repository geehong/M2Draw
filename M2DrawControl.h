#pragma once
#include <afxwin.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <memory>
#include <string>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// M2DrawControl: D2D 기반 UI 객체의 최상위 기본 클래스
// - 위치/크기(Bounds), 활성/호버/클릭/포커스 상태 관리
// - 공통 프레임(배경/테두리), 텍스트, 화살표, 체크마크 렌더링 도우미 내장
// ============================================================================
class M2DrawControl
{
public:
	enum class ArrowDirection
	{
		Up,
		Down,
		Left,
		Right
	};

	M2DrawControl();
	virtual ~M2DrawControl() = default;

	// 위치 및 크기 조절 (이동, 크기 줄이기/늘리기)
	void SetBounds(const D2D1_RECT_F& bounds) { m_bounds = bounds; }
	void SetBounds(float left, float top, float right, float bottom) { m_bounds = D2D1::RectF(left, top, right, bottom); }
	void SetPosition(float x, float y);
	void SetSize(float width, float height);
	D2D1_RECT_F GetBounds() const { return m_bounds; }
	float GetWidth() const { return m_bounds.right - m_bounds.left; }
	float GetHeight() const { return m_bounds.bottom - m_bounds.top; }

	// 가시성 및 활성화
	void SetVisible(bool b) { m_bVisible = b; }
	bool IsVisible() const { return m_bVisible; }
	void SetEnabled(bool b) { m_bEnabled = b; }
	bool IsEnabled() const { return m_bEnabled; }

	// 히트 테스트
	virtual bool HitTest(D2D1_POINT_2F pt) const;

	// 렌더링 인터페이스
	virtual void Draw(
		ID2D1RenderTarget* pRT,
		ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory,
		IDWriteTextFormat* pTextFormat
	) = 0;

	// 마우스/키보드 이벤트 인터페이스
	virtual bool OnLButtonDown(D2D1_POINT_2F pt);
	virtual bool OnMouseMove(D2D1_POINT_2F pt);
	virtual bool OnLButtonUp(D2D1_POINT_2F pt);
	virtual bool OnKeyDown(UINT nChar);
	virtual bool OnChar(UINT nChar);

	// 상태 리셋
	virtual void ResetState();

protected:
	D2D1_RECT_F m_bounds = { 0, 0, 0, 0 };
	bool        m_bVisible = true;
	bool        m_bEnabled = true;
	bool        m_bHovered = false;
	bool        m_bPressed = false;
	bool        m_bFocused = false;

	// ------------------------------------------------------------------------
	// 하위 컨트롤들이 공통으로 사용하는 표준 렌더링 헬퍼 메소드
	// ------------------------------------------------------------------------
	// 1. 상태별 컨트롤 배경 및 테두리 (Hover/Pressed/Normal/Disabled)
	void DrawControlFrame(
		ID2D1RenderTarget* pRT,
		ID2D1SolidColorBrush* pBrush,
		const D2D1_RECT_F& rect,
		float cornerRadius = 4.0f,
		D2D1_COLOR_F customBaseColor = D2D1::ColorF(0.20f, 0.20f, 0.23f, 1.0f)
	);

	// 2. 텍스트 라벨 렌더링
	void DrawTextLabel(
		ID2D1RenderTarget* pRT,
		ID2D1SolidColorBrush* pBrush,
		IDWriteTextFormat* pFormat,
		const wchar_t* text,
		const D2D1_RECT_F& rect,
		DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
		DWRITE_PARAGRAPH_ALIGNMENT pAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
		D2D1_COLOR_F textColor = D2D1::ColorF(0.92f, 0.92f, 0.95f, 1.0f)
	);

	// 3. 체크마크 (✓) 기호 렌더링
	void DrawCheckMark(
		ID2D1RenderTarget* pRT,
		ID2D1SolidColorBrush* pBrush,
		const D2D1_RECT_F& boxRect,
		D2D1_COLOR_F checkColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
	);

	// 4. 방향 화살표 기호 렌더링 (▲, ▼, ◀, ▶)
	void DrawArrow(
		ID2D1RenderTarget* pRT,
		ID2D1SolidColorBrush* pBrush,
		const D2D1_RECT_F& rect,
		ArrowDirection dir,
		float size = 4.0f,
		D2D1_COLOR_F color = D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f)
	);

	// 5. 포커스 / 액센트 아웃라인 링
	void DrawFocusRing(
		ID2D1RenderTarget* pRT,
		ID2D1SolidColorBrush* pBrush,
		const D2D1_RECT_F& rect,
		float cornerRadius = 4.0f,
		D2D1_COLOR_F ringColor = D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.9f)
	);
};

// ============================================================================
// 1. M2DrawButton: 버튼 오브젝트 (푸시 버튼 / 토글 버튼)
// ============================================================================
class M2DrawButton : public M2DrawControl
{
public:
	using ClickCallback = std::function<void(M2DrawButton* pBtn)>;

	M2DrawButton(const std::wstring& text = L"");
	void SetText(const std::wstring& text) { m_text = text; }
	const std::wstring& GetText() const { return m_text; }

	void SetOnClick(ClickCallback cb) { m_onClick = cb; }

	void SetToggleMode(bool bToggle) { m_bToggleMode = bToggle; }
	void SetChecked(bool bChecked) { m_bChecked = bChecked; }
	bool IsChecked() const { return m_bChecked; }

	void SetAccentColor(D2D1_COLOR_F clr) { m_accentColor = clr; m_bUseAccent = true; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnLButtonUp(D2D1_POINT_2F pt) override;

private:
	std::wstring  m_text;
	ClickCallback m_onClick;
	bool          m_bToggleMode = false;
	bool          m_bChecked = false;
	bool          m_bUseAccent = false;
	D2D1_COLOR_F  m_accentColor = D2D1::ColorF(0.0f, 0.48f, 0.85f, 1.0f);
};

// ============================================================================
// 2. M2DrawCheckBox: 체크박스 오브젝트
// ============================================================================
class M2DrawCheckBox : public M2DrawControl
{
public:
	using ChangeCallback = std::function<void(M2DrawCheckBox* pChk, bool bChecked)>;

	M2DrawCheckBox(const std::wstring& text = L"", bool bChecked = false);

	void SetText(const std::wstring& text) { m_text = text; }
	const std::wstring& GetText() const { return m_text; }

	void SetChecked(bool b);
	bool IsChecked() const { return m_bChecked; }

	void SetOnChanged(ChangeCallback cb) { m_onChanged = cb; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;

private:
	std::wstring   m_text;
	bool           m_bChecked = false;
	ChangeCallback m_onChanged;
};

// ============================================================================
// 3. M2DrawSlider: 바 / 슬라이더 오브젝트
// ============================================================================
class M2DrawSlider : public M2DrawControl
{
public:
	using ChangeCallback = std::function<void(M2DrawSlider* pSlider, float value)>;

	M2DrawSlider(float minVal = 0.0f, float maxVal = 1.0f, float curVal = 0.5f);

	void SetRange(float minVal, float maxVal);
	void SetValue(float val, bool triggerCallback = true);
	float GetValue() const { return m_val; }
	float GetMinVal() const { return m_minVal; }
	float GetMaxVal() const { return m_maxVal; }
	float GetNormalizedValue() const;

	void SetLabel(const std::wstring& label) { m_label = label; }
	const std::wstring& GetLabel() const { return m_label; }

	void SetAccentColor(D2D1_COLOR_F clr) { m_accentColor = clr; }
	void SetOnChanged(ChangeCallback cb) { m_onChanged = cb; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;
	bool OnLButtonUp(D2D1_POINT_2F pt) override;

private:
	float          m_minVal = 0.0f;
	float          m_maxVal = 1.0f;
	float          m_val = 0.5f;
	std::wstring   m_label;
	bool           m_isDragging = false;
	D2D1_COLOR_F   m_accentColor = D2D1::ColorF(0.0f, 0.48f, 0.85f, 1.0f);
	ChangeCallback m_onChanged;

	void UpdateValueFromX(float mouseX);
};

// ============================================================================
// 4. M2DrawNumericInput: 숫자 입력기 / 스핀 박스 오브젝트
// ============================================================================
class M2DrawNumericInput : public M2DrawControl
{
public:
	using ChangeCallback = std::function<void(M2DrawNumericInput* pNum, double value)>;

	M2DrawNumericInput(double minVal = 0.0, double maxVal = 100.0, double curVal = 10.0, double step = 1.0);

	void SetRange(double minVal, double maxVal) { m_minVal = minVal; m_maxVal = maxVal; }
	void SetStep(double step) { m_step = step; }
	void SetValue(double val, bool triggerCallback = true);
	double GetValue() const { return m_val; }

	void SetSuffix(const std::wstring& suffix) { m_suffix = suffix; }
	void SetOnChanged(ChangeCallback cb) { m_onChanged = cb; }

	void SetDecimals(int decimals) { m_decimals = decimals; }
	int  GetDecimals() const { return m_decimals; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;
	bool OnLButtonUp(D2D1_POINT_2F pt) override;
	bool OnKeyDown(UINT nChar) override;
	bool OnChar(UINT nChar) override;
	void ResetState() override;

	bool IsEditing() const { return m_bEditing; }
	void CommitEdit();
	void CancelEdit();

private:
	double         m_minVal = 0.0;
	double         m_maxVal = 100.0;
	double         m_val = 10.0;
	double         m_step = 1.0;
	int            m_decimals = 1;
	std::wstring   m_suffix;
	ChangeCallback m_onChanged;

	// 인라인 텍스트 직접 입력/편집 상태
	bool           m_bEditing = false;
	std::wstring   m_editText;

	// 버튼 히트 영역
	bool           m_bHoverUp = false;
	bool           m_bHoverDown = false;
	bool           m_bPressUp = false;
	bool           m_bPressDown = false;

	D2D1_RECT_F GetUpButtonRect() const;
	D2D1_RECT_F GetDownButtonRect() const;
	D2D1_RECT_F GetTextRect() const;
};

// ============================================================================
// 5. M2DrawLabel: 정적 텍스트 라벨 오브젝트
// ============================================================================
class M2DrawLabel : public M2DrawControl
{
public:
	M2DrawLabel(const std::wstring& text = L"");
	void SetText(const std::wstring& text) { m_text = text; }
	const std::wstring& GetText() const { return m_text; }

	void SetTextColor(D2D1_COLOR_F clr) { m_textColor = clr; }
	void SetAlignment(DWRITE_TEXT_ALIGNMENT align) { m_align = align; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

private:
	std::wstring           m_text;
	D2D1_COLOR_F           m_textColor = D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f);
	DWRITE_TEXT_ALIGNMENT  m_align = DWRITE_TEXT_ALIGNMENT_LEADING;
};

// ============================================================================
// 6. M2DrawColorWheel: 독립된 컬러휠 오브젝트
// ============================================================================
class M2DrawColorWheel : public M2DrawControl
{
public:
	using ColorCallback = std::function<void(float h, float s, float l, bool isFinal)>;

	M2DrawColorWheel();

	void SetColorHSL(float h, float s, float l);
	void SetColorRGB(const D2D1_COLOR_F& clr);
	float GetHue() const { return m_fHue; }
	float GetSat() const { return m_fSat; }
	float GetLight() const { return m_fLight; }
	D2D1_COLOR_F GetRGB(float alpha = 1.0f) const;

	void SetOnColorChanged(ColorCallback cb) { m_onColorChanged = cb; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;
	bool OnLButtonUp(D2D1_POINT_2F pt) override;

private:
	float         m_fHue = 0.0f;
	float         m_fSat = 1.0f;
	float         m_fLight = 0.5f;
	bool          m_isDraggingRing = false;
	bool          m_isDraggingTriangle = false;
	ColorCallback m_onColorChanged;

	void GetMetrics(D2D1_POINT_2F& center, float& radiusOuter, float& radiusInner) const;
	void GetTriangleVertices(D2D1_POINT_2F center, float radiusInner,
		D2D1_POINT_2F& ptHue, D2D1_POINT_2F& ptWhite, D2D1_POINT_2F& ptBlack) const;
	static void CalcBarycentric(D2D1_POINT_2F pt, D2D1_POINT_2F pA, D2D1_POINT_2F pB, D2D1_POINT_2F pC,
		float& wA, float& wB, float& wC);
	static void BarycentricToSL(float wHue, float wWhite, float wBlack, float& sat, float& light);
	static void SLToBarycentric(float sat, float light, float& wHue, float& wWhite, float& wBlack);
};

// ============================================================================
// 7. M2DrawPanel: 자식 컨트롤들을 담아 레이아웃 및 이벤트를 총괄하는 컨테이너 오브젝트
// ============================================================================
class M2DrawPanel : public M2DrawControl
{
public:
	M2DrawPanel();

	void AddChild(std::shared_ptr<M2DrawControl> child);
	void ClearChildren();
	const std::vector<std::shared_ptr<M2DrawControl>>& GetChildren() const { return m_children; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;
	bool OnLButtonUp(D2D1_POINT_2F pt) override;
	bool OnKeyDown(UINT nChar) override;
	bool OnChar(UINT nChar) override;
	void ResetState() override;

private:
	std::vector<std::shared_ptr<M2DrawControl>> m_children;
	std::shared_ptr<M2DrawControl>              m_capturedChild;
};

// ============================================================================
// 8. M2DrawExpander: 접기/펴기 (Collapsible Group) 컨테이너 오브젝트
// ============================================================================
class M2DrawExpander : public M2DrawControl
{
public:
	using ToggleCallback = std::function<void(M2DrawExpander* pExp, bool bExpanded)>;

	M2DrawExpander(const std::wstring& headerText = L"", bool bExpanded = true);

	void SetHeaderText(const std::wstring& text) { m_headerText = text; }
	const std::wstring& GetHeaderText() const { return m_headerText; }

	void SetExpanded(bool bExpanded);
	bool IsExpanded() const { return m_bExpanded; }

	void SetOnToggle(ToggleCallback cb) { m_onToggle = cb; }

	void AddChild(std::shared_ptr<M2DrawControl> child);
	void ClearChildren();
	const std::vector<std::shared_ptr<M2DrawControl>>& GetChildren() const { return m_children; }

	float GetHeaderHeight() const { return m_headerHeight; }
	void SetHeaderHeight(float h) { m_headerHeight = h; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;
	bool OnLButtonUp(D2D1_POINT_2F pt) override;
	bool OnKeyDown(UINT nChar) override;
	bool OnChar(UINT nChar) override;
	void ResetState() override;

private:
	std::wstring                                m_headerText;
	bool                                        m_bExpanded = true;
	float                                       m_headerHeight = 26.0f;
	std::vector<std::shared_ptr<M2DrawControl>> m_children;
	std::shared_ptr<M2DrawControl>              m_capturedChild;
	ToggleCallback                              m_onToggle;
};

// ============================================================================
// 9. M2DrawColorPalette: 16색 표준 팔레트 박스 (그림판/그래픽 툴 스타일)
// ============================================================================
class M2DrawColorPalette : public M2DrawControl
{
public:
	using ColorSelectCallback = std::function<void(M2DrawColorPalette* pPalette, D2D1_COLOR_F clr)>;

	M2DrawColorPalette();

	void SetSelectedColor(D2D1_COLOR_F clr) { m_selectedColor = clr; }
	D2D1_COLOR_F GetSelectedColor() const { return m_selectedColor; }

	void SetOnColorSelected(ColorSelectCallback cb) { m_onColorSelected = cb; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;

private:
	std::vector<D2D1_COLOR_F> m_colors; // 16개 표준 색상
	D2D1_COLOR_F              m_selectedColor = D2D1::ColorF(D2D1::ColorF::DodgerBlue);
	int                       m_hoverIndex = -1;
	ColorSelectCallback       m_onColorSelected;

	D2D1_RECT_F GetBoxRect(int index) const;
};

// ============================================================================
// 10. M2DrawListBox: 단일 선택 리스트 박스 컨트롤 (단위 선택 등)
// ============================================================================
class M2DrawListBox : public M2DrawControl
{
public:
	using SelectCallback = std::function<void(M2DrawListBox* pList, int selectedIndex)>;

	M2DrawListBox();

	void AddItem(const std::wstring& item);
	void ClearItems();
	int GetItemCount() const { return static_cast<int>(m_items.size()); }
	const std::wstring& GetItemText(int index) const;

	void SetCurSel(int index);
	int GetCurSel() const { return m_curSel; }

	void SetItemHeight(float h) { m_itemHeight = h; }
	float GetItemHeight() const { return m_itemHeight; }

	void SetOnSelectionChanged(SelectCallback cb) { m_onSelectionChanged = cb; }

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;

private:
	std::vector<std::wstring> m_items;
	int                       m_curSel = 0;
	int                       m_hoverIndex = -1;
	float                     m_itemHeight = 22.0f;
	SelectCallback            m_onSelectionChanged;

	D2D1_RECT_F GetItemRect(int index) const;
};

// ============================================================================
// 11. M2DrawComboBox: 드롭다운 선택 콤보박스 컨트롤
// ============================================================================
class M2DrawComboBox : public M2DrawControl
{
public:
	using SelectCallback = std::function<void(M2DrawComboBox* pCombo, int selectedIndex)>;

	M2DrawComboBox();

	void AddItem(const std::wstring& item);
	void ClearItems();
	int GetItemCount() const { return static_cast<int>(m_items.size()); }
	const std::wstring& GetItemText(int index) const;

	void SetCurSel(int index);
	int GetCurSel() const { return m_curSel; }

	bool IsDropped() const { return m_bDropped; }
	void SetDropped(bool bDropped) { m_bDropped = bDropped; }

	void SetItemHeight(float h) { m_itemHeight = h; }
	float GetItemHeight() const { return m_itemHeight; }

	void SetOnSelectionChanged(SelectCallback cb) { m_onSelectionChanged = cb; }

	bool HitTest(D2D1_POINT_2F pt) const override;

	void Draw(ID2D1RenderTarget* pRT, ID2D1SolidColorBrush* pBrush,
		IDWriteFactory* pDWFactory, IDWriteTextFormat* pTextFormat) override;

	bool OnLButtonDown(D2D1_POINT_2F pt) override;
	bool OnMouseMove(D2D1_POINT_2F pt) override;

private:
	std::vector<std::wstring> m_items;
	int                       m_curSel = 0;
	int                       m_hoverIndex = -1;
	bool                      m_bDropped = false;
	float                     m_itemHeight = 22.0f;
	SelectCallback            m_onSelectionChanged;

	D2D1_RECT_F GetHeaderRect() const { return m_bounds; }
	D2D1_RECT_F GetDropDownRect() const;
	D2D1_RECT_F GetItemRect(int index) const;
};


