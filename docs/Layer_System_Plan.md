# M2Draw 레이어 시스템 및 UI 상세 구현 계획 (Layer System Plan)

본 문서는 M2Draw의 **씬 그래프 레이어 아키텍처(D2DLayer.h/cpp)**, **Direct2D 레이어 렌더링 파이프라인**, **잉크스케이프(Inkscape) 스타일 레이어/오브젝트 트리 패널(`CLayerWnd`) UI 명세**, **오브젝트 자동 명명 규칙(rect1, path1 등)**, **Undo/Redo 및 히트테스트 연동**에 대한 종합 구현 계획을 정의합니다.

---

## 1. 개요 및 설계 목표

1. **독립 모듈화 (`D2DLayer.h / D2DLayer.cpp`)**:
   - `D2DGraphicEngine.h` 내 최소 정의만 되어 있던 `LayerNode`를 독립 클래스 및 파일로 분리.
   - `Node` ➔ `GroupNode` ➔ `LayerNode`의 객체지향 계층 구조 확립.
2. **레이어 속성(Attribute) 고도화 (잉크스케이프 스타일)**:
   - 레이어별 **이름(Name)**, **보기/가시성(Visible 👁)**, **잠금(Lock 🔒)**, **투명도/불투명도(Opacity 0~100%)**, **레이어 간 혼합 모드(Blend Mode: Normal, Multiply, Screen, Darken, Lighten 등)**.
3. **오브젝트 자동 명명 규칙 (잉크스케이프 스타일 `rect1`, `path1`)**:
   - 기존의 `{도형#1}`, `{도형#2}` 형태를 탈피.
   - 도형 종류별 고유 접두어 + 순번 카운터로 자동 생성:
     - 사각형: `rect1`, `rect2`, ...
     - 타원: `ellipse1`, `ellipse2`, ...
     - 삼각형: `triangle1`, `triangle2`, ...
     - 다각형: `polygon1`, `polygon2`, ...
     - 별: `star1`, `star2`, ...
     - 기어: `gear1`, `gear2`, ...
     - 나선: `spiral1`, `spiral2`, ...
     - 하트: `heart1`, `heart2`, ...
     - 펜/패스: `path1`, `path2`, ...
     - 그룹: `group1`, `group2`, ...
4. **전용 도킹 패널 (`CLayerWnd`) - 잉크스케이프 스타일 트리 구조**:
   - 상단 헤더: **혼합 모드(Blend Mode) 드롭다운**, **투명도(Opacity) 슬라이더**, **검색**, **새 레이어(+) / 위로(▲) / 아래로(▼) / 삭제(🗑)**.
   - 중앙: **레이어 및 하위 오브젝트 계층 트리뷰 (Layer ➔ Children Objects)**.
   - 각 항목별 빠른 토글 아이콘: **보기(👁)**, **잠금(🔒)**.
5. **Direct2D 하드웨어 레이어 렌더링**:
   - `ID2D1RenderTarget::PushLayer` / `PopLayer` 및 Direct2D Effect를 연동하여 레이어 단위 알파 블렌딩 및 혼합 모드 렌더링.

---

## 2. 오브젝트 자동 명명 규칙 모델

### 2.1 명명 규칙 정의
도형이 생성될 때 문서(Document) 레벨에서 종류별 일련번호 카운터를 유지하거나, 기존 노드들의 이름을 검사하여 중복 없는 고유 영문 소문자 이름을 자동 발급합니다.

| 도형 종류 (`ShapeKind` / `NodeKind`) | 영문 기본 접두사 (Prefix) | 예시 명칭 |
| :--- | :--- | :--- |
| **사각형 (Rect)** | `rect` | `rect1`, `rect2` |
| **타원 (Ellipse)** | `ellipse` | `ellipse1`, `ellipse2` |
| **삼각형 (Triangle)** | `triangle` | `triangle1` |
| **정다각형 (Polygon)** | `polygon` | `polygon1` |
| **별 (Star)** | `star` | `star1` |
| **기어 (Gear)** | `gear` | `gear1` |
| **스파이럴 (Spiral)** | `spiral` | `spiral1` |
| **하트 (Heart)** | `heart` | `heart1` |
| **선 (Line)** | `line` | `line1` |
| **자유 패스 / 펜 (Path)** | `path` | `path1` |
| **그룹 (Group)** | `group` | `group1` |
| **레이어 (Layer)** | `Layer ` | `Layer 1`, `Layer 2` |

### 2.2 구현 위치
- `ge::Document::GenerateNodeName(ShapeKind kind)`: 활성 문서 내에서 해당 접두사로 시작하는 최대 번호를 찾아 `prefix + to_wstring(nextNum)` 반환.
- `ITool.cpp`의 생성 시점(`BoxShapeTool`, `PenTool` 등)에서 하드코딩된 한국어 명칭(`"사각형"`, `"타원"`) 대신 `GenerateNodeName` 호출하여 자동 부여.
- 상단 도구 옵션바(`ToolOptionBar`) 및 우측 패널의 수정 모드 타이틀도 `node->GetName()`을 직접 바인딩하여 `rect1 수정:`, `ellipse2 수정:` 형태로 통일.

---

## 3. 레이어 데이터 모델 설계 (`D2DLayer.h / D2DLayer.cpp`)

### 3.1 레이어 혼합 모드 (Blend Mode)
```cpp
namespace ge {

enum class LayerBlendMode : uint8_t {
    Normal = 0,    // 기본 알파 블렌딩
    Multiply,      // 곱하기
    Screen,        // 스크린
    Darken,        // 어둡게
    Lighten,       // 밝게
    ColorDodge,    // 색상 닷지
    ColorBurn,     // 색상 번
    Overlay        // 오버레이
};

}
```

### 3.2 `LayerNode` 클래스 명세
```cpp
namespace ge {

class LayerNode : public GroupNode {
public:
    LayerNode();
    explicit LayerNode(const std::wstring& name);
    virtual ~LayerNode();

    NodeKind Kind() const override { return NodeKind::Layer; }
    std::unique_ptr<Node> Clone() const override;

    // --- 잉크스케이프 레이어 핵심 속성 ---
    bool  IsVisible() const noexcept        { return m_bVisible; }
    void  SetVisible(bool visible) noexcept { m_bVisible = visible; }

    bool  IsLocked() const noexcept         { return m_bLocked; }
    void  SetLocked(bool locked) noexcept   { m_bLocked = locked; }

    float GetOpacity() const noexcept       { return m_opacity; } // 0.0f ~ 1.0f (0~100%)
    void  SetOpacity(float opacity) noexcept;

    LayerBlendMode GetBlendMode() const noexcept { return m_blendMode; }
    void           SetBlendMode(LayerBlendMode mode) noexcept { m_blendMode = mode; }

    // 클리핑 마스크 지원
    bool  IsClippingMask() const noexcept     { return m_bClippingMask; }
    void  SetClippingMask(bool mask) noexcept { m_bClippingMask = mask; }

    // --- 렌더링 연동 ---
    D2D1_LAYER_PARAMETERS GetD2DLayerParameters(ID2D1Factory* pFactory) const;

private:
    bool           m_bVisible      = true;
    bool           m_bLocked       = false;
    float          m_opacity       = 1.0f;     // 100%
    LayerBlendMode m_blendMode     = LayerBlendMode::Normal;
    bool           m_bClippingMask = false;
};

} // namespace ge
```

---

## 4. 레이어 & 오브젝트 패널 UI 설계 (`CLayerWnd` : `CDockablePane`)

잉크스케이프(Inkscape)의 `Layers and Objects` 도킹 다이얼로그와 동일한 레이아웃과 조작 방식을 채택합니다.

### 4.1 패널 레이아웃 구성
```text
+-------------------------------------------------------------------------+
| Layers and Objects                                                [X]   |
+-------------------------------------------------------------------------+
| [+] 새 레이어 | [▲] 위로 | [▼] 아래로 | [🗑] 삭제                                 |
+-------------------------------------------------------------------------+
| v [혼합 모드 ▼] [투명도] [👁] [🔒] Layer 1                                 | <- 레이어 (컨테이너)
|                  [투명도] [👁] [🔒] rect1                                  | <- 자식 오브젝트
|                  [투명도] [👁] [🔓] ellipse1                               |
| > [혼합 모드 ▼] [투명도] [👁] [🔒] Layer 2                                 |
|                  [투명도] [🙈] [🔒] path1 (숨김/잠김)                      |
+-------------------------------------------------------------------------+
```
- **아이콘 연동**:
  1. **투명도**: `res/icon/transparency.svg` 체커보드 벡터 아이콘 버튼 클릭 시 불투명도 슬라이더 팝업 (`COpacityPopupWnd`) 표시
  2. **보기/안보기**: `res/icon/eye.svg` (보임) / `res/icon/eye-closed.svg` (숨김) 상태 벡터 아이콘 체크박스
  3. **잠금/풀림**: `res/icon/lock-sharp.svg` (잠김) / `res/icon/unlock-sharp.svg` (풀림) 상태 벡터 아이콘 체크박스
  4. **혼합 모드**: 투명도에서 분리되어 레이어 행에 독립 커스텀 드롭다운 메뉴(`M2DrawComboBox`)로 배치되어 16개 혼합 모드 즉시 변경

### 4.2 구성 요소 상세
1. **상단 툴바 버튼군**:
   - `[+] New Layer`: 활성 레이어 상단에 새 레이어 생성 (`레이어 N`).
   - `[▲] Move Up` / `[▼] Move Down`: 활성 레이어의 Z-Order 이동.
   - `[🗑] Delete`: 활성 레이어 삭제.
2. **리스트 행 구성 (M2DrawControl 기반 순수 D2D)**:
   - **계층 구조**: 레이어(루트 노드, Depth 0) ➔ 하위 그룹/도형(자식 노드, Depth 1).
   - **혼합 모드 커스텀 드롭다운 (`M2DrawComboBox`)**: 레이어 전용 16대 블렌드 모드 드롭다운.
   - **투명도 버튼 (`M2DrawButton`)**: `transparency.svg` 기반 벡터 아이콘 및 플로팅 불투명도 슬라이더.
   - **보기 아이콘 (`M2DrawCheckBox`)**: `eye.svg` / `eye-closed.svg` 가시성 실시간 토글.
   - **잠금 아이콘 (`M2DrawCheckBox`)**: `lock-sharp.svg` / `unlock-sharp.svg` 잠금 상태 실시간 토글.
   - **오브젝트 명 (`rect1`, `레이어 1`)**:
     - 클릭 시 해당 도형 캔버스 및 속성창 선택 동기화.
     - 더블클릭 또는 천천히 재클릭 시 인라인 이름 변경 편집창 활성화.
     - 더블 클릭 또는 F2 키 입력 시 인라인 이름 편집.
   - **드래그 앤 드롭 (Drag & Drop)**:
     - 오브젝트를 다른 레이어로 이동하거나 Z-Order 순서 변경.
3. **하단 레이어 속성 제어 바**:
   - **혼합 모드(Blend Mode)**: 드롭다운 콤보박스 (`Normal`, `Multiply`, `Screen`, `Darken`, `Lighten` 등).
   - **투명도(Opacity)**: 수평 슬라이더 + 수치 에디트 (0% ~ 100%).

---

## 5. Direct2D 렌더링 파이프라인 연동

### 5.1 렌더 루프 흐름 (`D2DGraphicEngine.cpp`)
1. **가시성 체크**: `!layer->IsVisible()` 인 경우 레이어 전체 스킵.
2. **PushLayer 처리**:
   - `layer->GetOpacity() < 1.0f` 또는 `layer->GetBlendMode() != Normal` 또는 마스크 활성 시:
   - `D2D1_LAYER_PARAMETERS`에 `opacity` 설정 후 `pRT->PushLayer(params, pD2DLayer)` 호출.
3. **자식 노드 렌더링**:
   - 숨겨진 자식 노드(`!node->IsVisible()`)는 건너뜀.
   - 화면에 표시되는 도형들을 순서대로 Draw.
4. **PopLayer**:
   - PushLayer가 호출된 경우 `pRT->PopLayer()` 호출.

### 5.2 히트테스트 및 선택 연동
- 숨겨진 레이어나 숨겨진 도형: 히트테스트 제외.
- 잠긴 레이어나 잠긴 도형: 캔버스 마우스 클릭 선택 방지 (안내선 표시 옵션).
- 캔버스에서 도형을 클릭하면 레이어 패널의 해당 트리 노드가 자동으로 동기화되어 선택 상태로 하이라이트.

---

## 6. MainFrame 도킹 배치 (`MainFrm.cpp`)

우측 패널의 `CPropertiesWnd`("Object Settings")과 **탭 그룹**으로 결합하여 작업 공간을 절약합니다.

```cpp
// MainFrm.cpp (CreateDockingWindows)
CString strLayerWnd = _T("Layers and Objects");
if (!m_wndLayers.Create(strLayerWnd, this, CRect(0, 0, 260, 400), TRUE, 
    ID_VIEW_LAYERWND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
{
    return FALSE;
}

// Object Settings 패널과 탭 결합
m_wndLayers.DockToWindow(&m_wndProperties, CBRS_ALIGN_BOTTOM);
m_wndProperties.AttachToTabWnd(&m_wndLayers, DM_SHOW, TRUE);
```

---

## 7. 단계별 구현 로드맵

| 단계 | 작업 내용 | 세부 작업 |
| :---: | :--- | :--- |
| **Step 1** | **도형 자동 명명 규칙 적용 (`rect1`, `path1`)** | • `ge::Document::GenerateNodeName` 구현<br>• `ITool.cpp`의 도형 생성 로직에서 한국어 하드코딩 제거 및 `rect1` 부여<br>• 옵션바/속성창 표시 이름 연동 |
| **Step 2** | **레이어 코어 분리 (`D2DLayer.h/cpp`)** | • `D2DLayer.h / D2DLayer.cpp` 생성<br>• `LayerNode` 분리 및 `Opacity`, `BlendMode`, `Visible`, `Locked` 속성 구현<br>• `Document`의 레이어 CRUD 메서드 확장 |
| **Step 3** | **D2D 렌더 파이프라인 연동** | • `D2DGraphicEngine.cpp` 내 `PushLayer / PopLayer` 및 투명도/혼합모드 적용<br>• 숨김/잠김 레이어/도형의 HitTest 필터링 |
| **Step 4** | **잉크스케이프 스타일 패널 구현 (`CLayerWnd`)** | • `LayerWnd.h / cpp` 생성 (`CDockablePane`)<br>• 상단 툴바 + 중앙 계층 트리(레이어/도형) + 하단 혼합모드/투명도 컨트롤 구현<br>• 눈(👁)/자물쇠(🔒) 클릭 토글 및 이름 인라인 편집 |
| **Step 5** | **MainFrame 도킹 및 선택 양방향 동기화** | • `MainFrm` 우측 탭 도킹<br>• 캔버스 선택 ↔ 레이어 트리 항목 실시간 양방향 동기화<br>• 레이어/도형 추가/삭제/이동 Undo/Redo 연동 |

---

## 8. 테스트 및 검증 항목

1. [ ] **자동 명명**: 사각형 생성 시 `rect1`, `rect2`, 타원 생성 시 `ellipse1` 형태로 이름이 부여되는지 확인.
2. [ ] **계층 트리 표시**: 레이어 아래에 생성된 도형들이 트리 항목으로 올바르게 전개되는지 확인.
3. [ ] **보기(👁) 토글**: 트리에서 눈 아이콘 클릭 시 해당 레이어 또는 개별 도형이 즉시 숨겨지고 캔버스에서 선택되지 않는지 확인.
4. [ ] **잠금(🔒) 토글**: 자물쇠 클릭 시 해당 레이어 또는 도형이 이동/수정되지 않도록 보호되는지 확인.
5. [ ] **투명도 & 혼합모드**: 하단 슬라이더 조절 시 레이어 전체가 반투명해지며 혼합 모드가 정상 반영되는지 확인.
6. [ ] **양방향 동기화**: 캔버스에서 도형 클릭 시 레이어 트리의 해당 항목이 선택되고, 트리에서 클릭 시 캔버스의 선택 상자가 갱신되는지 확인.
