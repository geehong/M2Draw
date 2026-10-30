#pragma once

#include "ITool.h"
#include <vector>

// 사이드바 플라이아웃 / 상단 서브 도구 바가 함께 쓰는 그룹 정의 (Step 6.18).
// 그룹 소속은 이 표에서만 정한다. head = 그룹 대표(버튼) 타입, items = 항목 순서, profileKey = 마지막 선택 저장 키.
struct ToolGroupDef
{
	ToolType head;
	std::vector<ToolType> items;
	const wchar_t* profileKey;
};

inline const std::vector<ToolGroupDef>& GetToolGroups()
{
	static const std::vector<ToolGroupDef> kGroups = {
		{ ToolType::Pen,         { ToolType::Pen, ToolType::Polyline, ToolType::Line },                                              L"PenGroupTool" },
		{ ToolType::Rect,        { ToolType::Rect, ToolType::RoundRect, ToolType::Square, ToolType::Diamond, ToolType::Triangle },    L"RectGroupTool" },
		{ ToolType::Ellipse,     { ToolType::Ellipse, ToolType::CircleCenter, ToolType::Circle3Pt, ToolType::Arc3Pt, ToolType::Pie, ToolType::Donut }, L"EllipseGroupTool" },
		{ ToolType::Polygon,     { ToolType::Polygon, ToolType::Star, ToolType::Gear, ToolType::Spiral, ToolType::Heart },           L"PolygonGroupTool" },
		{ ToolType::Frame,       { ToolType::Frame, ToolType::Table, ToolType::ComponentHeading, ToolType::ComponentButton,
		                           ToolType::ComponentDivider, ToolType::ComponentSpacer },                                           L"HtmlGroupTool" },
		{ ToolType::PaintBucket, { ToolType::PaintBucket, ToolType::Gradient },                                                      L"FillGroupTool" },
		{ ToolType::ZoomIn,      { ToolType::ZoomIn, ToolType::ZoomOut },                                                            L"ZoomGroupTool" },
		{ ToolType::ActionGroup, { ToolType::ActionGroup, ToolType::ActionUngroup, ToolType::ActionWrapFrame },                      L"GroupActionTool" },
		{ ToolType::ActionToFront, { ToolType::ActionToFront, ToolType::ActionForward, ToolType::ActionBackward, ToolType::ActionToBack }, L"ArrangeActionTool" },
		{ ToolType::ActionBooleanUnion, { ToolType::ActionBooleanUnion, ToolType::ActionBooleanSubtract, ToolType::ActionBooleanIntersect,
		                                  ToolType::ActionBooleanExclude }, L"BooleanActionTool" },
		{ ToolType::ActionClipMask, { ToolType::ActionClipMask, ToolType::ActionReleaseMask },                                       L"ClipMaskActionTool" },
	};
	return kGroups;
}

// 도구가 속한 그룹 (없으면 nullptr)
inline const ToolGroupDef* FindToolGroup(ToolType t)
{
	for (const ToolGroupDef& g : GetToolGroups())
		for (ToolType it : g.items)
			if (it == t) return &g;
	return nullptr;
}
