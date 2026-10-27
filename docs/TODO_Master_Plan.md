# TODO: M2Draw 마스터 계획 실행 목록

이 문서는 [Master_Plan_SVG_HTML.md](Master_Plan_SVG_HTML.md)의 실행 단계와 진행 상태를 관리합니다.

**진행 규칙**
- 한 번에 한 단계(Step)씩 진행합니다. 각 단계가 끝나면 빌드하고 결과를 보고한 뒤, **사용자 확인을 받고 나서** 다음 단계로 넘어갑니다.
- 상태 표기: `[ ]` 대기 · `[~]` 진행 중 · `[x]` 완료(사용자 확인됨) · `[!]` 보류/이슈
- 단계마다 따로 커밋할 수 있도록 변경 범위를 작게 유지합니다. 커밋은 사용자가 요청할 때만 합니다.

---

## Phase 0 — 기반 정리 (결함 제거)

### Step 0.1 참조 멤버 제거 (D1)  — ✅ 완료 (사용자 확인 2026-10-27)
- [x] 0.1.1 `Node`의 `name`/`m_nameStr`(CString), `xf`, `style` 참조 멤버 사용처를 getter/setter로 바꿈
  - `xf` 읽기 → `GetTransform()`, `xf = …` → `SetTransform(…)`, `style` → `GetStyleRef()`
- [x] 0.1.2 `PathNode`의 `paths` 참조 멤버 사용처를 `GetPathsRef()`로 바꿈
- [x] 0.1.3 참조 멤버 선언 삭제, 이름 필드를 `std::wstring m_name` 하나로 통일
  - `SetTransform()`이 지오메트리 캐시를 무효화하지 않도록 변경 (변환은 로컬 지오메트리와 무관)
- [x] 0.1.4 빌드 성공 (Debug x64, 신규 경고 0) / 회귀 확인 완료: 도형 생성, 이름 표시, 속성창, 레이어 패널
- ~~메모: `m_shapes`는 항상 비어 있음~~ → **정정**: `m_shapes`는 실사용 중인 이중 모델 (Master Plan D6 참고)

### Step 0.2 Clone 규약 통일 (D2)  — ✅ 완료 (사용자 확인 2026-10-27)
- [x] 1차 시도 버그 (사용자 보고): Ctrl+D가 `ge::Document`에만 복제 → `m_shapes[i]` ↔ `children[i]` 대응이 깨져
  복제본 선택 불가, 다른 도형 데이터가 엉뚱한 노드에 덮어써짐(원 → 사각형), 레이어 패널 선택 실패
  - 수정: 복제 시 `m_shapes`에도 같은 위치에 복제 삽입, 대응이 이미 깨졌거나 활성 레이어 밖이면 복제 거부
- [x] 0.2.1 모든 `Clone()`이 `m_id = 0`, `m_parent = nullptr`, 버전을 새로 시작하도록 수정
  - `Node::ResetIdentity()` / `Node::CopyNodePropsFrom()` 헬퍼 추가, Path/Group/Layer Clone 공통 사용
- [x] 0.2.2 문서에 삽입할 때 `AssignIds`가 새 ID를 발급하는지 확인
  - `Document::DuplicateSelection()` 추가: 원본 바로 위에 복제, `AssignIds` + 자동 이름(rect2 …), 복제본을 선택
  - `CM2DrawDoc::DuplicateSelectedObjects()` + **Ctrl+D** 단축키 연결 (검증 수단)
- [x] 0.2.3 빌드 성공 / 확인 완료: 복제한 도형을 편집해도 원본이 변하지 않는지

### Step 0.2b 단일 모델 전환 (D5 + D6) — ✅ 완료
> 이중 모델(`m_shapes` + `ge::Document`)이 확인됨. 이후 모든 기능(그룹, Z-Order, 레이어, 저장, Undo)이
> 인덱스 대응을 깨뜨리므로, 기존 0.4(선택 단일화)와 0.5.1(`m_shapes` 제거)을 합쳐 먼저 진행한다.
**1단계 — 모델 하나로 합치기**  — ✅ 완료 (사용자 확인 2026-10-27)
- [x] 0.2b.1 생성 도구가 `ge::Document`에만 추가하도록 변경
  - `BaseShapeTool::CommitShape`: 미리보기 도형(이미 `ge::PathNode`)을 그대로 `AddShape` (필드별 복사·이중 등록 제거)
  - 펜 도구: `SyncPenPathToGeDoc` + `push_back` → `CommitPenPath` (작성 중 패스를 문서로 이동)
- [x] 0.2b.4 동기화 코드 제거: `SyncShapeToGeDoc`, `SyncPenPathToGeDoc`, 캔버스 이중 렌더링 루프, 이름 기반 삭제, LayerWnd 이름 기반 가시성 동기화
- [x] 0.2b.5 `m_shapes` 삭제 → `GetShapes()`는 **활성 레이어 자식 목록의 읽기 전용 뷰**로 변경
  - `m_currentPath`는 "펜 작성 중 임시 패스"로 유지 (문서 모델 아님)
- [x] 부수 수정
  - `GetPathsRef()`/`GetStyleRef()`가 지오메트리 캐시를 무효화 (기존엔 구형 그리기가 캐시 없이 매번 재생성해 누락이 가려졌음). 읽기 전용 사용처는 `GetPaths()`/`GetStyle()`로
  - 활성 레이어가 숨김/잠금이면 선택 도구가 상호작용하지 않음 (기존 이름 동기화 꼼수 대체)
  - 레이어 패널: 레이어 전환 시 선택 초기화, 다른 레이어의 도형 클릭 시 그 레이어를 활성화

**2단계 — 인덱스 선택 → 노드 포인터 선택**  — ✅ 완료 (사용자 확인 2026-10-27)
- [x] 0.2b.2 SelectTool: 자체 선택 상태(`m_selectedIndices`) 삭제 → `ge::Document::GetSelection()`이 유일한 선택 상태
  - 히트테스트·마키 선택이 **모든 보이는/잠기지 않은 레이어** 대상, 잠긴 개별 도형도 제외
  - 다른 레이어 도형을 클릭하면 그 레이어가 활성화 (Inkscape 방식)
  - API: `SelectNode(Node*, pDoc)`, `ClearSelection(pDoc)`
  - 스케일/회전이 "핸들을 잡은 도형"에 적용되도록 수정 (기존: 다중 선택 시 인덱스가 가장 작은 도형에 적용되던 문제)
- [x] 0.2b.3 패널(`CD2DPropertyView`, `CPropertiesWnd`, `CToolOptionBar`): 인덱스 → `NodeId` 보관 + `FindById` 조회 (삭제/새 문서 후 dangling 없음)
  - `UpdateShapePropertiesSelection(ge::Node*, pDoc)` 등 선택 전달 함수 시그니처 변경
  - 속성창/옵션바의 "같은 인덱스 노드에 다시 복사" 동기화 블록 삭제 (다른 도형을 덮어쓸 위험)
  - `SyncDocSelection`, `GetShapes()` 삭제, 복제의 "활성 레이어만" 제한 해제
- [x] 0.2b.6 빌드 성공 / 확인 완료: 전체 회귀 (생성/선택/변형/노드편집/복제/삭제/레이어 패널/다중 레이어)

### Step 0.3 LayerNode 필드 가림 제거 + 조상 체인 판정 (D3)
- [ ] 0.3.1 LayerNode의 `m_bVisible`/`m_bLocked`/`m_opacity` 및 이름을 가리는 메서드 삭제
- [ ] 0.3.2 `blendMode`를 Node로 올림
- [ ] 0.3.3 `Node::IsEffectivelyVisible()` / `IsEffectivelyLocked()` 추가 → HitTester, SnapManager, 렌더러에 적용
- [ ] 0.3.4 `LayerWnd`의 자식 visible 우회 코드 정리
- [ ] 0.3.5 빌드 + 숨김/잠금 레이어 도형이 클릭되지 않는지 확인

### Step 0.4 선택 상태 단일화 (D5)  — ✅ Step 0.2b 로 통합 완료
- [x] 0.4.1 `SelectTool::m_selectedIndices` → `Document` 선택(`Node*`)으로 바꿈
- [x] 0.4.2 `SyncDocSelection` 및 인덱스 기반 사용처(PropertyView, ToolOptionBar, LayerWnd, Doc 삭제) 정리
- [x] 0.4.3 빌드 + 다중 선택/마키 선택/레이어 패널 선택 동기화 확인

### Step 0.5 구형 잔재 제거 (D6, D15)
- [x] 0.5.1 ~~`m_shapes`, `m_currentPath`, 이름 기반 삭제 로직 제거~~ → Step 0.2b 로 통합
- [ ] 0.5.2 `Shape`/`PathShape` using 별칭 정리
- [ ] 0.5.3 `D2DVectorEngine_old.h`, `backup_toolbar_20261021/` 삭제 (vcxproj 참조 포함)
- [ ] 0.5.4 빌드 + 삭제 기능(Del, 레이어 휴지통) 확인

### Step 0.6 모델에서 D2D/MFC 의존 분리 (D8, D9, D12)
- [ ] 0.6.1 `Node::Draw` 삭제 → 렌더러를 거쳐 그리도록 함
- [ ] 0.6.2 `GetD2DLayerParameters`를 렌더러로 옮김
- [ ] 0.6.3 D2D 변환 연산자를 `GeD2DInterop.h`의 자유 함수로 옮김 (`Matrix2D` reinterpret_cast 제거)
- [ ] 0.6.4 `Node::ScaleByHandle`/`RotateAboutCenter`/`GetLocalHandlePoints` 중복 제거 → `Transformer`만 사용
- [ ] 0.6.5 빌드 + 모델 헤더를 D2D 없이 컴파일하는 확인용 TU 추가

### Step 0.7 소소한 결함 (D10, D11, D13, D14)
- [ ] 0.7.1 `PathNode::GetKind()` → `GetShapeKind()` 개명
- [ ] 0.7.2 노드를 직접 편집하면 `shapeKind = Path`, `params = monostate`로 전환
- [ ] 0.7.3 `ArrangeSelection`의 Forward/Backward에서 인접한 선택 항목끼리 서로 바뀌는 문제 수정
- [ ] 0.7.4 `UngroupSelection`에서 그룹 opacity를 자식에 보존
- [ ] 0.7.5 빌드 + Phase 0 완료 기준(DoD) 전체 점검

---

## Phase 1 — 영속성 + Undo/Redo
- [ ] Step 1.1 `INodeVisitor` 도입
- [ ] Step 1.2 `SvgWriter` (확장 SVG 저장)
- [ ] Step 1.3 `SvgReader` (XmlLite, 자체 출력물 왕복)
- [ ] Step 1.4 `OnSaveDocument`/`OnOpenDocument` 연동 + `SetModifiedFlag`
- [ ] Step 1.5 스냅샷 방식 Undo/Redo + Ctrl+Z/Y

## Phase 2 — SVG 완성도
- [ ] Step 2.1 Style 모델 마이그레이션 (Paint/Gradient/Stroke)
- [ ] Step 2.2 렌더러를 SVG 규칙에 맞춤 (열린 패스 채우기, 그룹 opacity, 블렌드)
- [ ] Step 2.3 그라디언트 렌더링 + 편집 핸들
- [ ] Step 2.4 클리핑 마스크
- [ ] Step 2.5 불리언 연산 완성
- [ ] Step 2.6 패스 연산 (Simplify, Outline, Offset, Join/Split, Reverse)
- [ ] Step 2.7 엔진 기능 UI 연결 (그룹, Z-Order, 정렬/분배, 노드 추가/삭제 도구)
- [ ] Step 2.8 SVG 출력 다듬기 (네이티브 요소, defs, 순수 SVG 옵션)
- [ ] Step 2.9 외부 SVG 가져오기

## Phase 3 — 텍스트 & 이미지
- [ ] Step 3.1 TextNode 모델 + DirectWrite 렌더
- [ ] Step 3.2 TextTool (인라인 편집, IME)
- [ ] Step 3.3 텍스트 → 윤곽선 변환
- [ ] Step 3.4 ImageNode + 리소스 테이블
- [ ] Step 3.5 텍스트/이미지 SVG 출력

## Phase 4 — HTML 페이지 디자인
- [ ] Step 4.1 Page(Artboard) 개념
- [ ] Step 4.2 FrameNode + Frame 도구
- [ ] Step 4.3 LayoutEngine (flexbox 부분 구현)
- [ ] Step 4.4 Frame 안의 텍스트 동작
- [ ] Step 4.5 HtmlExporter
- [ ] Step 4.6 WebView2 미리보기
- [ ] Step 4.7 반응형 브레이크포인트

## Phase 5 — 생산성 & 부가 출력
- [ ] Step 5.1 스마트 가이드 & 오브젝트 스냅
- [ ] Step 5.2 다중 선택 공통 변환
- [ ] Step 5.3 그룹 격리 편집
- [ ] Step 5.4 클립보드 (SVG)
- [ ] Step 5.5 PNG/JPG 출력
- [ ] Step 5.6 PaintBucketTool
