#pragma once

#include <string>

namespace ge {
namespace test {

// 레퍼런스 / AI 패키지 / 아이콘 세트 / SVG 가져오기 자체 검증 (창 없이 실행, --selftest-ref)
// 샘플 이미지 경로는 환경변수 M2_SELFTEST_IMAGE 로 바꿀 수 있다 (기본: samples\테스트레퍼런스_티스토리.png)
// 진행 로그는 log 에 쌓고, 실패하면 false 와 사유를 failure 에 넣는다.
bool RunReferenceAgentVerification(std::wstring* failure, std::wstring* log);

} // namespace test
} // namespace ge
