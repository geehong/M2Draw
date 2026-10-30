# M2Draw — Direct2D 기반 벡터 일러스트레이션 & 시맨틱 웹 UI 디자인 스튜디오

**M2Draw**는 Microsoft Direct2D / DirectWrite 하드웨어 가속 그래픽 엔진(`ge::D2DGraphicEngine`) 위에 구축된 **데스크톱 벡터 그래픽 에디터 및 시맨틱 HTML5/CSS UI 빌더**입니다.  
Adobe Illustrator / Inkscape 수준의 **정밀 베지어 벡터 제도·불리언 연산**과 Figma 스타일의 **프레임(Artboard)·자동 레이아웃(Auto-Layout)·스마트 가이드**, 그리고 래스터화(이미지 굽기) 없이 100% 시맨틱 태그로 변환되는 **표준 HTML UI 컴포넌트 시스템**을 하나의 네이티브 C++/MFC 애플리케이션으로 통합했습니다.

> **상세 사용자 매뉴얼**: 모든 도구, 단축키, 속성창, HTML/SVG 워크플로우에 대한 자세한 사용법은 [**전체 사용자 매뉴얼 (`docs/USER_MANUAL.md`)**](docs/USER_MANUAL.md)을 참조하세요.

---

## 1. 핵심 특징 (Key Features)

### ① 순수 벡터 코어 엔진 (`ge::D2DGraphicEngine`)
- **단일 씬 그래프 모델**: `Document` → `LayerNode` → `GroupNode` / `FrameNode` / `TableNode` / `PathNode` / `TextNode` / `ImageNode`로 이어지는 단일 트리 구조
- **정밀 3차 베지어 기하학**: 1차 도함수($B'(t)=0$) 근 공식 기반의 **Tight Geometric Bounds** 산출 및 버전 연동 $O(1)$ 로컬 경계 캐시, 스트로크 두께를 반영한 시각적 월드 경계(`GetVisualWorldBounds`)
- **고성능 핫패스 캐싱**: `(NodeId, ID2D1Factory*, Version)` 키 기반의 공유 `ID2D1PathGeometry` 캐시(`PathOp::GetCachedGeometry`), 히트테스트/스냅 시 월드 AABB Early-out 사전 필터링
- **파라메트릭 2D 도형**: 사각형(모서리 반경 보존 스케일), 타원, 삼각형, 정다각형, 별(Star), 기어(Gear, 축 구멍 포함), 아르키메데스 나선(Spiral), 하트(Heart), **원호/부채꼴/현/도넛 아크(Arc/Pie/Chord/Donut)**, **블록 화살표(Block Arrow)**, **말풍선(Callout)**

### ② 직관적인 오브젝트 변형(Transform) & 캔버스 기즈모
- **양방향 변형 화살표 기즈모**: 선택된 도형의 모서리·면 핸들 호버 및 드래그 시 도형 회전각($\theta$)에 맞춰 정렬된 D2D 벡터 양방향 스케일 화살표(`↖↘`, `↕`, `↔`), `Ctrl`+면 핸들 시 접선 방향 **양방향 전단(Skew/Shear) 화살표(`⇄`, `⇅`)**, 회전 시 **곡선 회전 호** 및 중심 **기준점($\oplus$)** 표시
- **회전 연동 동적 커서 & 모서리 근접 회전**: 도형이 회전된 각도에 따라 마우스 커서 방향이 $45^\circ$ 단위로 자동 동기화되며, 상단 회전 봉뿐 아니라 4개 모서리 바깥쪽($8\sim 18\text{px}$)에서 즉시 회전 가능
- **다중 선택 결합 기즈모**: 복수 노드 선택 시 전체 월드 경계 상자 기준으로 일괄 스케일, 회전, `Ctrl`+변 드래그 전단(Shear) 지원
- **실시간 플로팅 HUD**: 드래그 중 크기(`W × H`), 회전각(`θ°`), 전단각(`Skew θ°`)을 실시간 배지로 표시

### ③ 패스파인더(Boolean) · 패스 연산 · 페인트 통
- **5종 불리언 연산**: 합치기(`Union`), 빼기(`Subtract`), 교차(`Intersect`), 제외(`Exclude`), 트림/분할(`Trim`)
- **고급 패스 편집**: 스트로크 윤곽선 변환(`Outline Stroke`), 텍스트 패스 변환(`Text to Path`), 패스 단순화(`Simplify`), 곡선 스무딩(`Smooth`), 방향 반전(`Reverse`), 열린 끝점 연결(`Join`), 클리핑 마스크(`Ctrl+7`)
- **닫힌 영역 채우기(Paint Bucket, `K`)**: 교차하는 선분들이 이루는 닫힌 영역을 자동 추적하여 새로운 채움 벡터 도형 생성

### ④ Figma 스타일 프레임(Artboard) · 자동 레이아웃 · 스마트 가이드
- **프레임 컨테이너(`FrameNode`, `F`)**: 4모서리 개별 반경(`tl, tr, br, bl`), 4변 개별 테두리(`Top/Right/Bottom/Left`), 내부 클리핑, 자동 부모 편입(드래그 앤 드롭)
- **자동 레이아웃(Auto-Layout)**: 가로(`Row`) / 세로(`Column`) 배치, 주축/교차축 정렬(`Start`, `Center`, `End`, `SpaceBetween`), 자식 간격(`Gap`), 4방향 여백(`Padding`), `Fixed` / `Hug` / `Fill` 크기 모드
- **자유 배치 제약(Constraints)**: 부모 프레임 리사이즈 시 `Left` / `Right` / `Center` / `LeftRight` 및 `Top` / `Bottom` / `Center` / `TopBottom` 반응형 위치·크기 보정
- **스마트 가이드 & 동일 간격 스냅**: 이동/크기 조절 시 부모·형제 노드의 중심/가장자리 정렬선과 동일 간격(`Equal Spacing`) 치수선 실시간 표시

### ⑤ 래스터화 없는 100% 시맨틱 HTML5 / CSS UI 컴포넌트 & 테이블
- **원자적(Atomic) 표준 웹 UI 컴포넌트 10종 (`UiComp`)**:
  1. **텍스트 박스 (`TextBox`)**: 본문 `<p>`, 제목 `<h1>~<h3>`, 인용구 `<blockquote>`, 캡션 `<small>` (자동 줄바꿈 및 프리셋 연동)
  2. **입력창 (`Input`)**: `<input type="text|password|email|number|search|date">` 및 멀티라인 `<textarea>`
  3. **체크 / 라디오 / 스위치 (`Check`)**: `<label><input type="checkbox|radio" role="switch"><span>...</span></label>`
  4. **콤보박스 & 리스트박스 (`Combo`)**: 드롭다운(`<select>`) 및 다중 행 리스트박스(`<select size="N" multiple>`)
  5. **목록 (`List`)**: 글머리 목록(`<ul>`), 번호 목록(`<ol>`), 박스형 목록(`<li>`)
  6. **바 (`Bar`)**: 진행률(`<progress>`), 슬라이더(`<input type="range">`), 구분선(`<hr>`)
  7. **탭 바 (`Tabs`)**: `role="tablist"` / `role="tab"` 버튼 그룹
  8. **배지 / 칩 (`Badge`)**: 알약형 상태 태그 및 닫기(`×`) 아이콘
  9. **카드 (`Card`)**: `<article>` 시맨틱 카드 레이아웃 (미디어 영역 + `<h3>` + `<p>` + `<button>`)
  10. **페이지네이션 / 경로 (`Pager`)**: `<nav aria-label="pagination|breadcrumb">`
- **인터랙티브 테이블 (`TableNode`)**: 셀 병합(`Merge`)·분할(`Split`), 행/열 추가·삭제 및 경계선 드래그 리사이즈, 헤더 행(`<thead><th>`), 셀 테두리 종류(`solid`/`dashed`/`dotted`), 더블클릭 인라인 텍스트 편집
- **시맨틱 HTML/CSS 내보내기 (`HtmlExporter`)**: 프레임 자동 레이아웃은 CSS `display: flex`로, 자유 배치는 `position: absolute`로, 사각형/원은 깔끔한 `<div>`로, 구분선은 `<hr>`로, 복합 곡선만 인라인 `<svg>`로 출력

### ⑥ 무손실 SVG 왕복(Round-Trip) & 공통 외형(Appearance)
- **무손실 SVG 저장/복원 (`SvgWriter` / `SvgReader`)**: 표준 SVG 1.1 호환 출력과 함께 `m2:*` 네임스페이스 메타데이터로 파라메트릭 도형 파라미터, 프레임 레이아웃, 테이블 병합 구조, UI 컴포넌트 속성을 100% 보존하며, 외부 배포용 **Clean SVG** 내보내기도 지원
- **공통 외형 & 그림자 (`Shadow`)**: 바깥 그림자(Drop Shadow) 및 안쪽 그림자(Inset Shadow), 블러(`Blur`)·확산(`Spread`, 음수 축소 포함), 혼합 모드(`Multiply`, `Screen`, `Overlay`), HTML `box-shadow` / `text-shadow` / `filter: drop-shadow(...)` 자동 매핑

---

## 2. 프로젝트 디렉터리 및 주요 소스 구조

```text
M2Draw/
├── D2DGraphicEngine.h / .cpp   # 핵심 벡터 씬 그래프, PathNode/FrameNode/TableNode, 베지어 기하학, 렌더러, 패스 연산
├── D2DLayer.h / .cpp           # LayerNode 및 레이어 블렌딩/가시성/잠금 모델
├── D2DShadowRender.h / .cpp    # 바깥(Drop) 및 안쪽(Inset) 그림자 D2D 렌더링 (4-코너 비균일 반경 지원)
├── D2DTableRender.h / .cpp     # TableNode 격자·병합 셀·헤더 D2D 렌더링
├── ITool.h / .cpp              # SelectTool(양방향 변형 기즈모/Skew/회전), GradientTool, BoxShapeTool 등 도구 구현
├── ToolManager.h / .cpp        # 도구 레지스트리, PenTool/PolylineTool, FrameTool, TableTool, UiComponentTool
├── ToolBoxPane.h / .cpp        # 좌측 도킹 도구상자 및 플라이아웃(Flyout) 서브도구 메뉴
├── ToolOptionBar.h / .cpp      # 상단 컨텍스트 옵션바 (좌표/크기, 도형 파라미터, 정렬/배분, HTML/UI 컴포넌트 바)
├── D2DPropertyView.h / .cpp    # 우측 Direct2D 속성 패널 (채우기/그라디언트, 스트로크, 그림자, 프레임, 테이블, UI 컴포넌트)
├── LayerWnd.h / .cpp           # 레이어 & 오브젝트 트리 패널 (드래그 순서 변경, 가시성/잠금 토글)
├── SnapManager.h / .cpp        # 그리드·정점·중점·중심·교차점 오브젝트 스냅 엔진
├── SmartGuides.h / .cpp        # Figma식 스마트 정렬 가이드 및 동일 간격(Equal Spacing) 스냅
├── GroupTransform.h / .cpp     # 다중 선택 결합 경계 상자 스케일·회전·전단(Skew) 행렬 연산
├── PaintBucket.h / .cpp        # 닫힌 영역 탐색 및 채우기(RegionFill 연동)
├── SvgWriter.h / .cpp          # 표준 SVG / M2Draw 확장 SVG / 스냅샷 직렬화
├── SvgReader.h / .cpp          # SVG 파서 및 파라메트릭 도형·프레임·테이블·UI 컴포넌트 복원
├── HtmlExporter.h / .cpp       # 시맨틱 HTML5 + CSS3 코드 생성기 (단일 파일 / 분리 파일)
├── HtmlPreview.h / .cpp        # 내보낸 HTML/CSS 실시간 브라우저 미리보기 연동
├── RasterExport.h / .cpp       # WIC 기반 PNG / JPEG / BMP / WebP 고해상도 래스터 내보내기
├── UndoManager.h / .cpp        # 압축 스냅샷 기반 다단계 Undo/Redo 및 원자 컴포넌트 선택 복원
├── HtmlComponents/
│   ├── TableComponent.h / .cpp # 테이블 생성·행/열 리사이즈·셀 텍스트 편집 헬퍼
│   └── UiComponents.h / .cpp   # 10종 표준 HTML UI 컴포넌트 생성·재구성·속성 기술자(Descriptor)
├── GeModelPureCheck.cpp        # D2D 의존성 없는 순수 모델 자체 검증 스위트 (섹션 1~45)
├── RuntimeVerification.cpp     # D2D/WIC 런타임 자체 검증 스위트
└── docs/
    ├── USER_MANUAL.md          # 전체 사용자 매뉴얼
    └── TODO_Master_Plan.md     # 아키텍처 로드맵 및 단계별 구현 기록
```

---

## 3. 빌드 및 자체 검증 (Build & Verification)

### 요구 사항
- **OS**: Windows 10 / 11 (x64)
- **컴파일러**: Visual Studio 2022 / 2026 (MSVC v143 이상, MFC 및 ATL 구성 요소 포함)
- **SDK**: Windows 10/11 SDK (Direct2D, DirectWrite, Windows Imaging Component 포함)

### MSBuild 명령줄 빌드
```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1

& $msbuild M2Draw.vcxproj /p:SolutionDir="C:\www\project\desktop\ApiDDraw\" /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```

### 자동 자체 검증 (Self-Verification Suite)
`Debug` 구성으로 `M2Draw.exe`를 시작하면 애플리케이션 초기화 단계(`CM2DrawApp::InitInstance`)에서 두 가지 자동 검증 스위트가 즉시 실행됩니다:
1. **`ge::test::RunPureModelVerification` (`GeModelPureCheck.cpp`, 섹션 1~45)**:
   - D2D 헤더 없이 순수 모델 ABI, 정밀 베지어 경계($B'(t)=0$), 불리언/패스 연산, 프레임 자동 레이아웃·제약 조건, 테이블 병합, 10종 UI 컴포넌트, 2D 파라메트릭 도형(`Arc`, `BlockArrow`, `Callout`), 단일/다중 전단(`Skew`), SVG 무손실 왕복, 시맨틱 HTML 내보내기, Undo/Redo 무결성을 전수 검사합니다.
2. **`ge::RunRuntimeVerification` (`RuntimeVerification.cpp`)**:
   - Direct2D/WIC 렌더 타깃을 생성하여 실제 픽셀 렌더링, PNG/JPEG 래스터 내보내기, 클립보드 SVG, 페인트 통 영역 채우기를 검증합니다.

---

## 4. 문서 링크
- 📘 [**전체 사용자 매뉴얼 (`docs/USER_MANUAL.md`)**](docs/USER_MANUAL.md)
- 🗂️ [**마스터 플랜 및 개발 이력 (`docs/TODO_Master_Plan.md`)**](docs/TODO_Master_Plan.md)

