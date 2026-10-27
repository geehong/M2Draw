#pragma once

#include <string>

namespace ge {
namespace test {

// D2D/MFC 없이(GE_NO_D2D) 컴파일된 순수 모델로 SVG 저장/복원, ID 보존, Undo/Redo 를 실제로 실행해 검증.
// 실패 시 false 와 원인을 반환 (Debug 빌드 시작 시 M2Draw.cpp 가 호출)
bool RunPureModelVerification(std::wstring* failure = nullptr);

} // namespace test
} // namespace ge
