#include "pch.h"
#include "ResponsiveLayout.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cstddef>

namespace ge {

namespace {

const ResponsiveWidthPreset kPresets[] = {
	{ L"데스크톱", 1920.0f },
	{ L"태블릿", 768.0f },
	{ L"모바일", 375.0f },
};

} // namespace

const ResponsiveWidthPreset& GetResponsiveWidthPreset(ResponsiveBreakpoint bp)
{
	const size_t index = static_cast<size_t>(bp);
	return kPresets[index < (sizeof(kPresets) / sizeof(kPresets[0])) ? index : 0];
}

ResponsiveFrameValues ResolveFrameValues(const ResponsiveFrameValues& base, const ResponsiveFrameOverride& ov)
{
	ResponsiveFrameValues out = base;
	if (ov.layoutMode) out.layoutMode = *ov.layoutMode;
	if (ov.gap) out.gap = (std::max)(0.0f, *ov.gap);
	if (ov.padTop) out.padTop = (std::max)(0.0f, *ov.padTop);
	if (ov.padRight) out.padRight = (std::max)(0.0f, *ov.padRight);
	if (ov.padBottom) out.padBottom = (std::max)(0.0f, *ov.padBottom);
	if (ov.padLeft) out.padLeft = (std::max)(0.0f, *ov.padLeft);
	if (ov.hidden) out.hidden = *ov.hidden;
	return out;
}

namespace {

bool SameValues(const ResponsiveFrameValues& a, const ResponsiveFrameValues& b)
{
	return a.layoutMode == b.layoutMode && a.gap == b.gap && a.padTop == b.padTop && a.padRight == b.padRight &&
		a.padBottom == b.padBottom && a.padLeft == b.padLeft && a.hidden == b.hidden;
}

} // namespace

bool RunResponsiveLayoutChecks(std::wstring* failure)
{
#define RL_CHECK(cond, text) do { if (!(cond)) { if (failure) *failure = (text); return false; } } while (0)

	// 1) 기준점 폭
	RL_CHECK(GetResponsiveWidthPreset(ResponsiveBreakpoint::Desktop).width == 1920.0f, L"데스크톱 폭이 1920 이 아님");
	RL_CHECK(GetResponsiveWidthPreset(ResponsiveBreakpoint::Tablet).width == 768.0f, L"태블릿 폭이 768 이 아님");
	RL_CHECK(GetResponsiveWidthPreset(ResponsiveBreakpoint::Mobile).width == 375.0f, L"모바일 폭이 375 가 아님");

	// 2) 재정의가 없으면 기본 값 그대로
	ResponsiveFrameValues base;
	base.layoutMode = 1;
	base.gap = 12.0f;
	base.padTop = 20.0f;
	ResponsiveFrameOverride none;
	RL_CHECK(none.IsEmpty(), L"빈 재정의가 비어 있다고 판정되지 않음");
	RL_CHECK(SameValues(ResolveFrameValues(base, none), base), L"재정의가 없는데 기본 값이 바뀜");

	// 3) 간격만 재정의하면 간격만 바뀌고 나머지는 기본 값 유지, 기본 값은 바뀌지 않음
	ResponsiveFrameOverride tabletGap;
	tabletGap.gap = 8.0f;
	const ResponsiveFrameValues tablet = ResolveFrameValues(base, tabletGap);
	RL_CHECK(tablet.gap == 8.0f, L"간격 재정의가 적용되지 않음");
	RL_CHECK(tablet.layoutMode == 1 && tablet.padTop == 20.0f, L"간격 재정의가 다른 항목을 건드림");
	RL_CHECK(base.gap == 12.0f, L"기본 값이 재정의 때문에 바뀜");

	// 4) 모바일: 세로 배치로 바꾸고 숨기기
	ResponsiveFrameOverride mobile;
	mobile.layoutMode = 2;
	mobile.hidden = true;
	const ResponsiveFrameValues phone = ResolveFrameValues(base, mobile);
	RL_CHECK(phone.layoutMode == 2 && phone.hidden, L"모바일 재정의(세로 배치, 숨김)가 적용되지 않음");
	RL_CHECK(phone.gap == 12.0f, L"모바일 재정의가 간격을 건드림");

	// 5) 음수 여백은 0 으로 자른다
	ResponsiveFrameOverride negative;
	negative.padLeft = -5.0f;
	RL_CHECK(ResolveFrameValues(base, negative).padLeft == 0.0f, L"음수 여백이 0 으로 잘리지 않음");

	return true;
#undef RL_CHECK
}

} // namespace ge
