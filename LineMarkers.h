#pragma once

#include "D2DGraphicEngine.h"
#include <string>
#include <vector>

namespace ge {
namespace Markers {

// 선 마커 모듈 (6.25.3). D2D 에 의존하지 않는다 (GE_NO_D2D 검증 가능).
// 시작/끝 마커는 열린 서브패스에만, 중간 마커는 꼭짓점(열린 패스는 안쪽 앵커, 닫힌 패스는 모든 앵커)에 붙는다.

constexpr int kKindCount = 7;
const char*    KindId(MarkerKind k);                          // "arrow" ...
bool           KindFromId(const std::string& id, MarkerKind& out);
const wchar_t* KindLabel(MarkerKind k);                       // 속성창 표시 이름

// 마커 하나의 배치: 경로 로컬 좌표의 위치와 방향 (마커 도형의 +x 가 dir 쪽)
struct Placement {
	enum Role { Start, Mid, End };
	MarkerKind kind = MarkerKind::None;
	Role       role = Mid;
	Vec2       pos{};
	Vec2       dir{ 1.0f, 0.0f };   // 단위 벡터. 시작 마커는 선 바깥쪽을 향한다
	float      size = 0.0f;
};

// 마커 크기 = 선 두께(최소 0.5) x markerScale
float MarkerSize(const Stroke& s);

// 열린 서브패스가 하나라도 있는가 (시작/끝 마커를 줄 수 있는가)
bool HasOpenSubPath(const std::vector<SubPath>& paths);

// 모든 마커의 배치
std::vector<Placement> Compute(const std::vector<SubPath>& paths, const Stroke& s);

// 채워진 화살촉(Arrow)이 선 끝과 겹치지 않도록 열린 서브패스의 해당 끝을 화살촉 길이만큼 짧게 만든 경로
std::vector<SubPath> TrimEnds(const std::vector<SubPath>& paths, const Stroke& s);

// 마커 도형 (경로 로컬 좌표). circle 이면 pts[0] 이 중심, radius 가 반지름
struct Shape {
	std::vector<Vec2> pts;
	bool  closed = true;
	bool  filled = true;
	bool  circle = false;
	float radius = 0.0f;
};
Shape MakeShape(const Placement& p);

// 표준 SVG 출력 (다른 프로그램에서도 마커가 보이도록): 종류 + 선 색(#rrggbb) + 배율마다 <marker> 하나를 <defs> 에 정의한다.
// 크기 단위는 선 두께(markerUnits=strokeWidth), 방향은 orient=auto-start-reverse (시작 마커는 바깥쪽)
// trimmed: 순수 내보내기에서 채워진 화살촉(Arrow) 아래 선 끝을 짧게 쓴 경우. 마커 기준점을 앞으로 밀어 꼭짓점이 원래 끝점에 오게 한다
std::string SvgMarkerId(MarkerKind k, const std::string& colorHex, float scale, bool trimmed = false);
std::string SvgMarkerDef(MarkerKind k, const std::string& colorHex, float scale, bool trimmed = false);   // 한 줄짜리 <marker ...>...</marker>

} // namespace Markers
} // namespace ge
