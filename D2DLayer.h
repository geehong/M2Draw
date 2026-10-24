#pragma once

namespace ge {

// 잉크스케이프 호환 레이어 혼합 모드 (Blend Mode)
enum class LayerBlendMode : uint8_t {
	Normal = 0,    // 정상
	Darken,        // 어둡게
	Multiply,      // 곱하기
	ColorBurn,     // 컬러 번
	Lighten,       // 밝게
	Screen,        // 화면(스크린)
	ColorDodge,    // 컬러 닷지
	Overlay,       // 오버레이
	SoftLight,     // 부드러운 빛
	HardLight,     // 강렬한 빛
	Difference,    // 디퍼런스(다름)
	Exclusion,     // 익스클루전(제외)
	Hue,           // 색조
	Saturation,    // 채도
	Color,         // 색상
	Luminosity     // 광도
};

// ---- 잉크스케이프 스타일 레이어 노드 (씬 루트 컨테이너) ----------------------
class LayerNode : public GroupNode {
public:
	LayerNode();
	explicit LayerNode(const std::wstring& name);
	virtual ~LayerNode() = default;

	NodeKind Kind() const override { return NodeKind::Layer; }
	std::unique_ptr<Node> Clone() const override;

	// --- 잉크스케이프 레이어 핵심 속성 ---
	bool  IsVisible() const noexcept        { return m_bVisible; }
	void  SetVisible(bool visible) noexcept { m_bVisible = visible; }

	bool  IsLocked() const noexcept         { return m_bLocked; }
	void  SetLocked(bool locked) noexcept   { m_bLocked = locked; }

	float GetOpacity() const noexcept       { return m_opacity; } // 0.0f ~ 1.0f (0~100%)
	void  SetOpacity(float opacity) noexcept {
		m_opacity = (std::max)(0.0f, (std::min)(1.0f, opacity));
	}

	LayerBlendMode GetBlendMode() const noexcept             { return m_blendMode; }
	void           SetBlendMode(LayerBlendMode mode) noexcept { m_blendMode = mode; }

	// 클리핑 마스크 지원 플래그
	bool  IsClippingMask() const noexcept     { return m_bClippingMask; }
	void  SetClippingMask(bool mask) noexcept { m_bClippingMask = mask; }

	// Direct2D 하드웨어 레이어 파라미터 계산
	D2D1_LAYER_PARAMETERS GetD2DLayerParameters() const;

private:
	bool           m_bVisible      = true;
	bool           m_bLocked       = false;
	float          m_opacity       = 1.0f;     // 100%
	LayerBlendMode m_blendMode     = LayerBlendMode::Normal;
	bool           m_bClippingMask = false;
};

} // namespace ge
