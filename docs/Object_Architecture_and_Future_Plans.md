# M2Draw 벡터 그래픽스 확장 아키텍처 및 구현 계획

이 문서는 M2Draw의 현재 오브젝트 저장 구조와 데이터 흐름을 분석하고, 향후 예정된 **고급 벡터 연산(불리언/트림/노드편집)** 및 **관리 기능(그룹핑/Z-Order/정렬)**의 구현 계획을 정리한 문서입니다.

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
  1. 선택된 대상 도형들의 로컬 변환(`m_xf`)을 월드 좌표계 기준으로 합성.
  2. `CombineWithGeometry`를 호출하여 합성된 신규 `PathGeometry` 추출.
  3. `ID2D1SimplifiedGeometrySink`를 구현하여 새 지오메트리로부터 정점을 추출, `BezierNode` 목록(`SubPath`)으로 역변환.
  4. 기존 참여 도형들을 `m_shapes`에서 제거하고 새 단일 `PathShape`로 교체 등록.

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
  2. 전체 자식들의 결합 바운딩 박스를 계산하여 그룹의 기준점으로 설정.
  3. `m_shapes`의 해당 인덱스 자리에 `GroupShape` 삽입.
- **그룹 해제 (Ctrl + Shift + G)**:
  1. `GroupShape` 내부의 자식들을 꺼내며, 그룹의 누적 변환 행렬을 각 자식의 `m_xf`에 적용하여 월드 좌표로 환원.
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
현재 구축된 신형 씬 그래프 기반 엔진(`D2DGraphicEngine.cpp`)과 프로젝트의 도구 체계(`ITool`, `ToolBoxPane`, `ToolOptionBar`)를 종합적으로 고려했을 때, 실무 수준의 벡터 드로잉 에디터로 완성도를 끌어올리기 위해 우선적으로 추가해야 할 핵심 기능들입니다.

---

### 1. 렌더링 및 스타일링 고도화 (스타일 시스템 확장)

* **그라데이션 및 패턴 브러시 (`GradientBrush`)**
* 현재 스타일 구조는 단색(`FillType::Solid`) 위주로 지원하고 있습니다.


* 선형 그라데이션(`ID2D1LinearGradientBrush`)과 방사형 그라데이션(`ID2D1RadialGradientBrush`)을 지원하도록 `Style` 구조체와 렌더러를 확장할 필요가 있습니다.


* 시작점/끝점, 컬러 스톱(Color Stop) 편집 핸들을 캔버스 상에서 직접 조작하는 도구가 추가되면 완성도가 크게 올라갑니다.


* **선 팁/조인(Line Cap & Line Join) 스타일 제어**
* 베지에 곡선과 꺾인 선의 표현력을 위해 끝점 모양(Round, Square, Flat) 및 코너 조인(Miter, Round, Bevel)을 `D2D1_STROKE_STYLE_PROPERTIES`에 바인딩할 수 있어야 합니다.




* **클리핑 마스크 (Clipping Mask)**
* 한 도형(또는 텍스트)을 상위 마스크로 지정하여 하위 도형들이 그 영역 안에서만 렌더링되도록 하는 기능(`ID2D1RenderTarget::PushLayer` / `PopLayer` 또는 `ID2D1Layer` 활용)입니다.



---

### 2. 패스 및 지오메트리 고급 연산

* **불리언 연산 완성 (`PathOp::Combine`)**
* 현재 뼈대(Stub) 상태인 불리언 연산을 `ID2D1Geometry::CombineWithGeometry`와 연동하여 합집합, 차집합, 교집합, 배타적 논리합 결과를 실제 `BezierNode` 목록으로 환원하는 작업이 필요합니다.




* **패스 단순화 (Simplify Path)**
* 자유 곡선(Pencil/Freehand)으로 그렸을 때 지나치게 많이 생성되는 앵커 포인트를 Ramer-Douglas-Peucker 알고리즘 등을 통해 곡률 손실 없이 최소한의 베지에 노드로 줄여주는 최적화 기능입니다.


* **오프셋 패스 / 외곽선 확장 (Outline Stroke / Offset Path)**
* 획(Stroke) 두께를 가진 열린/닫힌 패스를 면(Fill) 형태의 폐곡선 패스로 변환(`ID2D1Geometry::Widen`)하는 기능으로, 로고 및 타이포그래피 작업에 필수적입니다.


* **패스 결합 및 분리 (Join & Split Path)**
* 떨어져 있는 두 앵커 노드를 하나로 이어붙이거나, 단일 서브패스의 특정 세그먼트를 잘라 두 개의 독립된 패스로 나누는 연산입니다.





---

### 3. 상호작용 및 편집 생산성 향상

* **텍스트 노드 (`TextNode`) 구현**
* `NodeKind`에 `Text`를 추가하고 DirectWrite(`IDWriteFactory`, `IDWriteTextFormat`, `IDWriteTextLayout`)를 연동하여 글꼴, 자간, 행간, 정렬을 지원하는 벡터 텍스트 객체를 지원해야 합니다.
* 추후 필요 시 텍스트를 일반 패스(`PathNode`)로 깨뜨리는(Convert to Curves / Outlines) 기능으로 확장 가능합니다.


* **스마트 가이드 (Smart Guides) & 오브젝트 스냅**
* 현재 `SnapManager`와 `GridRenderer`가 있으나, 그리드 격자 외에 다른 도형의 경계선(Center, Edge, Anchor)에 실시간으로 자석처럼 달라붙고 보조선(가이드 라인)을 띄워주는 기능이 필요합니다.




* **고급 다중 선택 변환 (Bounding Box Transform)**
* 여러 개 오브젝트를 선택했을 때 공통 바운딩 박스를 계산하여 일괄 크기 조절, 일괄 회전, 기울이기(Shear/Skew)가 가능한 조작 기즈모를 제공해야 합니다.




* **그룹 내부 직접 진입 (Isolation / Direct Selection Mode)**
* 그룹화된 상태에서 그룹을 해제(Ungroup)하지 않고도, 특정 자식 패스만 더블 클릭하여 격리 편집하거나 노드 툴로 내부 정점을 즉시 수정할 수 있는 진입 모드입니다.



---

### 4. 문서 관리, 직렬화 및 입출력 (I/O)

* **파일 저장 및 불러오기 (Serialization)**
* 현재 인메모리 `Document` 트리 구조를 디스크에 영속화할 수 있는 포맷이 필요합니다.


* JSON 또는 바이너리 포맷으로 씬 그래프(`LayerNode` ➔ `GroupNode` ➔ `PathNode`)와 스타일 속성을 저장/복원하는 직렬화 모듈이 필수적입니다.




* **SVG 가져오기 / 내보내기 (SVG Import & Export)**
* 벡터 툴의 범용성을 위해 XML 파서를 활용한 SVG 내보내기(`Path`의 `M`, `C`, `L`, `Z` 명령어 생성) 및 외부 SVG 파일 로딩 기능이 추가되면 상용 툴과의 연계가 비약적으로 쉬워집니다.


* **고해상도 비트맵 익스포트 (PNG / JPG / PDF)**
* 캔버스 뷰포트 배율과 무관하게 지정한 해상도(DPI)로 Direct2D 렌더 타깃을 오프스크린 비트맵(`ID2D1BitmapRenderTarget` / WIC)에 렌더링하여 이미지 파일로 내보내는 기능입니다.



---

### 추천 구현 로드맵

1. **1단계 (기본기 완성)**: 파일 저장/불러오기(Serialization) + 불리언 연산 완성(`PathOp::Combine`)


2. **2단계 (표현력 확장)**: 그라데이션 브러시 + 텍스트 노드(`TextNode`, DirectWrite)
3. **3단계 (작업 편의성)**: 스마트 가이드(도형 간 스냅) + 패스 외곽선 확장(Outline Stroke) 및 SVG Export