#pragma once

#include "D2DGraphicEngine.h"
#include <string>
#include <vector>

namespace ge {

// 페인트통(Step 5.6)용 닫힌 영역 탐지.
// D2D 에는 평면 분할(Divide)이 없으므로 "래스터 플러드 필 + 윤곽 추적 + 단순화" 방식으로 구현한다:
//  1) 보이는 경로/프레임의 윤곽을 오프스크린 비트맵에 선으로 그린다 (끊긴 틈 2px 이하는 선 굵기로 메워짐)
//  2) 클릭 지점에서 선이 아닌 픽셀을 플러드 필 (비트맵 가장자리에 닿으면 열린 영역 = 실패)
//  3) 채워진 영역의 바깥 윤곽을 따라가 다각형으로 만들고 Ramer-Douglas-Peucker 로 단순화
// 결과 다각형은 월드 좌표이며 정밀도는 래스터 해상도(최대 약 2px/단위)에 따른다.
struct RegionFillOptions {
	float maxPixelsPerUnit = 2.0f;   // 월드 1 단위당 최대 픽셀 수 (영역이 크면 자동으로 낮춤)
	int   maxSide = 2400;            // 비트맵 한 변 최대 픽셀
	float lineWidthPx = 2.0f;        // 윤곽선을 그릴 최소 굵기(픽셀). 이 값 이하의 틈은 막힌 것으로 본다
	float simplifyPx = 1.3f;         // 윤곽 단순화 허용 오차(픽셀). 대각선의 계단 모양이 직선으로 합쳐질 만큼
};

// clickWorld 를 둘러싼 가장 안쪽 닫힌 영역의 바깥 윤곽(월드 좌표 다각형)을 구한다. 못 찾으면 false + err
bool FindEnclosedRegion(Document& doc, Vec2 clickWorld, std::vector<Vec2>& polygon, std::wstring* err, const RegionFillOptions& opt = RegionFillOptions());

// 다각형 면적(절대값) — 검증/표시용
float PolygonArea(const std::vector<Vec2>& poly);

} // namespace ge
