# M2Draw 벡터 그래픽스 확장 아키텍처 및 구현 계획

이 문서는 M2Draw의 현재 오브젝트 저장 구조와 데이터 흐름을 분석하고, 향후 예정된 **고급 벡터 연산(불리언/트림/노드편집)** 및 **관리 기능(그룹핑/Z-Order/정렬)**의 구현 계획을 정리한 문서입니다.

> 본 문서의 구현 우선순위는 렌더링 기능보다 문서 모델, 좌표계, 편집 명령, 선택/히트테스트를 먼저 안정화하는 것을 원칙으로 합니다. SVG 호환성은 지원 범위를 단계적으로 확대하며, 지원하지 않는 SVG 기능은 조용히 삭제하지 않고 보존 또는 경고합니다.

---

## 1. 현재 오브젝트 데이터 관리 및 정렬 방식

### 1.1 저장 위치 및 자료구조
- **문서 단위 저장소**: `CM2DrawDoc`
  - 변수명: `std::vector<std::unique_ptr<Shape>> m_shapes;`
  - 설명: 다형성(Polymorphism) 기반의 `Shape` 추상 클래스 포인터 벡터입니다.
- **오브젝트 계층 구조**:
  ```text
  CM2DrawDoc
   └── m_shapes: std::vector<std::unique_ptr<Shape>>
        └── PathShape (Shape 상속)
             ├── m_xf: D2D1::Matrix3x2F (로컬 변환 행렬: 이동, 회전, 스케일)
             ├── m_style: ShapeStyle (Fill, Stroke, StrokeWidth, Dash)
             ├── m_geo: ComPtr<ID2D1PathGeometry> (D2D 캐시 기하학)
             └── m_paths: std::vector<SubPath> (기하학 정점 데이터)
                  └── SubPath (서브패스)
                       ├── closed: bool (닫힌 패스 여부)
                       └── nodes: std::vector<BezierNode>
                            └── BezierNode (각 앵커 포인트 및 베지에 핸들)
                                 ├── anchor: D2D1_POINT_2F (기준 앵커 점)
                                 ├── hIn:    D2D1_POINT_2F (In 방향 제어점 핸들 오프셋)
                                 ├── hOut:   D2D1_POINT_2F (Out 방향 제어점 핸들 오프셋)
                                 └── type:   BezierNode::Type (Corner / Smooth / Symmetric)
  ```

### 1.2 Z값(Z-Order) 및 렌더링/선택 순서
- **정렬 방식**: 배열 인덱스 순서 기반 Z-Order
  - `m_shapes[0]`: 최하단 레이어 (가장 먼저 그려져서 밑에 깔림)
  - `m_shapes[m_shapes.size() - 1]`: 최상단 레이어 (가장 마지막에 그려져 위에 덮음)
- **렌더링 순서**: `0`번부터 `N-1`번까지 순차 렌더링 (Painter's Algorithm)
- **마우스 히트테스트(선택)**: `N-1`번부터 `0`번으로 역순(Reverse) 순회하여 맨 위에 보이는 도형이 우선 선택됨

### 1.3 권장 문서 계층 및 객체 식별
- 장기적으로 문서 구조는 `Document -> Layer -> Shape/GroupShape` 계층으로 확장합니다.
- 각 도형에는 복사, 선택, 저장, Undo/Redo를 위한 안정적인 `ShapeId`를 부여합니다.
- `std::unique_ptr`는 소유권 관리에 사용하고, 외부 참조는 포인터 대신 `ShapeId` 또는 명시적인 부모 참조를 사용합니다.
- `GroupShape`는 중첩 그룹을 지원하며, 그룹의 자식 순서가 그룹 내부의 Z-Order가 됩니다.

### 1.4 좌표계 및 변환 규칙
- 도형은 로컬 좌표계에서 데이터를 보유하고, 렌더링 시 부모 변환을 포함한 월드 변환을 계산합니다.
- 변환 합성 규칙은 다음과 같이 통일합니다.
  ```text
  WorldTransform = ParentWorldTransform * LocalTransform
  ```
- 그룹 해제 시 자식의 월드 위치를 유지해야 하므로 새 부모 기준의 로컬 변환은 다음과 같이 계산합니다.
  ```text
  NewLocalTransform = Inverse(NewParentWorldTransform) * OldWorldTransform
  ```
- 로컬 바운딩 박스, 스트로크를 포함한 바운딩 박스, 월드 바운딩 박스를 구분합니다.

### 1.5 기반 편집 기능
고급 기능을 구현하기 전에 다음 공통 인프라를 완료합니다.
- 선택 모델: 단일/다중 선택, 그룹 내부 선택, 잠금 및 숨김 상태
- 히트테스트: Fill, Stroke, 그룹, 변환 행렬을 고려한 역변환 기반 판정
- 변환: 이동, 크기 조절, 회전, 기준점 변경
- 뷰: 확대/축소, 패닝, 문서 좌표와 화면 좌표 변환
- 캐시: `ID2D1PathGeometry` 무효화 및 재생성 정책
- 명령 기반 편집: 모든 문서 변경을 Undo/Redo 가능한 명령으로 처리

예정된 명령 예시는 다음과 같습니다.
`CreateShapeCommand`, `DeleteShapeCommand`, `TransformShapeCommand`, `GroupCommand`, `UngroupCommand`, `EditPathCommand`, `BooleanOperationCommand`.

---

## 2. 향후 고급 벡터 연산 설계 계획

### 2.1 불리언 연산 (합치기 / 빼기 / 교차 / 배타적 분할)
- **Direct2D 네이티브 엔진 활용**:
  - `ID2D1Geometry::CombineWithGeometry` 인터페이스 사용
  - 지원 연산 모드:
    - `D2D1_COMBINE_MODE_UNION`: 합집합 (오브젝트 합치기)
    - `D2D1_COMBINE_MODE_EXCLUDE`: 차집합 (오브젝트 빼기)
    - `D2D1_COMBINE_MODE_INTERSECT`: 교집합
    - `D2D1_COMBINE_MODE_XOR`: 배타적 논리합
- **구현 프로세스**:
  1. 선택된 대상 도형을 월드 좌표계로 평가합니다. 그룹 내부 도형도 누적 부모 변환을 포함해야 합니다.
  2. `CombineWithGeometry`를 호출하여 합성된 신규 `PathGeometry` 추출.
  3. Geometry Sink를 통해 새 지오메트리의 figure와 세그먼트를 `SubPath`로 역변환합니다. 결과는 여러 개의 독립 외곽선 또는 구멍을 포함할 수 있으므로 하나의 `PathShape`가 여러 `SubPath`를 보유해야 합니다.
  4. 기존 참여 도형들을 `m_shapes`에서 제거하고 새 단일 `PathShape`로 교체 등록.
- Boolean 결과는 원본 노드 구조와 동일하다고 가정하지 않습니다. Direct2D의 결과에는 선분 추가, figure 분리, 경로 방향 변경, 수치 오차가 발생할 수 있습니다.
- `FillRule`, figure 방향, 열린/닫힌 상태를 함께 보존하고, 실패 또는 빈 결과는 원본을 삭제하지 않고 사용자에게 오류를 알립니다.

### 2.2 패스 트림(Trim) 및 절단(Cut/Divide)
- **교차점 분할 및 곡선 세그먼트 분할**:
  - 두 패스 간 교차점을 계산하여 해당 위치에 신규 앵커 노드를 삽입.
  - 베지에 곡선의 경우 De Casteljau 알고리즘을 이용해 특정 파라미터 $t$ ($0 \le t \le 1$) 위치에서 곡선을 분할하여 정확한 In/Out 핸들 계산.
  - 트림 구간의 노드를 제거하거나 서브패스를 분리하여 개별 도형으로 나눔.

### 2.3 포인트/핸들 추가·삭제 (노드 편집 툴)
- 이미 `BezierNode`에 `anchor`, `hIn`, `hOut`, `type`이 모두 갖추어져 있음.
- **포인트 추가**: 세그먼트 클릭 지점의 $t$ 값을 기반으로 `std::vector<BezierNode>` 사이에 `insert` 후 `Invalidate()`.
- **포인트 삭제**: 선택된 앵커 노드를 `nodes.erase()` 후 인접 노드 간 재연결.
- **핸들 조작**: 직접 선택 도구(Direct Selection Tool)를 통해 In/Out 핸들의 대칭성(Symmetric), 부드러움(Smooth), 꺾임(Corner) 모드를 유지하며 드래그 제어.

### 2.4 Path 데이터 모델 보완
- `SubPath`는 `closed` 외에 경로 방향과 Fill Rule을 지원합니다.
- 향후 SVG 아크와 원본 세그먼트 보존을 고려하여 세그먼트 타입을 구분합니다.
  - `Line`, `CubicBezier`, `QuadraticBezier`, `Arc`
- Boolean 결과의 안정적인 처리를 위해 하나의 `PathShape`가 여러 `SubPath`를 보유하는 현재 구조를 유지합니다.
- 노드 편집 후에는 기하학 캐시를 무효화하고, 바운딩 박스와 선택 인덱스를 함께 갱신합니다.

---

## 3. 그룹핑 및 정렬(Align & Z-Order) 구현 계획

### 3.1 그룹핑 (Grouping / Ungrouping)
- **설계 구조**: 컴포지트 패턴(Composite Pattern) 도입
  - `Shape`를 상속받는 `GroupShape` 클래스 신설:
    ```cpp
    class GroupShape : public Shape
    {
    public:
        std::vector<std::unique_ptr<Shape>> m_children;
        // Group 자체 변환 행렬(m_xf)이 자식들의 렌더링/히트테스트 시 계층적으로 곱해짐
    };
    ```
- **그룹화 (Ctrl + G)**:
  1. 선택된 $N$개의 도형을 `m_shapes`에서 분리하여 `GroupShape::m_children`으로 이동.
  2. 전체 자식들의 월드 바운딩 박스를 계산하여 그룹의 기준점으로 설정합니다. 그룹 생성 후에도 자식의 화면상 위치와 방향은 변하지 않아야 합니다.
  3. `m_shapes`의 해당 인덱스 자리에 `GroupShape` 삽입.
- **그룹 해제 (Ctrl + Shift + G)**:
  1. `GroupShape` 내부의 자식을 꺼내며, 각 자식의 기존 월드 변환을 계산한 뒤 새 부모 기준의 로컬 변환으로 환원합니다. 단순히 `m_xf`를 복사하면 중첩 그룹, 회전, 비균일 스케일에서 위치가 달라질 수 있습니다.
  2. `GroupShape`를 제거하고 그 자리에 자식 도형들을 복원.

### 3.2 Z-Order 순서 변경
`m_shapes` 벡터 내 원소들의 위치를 재배치:
- **맨 앞으로 가져오기 (Bring to Front)**: 선택 원소를 벡터 끝(`m_shapes.end() - 1`)으로 이동.
- **맨 뒤로 보내기 (Send to Back)**: 선택 원소를 벡터 시작(`m_shapes.begin()`)으로 이동.
- **한 단계 앞으로 (Bring Forward)**: 인덱스 $i$와 $i+1$ 원소 간 `std::swap`.
- **한 단계 뒤로 (Send Backward)**: 인덱스 $i$와 $i-1$ 원소 간 `std::swap`.

### 3.3 캔버스 및 오브젝트 기준 정렬 (Align & Distribute)
- **바운딩 박스 인프라**: 각 도형의 `GetLocalBounds`와 `m_xf`를 통해 정밀한 Bounding Rect(`left, right, top, bottom, midX, midY`) 취득.
- **정렬 모드**:
  1. **Align to Selection**: 선택된 다중 오브젝트들의 결합 바운딩 박스 기준.
  2. **Align to Canvas**: 문서 캔버스 경계(`0, 0, Width, Height`) 기준.
- **6대 정렬 연산**:
  - 좌측 정렬 / 수평 가운데 정렬 / 우측 정렬
  - 상단 정렬 / 수직 가운데 정렬 / 하단 정렬
- **균등 분할 (Distribute Spacing)**:
  - 3개 이상의 오브젝트 선택 시, 양 끝 오브젝트 사이의 간격을 계산하여 중간 오브젝트들의 위치를 일정하게 재배치.

추가로 다음 기준을 정의합니다.
- 선택 영역 기준, 캔버스/아트보드 기준, 키 오브젝트 기준을 구분합니다.
- 정렬 대상 바운딩 박스는 기하학 경계와 Stroke 포함 경계를 구분합니다.
- 균등 분할은 오브젝트 중심 간격 균등과 오브젝트 사이 빈 공간 균등을 별도 연산으로 제공합니다.

---

## 4. SVG 입출력 및 호환성 계획

### 4.1 단계별 지원 범위
“손실 없는 SVG 호환”은 SVG 전체 기능을 의미하지 않도록 지원 수준을 정의합니다.

#### SVG Lite
- `path`, `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon`
- 그룹과 변환 행렬
- 단색 Fill/Stroke, 투명도, Stroke Width
- 대시, 선 끝, 선 연결, `fill-rule`

#### SVG Standard
- 선형/방사형 그라디언트
- 클리핑 패스와 마스크
- CSS 스타일 상속
- SVG 아크 세그먼트
- 패턴 및 심볼의 제한적 지원

#### SVG Advanced
- 텍스트와 폰트
- 필터
- 외부 리소스
- `use`, 애니메이션 및 기타 고급 요소

초기 상용 버전의 목표는 SVG Lite와 일부 SVG Standard로 제한하고, 미지원 요소는 Import 결과에 경고를 표시합니다.

### 4.2 Import/Export 원칙
- NanoSVG와 같은 파서는 기본 도형을 빠르게 읽는 데 사용하되, 원본 SVG의 모든 정보를 보존한다고 가정하지 않습니다.
- 내부 렌더 모델과 별도로 지원하지 않는 원본 속성을 보존하는 Metadata Model을 둘 수 있습니다.
- Export 시 Fill Rule, Stroke Cap/Join, Miter Limit, Dash Array/Offset, Fill/Stroke Opacity, Clip Path를 포함합니다.
- Import 후 Export에서 원본 XML과 byte 단위로 동일한 것을 목표로 하지 않고, 지원 범위 내에서 시각적·기하학적 동등성을 목표로 합니다.
- 잘못된 SVG, 매우 큰 좌표, NaN/Infinity, 순환 참조를 방어하고 오류를 문서에 기록합니다.

---

## 5. 권장 단계별 개발 순서

### Phase 0: 기반 모델
- Shape ID, Document/Layer/Group 구조
- 로컬/월드 좌표계와 변환 합성
- Style, Fill Rule, 바운딩 박스
- 선택 및 히트테스트
- Undo/Redo 명령 스택

### Phase 1: 기본 편집
- 이동, 복사, 삭제, 변환
- 그룹/그룹 해제
- Z-Order
- 확대/축소, 패닝, 스냅

### Phase 2: Path 및 노드 편집
- 펜 툴과 Path 생성
- 노드/핸들 선택 및 편집
- 노드 추가/삭제
- 경로 닫기/열기와 세그먼트 분할

### Phase 3: SVG Lite
- 기본 도형 및 Path Import
- 그룹, 변환, 기본 스타일 Import/Export
- 지원되지 않는 요소 경고

### Phase 4: 정렬 및 배치
- 6대 정렬
- 캔버스 및 키 오브젝트 기준 정렬
- 수평/수직 간격 분배

### Phase 5: Pathfinder
- Union, Difference, Intersect, XOR
- Divide, Trim, Expand Stroke, Offset Path
- Geometry 역변환 및 Boolean 실패 처리

### Phase 6: SVG 확장
- 그라디언트, 클리핑, 마스크, 패턴
- 텍스트와 CSS 스타일
- 제한적 필터 및 심볼 지원

---

## 6. 품질 관리 및 테스트 계획

### 6.1 단위 및 통합 테스트
- 행렬 곱셈, 역행렬, 로컬/월드 변환
- 중첩 그룹 생성/해제 후 월드 위치 유지
- 회전 및 비균일 스케일
- Path 세그먼트와 De Casteljau 분할
- 열린 경로, 닫힌 경로, 복수 SubPath, 자기 교차 경로
- Fill Rule과 Stroke Bounds
- Boolean의 비교차, 포함, 동일, 접점, 다중 교차, 구멍 결과
- Import -> Export -> Reimport 결과 비교
- Undo/Redo 후 문서 상태와 선택 상태 복원

### 6.2 성능 및 안정성
- 1,000개 및 10,000개 도형 렌더링
- 복잡한 SVG Import와 Geometry 캐시 재생성
- 연속 Boolean 및 노드 편집
- 캐시 무효화 비용과 메모리 사용량
- 렌더링 실패, Boolean 실패, 잘못된 입력에 대한 복구
- 장시간 편집 중 메모리 누수와 COM 리소스 수명 검사

### 6.3 문서 및 복구
- 자체 프로젝트 파일 포맷과 버전 필드
- 자동 저장 및 복구 파일
- 이미지·폰트 외부 리소스 경로 정책
- 저장 전후 Shape ID, 변환, 스타일, 선택 상태 검증

---

## 7. 주요 위험 요소 및 대응

| 위험 요소 | 대응 |
|---|---|
| Direct2D Boolean 결과의 노드 구조 변화 | 다중 SubPath와 Fill Rule을 지원하고 결과를 새 Path로 취급 |
| 그룹 해제 후 위치 오차 | 항상 월드 변환을 계산한 뒤 새 부모 기준으로 역변환 |
| SVG 기능 손실 | 지원 수준을 정의하고 Metadata 보존 또는 사용자 경고 |
| Undo/Redo 누락 | 문서 변경을 명령 객체를 통해서만 수행 |
| Stroke/Transform 바운딩 박스 불일치 | Geometry, Stroke, Visible Bounds를 구분 |
| 대용량 문서 성능 저하 | Geometry 캐시, 변경 시 선택적 무효화, 렌더링 범위 제한 |
| 부동소수점 오차와 잘못된 입력 | 허용 오차 정책, NaN 검사, 실패 시 원본 보존 |

---

## 8. 완료 기준

각 단계는 기능 구현만으로 완료하지 않고 다음 조건을 만족해야 합니다.

- 정상 경로와 실패 경로에 대한 테스트가 존재할 것
- Undo/Redo가 동작할 것
- 저장 후 재로드해도 문서 구조와 변환이 유지될 것
- 렌더링과 히트테스트 결과가 동일한 좌표계 정책을 사용할 것
- 지원하지 않는 입력을 사용자에게 명확히 알릴 것
- 성능 및 메모리 회귀 테스트를 통과할 것

---

## 9. 현재 구현 기준 기능 감사

현재 소스의 엔진, 아트 툴, 프로퍼티 패널, 컬러휠을 기준으로 확인한 기능은 다음과 같습니다.

### 9.1 엔진 및 문서 모델
관련 파일: `D2DVectorEngine.h`, `M2DrawDoc.h`, `M2DrawView.h`

현재 구현된 기반 기능:
- `Shape` 추상 클래스와 `PathShape` 중심의 다형성 구조
- `std::vector<std::unique_ptr<Shape>>` 기반 도형 소유권 관리
- `BezierNode`, `SubPath`, 핸들 타입(`Corner`, `Smooth`, `Symmetric`)
- 로컬 좌표와 화면 좌표 사이의 변환
- 줌, 팬, 마우스 위치 기준 줌, 사각형 영역 줌
- Fill/Stroke 렌더링과 Direct2D Geometry 캐시
- 선택 결과를 Fill, Stroke, Anchor, Handle로 구분하는 `HitResult`
- Grid, Ruler, Snap Manager와 단위 변환(px, mm, cm, inch, rem)

보완이 필요한 부분:
- `Shape`에 고유 ID, 부모 그룹 ID, 표시/잠금 상태가 없습니다. 현재 프로퍼티 패널이 `shapeIndex`로 대상을 추적하므로 벡터 재배치 후 잘못된 객체를 수정할 위험이 있습니다. 인덱스 대신 `ShapeId` 기반 참조로 전환합니다.
- `CM2DrawDoc`에 현재 작성 중인 `m_currentPath`가 별도로 존재합니다. 작성 중 상태와 저장 가능한 문서 상태를 분리하고, 확정/취소/복구 수명 주기를 명확히 정의해야 합니다.
- `GetStyleRef()` 및 공개 호환 레퍼런스(`name`, `xf`, `style`)를 통한 직접 변경은 변경 추적과 Undo/Redo를 우회할 수 있습니다. 최종적으로는 명령 또는 검증된 setter를 통해 변경합니다.
- `ShapeStyle`에는 `None`, `Solid`, `Pattern`만 정의되어 있지만 프로퍼티 UI에는 선형 그라디언트와 방사형 그라디언트가 노출되어 있습니다. 엔진 모델과 UI 선택지를 일치시키거나 미구현 항목을 비활성화해야 합니다.
- 현재 스타일에는 전역 불투명도, Stroke Cap, Line Join, Miter Limit, Dash Array/Offset, Fill Rule이 부족합니다.
- `Draw()`는 패턴을 실제 브러시로 렌더링하지 않고 반투명 단색으로 미리보기합니다. UI에서 패턴을 선택할 수 있다면 지원 상태를 명확히 표시해야 합니다.
- 현재 기하학 경계와 Stroke를 포함한 시각적 경계를 구분하지 않습니다. 정렬, 선택 핸들, 내보내기에는 `GeometryBounds`, `StrokeBounds`, `VisibleBounds`를 분리합니다.
- Direct2D 장치 리소스가 폐기되거나 재생성될 때 Geometry 캐시와 브러시/StrokeStyle 캐시를 함께 재생성하는 정책이 필요합니다.

### 9.2 아트 툴 및 캔버스 상호작용
관련 파일: `M2DrawView.cpp`, `M2DrawView.h`, `SnapManager.*`, `GridRenderer.*`

현재 구현 방향:
- 활성 도구를 `ToolManager`로 관리
- `Select`, `Pan`과 도형 작성 도구를 구분
- 도구별 마우스 다운/이동/업 및 더블클릭/우클릭 종료 흐름
- 선택 도형의 변형 핸들 오버레이 렌더링
- 중간 마우스 팬, 마우스 휠 줌, 돋보기 영역 줌
- 그리드 표시, 주/보조 격자, 좌표축, 단위 변경, 그리드 스냅

아트 툴 확장 계획:
- 도구 공통 인터페이스에 `Begin`, `Update`, `Commit`, `Cancel` 상태를 명시하고 모든 드래그 작업을 하나의 Undo 명령으로 묶습니다.
- 선택 도구에 단일 선택, 다중 선택, 올가미/영역 선택, 그룹 내부 진입, 잠금 객체 건너뛰기를 추가합니다.
- 변형 도구에 Shift 비율 유지, Alt 중심 기준, Ctrl 축 제한, 회전 스냅, 기준점 이동을 추가합니다.
- 펜 도구에 클릭/드래그 구분, 자동 경로 닫기, 마지막 노드 취소, 핸들 독립 편집을 추가합니다.
- 직접 선택 도구를 별도 편집 모드로 만들고 Anchor/Handle/Segment의 선택 상태를 문서 선택 상태와 분리합니다.
- 선, 사각형, 타원, 다각형, 별, 자유곡선 등 생성 도구는 공통 프리뷰 레이어를 사용하며 마우스 이동 중 문서에 임시 객체를 삽입하지 않습니다.
- 스냅 대상을 Grid뿐 아니라 도형의 앵커, 핸들, 경로, 바운딩 박스, 중심점, 가이드로 확장합니다.
- 스냅 표시기와 현재 커서 위치의 월드 좌표 표시를 추가합니다.
- 모든 도구는 `Escape` 취소, `Enter` 확정, 우클릭 종료, 포커스 상실 시 처리 규칙을 동일하게 적용합니다.
- 도구 전환 시 작성 중인 경로를 자동 확정하지 말고 사용자 설정에 따라 확정/취소를 선택하도록 합니다.

#### ITool 및 ToolManager 구현 감사
관련 파일: `ITool.h`, `ITool.cpp`, `ToolManager.h`, `ToolManager.cpp`

현재 구현된 구조:
- `ITool`이 도구 종류, 이름, 단축키, 커서, 마우스/키보드 이벤트, 오버레이 렌더링을 공통 인터페이스로 제공합니다.
- `ToolEvent`가 화면 좌표, 월드 좌표, 단위 좌표, 수정키, 뷰포트, 문서/뷰/Direct2D 팩토리를 도구에 전달합니다.
- `SelectTool`이 선택, 이동, 크기 조절, 회전, 노드/핸들 편집을 담당합니다.
- `BaseShapeTool`이 도형 작성 도구의 프리뷰, 확정, 취소를 공통화합니다.
- `PenTool`은 `Idle`, `Placing`, `Pressed`, `DraggingHandle` 상태로 베지어 패스를 작성합니다.
- `ToolManager`가 도구 인스턴스, 현재/이전 도구, 임시 Pan, 단축키, 도구 변경 리스너를 관리합니다.
- `Line`, `Rect`, `Ellipse`, `Polygon`, `Text`, `PaintBucket`, `Zoom` 도구의 확장 지점이 이미 정의되어 있습니다.

현재 구조에서 확인된 보완 사항:
- 선택 대상이 `std::set<int>` 인덱스로 저장됩니다. 그룹핑, Z-Order 변경, 삭제 후에도 선택 대상을 유지하려면 `ShapeId` 기반의 `SelectionModel`로 교체해야 합니다.
- `ToolEvent`가 `CM2DrawDoc*`, `CM2DrawView*`, `ID2D1Factory*`를 직접 보유합니다. 도구 테스트와 수명 안전성을 위해 문서/뷰 서비스 인터페이스 또는 명시적인 `ToolContext`로 감싸는 것이 좋습니다.
- `OnDown`, `OnMove`, `OnUp`, `OnDoubleClicked`, `OnRightClicked`만으로는 드래그 작업의 확정/취소/실패 상태가 충분히 표현되지 않습니다. `BeginInteraction`, `UpdateInteraction`, `CommitInteraction`, `CancelInteraction` 상태를 추가합니다.
- 현재 `PenTool`이 `CM2DrawDoc::m_currentPath`를 직접 생성하고 `m_shapes`에 직접 이동시킵니다. 이 동작은 Undo/Redo, 문서 Dirty 상태, 선택 알림, 속성 패널 갱신을 우회하므로 `CreatePathCommand` 또는 `CommitToolOperationCommand`를 통해 확정해야 합니다.
- 펜 도구의 Enter, 더블클릭, 우클릭 완료 경로가 유사한 로직을 중복 구현합니다. 공통 `CommitCurrentPath()`와 `CancelCurrentPath()`로 통합하고, 노드가 하나뿐인 경로·중복 노드·열린/닫힌 경로의 규칙을 한 곳에서 관리합니다.
- `ToolCreationSetting`은 공통 스타일을 보유하지만 모든 도형 도구가 동일하게 적용되는지 보장되지 않습니다. 새 도형 생성 시 Fill, Stroke, Width, Dash, Fill Rule, Opacity를 일관되게 복사하는 팩토리를 둡니다.
- `ToolType`에는 Text와 PaintBucket이 있지만 현재 구현은 이름, 단축키, 커서 중심의 골격입니다. 미구현 도구는 UI에서 활성화하지 않거나 기능 상태를 `Planned/Available`로 표시해야 합니다.
- 임시 도형과 오버레이는 문서 도형 목록에 삽입하지 않는 현재 방향을 유지하되, 렌더링 프리뷰와 실제 문서 객체의 스타일·좌표·변환을 동일한 코드 경로에서 계산해야 합니다.
- 마우스 캡처, 포커스 상실, 창 크기 변경, 도구 전환 중 버튼 해제와 같은 예외 입력을 명시적으로 처리해야 합니다. 그렇지 않으면 `m_mode` 또는 Pen 상태가 계속 드래그 중으로 남을 수 있습니다.
- `SelectTool`의 다중 선택은 인덱스 집합으로만 표현되므로 각 선택 객체의 초기 변환, 이동 기준점, 변형 기준점을 별도로 저장해야 합니다. 다중 이동/스케일/회전은 선택 객체 전체에 동일한 월드 변환을 적용해야 합니다.
- 선택 도구의 핸들 판정 허용 오차는 화면 픽셀 기준으로 유지하고, 실제 노드 이동량은 월드 또는 도형 로컬 좌표로 변환하는 현재 원칙을 문서화합니다.
- 단축키는 대문자/소문자, 한글 입력 상태, Ctrl/Shift/Alt 조합, 도구 작성 중 입력을 구분해야 합니다. 임시 Pan은 Space 키의 누름/해제를 안전하게 중첩 처리합니다.

권장 ITool 확장 형태:
```cpp
class ITool
{
public:
    virtual ~ITool() = default;
    virtual ToolType GetType() const = 0;
    virtual void OnEnter(ToolContext&) {}
    virtual void OnLeave(ToolContext&) { Cancel(ToolContext{}); }
    virtual void Begin(const ToolEvent&) {}
    virtual void Update(const ToolEvent&) {}
    virtual void Commit(ToolContext&) {}
    virtual void Cancel(ToolContext&) {}
    virtual bool HasActiveInteraction() const { return false; }
};
```

기존 이벤트 함수와 즉시 교체하기보다는 어댑터를 두고 단계적으로 이전합니다. 핵심은 도구가 문서를 직접 수정하는 것이 아니라 `ToolManager` 또는 `DocumentCommandService`를 통해 변경하도록 하는 것입니다.

추가해야 할 도구 테스트:
- 펜 클릭/드래그/더블클릭/우클릭/Enter/Escape
- 첫 노드 닫기와 최소 노드 수
- 도구 전환 중 작성 중 경로 처리
- 선택 다중 이동/스케일/회전과 Ctrl/Shift/Alt 제약
- 노드/핸들 히트테스트와 줌 배율별 허용 오차
- 마우스 캡처 해제와 포커스 상실
- 도구 작업 1회당 Undo/Redo 1회 생성

### 9.3 프로퍼티 패널
관련 파일: `D2DPropertyView.h`, `D2DPropertyView.cpp`

현재 제공되는 패널:
- Shape, Grid, Canvas의 3개 모드
- Fill/Stroke 대상 탭
- Fill None/Solid/Linear Gradient/Radial Gradient/Pattern UI 슬롯
- 색상 팔레트, 컬러휠, 불투명도, Stroke Width, 대시 스타일
- 경로 닫기 옵션
- Grid/Ruler 표시와 스냅, 단위, Step, Major Every, 색상, 투명도
- Canvas Width/Height 및 배경색

우선 보완 사항:
- `SetTargetShape()`와 `ApplyToShape()`가 단일 `shapeIndex`만 사용합니다. 다중 선택 시 공통값, 혼합값(`Mixed`), 일괄 적용을 지원하고 ID 기반으로 변경합니다.
- 현재 속성값을 직접 도형에 대입하고 `UpdateAllViews()`를 호출하므로 변경 단위가 명확하지 않습니다. 입력 시작 시 이전 상태를 저장하고 입력 완료 시 `ChangeStyleCommand`를 생성합니다.
- `m_fillMode`가 실제 적용 과정에서 Solid로 고정되는 경로가 있으므로 UI와 엔진의 FillType을 연결합니다. 미구현 그라디언트/패턴은 임시로 선택 불가 처리하거나 실제 데이터 구조를 먼저 추가합니다.
- Fill과 Stroke의 알파를 별도 관리하고, 오브젝트 전체 불투명도와 구분합니다.
- Stroke Width의 단위 변환은 문서 단위와 화면 DPI를 분리하여 저장합니다. 표시 단위가 바뀌어도 실제 문서 값이 의도하지 않게 변하지 않아야 합니다.
- 이름, 위치 X/Y, 폭/높이, 회전, 기준점, 잠금, 표시, 블렌드 모드, Fill Rule, Stroke Cap/Join을 추가합니다.
- 여러 도형 선택 시 각 필드에 공통값과 혼합값을 표시하고, 입력 중 선택 대상이 사라지거나 순서가 바뀌어도 안전하게 처리합니다.
- 속성 변경은 문서 모델이 단일 진실 공급원(Source of Truth)이 되도록 하고, 패널 내부의 복제 상태는 임시 편집 버퍼로 제한합니다.
- Grid와 Canvas 설정도 앱 전역 설정과 문서 설정을 구분합니다. 문서를 다시 열었을 때 캔버스 크기, 배경, 그리드 원점과 단위가 유지되어야 합니다.

### 9.4 컬러휠 및 색상 시스템
관련 파일: `D2DColorWheel.h`, `D2DColorWheel.cpp`

현재 구현된 기능:
- Hue 링과 내부 Saturation/Lightness 삼각형
- HSL <-> RGB 변환
- 마우스 드래그 중 변경 및 최종 변경 콜백
- 공통 슬라이더, 색상 바, 세그먼트 버튼 HitTest/렌더링 보조 함수

추가 및 개선 계획:
- `D2DPropertyView.cpp`에도 HSL/RGB 변환 코드가 중복되어 있으므로 색상 모델을 하나의 `ColorModel` 또는 `ColorUtils`로 통합합니다.
- HSL만으로는 디자이너 작업에 부족하므로 HSV/HSB, RGB, 8비트 RGB, HEX, 알파 입력을 추가합니다.
- 회색 계열에서 Hue를 0으로 재설정하지 않고 이전 Hue를 유지하는 정책을 검토합니다. 색상휠에서 명도만 변경할 때 색상 의도가 유지되어야 합니다.
- 변환 입력과 출력에 대해 0~1 범위 Clamp, NaN/Infinity 검사, Hue Wrap을 공통 적용합니다.
- 현재 렌더링은 Draw 호출마다 Hue 링의 다수 Geometry를 생성하므로 링 Geometry와 브러시를 크기/테마별로 캐시합니다. Direct2D 장치 손실 시 캐시를 폐기합니다.
- 마우스 좌표를 DPI와 패널 클라이언트 좌표 기준으로 통일하고, 컬러휠 크기 변경 시 삼각형 선택점이 범위를 벗어나지 않도록 Clamp합니다.
- 키보드 접근성, 숫자 입력, 드래그 중 미리보기, 취소, Enter 확정, 포커스 상실 처리와 색상 변경 Undo 단위를 추가합니다.
- 현재 색상/이전 색상 비교, 투명 체커보드 배경, 최근 색상, 사용자 팔레트, 스포이드 도구를 추가합니다.
- 색상 프로파일과 렌더링 색공간은 초기에는 sRGB로 고정하되, 문서 포맷에 색공간 필드를 예약합니다.

### 9.5 엔진-도구-프로퍼티 연결 원칙
현재처럼 각 UI가 도형을 직접 수정하면 기능이 늘어날수록 동기화 문제가 발생합니다. 다음 흐름으로 통일합니다.

```text
Tool / Property / Menu
        |
        v
Document Command (Undo/Redo)
        |
        v
Document Model (Shape, Style, Selection, Canvas)
        |
        +--> Invalidate Geometry/Bounds
        +--> Notify Selection/Property Observers
        +--> Update Views
```

- 도구와 프로퍼티 패널은 `CM2DrawDoc`를 직접 임의 변경하지 않고 명령을 호출합니다.
- 문서 변경 후 Geometry, Bounds, Selection, Property Panel을 순서대로 통지합니다.
- 렌더링은 문서 모델을 읽기만 하며, 렌더링 도중 문서 데이터를 변경하지 않습니다.
- 선택은 Shape 인덱스가 아닌 `ShapeId`와 필요 시 `SubPath/Node` 식별자로 저장합니다.
- UI 이벤트가 연속적으로 발생하는 동안에는 Preview 상태를 사용하고, 마우스 업 또는 Enter 시 하나의 편집 명령으로 확정합니다.

---

## 10. 기능 감사 기반 추가 개발 우선순위

### 10.1 최우선: 현재 UI와 엔진의 불일치 제거
1. `ShapeStyle`에 있는 실제 지원 기능과 프로퍼티 UI 선택지를 일치시킵니다.
2. 프로퍼티의 직접 인덱스 접근을 `ShapeId`와 문서 명령으로 교체합니다.
3. 중복된 색상 변환 코드를 통합하고 색상/알파 저장 규칙을 확정합니다.
4. 속성 변경, 도구 드래그, 컬러휠 드래그에 Undo/Redo를 연결합니다.

### 10.2 다음 단계: 편집기 사용성
1. 다중 선택과 혼합 속성 표시
2. 오브젝트 이름/잠금/가시성 및 레이어 패널
3. 기준점, 회전, 비율 고정, 키보드 이동
4. 앵커/경로/가이드/중심점 스냅
5. 선택 프리뷰, 드래그 프리뷰, 취소/확정 상태 통일

### 10.3 이후 단계: 상용 벡터 기능
1. 그라디언트와 패턴을 실제 Direct2D Brush로 구현
2. 커스텀 대시 배열, Stroke Cap/Join, Fill Rule
3. Path 노드/세그먼트 편집
4. Boolean, Divide, Trim, Offset, Expand Stroke
5. SVG Lite Import/Export와 미지원 기능 보고

---

## 11. SnapManager 및 RulerRenderer 기능 감사

### 11.1 SnapManager
관련 파일: `SnapManager.h`, `SnapManager.cpp`

현재 구현된 기능:
- 비트마스크 기반 스냅 대상 관리
- `Grid`, `OriginAxis`, `Anchor`, `Midpoint`, `Center` 스냅
- 줌 배율에 따른 화면 픽셀 허용 오차의 월드 좌표 환산
- 제외 도형을 지정할 수 있는 `FindSnapPoint()` 인터페이스
- 스냅 종류별 다이아몬드, 삼각형, 원, 십자 형태의 시각적 표시
- `SnapResult`를 통한 스냅 위치, 유형, 설명 전달

현재 구현에서 보완할 부분:
- 후보를 순회하면서 거리만 비교하므로 스냅 우선순위가 거리와 동일하게 처리됩니다. `Anchor > Handle > Intersection > Center > Midpoint > Axis > Grid`와 같은 명시적 우선순위와 동점 처리 규칙을 추가합니다.
- `Midpoint`가 노드 앵커 사이의 직선 중간점으로 계산됩니다. 베지어 세그먼트와 아크는 실제 곡선의 중점 또는 길이 기준 중점을 계산해야 합니다.
- `Center`가 바운딩 박스 중심이 아니라 앵커들의 산술 평균으로 계산됩니다. 정렬과 일관되도록 Geometry/Stroke 기준을 선택할 수 있는 중심 계산 정책을 둡니다.
- 현재 도형 순회는 최상위 `PathShape` 중심입니다. 중첩 그룹, 그룹 변환, 타원/사각형 등 다른 도형 타입의 스냅 포인트를 재귀적으로 수집해야 합니다.
- 스냅 후보가 `ShapeId`, SubPath, Segment, Node를 식별하지 않습니다. 선택 도구와 스냅 표시가 동일 후보를 참조할 수 있도록 후보 ID를 포함합니다.
- `excludeShape` 단일 포인터보다 이동 중인 모든 선택 도형을 제외하는 `SelectionFilter`가 필요합니다. 다중 선택 이동 시 자기 자신 또는 같은 그룹에 스냅되는 현상을 방지합니다.
- Anchor와 Center만으로는 정밀 편집에 부족하므로 Handle, Segment, BoundingBox Corner/Edge, Intersection, Tangent, Guide, Artboard를 추가합니다.
- 스냅 허용 오차를 전역 하나의 값으로만 두지 말고 도형 스냅과 그리드 스냅의 픽셀 허용 오차를 분리합니다. 최소/최대 월드 허용 오차도 설정합니다.
- 스냅 대상 후보를 모든 마우스 이동마다 전부 계산하면 문서가 커질수록 느려집니다. Geometry bounds 기반 공간 인덱스 또는 화면 영역 후보 캐시를 도입합니다.
- 스냅 결과에 `distancePx`, `priority`, `candidateId`, `guideLine`을 추가하면 UI 피드백과 디버깅이 쉬워집니다.
- `GridRenderer::snapToGrid`와 `SnapManager::m_targets`가 동시에 Grid 상태를 관리합니다. 전역 활성화, 대상 플래그, 일시적 수정키 스냅을 하나의 정책으로 통합합니다.
- Alt 등 수정키로 일시적으로 스냅을 끄거나, SnapManager에 설정된 대상만 일시적으로 활성화하는 기능을 추가합니다.

권장 후보 구조:
```cpp
struct SnapCandidate
{
    D2D1_POINT_2F worldPoint;
    SnapTargets type;
    int priority;
    ShapeId shapeId;
    int subPath = -1;
    int segment = -1;
    int node = -1;
};
```

### 11.2 RulerRenderer
관련 파일: `RulerRenderer.h`, `RulerRenderer.cpp`

현재 구현된 기능:
- DirectWrite 기반 상단/좌측 눈금자
- Grid의 단위와 Step을 공유하는 눈금 표시
- 줌과 팬에 따른 월드 원점 동기화
- 화면 축소 시 라벨 겹침을 줄이는 LOD 간격 조정
- DPI를 고려한 1픽셀 선 정렬
- px, mm, cm, in, rem 단위 라벨
- 눈금자 배경, 테두리, 눈금, 텍스트 색상 설정

현재 구현에서 보완할 부분:
- 눈금자가 `GridRenderer`의 Step과 Major 설정에 강하게 결합되어 있습니다. 눈금 표시 간격과 그리드 간격을 독립적으로 설정할 수 있도록 `RulerOptions`를 분리합니다.
- 현재 상단/좌측 눈금자와 문서 뷰의 좌표 원점은 팬/두께 보정에 의존합니다. `ScreenToWorld`/`WorldToScreen`과 동일한 변환 서비스를 사용하여 눈금 숫자와 캔버스 좌표가 항상 일치하도록 합니다.
- 주요 눈금과 중간 눈금의 단계가 고정된 1/2 구조입니다. 1-2-5 계열 자동 간격과 줌 단계별 보조 눈금 수를 도입합니다.
- 눈금자 숫자 포맷이 정수 또는 소수 한 자리 중심입니다. 단위별 정밀도, 음수 좌표, 큰 값의 과학적 표기, 지역화된 소수점 구분을 지원합니다.
- 눈금자에서 드래그하여 가이드라인을 생성하는 기능이 없습니다. 수평/수직 가이드, 가이드 잠금, 가이드 숨김, 가이드 스냅을 추가합니다.
- 눈금자 원점과 축 방향을 문서 설정으로 저장하고, 좌표축 원점(`originWorld`) 및 Canvas 경계와 동기화합니다.
- 눈금자 클릭/드래그의 입력 영역과 캔버스 입력 영역을 분리하고, 눈금자 표시를 끄거나 창 크기를 변경해도 팬과 중앙 원점 계산이 안정적이어야 합니다.
- DirectWrite 리소스의 장치 의존성, DPI 변경, 폰트 변경, 다크 테마 전환 시 재생성 정책을 명확히 합니다.
- 눈금자 렌더링은 모든 프레임에 텍스트를 다시 그리므로, 동일 줌/팬/단위 구간에 대한 라벨 레이아웃 캐시를 검토합니다.

### 11.3 프로퍼티 윈도우와의 연결
`D2DPropertyView`의 Grid 모드에는 현재 다음 설정이 연결되어 있습니다.
- Grid 표시
- Grid 스냅
- Ruler 표시
- 단위 선택
- Grid Step
- Major Every
- 보조선/주선 색상
- Grid 투명도

추가해야 할 프로퍼티:
- SnapManager 전체 활성화
- 스냅 대상별 체크박스: Grid, Axis, Anchor, Handle, Midpoint, Center, Intersection, Guide
- 스냅 허용 오차(px)
- 스냅 우선순위 정책
- 눈금자 두께와 눈금 표시 간격
- 보조 눈금 표시 여부
- 가이드 표시/잠금
- 문서별 원점과 앱 전역 기본값 구분

현재 체크박스 변경은 `GetApp()->GetGrid()`, `GetApp()->GetSnapManager()`, `GetApp()->GetRuler()`의 상태를 직접 수정하고 `UpdateAllViews()`를 호출합니다. 다음 규칙을 적용합니다.
- 문서에 저장되는 설정과 앱 세션 설정을 분리합니다.
- 설정 변경도 필요하면 Undo/Redo 대상이 되도록 합니다.
- Grid 표시, Grid 스냅, SnapManager 활성화의 의미를 UI에서 구분합니다. Grid를 숨겨도 스냅은 사용할 수 있어야 하는지 정책을 명시합니다.
- Property View가 닫혔다가 다시 열려도 실제 엔진 상태에서 모든 컨트롤을 재동기화합니다.

### 11.4 추가 테스트
- 줌 0.1배, 1배, 10배에서 스냅 허용 반경이 화면 픽셀 기준으로 동일한지 확인
- 음수 월드 좌표와 원점 이동 시 눈금자 라벨/그리드/스냅 일치 여부
- mm, cm, inch, rem 단위 변경 후 물리적 Grid 간격과 눈금 라벨 검증
- 그룹과 회전/비균일 스케일 도형의 Anchor, Center, Midpoint 스냅
- 곡선 세그먼트와 아크의 실제 중점 스냅
- 다중 선택 이동 중 자기 도형 제외
- Grid 숨김, Grid 스냅, SnapManager 비활성화의 독립 동작
- 창 크기 변경, DPI 변경, Direct2D 리소스 재생성 후 눈금자 정상 렌더링
- 가이드 생성/이동/잠금/삭제와 Undo/Redo
