#pragma once

#include "D2DGraphicEngine.h"
#include <vector>

namespace ge {
namespace Trim {

// 트림/연장 기하 모듈 (Step 6.21). D2D 에 의존하지 않는다 (GE_NO_D2D 검증 가능).
// 곡선 교차는 세그먼트를 잘게 평탄화해서 근사한다 (정밀 해석 교차는 6.8 과 함께 개선).

struct Cubic { Vec2 p0, p1, p2, p3; };

// SubPath -> 3차 베지에 세그먼트 목록 (닫힌 경로는 마지막 노드 -> 첫 노드 세그먼트 포함)
std::vector<Cubic> ToCubics(const SubPath& sp);
// 연속된 세그먼트 목록 -> SubPath (closed 면 닫힌 경로)
SubPath FromCubics(const std::vector<Cubic>& segs, bool closed);
// 세그먼트 목록에서 파라미터 구간 [a, b] (파라미터 = 세그먼트 인덱스 + t).
// closed 이면 a > b 일 때 끝을 넘어 감싼다 (a..N 과 0..b 를 이어 붙임)
std::vector<Cubic> Extract(const std::vector<Cubic>& segs, float a, float b, bool closed);

// 직선 여부 (제어점이 끝점을 잇는 직선 위에 있음)
bool IsStraight(const Cubic& c);

struct Cut
{
	float g;    // 교차 파라미터 (대상 세그먼트 인덱스 + t)
	Vec2  pt;   // 교차점 (대상 로컬 좌표)
};

// 대상 세그먼트들과 절단 세그먼트들의 교차. extendCutters 가 true 면 직선 절단 개체를 무한 직선으로 본다 (절단 선 연장)
std::vector<Cut> FindCuts(const std::vector<Cubic>& target, const std::vector<Cubic>& cutters, bool extendCutters);
// 점에 가장 가까운 대상 파라미터 (outDist 가 있으면 거리도 돌려줌)
float NearestParam(const std::vector<Cubic>& target, Vec2 p, float* outDist = nullptr);

struct TrimResult
{
	std::vector<SubPath> pieces;   // 남은 조각 (열린 경로 0~2개, 닫힌 경로는 1개)
	bool ok = false;
};

// 클릭한 구간을 두 교차점 사이에서 잘라 제거한다. 교차점이 두 개 미만인 닫힌 경로는 실패
TrimResult TrimAtClick(const SubPath& target, const std::vector<Cubic>& cutters, bool extendCutters, Vec2 click);

// 열린 경로의 클릭한 쪽 끝을 절단 개체까지 연장한다. virtualCuts 가 true 면 직선 절단 개체의 연장선과의 교차도 쓴다 (가상 교차).
// tol: 끝점으로 인정할 거리 (대상 로컬 단위). 성공하면 out 에 연장된 경로를 넣는다
bool ExtendEnd(const SubPath& target, const std::vector<Cubic>& cutters, bool virtualCuts, Vec2 click, float tol, SubPath& out);

} // namespace Trim
} // namespace ge
