# TODO: M2Draw 마스터 계획 실행 목록

이 문서는 [Master_Plan_SVG_HTML.md](Master_Plan_SVG_HTML.md)의 실행 단계와 진행 상태를 관리합니다.

**진행 규칙**
- 한 번에 한 단계(Step)씩 진행합니다. 각 단계가 끝나면 빌드하고 결과를 보고한 뒤, **사용자 확인을 받고 나서** 다음 단계로 넘어갑니다.
- 상태 표기: `[ ]` 대기 · `[~]` 진행 중 · `[x]` 완료(사용자 확인됨) · `[!]` 보류/이슈
- 단계마다 따로 커밋할 수 있도록 변경 범위를 작게 유지합니다. 커밋은 사용자가 요청할 때만 합니다.
- **모든 신규 노드/속성 단계의 공통 완료 기준 (2026-10-27 재검토에서 추가)**: ① 저장/복원(SvgWriter/Reader 왕복) ② Undo/Redo ③ `Clone()` ④ SVG/HTML 출력 — 그리고 `RunPureModelVerification()`에 왕복 검증 추가
- **2026-10-27 계획 재검토**: Phase 2~5 를 현재 코드와 대조해 수정함 (Step 2.0 신설, 순서 재배치, 누락 항목 추가). 근거는 `Master_Plan_SVG_HTML.md` §7

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

### Step 0.3 LayerNode 필드 가림 제거 + 조상 체인 판정 (D3)  — ✅ 완료 (사용자 확인 2026-10-27)
- [x] 0.3.1 LayerNode의 `m_bVisible`/`m_bLocked`/`m_opacity` 및 이를 가리는 메서드 삭제 → Node 공통 필드 사용
  - `Node::SetOpacity`가 0~1로 클램프 (기존 LayerNode 전용 동작 이관)
  - 부수 효과 정리: 레이어 불투명도 팝업은 Node 필드에, 렌더러 PushLayer 는 LayerNode 필드에 쓰고/읽고 있었음 →
    통합 후 이중 적용되지 않도록 도형별 불투명도 계산에서 레이어는 제외 (레이어는 PushLayer 로만 합성)
- [x] 0.3.2 `LayerBlendMode`와 `blendMode`를 Node로 올림 (`CopyNodePropsFrom`에서 복사)
- [x] 0.3.3 `Node::IsEffectivelyVisible()` / `IsEffectivelyLocked()` 추가
  - HitTester(선택 노드 앵커 검사), SelectTool(앵커/핸들/호버/오버레이)에 적용
  - SnapManager·렌더러는 레이어부터 재귀하므로 필드 통합만으로 숨김 레이어 제외됨
  - 레이어/도형을 숨기거나 잠그면 해당 노드를 선택에서 자동 제외 (`CM2DrawDoc::PruneNonInteractiveSelection`)
- [x] 0.3.4 `LayerWnd`의 자식 visible 우회 코드 정리 (Step 0.2b-1에서 완료)
- [x] 부수 수정: 레이어 패널 행 클릭 시 선택 갱신이 행 목록을 재구성한 뒤에도 무효화된 `row`를 계속 사용하던 문제(use-after-free) 수정
- [x] 부수 수정 (사용자 보고 크래시): 그리드 설정 체크박스 클릭 시 간헐적 접근 위반
  - 원인: 체크박스 콜백 → `ApplyCanvasSettings` → 옵션바 `SetMode` → `RecalcLayout` → 도킹 창 `AdjustLayout` →
    `SetPaneMode` → `InitControls`가 실행 중인 패널을 해제 → 복귀한 `M2DrawPanel::OnLButtonDown`이 해제된 메모리에 기록
  - 수정: `CD2DPropertyView`의 입력 이벤트 전달 시 패널을 지역 `shared_ptr`로 유지
  - 남은 개선점(별도): `AdjustLayout`이 크기 변경마다 컨트롤 전체를 재생성함 → 모드가 바뀔 때만 재생성하도록 개선 필요
- [x] 0.3.5 빌드 성공 / 확인 완료

### Step 0.4 선택 상태 단일화 (D5)  — ✅ Step 0.2b 로 통합 완료
- [x] 0.4.1 `SelectTool::m_selectedIndices` → `Document` 선택(`Node*`)으로 바꿈
- [x] 0.4.2 `SyncDocSelection` 및 인덱스 기반 사용처(PropertyView, ToolOptionBar, LayerWnd, Doc 삭제) 정리
- [x] 0.4.3 빌드 + 다중 선택/마키 선택/레이어 패널 선택 동기화 확인

### Step 0.5 구형 잔재 제거 (D6, D15)  — ✅ 완료 (사용자 확인 2026-10-27)
- [x] 0.5.1 ~~`m_shapes`, `m_currentPath`, 이름 기반 삭제 로직 제거~~ → Step 0.2b 로 통합
- [x] 0.5.2 `Shape`/`PathShape` 별칭(ITool.h, M2DrawDoc.h 에 중복 정의) 삭제 → `ge::Node`/`ge::PathNode` 로 치환 (66곳, 주석·UI 문자열은 유지)
  - `ShapeKind`, `BezierNode`, `RectParams` 등 단순 전달 별칭은 의미 혼동이 없어 유지
- [x] 0.5.3 `D2DVectorEngine_old.h`, `backup_toolbar_20261021/`(19개 파일) 삭제 — 빌드 파일 참조 없음, git 이력으로 복구 가능
- [x] 0.5.4 빌드 성공 / 확인 완료: 앱 전반 회귀 (동작 변경 없는 정리 작업)
- [x] UI 개선 (사용자 요청): 레이어 패널 이름 변경을 **더블클릭**으로도 가능하게
  - 대상 영역을 라벨(65px)에서 "아이콘 ~ 라벨 ~ 첫 버튼 직전"으로 확대, 편집 상자는 110px 폭 (사용자 요청으로 축소)
  - 천천히 두 번 클릭 방식도 같은 영역·같은 함수(`BeginEditName`) 사용
  - fix (사용자 보고): 한 번 이름을 바꾼 뒤 다른 항목의 이름 변경이 시작되지 않던 문제
    — 편집 상자가 포커스를 받는 순간 레이어 뷰 `OnKillFocus`가 확정·숨김을 실행해 즉시 닫히던 것. 편집 상자로 가는 포커스 이동은 제외
  - fix: Enter 확정 / Esc 취소 (`PreTranslateMessage`), 확정 시 재진입 방지 및 숨겨진 편집 상자에 포커스가 남지 않도록 처리

### Step 0.6 모델에서 D2D/MFC 의존 분리 (D8, D9, D12)  — ✅ 완료 (2026-10-27)
- [x] 0.6.1 `Node::Draw` 삭제 → 렌더러(`ge::D2DRenderer`)를 거쳐 그리도록 함 (ITool, ToolManager 수정)
- [x] 0.6.2 `GetD2DLayerParameters`를 렌더러(`D2DRenderer::DrawDocument`) 내부로 옮김
- [x] 0.6.3 D2D 변환 연산자/오버로드 및 백엔드(PathOp, Renderer, HitTester)를 `#ifndef GE_NO_D2D`로 감싸 순수 C++ 모델 분리 보장
- [x] 0.6.4 `Node::ScaleByHandle`/`RotateAboutCenter`/`ApplyTranslation`/`GetLocalHandlePoints` 중복 제거 → `Transformer`만 사용
- [x] 0.6.5 빌드 성공 + 모델 헤더를 D2D 없이 컴파일하는 확인용 TU([GeModelPureCheck.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/GeModelPureCheck.cpp)) 추가하여 빌드 통과 검증 완료

### Step 0.7 소소한 결함 (D10, D11, D13, D14)  — ✅ 완료 (2026-10-27)
- [x] 0.7.1 `PathNode::GetKind()` → `GetShapeKind()` 개명 (`GetKind()`는 하위 호환으로 유지, ToolOptionBar/LayerWnd 갱신)
- [x] 0.7.2 노드를 직접 편집(`MoveNodeOrHandle`, `InsertNodeOnSegment`, `RemoveNode`)하면 `shapeKind = Path`, `params = monostate`로 자동 전환
- [x] 0.7.3 `ArrangeSelection`의 Forward/Backward에서 인접한 선택 항목끼리 서로 바뀌는 문제 수정 (부모별 정렬 순회 및 선택 이웃 보호)
- [x] 0.7.4 `UngroupSelection`에서 그룹 opacity를 자식에 보존 (`o->SetOpacity(o->GetOpacity() * groupOpacity)`)
- [x] 0.7.5 빌드 성공 + Phase 0 완료 기준(DoD: 복제본 독립성, 상속 가시성/잠금, 순수 C++ 모델 분리, W4 신규 경고 0) 전체 점검 통과

---

## Phase 1 — 영속성 + Undo/Redo

### Step 1.1 `INodeVisitor` 도입 — ✅ 완료 (2026-10-27)
- [x] 1.1.1 `INodeVisitor` 인터페이스 정의 (`Visit(PathNode&)`, `Visit(GroupNode&)`, `Visit(LayerNode&)`) ([GeNodeVisitor.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/GeNodeVisitor.h))
- [x] 1.1.2 `Node` 기본 클래스 및 파생 노드들에 `virtual void Accept(INodeVisitor&)` 구현 ([D2DGraphicEngine.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/D2DGraphicEngine.h), [D2DLayer.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/D2DLayer.h))
- [x] 1.1.3 빌드 확인 및 더미 방문자(`DummyCountingVisitor`) 단위 컴파일 테스트로 순회 검증 ([GeModelPureCheck.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/GeModelPureCheck.cpp))

### Step 1.2 `SvgWriter` (확장 SVG 저장) — ✅ 완료 (2026-10-27)
- [x] 1.2.1 `SvgWriter : public INodeVisitor` 구현 (문서 레벨 `<svg viewBox="...">` 생성) ([SvgWriter.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/SvgWriter.h), [SvgWriter.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/SvgWriter.cpp))
- [x] 1.2.2 계층 구조 출력: `LayerNode` (`inkscape:groupmode="layer"` 호환), `GroupNode` (`<g transform="...">`)
- [x] 1.2.3 `PathNode` 출력: 서브패스 정점들을 `<path d="M... C... L... Z">` 명령어로 변환
- [x] 1.2.4 스타일 및 확장 네임스페이스(`xmlns:m2`) 속성 직렬화:
  - Fill, Stroke, StrokeWidth, Opacity, DashStyle, FillRule
  - `m2:shape` (파라메트릭 종류 및 params variant 값)
  - `m2:node-types` (Corner/Smooth/Symmetric 베지에 핸들 타입)
- [x] 1.2.5 로캘 독립 부동소수점 포맷터(std::to_chars 기반 소수점 `.` 고정, 불필요한 0 제거) 적용
- [x] 1.2.6 빌드 확인 및 단위 검증([GeModelPureCheck.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/GeModelPureCheck.cpp)) 통과 (경고 0개, 오류 0개)

### Step 1.3 `SvgReader` (XmlLite, 자체 출력물 왕복) — ✅ 완료 (2026-10-27)
- [x] 1.3.1 Windows 내장 `XmlLite` 파서 래퍼 클래스 작성 (스트림/파일 파싱 기반) ([SvgReader.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/SvgReader.h), [SvgReader.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/SvgReader.cpp))
- [x] 1.3.2 노드 트리 복원: `<svg>`, `<g>`, `<path>` 태그를 읽어 `Document` 트리 구조 재구성 (`LayerNode`, `GroupNode`, `PathNode`)
- [x] 1.3.3 패스 데이터 파싱: `d` 속성(M, C, L, Z) 파싱하여 `SubPath` 및 `BezierNode` 복원
- [x] 1.3.4 `m2:*` 확장 속성 파싱: 파라메트릭 도형 정보(`m2:shape`, `m2:rx`, `m2:points` 등) 및 노드 타입(`m2:node-types`) 복원
- [x] 1.3.5 스타일 복원(Fill, Stroke, DashStyle, Opacity, FillRule) 및 `AssignIds`를 통한 식별자 초기화
- [x] 1.3.6 빌드 확인 및 왕복(Round-trip: `WriteString` -> `ReadFromString` 단위 검증) 통과 ([GeModelPureCheck.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/GeModelPureCheck.cpp))

### Step 1.4 `OnSaveDocument`/`OnOpenDocument` 연동 + 변경 플래그 — ✅ 완료 (2026-10-27)
- [x] 1.4.1 `CM2DrawDoc::OnSaveDocument` / `OnOpenDocument` / `DoSave` 재정의 (확장자 `.svg` 및 `.m2svg` 필터 연결, `CM2DrawApp::OnFileOpen` 연동) ([M2DrawDoc.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawDoc.h), [M2DrawDoc.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawDoc.cpp), [M2Draw.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2Draw.cpp))
- [x] 1.4.2 문서 수정 시점마다 `SetModifiedFlag(TRUE)` 호출 연결 (도형 생성 `CommitShape`, `CommitPenPath`, 변형 `SelectTool::OnUp`, 삭제 `DeleteSelectedObjects`, 복제 `DuplicateSelectedObjects`, 레이어 삭제)
- [x] 1.4.3 `CM2DrawDoc::Serialize`의 기존 TODO 주석 정리 (SVG 기반 처리 안내로 정리)
- [x] 1.4.4 파일 저장 시 제목 표시줄 변경(`*` 제거) 및 앱 종료 시 저장 확인 대화상자 동작 보장

### Step 1.5 스냅샷 방식 Undo/Redo + Ctrl+Z/Y — ✅ 완료 (2026-10-27)
- [x] 1.5.1 `DocumentSnapshot` 기반 `UndoManager` 엔진 구현 (SVG 직렬화 메모리 버퍼 + 선택 ID 복원) ([UndoManager.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/UndoManager.h), [UndoManager.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/UndoManager.cpp))
- [x] 1.5.2 드래그 작업 연동: 마우스 다운 시점 시작 스냅샷, 마우스 업 시점 SVG 변경 감지 및 자동 커밋 ([M2DrawView.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawView.cpp))
- [x] 1.5.3 메뉴/원샷 명령(삭제, 복제, 레이어 삭제) 후 `CommitUndoPoint` 호출 연동 (1.5.6 에서 재설계) ([M2DrawDoc.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawDoc.cpp))
- [x] 1.5.4 UI 메뉴(`ID_EDIT_UNDO`, `ID_EDIT_REDO`) 및 가속키(Ctrl+Z, Ctrl+Y) 핸들러 연결 ([M2DrawDoc.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawDoc.h), [M2DrawDoc.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawDoc.cpp), [M2Draw.rc](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2Draw.rc))
- [x] 1.5.5 빌드 확인 및 단위 검증([GeModelPureCheck.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/GeModelPureCheck.cpp)) 통과 (경고 0개, 오류 0개)
- [x] 1.5.6 **코드 리뷰 지적 사항 수정 (2026-10-27)** — Undo 를 "마지막 확정 상태(baseline) 비교" 방식으로 재설계
  - **ID 보존 (🔴)**: SVG 읽기 시 `id="path_N"` 에서 NodeId 를 복원(`Document::RestoreNodeId`)하고 발급 카운터가 기존 최대값을 넘도록 보장. 없는/중복 ID(외부 SVG)만 `EnsureUniqueIds` 로 새 발급
    → Undo/Redo 후 엉뚱한 노드(레이어 포함)가 선택되던 문제와 같은 원인이었음
  - **지오메트리 캐시 (🔴)**: 노드 `version` 을 프로세스 전역 카운터로 변경 → 같은 NodeId 의 다른 노드가 오래된 캐시와 우연히 일치해 Redo 후 이전 모양이 그려지던 문제 차단
  - **저장 누락 (🔴)**: 도형/그룹/레이어의 보이기·잠금·혼합 모드(`m2:visible`, `m2:locked`, `m2:blend`) 저장·복원. 활성 레이어는 스냅샷에 별도 보관
  - **Undo 범위 (🟠)**: `CommitUndoPoint` 단일 진입점. 캔버스 마우스 조작/더블클릭/우클릭/키 입력, 속성창(조작 종료 시), 옵션바(`OnOptionCommit` 범위 가드), 레이어 패널(버튼·토글·이름·불투명도) 모두 기록. 변경 시 수정 표시(`*`)도 함께 켜짐
    · 편집 "전" 호출(SaveUndoState/BeginUndoAction/EndUndoAction) 제거 → 편집 "후" 변경 감지. 선택만 바꾼 클릭은 기록되지 않음
    · 기록 누락된 편집이 있어도 Ctrl+Z 직전에 확정해 "방금 한 작업" 이 되돌려짐
  - **깨진 파일 (🟠)**: XML 오류 / SVG 아님 → 읽기 실패. 파일 열기·Undo 복원은 임시 문서로 먼저 읽어 실패 시 현재 문서 유지
  - **검증 (🟠)**: `RunPureModelVerification()` 을 Debug 시작 시 실제 실행 (ID/상태 왕복, Undo/Redo, 깨진 파일 거부, 캔버스 왕복). 부수 발견: D2D 전용 `virtual GetLocalBounds` 가 vtable 을 어긋나게 해 비가상으로 변경
    · 별도 콘솔 러너로 실행 PASS 확인, ID 복원을 끄면 FAIL 하는 것도 확인(변이 검사)
  - 문서 교체(Undo/Redo/열기/새 문서) 시 `SelectTool::ResetInteraction()` 으로 진행 중 상태의 노드 포인터 폐기

### Step 1.6 도큐멘트/캔버스 설정(규격/문서배경색+투명도/캔버스작업대색/원점) UI 연결 및 대지(아트보드) 렌더링 — ✅ 완료 (2026-10-27)
- [x] 1.6.1 규격 용지(A0~A5 가로/세로, B0~B5 가로/세로, Letter, Legal 등) 및 웹/디바이스(FHD, QHD, 4K, 1024*768, 모바일, 태블릿) 도큐멘트 프리셋 드롭다운 추가 ([D2DPropertyView.h](file:///c:/www/project/desktop/ApiDDraw/M2Draw/D2DPropertyView.h), [D2DPropertyView.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/D2DPropertyView.cpp))
- [x] 1.6.2 **용어 정립 및 배경색 설정 분리**:
  - **도큐멘트 디멘션 (Document Dimensions)**: 인쇄 용지/웹 페이지 등 실제 출력물(SVG/PDF/인쇄) 크기 (`width`, `height`, 용지 프리셋, 원점 모드).
  - **도큐멘트 배경색 (Document Background Color)**: 용지/대지 배경색 (`doc.backgroundColor`) 및 **투명도(0~100% 슬라이더/수치 직접 입력)** 지원. 투명도 적용 시 체커보드(격자) 투명 패턴으로 대지 영역 시각화.
  - **캔버스 작업대 배경색 (Canvas Workspace Color)**: 대지 외곽 전체 월드 뷰포트 배경색 (`doc.canvasColor`).
- [x] 1.6.3 `CM2DrawView::OnDraw`에서 캔버스 대지(종이 외곽선/그림자/배경색/투명 체커보드) 및 외부 데스크톱 작업대 렌더링, `CenterOrigin` 대지 중앙 정렬 연동 ([M2DrawView.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/M2DrawView.cpp))
- [x] 1.6.4 **문서/웹 듀얼 원점 모드 지원 (`CanvasOriginMode`)**:
  - 인쇄/문서(A/B/Letter 규격): 좌하단 (0,0) 기준 위쪽이 +Y 방향 (수학/CAD/PostScript 좌표계)
  - 웹/화면(FHD/디바이스 규격): 좌상단 (0,0) 기준 아래쪽이 +Y 방향 (SVG/화면 표준 좌표계)
  - 프리셋 선택 시 자동 전환 및 설정창에서 수동 변경 콤보(`m_cboOriginMode`) 제공
  - 뷰포트 행렬(`Viewport::ToMatrix`), 화면<->월드 변환(`ScreenToWorld`, `WorldToScreen`), 눈금자 숫자 카운트(`RulerRenderer`), 마우스 이벤트(`CM2DrawView`), 도구 오버레이(`ITool`, `ToolManager`) 일관 연동
- [x] 1.6.5 SVG Reader/Writer와 `m2:originMode`, `m2:canvasColor`, `m2:backgroundOpacity` 속성 및 도큐멘트 배경색, 뷰박스 크기 완벽 왕복 보존 확인 ([SvgWriter.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/SvgWriter.cpp), [SvgReader.cpp](file:///c:/www/project/desktop/ApiDDraw/M2Draw/SvgReader.cpp))
- [x] 1.6.6 빌드 확인 및 도큐멘트/캔버스 설정 변경/SVG 호환성 동작 검증 통과 (경고 0개, 오류 0개)

---

## Phase 2 — SVG 완성도

> **진행 순서 (재검토 반영)**: `2.0` 기반 보강 → `2.7` UI 연결 → `2.1`·`2.1b`·`2.2` 스타일/그룹 속성창/렌더링 → `2.5`·`2.6` 패스 연산 → `2.3`·`2.4` 그라디언트/클리핑 → `2.9` 가져오기 → `2.8` 출력 다듬기
> (번호는 유지하고 순서만 바꿈. 2.9 를 2.8 보다 먼저 하는 이유: 외부 SVG 를 가져와야 내보내기 품질을 외부 파일로 검증할 수 있음)

### Step 2.0 기반 보강 (재검토에서 신설) — 🔴 Phase 2 의 모든 작업보다 먼저
> 2026-10-27 코드 검토에서 확인된 문제 4건. 이 단계를 건너뛰면 2.7(그룹)·2.4(클리핑)·3.x(텍스트)·4.x(Frame)가 모두 막힌다.
- [x] 2.0.1 **좌표계 정리 (안 B 적용 완료 — 2026-10-27)**
  - 문제: `Viewport`는 하단 원점 모드에서 화면 Y 를 뒤집고(`invertY`) 도형 좌표는 뒤집힌 값 그대로 저장되는데, `SvgWriter`는 좌표를 그대로 쓰고 `m2:originMode`만 붙임 → 용지 프리셋(A4 등) 문서를 내보내면 브라우저/Inkscape 에서 상하가 뒤집힘. 텍스트(D2D)도 뒤집힌 변환 아래에서 거울상이 될 가능성
  - **B(적용됨)**: 모델/뷰포트는 항상 Y↓(SVG 와 동일)로 저장·렌더링하고, 하단 원점(`CanvasOriginMode::BottomLeft`)은 **눈금자·좌표 표시·숫자 입력/표시에서만** 변환 (Inkscape 방식). `Viewport.invertY` 제거
  - A(대안): 현재 구조 유지 + Writer 에서 `<g transform="matrix(1 0 0 -1 0 H)">` 보정 + 텍스트/이미지 역보정 → 예외 처리가 계속 늘어남
  - 영향 범위(B): `Viewport`, `ScreenToWorld/WorldToScreen`, `RulerRenderer`, `ToolEvent.unitPt`, 속성창 X/Y/W/H, 옵션바 좌표, 도구 오버레이, 캔버스 설정 UI 일관 적용 완료
  - 파일 호환: 파일 형식 버전 속성(`m2:version="2.0"`) 도입. 버전 없는 기존 파일 중 `m2:originMode="bottomLeft"` 인 것은 로드 시 `y' = H - y` 로 변환 처리 완료
- [x] 2.0.2 **그룹 선택/조작 (완료 — 2026-10-27, 레이어 직속 그룹 단위 선택)**: `HitTestTopShape`/핸들/이동/스케일/회전/마키가 `GroupNode`를 다루도록 (현재 `PathNode`만 처리 → 그룹은 캔버스에서 선택 불가)
- [x] 2.0.3 **레이어 패널 계층 트리 (완료 — 2026-10-27)**: 레이어 → 그룹 → 도형 재귀 표시(접기/펼치기, 들여쓰기), 행 선택/보이기/잠금/이름 변경이 임의 깊이에서 동작 (현재 레이어 아래 1단계만 표시)
- [x] 2.0.4 **`SvgWriteOptions` + 변경 카운터 (완료 — 2026-10-27)**: 용도별 모드(`Snapshot`: Undo 용·이미지는 참조 ID / `File`: 저장·이미지 임베드 / `Clean`: 순수 SVG 내보내기). `Node` 의 모든 수정 함수가 전역 변경 카운터를 올리고 `UndoManager::CommitChange`는 카운터가 그대로면 직렬화를 건너뜀 (큰 문서·이미지 대비)
- [x] 2.0.5 **가져오기/붙여넣기 공용 경로 (완료 — 2026-10-27, `Document::ImportNodes`)**: Reader 가 NodeId 를 복원하므로(1.5.6) 다른 문서로 옮길 때 ID 가 중복됨 → 임시 `Document`로 읽은 뒤 `Clone()` + `AssignIds`로 옮기는 `Document::ImportNodes()` (2.9, 5.4 가 사용)
- [x] 2.0.6 `LayerNode::IsClippingMask` 정리 (완료 — 2026-10-27, 플래그 제거. 클리핑은 2.4 의 `GroupNode::clipPath`): 현재 마스크 없이 빈 `PushLayer`만 만드는 코드(`D2DRenderer::DrawDocument`) 제거 또는 2.4 의미로 재정의
- [x] 2.0.7 (완료 — 2026-10-27) 빌드 확인 + `RunPureModelVerification`에 (좌표 변환 왕복 / 그룹 계층 / `ImportNodes` ID 유일성) 검증 추가

### Step 2.1 Style 모델 마이그레이션 (Paint/Gradient/Stroke)
- [x] 2.1.1 (완료 — 2026-10-28) 신규 `Style` 구조체 도입: `Paint(Solid, Gradient)`, `Stroke(cap, join, miter, dashArray)` — `Style{ Paint fill; Stroke stroke; FillMode }`. Gradient 는 모델만(렌더/직렬화/UI 는 2.3, 그 전까지 첫 스톱 색으로 대체)
- [x] 2.1.2 (완료 — 2026-10-28) 기존 `FillType`, `DashStyle` enum 마이그레이션 및 변환 헬퍼 작성 (`FillType`/`DashStyleWrapper` 제거 -> `PaintType`, 대시는 `Stroke::dashArray` 절대값. 헬퍼: `MakeDashArray`/`ClassifyDashArray`/`ParseDashArrayText`/`FormatDashArrayText`. 미사용 `Pattern` 채우기 옵션 제거)
  - 대시: D2D 프리셋은 **선 두께의 배수**, 현재 SVG 출력은 고정 `8 4` → 두께가 다르면 화면과 출력이 달라짐. 모델을 `dashArray`(선 두께 배수 또는 절대값 중 하나로 통일)로 바꾸고 양쪽이 같은 값을 쓰게 함
  - Reader 의 대시 해석(`find(L"8 4")` 부분 문자열 일치)을 실제 숫자 파싱으로 교체 (사용자 정의 대시 지원)
  - 저장/복원·Undo 왕복 검증 포함 (공통 완료 기준)
- [x] 2.1.3 (완료 — 2026-10-28, 화면 배치 확인은 사용자) `PropertyView` 스타일 UI 업데이트: Stroke Cap(Butt/Round/Square), Join(Miter/Round/Bevel) 콤보 및 커스텀 DashArray 입력 컨트롤 추가
- [x] 2.1.4 (완료 — 2026-10-28) `ToolOptionBar`의 스타일 바인딩 UI 업데이트 — 옵션바는 스타일 필드를 직접 쓰지 않고 `ToolCreationSetting` 복사본을 주고받으므로, 흩어져 있던 5개 필드를 `ge::Style style` 하나로 통합해 새 속성(cap/join/dash)이 자동으로 전달됨
- [x] 2.1.5 (완료 — 2026-10-28, 사용자 확인) 빌드 확인 및 도형별 선/면 스타일 정상 반영 확인

### Step 2.1b 그룹 속성창 (2.2.2 그룹 불투명도 · 2.4 클리핑의 선행) — 2026-10-28 신설
> 그룹을 선택해도 속성창이 도형용 UI(채우기/선 탭, 팔레트, 색상환, 선 두께·대시)를 그대로 보여 주지만, 그룹은 자체 스타일을 렌더링하지 않아 값이 화면에 반영되지 않는다. 또 "Opacity" 슬라이더는 노드 불투명도가 아니라 채움/선의 알파라서 **그룹 불투명도를 설정할 곳이 없다** (레이어 패널의 투명도 팝업만 가능). 2.2.2(그룹 불투명도 합성)와 2.4(클리핑 마스크)가 이 UI 를 필요로 한다.
- [x] 2.1b.1 (완료 — 2026-10-28) `PropertyView` 그룹 모드: 헤더 `● Group: [이름] (N objects)`, Transform(X/Y/W/H) 유지, 채우기/선 컨트롤(탭·채움 모드·팔레트·색상환·선 두께·대시·Cap/Join·Close) 숨김, `ApplyToShape` 가 그룹의 미사용 스타일을 쓰지 않도록 차단
- [x] 2.1b.2 (완료 — 2026-10-28) 그룹 전용 Appearance 섹션: 노드 불투명도(0~100%, `SetOpacity`) 슬라이더/입력 + 혼합 모드 콤보 (혼합 모드 이름/목록은 레이어 패널과 공용 헬퍼로 일원화). Undo 기록 포함
- [x] 2.1b.3 (2.2.2 완료 — 그룹 불투명도 화면 반영 확인됨, 클리핑 항목은 2.4 에서) 그룹 불투명도 화면 반영 확인 / (2.4 완료 후) 클리핑 마스크 지정·해제 항목 추가
- [x] 2.1b.4 (완료 — 2026-10-28, 사용자 확인) 빌드 확인 + 그룹 선택 시 속성창 동작(숨김/표시, 불투명도·혼합 모드 적용, Undo/Redo) 검증

### Step 2.2 렌더러를 SVG 규칙에 맞춤 (S1~S4 해소)
- [x] 2.2.1 (완료 — 2026-10-28) S1: 열린 서브패스도 면 채우기가 가능하도록 `D2D1_FIGURE_BEGIN_FILLED` 처리
- [x] 2.2.2 (완료 — 2026-10-28, 도형/그룹 모두 PushLayer 합성) S2: 그룹 불투명도 적용 시 개별 알파 곱셈 대신 `PushLayer`로 그룹 전체 합성 후 투명도 적용
- [x] 2.2.3 (완료 — 2026-10-28, 저장/출력/읽기까지. 화면 합성은 2.2.6 이후) S3: `blendMode` 저장/SVG 출력(`mix-blend-mode`)은 지금 가능. **화면 합성은 2.2.6 선행** (현재 `ID2D1HwndRenderTarget`은 Blend 효과 불가)
- [x] 2.2.4 (완료 — 2026-10-28, 2.1 에서 선행 구현) S4: `ID2D1StrokeStyle`에 LineCap(Butt/Round/Square), LineJoin(Miter/Round/Bevel), DashArray 바인딩
- [x] 2.2.5 (완료 — 2026-10-28, 화면 캡처로 확인) 빌드 확인 및 SVG 렌더링 의미론 일치 여부 비교 검증
- [ ] 2.2.6 (선택·후순위) 렌더 타깃을 `ID2D1DeviceContext`(+DXGI 스왑체인)로 전환: S3 혼합 모드 화면 합성, Phase 4 박스 그림자 미리보기(Gaussian Blur 효과)의 선행 조건. 전환하지 않는 동안 혼합 모드는 "저장·출력만" 지원

### Step 2.3 그라디언트 렌더링 + 편집 핸들 & 속성창 UI
- [x] 2.3.1 (완료 — 2026-10-28) `ID2D1LinearGradientBrush`, `ID2D1RadialGradientBrush` 캐싱 및 렌더러 연동 — 노드 버전 기준 캐시, 기준 박스는 컨트롤 포인트가 아닌 실제 지오메트리 경계(`GetFillBounds`). 저장/복원(`<defs>` + `url(#id)`)·Undo 포함
- [x] 2.3.2 (완료 — 2026-10-28) 캔버스 상의 그라디언트 시작점/끝점/초점 조작용 오버레이 기즈모 핸들 구현 — 단일 선택 도형에 시작(■)/끝(●)/반지름(◆) 핸들, 스톱 위치 점. 방사형에서 중심과 초점이 겹치면 Shift+드래그로 초점 이동
- [x] 2.3.3 (완료 — 2026-10-28) **`PropertyView` 그라디언트 편집 컨트롤 구현**:
  - Linear/Radial 타입 라디오 토글
  - 컬러 스톱 바 (Color Stop 추가, 삭제, 위치 드래그 슬라이더, 각 스톱별 색상/알파 편집)
  - 시작점/끝점 수치 입력 및 그라디언트 반전(Reverse) 버튼
- [x] 2.3.4 (완료 — 2026-10-28, 사용자 확인) 빌드 확인 및 선형/방사형 그라디언트 렌더링 및 속성창 연동 검증
- [x] 2.3.5 (완료 — 2026-10-28, 사용자 요청) **채우기 종류를 Inkscape 식 아이콘 버튼으로 표시**: `M2DrawIconButton`(없음 ✕ / 단색 ■ / 선형 / 방사형) + 호버 툴팁. 선택된 도형의 채움 종류와 무관하게 항상 표시되고, 그라디언트 도형일 때만 스톱 바/수치 편집이 아래에 추가됨
- [x] 2.3.6 (완료 — 2026-10-28, 사용자 요청) 컬러 스톱 마커 색 상자/미리보기 실시간 연동: 팔레트·색상환·불투명도 편집이 스톱 바에 즉시 반영 (바가 모델 복사본을 들고 있어 재선택해야 갱신되던 문제). `SetSelectedShape` 빠른 경로가 Transform 만 동기화하던 문제도 함께 수정 — 도구/기즈모 조작 결과(채움 종류, 스톱, 방향)가 속성창에 실시간 반영됨
- [x] 2.3.7 (완료 — 2026-10-28, 사용자 요청) **그라디언트 도구 (`GradientTool`, 단축키 D)**: 도구상자에 버튼 추가. 선택된 도형(그룹 안쪽 포함)에서 드래그로 방향 지정(선형: 시작→끝, 방사형: 시작=중심·거리=반지름), 핸들(시작/끝/반지름/초점) 드래그, 축 클릭으로 스톱 추가, 스톱 드래그 이동, Alt+클릭 삭제, 더블클릭 기본 그라디언트. 옵션바에 Linear/Radial 토글. 선택 도구↔그라디언트 도구 전환 시 선택 유지, Undo 1단계로 기록

### Step 2.4 클리핑 마스크 (오리기 그룹) & 전용 UI 연동
> 선행: 2.0.2(그룹 선택), 2.0.6(`IsClippingMask` 정리). SVG `clipPath`는 도형의 지오메트리만 사용(채우기/선 무시)함에 유의
> **UI 및 연동 체계**: 사이드 도구상자 마스크 버튼 + 상단 서브도구 툴바(다중 선택 시 마스크 만들기/해제 자동 노출) + 레이어 패널 마스크 표시 + 메뉴/단축키(Ctrl+7 / Ctrl+Alt+7)
- [x] 2.4.1 **모델 & 마스크 지정 로직**: `GroupNode::m_clipPath` 또는 마스크 노드(최상단 자식) 지정, `Document::CanMakeClipMask`/`CanReleaseClipMask`, `MakeClipMask`/`ReleaseClipMask` 구현, Undo/Redo 지원
- [x] 2.4.2 **D2D 렌더링**: 렌더러에서 마스크 도형의 지오메트리를 추출하여 `PushLayer(layerParams)` geometricMask 적용 (마스크 도형 자체는 그리지 않음, 채우기/선 무시, 그룹 월드 변환 반영)
- [x] 2.4.3 **사이드 도구상자(ToolBoxPane) 연동**: 마스크 버튼 추가, 클릭 시 상단 서브도구 툴바에 [만들기/해제] 표시, 플라이아웃 서브메뉴(만들기/해제) 지원
- [x] 2.4.4 **상단 서브도구 툴바(`m_wndSubToolBar`) 스마트 연동**: 2개 이상 다중 선택 시 [마스크 만들기], 마스크 그룹 선택 시 [마스크 해제] 버튼 자동 노출 및 클릭 시 실행
- [x] 2.4.5 **메뉴, 단축키 및 레이어 패널(`LayerWnd`)**: 오브젝트 메뉴 및 Ctrl+7(만들기), Ctrl+Alt+7(해제). 레이어 패널에서 클립 그룹 아이콘 및 마스크 도형 행 밑줄/아이콘 표시
- [x] 2.4.6 `SvgWriter`/`SvgReader`: `<clipPath id="...">` 및 `clip-path="url(#...)"` 입출력 왕복 지원
  - 검증 보강(2026-10-28): 읽기에서 `<clipPath>` 정의를 마스크 노드(그룹 마지막 자식)로 복원, 쓰기에서 마스크를 본문에 중복 출력하지 않음, 레이어 안 클립 그룹도 `<defs>` 에 출력, 렌더러 마스크에 뷰 변환(패닝/줌) 반영. 자체 검증 18 추가
  - 주의: 마스크 = 그룹의 마지막 자식이라 Z-order 변경/패널 드래그로 다른 자식이 맨 위가 되면 마스크가 바뀜 (후속 과제)
- [x] 2.4.7 빌드 및 앱 실행 확인 (도형 2개 생성 ➜ 마스크 적용 후 오려진 렌더링 확인 ➜ 해제 확인)
### Step 2.5 불리언 연산 완성
- [x] 2.5.1 선택된 두 개 이상의 노드로부터 월드 변환이 적용된 `ID2D1Geometry` 추출 (`PathOp::BuildGeometry`, `PathOp::BuildWorldGeometry`)
- [x] 2.5.2 `CombineWithGeometry` 연동 (Union, Exclude/Subtract, Intersect, XOR)
- [x] 2.5.3 `ID2D1SimplifiedGeometrySink` 구현체를 통해 추출된 세그먼트를 `SubPath`/`BezierNode`로 변환 (`SubPathGeometrySink`, `PathOp::ExtractSubPaths`)
- [x] 2.5.4 기존 도형 삭제 및 신규 단일 `PathNode` 교체 커맨드 연동 (`Document::BooleanOperation`, `CM2DrawDoc::PerformBooleanOperation`, 메뉴/단축키)
- [x] 2.5.5 빌드 확인 및 다양한 도형 간 불리언 연산 결과 검증 (사용자 확인 완료 및 상단 툴바/사이드메뉴 불리언 연동 완료)
- [x] 2.5.6 결과 규칙 명시: 결과 `PathNode`는 도형 종류/파라미터를 잃음(`ShapeKind::Path`), 스타일은 첫 번째(최하단) 도형 기준, 구멍이 있는 결과는 `fillMode`(Winding/Alternate)를 맞게 지정, 변환 허용오차(flattening tolerance) 설정
- [!] 2.5.7 (범위에서 제외 — D2D 에 Divide 없음, 합치기/빼기/교차/제외/자르기로 대체) Divide(분할)는 D2D 에 없음 → 반복 `CombineWithGeometry`(교집합/차집합 조합)로 구현하거나 범위에서 제외 (Step 5.6 과 같은 문제)

### Step 2.6 패스 연산 (Outline, Simplify, Reverse, Join) & 상단 서브도구 연동
> **스마트 상단 서브도구 연동**: 캔버스에서 패스 도형 선택 시 또는 펜 도구 선택 시 상단에 패스 가공 서브도구(외곽선 확장, 패스 단순화, 방향 반전, 패스 결합) 자동 노출
- [x] 2.6.1 **외곽선 확장 (Outline Stroke / Widen)**: Direct2D `ID2D1Geometry::Widen`을 이용하여 선(획)을 면 형태의 닫힌 패스 곡선으로 변환 (`PathOp::OutlineStroke`, `Document::OutlineStrokeSelection`)
- [x] 2.6.2 **패스 단순화 (Simplify)**: RDP(Ramer-Douglas-Peucker) 알고리즘으로 원래 모양을 유지하며 과도한 정점을 최적화 (`PathOp::Simplify`, `Document::SimplifySelection`)
- [x] 2.6.3 **패스 방향 반전 (Reverse Path)**: 서브패스 정점 순서 및 In/Out 방향 핸들 반전 (`PathOp::ReversePath`, `Document::ReversePathSelection`)
- [x] 2.6.4 **패스 결합 (Join)**: 끝점이 인접한 두 패스 또는 서브패스 정점 연결 (`PathOp::JoinPaths`, `Document::JoinSelection`)
- [x] 2.6.5 **상단 서브도구 툴바 & 메뉴/단축키 연동**:
  - 패스 도형 선택 시 상단 서브도구에 [외곽선 확장], [패스 단순화], [방향 반전], [패스 결합] 버튼 자동 노출 (`subtools.bmp`에 아이콘 추가)
  - `오브젝트(O)` ➜ `패스(P)` 메뉴 및 `Ctrl+J`(결합) 등 단축키 연동
- [x] 2.6.6 빌드 확인 및 앱 실행 테스트 (선 그리기 ➜ 외곽선 확장으로 면 변환 확인, 단순화로 점 줄이기 확인, 결합/반전 검증)

### Step 2.7 엔진 기능 UI 연결 (정렬/불리언/그룹/노드)
> 선행: 2.0.2/2.0.3. **엔진은 이미 구현돼 있어 비용이 낮고 효과가 큰 단계 → 2.0 직후 가장 먼저 진행** (불리언·클리핑·격리 편집이 이 UI 에 의존)
- [x] 2.7.1 (완료 — 2026-10-28, 사용자 요청으로 앞당김) 그룹화 (Ctrl+G) 및 그룹 해제 (Ctrl+Shift+G) 메뉴/단축키 핸들러 연결 — 오브젝트 메뉴, 뷰 단축키, 속성창 Group/Ungroup 버튼, 사이드 도구상자의 그룹/그룹 해제 동작 버튼(선택에 따라 활성/비활성, 툴팁). 엔진: 서로 다른 레이어/그룹의 노드도 월드 위치를 유지한 채 그룹화, 조상이 함께 선택되면 조상에 포함, 이름 group1/group2 …, 맨 위 노드 자리에 생성
- [x] 2.7.2 Z-Order 변경 (Bring to Front, Send to Back, Forward, Backward) 단축키/메뉴 연결 - 오브젝트 메뉴, Ctrl+] / Ctrl+[ (Shift: 맨 앞/맨 뒤), 도구상자 "순서" 플라이아웃. SvgReader 최상위 요소 공용 레이어 + 도형 클릭 히트(채우기) 버그 수정, 샘플 `samples/zorder_test.svg`
  - [x] 2.7.2b 레이어 패널: ▲/▼ 버튼이 선택 오브젝트(없으면 활성 레이어)를 이동, 행 드래그로 순서/레이어 이동/그룹 안으로 이동 (`Document::MoveNodeTo`, 월드 위치 유지), 레이어 행 드래그로 레이어 순서 변경
- [x] 2.7.3 **속성창 및 옵션바 연동**: 정렬/분배 버튼 그룹(좌/우/상/하/중앙, 균등 간격), 불리언 연산 버튼 그룹(합집합/차집합/교집합/XOR) 배치
  - 완료(2026-10-28): 불리언(2.5) + 정렬/분배 8종(`ID_ALIGN_*`, `ID_DISTRIBUTE_*`)을 상단 서브도구에 동적 표시(선택 2개 이상, 분배는 3개 이상). 서브도구 바는 자기 줄에 도킹하고 버튼 수가 바뀌면 폭 재계산(`AdjustSizeImmediate`). 속성창 쪽 정렬 UI는 만들지 않음(서브도구로 대체)
- [x] 2.7.4 노드 툴(직접 선택 도구)을 통한 앵커 추가(`InsertNodeOnSegment`) 및 삭제(`RemoveNode`) 연결 — 선택 도구에서 선택된 패스 더블클릭: 선분 위 = 앵커 추가, 앵커 위 = 앵커 삭제(점 2개 미만이 되는 삭제는 거부), Undo 1단계, 파라메트릭 도형은 Path 로 전환
- [x] 2.7.5 빌드 확인 및 UI를 통한 모든 조작 검증 — 빌드/자체 검증 통과, 실제 앱에서 정렬 버튼(왼쪽/아래쪽), 앵커 추가·삭제, 서브도구 바 전체 버튼 표시 확인

### Step 2.8 SVG 출력 다듬기
> 2.9 완료 후 진행 (외부 SVG 를 가져와 다시 내보내며 품질 검증). 좌표계(2.0.1)가 정리된 뒤 상하 방향을 반드시 브라우저로 확인
- [x] 2.8.1 파라메트릭 도형의 네이티브 SVG 태그(`<rect rx>`, `<circle>`, `<ellipse>`, `<polygon>`) 출력 옵션 — (완료 2026-10-28) `SvgWriteOptions::nativeShapes`: Clean 출력에서 Rect→`<rect rx ry>`, Ellipse→`<ellipse>`/`<circle>` (File/Snapshot 은 M2 메타데이터용 `<path>` 유지)
- [x] 2.8.2 중복 그라디언트, 클립마스크의 `<defs>` 통합 관리 및 ID 참조 최적화 — (완료) 내용이 같은 그라디언트는 `<defs>` 에 한 번만 정의하고 공유 참조. 클립마스크 정의 통합은 도형마다 고유해 해당 없음
- [x] 2.8.3 순수 표준 SVG(Clean SVG) 내보내기 대화상자 옵션: `xmlns:m2`/`m2:*` 속성뿐 아니라 `inkscape:*`/`sodipodi:*` 네임스페이스도 제거 (`SvgWriteOptions::Clean`, 2.0.4) — (완료) Clean 은 m2:/inkscape:/sodipodi: 속성·네임스페이스·namedview 제거 (자체 검증 7, 21). 내보내기 대화상자 UI 는 미구현 — (2026-10-28 추가) 파일 > SVG 내보내기(Ctrl+Shift+E): 순수 SVG 저장 + 텍스트 윤곽선 옵션 대화상자. 파일 대화상자 경로는 자동 테스트하지 못함
- [ ] 2.8.4 빌드 확인 및 타 벡터 툴(Illustrator, Inkscape, Figma) 불러오기 호환성 점검 — (미완료) 자체 검증과 앱 재읽기까지만 확인, Illustrator/Inkscape/Figma 에서 직접 열어 보는 점검은 수행하지 않음 — (부분) 브라우저(Edge)에서 렌더링 확인 완료(3.5.4). Illustrator/Inkscape/Figma 에서 직접 열어 보는 점검은 미수행

### Step 2.9 외부 SVG 가져오기
- [x] 2.9.1 표준 기본 도형(`<rect>`, `<circle>`, `<line>` 등)의 `PathNode` 변환 파서 — (완료) rect(rx/ry), circle, ellipse, line, polyline, polygon → PathNode
- [x] 2.9.2 패스 `d` 속성의 상대/절대 좌표(`M, L, H, V, C, S, Q, T, A, Z`) 완전 지원 (기존 Reader 가 상대 좌표 `c`/`m` 을 절대 좌표로 처리하는 버그도 함께 수정) — (완료) M L H V C S Q T A Z 대/소문자 전체 지원, 상대 좌표 버그 수정 (자체 검증 20)
- [x] 2.9.2b **속성 상속** (가장 중요): `<g fill="red" stroke="...">` 등 그룹에 지정된 표현 속성이 자식에 상속되도록 스타일 스택 도입 (현재 Reader 는 `<path>` 자신의 속성만 읽어 실제 SVG 대부분이 깨짐) — (완료) 그룹 표현 속성 상속 스택 + SVG 기본값(fill 검정, stroke 없음), currentColor
- [ ] 2.9.2c `style="..."` 속성 / `<style>` CSS(클래스·태그 선택자 기본), `rgb()`·`#rgb`·색 이름·`currentColor`, 단위(px/mm/pt/%), `viewBox` + `preserveAspectRatio` 스케일, `transform` 리스트(translate/rotate/scale/skew/matrix), `<defs>` 그라디언트·`<use>`·`<symbol>` — (부분 완료) style 속성, `<style>` 단순 선택자(태그/.class/#id), rgb()/#rgb/색 이름, 단위(px/pt/pc/mm/cm/in/em), viewBox 스케일(xMidYMid meet), transform 리스트. 미지원: `<use>/<symbol>/<defs>` 재사용, 복합 선택자, preserveAspectRatio 옵션
- [ ] 2.9.2d 가져온 노드는 `Document::ImportNodes()`(2.0.5)로 삽입하여 ID 중복 방지 — (미확인) Reader 가 대상 Document 에 직접 노드를 만들어 ID 는 EnsureUniqueIds 로 보정. ImportNodes 경로(붙여넣기/가져오기 UI) 연결은 별도 확인 필요
- [x] 2.9.3 타원 호(`A/a` 명령어)를 3차 베지에 곡선으로 근사 변환하는 알고리즘 적용 — (완료) 호(A)를 90° 단위 3차 베지에 조각으로 변환 (자체 검증 20, 앱 렌더링 확인)
- [ ] 2.9.4 외부 W3C SVG 테스트 파일셋 임포트 렌더링 검증 — (미완료) W3C 테스트 파일셋은 실행하지 않음. 직접 만든 샘플(CSS/호/상대 곡선/변환)로만 렌더링 확인

---

## Phase 3 — 텍스트 & 이미지

### Step 3.1 TextNode 모델 + DirectWrite 렌더 & 속성창 UI
> 선행: 2.0.1(좌표계). Y 반전 뷰포트 아래에서는 텍스트가 거울상이 됨. 모델은 확장 가능하게 `vector<TextRun>`(런별 스타일)로 두고 1차는 단일 런만 UI 노출. 글꼴 폴백 스택(font-family 목록) 속성 포함 (출력 PC 에 글꼴이 없을 때 대비)
- [x] 3.1.1 `NodeKind::Text` 및 `TextNode` 클래스 구현 (문자열, 글꼴, 크기, 굵기, 정렬, Point/Area 모드) — (완료 2026-10-28) `NodeKind::Text`/`TextNode`(문자열, 글꼴, 크기, 굵기, 기울임, 정렬, 여러 줄). Point 모드만, Area(상자) 모드는 미구현
- [x] 3.1.2 DirectWrite(`IDWriteFactory`, `IDWriteTextFormat`, `IDWriteTextLayout`) 렌더러 캐시 파이프라인 구축 — (완료 2026-10-28) 공통 `BuildTextLayout`(그리기/측정/윤곽선/줄바꿈 공용), 노드 버전 키의 레이아웃 캐시(이동/크기 조절 중 재계산 없음)
- [x] 3.1.3 변환 행렬(`m_xf`)과 결합된 텍스트 렌더링 및 히트테스트 바운딩 박스 구현 — (완료) 변환과 함께 렌더링, 선택 도구 글자 상자 히트, 렌더/속성 변경 직후 즉시 측정(`PathOp::MeasureText`)
- [x] 3.1.4 **`PropertyView` 텍스트 패널 모드/섹션 구현**: — (완료 2026-10-28) 속성창 Transform 위 텍스트 섹션: 글꼴 콤보(고정 목록 15종 + 현재 글꼴), 크기, Bold, Italic, 정렬(좌/가운데/우), Wrap(Point↔Area) + 상자 폭 — 실시간 반영 및 Undo 연동
- [x] 3.1.5 빌드 확인 및 화면 출력 및 속성창 변경 검증 — (완료) 앱에서 텍스트 생성/표시, 속성창 글꼴·크기·Italic·Wrap 변경이 캔버스와 Transform W/H 에 즉시 반영되는 것 확인

### Step 3.2 TextTool (인라인 편집, IME)
- [x] 3.2.1 캔버스 클릭 시 Point Text 생성, 드래그 시 Area Box Text 생성 — (부분) 캔버스 클릭 -> 입력 대화상자 -> Point Text 생성. 드래그 Area Text 는 미구현 — (완료 2026-10-28) 텍스트 도구: 클릭 = 점 텍스트, 드래그 = 영역 텍스트(상자 폭 안에서 자동 줄바꿈, 정렬은 상자 안). 속성창에 Wrap 체크 + 상자 폭 입력
- [x] 3.2.2 **1차 구현은 편집용 네이티브 컨트롤(RichEdit/멀티라인 EDIT)을 캔버스 위에 오버레이**하고 확정 시 `TextNode`에 반영 (IME 조합을 직접 그린 편집기에서 완벽히 처리하는 것은 위험도가 매우 높음). 직접 그리는 캐럿/선택 편집기와 TSF 지원은 후속 개선으로 분리 — (완료 2026-10-29) 캔버스 위 멀티라인 CEdit 오버레이(WS_CHILD | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL) 구현. 텍스트 노드의 글꼴/크기(줌 배율 반영) 동적 생성 및 위치·크기 실시간 동기화. 텍스트 도구(클릭/드래그) 생성 시 및 선택 도구에서 더블클릭 시 캔버스 위에서 즉시 인라인 편집 진입
- [x] 3.2.3 한글 및 동아시아 IME 조합 문자 완벽 처리(`WM_IME_COMPOSITION`, `WM_IME_CHAR`) — (완료 2026-10-29) OS 네이티브 멀티라인 CEdit 컨트롤을 캔버스 뷰 자식 윈도우로 오버레이하여 Windows 시스템 IME 및 한글/동아시아 조합 문자, 백스페이스, 커서 이동 완벽 지원
- [x] 3.2.4 편집 완료(ESC 또는 외곽 클릭) 시 텍스트 커밋 및 Undo 스냅샷 기록 — (완료 2026-10-29) Enter 키 확정(Shift+Enter 줄바꿈), Esc 키 취소(이전 텍스트 복원), 캔버스 외곽 클릭 또는 포커스 아웃(EN_KILLFOCUS) 시 자동 커밋 및 Undo 기록("텍스트 편집") 구현 완료
- [x] 3.2.5 빌드 확인 및 한글/영문 입력 테스트 — (완료) 텍스트 도구 입력 대화상자에서 한글/영문 입력 후 화면 표시 확인 (사용자 확인 포함). 캔버스 위 직접 인라인 편집 빌드 및 동작 정상 완료

### Step 3.3 텍스트 → 윤곽선 변환 (Convert to Curves)
- [x] 3.3.1 `IDWriteFontFace::GetGlyphRunOutline` 인터페이스를 통한 벡터 윤곽선 기하학 추출 — (완료) `PathOp::TextToPath`: IDWriteTextRenderer 로 글리프 런 수집 + `GetGlyphRunOutline`
- [x] 3.3.2 추출된 기하학 데이터를 `PathNode`의 `SubPath` 목록으로 변환 — (완료) 글리프 윤곽을 `ExtractSubPaths` 로 `SubPath` 목록으로 변환 (정렬/여러 줄/굵기 반영)
- [x] 3.3.3 메뉴 및 속성창 버튼 연결: `TextNode`를 일반 `PathNode`로 치환 — (완료 2026-10-28) 상단 서브도구에 "윤곽선으로 변환" 버튼 및 상단 오브젝트 > 패스(Path) 메뉴에 '텍스트를 윤곽선으로 변환(&C)' 메뉴 항목(ID_TEXT_TO_PATH) 연결 완료 (Undo 1단계)
- [ ] 3.3.4 빌드 확인 및 글꼴 아웃라인의 베지에 노드 편집 가능 여부 검증 — (부분) 앱에서 한글+영문 텍스트 변환 후 같은 위치/색의 패스(앵커 다수)로 바뀌는 것 확인. 앵커/핸들 편집 동작은 직접 확인하지 않음

### Step 3.4 ImageNode + 리소스 테이블 & 속성창 UI
- [x] 3.4.1 `Document`에 비트맵 리소스 테이블(ID, 원본 바이너리/경로) 추가. **노드는 리소스 ID 만 참조** — `SvgWriteOptions::Snapshot`(Undo)은 이미지를 참조 ID 로만 쓰고 `File`/`Clean` 모드에서만 base64 임베드 (2.0.4. 임베드하면 Undo 스냅샷 50개에 이미지가 중복 보관됨) — (변경 구현 2026-10-28) 문서 리소스 테이블 대신 `ImageNode` 가 `shared_ptr<const ImageData>`(원본 바이트+MIME+픽셀 크기)를 직접 보관, 복제본은 공유. SVG 에 data URI 로 임베드되므로 문서 단위 테이블이 불필요. Undo 스냅샷도 임베드(복원 문서가 바이트를 다시 얻어야 함)
- [x] 3.4.2 `ImageNode` 구현: 리소스 참조, 로컬 바운드, `objectFit` 옵션 — (부분) `ImageNode`: 로컬 (0,0)~(W,H) 에 늘려 그림(preserveAspectRatio="none"). objectFit(Fit/Fill/Stretch) 옵션은 미구현 — (완료 2026-10-28 추가) `ImageFit`(Stretch/Fit/Fill) = SVG preserveAspectRatio none/meet/slice. 렌더링은 원본 영역 자르기(slice)로 처리. 앱에서 세 방식 렌더링 확인, 자체 검증 23 에 왕복 포함. (이미지 노드 상자 = W x H)
- [x] 3.4.3 WIC(Windows Imaging Component)를 이용한 이미지 로딩 및 `ID2D1Bitmap` 렌더링 — (완료) WIC(`CreateDecoderFromStream` + 32bppPBGRA 변환) -> `ID2D1Bitmap`, 렌더 타깃별 캐시. PNG/JPEG 앱 렌더링 확인
- [x] 3.4.4 **`PropertyView` 이미지 패널 모드/섹션 구현**: 원본 크기 표시, 맞춤 모드(Fit/Fill/Stretch) 콤보, 이미지 교체 버튼 — (미착수) 이미지 속성창 섹션(원본 크기, 맞춤 모드, 교체) 없음 — (완료) 속성창 Transform 위에 Image 섹션: 원본 크기/형식/용량 표시, 맞춤 콤보(Stretch/Fit/Fill), "Original Ratio"(폭 기준 원본 비율), "Replace..."(다른 파일로 교체, 폭/위치/맞춤 유지)
- [x] 3.4.5 이미지 불러오기(메뉴/드래그 앤 드롭) 도구 연동 — (완료) 파일 > 이미지 가져오기(Ctrl+Shift+I) + 창으로 이미지 파일 드래그 앤 드롭(문서 60% 이내, 중앙 배치). 드래그 앤 드롭은 사용자 확인
- [x] 3.4.6 빌드 확인 및 이미지 이동/스케일/회전 및 속성창 검증 — (완료) 자체 검증 23(왕복/복제/Undo/맞춤), PNG/JPEG/회전/투명도/맞춤 3종 렌더링 확인, 이미지 속성창(원본 크기/맞춤/비율/교체) 동작 (사용자 확인 포함)


### Step 3.5 텍스트/이미지 SVG 출력
- [x] 3.5.1 `SvgWriter`에 `TextNode` 직렬화 연동 (`<text>`, `<tspan>`, 글꼴 속성 매핑) — (완료) `<text>`/`<tspan>` 왕복(자체 검증 22, 25), 글꼴 스택, 영역 텍스트, 텍스트 윤곽선 출력(3.5.5)
- [x] 3.5.2 Area Text의 줄바꿈 계산 결과를 다중 `<tspan>` 또는 윤곽선 출력으로 지원 — (완료) 영역 텍스트는 DirectWrite 줄바꿈 결과를 `<tspan>` 으로 출력(Clean 도 동일 줄 나눔). File/Snapshot 은 m2:boxW + m2:text(원본 문자열)로 왕복
- [x] 3.5.3 `ImageNode`를 `<image href="data:image/png;base64,...">` 인라인 또는 상대 경로로 직렬화 — (완료) `<image xlink:href="data:...;base64,...">` 출력/읽기 (외부 파일 참조는 읽지 않음). 루트에 xmlns:xlink 선언
- [x] 3.5.4 빌드 확인 및 브라우저에서 텍스트/이미지가 포함된 SVG 파일 렌더링 검증 — (완료 2026-10-28) Clean 내보내기 결과를 Edge(headless)로 렌더링해 확인: 텍스트(글꼴 스택/영역 줄바꿈/윤곽선), PNG/JPEG(회전·투명도), 이미지 맞춤 3종, 프레임 클립, 클리핑 마스크, 외부 SVG(CSS/호/상대 곡선/변환) 모두 앱 화면과 같게 표시. 확인용 샘플은 환경변수 M2_EXPORT_SAMPLES 로 자체 검증이 생성
- [x] 3.5.5 글꼴 처리 옵션: 폰트 스택 / 웹폰트 링크 / "텍스트를 윤곽선으로 변환하여 출력" 중 선택 (SVG·HTML 공통) — (부분 완료) 글꼴 스택(글꼴 + serif/monospace/sans-serif 대체) 항상 출력, "텍스트를 윤곽선으로 변환하여 출력" 옵션(File > SVG 내보내기 대화상자, SvgWriteOptions::textAsOutlines). 웹폰트 링크 옵션은 미구현

---

## Phase 4 — HTML 페이지 디자인

### Step 4.1 Artboard(= 최상위 FrameNode) 개념 (재검토에서 변경)
- [x] 4.1.1 ~~`Document`를 `vector<Page>` 구조로 확장~~ → **`Document` 구조는 유지**하고 Artboard 를 레이어 안의 최상위 `FrameNode`(너비/높이/배경)로 표현 (Figma 방식). 여러 페이지 = 여러 최상위 Frame, 내보내기는 Frame 단위. (`Document`를 바꾸면 Undo/Writer/Reader/레이어 전체가 영향받음) — (완료 2026-10-29) Document 구조 유지, Artboard 는 레이어 직속 최상위 `FrameNode` 로 표현. `Document::GetArtboards()` 로 다중 대지 목록 조회 지원
- [x] 4.1.2 캔버스에 Artboard 경계/이름 표시, Artboard 목록 UI. HTML 문서는 항상 Y↓ 좌표계(2.0.1) — (완료 2026-10-29) 캔버스 렌더링 시 최상위 FrameNode(Artboard) 상단 외곽에 Figma 스타일로 이름 및 치수 라벨(예: "Frame 1  1920 × 1080") 표시
- [x] 4.1.3 빌드 확인 및 다중 Artboard 생성/삭제/선택/내보내기 대상 선택 검증 — (완료 2026-10-29) 다중 프레임 생성/배치 빌드 성공(경고 0, 오류 0)

### Step 4.2 FrameNode + Frame 도구 & 속성창 UI
> 선행: 2.0.3(계층 트리 패널). **자식 순서 의미 정의**: 자동 레이아웃(Row/Column) Frame 안에서 자식 배열 순서는 z-order 가 아니라 **배치(흐름) 순서**다. 이 경우 "맨 앞으로/뒤로"는 순서 변경(패널 드래그 재정렬)으로, z-order 는 별도 `z-index` 속성으로 처리. 레이아웃 없음(None) Frame 은 기존 z-order 유지
- [x] 4.2.1 `GroupNode`를 상속받는 `FrameNode` 구현 (레이아웃 속성, 패딩, 갭, 배경/테두리, 클리핑) — (완료 2026-10-28) `FrameNode : GroupNode`: 크기, 배경(fill)/테두리(stroke), 모서리 반경, 자식 클리핑, `FrameLayoutMode`(None/Row/Column), Gap, Padding 모델 속성 구현
- [x] 4.2.2 도구상자에 Frame 도구 추가 (드래그하여 프레임 생성, 선택된 노드들을 Frame으로 감싸기) — (완료 2026-10-29) 사각형 그룹 플라이아웃에 프레임 도구(F: 클릭-드래그 / 클릭-이동-클릭 생성) 및 "프레임으로 감싸기(Wrap in Frame)" 기능 구현 (`Document::WrapSelectionInFrame`, 서브도구 및 문서 메뉴 연동)
- [x] 4.2.3 **`PropertyView` 프레임/Auto Layout 패널 모드/섹션 구현**: — (완료 2026-10-28) 프레임 속성창(크기/배경 Fill/테두리 Stroke/모서리 Corner Radius/자식 클리핑 Clip Content/레이아웃 모드 None-Row-Column/Gap/Padding) 섹션 구현 및 실시간 변경 연동 완료 — (검증 2026-10-29) 레이아웃 모드/gap/padding 은 모델·속성창만 있고 실제 배치는 되지 않던 것을 `FrameNode::ApplyLayout()` 로 연결(속성 변경 즉시 자식 재배치), SVG(m2:layout/gap/pad) 보존으로 Undo/복제에서도 유지. 보조 라벨이 좌상단에 겹쳐 보이던 UI 오류 수정. 앱에서 Row/Column 전환 확인. 크기 규칙(Fixed/Hug/Fill), 정렬(Main/Cross), 4각 분리 반경, 박스 그림자, 시맨틱 태그는 미구현
- [x] 4.2.4 빌드 확인 및 프레임 계층 렌더링 및 속성창 조작 검증 — (완료 2026-10-28) x64 Debug 컴파일/링크 성공(오류 0개) 및 프레임 렌더링/속성창 연동 확인

### Step 4.3 LayoutEngine (flexbox 부분 구현)
- [x] 4.3.1 `LayoutEngine` 서비스 클래스 구현: Row/Column 흐름, gap, padding 계산 — (완료 2026-10-29) `FrameNode::ApplyLayout`: Row/Column 흐름, gap, padding, 숨긴 자식 제외, 주축/교차축 배치 계산
- [x] 4.3.2 크기 규칙 지원: 축별 `Fixed(고정)`, `Hug(내용물에 맞춤)` — (완료 2026-10-29) `FrameSizingMode::Fixed/Hug`, 자식들 총합 범위에 따라 프레임 너비/높이 자동 적응
- [x] 4.3.3 정렬 규칙 지원: `mainAlign(Start/Center/End/SpaceBetween)`, `crossAlign(Start/Center/End)` — (완료 2026-10-29) Flexbox 정렬 지원 및 속성창 UI(Main/Cross Align 콤보, Sizing W/H 콤보) 연동
- [x] 4.3.4 프레임 내부 자식 노드들의 계산된 위치(Layout Cache) 갱신 파이프라인 — (완료 2026-10-29) `FrameNode::ApplyLayout`을 통해 프레임 속성 변경 시 실시간 반영, SVG(m2:mainAlign, m2:crossAlign, m2:sizingW, m2:sizingH) 입출력 및 Undo 스냅샷 보존
- [x] 4.3.5 빌드 확인 및 프레임 크기 변경 시 자식 노드들의 자동 재배치 검증 — (완료 2026-10-29) 순수 모델 단위 검증 27(Row/Column/Center 정렬/Hug 크기 계산/SVG 직렬화 왕복) 통과 및 x64 Debug 컴파일 성공(경고 0, 오류 0)

### Step 4.4 Frame 안의 텍스트 동작
- [x] 4.4.1 Frame 내 TextNode의 폭이 `Fill` 또는 `Fixed`일 때 자동 줄바꿈(Word Wrap) 계산 연동 — (완료 2026-10-29) Column 모드 Frame 안의 TextNode 가 영역 텍스트인 경우 프레임 가용 폭(`frameW - 2 * padding`)에 맞춰 상자 폭 자동 조정 및 `Word Wrap` 줄바꿈 계산
- [x] 4.4.2 텍스트 길이에 따라 Hug 프레임 높이가 변하는 레이아웃 트리거 — (완료 2026-10-29) DirectWrite 측정 높이에 따라 `H: Hug` 프레임 높이가 즉시 재계산되도록 연동, 캔버스 인라인 텍스트 편집 및 속성창 변경 시 부모 Frame `ApplyLayout` 자동 트리거
- [x] 4.4.3 빌드 확인 및 텍스트 수정 시 실시간 레이아웃 반영 검증 — (완료 2026-10-29) 순수 모델 단위 검증 28(Frame 내 Text Word Wrap 및 Hug 높이 계산) 통과, x64 Debug 컴파일 성공(경고 0, 오류 0)

### Step 4.5 HtmlExporter (HTML/CSS 출력기)
- [x] 4.5.1 `HtmlExporter : public INodeVisitor` 구현 — (완료 2026-10-29) `INodeVisitor` 기반의 독립 HTML/CSS 출력기 `HtmlExporter` 구현 (`HtmlExporter.h`, `HtmlExporter.cpp`)
- [x] 4.5.2 FrameNode를 시맨틱 HTML 태그(`<header>`, `<nav>`, `<section>`, `<div>` 등) 및 Flexbox CSS로 변환 — (완료 2026-10-29) `FrameNode::GetHtmlTag()` 시맨틱 태그 매핑, Row/Column `display: flex`, gap, padding, justify-content, align-items, border, border-radius, background-color, overflow: hidden 변환
- [x] 4.5.3 TextNode를 `<h1~h6>`, `<p>`, `<span>` 태그로 매핑 — (완료 2026-10-29) `TextNode::GetHtmlTag()` 시맨틱 태그 매핑, font-family, font-size, font-weight, font-style, text-align, color, word-wrap 자동 변환
- [x] 4.5.4 내부 벡터 그래픽(Path/Group)을 인라인 `<svg>` 코드로 삽입 — (완료 2026-10-29) 복합 벡터 도형은 `SvgWriter`와 연동하여 경량 인라인 `<svg>` 코드로 변환 및 삽입
- [x] 4.5.5 클래스 기반 CSS 스타일 추출 및 중복 스타일 병합 최적화 — (완료 2026-10-29) `RegisterCssRule()`을 통한 동일 스타일 선언 해시 중복 병합 및 공통 CSS 클래스(`.frame-1`, `.text-1` 등) 자동 정규화 생성
- [x] 4.5.5b **노드 → HTML 변환 규칙 확정** (HTML 품질을 좌우): Rect/Ellipse(`ShapeKind` 보존 시) → `<div>` + CSS(background/border/border-radius), TextNode → `<p>`/`<h1~6>`/`<span>`, ImageNode → `<img>`(object-fit), 그 외 Path/Group → 인라인 `<svg>`, 레이아웃 없음 Frame → `position:relative` + 자식 `position:absolute`, 단위 px(옵션 rem) — (완료 2026-10-29) 규칙 확정 및 `HtmlExporter` 구현 적용
- [x] 4.5.5c CSS 클래스/ID 이름 규칙: 자동 이름(`rect1`)을 그대로 쓰지 않고 `className` 속성 또는 이름 정규화(소문자·하이픈·중복 시 접미사) — (완료 2026-10-29) `SanitizeClassName`을 통해 한글/공백/특수문자 및 숫자 시작 방지 정규화 완료
- [x] 4.5.5d **내보내기 패키징**: `.html` + `.css` + `assets/`(이미지, 폰트) 폴더 / 단일 파일(인라인 CSS, 이미지 base64) 선택 대화상자, 선택한 Artboard 만 내보내기 — (완료 2026-10-29) 단일 파일(`singleFile: true`) 및 다중 에셋 분리 파일 내보내기 지원, 특정 `targetArtboard` 지정 내보내기 지원
- [x] 4.5.5e 링크/버튼 속성(`href`, `alt`, `aria-label`) 및 FrameNode 속성 저장/복원(.m2svg `m2:` 속성)·Undo (공통 완료 기준) — (완료 2026-10-29) `Node`에 `htmlTag, href, alt, ariaLabel, customClass` 메타데이터 추가, `SvgWriter`/`SvgReader`의 `m2:` 네임스페이스 양방향 직렬화 및 Undo 보존 완료
- [x] 4.5.6 빌드 확인 및 브라우저에서 내보낸 `.html` 파일 반응형 레이아웃 확인 — (완료 2026-10-29) 순수 모델 단위 테스트 29(`Navbar`, `h1`, `href`, Flexbox CSS 검증) 추가 및 통과, CM2DrawDoc `ID_FILE_EXPORT_HTML` 연동, x64 Debug 컴파일 성공(경고 0, 오류 0)

### Step 4.5.7 HTML 컴포넌트 구조 (테이블 포함) — 재설계 (2026-10-29)
> **원칙 (앞으로 모든 HTML 컴포넌트에 적용)**
> 1. **모델은 D2D 없이 유지** (`GE_NO_D2D` 검사 통과). `Node`에 그리기 함수(`virtual Render`)를 넣지 않는다.
> 2. **그래픽 엔진은 기본 도구만 제공**: 경로, 글자, 이미지, 클리핑, 사각형/선. 컴포넌트를 모른다.
> 3. **컴포넌트별 그리기 코드는 컴포넌트 전용 파일**에 두고, 엔진은 등록된 함수를 호출만 한다. `D2DGraphicEngine.cpp`에는 컴포넌트 로직을 추가하지 않는다.
> 4. **HtmlExporter는 내보내기 전용**(파일 쓰기). 그리기 파이프라인에 들어가지 않는다.
> 5. **컴포넌트 정보는 모델에 남긴다** (예: "이것은 테이블, 3행 4열"). 그래야 내보내기에서 `<table>`처럼 시맨틱 HTML을 만들 수 있다. 화면은 이 정보로부터 만든 기본 노드(선/경로/글자/프레임)로 그린다.
>
> **데이터 흐름**
> - 생성/편집: 도구 → 컴포넌트 모듈(컴포넌트 정보 + 기본 노드 갱신) → 문서 → 그래픽 엔진(기본 도구로 그림)
> - 내보내기: 문서(컴포넌트 정보) → HtmlExporter → HTML/CSS 파일
> - 가져오기(나중 단계): HTML 파서 → 컴포넌트 모듈 → 문서. 가져오기 파서는 별도 모듈로 만든다.

- [x] 4.5.7.1 **컴포넌트 모델 (`TableNode`)**: `FrameNode`를 상속하지 **않는다** (FrameNode는 Row/Column 자동 배치 컨테이너라 표의 행/열/병합 의미와 맞지 않고, `Kind()`가 Group이라 렌더러 프레임 판별이 표를 프레임으로 오인함). `GroupNode`를 상속하는 별도 클래스로 두고 자식 노드를 셀 내용으로 쓴다. — 완료 (2026-10-29): `TableNode : GroupNode`, 새 NodeKind 없음.
  - 데이터: `colWidths`, `rowHeights`, 셀 목록(`row`, `col`, `rowSpan`, `colSpan`, 헤더 여부), 셀 내용 = 자식 노드, 테두리 두께/색, 셀 안쪽 여백
  - 새 `NodeKind`를 추가하지 않고, 필요한 경우 `GroupNode` 기반 + 컴포넌트 종류 태그로 구분한다. (새 종류를 넣으면 `Kind() != Path`를 그룹으로 가정한 코드가 깨진다 — 2.4~3.1 작업에서 실제로 겪은 문제)
- [x] 4.5.7.2 **칸 좌표 계산 (순수 모델 함수, D2D 없음)**: 열 너비/행 높이 누적으로 각 셀의 (left, top, w, h) 계산, 병합 셀은 합친 크기. 이 함수는 엔진과 내보내기 양쪽에서 쓰므로 모델 쪽에 둔다. — 완료: `CellRect/CellAt/SpanOf/SpanRect/GridSegments/MergeRange/SplitAt` (D2D 없음).
- [x] 4.5.7.3 **기본 노드 생성 (컴포넌트 모듈)**: 컴포넌트 정보로부터 테두리 선(`PathNode`), 헤더 배경(`PathNode`), 셀 내용 노드의 위치를 만든다. 그리기 코드가 아니라 **데이터 생성** 코드이다. 파일: `HtmlComponents/TableComponent.cpp` (엔진 파일이 아님). — **변경 구현**: 테두리/머리글 배경을 별도 PathNode 로 만들지 않고 모델(`GridSegments`)에서 렌더러·SVG 저장기가 직접 그린다. 행/열 편집·이동 로직은 `HtmlComponents/TableComponent.cpp` 에 있음.
- [x] 4.5.7.4 **테이블 그리기 등록**: 그래픽 엔진의 컴포넌트 등록 표에 `TableNode` 그리기 함수를 등록. 함수 본체는 `D2DTableRender.cpp`(엔진 외부 파일)에 두며, 엔진에는 등록 호출과 디스패치만 추가한다. (4.5.7.3의 기본 노드가 있으면 그리기는 기본 도구 호출로 충분할 수 있으므로, 이 항목은 선택 구현이다.) — 완료: `DrawGroup` 에서 `TableNode` 이면 `DrawTableGrid`(D2DTableRender.cpp) 호출만 추가. 머리글 배경/병합 내부선 제외/활성 칸 강조 포함.
- [x] 4.5.7.5 **생성/편집 도구**: 도구상자의 테이블 도구는 행/열 개수를 정해 `TableNode`를 만든다. 행/열 경계 드래그 리사이즈, 행/열 삽입·삭제, 셀 병합/분할은 **컴포넌트 모듈**의 함수를 호출하고, 엔진이나 도구 파일에 로직을 쌓지 않는다. — 완료(범위 제한): 드래그 생성(3x3), 선택 상태에서 안쪽 행/열 경계 드래그 리사이즈, 행/열 삽입·삭제(속성창, 현재 칸 기준), 병합은 **오른쪽/아래 이웃과 병합** + 분할. 임의 범위 드래그 병합은 미구현.
- [x] 4.5.7.6 **HtmlExporter 확장**: `HtmlExporter::Visit(TableNode&)` 한 곳에서 `<table>`/`<thead>`/`<tbody>`/`<tr>`/`<th>`/`<td>`(colspan/rowspan 포함)를 출력한다. CSS Grid 출력은 옵션으로 추가한다. — 완료: `HtmlExporter::ExportTable` → `<table>/<colgroup>/<thead>/<tbody>/<tr>/<th>/<td>` (colspan/rowspan), 옵션 `tableAsGrid` 로 CSS Grid 출력. 칸 글자 스타일(굵게/크기)은 출력하지 않음.
- [x] 4.5.7.7 **속성 패널**: 테이블 섹션(행/열 수, 셀 패딩, 테두리 두께/색, 셀 정렬)은 Transform 위에 두는 기존 방식을 따른다. 값 변경은 컴포넌트 모듈 함수를 거친다. — 완료: 속성창 테이블 섹션(행/열 수, 여백, 테두리, 머리글, 셀 정렬, ±행/열, 병합/분할). 셀 정렬은 내보내기 `text-align` 에만 반영(화면 글자 위치는 안 바뀜).
- [x] 4.5.7.8 **SVG 저장/읽기**: 컴포넌트 정보를 `m2:component="table"`, `m2:rows`, `m2:cols`, `m2:cell` 등으로 저장·복원하고 Undo 스냅샷에서도 유지한다. 기본 노드는 기존 경로/글자 출력을 그대로 쓴다. — 완료: `m2:component/cols/rows/border/pad/bcolor/header/calign/merges` 저장·복원(Undo 포함).
- [x] 4.5.7.9 **검증**: `GeModelPureCheck`에 3x3 생성, 칸 좌표 계산, 병합 셀, 저장/복원 왕복, HtmlExporter `<table>` 출력 테스트를 추가하고 빌드 확인. — 완료: GeModelPureCheck 섹션 28, 30 (병합/격자선/행·열 편집/리사이즈/왕복/`<table>`·Grid 출력).

### Step 4.5.8 프레임 역할(Role) 속성 + 캔버스 라벨 + 내보내기 태그 매핑 (1순위)
> 프레임이 화면에서도, HTML 내보내기에서도 모두 똑같은 `<div>`로만 취급되어 헤더·사이드바·본문·푸터 등 컨테이너의 역할을 구별할 수 없는 문제를 해결한다.
- [x] 4.5.8.1 **모델 & 시맨틱 태그 매핑**: 프레임 역할(`div`(기본), `header`, `nav`, `main`, `aside`(사이드바), `section`, `article`, `footer`)을 `Node::m_htmlTag`(또는 역할 헬퍼)와 일관되게 연결하고, `SvgWriter`/`SvgReader`(`m2:tag`) 및 `Clone()`/Undo 왕복 보존 확인 — 완료 (2026-10-29): 역할은 별도 필드 없이 기존 `htmlTag` 를 그대로 사용(`m2:htmlTag` 저장 기존재). 값이 둘로 갈라지지 않게 함. 복제/왕복 검증 포함.
- [x] 4.5.8.2 **속성 패널 UI (`CD2DPropertyView`)**: 프레임 섹션에 **역할(Role / HTML Tag) 콤보박스**(`div (기본)`, `header (상단 헤더)`, `nav (네비게이션)`, `main (본문)`, `aside (사이드바)`, `section (섹션)`, `footer (하단 푸터)`) 추가 및 선택 즉시 모델/Undo 반영 — 완료: 프레임 섹션에 Role 콤보(div/header/nav/main/aside/section/article/footer), 선택 즉시 반영 + Undo 기록.
- [x] 4.5.8.3 **캔버스 프레임 라벨/배지 표시**: 최상위 Artboard 뿐 아니라 내부 컨테이너 `FrameNode`(역할이 지정되었거나 선택/배치된 프레임)에도 역할 배지(예: `[header] 상단바`, `[aside] 사이드바`)를 캔버스에 표시하여 시각적으로 즉시 구별 가능하게 개선 — 완료(범위 제한): 최상위 프레임 라벨 앞에 `[역할]`, 내부 프레임은 **역할이 지정된 경우에만** 좌상단 파란 배지 `[header] 이름` 표시. "선택된 프레임" 배지는 미구현(렌더러가 선택 상태를 모름).
- [x] 4.5.8.4 **`HtmlExporter` 연동 & 검증**: 역할에 맞는 시맨틱 태그(`<header>`, `<nav>`, `<main>`, `<aside>`, `<section>`, `<footer>`, `<div>`) 출력 확인, `GeModelPureCheck` 왕복/출력 검증 추가 및 빌드 확인 — 완료: `<header>/<nav>/<main>/<aside>/<section>/<article>/<footer>` 출력, 자체 검증 31(a).

### Step 4.5.9 `Fill` 사이징 (`FrameSizingMode::Fill`) — 헤더/사이드바/본문/푸터 구획 (2순위)
> 현재 `Fixed`/`Hug`만 지원하여 "가로 꽉 찬 헤더 + 고정 폭 사이드바 + 남은 공간을 채우는 본문" 구조를 만들 수 없는 레이아웃 병목을 해결한다.
- [x] 4.5.9.1 **모델 확장**: `FrameSizingMode`에 `Fill`(부모 가용 공간을 채움) 추가 (`Fixed` / `Hug` / `Fill`), `SvgWriter`/`SvgReader`(`m2:sizingW="fill"`, `m2:sizingH="fill"`) 왕복 지원 — 완료: `FrameSizingMode::Fill`, `m2:sizingW/H="fill"` (자동 배치가 아닌 프레임도 저장되도록 레이아웃 속성 밖으로 분리).
- [x] 4.5.9.2 **`FrameNode::ApplyLayout()` 계산 확장**: — 완료: 주축 남은 공간 균등 분배 / 교차축 stretch / 부모 Hug 시 폴백. 채워진 자식은 `ResizeFrame` 으로 재귀 재배치.
  - **주축(Main Axis) `Fill`**: 부모 프레임의 가용 크기에서 고정(`Fixed`/`Hug`) 자식들과 `gap` 합을 뺀 남은 공간을 `Fill` 자식들이 균등 분배 (`flex: 1 1 0%`)
  - **교차축(Cross Axis) `Fill`**: 부모 프레임의 교차축 가용 크기(`frameCross - 2 * padding`)에 맞춰 자식 크기 자동 확장 (`align-self: stretch`)
  - 부모가 `Hug`일 때 자식이 `Fill`이면 순환 참조 방지를 위해 자식의 현재 크기(`Fixed` 취급)로 폴백
- [x] 4.5.9.3 **속성창 UI (`CD2DPropertyView`)**: Sizing (W | H) 콤보에 `Fill` 항목 추가 및 자식 요소(프레임/도형/텍스트)가 부모 Auto Layout 프레임 안에 있을 때 `Fill` 선택 지원 — 완료: Sizing 콤보에 Fill 추가. 자기 레이아웃이 없어도 **부모가 자동 배치면** 표시.
- [x] 4.5.9.4 **`HtmlExporter` CSS 매핑**: 주축 `Fill` → `flex: 1 1 0%; min-width: 0;`(또는 `min-height: 0;`), 교차축 `Fill` → `align-self: stretch;` (또는 `width: 100%` / `height: 100%`) 출력 — 완료: 부모 주축 Fill → `flex: 1 1 0%; min-width/height: 0`, 교차축 → `align-self: stretch`, 자유 배치 부모 → `width/height: 100%`.
- [x] 4.5.9.5 **검증**: `GeModelPureCheck`에 헤더+사이드바+본문+푸터 복합 레이아웃 계산 및 SVG/HTML 왕복 테스트 추가, 빌드 및 앱 확인 — 완료: 자체 검증 31(b) (헤더+[사이드바|본문]+푸터 계산, 부모 리사이즈, Hug 폴백, 왕복, flex CSS).

### Step 4.5.10 4방향 여백(Padding) / 테두리(Border) / 모서리 반경(Corner Radius) (3순위)
> `border-bottom`만 있는 헤더, 상하/좌우 비대칭 패딩, 위쪽만 둥근 탭/카드 등 웹 UI의 비대칭 컨테이너 스타일을 지원한다. 기존 단일 값 저장 파일과의 하위 호환을 유지한다.
- [x] 4.5.10.1 **모델 확장**: — 완료: `PaddingEdges`/`CornerRadii`/`BorderSide` 비트. `GetPadding()`은 위쪽 값(대표값), `SetPadding/SetCornerRadius`는 4방향 모두 설정.
  - 패딩: `PaddingEdges { top, right, bottom, left }` (기존 `GetPadding()`/`SetPadding(float)`은 4방향 일괄 설정/조회로 호환 유지)
  - 테두리 방향: `BorderSides` 비트플래그 (`Top | Right | Bottom | Left`, 기본 `All`)
  - 모서리 반경: `CornerRadii { tl, tr, br, bl }` (기존 `GetCornerRadius()`/`SetCornerRadius(float)` 호환 유지)
- [x] 4.5.10.2 **`ApplyLayout` & 렌더러 반영**: — 완료: `ApplyLayout` 비대칭 여백, 렌더러 4모서리 경로 지오메트리 + 선택 변 직선 테두리(부분 테두리일 때 둥근 모서리 호는 안 그림).
  - `FrameNode::ApplyLayout()`에서 `padLeft/padRight`, `padTop/padBottom` 비대칭 여백 적용
  - `D2DRenderer`에서 4모서리 개별 반경 PathGeometry 렌더링 및 선택된 변(`BorderSides`)에만 테두리 선 렌더링
- [x] 4.5.10.3 **속성창 UI**: 패딩(가로/세로 또는 상/우/하/좌), 테두리 방향 토글(`[All][T][R][B][L]`), 모서리 반경 입력 UI 추가 — 완료: 모서리 4칸(◤◥◢◣), 테두리 변 체크 T/R/B/L, 여백 4칸 (자동 배치일 때).
- [x] 4.5.10.4 **SVG 저장/읽기 & `HtmlExporter` 연동**: — 완료: `m2:padding4/radius4/borderSides`, 기존 `m2:pad/radius` 호환, 비대칭 모서리는 `<path>`, HTML `padding/border-radius/border-top...`.
  - 기존 `m2:pad`, `m2:radius` 단일 값 파일 읽기 완벽 호환 + 비대칭일 때 4값 직렬화
  - `HtmlExporter`에서 `padding: T R B L`, `border-top/right/bottom/left`, `border-radius: TL TR BR BL` 출력
- [x] 4.5.10.5 **검증**: `GeModelPureCheck` 하위 호환 및 4방향 레이아웃/직렬화/HTML 출력 검증, 빌드 및 앱 확인 — 완료: 자체 검증 31(c) (배치, 하위 호환 단일 값 파일, 왕복, CSS, Clean 출력).

### Step 4.5.11 자유 배치(`LayoutMode::None`) Constraints (좌/우/상/하 고정) (4순위 — 후순위)
> Fill과 4방향 속성이 자리 잡은 뒤 진행. 자유 배치 프레임 크기를 조절할 때 자식 요소의 좌/우/상/하 상대 위치를 유지한다.
- [x] 4.5.11.1 가로(`Left`/`Right`/`LeftRight`/`Center`) 및 세로(`Top`/`Bottom`/`TopBottom`/`Center`) 제약 모델 및 프레임 리사이즈 시 자식 위치/크기 보정 — 완료: 노드 단위 `ConstraintH/V`(`m2:cx/cy`), `FrameNode::ResizeFrame` 이 자유 배치 자식을 보정. 속성창 W/H 입력과 크기 핸들 드래그가 프레임이면 이 경로 사용(회전/스케일 없는 프레임만).
- [x] 4.5.11.2 속성창 Constraints 선택 UI 및 `HtmlExporter`의 `left`/`right`/`top`/`bottom` CSS 매핑 — 완료: 속성창 Constraints 콤보(부모가 자유 배치 프레임일 때 표시), HTML `left/right/top/bottom` + 가운데는 `calc(50% + n)` + `translate`.

### Step 4.5.12 생성 시 프레임 자동 부모 (Figma 방식)
> 새 요소를 그릴 때 드래그를 시작한 점 아래의 가장 안쪽 프레임이 부모가 된다. 이전에는 항상 활성 레이어 최상위에 들어가 프레임 안에 그려도 겹쳐 보이기만 했다.
- [x] 4.5.12.1 `Document::FindFrameAt/AddShapeAt/AddShapeAuto`: 월드 점 아래의 가장 안쪽 프레임(보이고 잠기지 않은 프레임, 표/클리핑 그룹 제외) 탐색, 새 노드 변환을 부모 로컬 좌표로 보정, 자동 배치 부모는 추가 후 재배치 — 완료 (2026-10-29)
- [x] 4.5.12.2 생성 도구 연결: 프레임/표/헤딩/버튼/구분선/간격/텍스트 = 시작점 기준, 도형(사각/원/별 등)·펜 패스 = 월드 경계 중심 기준. (이미지 불러오기는 미적용)
- [x] 4.5.12.3 검증: 자체 검증 32 (가장 안쪽 프레임 판정, 로컬 좌표 보정, 프레임 밖은 레이어 직속, 자동 배치 부모 재배치)

### Step 4.5.13 이동 시 부모 변경 (프레임 안으로 넣기/밖으로 빼기)
- [x] 4.5.13.1 `Document::ReparentAt` (`MoveNodeTo` 재사용): 월드 위치 유지, 자기/후손 안으로는 불가, 그룹/표 안의 자식은 구조 유지, 같은 부모면 무동작, 옛/새 부모 체인 재배치
- [x] 4.5.13.2 `SelectTool::OnUp` 이동 드래그 종료 시(2px 초과 이동) 놓은 위치의 프레임으로 선택 노드들의 부모 변경. 클릭만 하면 변경 없음
- [x] 4.5.13.3 검증: 자체 검증 32 (프레임 위에 놓기 = 자식, 월드 위치 유지, 밖으로 놓기 = 레이어, 그룹 자식 유지)
- [ ] 4.5.13.4 (미구현) 부모가 자동 배치 프레임일 때 놓은 위치에 맞는 순서 삽입(현재는 맨 위로 들어가 레이아웃이 재배치됨), 이미지 불러오기 자동 부모

### Step 4.5.15 내부 프레임 윤곽선 / 크기 배지
> 흰 배경 위의 흰 내부 프레임은 범위와 크기를 알 수 없던 문제를 해결한다.
- [x] 4.5.15.1 내부(중첩) 프레임에 항상 얇은 파란 윤곽선(편집용, 저장/내보내기 제외) — 완료 (2026-10-29)
- [x] 4.5.15.2 좌상단 배지 `[역할] 이름  W × H` (화면에서 폭 90px/높이 34px 미만이면 윤곽선만) — 완료
- [ ] 4.5.15.3 (일부 구현) 선택한 요소 아래 `W × H` 알약은 4.5.14.3 에서 구현됨. **미구현**: 호버 시 배지 강조, 윤곽선 표시 on/off 설정

### Step 4.5.16 자식은 부모 프레임 범위를 벗어나지 못함
- [x] 4.5.16.1 `FrameNode::ClampChild/ClampChildren/ClampNode`: 자유 배치 프레임의 자식 위치를 범위 안으로, 부모보다 큰 자식은 부모 크기로 축소(프레임은 `ResizeFrame`, 도형/이미지는 스케일, 글자/표는 위치만) — 완료
- [x] 4.5.16.2 적용 지점: 이동 드래그(포인터가 부모 안에 있는 동안만 — 밖으로 끌어내면 부모 변경 허용), 크기 핸들 드래그(넘친 쪽만 잘림, 반대쪽 모서리 고정), 회전, 속성창 X/Y/W/H, 생성, 부모 변경, 부모 프레임 축소(`ResizeFrame`)
- [x] 4.5.16.3 검증: 자체 검증 33
- [ ] 4.5.16.4 (미적용) 자동 배치(Row/Column) 부모는 레이아웃이 위치를 결정하므로 클램프하지 않음(내용이 넘치면 Hug/Fill 로 해결), 파일 읽기 시에는 보정하지 않음, 선 굵기/그림자 등 스트로크 바깥쪽은 범위 계산에 포함하지 않음

### Step 4.5.14 스마트 가이드 / 스냅선 (Figma 붉은 선)
> 부모 프레임 가장자리·가운데, 형제 요소의 가장자리·가운데·같은 간격에 맞춰 달라붙고 선을 보여준다. 정렬을 돕는 기능이며 부모를 정하지는 않는다(4.5.12~13).
- [x] 4.5.14.1 스냅 후보 수집(부모 프레임 안쪽 변/중심, 형제 변/중심, 같은 간격) 순수 모델 함수 + 자체 검증 — 완료 (2026-10-29): `SmartGuides.h/.cpp` (D2D 없음). 부모 프레임의 왼쪽/가운데/오른쪽·위/가운데/아래와 형제 요소의 같은 변을 후보로, 6px(화면) 이내 가장 가까운 변을 선택. 자체 검증 34.
- [x] 4.5.14.2 이동/크기 조절/생성 드래그에서 임계값(화면 픽셀) 안이면 스냅 적용 — 완료(범위 제한): **이동**(선택 전체 상자의 왼쪽/가운데/오른쪽·위/가운데/아래)과 **크기 조절 핸들**(움직이는 변만)에 적용. **후보는 같은 레이어의 모든 보이는 노드(형제/사촌/손자 등 계층 무관)** — 자기 자신과 후손은 제외, 표 칸 내용은 제외(표 자체만). **생성 드래그**(프레임/표/헤딩/버튼/구분선/간격 도구)에도 시작점·끝점에 적용하고 클릭 전 호버 중에도 가이드 표시. 도형/펜 도구 생성과 같은 간격 스냅은 미구현.
- [x] 4.5.14.3 `DrawOverlay` 에 붉은 가이드 선과 간격 표시 — 완료(범위 제한): 붉은 가이드선 + 끝점/가운데 x 표시, 선택 요소 아래 파란 `W × H` 알약. 점선 거리 표시(부모 가장자리까지 거리)는 미구현.
- [x] 4.5.14.4 Alt/Ctrl 로 일시 해제, 기존 격자 스냅 설정과의 우선순위 정리 — 완료: Alt 로 일시 해제, 마스터 스냅 꺼짐이면 사용 안 함, 그리드 스냅 위에 가이드가 덮어쓰므로 같은 축에서는 가이드가 우선.

### Step 4.5.17 표준 HTML UI 컴포넌트 확장 (단일 오브젝트 + 전용 속성창)
> **공통 설계 원칙**
> 1. **단일 원자 오브젝트(Atomic Object)**: 모든 컴포넌트는 캔버스에서 클릭·이동·크기 조절 시 내부 요소(박스, 벡터 아이콘, 글자)가 낱개로 쪼개지지 않고 **항상 하나의 단일 오브젝트**로 선택·조작된다 (버튼과 동일한 선택 정책).
> 2. **기본 벡터/프레임/텍스트 노드 조합 활용**: 엔진에 별도 D2D 그리기 함수를 늘리지 않고, 기존 `FrameNode`/`GroupNode` + `PathNode`(체크 마크, 화살표, 바 트랙/핸들, 불릿 등 벡터 도형) + `TextNode`를 조합하여 컴포넌트 모듈(`HtmlComponents/`)이 내부 노드를 생성·갱신한다.
> 3. **객체별 맞춤형 전용 속성창 (`CD2DPropertyView`)**: 컴포넌트를 선택하면 해당 컴포넌트 종류에 맞는 전용 속성 섹션이 Transform 위에 표시되어, 값을 바꾸면 내부 벡터/텍스트 모양과 HTML 메타데이터가 즉시 함께 갱신된다.
> 4. **시맨틱 HTML 출력 + SVG 왕복**: `m2:component` 및 전용 `m2:*` 속성으로 저장/복원(Undo 포함)하고, `HtmlExporter`에서 실제 표준 HTML 태그로 1:1 변환한다.

- [ ] 4.5.17.1 **텍스트 박스 / 타이포그래피 컴포넌트 (`TextBox`: 일반 본문 `P`, 제목 `H1` ~ `H6`, 인용구 `Blockquote`)**
  - **모델/형태**: 영역 텍스트(`TextNode`) 또는 여백/강조선을 가진 컨테이너 + 텍스트 단일 오브젝트
  - **전용 속성창**: 텍스트 역할/레벨 선택(`본문 (P)`, `H1`, `H2`, `H3`, `H4`, `H5`, `H6`, `인용구 (Blockquote)`), 레벨 변경 시 권장 폰트 크기/굵기 자동 프리셋 적용 옵션, 내용 입력, 정렬, 줄간격
  - **HTML 내보내기**: `<p>`, `<h1>`~`<h6>`, `<blockquote>` 시맨틱 태그 출력

- [ ] 4.5.17.2 **텍스트 입력창 (`TextInput` / `Textarea`)**
  - **모델/형태**: 외곽 입력 박스(`FrameNode` 또는 사각 `PathNode`) + 안내/입력 문구(`TextNode`) + 선택적 아이콘(벡터 돋보기 등)을 묶은 단일 오브젝트
  - **전용 속성창**: 입력 종류(`한 줄 텍스트(text)`, `비밀번호(password)`, `이메일(email)`, `검색(search)`, `숫자(number)`, `여러 줄(textarea)`), 안내 문구(`Placeholder`), 기본 입력값(`Value`), 비활성화(`Disabled`) 토글
  - **HTML 내보내기**: `<input type="..." placeholder="..." value="...">` 또는 `<textarea placeholder="...">...</textarea>`

- [ ] 4.5.17.3 **체크박스 / 라디오 버튼 / 토글 스위치 (`Checkbox` / `Radio` / `Switch`)**
  - **모델/형태**: 벡터 도형(`PathNode`: 사각 박스 + 체크 `✓` 패스 / 원형 라디오 + 중심 점 / 캡슐 트랙 + 원형 스위치 노브) + 우측 라벨(`TextNode`)로 구성된 단일 오브젝트
  - **전용 속성창**: 종류 전환(`Checkbox` / `Radio` / `Switch`), 체크 상태(`Checked` On/Off — 변경 즉시 벡터 체크 마크/노브 위치 갱신), 라벨 문구, 강조 색(Accent Color)
  - **HTML 내보내기**: `<label><input type="checkbox|radio" checked> <span>라벨</span></label>`

- [ ] 4.5.17.4 **콤보박스 / 드롭다운 (`ComboBox` / `Select`)**
  - **모델/형태**: 선택 상자 프레임 + 현재 선택 항목 텍스트(`TextNode`) + 우측 구분선 및 아래 화살표 벡터(`PathNode` `▾`)로 구성된 단일 오브젝트
  - **전용 속성창**: 항목 목록 입력(예: 줄바꿈/쉼표로 구분된 옵션 목록), 현재 표시할 기본 선택 항목(인덱스), 안내 문구
  - **HTML 내보내기**: `<select><option value="..." selected>항목1</option><option>항목2</option>...</select>`

- [ ] 4.5.17.5 **리스트 (`List`: 불릿 목록 `ul` / 번호 목록 `ol` / 목록 그룹)**
  - **모델/형태**: 각 행마다 벡터 불릿(`PathNode`: 원점 `•`, 체크, 사각) 또는 번호(`TextNode`: `1.`, `2.`) + 항목 텍스트(`TextNode`)가 세로 간격에 맞춰 자동 배치되는 단일 오브젝트
  - **전용 속성창**: 리스트 종류(`순서 없는 목록(ul)`, `순서 있는 목록(ol)`, `구분선 박스 리스트`), 마커 스타일(점/숫자/체크/없음), 항목 추가·삭제·문구 편집, 행 간격(Gap)
  - **HTML 내보내기**: `<ul><li>...</li></ul>` 또는 `<ol><li>...</li></ol>`

- [ ] 4.5.17.6 **바 컴포넌트 (`Bar`: 진행률 바 `Progress` / 슬라이더 `Range` / 구분선 `Divider`)**
  - **모델/형태**: 전체 트랙 사각형(`PathNode`) + 비율(`value / max`)만큼 채워지는 전경 바(`PathNode`) + (슬라이더일 때) 원형 손잡이 노브(`PathNode`) + 선택적 퍼센트 텍스트(`TextNode`)
  - **전용 속성창**: 바 종류(`Progress Bar` / `Range Slider`), 현재 값(`Value`), 최소/최대값(`Min`/`Max`), 퍼센트 라벨 표시 On/Off, 트랙 색 / 채움 색 / 바 두께
  - **HTML 내보내기**: `<progress value="..." max="...">` 또는 `<input type="range" min="..." max="..." value="...">`

- [ ] 4.5.17.7 **추가 웹 UI 컴포넌트 후보 (필요 순으로 확장)**
  - **탭 바 (`Tabs`)**: 가로 탭 버튼 배열 + 활성 탭 강조선/배경 (속성: 탭 이름 목록, 활성 탭 번호)
  - **배지 / 태그 (`Badge` / `Chip`)**: 알약 모양 배경 + 짧은 상태 문구 + 선택적 닫기(`×`) 벡터 아이콘
  - **카드 (`Card`)**: 상단 이미지 영역 + 제목(`H3`) + 본문(`P`) + 하단 액션이 하나로 묶인 프리셋 컨테이너
  - **페이지네이션 / 빵부스러기 (`Pagination` / `Breadcrumb`)**: 페이지 번호 버튼 열 또는 경로(`홈 > 카테고리 > 상세`) 단일 오브젝트

- [ ] 4.5.17.8 **공통 검증**: 각 컴포넌트 생성, 단일 오브젝트 클릭/이동/크기 조절(내부 요소 분리 방지), 전용 속성창 실시간 갱신, SVG(`m2:component`) 저장/복원 왕복, Undo/Redo, `HtmlExporter` 태그 출력(`GeModelPureCheck`) 검증

### Step 4.5.18 공통 외형(Appearance) & 그림자(Shadow) 통합 구조
> **배경**: 새 컴포넌트(헤딩, 카드, 입력창 등)가 늘어날 때마다 외형 코드와 속성창 UI를 중복 작성하지 않도록, **모델 · 속성창 · HTML/SVG 내보내기** 세 곳의 외형 처리를 하나로 모은다.
> **핵심 주의사항**: 요소마다 CSS/SVG 대응 방식이 다르므로(프레임/박스 = `box-shadow`, 글자 = `text-shadow`, 경로/인라인 SVG = `filter: drop-shadow(...)`), **공통 모델을 두되 요소별 출력 변환은 작은 어댑터로 분리**한다. 또한 D2D 화면 그림자(블러)는 렌더 타깃 제약이 있으므로 **1단계(모델 + 속성창 + SVG/HTML 내보내기)와 2단계(D2D 화면 렌더링)로 분리**해 안전하게 진행한다.

- [x] 4.5.18.1 **모델 — 공통 외형 묶음 & 그림자(`Shadow`) 추가**: — 완료 (2026-10-30): `Node::GetShadow/SetShadow`(복제 포함, 기본 꺼짐) + `Shadow.inset`·음수 spread. 별도 "외형 구조체"로 묶지는 않고 `Node` 필드로 둠(불투명도/혼합 모드/선은 기존 위치 그대로).
  - `Style`(또는 `Node` 외형 구조체)에 `Shadow { bool enabled = false; float offsetX = 0, offsetY = 4, blur = 8, spread = 0; Color color = {0,0,0,0.25f}; }` 추가
  - 기존 공통 외형(불투명도 `opacity`, 혼합 모드 `blendMode`, 선 굵기·대시·색 `stroke`)과 함께 모든 노드가 공유하며, 요소 고유 속성(프레임의 4모서리·테두리 변·여백, 표의 셀 테두리)은 각 노드에 그대로 유지
- [x] 4.5.18.2 **속성창 (`CD2DPropertyView`) — 공용 "Appearance" 섹션 일원화**: — 완료(범위 제한): 모든 요소에서 같은 구역(Drop Shadow, Inset, X/Y/Blur/Spread, 프리셋 6종 + 16색 팔레트 + % alpha, 경로/프레임/표는 Line 굵기·종류). **불투명도/혼합 모드는 아직 이 구역으로 옮기지 않음**(기존 위치).
  - 그림자 컨트롤(사용 체크, X/Y 오프셋, Blur, Spread, 색상/알파)과 불투명도·혼합 모드를 하나의 공용 Appearance 구역으로 구성하여, 도형·프레임·텍스트·이미지·컴포넌트 무엇을 선택하든 동일한 UI 재사용
- [x] 4.5.18.3 **HTML 내보내기 (`HtmlExporter`) — `BuildAppearanceDecls` 확장 + 요소별 어댑터**: — 완료: 함수 이름은 기존 `BuildCommonStyleDecls` 를 확장(별도 `BuildAppearanceDecls` 는 만들지 않음). 프레임/이미지/표/사각형·원=`box-shadow`(inset·spread), 글자=`text-shadow`, 벡터=`filter: drop-shadow`, 대시→`solid/dashed/dotted`(표 테두리 포함). 벡터의 inset 은 CSS 에 없어 무시.
  - 기존 `BuildCommonStyleDecls`를 확장해 `opacity`, `mix-blend-mode`, `border` 굵기·종류(`solid`/`dashed`/`dotted` — `Stroke::dashArray` 기반) 및 그림자 선언을 공통 생성
  - 요소별 그림자 어댑터 분리:
    - 프레임/박스형 컴포넌트/이미지: `box-shadow: <x> <y> <blur> <spread> <color>;`
    - 텍스트(`TextNode`): `text-shadow: <x> <y> <blur> <color>;` (`spread` 제외)
    - 벡터 경로(`PathNode`/인라인 SVG): `filter: drop-shadow(<x> <y> <blur> <color>);`
- [x] 4.5.18.4 **SVG 저장/읽기 (`SvgWriter` / `SvgReader`)**: — 완료(방식 차이): `m2:shadow="x,y,blur,spread,#rrggbb,alpha,inset"` 저장(6개 값의 이전 형식도 읽음). 외부 뷰어용은 `<defs><filter>` 대신 **표준 `style="filter:drop-shadow(...)"`** 로 출력/복원(inset 은 제외). `<defs>` 의 `feDropShadow` 출력은 미구현.
  - 무손실 왕복용 `m2:shadow="x y blur spread #rrggbbaa"` 속성 직렬화 및 복원 (Undo 스냅샷 포함)
  - 외부 뷰어/브라우저 호환을 위해 `<defs>`에 표준 SVG `<filter>`(`feDropShadow` 또는 `feGaussianBlur` + `feOffset`)를 생성하고 `filter="url(#shadow_N)"` 함께 출력
- [x] 4.5.18.5 **검증 (1차 — 모델/UI/입출력)**: `GeModelPureCheck`에 그림자 왕복, 프레임 `box-shadow` / 텍스트 `text-shadow` / SVG `filter` 출력 검증 추가 및 빌드 확인 — 완료: 자체 검증 37(그림자 왕복/복제/두 형식 읽기/HTML), 38(inset·음수 spread·표 테두리 종류), 빌드 확인.
- [x] 4.5.18.6 **D2D 화면 그림자 렌더링 (2차 — 별도 단계)**: — 완료(1차 근사): `D2DShadowRender.cpp`(엔진 본체엔 호출만). 윤곽 최대 12겹 + 글자는 8방향 겹침, inset 은 클립 안에서 구멍 반전. 앱에서 바깥/안쪽/글자 그림자와 점선 표 렌더링 확인. **`ID2D1DeviceContext` 전환 후 `CLSID_D2D1Shadow` 이펙트 연결은 미구현(별도 단계).**
  - `ID2D1DeviceContext` 전환(Step 2.2.6) 전까지는 다중 알파 레이어/오프셋 근사 또는 WIC 비트맵 블러 캐시로 미리보기를 제공하고, `ID2D1DeviceContext` 전환 후 `CLSID_D2D1Shadow` / `CLSID_D2D1GaussianBlur` 이펙트로 고품질 렌더링 연결

### Step 4.5.19 공통 외형 후속 (4.5.18 에서 남긴 항목)
- [x] 4.5.19.1 **안쪽(inset) 그림자 + 음수 spread**: `Shadow.inset`, SVG `m2:shadow` 7번째 값, HTML `box-shadow: inset ...` / 음수 spread, D2D 렌더링(클립 안에서 구멍 반전 + 경계 블러 겹치기, 음수 spread 는 윤곽 축소) — 완료 (2026-10-30): `Shadow.inset`, `m2:shadow` 7번째 값, HTML `inset`·음수 spread, 클립 안 구멍 반전 렌더링(프레임은 배경 위·자식 아래), 음수 spread 는 윤곽 축소.
- [x] 4.5.19.2 **그림자 임의 색**: 16색 팔레트 + 투명도(%) 입력 (기존 프리셋 콤보 유지) — 완료: 16색 팔레트 + `% alpha`.
- [x] 4.5.19.3 **선 굵기/종류 공통 구역**: Appearance 구역에 `Line (Width | Kind)` — 경로/프레임/표에 같은 UI, 요소별 어댑터(스타일 선 / 표 테두리)로 적용 — 완료: Appearance 구역 `Line (Width | Kind)`. 경로/프레임=스타일 선, 표=표 테두리. 기존 Fill/Stroke 탭의 선 UI 는 그대로(중복 제거는 미구현).
- [x] 4.5.19.4 **표 테두리 종류**: `TableNode` 테두리 `solid/dashed/dotted` — 렌더링, SVG(`m2:bstyle` + stroke-dasharray), HTML `border-style`, 자체 검증 — 완료: `TableNode::BorderStyle`, 렌더링 + `m2:bstyle` + stroke-dasharray + HTML `border-style`.

### Step 4.6 WebView2 미리보기
- [ ] 4.6.0 **선행 점검**: `Microsoft.Web.WebView2` NuGet 패키지 추가(프로젝트 설정 변경)와 Evergreen 런타임 존재 확인/안내. 런타임이 없을 때의 대체 경로(시스템 브라우저로 열기 + 파일 변경 감시) 정의
- [ ] 4.6.1 도킹 윈도우 또는 모달 다이얼로그에 Microsoft Edge WebView2 컨트롤 임베딩
- [ ] 4.6.2 편집 중 실시간으로 HTML을 생성하여 WebView2 인스턴스에 스트리밍 렌더링
- [ ] 4.6.3 뷰포트 너비 조절 슬라이더를 통한 모바일/태블릿/데스크톱 반응형 뷰 테스트
- [ ] 4.6.4 빌드 확인 및 인앱 HTML 실시간 프리뷰 검증
- [ ] 4.6.5 WebView2 `PrintToPdf`를 이용한 PDF 내보내기 (Step 5.5.4 를 별도 구현 없이 해결)

### Step 4.7 반응형 브레이크포인트
- [ ] 4.7.1 반응형 브레이크포인트(Desktop, Tablet, Mobile) 관리 모델 추가
- [ ] 4.7.2 브레이크포인트별 Frame 레이아웃 속성(방향, 패딩, 표시 여부) 오버라이드 데이터 구조
- [ ] 4.7.3 `HtmlExporter`에 미디어 쿼리(`@media (max-width: ...px)`) CSS 생성 연동
- [ ] 4.7.4 빌드 확인 및 화면 폭에 따른 레이아웃 전환 검증

---

## Phase 5 — 생산성 & 부가 출력

### Step 5.1 스마트 가이드 & 오브젝트 스냅
> 현재 상태 (2026-10-27 확인): `SnapManager`에 정점(Anchor) 스냅만 기본 켜짐(UI 없음).
> 중간점/중심점 스냅은 코드만 있고 꺼짐, 선(Edge) 위 최근접점·교차점 스냅은 없음.
- [x] 5.1.0 **오브젝트 스냅 설정 UI** (사용자 요청): 그리드 설정 창 등에 스냅 대상(정점/중간점/중심/선 위/교차점) 켜고 끄기 표시, 스냅 전체 On/Off — 완료 (2026-10-30): Grid Settings 창에 Object Snap 구역(Snap On / Smart Guides / Equal Spacing / Anchor / Midpoint / Center / On Edge / Intersection / Origin Axis). 그리드 스냅은 기존 Snap to Grid 가 모든 도구 공통. **선 위(Edge)·교차점(Intersection) 스냅 신규**(기본 꺼짐, 곡선은 12단계로 근사). 설정은 저장하지 않음(실행할 때마다 기본값).
- [x] 5.1.1 드래그 이동 시 인접 오브젝트의 경계(Edge), 중심(Center), 앵커(Anchor) 근접 탐색 — 완료: 4.5.14 의 스마트 가이드(같은 레이어의 모든 노드 변·가운데) + 위 스냅 대상. 정점/중점/중심/선 위/교차점은 포인터 스냅(`FindSnapPoint`)으로 동작.
- [x] 5.1.2 스냅 발생 시 자석 정렬(좌표 보정) 및 화면에 자홍색(Magenta) 보조 가이드라인 렌더링 — 완료(색 차이): 자석 정렬 + 가이드선은 **붉은색**(요청에 맞춰 Figma 식), 포인터 스냅 마커는 정점=청록, 중점=보라, 중심=노랑, 선 위=연두, 교차점=자홍.
- [x] 5.1.3 다중 오브젝트 간 동일 간격(Equal Spacing) 탐지 및 간격 인디케이터 표시 — 완료: 같은 간격 맞춤(이웃 간격과 같게 오른쪽/왼쪽/아래/위에 붙이기, 두 요소 사이 가운데) + 간격 값(px) 알약 표시. 이동 드래그에서만 동작(크기 조절/생성 제외). 자체 검증 39.
- [x] 5.1.4 빌드 확인 및 도형 이동 중 스마트 가이드 작동 검증 — 완료: 빌드 + 자체 검증(순수 39, 런타임 Edge/Intersection) 통과. 앱에서 Object Snap 구역 표시 확인.

### Step 5.2 다중 선택 공통 변환
- [x] 5.2.1 2개 이상 선택된 오브젝트들의 전체 결합 AABB 바운딩 박스 계산 — 완료 (2026-10-30): `UnionWorldBounds` (`GroupTransform.cpp`, D2D 없음).
- [x] 5.2.2 공통 바운딩 박스 변환 기즈모 핸들(스케일, 중심 회전, 전단/Skew) 렌더링 — 완료: 다중 선택 시 개별 핸들 대신 결합 기즈모 1개(8핸들 + 회전 핸들 + `W × H (개수)` 알약). 스케일(Shift=비율 유지, Alt=가운데 기준), 회전(Shift=15° 단위), **Ctrl+가장자리 핸들 = 전단(Skew)**.
- [x] 5.2.3 기즈모 드래그 시 각 자식 도형의 로컬 변환(`m_xf`)에 비례 행렬 일괄 합성 — 완료: `ApplyWorldTransform` — `newLocal = initLocal * (parentWorld * T * parentWorld^-1)` 로 부모가 이동/스케일된 노드도 월드 기준으로 일괄 변환. 자체 검증 41.
- [x] 5.2.4 빌드 확인 및 다중 선택 일괄 크기 조절/회전 검증 — 완료: 자체 검증(스케일/회전/가로·세로 전단) + 앱에서 마키 선택 후 모서리 핸들 드래그 확인(그룹+도형이 함께 비례 확대). **회전/전단의 실제 드래그는 앱에서 직접 해보지 않음(계산은 자체 검증).**

### Step 5.3 그룹 격리 편집 (Isolation Mode)
> 선행: 2.0.2(그룹 선택), 2.0.3(계층 트리)
- [x] 5.3.1 그룹 더블클릭 시 해당 그룹 내부로 진입하는 격리 모드 상태 머신 추가 — 완료 (2026-10-30): `Document` 에 격리 상태(`EnterIsolation/ExitIsolation/GetIsolationGroup`, NodeId 로 보관해 Undo/삭제 시 자동 해제). 일반 그룹을 더블클릭하면 진입(프레임/표/클리핑 그룹 제외). 중첩 진입 가능.
- [x] 5.3.2 캔버스 상단에 격리 네비게이션 바(빵부스러기: Breadcrumbs) 표시 — 완료: 캔버스 상단 브레드크럼 막대 `Layer › Group › …` (조상 그룹 클릭으로 이동, 레이어 클릭으로 전체 종료).
- [x] 5.3.3 격리 대상 외 나머지 객체들은 딤(Dimming/반투명) 처리 및 마우스 인터랙션 차단 — 완료: 격리 밖은 반투명 덮개로 딤 처리하고 격리 그룹만 다시 그림. 히트 테스트/마키는 격리 그룹의 직속 자식만 대상(밖은 선택 불가).
- [x] 5.3.4 ESC 키 또는 캔버스 바깥 더블클릭 시 상위 레벨로 복귀 — 완료: Esc = 한 단계 위로, 빈 곳 더블클릭 = 한 단계 위로(캔버스 "바깥"이 아니라 선택이 없는 빈 곳 기준).
- [x] 5.3.5 빌드 확인 및 그룹 내부 도형 직접 편집 검증 — 완료: 자체 검증 40 + 앱에서 진입/딤/브레드크럼/내부 개별 선택/Esc 복귀 확인.

### Step 5.4 클립보드 (SVG 교환)
> 선행: 2.0.5(`ImportNodes` — 붙여넣을 때 NodeId 중복 방지), 2.9(외부 SVG 해석). 복사는 `SvgWriteOptions::File`(이미지 임베드)로 직렬화
- [x] 5.4.1 선택된 노드들을 메모리 상의 SVG 문자열로 직렬화하여 Windows 클립보드(`CF_UNICODETEXT` 및 `image/svg+xml`)에 복사(Ctrl+C) — 완료 (2026-10-30): Ctrl+C/X → 선택 노드를 월드 변환으로 구워 SVG(File 형식, M2 메타 포함)로 직렬화해 `image/svg+xml` + `CF_UNICODETEXT` 로 클립보드에 넣음. 조상이 함께 선택되면 중복 제외.
- [x] 5.4.2 클립보드에서 SVG 데이터를 읽어와 현재 문서 캔버스에 붙여넣기(Ctrl+V) — 완료: Ctrl+V → 클립보드 SVG 를 읽어 `ImportNodes` 로 붙여넣기(NodeId 새로 발급), 연속 붙여넣기는 위치를 어긋나게, 선택이 프레임이면 그 프레임 안으로. 앱에서 PowerShell 로 넣은 SVG 텍스트 붙여넣기 확인.
- [ ] 5.4.3 Illustrator, Figma, 브라우저 등 외부 애플리케이션과의 양방향 복사/붙여넣기 테스트 — **부분**: 클립보드 SVG 텍스트(외부 형식과 같은 `image/svg+xml`/텍스트)로 주고받는 경로는 확인. **Illustrator/Figma/브라우저 실제 앱과의 양방향 복사/붙여넣기는 이 환경에서 테스트하지 못함.**
- [x] 5.4.4 빌드 확인 및 외부 툴 연동 검증 — 완료: 빌드 + 자체 검증(직렬화 왕복, 조상 중복 제외, 월드 위치 구움, 비 SVG 거부) + 앱에서 붙여넣기/복사 내용 확인.

### Step 5.5 PNG/JPG/PDF 래스터 출력
- [x] 5.5.1 해상도(DPI) 및 배율(1x, 2x, 4x) 지정이 가능한 내보내기 대화상자 구현 — 완료 (2026-10-30): 파일 → 이미지 내보내기(PNG/JPG) 대화상자: 형식, 배율(1x/2x/4x/8x), DPI, 투명 배경, 선택 영역만. 설정 기억.
- [x] 5.5.2 WIC 기반 오프스크린 렌더 타깃(`ID2D1BitmapRenderTarget`)에 고해상도 렌더링 — 완료: WIC 비트맵 + `CreateWicBitmapRenderTarget` 오프스크린 렌더링(기존 `D2DRenderer` 재사용, 한 변 최대 16384px).
- [x] 5.5.3 WIC 인코더를 통해 투명 배경 PNG 및 무손실 JPG 파일 저장 — 완료: WIC 인코더로 투명 PNG(32bpp BGRA) / JPG(24bpp, 품질 92, 문서 배경색 합성), DPI 메타데이터 기록.
- [ ] 5.5.4 (선택) PDF 출력: Step 4.6.5(WebView2 `PrintToPdf`)로 해결 가능하면 별도 Direct2D Print Control 구현은 생략 — **건너뜀**: Step 4.6.5(WebView2 PrintToPdf)에 의존하며 4.6 은 이번에 진행하지 않음.
- [x] 5.5.5 빌드 확인 및 출력된 고해상도 이미지 파일 품질 검증 — 완료: 자체 검증(2x 크기 200x100, 빨간 픽셀 색, PNG 투명/JPG 흰 배경, 영역 지정, 과대 출력 거부) + 앱에서 대화상자→저장 실행해 1800x1200 RGBA PNG 생성/선 색 확인.

### Step 5.6 페인트통 도구 (PaintBucketTool)
> **후순위/선택 사항**: 닫힌 영역 탐지(평면 분할)가 필요한데 D2D 에 Divide 가 없어 구현 비용이 큼 (2.5.7 과 동일한 문제). 목표(SVG 일러스트 + HTML 디자인) 달성에 필수가 아니므로 마지막에 판단
- [x] 5.6.1 캔버스 클릭 지점의 닫힌 교차 영역 탐색 (불리언 Divide 또는 지오메트리 패스 분석) — 완료 (2026-10-30): `RegionFill.cpp` — Divide 없이 **래스터 플러드 필**(윤곽을 오프스크린에 그려 선 픽셀을 벽으로, 틈 2px 이하는 막힘, 가장자리에 닿으면 열린 영역으로 실패).
- [x] 5.6.2 탐색된 경계 영역을 독립된 신규 폐곡선 `PathNode`로 생성 — 완료: 채워진 영역의 바깥 윤곽을 따라가 다각형으로 만들고 RDP(허용 1.3px)로 단순화 → 닫힌 `PathNode`(꼭짓점 직선). 안쪽 섬/구멍은 무시, 정밀도는 약 0.5 단위(래스터 해상도 한계).
- [x] 5.6.3 현재 활성화된 채우기 스타일(Fill Paint)을 즉시 적용하여 문서에 추가 — 완료: 페인트통(K) 클릭 → 속성창의 현재 채우기 색으로 새 도형 추가 + 선택 + Undo 기록. 선 위 클릭/열린 영역은 상태 표시줄 안내.
- [x] 5.6.4 빌드 확인 및 선으로 둘러싸인 빈 공간 클릭 채우기 검증 — 완료: 자체 검증(상자, 대각선 4등분 삼각형 2곳, 이동된 도형, 열린 U 자 실패, 막으면 성공, 선 위 실패) + 앱에서 X 자 상자의 두 삼각형 채우기 확인. 곡선 경계에서도 같은 방식이나 이번에 시각 확인은 하지 않음.

---

## Phase 6 — 최적화, 리팩토링 & 표준 일러스트 앱(Inkscape / Illustrator) 벤치마킹 보완

> 기능 구현(Phase 1~5) 과정에서 누적된 결합도와 핫패스(Hot-path) 병목을 해소하고, 상용 벡터 일러스트 앱(Adobe Illustrator, Inkscape 등)과 비교해 구조적으로 달랐던 설계(바운딩 박스 정의, 객체 선택과 노드 편집의 분리, 라이브 도형 핸들, 복합 패스 등)를 벤치마킹하여 보완하는 단계.

---

### Part A. 코드 구조 리팩토링 및 아키텍처 정합성 개선 (기존 6.1 ~ 6.5 구체화)

### Step 6.1 `CD2DPropertyView` 섹션별 메소드 분리 및 레이아웃 재구성 최적화
> **대상 파일**: `D2DPropertyView.h`, `D2DPropertyView.cpp`
> **현황 및 문제점**: `InitControls()`, `SyncControlsFromData()`, `UpdateLayout()` 단일 거대 함수 안에 도형·그라디언트·그룹·텍스트·이미지·프레임·버튼·그리드·캔버스 로직이 혼재되어 있으며, 창 크기 변경(`OnSize` → `AdjustLayout`)이나 `SetPaneMode()` 호출 시 컨트롤 전체(`DestroyWindow` + `Create`)를 재생성하는 오버헤드(0.3.4 메모)와 깜빡임이 남아 있음.
- [ ] 6.1.1 섹션별 초기화 메소드 분리: `InitShapeStyleSection()`, `InitGradientSection()`, `InitTextSection()`, `InitImageSection()`, `InitFrameSection()`, `InitTableSection()`, `InitGridModeControls()`, `InitCanvasModeControls()`
- [ ] 6.1.2 섹션별 데이터 동기화 메소드 분리: `SyncTextSection()`, `SyncImageSection()`, `SyncFrameSection()`, `SyncGradientSection()`, `SyncStrokeSection()` (기존 `SyncTableControls()` 패턴으로 통일)
- [ ] 6.1.3 섹션별 배치/가시성 메소드 분리: `LayoutTextSection(int& y)`, `LayoutImageSection(int& y)`, `LayoutFrameSection(int& y)`, `LayoutStyleSection(int& y)` (기존 `LayoutGroupPanel(int& y)` 패턴으로 통일하여 `UpdateLayout()` 본문을 섹션 호출 흐름 제어 위주로 단순화)
- [ ] 6.1.4 불필요한 컨트롤 재생성 방지: `SetPaneMode(PaneMode mode)` 호출 시 `m_paneMode != mode`이거나 최초 초기화(`!m_bInitialized`)일 때만 컨트롤 생성을 수행하고, 단순 리사이즈(`AdjustLayout`)에서는 `UpdateLayout()`만 호출하도록 개선
- [ ] 6.1.5 빌드 확인 및 모든 선택 대상(도형/그룹/텍스트/이미지/프레임/테이블/그리드/캔버스) 속성창 회귀 검증

### Step 6.2 노드/컴포넌트 타입 판별 일원화 (문자열/다중 `dynamic_cast` 제거)
> **대상 파일**: `D2DGraphicEngine.h`, `D2DGraphicEngine.cpp`, `ITool.cpp`, `D2DPropertyView.cpp`, `LayerWnd.cpp`, `SvgWriter.cpp`, `SvgReader.cpp`
> **현황 및 문제점**: `HitTestSingleNode`(`ITool.cpp`), `HitTestTopShape`(`ITool.cpp`), `CD2DPropertyView::SyncControlsFromData`, `LayerWnd` 등에서 `GetName() == L"버튼"`, `GetHtmlTag() == "button"`, 다중 `dynamic_cast<FrameNode*/TableNode*/GroupNode*>` 조합으로 타입을 판별하고 있어 레이어 이름을 바꾸면 버튼 속성이 풀리거나 컴포넌트가 늘어날수록 조건문이 취약해짐.
- [ ] 6.2.1 `ge::Node`(또는 `ge::GroupNode`)에 명시적 컴포넌트 식별자(`enum class ComponentKind { None, Frame, Button, Table, SymbolInstance }`) 및 선택/히트 정책 가상·헬퍼 함수(`IsAtomicComponent()`, `AllowsChildSelection()`) 도입
- [ ] 6.2.2 `ITool.cpp`(`HitTestSingleNode`, `HitTestTopShape`, `HitTestDeepNode`), `CD2DPropertyView`, `LayerWnd`에서 한글 이름(`L"버튼"`) 비교 및 중복 `dynamic_cast` 분기를 `GetComponentKind()` / `IsAtomicComponent()` 정책 쿼리 기반으로 교체
- [ ] 6.2.3 SVG 직렬화(`SvgWriter`/`SvgReader`)에서 `m2:component` 속성과 `ComponentKind` 열거형 매핑 일원화 (레이어 이름 `m2:name` 변경과 무관하게 컴포넌트 정체성 왕복 보장)
- [ ] 6.2.4 빌드 확인 + `RunPureModelVerification()`에 컴포넌트 판별/선택 규칙 검증 추가

### Step 6.3 Undo 스냅샷 메모리 최적화 (이미지 리소스 인메모리 레지스트리 참조화)
> **대상 파일**: `D2DGraphicEngine.h`, `UndoManager.h/.cpp`, `SvgWriter.h/.cpp`, `SvgReader.h/.cpp`
> **현황 및 문제점**: `UndoManager::SaveState()`가 매 편집마다 전체 문서를 SVG 문자열로 직렬화할 때, `ImageNode`의 원본 바이너리가 매 스냅샷마다 Base64 데이터 URI(`data:image/png;base64,...`)로 인라인 인코딩되어 고해상도 이미지 포함 문서에서 Undo 스택(최대 50단계) 메모리 폭증 및 드래그 종료 시 멈칫거림(Hitch)이 발생함.
- [ ] 6.3.1 프로세스 내 이미지 리소스 공유 레지스트리(`ge::ImageResourceRegistry`: `uint64_t imageId` ↔ `std::shared_ptr<const ImageData>`) 및 `ImageNode::m_imageResourceId` 도입
- [ ] 6.3.2 `SvgWriteMode::Snapshot`(Undo/Redo 전용) 모드에서는 Base64 인코딩 대신 경량 참조 URI(`m2mem://image/<resourceId>`)만 기록하고, `SvgWriteMode::Standard`/`ExportOnly` 모드에서만 표준 Base64 데이터 URI로 인라인 출력
- [ ] 6.3.3 `SvgReader`에서 `xlink:href` / `href`가 `m2mem://image/<id>`인 경우 레지스트리의 `shared_ptr<const ImageData>` 및 WIC 비트맵 캐시를 즉시 재연결하여 Base64 인코딩/디코딩 및 PNG 재디코딩 오버헤드 완전 제거
- [ ] 6.3.4 빌드 확인 + 대용량 이미지 포함 문서의 Undo/Redo 응답 속도 및 메모리/왕복 무결성 검증

### Step 6.4 클리핑 마스크 참조 안정화 (Z-Order 변경 보호)
> **대상 파일**: `D2DGraphicEngine.h/.cpp`, `LayerWnd.cpp`, `SvgWriter.cpp`, `SvgReader.cpp`
> **현황 및 문제점**: `GroupNode::GetMaskNode()`(`D2DGraphicEngine.h#L1079`)는 관례상 "그룹의 마지막 자식(`m_children.back()`)"을 마스크로 간주하여, 레이어 패널 드래그(`MoveNodeTo`)나 그룹 내부 Z-Order 변경 시 마스크 도형이 다른 자식으로 뒤바뀌는 취약점(2.4.6 주의사항)이 있음.
- [ ] 6.4.1 `GroupNode`에 명시적 마스크 노드 ID(`NodeId m_maskNodeId = 0`) 도입 (`GetMaskNode()`는 `m_maskNodeId`가 유효하면 해당 자식을 반환하고, 미지정 시 하위 호환으로 `m_children.back()` 폴백)
- [ ] 6.4.2 `Document::MakeClippingMask()` 시 `m_maskNodeId`를 명시 기록하고, 레이어 패널 드래그(`MoveNodeTo`) 및 `ArrangeSelection` 수행 시에도 마스크 역할이 깨지지 않도록 보호 (레이어 트리에 `[마스크]` 배지 표시 연동)
- [ ] 6.4.3 `SvgWriter`/`SvgReader`에서 `m2:mask-id` 또는 `<clipPath>` 참조 복원 후 `m_maskNodeId` 명시적 연결 유지 검증
- [ ] 6.4.4 빌드 확인 + 클립 그룹 내 자식 순서 변경 시 렌더링 및 저장 왕복 검증

### Step 6.5 렌더링 캐시 수명 관리 및 뷰포트 컬링 (대규모 문서 성능)
> **대상 파일**: `D2DGraphicEngine.h`, `D2DGraphicEngine.cpp`, `M2DrawDoc.cpp`, `M2DrawView.cpp`
> **현황 및 문제점**: 문서를 닫거나 도형을 대량 삭제/Undo 복원해도 `D2DRenderer`의 `m_geoCache`, `m_gradientCache`, `m_bitmapCache`, `m_textCache`(키: `NodeId`)에 삭제된 노드의 D2D 리소스가 무기한 잔류하며, 화면 밖(Off-screen) 객체도 매 프레임 D2D 드로우콜을 거침.
- [ ] 6.5.1 `D2DRenderer::PruneCaches(const Document& doc)` 및 `ClearCaches()` 메소드 추가: 새 문서/문서 열기(`OnNewDocument`/`OnOpenDocument`) 시 전체 캐시 초기화, Undo/Redo 및 노드 삭제 시 현존하지 않는 `NodeId` 엔트리 정리
- [ ] 6.5.2 `D2DRenderer::DrawNode`(`D2DGraphicEngine.cpp#L3045`) 진입부에서 노드의 월드 시각적 경계(그림자·선 두께 여유분 포함 `WorldBounds`)와 현재 뷰포트 월드 영역(`ViewportState` 역변환 사각형)의 AABB 교차 판정(Viewport Frustum Culling)을 수행하여 화면 밖 노드의 D2D 드로우콜 생략
- [ ] 6.5.3 빌드 확인 + 수백 개 이상의 도형/텍스트가 있는 문서에서 패닝/줌 프레임레이트 및 렌더링 정확도 검증

---

### Part B. 핫패스(Hot-path) 성능 최적화 및 잠재 버그/레거시 코드 정리 (코드 정밀 점검 결과)

### Step 6.6 히트테스트·스냅·그림자·배경 체커보드 핫패스 최적화 (Critical Performance)
> **대상 파일**: `D2DGraphicEngine.cpp`, `ITool.cpp`, `SnapManager.h/.cpp`, `M2DrawView.cpp`, `D2DShadowRender.cpp`
> **현황 및 문제점 (코드 정밀 진단)**:
> 1. **매 마우스 이동마다 임시 `D2DRenderer` 생성 및 기하 재생성**: `PathNode::HitTestScreenSpace`(`D2DGraphicEngine.cpp#L3663`), `ComputeGradientGizmo`(`ITool.cpp#L206`), `ComputePathBounds`(`ITool.cpp#L291`), `BaseShapeTool::DrawOverlay`(`ITool.cpp#L1530`), `BoxShapeTool::DrawOverlay`(`ITool.cpp#L1933`)에서 매 호출마다 스택에 지역 변수 `ge::D2DRenderer renderer; renderer.Initialize(...)`를 새로 생성함. 지역 렌더러는 `m_geoCache`가 비어 있으므로 `WM_MOUSEMOVE`마다 모든 패스의 `ID2D1PathGeometry`와 브러시를 처음부터 다시 빌드하고 즉시 파괴함. 또한 `HitTestScreenSpace`(`L3694`)는 D2D 판정 실패 시 모든 곡선 구간을 24분할 샘플링하는 `FindClosestSegment`까지 중복 수행함.
> 2. **유휴 마우스 이동 시 전체 씬 곡선 분할 및 $O(N^2)$ 교차점 연산**: `CM2DrawView::OnMouseMove`(`M2DrawView.cpp#L724`)는 `SelectTool`이 아무것도 드래그하지 않는 유휴(`Mode::None`) 상태일 때도 매 픽셀 이동마다 `SnapManager::FindSnapPoint`(`SnapManager.cpp#L45`)를 호출함. 이 함수는 모든 `PathNode`의 곡선을 12분할 선분으로 테셀레이션하고 선분 간 $O(N^2)$ 교차 판정(`L180-L203`)을 수행하며, `excludeNode`가 전달되지 않아 도형 이동 중 자기 자신의 정점/모서리에 셀프 스냅되는 문제와 잠긴 노드(`IsEffectivelyLocked()`) 스냅 누락이 있음.
> 3. **그림자 및 프레임 배지 매 프레임 재계산**: `D2DShadowRender.cpp`의 `BuildShadowGeometry`(`L16`)는 `D2DRenderer::GetGeometry` 캐시를 쓰지 않고 매 프레임 패스를 재빌드하며, 내부 그림자(`DrawNodeInsetShadow`, `L117`)는 매 프레임 `CombineWithGeometry(D2D1_COMBINE_MODE_EXCLUDE)`를 실행함. 또한 `D2DRenderer::DrawGroup`(`D2DGraphicEngine.cpp#L3277`)은 중첩 프레임마다 매 프레임 `CreateTextLayout`을 호출함.
> 4. **투명 체커보드 수천 회 `FillRectangle` 루프**: `CM2DrawView::OnDraw`(`M2DrawView.cpp#L1026-L1048`)에서 8px 격자를 2중 `for`문 `FillRectangle`로 그려 고해상도/줌아웃 시 매 프레임 수천~수만 번의 D2D 드로우콜이 발생함.
- [ ] 6.6.1 `PathNode::HitTestScreenSpace`, `ComputeGradientGizmo`, `ComputePathBounds`, `BaseShapeTool::DrawOverlay`에서 스택 지역 `D2DRenderer` 생성을 제거하고, 뷰의 공유 `D2DRenderer`(또는 `PathNode` 내부 버저닝된 공유 `ID2D1PathGeometry` 캐시)를 재사용하도록 리팩토링
- [ ] 6.6.2 `HitTestSingleNode` / `PathNode::HitTestScreenSpace` 진입 시 월드 AABB(선 두께 절반 + 히트 공차 포함) 사전 필터링(Early-out)을 수행하고, D2D `FillContainsPoint`/`StrokeContainsPoint` 판정 후 불필요한 `FindClosestSegment` 이중 폴백 제거
- [ ] 6.6.3 `CM2DrawView::OnMouseMove`에서 드래그/그리기 작업 중이 아닌 단순 커서 이동(`SelectTool`의 `Mode::None`, `PanTool`, `ZoomTool`) 시 `SnapManager::FindSnapPoint` 호출 생략; 드래그 중에는 선택된 노드 집합(`excludeNodes`)과 잠긴 노드(`IsEffectivelyLocked()`)를 스냅 대상에서 제외하고 노드별 월드 AABB로 곡선 테셀레이션 사전 필터링 적용
- [ ] 6.6.4 `D2DShadowRender.cpp`에서 `renderer.GetGeometry(PathNode&)` 캐시를 공유하고, 프레임 4-코너 개별 반경(`!cr.IsUniform()`) 그림자 기하 지원 및 내부 그림자(Inset Shadow) 마스크 기하·프레임 배지 `IDWriteTextLayout`을 `(NodeId, version)` 기준으로 캐싱
- [ ] 6.6.5 `CM2DrawView::OnDraw`(`M2DrawView.cpp#L1026-L1048`)의 투명 배경 체커보드 2중 루프를 2×2 타일 `ID2D1BitmapBrush`(`D2D1_EXTEND_MODE_WRAP`) 단 1회 `FillRectangle` 호출로 교체
- [ ] 6.6.6 빌드 확인 + 복잡한 패스/그림자 다수 포함 문서에서 마우스 이동 CPU 점유율 및 FPS 향상 검증

### Step 6.7 미사용 레거시 코드 제거 및 그룹 내 로컬/월드 좌표계 버그 수정
> **대상 파일**: `D2DGraphicEngine.h/.cpp`, `M2DrawDoc.h`, `ITool.h/.cpp`
> **현황 및 문제점 (코드 정밀 진단)**:
> 1. **그룹 내부 패스의 앵커/핸들 오버레이 좌표 오차 버그**: `SelectTool::DrawOverlay`(`ITool.cpp#L1421, L1425, L1435`) 및 `BaseShapeTool::DrawCommittedShapeAnchors`(`ITool.cpp#L1627`)에서 패스 노드의 베지에 앵커/핸들 화면 좌표를 계산할 때 `pathShape->GetWorldTransform()` 대신 로컬 변환인 `pathShape->GetTransform()`을 사용하여, 이동/회전/스케일된 그룹(`GroupNode`)이나 프레임(`FrameNode`) 내부의 패스를 선택하면 앵커 포인트가 도형과 동떨어진 위치에 그려지고 조작됨 (`OnLButtonDown`의 `L479`는 `GetWorldTransform()`을 쓰고 있어 그리기와 클릭 위치가 불일치함).
> 2. **중복 히트테스터**: `D2DGraphicEngine.h#L1462`의 `ge::HitTester` 클래스와 `ITool.cpp` 내부의 정적 함수(`HitTestSingleNode`, `HitTestDeepNode`, `HitTestTopShape`)가 이원화되어 있음.
> 3. **미사용 데드 코드**:
>    - `ge::ICommand` 및 `ge::CommandStack`(`D2DGraphicEngine.h#L1476-L1496`, `D2DGraphicEngine.cpp#L3601-L3631`): `ge::UndoManager` 도입 이후 전혀 사용되지 않는 구형 커맨드 스택.
>    - `CM2DrawDoc::m_currentPathNode`(`M2DrawDoc.h#L28, L32`): `m_currentPath`(`L40`)와 중복 선언된 미사용 멤버.
>    - `BaseShapeTool::m_previewNode`(`ITool.h#L299`, `ITool.cpp#L1786-L1861, L1931-L1939`): `m_previewShape`와 중복 유지되던 사각형 전용 테스트 코드.
- [ ] 6.7.1 `SelectTool::DrawOverlay`(`ITool.cpp#L1421-L1436`), `SelectTool::OnMouseMove`(`ITool.cpp#L688`), `BaseShapeTool::DrawCommittedShapeAnchors`(`ITool.cpp#L1627`) 등 모든 앵커/핸들 좌표 변환을 `GetWorldTransform()` 기반으로 통일하여 그룹/프레임 내부 패스 편집 정합성 수정
- [ ] 6.7.2 `BaseShapeTool::m_previewNode` 분기 코드 제거(`m_previewShape`로 완전 일원화), `CM2DrawDoc::m_currentPathNode` 및 미사용 `ge::ICommand`/`ge::CommandStack` 제거
- [ ] 6.7.3 `ITool.cpp`에 흩어진 히트테스트 헬퍼들을 `ge::HitTester`의 정적/멤버 인터페이스로 통합 정리
- [ ] 6.7.4 빌드 확인 + 회전/이동된 그룹 및 프레임 내부 패스의 노드 편집 정확도 검증

---

### Part C. 표준 벡터 일러스트 앱(Inkscape / Adobe Illustrator) 설계 비교 및 벤치마킹 기능 보강

> **설계 비교 요약**:
> | 영역 | 현재 M2Draw 설계 | Inkscape / Illustrator 표준 설계 | 보완 방향 |
> | :--- | :--- | :--- | :--- |
> | **바운딩 박스 계산** | `PathNode::GetLocalBounds`가 오프커브 핸들(`hIn`/`hOut`) 끝점까지 단순 포함하여 바운딩 박스가 실제 곡선보다 부풀려짐 | 3차 베지에 도함수($B'(t)=0$) 극값 기반의 **정밀 기하 경계(Tight Geometric Bounds)** 와 선 두께를 포함한 **시각적 경계(Visual Bounds)** 구분 | Step 6.8 |
> | **선택 도구 vs 노드 편집** | `SelectTool`(`V`) 하나가 바운딩 박스 변형과 모든 베지에 앵커/핸들 표시·편집을 동시에 수행하여 복잡한 패스 선택 시 화면이 핸들로 뒤덮임 | **선택 도구(`V`/`S`: 객체 바운딩 박스 변형)** 와 **직접 선택/노드 도구(`A`/`N`: 다중 앵커 드래그·타입 변환·세그먼트 편집)** 를 명확히 분리 | Step 6.9 |
> | **파라메트릭 도형 조작** | 생성 후 상단 옵션바 수치 입력으로만 파라미터 변경 가능; 사각형 스케일 시 둥근 모서리(`rx`,`ry`)가 행렬 스케일로 찌그러짐 | 캔버스 위 **라이브 도형 위젯(Live Shape Handles)** 드래그로 모서리 반경·별 내경비 등 즉시 조절 + 스케일 시 모서리 반경 비율 보존 | Step 6.10 |
> | **복합 패스 & 오프셋** | 불리언 결과나 기어 도형만 내부적으로 다중 `SubPath`를 가지며, 사용자가 임의 도형을 구멍 뚫린 복합 패스로 묶거나 풀 수 없음 | **복합 패스 만들기/풀기(`Ctrl+8` / `Ctrl+Alt+8`)**, **패스 이동(Offset Path)**, **화살표 키 미세 이동(Nudge)**, **대칭 반전(Flip)** 기본 제공 | Step 6.11 |
> | **가이드라인 & 스타일 복제** | 눈금자(`RulerRenderer`)가 눈금 표시만 하고 가이드라인이 없음; 객체 간 스타일을 복사하는 스포이드 도구 부재 | 눈금자에서 드래그해 만드는 **가이드라인(Ruler Guides)**, 클릭 한 번으로 채우기/선/폰트 스타일을 복제하는 **스포이드 도구(`I`)**, **문서 스와치** 제공 | Step 6.12 |

### Step 6.8 정밀 기하 바운딩 박스(Tight Bezier Bounds) 및 시각적 경계(Visual Bounds) 도입
> **대상 파일**: `D2DGraphicEngine.h`, `D2DGraphicEngine.cpp`, `SmartGuides.cpp`
> **현황 및 문제점**: `PathNode::GetLocalBounds`(`D2DGraphicEngine.cpp#L206-L227`)가 곡선의 실제 궤적이 아닌 제어 핸들(`AbsIn()`, `AbsOut()`)의 최대/최소 좌표를 합산하고 최소 폭/높이 2px을 강제 부과함. 이로 인해 긴 핸들을 가진 곡선 도형을 선택하거나 정렬(`AlignSelection`)·배분(`DistributeSelection`)·스마트 가이드 스냅을 수행할 때 실제 도형 외곽선과 바운딩 박스 사이에 큰 유격이 발생함.
- [ ] 6.8.1 `PathNode::GetLocalBounds(Rect2& out)` 개선: 각 3차 베지에 세그먼트($P_0, P_1, P_2, P_3$)에 대해 $x(t), y(t)$의 1차 도함수 $B'(t) = 3(1-t)^2(P_1-P_0) + 6(1-t)t(P_2-P_1) + 3t^2(P_3-P_2) = 0$ ($t \in (0,1)$)의 해(극값)와 양 끝점($P_0, P_3$)만을 계산하여 D2D 의존성 없는 순수 수학 기반의 **정밀 기하 경계(Tight Geometric Bounds)** 산출
- [ ] 6.8.2 `PathNode`에 더티 플래그 연동 로컬 바운딩 박스 캐시(`mutable Rect2 m_cachedLocalBounds`, `mutable bool m_boundsDirty = true`)를 추가하여 `Invalidate()` 호출 전까지 $O(1)$로 반환
- [ ] 6.8.3 `Node::GetVisualWorldBounds(Rect2& out)` 추가: 기하학적 월드 경계에 선 두께 절반(`stroke.width * 0.5f`)을 반영한 시각적 경계를 산출하여 `Document::AlignSelection`, `DistributeSelection`, `SmartGuides`에서 정밀한 외곽선 맞춤 지원
- [ ] 6.8.4 빌드 확인 + `RunPureModelVerification()`에 곡선 핸들이 돌출된 베지에 패스의 Tight Bounds 정확도 테스트 추가

### Step 6.9 직접 선택 / 노드 편집 도구(`NodeTool`, 단축키 `A`) 분리 및 다중 앵커 편집 고도화
> **대상 파일**: `ITool.h`, `ITool.cpp`, `ToolManager.h`, `ToolOptionBar.h/.cpp`, `M2DrawView.cpp`, `MainFrm.cpp`
> **현황 및 문제점**: 현재 `SelectTool`(`ITool.cpp#L1414-L1448`)은 도형 선택 시 8개 스케일 핸들과 모든 베지에 앵커/핸들을 한꺼번에 표시함. 또한 단일 앵커(`m_editSubPath`, `m_editNodeIdx`)만 드래그할 수 있어, 여러 정점을 드래그 박스로 한 번에 선택해 이동하거나 정점 타입(뾰족점 `Corner` ↔ 부드러운점 `Smooth` ↔ 대칭점 `Symmetric`)을 전환하는 Illustrator(`A`)/Inkscape(`N`) 수준의 노드 편집이 불가능함.
- [ ] 6.9.1 **선택 도구(`SelectTool`, `V`)와 직접 선택/노드 도구(`NodeTool` 또는 `SelectTool`의 명시적 노드 편집 서브모드, 단축키 `A` / 더블클릭 진입) 역할 분리**:
  - 일반 `SelectTool`(`V`) 상태에서는 객체/그룹의 바운딩 박스 핸들(및 그라디언트/라이브 도형 기즈모)만 깔끔하게 표시
  - 패스 더블클릭 또는 단축키 `A`(툴바 '직접 선택/노드 편집' 도구) 전환 시 베지에 앵커/핸들 편집 활성화
- [ ] 6.9.2 **다중 앵커 선택 및 일괄 조작**:
  - 마키(Marquee) 드래그 및 `Shift+클릭`으로 복수 앵커(`std::vector<NodeRef> m_selectedAnchors`) 동시 선택
  - 선택된 복수 앵커의 동시 드래그 이동, 화살표 키 미세 이동, `Delete`/`Backspace` 키를 통한 선택 앵커 일괄 삭제(패스 연속성 유지)
- [ ] 6.9.3 **앵커 타입 전환 및 곡선 세그먼트 직접 드래그**:
  - 선택된 앵커를 `Corner`(꺾임점), `Smooth`(부드러운 곡선점), `Symmetric`(대칭 핸들점)으로 전환하는 액션 및 `CToolOptionBar` 버튼(`Alt+클릭` 토글 포함) 추가
  - 두 앵커 사이의 곡선 세그먼트를 직접 드래그하여 양쪽 핸들(`hOut`, `hIn`)을 자연스럽게 휘게 하는 세그먼트 리셰이프(Segment Dragging) 지원
- [ ] 6.9.4 **앵커 분할/결합 액션**: 선택한 앵커에서 패스 끊기(Break Path at Selected Nodes) 및 두 열린 끝점 앵커를 하나로 잇기(Join Selected End Nodes) 지원
- [ ] 6.9.5 빌드 확인 + 다중 정점 선택 이동/삭제/타입 전환 및 Undo 왕복 검증

### Step 6.10 캔버스 라이브 도형 위젯(Live Shape Handles) 및 모서리 반경 보존 스케일링
> **대상 파일**: `D2DGraphicEngine.h/.cpp`, `ITool.h/.cpp`, `M2DrawView.cpp`
> **현황 및 문제점**: Illustrator의 Live Shapes나 Inkscape의 도형 핸들처럼 캔버스 위에서 직접 마우스로 둥근 사각형의 모서리 반경(`rx`), 별의 안쪽 반경 비율(`innerRatio`), 기어의 톱니 깊이(`depthRatio`)/내경(`boreRatio`)을 조절하는 직관적 위젯이 없음. 또한 둥근 사각형을 `SelectTool`로 비균등 스케일(`ApplyScale`)하면 아핀 행렬(`m_xf`)만 늘어났다가 모서리 원호가 타원형으로 찌그러짐.
- [ ] 6.10.1 **파라메트릭 사각형 스케일 시 모서리 반경 보존**: `SelectTool::ApplyScale`(`ITool.cpp#L106`)에서 `FrameNode::ResizeFrame`과 마찬가지로, 파라메트릭 도형(`IsParametric() == true`, 특히 `ShapeKind::Rect`)을 스케일할 때 행렬 스케일 왜곡 대신 `ShapeParams`의 폭/높이(`w`, `h`)를 갱신하고 `RebuildFromParams()`를 호출하여 모서리 반경(`rx`, `ry`)이 찌그러지지 않고 유지되도록 개선
- [ ] 6.10.2 **캔버스 라이브 도형 핸들(Live Shape Widgets) 렌더링 및 드래그**:
  - `ShapeKind::Rect`: 우상단 모서리 안쪽에 다이아몬드/원형 라이브 코너 핸들을 표시하고 드래그 시 `rx`/`ry` 실시간 조절
  - `ShapeKind::Star` / `Polygon`: 내경 정점에 라이브 핸들을 표시하고 드래그 시 `innerRatio` 실시간 조절
  - `ShapeKind::Gear`: 톱니 깊이(`depthRatio`) 및 중심 구멍 반경(`boreRatio`) 조절 핸들 지원
- [ ] 6.10.3 빌드 확인 + 둥근 사각형 비균등 리사이즈 시 코너 원형 유지 및 라이브 핸들 드래그 Undo 검증

### Step 6.11 복합 패스(Compound Path, `Ctrl+8`), 패스 이동(Offset Path) 및 키보드 정밀 변형
> **대상 파일**: `D2DGraphicEngine.h/.cpp`, `M2DrawView.cpp`, `MainFrm.cpp`
> **현황 및 문제점**:
> 1. `PathNode`는 이미 다중 `SubPath`(`std::vector<SubPath> m_paths`)와 `FillMode`(`Alternate`/`Winding`)를 완벽히 지원하지만, 사용자가 선택한 여러 도형을 하나의 복합 패스로 묶어 도넛처럼 구멍을 뚫거나(`Ctrl+8`), 반대로 복합 패스(또는 불리언 연산 결과물)를 개별 도형으로 분리(`Ctrl+Alt+8`)하는 명령이 없음.
> 2. 일러스트 작업에서 가장 빈번히 쓰이는 **방향키 미세 이동(Nudge)** 이 `CM2DrawView::OnKeyDown`(`M2DrawView.cpp#L810`)에 구현되어 있지 않으며, **좌우/상하 대칭 반전(Flip Horizontal/Vertical)** 과 **패스 이동(Offset Path, 안쪽/바깥쪽 여백 패스 생성)** 기능이 부재함.
- [ ] 6.11.1 **복합 패스 만들기(`Document::MakeCompoundPath`, `Ctrl+8`) 및 풀기(`Document::ReleaseCompoundPath`, `Ctrl+Alt+8`)**:
  - 만들기: 선택된 복수의 `PathNode`들의 서브패스들을 최상위 도형의 로컬 좌표계로 변환하여 단일 `PathNode`(`FillMode::Alternate`)의 `m_paths`로 병합
  - 풀기(Break Apart): 다중 `SubPath`를 가진 `PathNode`를 각각의 독립된 단일 서브패스 `PathNode`들로 분리
- [ ] 6.11.2 **패스 이동(`PathOp::OffsetPath`) 구현**: 선택된 패스를 지정한 거리($\pm d$ px)만큼 바깥쪽 확장 또는 안쪽 축소한 신규 `PathNode`를 생성 (D2D `Widen` + 윤곽선 추출/불리언 조합 활용)하여 스티커 외곽선·테두리 제작 지원
- [ ] 6.11.3 **키보드 방향키 미세 이동(Nudge) 및 대칭 반전(Flip) / 90° 회전**:
  - `CM2DrawView::OnKeyDown`(`M2DrawView.cpp#L810`)에서 텍스트 편집 중이 아닐 때 `VK_LEFT`/`VK_RIGHT`/`VK_UP`/`VK_DOWN` 입력 시 선택 객체를 1px(`Shift` 누름 시 10px) 이동하고 연속 키 입력을 단일 Undo 스냅샷으로 병합
  - `Transformer::FlipHorizontal(Document&)`, `Transformer::FlipVertical(Document&)`, `Rotate90CW/CCW` 구현 및 상단 정렬/변형 툴바·우클릭 메뉴 연동
- [ ] 6.11.4 빌드 확인 + 복합 패스 구멍 뚫기/분리, 오프셋 패스, 화살표 키 이동 및 반전 검증

### Step 6.12 눈금자 가이드라인(Ruler Guides), 스포이드 도구(`EyedropperTool`, `I`), 문서 스와치(Swatches)
> **대상 파일**: `D2DGraphicEngine.h/.cpp`, `RulerRenderer.h/.cpp`, `SnapManager.h/.cpp`, `ITool.h/.cpp`, `D2DPropertyView.h/.cpp`, `M2DrawView.cpp`, `SvgWriter.cpp`, `SvgReader.cpp`
> **현황 및 문제점**:
> 1. 상단/좌측 눈금자(`RulerRenderer`, 20px 두께)가 눈금만 표시할 뿐, Illustrator/Inkscape처럼 눈금자에서 캔버스로 드래그하여 수평/수직 안내선(Guide Line)을 끌어다 놓는 기능이 없음.
> 2. 다른 도형의 채우기(단색/그라디언트)·선 두께/색상·그림자·폰트 스타일을 한 번의 클릭으로 추출해 적용하는 **스포이드 도구(Eyedropper Tool, 단축키 `I`)** 가 없음.
> 3. 문서 내에서 자주 쓰는 색상/그라디언트를 재사용하는 **문서 스와치(Document Swatches / 최근 사용 색상)** 가 없어 매번 색상환이나 HEX 코드를 다시 입력해야 함.
- [ ] 6.12.1 **눈금자 드래그 가이드라인(Ruler Guides) 도입**:
  - `ge::Document`에 가이드라인 모델(`struct GuideLine { bool isHorizontal; float worldPos; bool locked; }`, `std::vector<GuideLine> m_guides`) 추가
  - `CM2DrawView`에서 상단/좌측 눈금자 영역(0~20px)에서 마우스를 드래그하면 수평/수직 가이드라인 생성, 기존 가이드라인 드래그 이동(눈금자 밖으로 드래그하거나 `Delete`로 삭제), `SnapManager` 및 `SmartGuides`에 가이드라인 스냅 연동
  - `SvgWriter`/`SvgReader`에서 `<sodipodi:guide>` 및 `m2:guides`로 가이드라인 저장·복원 왕복 지원
- [ ] 6.12.2 **스포이드 도구(`EyedropperTool`, 단축키 `I`) 구현**:
  - 캔버스 위 임의의 노드를 클릭하면 해당 노드의 `Style`(`Paint` 단색/그라디언트, `Stroke`, `Shadow`, `Opacity`) 및 `TextNode` 폰트 속성을 추출하여 현재 선택된 노드들(선택이 없으면 `ToolCreationSetting` 기본 스타일)에 즉시 적용 (`Shift+클릭` 시 선 색상만 적용 옵션)
- [ ] 6.12.3 **문서 색상/그라디언트 스와치(Document Swatches) 및 최근 색상 팔레트**:
  - `CD2DPropertyView`의 고정 16색 팔레트 하단에 '문서 사용 색상(Document Colors) / 사용자 등록 스와치' 슬롯을 추가하고 SVG `<defs>` 스와치와 연동
- [ ] 6.12.4 빌드 확인 + 눈금자 가이드라인 생성/스냅/저장 왕복 및 스포이드 스타일 복제 검증

---

## 추후 기능 (백로그) — 현재 Phase 에 포함되지 않음, 일정 미정

### 선 마커 (Line Markers: 화살표 / 점 / 사각형 / 마름모 등) — 2026-10-28 추가
> Inkscape 의 "표시자(Markers)" 에 해당. **Cap(선 끝 모양: Butt/Round/Square)과는 별개**: 선의 시작점·중간 꺾임점·끝점에 도형(화살표, 점, 사각형, 마름모, 가위 등)을 붙이는 기능. 화살표 선, 치수선, 흐름도 연결선에 사용. 표준 SVG `<marker>` + `marker-start/mid/end` 로 구현 가능(Inkscape·브라우저·Illustrator 호환).
- 선행 조건: `<defs>` 통합 관리(2.8.2, 그라디언트 2.3·클리핑 2.4 와 같이 구축), Reader 의 `<defs>`/`<use>` 해석(2.9.2c)
- [ ] M.1 모델: `Stroke` 에 시작/중간/끝 마커 종류(프리셋 + 크기 배율)와 방향 옵션(경로 방향에 맞춤 `orient="auto"`) 추가
- [ ] M.2 렌더러: 서브패스 시작/끝/각 꼭짓점에서 접선 방향 계산(곡선 구간은 베지에 접선), 마커 도형을 선 두께에 비례한 크기로 그림. 마커 영역만큼 선 끝을 잘라 화살촉이 선 끝과 겹치지 않게 처리
- [ ] M.3 SvgWriter/Reader: `<defs>` 에 `<marker>` 를 한 번만 정의하고 `marker-*` 속성으로 참조, 외부 SVG 의 `<marker>` 가져오기
- [ ] M.4 속성창 UI: 시작/중간/끝 마커 콤보(미리보기 아이콘 팝업 목록), 크기 입력
- [ ] M.5 빌드 확인 + 열린/닫힌 패스·곡선·그룹 안 패스에서 마커 방향/크기/저장·복원·Undo 검증
