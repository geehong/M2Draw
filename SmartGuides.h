#pragma once

#include "D2DGraphicEngine.h"
#include <vector>

namespace ge {

// 스마트 가이드 (Step 4.5.14): 이동/크기 조절 중인 요소를 부모 프레임과 형제 요소의 가장자리/가운데에 맞추고
// Figma 처럼 붉은 가이드선을 보여주기 위한 계산 모듈. 렌더링 API 에 의존하지 않는다 (월드 좌표만 사용).

// 화면에 그릴 가이드 한 줄. vertical=true 면 x=pos 의 세로선(from~to 는 y 범위), false 면 y=pos 의 가로선(x 범위)
struct GuideLine {
	bool  vertical = true;
	float pos = 0.0f;
	float from = 0.0f, to = 0.0f;
	std::vector<float> marks;    // 선 위에 x 표시를 찍을 위치 (끝점/가운데)
};

// 맞춰볼 변: 왼쪽/가운데/오른쪽, 위/가운데/아래
struct GapMark {
	bool  horizontal = true;   // true: y=cross 에서 x 가 a~b 인 가로 간격, false: x=cross 에서 y 가 a~b 인 세로 간격
	float a = 0.0f, b = 0.0f;
	float cross = 0.0f;
	float value = 0.0f;        // 간격(px)
};

struct GuideFlags {
	bool left = true, centerX = true, right = true;
	bool top = true, centerY = true, bottom = true;
	bool equalSpacing = false;   // 같은 간격(5.1.3)도 찾는다
};

struct GuideResult {
	float dx = 0.0f, dy = 0.0f;           // 맞추기 위해 더 이동해야 하는 양 (월드)
	bool  snappedX = false, snappedY = false;
	std::vector<GuideLine> lines;
	std::vector<GapMark>   gaps;   // 같은 간격 표시 (equalSpacing 으로 맞춰졌을 때)
};

// 맞출 후보: 형제 요소 상자들과 (있다면) 부모 프레임 상자 — 모두 월드 좌표
struct GuideTargets {
	std::vector<Rect2> rects;
	bool  hasParent = false;
	Rect2 parent{};
};

// 노드의 월드 좌표 경계 상자. 경계가 없으면 false
bool GuideWorldBounds(const Node& n, Rect2& out);

// moving[0] 이 속한 레이어의 모든 보이는 노드(형제, 사촌, 손자 등 계층에 상관없이)의 상자와 부모 프레임 상자를 수집한다.
// moving 자신과 그 후손은 제외한다. 표 내부(칸 내용)는 제외하고 표 자체의 상자만 포함한다.
GuideTargets CollectGuideTargets(const std::vector<Node*>& moving);
// 새로 만들 요소(아직 노드가 없음)용: 활성 레이어의 모든 보이는 노드 상자 + worldPt 아래 가장 안쪽 프레임을 부모로
GuideTargets CollectGuideTargetsAt(const Document& doc, Vec2 worldPt);

// moving 상자를 후보에 맞춘다. tolWorld 이내의 가장 가까운 변을 선택하며, 맞춰진 상태의 가이드선을 만든다.
// flags 로 맞춰볼 변을 제한할 수 있다 (크기 조절 시 움직이는 변만)
GuideResult SnapToGuides(const Rect2& moving, const GuideTargets& targets, float tolWorld, const GuideFlags& flags = GuideFlags());

} // namespace ge
