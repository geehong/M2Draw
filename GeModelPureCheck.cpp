// GeModelPureCheck.cpp
//
// Step 0.6 검증용 TU:
// GE_NO_D2D 를 정의한 상태에서 그래픽스 엔진의 모델 헤더(D2DGraphicEngine.h, D2DLayer.h)가
// Direct2D(<d2d1.h>)나 Windows/MFC 헤더 의존 없이 순수 C++로 독립 컴파일되는지 빌드 시 확인합니다.

#define GE_NO_D2D
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"

// 컴파일 단위가 비어있지 않도록 더미 함수 추가
namespace ge {
namespace test {

inline void PureModelVerification()
{
	Document doc;
	auto layer = doc.AddLayer(L"TestLayer");
	auto path = std::make_unique<PathNode>();
	path->AddAnchor(Vec2{ 10.0f, 20.0f });
	layer->AddChild(std::move(path));

	Matrix2D mat = Matrix2D::Translation(5.0f, 5.0f);
	Vec2 pt = mat.TransformPoint(Vec2{ 1.0f, 2.0f });
	(void)pt;
}

} // namespace test
} // namespace ge
