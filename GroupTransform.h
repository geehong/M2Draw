#pragma once

#include "D2DGraphicEngine.h"
#include <vector>

namespace ge {

// 다중 선택 공통 변환 (Step 5.2): 선택 전체의 결합 경계 상자에 대한 스케일/회전/전단(Skew)을
// 각 노드의 로컬 변환에 비례 행렬로 한꺼번에 합성한다. 렌더링 API 에 의존하지 않는다.

// 선택 노드들의 월드 경계 상자 합집합 (경계가 있는 노드가 하나도 없으면 false)
bool UnionWorldBounds(const std::vector<Node*>& nodes, Rect2& out);

// 월드 좌표 변환 행렬 만들기 (anchor 는 변하지 않는 기준점)
Matrix2D MakeWorldScale(float sx, float sy, Vec2 anchor);
Matrix2D MakeWorldRotation(float degrees, Vec2 center);
// 가로 전단: x' = x + k*(y - anchor.y), 세로 전단: y' = y + k*(x - anchor.x)
Matrix2D MakeWorldSkewX(float k, Vec2 anchor);
Matrix2D MakeWorldSkewY(float k, Vec2 anchor);

// 월드 변환 T 를 노드들에 적용: newLocal = initialLocal * (parentWorld * T * parentWorld^-1)
// initialLocal[i] 는 nodes[i] 의 드래그 시작 시점 로컬 변환. 부모 변환을 역행렬로 되돌릴 수 없으면 그 노드는 건너뜀
void ApplyWorldTransform(const std::vector<Node*>& nodes, const std::vector<Matrix2D>& initialLocal, const Matrix2D& T);

} // namespace ge
