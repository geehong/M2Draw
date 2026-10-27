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

### Step 1.1 `INodeVisitor` 도입
- [ ] 1.1.1 `INodeVisitor` 인터페이스 정의 (`Visit(PathNode&)`, `Visit(GroupNode&)`, `Visit(LayerNode&)`)
- [ ] 1.1.2 `Node` 기본 클래스 및 파생 노드들에 `virtual void Accept(INodeVisitor&)` 구현
- [ ] 1.1.3 빌드 확인 및 더미 방문자(카운트/디버그 덤프) 단위 테스트로 순회 검증

### Step 1.2 `SvgWriter` (확장 SVG 저장)
- [ ] 1.2.1 `SvgWriter : public INodeVisitor` 구현 (문서 레벨 `<svg viewBox="...">` 생성)
- [ ] 1.2.2 계층 구조 출력: `LayerNode` (`inkscape:groupmode="layer"` 호환), `GroupNode` (`<g transform="...">`)
- [ ] 1.2.3 `PathNode` 출력: 서브패스 정점들을 `<path d="M... C... L... Z">` 명령어로 변환
- [ ] 1.2.4 스타일 및 확장 네임스페이스(`xmlns:m2`) 속성 직렬화:
  - Fill, Stroke, StrokeWidth, Opacity
  - `m2:shape` (파라메트릭 종류 및 params variant 값)
  - `m2:node-types` (Corner/Smooth/Symmetric 베지에 핸들 타입)
- [ ] 1.2.5 로캘 독립 부동소수점 포맷터(소수점 `.` 고정, 불필요한 0 제거) 적용
- [ ] 1.2.6 빌드 확인 및 생성된 SVG 파일 Chrome/Edge 브라우저 시각 렌더링 검증

### Step 1.3 `SvgReader` (XmlLite, 자체 출력물 왕복)
- [ ] 1.3.1 Windows 내장 `XmlLite` 파서 래퍼 클래스 작성 (스트림/파일 파싱 기반)
- [ ] 1.3.2 노드 트리 복원: `<svg>`, `<g>`, `<path>` 태그를 읽어 `Document` 트리 구조 재구성
- [ ] 1.3.3 패스 데이터 파싱: `d` 속성(M, C, L, Z) 파싱하여 `SubPath` 및 `BezierNode` 복원
- [ ] 1.3.4 `m2:*` 확장 속성 파싱: 파라메트릭 도형 정보 및 노드 타입 복원 (누락 시 일반 Path로 수용)
- [ ] 1.3.5 스타일 복원 및 `AssignIds`를 통한 식별자 초기화
- [ ] 1.3.6 빌드 확인 및 왕복(Round-trip: 저장 후 불러왔을 때 원본과 완전 동일 여부) 검증

### Step 1.4 `OnSaveDocument`/`OnOpenDocument` 연동 + 변경 플래그
- [ ] 1.4.1 `CM2DrawDoc::OnSaveDocument` / `OnOpenDocument` 재정의 (확장자 `.svg` 및 `.m2svg` 필터 연결)
- [ ] 1.4.2 문서 수정 시점마다 `SetModifiedFlag(TRUE)` 호출 연결 (도형 추가/수정/삭제/레이어 조작)
- [ ] 1.4.3 `CM2DrawDoc::Serialize`의 기존 TODO 주석 정리
- [ ] 1.4.4 파일 저장 시 제목 표시줄 변경(`*` 제거) 및 앱 종료 시 저장 확인 대화상자 동작 검증

### Step 1.5 스냅샷 방식 Undo/Redo + Ctrl+Z/Y
- [ ] 1.5.1 `DocumentSnapshot` 기반 `CommandStack` 엔진 연동 (스냅샷 깊은 복제 or SVG 직렬화 메모리 버퍼)
- [ ] 1.5.2 드래그(이동/변형/핸들 조작) 작업: 마우스 다운 시점 시작 스냅샷, 마우스 업 시점 커밋 처리
- [ ] 1.5.3 메뉴 명령(삭제, 복제, 순서 변경 등) 직전/직후 커밋 연동
- [ ] 1.5.4 UI 메뉴(`ID_EDIT_UNDO`, `ID_EDIT_REDO`) 및 가속키(Ctrl+Z, Ctrl+Y) 핸들러 연결
- [ ] 1.5.5 빌드 확인 및 연속 작업 후 Undo/Redo 동작 및 선택 상태 복원 확인

---

## Phase 2 — SVG 완성도

### Step 2.1 Style 모델 마이그레이션 (Paint/Gradient/Stroke)
- [ ] 2.1.1 신규 `Style` 구조체 도입: `Paint(Solid, Gradient)`, `Stroke(cap, join, miter, dashArray)`
- [ ] 2.1.2 기존 `FillType`, `DashStyle` enum 마이그레이션 및 변환 헬퍼 작성
- [ ] 2.1.3 `PropertyView` 및 `ToolOptionBar`의 스타일 바인딩 UI 업데이트
- [ ] 2.1.4 빌드 확인 및 도형별 선/면 스타일 정상 반영 확인

### Step 2.2 렌더러를 SVG 규칙에 맞춤 (S1~S4 해소)
- [ ] 2.2.1 S1: 열린 서브패스도 면 채우기가 가능하도록 `D2D1_FIGURE_BEGIN_FILLED` 처리
- [ ] 2.2.2 S2: 그룹 불투명도 적용 시 개별 알파 곱셈 대신 `PushLayer`로 그룹 전체 합성 후 투명도 적용
- [ ] 2.2.3 S3: `blendMode`에 따른 D2D 합성 레이어 처리
- [ ] 2.2.4 S4: `ID2D1StrokeStyle`에 LineCap(Butt/Round/Square), LineJoin(Miter/Round/Bevel), DashArray 바인딩
- [ ] 2.2.5 빌드 확인 및 SVG 렌더링 의미론 일치 여부 비교 검증

### Step 2.3 그라디언트 렌더링 + 편집 핸들
- [ ] 2.3.1 `ID2D1LinearGradientBrush`, `ID2D1RadialGradientBrush` 캐싱 및 렌더러 연동
- [ ] 2.3.2 캔버스 상의 그라디언트 시작점/끝점/초점 조작용 오버레이 기즈모 핸들 구현
- [ ] 2.3.3 속성창에 Color Stop 편집기(오프셋, 색상, 알파) 연동
- [ ] 2.3.4 빌드 확인 및 선형/방사형 그라디언트 렌더링 검증

### Step 2.4 클리핑 마스크
- [ ] 2.4.1 `GroupNode::m_clipPath` (마스크로 사용할 자식 노드 지정) 모델 필드 추가
- [ ] 2.4.2 렌더러에서 마스크 도형의 지오메트리를 추출하여 `PushLayer(layerParams)` geometricMask 적용
- [ ] 2.4.3 `SvgWriter`에 `<clipPath id="...">` 및 자식의 `clip-path="url(#...)"` 출력 연결
- [ ] 2.4.4 메뉴/단축키(Ctrl+7 등)를 통한 클리핑 마스크 생성/해제 구현 및 빌드 검증

### Step 2.5 불리언 연산 완성
- [ ] 2.5.1 선택된 두 개 이상의 노드로부터 월드 변환이 적용된 `ID2D1Geometry` 추출
- [ ] 2.5.2 `CombineWithGeometry` 연동 (Union, Exclude/Subtract, Intersect, XOR)
- [ ] 2.5.3 `ID2D1SimplifiedGeometrySink` 구현체를 통해 추출된 세그먼트를 `SubPath`/`BezierNode`로 변환
- [ ] 2.5.4 기존 도형 삭제 및 신규 단일 `PathNode` 교체 커맨드 연동
- [ ] 2.5.5 빌드 확인 및 다양한 도형 간 불리언 연산 결과 검증

### Step 2.6 패스 연산 (Simplify, Outline, Offset, Join/Split, Reverse)
- [ ] 2.6.1 패스 단순화: 불리언 후 노드 폭증 완화 및 자유곡선용 RDP(Ramer-Douglas-Peucker) + 곡률 피팅
- [ ] 2.6.2 외곽선 확장(Outline Stroke): `ID2D1Geometry::Widen`을 이용하여 획을 면 폐곡선으로 변환
- [ ] 2.6.3 패스 결합(Join) 및 분리(Split): 인접 앵커 노드 병합 및 세그먼트 절단 분할
- [ ] 2.6.4 패스 방향 반전(Reverse Path): 서브패스 정점 순서 및 In/Out 핸들 반전
- [ ] 2.6.5 빌드 확인 및 각 패스 연산 동작 검증

### Step 2.7 엔진 기능 UI 연결
- [ ] 2.7.1 그룹화 (Ctrl+G) 및 그룹 해제 (Ctrl+Shift+G) 메뉴/단축키 핸들러 연결
- [ ] 2.7.2 Z-Order 변경 (Bring to Front, Send to Back, Forward, Backward) 단축키/메뉴 연결
- [ ] 2.7.3 정렬 및 분배 (좌/우/상/하/중앙 정렬, 균등 간격 분배) 툴바/옵션바 UI 연결
- [ ] 2.7.4 노드 툴(직접 선택 도구)을 통한 앵커 추가(`InsertNodeOnSegment`) 및 삭제(`RemoveNode`) 연결
- [ ] 2.7.5 빌드 확인 및 UI를 통한 모든 조작 검증

### Step 2.8 SVG 출력 다듬기
- [ ] 2.8.1 파라메트릭 도형의 네이티브 SVG 태그(`<rect rx>`, `<circle>`, `<ellipse>`, `<polygon>`) 출력 옵션
- [ ] 2.8.2 중복 그라디언트, 클립마스크의 `<defs>` 통합 관리 및 ID 참조 최적화
- [ ] 2.8.3 순수 표준 SVG(Clean SVG: `xmlns:m2` 속성 제거)로 내보내기 대화상자 옵션 추가
- [ ] 2.8.4 빌드 확인 및 타 벡터 툴(Illustrator, Inkscape, Figma) 불러오기 호환성 점검

### Step 2.9 외부 SVG 가져오기
- [ ] 2.9.1 표준 기본 도형(`<rect>`, `<circle>`, `<line>` 등)의 `PathNode` 변환 파서
- [ ] 2.9.2 패스 `d` 속성의 상대/절대 좌표(`M, L, H, V, C, S, Q, T, A, Z`) 완전 지원
- [ ] 2.9.3 타원 호(`A/a` 명령어)를 3차 베지에 곡선으로 근사 변환하는 알고리즘 적용
- [ ] 2.9.4 외부 W3C SVG 테스트 파일셋 임포트 렌더링 검증

---

## Phase 3 — 텍스트 & 이미지

### Step 3.1 TextNode 모델 + DirectWrite 렌더
- [ ] 3.1.1 `NodeKind::Text` 및 `TextNode` 클래스 구현 (문자열, 글꼴, 크기, 굵기, 정렬, Point/Area 모드)
- [ ] 3.1.2 DirectWrite(`IDWriteFactory`, `IDWriteTextFormat`, `IDWriteTextLayout`) 렌더러 캐시 파이프라인 구축
- [ ] 3.1.3 변환 행렬(`m_xf`)과 결합된 텍스트 렌더링 및 히트테스트 바운딩 박스 구현
- [ ] 3.1.4 빌드 확인 및 화면 출력 검증

### Step 3.2 TextTool (인라인 편집, IME)
- [ ] 3.2.1 캔버스 클릭 시 Point Text 생성, 드래그 시 Area Box Text 생성
- [ ] 3.2.2 인라인 텍스트 편집기 오버레이(캐럿 깜빡임, 텍스트 블록 드래그 선택)
- [ ] 3.2.3 한글 및 동아시아 IME 조합 문자 완벽 처리(`WM_IME_COMPOSITION`, `WM_IME_CHAR`)
- [ ] 3.2.4 편집 완료(ESC 또는 외곽 클릭) 시 텍스트 커밋 및 Undo 스냅샷 기록
- [ ] 3.2.5 빌드 확인 및 한글/영문 입력 테스트

### Step 3.3 텍스트 → 윤곽선 변환 (Convert to Curves)
- [ ] 3.3.1 `IDWriteFontFace::GetGlyphRunOutline` 인터페이스를 통한 벡터 윤곽선 기하학 추출
- [ ] 3.3.2 추출된 기하학 데이터를 `PathNode`의 `SubPath` 목록으로 변환
- [ ] 3.3.3 메뉴(Ctrl+Shift+O 등) 연결: `TextNode`를 일반 `PathNode`로 치환
- [ ] 3.3.4 빌드 확인 및 글꼴 아웃라인의 베지에 노드 편집 가능 여부 검증

### Step 3.4 ImageNode + 리소스 테이블
- [ ] 3.4.1 `Document`에 비트맵 리소스 테이블(ID, 파일 경로/바이너리 데이터) 추가
- [ ] 3.4.2 `ImageNode` 구현: 리소스 참조, 로컬 바운드, `objectFit` 옵션
- [ ] 3.4.3 WIC(Windows Imaging Component)를 이용한 이미지 로딩 및 `ID2D1Bitmap` 렌더링
- [ ] 3.4.4 이미지 불러오기(메뉴/드래그 앤 드롭) 도구 연동
- [ ] 3.4.5 빌드 확인 및 이미지 이동/스케일/회전 검증

### Step 3.5 텍스트/이미지 SVG 출력
- [ ] 3.5.1 `SvgWriter`에 `TextNode` 직렬화 연동 (`<text>`, `<tspan>`, 글꼴 속성 매핑)
- [ ] 3.5.2 Area Text의 줄바꿈 계산 결과를 다중 `<tspan>` 또는 윤곽선 출력으로 지원
- [ ] 3.5.3 `ImageNode`를 `<image href="data:image/png;base64,...">` 인라인 또는 상대 경로로 직렬화
- [ ] 3.5.4 빌드 확인 및 브라우저에서 텍스트/이미지가 포함된 SVG 파일 렌더링 검증

---

## Phase 4 — HTML 페이지 디자인

### Step 4.1 Page(Artboard) 개념
- [ ] 4.1.1 `Document`를 `vector<Page>` 구조로 확장 (페이지별 이름, 너비, 높이, 배경색)
- [ ] 4.1.2 캔버스 뷰포트에 아트보드 경계 및 배경 렌더링, 페이지 전환 탭/UI 구성
- [ ] 4.1.3 빌드 확인 및 다중 페이지 전환/생성/삭제 검증

### Step 4.2 FrameNode + Frame 도구
- [ ] 4.2.1 `GroupNode`를 상속받는 `FrameNode` 구현 (레이아웃 속성, 패딩, 갭, 배경/테두리, 클리핑)
- [ ] 4.2.2 도구상자에 Frame 도구 추가 (드래그하여 프레임 생성, 선택된 노드들을 Frame으로 감싸기)
- [ ] 4.2.3 빌드 확인 및 프레임 계층 렌더링 검증

### Step 4.3 LayoutEngine (flexbox 부분 구현)
- [ ] 4.3.1 `LayoutEngine` 서비스 클래스 구현: Row/Column 흐름, gap, padding 계산
- [ ] 4.3.2 크기 규칙 지원: 축별 `Fixed(고정)`, `Hug(내용물에 맞춤)`, `Fill(잔여 영역 채움)`
- [ ] 4.3.3 정렬 규칙 지원: `mainAlign(Start/Center/End/SpaceBetween)`, `crossAlign(Start/Center/End/Stretch)`
- [ ] 4.3.4 프레임 내부 자식 노드들의 계산된 위치(Layout Cache) 갱신 파이프라인
- [ ] 4.3.5 빌드 확인 및 프레임 크기 변경 시 자식 노드들의 자동 재배치 검증

### Step 4.4 Frame 안의 텍스트 동작
- [ ] 4.4.1 Frame 내 TextNode의 폭이 `Fill` 또는 `Fixed`일 때 자동 줄바꿈(Word Wrap) 계산 연동
- [ ] 4.4.2 텍스트 내용 길이에 따라 부모 Frame의 높이가 `Hug`로 유동 변경되는 레이아웃 트리거
- [ ] 4.4.3 빌드 확인 및 텍스트 수정 시 실시간 레이아웃 반영 검증

### Step 4.5 HtmlExporter (HTML/CSS 출력기)
- [ ] 4.5.1 `HtmlExporter : public INodeVisitor` 구현
- [ ] 4.5.2 FrameNode를 시맨틱 HTML 태그(`<header>`, `<nav>`, `<section>`, `<div>` 등) 및 Flexbox CSS로 변환
- [ ] 4.5.3 TextNode를 `<h1~h6>`, `<p>`, `<span>` 태그로 매핑
- [ ] 4.5.4 내부 벡터 그래픽(Path/Group)을 인라인 `<svg>` 코드로 삽입
- [ ] 4.5.5 클래스 기반 CSS 스타일 추출 및 중복 스타일 병합 최적화
- [ ] 4.5.6 빌드 확인 및 브라우저에서 내보낸 `.html` 파일 반응형 레이아웃 확인

### Step 4.6 WebView2 미리보기
- [ ] 4.6.1 도킹 윈도우 또는 모달 다이얼로그에 Microsoft Edge WebView2 컨트롤 임베딩
- [ ] 4.6.2 편집 중 실시간으로 HTML을 생성하여 WebView2 인스턴스에 스트리밍 렌더링
- [ ] 4.6.3 뷰포트 너비 조절 슬라이더를 통한 모바일/태블릿/데스크톱 반응형 뷰 테스트
- [ ] 4.6.4 빌드 확인 및 인앱 HTML 실시간 프리뷰 검증

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
- [ ] 5.1.0 **오브젝트 스냅 설정 UI** (사용자 요청): 그리드 설정 창 등에 스냅 대상(정점/중간점/중심/선 위/교차점) 켜고 끄기 표시, 스냅 전체 On/Off
- [ ] 5.1.1 드래그 이동 시 인접 오브젝트의 경계(Edge), 중심(Center), 앵커(Anchor) 근접 탐색
- [ ] 5.1.2 스냅 발생 시 자석 정렬(좌표 보정) 및 화면에 자홍색(Magenta) 보조 가이드라인 렌더링
- [ ] 5.1.3 다중 오브젝트 간 동일 간격(Equal Spacing) 탐지 및 간격 인디케이터 표시
- [ ] 5.1.4 빌드 확인 및 도형 이동 중 스마트 가이드 작동 검증

### Step 5.2 다중 선택 공통 변환
- [ ] 5.2.1 2개 이상 선택된 오브젝트들의 전체 결합 AABB 바운딩 박스 계산
- [ ] 5.2.2 공통 바운딩 박스 변환 기즈모 핸들(스케일, 중심 회전, 전단/Skew) 렌더링
- [ ] 5.2.3 기즈모 드래그 시 각 자식 도형의 로컬 변환(`m_xf`)에 비례 행렬 일괄 합성
- [ ] 5.2.4 빌드 확인 및 다중 선택 일괄 크기 조절/회전 검증

### Step 5.3 그룹 격리 편집 (Isolation Mode)
- [ ] 5.3.1 그룹 더블클릭 시 해당 그룹 내부로 진입하는 격리 모드 상태 머신 추가
- [ ] 5.3.2 캔버스 상단에 격리 네비게이션 바(빵부스러기: Breadcrumbs) 표시
- [ ] 5.3.3 격리 대상 외 나머지 객체들은 딤(Dimming/반투명) 처리 및 마우스 인터랙션 차단
- [ ] 5.3.4 ESC 키 또는 캔버스 바깥 더블클릭 시 상위 레벨로 복귀
- [ ] 5.3.5 빌드 확인 및 그룹 내부 도형 직접 편집 검증

### Step 5.4 클립보드 (SVG 교환)
- [ ] 5.4.1 선택된 노드들을 메모리 상의 SVG 문자열로 직렬화하여 Windows 클립보드(`CF_UNICODETEXT` 및 `image/svg+xml`)에 복사(Ctrl+C)
- [ ] 5.4.2 클립보드에서 SVG 데이터를 읽어와 현재 문서 캔버스에 붙여넣기(Ctrl+V)
- [ ] 5.4.3 Illustrator, Figma, 브라우저 등 외부 애플리케이션과의 양방향 복사/붙여넣기 테스트
- [ ] 5.4.4 빌드 확인 및 외부 툴 연동 검증

### Step 5.5 PNG/JPG/PDF 래스터 출력
- [ ] 5.5.1 해상도(DPI) 및 배율(1x, 2x, 4x) 지정이 가능한 내보내기 대화상자 구현
- [ ] 5.5.2 WIC 기반 오프스크린 렌더 타깃(`ID2D1BitmapRenderTarget`)에 고해상도 렌더링
- [ ] 5.5.3 WIC 인코더를 통해 투명 배경 PNG 및 무손실 JPG 파일 저장
- [ ] 5.5.4 (선택) Direct2D Print Control을 이용한 PDF 문서 출력
- [ ] 5.5.5 빌드 확인 및 출력된 고해상도 이미지 파일 품질 검증

### Step 5.6 페인트통 도구 (PaintBucketTool)
- [ ] 5.6.1 캔버스 클릭 지점의 닫힌 교차 영역 탐색 (불리언 Divide 또는 지오메트리 패스 분석)
- [ ] 5.6.2 탐색된 경계 영역을 독립된 신규 폐곡선 `PathNode`로 생성
- [ ] 5.6.3 현재 활성화된 채우기 스타일(Fill Paint)을 즉시 적용하여 문서에 추가
- [ ] 5.6.4 빌드 확인 및 선으로 둘러싸인 빈 공간 클릭 채우기 검증

