#pragma once
// 반응형 레이아웃 계산 (Step 4.7 / 7.4.6). 그래픽 엔진(D2D)에 의존하지 않는 순수 계산이다.
// 화면 미리보기와 HTML 내보내기가 같은 결과를 내도록 여기서만 값을 계산한다.
// 기본 값(데스크톱)에 폭별 재정의를 덮어쓰는 구조이며, 재정의가 없는 항목은 기본 값을 따른다.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ge {

// 폭 기준점 (데스크톱 / 태블릿 / 모바일)
enum class ResponsiveBreakpoint : uint8_t {
	Desktop = 0,
	Tablet = 1,
	Mobile = 2
};

struct ResponsiveWidthPreset {
	const wchar_t* name;   // 표시 이름
	float width;           // 기준 가로 폭 (px)
};

// 기준점의 이름과 폭 (데스크톱 1920, 태블릿 768, 모바일 375)
const ResponsiveWidthPreset& GetResponsiveWidthPreset(ResponsiveBreakpoint bp);

// 프레임 레이아웃 값 (FrameNode 의 값과 같은 뜻을 순수 값으로 옮긴 것)
struct ResponsiveFrameValues {
	uint8_t layoutMode = 0;      // 0 = 없음(자유 배치), 1 = 가로(Row), 2 = 세로(Column). FrameLayoutMode 와 같은 값
	float gap = 0.0f;
	float padTop = 10.0f;
	float padRight = 10.0f;
	float padBottom = 10.0f;
	float padLeft = 10.0f;
	bool hidden = false;         // 이 폭에서 숨긴다
};

// 폭별 재정의: 값이 있는 항목만 기본 값을 덮어쓴다
struct ResponsiveFrameOverride {
	std::optional<uint8_t> layoutMode;
	std::optional<float> gap;
	std::optional<float> padTop;
	std::optional<float> padRight;
	std::optional<float> padBottom;
	std::optional<float> padLeft;
	std::optional<bool> hidden;

	bool IsEmpty() const {
		return !layoutMode && !gap && !padTop && !padRight && !padBottom && !padLeft && !hidden;
	}
};

// 기본 값에 재정의를 적용한 결과 (기본 값은 바꾸지 않는다)
ResponsiveFrameValues ResolveFrameValues(const ResponsiveFrameValues& base, const ResponsiveFrameOverride& ov);

// 순수 계산 검사 (GeModelPureCheck 의 RunPureModelVerification 에서 호출)
bool RunResponsiveLayoutChecks(std::wstring* failure);

} // namespace ge
