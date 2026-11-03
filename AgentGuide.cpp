#include "pch.h"
#include "AgentGuide.h"

#include <cstdlib>
#include <string>

namespace ge {

std::wstring AgentWorkspaceRoot()
{
	wchar_t* env = nullptr;
	size_t len = 0;
	std::wstring root;
	if (_wdupenv_s(&env, &len, L"M2DRAW_WORKSPACE") == 0 && env && *env) root = env;
	if (env) std::free(env);
	if (!root.empty()) return root;

	wchar_t* profile = nullptr;
	if (_wdupenv_s(&profile, &len, L"USERPROFILE") == 0 && profile) root = profile;
	if (profile) std::free(profile);
	return root + L"\\M2Draw_Agent";
}

std::string BuildAgentGuideUtf8()
{
	return R"(# M2Draw 에이전트 안내

M2Draw 는 벡터 디자인 앱입니다. 당신(에이전트)은 이 앱의 작업 폴더에 있는 파일을 읽고, 결과 SVG 를 써서 앱 화면에 레이아웃을 만들어 줍니다.

## 1. 작업 흐름
1. 원본 디자인을 읽습니다: `reference.png` (원본 그림), `dom.json` (원본 페이지의 영역 요약).
2. 현재 작업 상태를 읽습니다: `current/scene.json` (사용자가 고친 최신 상태), `current/preview.png` (최신 그림). 없으면 `scene.json`, `preview.png` 를 봅니다.
3. 지시문 `prompt.md` 와 사용자 요청을 따릅니다.
4. 결과를 `result/<이름>.svg` 로 저장합니다. 앱이 자동으로 가져와 작업 프레임 안에 표시합니다.
5. 사용자가 고치면 `current/` 가 갱신됩니다. 그 내용을 읽고 수정 요청을 처리합니다.

## 2. 좌표와 크기
- 대지 크기는 `scene.json` 의 `document.width`, `document.height` 입니다 (보통 1920 x 1080).
- SVG 좌표는 대지(문서) 좌표를 씁니다. 원점은 왼쪽 위 (0,0) 입니다.
- SVG 루트에 `width`, `height`, `viewBox` 를 맞춰 쓰세요.

## 3. 영역 역할 (role)
`scene.json` 과 `dom.json` 의 role 값은 HTML 태그 역할입니다.
- header: 머리 영역 (보통 위쪽 전체 너비)
- nav: 내비게이션 (메뉴 링크 목록)
- sidebar (aside): 옆 목록 영역
- main: 본문
- section: 큰 구획
- footer: 푸터 (보통 아래쪽 전체 너비)
- 역할이 없는 프레임은 용도가 정해지지 않은 상자입니다.

## 4. 앱에서 쓸 수 있는 컨트롤
앱에는 HTML 컨트롤 도형이 있습니다. 결과 SVG 에서도 같은 모양을 표현하세요.
- 버튼(button), 헤딩(h1~h6), 단락(p), 링크(a)
- 입력칸(text input), 여러 줄 입력(textarea), 콤보박스(select), 목록 상자(listbox)
- 체크박스, 라디오, 토글 스위치, 슬라이더, 진행 막대
- 카드(card), 탭(tabs), 아코디언, 알림(alert), 모달, 배지(badge), 이미지 자리
- 테이블(table), 목록(ul/ol), 구분선(hr), 간격(spacer)
- 주석은 빨간 글씨 메모입니다. 사용자의 수정 요청으로 이해하세요.

## 5. 결과 SVG 규칙
- 파일 이름: 영문, 숫자, `-`, `_` 만 (예: `v1.svg`, `header-v2.svg`).
- 같은 이름으로 다시 저장하면 앱은 이전 결과를 지우고 새 결과로 바꿉니다.
- 레이어나 `<image>` 로 원본 그림을 넣지 마세요. 원본은 참고용입니다.
- 원본 사진이나 그림을 복사하지 마세요. 사진 자리는 회색 상자와 "사진" 라벨로 두세요.
- 영역 위치, 비율, 구성 요소 개수를 원본과 맞추세요.

## 5-1. 글자와 내용 (반드시 지키세요)
- 제목, 메뉴, 버튼, 기사 제목 등 글자는 `reference.png` 에서 읽은 그대로 옮기세요. 새로 만들거나 바꾸지 마세요.
- 브랜드 이름, 로고 글자는 원본에 보이는 것만 씁니다. 없으면 로고 자리만 두세요.
- 글자를 읽을 수 없으면 추측하지 말고 "[읽기 불가]" 라고 두세요.

## 5-2. 글자 크기와 줄바꿈
- 메뉴, 버튼, 링크 글자는 한 줄에 들어가도록 칸 폭을 넉넉히 잡으세요. 대략 글자 하나에 9px (Segoe UI 16px 기준) 이상입니다.
- 로고와 메뉴가 겹치지 않도록 로고 오른쪽에 여백 24px 이상을 두세요.
- 제목은 본문보다 확실히 크고 굵게 (대략 제목 32px 이상, bold) 씁니다. 제목은 이미지 위쪽에 둡니다.

## 6. 사용할 도구 (MCP)
- `m2draw_list_packages`: 패키지 목록
- `m2draw_get_guide`: 이 안내 읽기
- `m2draw_get_scene`: 구조 데이터 (current 가 있으면 최신)
- `m2draw_get_prompt`: 지시문
- `m2draw_get_image_paths`: 그림 경로 (원본, 최신 미리보기)
- `m2draw_put_result`: 결과 SVG 저장
)";
}

} // namespace ge