# M2Draw 마스터 계획 — 벡터 일러스트(SVG 출력) & HTML 페이지 디자인(HTML 출력)

> 작성일: 2026-10-27
> 대체 문서: `Object_Architecture_and_Future_Plans.md` (1장 "현재 구조"는 구형 엔진 기준이라 더 이상 유효하지 않음. 기능 아이디어는 본 문서에 흡수)
> 관련 문서: `Layer_System_Plan.md`, `TODO_Layer_System.md`, `Migration_Plan_D2DGraphicEngine.md`, `ToolOptionBar_and_Shape_Extension_Plan.md`

---

## 0. 최종 목표와 설계 원칙

| 목표 | 산출물 | 성격 |
|---|---|---|
| **A. 벡터 일러스트** | `.svg` (표준 SVG 1.1 / 2 호환) | 절대 좌표 + 변환 행렬 기반 자유 배치 (Illustrator / Inkscape 계열) |
| **B. HTML 페이지 디자인** | `.html` + `.css` (벡터는 인라인 `<svg>`) | 박스 모델 + 흐름 레이아웃 기반 (Figma Auto Layout / Webflow 계열) |

### 설계 원칙
1. **모델은 순수 데이터** — `ge` 모델 계층(Node 계열, Style, Document)은 MFC·Direct2D에 의존하지 않는다. D2D 의존은 `D2DRenderer`/`HitTester`/`PathOp` 구현부에만 둔다.
2. **SVG 의미론을 기준으로 삼는다** — 화면 렌더링 결과가 SVG 출력 결과와 같아야 한다(WYSIWYG). 렌더러가 SVG 규칙에 맞춘다(반대가 아님).
3. **출력기는 하나의 순회 구조를 공유** — `INodeVisitor` 기반으로 `SvgExporter`, `HtmlExporter`, 저장(Serializer)이 같은 트리 순회를 쓴다.
4. **선택·식별은 포인터/NodeId 기반** — 인덱스 기반 상태를 두지 않는다.
5. **모든 문서 변경은 Undo 가능** — 변경은 Command(또는 스냅샷)를 통해서만 한다.
6. **저장된 값과 계산된 값을 분리** — 레이아웃 *속성*과 스타일은 저장하고, 계산된 좌표·지오메트리·텍스트 줄바꿈은 캐시로만 둔다.

---

## 1. 현재 상태 진단 (2026-10-27 기준 코드)

### 1.1 실제 오브젝트 구조
```text
CM2DrawDoc
 ├── m_doc : ge::Document                       ← 실사용
 │    ├── m_layers : vector<unique_ptr<LayerNode>>   (0 = 최하단)
 │    │    └── LayerNode : GroupNode : Node
 │    │         └── m_children : vector<unique_ptr<Node>>
 │    │              ├── PathNode  (m_paths: vector<SubPath>, m_kind: ShapeKind, m_params: variant)
 │    │              └── GroupNode (재귀)
 │    ├── m_selection : vector<Node*>
 │    └── m_nextId    : NodeId 발급기
 ├── m_currentPathNode                          ← 펜 작성 중 임시 패스
 └── m_shapes / m_currentPath                   ← 구형 호환 잔재 (채우는 곳 없음)

D2DRenderer : m_geoCache[NodeId] = { version, ID2D1PathGeometry }
```

### 1.2 구현 현황

| 기능 | 엔진 | UI 연결 | 비고 |
|---|:---:|:---:|---|
| 씬 그래프 (Layer/Group/Path) | ✅ | ✅ | |
| 파라메트릭 도형 9종 (Rect~Heart) | ✅ | ✅ | ToolOptionBar 연동 |
| 레이어 패널 (보기/잠금/이름/불투명도/블렌드) | ✅ | ✅ | 블렌드 모드는 값만 저장, 렌더 미적용 |
| 선택/이동/스케일/회전 | ✅ | ✅ | 인덱스 기반 선택 (결함) |
| 앵커/핸들 이동 | ✅ | ✅ | SelectTool 내부 |
| 노드 삽입/삭제 (`InsertNodeOnSegment`, `RemoveNode`) | ✅ | ❌ | |
| 그룹/그룹 해제 | ✅ | ❌ | |
| Z-Order | ✅ | ❌ | 인접 다중 선택 시 버그 |
| 정렬/분배 | ✅ | ❌ | |
| 불리언 연산 `PathOp::Combine` | ⚠️ 뼈대 | ❌ | 빈 PathNode 반환 |
| Undo/Redo `CommandStack` | ⚠️ 선언만 | ❌ | 사용처 없음 |
| 저장/불러오기 | ❌ | ❌ | `Serialize` TODO |
| 텍스트 / 페인트통 도구 | ❌ | ⚠️ 껍데기 | `TextTool`, `PaintBucketTool` |
| 그라디언트 / cap·join / dasharray | ❌ | ❌ | |
| SVG / HTML / PNG 출력 | ❌ | ❌ | |

### 1.3 결함 목록 (Phase 0에서 모두 해소)

| ID | 심각도 | 위치 | 내용 |
|---|---|---|---|
| D1 | 🔴 | `D2DGraphicEngine.h` Node: `name`/`xf`/`style`, PathNode: `paths` | **참조 멤버** → 복사 생성 시 복제본의 참조가 *원본* 멤버를 가리킴. `PathNode::Clone()`이 복사 생성자를 쓰므로 복제본 편집이 원본을 오염. 복사 대입도 불가. |
| D2 | 🔴 | `PathNode::Clone()` | `m_id`·`m_version`까지 복사 → NodeId 중복, `m_geoCache` 충돌, `FindById` 오동작. GroupNode/LayerNode Clone은 id=0 으로 동작 불일치. |
| D3 | 🔴 | `D2DLayer.h` | LayerNode가 `m_bVisible`/`m_bLocked`/`m_opacity`를 **중복 선언**하고 비가상 `IsVisible()` 등을 숨김(hiding). `Node&`로 호출하는 `HitNode`, `GetEffectiveOpacity`, SnapManager는 Node 쪽 값을 읽음 → 숨김/잠금 레이어 도형이 클릭됨, 레이어 불투명도 누락. |
| D4 | 🔴 | `CM2DrawDoc::Serialize` | 저장/불러오기 미구현. |
| D5 | 🟠 | `SelectTool::m_selectedIndices` vs `Document::m_selection` | 선택 상태 이중화. 인덱스 기반이라 그룹 내부·타 레이어 선택 불가, Z-Order 변경 시 어긋남. |
| D6 | 🔴 | `CM2DrawDoc::m_shapes` (이중 모델) | **[정정 2026-10-27]** 미사용이 아니라 **실사용 중**. 생성 도구가 `m_shapes`와 활성 레이어에 같은 도형을 각각 넣고, SelectTool·핸들·속성창은 `m_shapes`를, 레이어 패널은 `ge::Document`를 사용. 캔버스에는 두 목록이 **겹쳐서 이중 렌더링**됨. `m_shapes[i]` ↔ 활성 레이어 `children[i]` 인덱스 대응에 의존하므로, 한쪽만 바꾸는 모든 작업(복제, 그룹, Z-Order, 레이어 이동, 다중 레이어)이 데이터를 오염시킴. 이름 기반 삭제 로직도 존재. |
| D7 | 🟠 | `CommandStack` | 미사용 → Undo/Redo 없음. |
| D8 | 🟠 | Node / LayerNode | 모델이 `CString`(MFC), `D2D1_*`, `Node::Draw(ID2D1RenderTarget*)`, `GetD2DLayerParameters()` 에 의존. `Node::Draw`는 매 호출 렌더러를 새로 생성해 캐시 무의미. |
| D9 | 🟡 | `Matrix2D::operator D2D1::Matrix3x2F` | 임시 객체 주소 `reinterpret_cast` (MSVC 확장 의존 UB). |
| D10 | 🟡 | `Document::ArrangeSelection` | Forward/Backward 시 인접 선택 항목끼리 서로 swap 되어 상쇄. |
| D11 | 🟡 | `Document::UngroupSelection` | 그룹 opacity/style이 자식에 전파되지 않고 소실. |
| D12 | 🟡 | `Node` vs `Transformer` | `ScaleByHandle`/`RotateAboutCenter`/`GetLocalHandlePoints` 중복 구현. |
| D13 | 🟡 | `PathNode::GetKind()` vs `Node::Kind()` | `ShapeKind`/`NodeKind` 혼동 → `GetShapeKind()`로 개명. |
| D14 | 🟡 | `ShapeParams` | 노드를 직접 편집한 뒤에도 파라메트릭 params가 남아 데이터 불일치. |
| D15 | 🟡 | 저장소 | `D2DVectorEngine_old.h`, `backup_toolbar_20261021/` 잔재 (git 이력으로 대체 가능). |

### 1.4 SVG 의미론과 어긋나는 렌더링 (Phase 2에서 해소)

| ID | 위치 | 현재 | SVG 규칙 |
|---|---|---|---|
| S1 | `D2DRenderer::BuildGeometry` | 열린 서브패스는 `FIGURE_BEGIN_HOLLOW` → 채우지 않음 | 열린 서브패스도 암묵적으로 닫아 **채움** |
| S2 | `D2DRenderer::DrawPath` | 그룹 불투명도를 도형별 색 알파에 곱함 | `<g opacity>`는 그룹 전체를 합성 후 투명도 적용 (겹침 부분 결과가 다름) |
| S3 | 레이어 블렌드 / 클리핑 | 플래그만 존재 | `mix-blend-mode`, `<clipPath>` |
| S4 | Style | DashStyle enum 5종, cap/join 없음 | `stroke-dasharray`, `stroke-linecap`, `stroke-linejoin`, `stroke-miterlimit` |

---

## 2. 목표 아키텍처

### 2.1 계층 구조
```text
┌───────────────────────── UI (MFC) ─────────────────────────┐
│ CM2DrawView · ToolManager/ITool · LayerWnd · PropertyView  │
│ ToolOptionBar · ToolBoxPane                                 │
└───────────────┬─────────────────────────────────────────────┘
                │ Command 만 통해 변경
┌───────────────▼──────────── Editor 서비스 ──────────────────┐
│ CommandStack · SelectionModel · ArrangeService(정렬/Z-Order) │
│ PathOps(불리언/단순화/외곽선) · LayoutEngine(HTML Frame)     │
└───────────────┬─────────────────────────────────────────────┘
┌───────────────▼──────────── Model (순수 C++) ───────────────┐
│ Document → Page/Artboard → Layer → Group/Frame → Path/Text/Image │
│ Style(Paint, Stroke, Effects) · Matrix2D · Vec2 · Rect2      │
└───────┬───────────────────────┬─────────────────────────────┘
        │ INodeVisitor          │
┌───────▼──────────┐   ┌────────▼───────────────────────────┐
│ D2DRenderer      │   │ IO: SvgWriter/SvgReader · HtmlExporter │
│ HitTester        │   │     PngExporter(WIC)                   │
└──────────────────┘   └────────────────────────────────────┘
```

### 2.2 노드 계층 (최종)
```text
Node (추상)                         ─ id, parent, name, xf, style, visible, locked, opacity, blendMode
 ├── PathNode                       ─ subpaths(BezierNode), shapeKind, shapeParams
 ├── TextNode            [Phase 3]  ─ runs(text+스타일), boxMode(Point/Area), 정렬, 줄간격, 자간
 ├── ImageNode           [Phase 3]  ─ 리소스 참조(id), 원본 크기, objectFit
 └── GroupNode                      ─ children, clipPath(옵션)
      ├── LayerNode                 ─ (추가 필드 없음 — Node 필드 사용)
      └── FrameNode      [Phase 4]  ─ layout, padding, gap, align, sizing, background, border, radius, shadow, htmlTag, className
Document
 └── pages : vector<Page>  [Phase 4] ─ 이름, 크기, 브레이크포인트
      └── layers
```

### 2.3 Style 모델 (Phase 2 목표)
```cpp
struct GradientStop { float offset; Color color; };
struct Gradient {
    enum class Type { Linear, Radial } type;
    Vec2 p0, p1;            // Linear: 시작/끝, Radial: 중심/초점
    float r = 0;            // Radial 반지름
    std::vector<GradientStop> stops;
    enum class Units { ObjectBBox, UserSpace } units = Units::ObjectBBox;
    Matrix2D xf;            // gradientTransform
};
struct Paint {
    enum class Kind { None, Solid, Gradient } kind = Kind::None;
    Color    color;
    std::shared_ptr<const Gradient> gradient;   // 불변 공유 (복제 비용 절감)
};
enum class LineCap  { Butt, Round, Square };
enum class LineJoin { Miter, Round, Bevel };
struct Stroke {
    Paint paint; float width = 1; float opacity = 1;
    LineCap cap = LineCap::Butt; LineJoin join = LineJoin::Miter; float miterLimit = 4;
    std::vector<float> dashArray; float dashOffset = 0;
};
struct Style {
    Paint fill; float fillOpacity = 1; FillRule fillRule = FillRule::NonZero;
    Stroke stroke;
};
```
- 기존 `FillType`/`DashStyle`/`DashStyleWrapper`는 Phase 2에서 이 구조로 마이그레이션 (UI의 DashStyle 콤보는 프리셋 → dashArray 변환).
- SVG 기본값(`fill-rule: nonzero`, `stroke-miterlimit: 4`)을 기본값으로 채택.

### 2.4 속성창(PropertyView) 아키텍처 및 UI 연동 계획
기능 확장에 따라 `CD2DPropertyView`의 패널 모드 및 섹션 컨트롤을 유기적으로 확장합니다.

| 속성창 모드/섹션 | 대상 객체 / 상태 | 포함 컨트롤 및 연동 기능 |
|---|---|---|
| **스타일 섹션** (공통) | 단일/다중 노드 | - 채우기/윤곽선 색상 휠, 투명도 슬라이더<br>- **Fill 모드**: 단색, 선형/방사형 그라디언트(Color Stop 편집 바, 반전 버튼)<br>- **Stroke 모드**: 두께, Cap(Butt/Round/Square), Join(Miter/Round/Bevel), DashArray 프리셋/커스텀 |
| **도형/패스 속성** | `PathNode` | - 기본 변환(X, Y, W, H, 회전각)<br>- 파라메트릭 도형별 맞춤 파라미터(Corner Radius, Star Points/InnerRadius, Polygon Sides 등)<br>- 불리언 연산/패스 분할/단순화 실행 버튼 |
| **텍스트 패널** (신규) | `TextNode` (Phase 3) | - 글꼴 패밀리(DirectWrite 폰트 콤보박스), 크기, 스타일(Regular/Bold/Italic)<br>- 정렬(좌/중앙/우/양쪽), 행간(Line-Height), 자간(Letter-Spacing)<br>- 텍스트 모드(Point Text / Area Box Text), 윤곽선 변환(Convert to Curves) 버튼 |
| **이미지 패널** (신규) | `ImageNode` (Phase 3) | - 원본 크기, 배치 모드(Fill/Fit/Crop), 이미지 교체/추출 버튼, 투명도 제어 |
| **프레임/레이아웃 패널** (신규) | `FrameNode` (Phase 4) | - 레이아웃 방향(Row/Column/None), Gap, Padding(상하좌우 개별/통합)<br>- 정렬 방식(Main Align, Cross Align, Wrap 허용)<br>- 크기 규칙(Sizing: Fixed / Hug / Fill), 모서리 Radius(4모서리), Box-Shadow, 시맨틱 태그 선택 |
| **환경설정 패널** (전환) | 캔버스 / 그리드 / 스냅 | - 그리드 뷰/스냅 간격, 색상, 스케일<br>- **오브젝트 스냅 설정 (신규)**: 스냅 On/Off, 대상(정점, 중간점, 중심점, 변/선 위, 교차점) 개별 체크박스 |

---

## 3. 단계별 실행 계획

각 단계는 **완료 기준(DoD)** 을 모두 만족해야 다음 단계로 넘어간다. 기능 구현 시 **해당 기능을 조작할 수 있는 속성창(PropertyView) 및 옵션바 UI 작업을 반드시 세트로 진행**한다.

### Phase 0 — 기반 정리 (결함 제거) 🔴 최우선

| # | 작업 | 해소 결함 |
|---|---|---|
| 0.1 | Node/PathNode의 참조 멤버(`name`, `xf`, `style`, `paths`) 제거 → 사용처를 getter/setter로 치환. 이름 필드는 `std::wstring m_name` 하나로 통일 (`m_nameStr` 제거) | D1 |
| 0.2 | `Clone()` 규약 통일: 모든 Clone은 `m_id = 0`, `m_parent = nullptr`, `m_version` 새로 시작. 문서에 삽입할 때 `AssignIds`로 새 ID 발급 | D2 |
| 0.3 | LayerNode의 중복 필드/메서드 삭제 → Node의 `m_visible`/`m_locked`/`m_opacity` 사용. `LayerWnd`의 자식 visible 우회 코드 제거. `blendMode`는 Node로 승격(그룹/도형에도 SVG `mix-blend-mode` 적용 가능) | D3 |
| 0.4 | 잠금/숨김 판정을 "자신 + 조상 체인"으로: `Node::IsEffectivelyVisible()`, `IsEffectivelyLocked()` 추가. HitTester, SnapManager, 렌더러가 이를 사용 | D3 |
| 0.5 | 선택 단일화: `SelectTool::m_selectedIndices` 제거 → `Document` 선택(`vector<Node*>` 또는 `SelectionModel`) 하나만 사용. `SyncDocSelection` 제거 | D5 |
| 0.6 | 구형 잔재 제거: `m_shapes`, `m_currentPath`, 이름 기반 삭제 로직, `Shape`/`PathShape` using 별칭, `D2DVectorEngine_old.h`, `backup_toolbar_20261021/` | D6, D15 |
| 0.7 | 모델에서 D2D/MFC 분리: `Node::Draw` 삭제(렌더러 경유), `GetD2DLayerParameters`는 렌더러로 이동, D2D 변환 연산자(`operator D2D1_*`)는 별도 헤더 `GeD2DInterop.h`의 자유 함수(`ToD2D()`/`FromD2D()`)로 이전 | D8, D9 |
| 0.8 | `Node::ScaleByHandle` 등 중복 제거 → `Transformer`만 사용 | D12 |
| 0.9 | `PathNode::GetKind()` → `GetShapeKind()` 개명. 노드를 직접 편집(앵커 이동/삽입/삭제)하면 `shapeKind = Path`, `params = monostate`로 전환하는 규칙 적용 | D13, D14 |
| 0.10 | `ArrangeSelection` 수정: Forward는 위쪽부터, Backward는 아래쪽부터 처리하고 선택된 이웃과는 swap하지 않음 | D10 |
| 0.11 | `UngroupSelection`: 그룹 opacity를 자식 opacity에 곱해 보존 | D11 |

**DoD**
- 복제(Ctrl+D)한 도형을 편집해도 원본이 변하지 않고, 두 도형의 NodeId가 다름.
- 숨김/잠금 레이어의 도형을 클릭해도 선택되지 않음.
- 그룹 내부 도형, 다른 레이어 도형을 선택해도 선택 상태가 일관됨.
- `D2DGraphicEngine.h`의 모델 부분이 `<d2d1.h>`, MFC 헤더 없이 컴파일됨(별도 테스트 TU로 확인).
- 빌드 경고 레벨 W4에서 신규 경고 없음.

### Phase 1 — 영속성 + Undo/Redo

| # | 작업 |
|---|---|
| 1.1 | **저장 포맷 결정: "M2Draw 확장 SVG"** — 표준 SVG에 `xmlns:m2="https://m2draw/ns"` 네임스페이스로 파라메트릭 정보(`m2:shape="star" m2:points="5" ...`), 노드 타입(Corner/Smooth/Symmetric), 레이어 속성(`inkscape:groupmode="layer"` 호환)을 기록. → 저장 파일이 곧 브라우저/Inkscape에서 열리는 SVG |
| 1.2 | `INodeVisitor` 도입 (`Visit(PathNode&)`, `Visit(GroupNode&)`, `Visit(LayerNode&)` …) |
| 1.3 | `SvgWriter` (Visitor): `<svg viewBox>` → 레이어 `<g>` → 그룹 `<g transform="matrix(...)">` → `<path d="M..C..L..Z">`. 숫자 출력은 로캘 독립(`.` 소수점), 유효자릿수 제한 |
| 1.4 | `SvgReader`: XML 파서 선택(경량 `pugixml` 권장 또는 MSXML/XmlLite — Windows 내장 **XmlLite** 사용 시 외부 의존 없음). Phase 1에서는 자체 출력물 왕복(round-trip)만 보장 |
| 1.5 | `CM2DrawDoc::Serialize` → `OnSaveDocument`/`OnOpenDocument` 재정의로 SvgWriter/Reader 호출, 확장자 `.m2svg` 또는 `.svg` |
| 1.6 | Undo/Redo: 1차는 **문서 스냅샷 방식**(Document 깊은 복제 or SVG 문자열 스냅샷), 드래그 등 연속 작업은 시작/종료 시점에 1회만 기록. `ID_EDIT_UNDO/REDO`, Ctrl+Z / Ctrl+Y 연결 |
| 1.7 | 2차(성능 필요 시) 세분화 Command: `TransformCommand`, `EditPathCommand`, `AddRemoveNodeCommand`, `ReorderCommand`, `StyleCommand` |
| 1.8 | `SetModifiedFlag` 연동 (변경 시 제목 표시줄 `*`) |

**DoD**: 저장 → 종료 → 열기 시 모든 도형/레이어/그룹/스타일/파라메트릭 속성이 동일. 저장 파일을 Chrome·Inkscape에서 열었을 때 화면과 같게 보임. 모든 편집 작업 Undo/Redo 가능.

### Phase 2 — SVG 완성도 (스타일 · 렌더 의미론 · 패스 연산)

| # | 작업 |
|---|---|
| 2.1 | Style 마이그레이션 (§2.3): Paint/Gradient/Stroke(cap·join·miter·dashArray), fillRule. **PropertyView(Cap/Join 콤보, DashArray 제어)·ToolOptionBar UI 갱신** |
| 2.2 | 렌더러 SVG 정렬: S1 열린 패스 채우기, S2 그룹 opacity < 1 이면 `PushLayer`, S3 블렌드 모드(`ID2D1DeviceContext` + Blend Effect 또는 레이어 오프스크린 합성) |
| 2.3 | 그라디언트 렌더링 + **PropertyView Color Stop 편집 바(색상/위치 조절, 추가/삭제, 프리셋)** 및 캔버스 그라디언트 편집 핸들 도구 |
| 2.4 | 클리핑 마스크: `GroupNode::clipPath`(첫 자식 또는 지정 노드) → D2D `PushLayer(geometricMask)` / SVG `<clipPath>` |
| 2.5 | 불리언 연산 완성: 월드 변환 적용 → `CombineWithGeometry` → `ID2D1GeometrySink` 구현체로 베지에 수신 → SubPath 복원 → Command로 교체. Union/Subtract/Intersect/Exclude(XOR) + Divide |
| 2.6 | 패스 연산: Simplify(RDP + 베지에 피팅), Outline Stroke(`Widen`), Offset Path, Join/Split Path, Reverse Path |
| 2.7 | 엔진 기능 UI 연결: 그룹(Ctrl+G / Ctrl+Shift+G), Z-Order(Ctrl+] / [ …), **정렬·분배 패널/옵션바 UI, 속성창 불리언/패스 버튼**, 노드 추가/삭제 도구(Direct Selection) |
| 2.8 | SVG 출력 다듬기: 파라메트릭 도형은 `<rect rx>`/`<ellipse>`/`<polygon>` 네이티브 요소로 출력 옵션, `<defs>`(gradient, clipPath) 중복 제거, "순수 SVG로 내보내기"(m2 네임스페이스 제거) 옵션 |
| 2.9 | SVG 가져오기 확장: 외부 SVG(`rect/circle/ellipse/line/polyline/polygon/path` 전체 명령 `MLHVCSQTAZ` 상대/절대, `transform`, `style` 속성, `use`)를 PathNode로 변환. 원호(A)는 베지에로 근사 |

**DoD**: W3C SVG 기본 도형/패스 샘플 세트를 import → export 시 브라우저 렌더링과 시각적으로 일치. 화면 렌더와 Chrome 렌더 비교에서 차이 없음(열린 패스, 그룹 opacity, 대시, 조인 포함). **새로 추가된 모든 스타일 속성이 속성창에서 양방향 편집 가능.**

### Phase 3 — 텍스트 & 이미지 (SVG/HTML 공통)

| # | 작업 |
|---|---|
| 3.1 | `NodeKind::Text`, `TextNode`: runs(텍스트 + 글꼴/크기/굵기/기울임/색), Point 텍스트 / Area(박스) 텍스트, 정렬, 줄간격, 자간 |
| 3.2 | 렌더링: DirectWrite `IDWriteTextLayout` (렌더러 쪽 캐시, 모델에는 줄바꿈 결과 저장 안 함) |
| 3.3 | `TextTool` 구현: 클릭 생성, 인라인 편집(캐럿·선택·IME 한글 조합), 박스 드래그 생성 |
| 3.4 | **PropertyView 텍스트 모드/섹션 구현**: 글꼴 콤보(시스템 폰트 열거), 크기/굵기/자간/행간 컨트롤, 텍스트 정렬 토글, 윤곽선 변환 버튼 |
| 3.5 | `ImageNode`: Document 리소스 테이블(id → 바이트/경로), WIC 디코딩, 렌더 캐시 + **PropertyView 이미지 속성(크기, 맞춤, 교체 버튼)** |
| 3.6 | 텍스트 → 윤곽선 변환 (`IDWriteFontFace::GetGlyphRunOutline` → PathNode) |
| 3.7 | SVG 출력: `<text>`/`<tspan>`(Area 텍스트는 줄바꿈 계산 결과를 tspan으로 출력 or 윤곽선 옵션), `<image href="data:..."|파일>` |

**DoD**: 한글/영문 혼합 텍스트 생성·편집·저장·SVG 출력 가능. 이미지 삽입·변형·저장 가능.

### Phase 4 — HTML 페이지 디자인

#### 4.1 개념 모델
- 자유 배치 벡터 모델로는 반응형/시맨틱 HTML을 만들 수 없으므로 **Figma식 Frame + Auto Layout** 을 도입한다.
- 벡터 일러스트 영역(Group/Path)은 Frame 안에서 하나의 "그림"으로 취급하여 인라인 `<svg>`로 출력.

#### 4.2 FrameNode 속성
| 범주 | 속성 | CSS 매핑 |
|---|---|---|
| 레이아웃 | `layout: None / Row / Column / (Grid)` | `position:relative` + 자식 absolute / `display:flex; flex-direction` / `display:grid` |
| | `gap`, `padding(t,r,b,l)` | `gap`, `padding` |
| | `mainAlign`, `crossAlign`, `wrap` | `justify-content`, `align-items`, `flex-wrap` |
| 크기 | 축별 `sizing: Fixed / Hug / Fill`, min/max | `width:Npx` / `fit-content` / `flex:1` 또는 `align-self:stretch`, `min-/max-width` |
| 외관 | background(Paint), border(Stroke), radius(4모서리), shadow, opacity, clipContent | `background`, `border`, `border-radius`, `box-shadow`, `opacity`, `overflow:hidden` |
| 시맨틱 | `htmlTag`(div/section/header/nav/main/footer/button/a/ul/li), `className`, `href`, `alt` | 태그 / class / 속성 |
| 반응형 | 브레이크포인트별 속성 오버라이드 | `@media` |

#### 4.3 작업
| # | 작업 |
|---|---|
| 4.3.1 | `Page`(Artboard) 개념 추가: 문서 = 여러 페이지, 페이지마다 크기/배경. 일러스트 모드 = 페이지 1개 + Frame 미사용 |
| 4.3.2 | `FrameNode : GroupNode` 및 Frame 도구 (드래그 생성, 선택 영역을 Frame으로 감싸기) |
| 4.3.3 | **PropertyView 프레임/Auto Layout 모드/섹션 구현**: 레이아웃 방향(Row/Col), Gap/Padding 스핀/슬라이더, Sizing(Fixed/Hug/Fill), 정렬 토글, Radius(4각), Shadow, HTML 태그 콤보 |
| 4.3.4 | `LayoutEngine` (모델 외부 서비스): flexbox 부분 구현(방향, gap, padding, align, Fixed/Hug/Fill, wrap). 결과는 자식의 *계산 위치 캐시*에 기록, 저장 안 함 |
| 4.3.5 | 규칙: `layout != None` 인 Frame의 직계 자식은 위치를 LayoutEngine이 결정 → 선택 툴에서 이동 = 순서 변경(드래그 재정렬), 회전/스큐는 CSS transform 별도 속성으로 제한 |
| 4.3.6 | TextNode의 Frame 내 동작: 폭 Fill/Fixed 시 자동 줄바꿈, 높이 Hug |
| 4.3.7 | `HtmlExporter` (Visitor): Frame → 태그 + 클래스, 스타일은 클래스 단위 CSS로 추출(중복 스타일 병합), Text → `<p>/<h1~6>/<span>`, Image → `<img>`, 벡터 그룹 → 인라인 `<svg>`(Phase 2의 SvgWriter 재사용), 폰트 → `@font-face` 또는 웹폰트 링크 |
| 4.3.8 | 미리보기: WebView2로 내보낸 HTML을 앱 안에서 바로 확인 |
| 4.3.9 | 반응형: 브레이크포인트 편집 UI + 속성 오버라이드 → `@media` 출력 |
| 4.3.10 | 컴포넌트(선택): 재사용 Frame(마스터/인스턴스) → HTML에서는 반복 마크업 |

**DoD**: Header / Hero / 카드 3열 / Footer 구성의 샘플 페이지를 디자인 → HTML 출력 → Chrome에서 디자인과 일치, 창 폭 변경 시 Fill/Hug 규칙대로 반응.

### Phase 5 — 생산성 & 부가 출력

| # | 작업 |
|---|---|
| 5.1 | 스마트 가이드 & 오브젝트 스냅 (Edge/Center/Anchor, 간격 균등 표시) — 기존 `SnapManager` 확장 |
| 5.2 | 다중 선택 공통 바운딩 박스 변환 (일괄 스케일/회전/스큐) |
| 5.3 | 그룹 격리 편집 모드 (더블클릭 진입, Esc 탈출) |
| 5.4 | 복사/붙여넣기 (앱 내부 + 클립보드에 SVG 텍스트 제공 → 타 툴로 붙여넣기) |
| 5.5 | PNG/JPG 출력 (WIC + `ID2D1BitmapRenderTarget`, DPI 지정), PDF(선택) |
| 5.6 | 페인트통 도구(`PaintBucketTool`): 클릭 영역의 Fill 적용 (닫힌 영역 탐지는 불리언 Divide 활용) |

---

## 4. 기존 계획 대비 변경 사항

| 기존 계획 (`Object_Architecture_and_Future_Plans.md`) | 본 계획 |
|---|---|
| 1장: `CM2DrawDoc::m_shapes` 기반 구조 설명 | 실제 구조(`ge::Document` 씬 그래프)로 교체 (§1.1) |
| 2·3장: 그룹/Z-Order/정렬/노드 편집 "계획" | 엔진은 구현 완료 → **UI 연결 작업**(2.7)과 버그 수정(0.10, 0.11)으로 축소 |
| 로드맵 1단계: 직렬화 + 불리언 | **Phase 0 결함 제거**를 앞에 추가, 직렬화는 **SVG 기반 포맷**으로 결정 + Undo 동시 진행 |
| 로드맵 2단계: 그라디언트 + 텍스트 | Style 모델 재설계를 Phase 2에, 텍스트는 HTML과 공통이므로 Phase 3으로 분리 |
| 로드맵 3단계: 스마트 가이드 + SVG Export | **SVG Export를 Phase 1로 앞당김**(저장 포맷 겸용). 스마트 가이드는 Phase 5 |
| HTML 출력 | **신규** — Page/FrameNode/LayoutEngine/HtmlExporter (Phase 4) |
| JSON/바이너리 직렬화 | 채택하지 않음 (SVG 확장 포맷으로 저장/출력 일원화) |

---

## 5. 우선순위 요약

```text
Phase 0  결함 제거 (D1~D15)              ← 지금
Phase 1  SVG 저장/불러오기 + Undo/Redo
Phase 2  Style 확장 · SVG 의미론 렌더 · 불리언/패스 연산 · 엔진 기능 UI 연결 · SVG Import
Phase 3  TextNode · ImageNode
Phase 4  Page · FrameNode · LayoutEngine · HtmlExporter
Phase 5  스마트 가이드 · 다중 변환 · 격리 편집 · 클립보드 · PNG
```

## 6. 리스크와 대응

| 리스크 | 대응 |
|---|---|
| D2D와 브라우저의 렌더 차이(안티앨리어싱, 텍스트 셰이핑) | 텍스트는 줄바꿈 결과를 저장하지 않고 "박스 + 스타일"만 저장. 시각 비교 테스트 세트 유지 |
| 불리언 연산 결과가 직선 근사로 노드 수 폭증 | `CombineWithGeometry`의 flatteningTolerance 조정 + Phase 2.6 Simplify 후처리 |
| 스냅샷 Undo의 메모리/속도 | 연속 작업 병합, 스택 상한, 필요 시 세분화 Command(1.7)로 전환 |
| Frame 레이아웃과 자유 변환의 충돌 | 규칙 4.3.4로 Auto Layout 자식의 변환 제한 |
| 대형 리팩터링(Phase 0) 중 회귀 | 항목별 커밋, 각 항목 후 빌드 + 수동 회귀 체크리스트(도형 생성/선택/이동/레이어 토글/삭제) |
