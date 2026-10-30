#pragma once

#include "D2DGraphicEngine.h"
#include <vector>

namespace ge {
namespace Shapes {

// 도형 프리셋 기하 (Step 6.13 / 6.18.3). 모두 일반 PathNode 로 저장되는 경로만 만든다 (D2D 없음).
// 좌표는 호출자가 준 월드 좌표를 그대로 쓴다 (노드 변환은 항등).

// 둥근 사각형: (x,y) 좌상단, 크기 w x h, 모서리 반경 r (변 절반으로 제한)
std::vector<SubPath> RoundedRect(float x, float y, float w, float h, float r);
// 마름모: 바운딩 상자 (x,y,w,h) 안의 네 꼭짓점
std::vector<SubPath> Diamond(float x, float y, float w, float h);
// 타원/원: 중심 (cx,cy), 반지름 (rx,ry)
std::vector<SubPath> Ellipse(float cx, float cy, float rx, float ry);
// 도넛: 바깥 원과 안쪽 원 (안쪽 반지름 = innerRatio * 바깥 반지름). 안쪽 원은 반대 방향 (구멍)
std::vector<SubPath> Donut(float cx, float cy, float rOuter, float innerRatio);
// 파이(부채꼴): 중심 (cx,cy), 반지름 r, 시작각/끝각 (라디안, 화면 y 아래 기준)
std::vector<SubPath> Pie(float cx, float cy, float r, float a0, float a1);
// 세 점을 지나는 원의 중심/반지름. 세 점이 일직선이면 false
bool CircleThrough3(Vec2 a, Vec2 b, Vec2 c, Vec2& center, float& radius);
// 시작 a, 중간 m, 끝 b 를 지나는 열린 호. 세 점이 일직선이면 빈 결과
std::vector<SubPath> ArcThrough3(Vec2 a, Vec2 m, Vec2 b);

} // namespace Shapes
} // namespace ge
