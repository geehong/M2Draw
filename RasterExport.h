#pragma once

#include "D2DGraphicEngine.h"
#include <string>

namespace ge {

// PNG/JPG 래스터 출력 (Step 5.5): WIC 비트맵에 Direct2D 로 오프스크린 렌더링한 뒤 WIC 인코더로 저장한다.
struct RasterExportOptions {
	bool  jpeg = false;           // false = PNG, true = JPG
	float scale = 1.0f;           // 배율 (1x, 2x, 4x ...)
	float dpi = 96.0f;            // 파일에 기록할 해상도 (픽셀 수에는 영향 없음, 배율이 픽셀 수를 정함)
	bool  transparent = true;     // PNG 투명 배경. JPG 는 항상 불투명 (문서 배경색으로 채움)
	int   jpegQuality = 92;       // 1~100
};

// region: 문서 좌표의 출력 영역 (nullptr = 문서 전체 0,0,width,height). 성공하면 true, 실패 시 err 에 사유
bool ExportRaster(Document& doc, const std::wstring& path, const RasterExportOptions& opt, const Rect2* region, std::wstring* err);

// 출력 픽셀 크기 계산 (대화상자 미리 표시용). 너무 크면 false
bool RasterExportPixelSize(const Document& doc, const RasterExportOptions& opt, const Rect2* region, unsigned& w, unsigned& h);

// Debug 시작 시 실행되는 런타임 자체 검증 (D2D/WIC/클립보드 직렬화/페인트통). 실패하면 false + 사유
bool RunRuntimeVerification(std::wstring* failure);

} // namespace ge
