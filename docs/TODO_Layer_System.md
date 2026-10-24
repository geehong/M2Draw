# TODO: 레이어 시스템 및 잉크스케이프 스타일 패널 구현 작업 목록

이 문서는 [Layer_System_Plan.md](file:///c:/www/project/desktop/ApiDDraw/M2Draw/docs/Layer_System_Plan.md)에 따른 구체적인 실행 단계와 진행 상태를 관리합니다.

---

## [Task 1] 도형 자동 명명 규칙 구현 (Inkscape 스타일: rect1, ellipse1 등)
- [x] 1.1 `D2DGraphicEngine.h` / `.cpp`의 `ge::Document`에 `std::wstring GenerateNodeName(ShapeKind kind)` 구현
- [x] 1.2 `ITool.cpp`의 `BoxShapeTool` 및 `PenTool` 등 도형 생성 시점에서 기존 한국어 명칭 대신 `GenerateNodeName` 호출하여 자동 부여
- [x] 1.3 `ToolOptionBar.cpp` 및 `D2DPropertyView.cpp`에서 수정 모드 타이틀 표시 시 `{도형 #1}` 대신 `rect1`, `ellipse1` 등 실제 노드명 표시 확인 및 연동
- [x] 1.4 단위 빌드 및 생성 테스트

---

## [Task 2] 레이어 코어 모듈 분리 및 속성 확장 (`D2DLayer.h / cpp`)
- [x] 2.1 `D2DLayer.h` 및 `D2DLayer.cpp` 파일 생성
  - `NodeKind::Layer`
  - `LayerBlendMode` (Normal, Multiply, Screen, Darken, Lighten 등)
  - `LayerNode : public GroupNode` 확장 (m_bVisible, m_bLocked, m_opacity, m_blendMode)
  - `GetD2DLayerParameters` 계산 함수
- [x] 2.2 `D2DGraphicEngine.h`에서 `LayerNode` 선언을 `D2DLayer.h` 인클루드로 연결
- [x] 2.3 `M2Draw.vcxproj` 및 `M2Draw.vcxproj.filters`에 `D2DLayer.h / cpp` 등록
- [x] 2.4 컴파일 성공 검증

---

## [Task 3] Direct2D 렌더 파이프라인 및 히트테스트 연동
- [x] 3.1 `D2DGraphicEngine.cpp`의 레이어 렌더 루프에 `PushLayer` / `PopLayer` 및 불투명도(Opacity) 연동
- [x] 3.2 숨겨진 레이어(`!m_bVisible`) 렌더링 스킵 처리
- [x] 3.3 히트테스트/선택 시 숨김/잠김 레이어 및 도형 제외 필터링 (`HitTest` 보완)

---

## [Task 4] 잉크스케이프 스타일 패널 UI 구현 (`CLayerWnd` : `CDockablePane`)
- [x] 4.1 `LayerWnd.h` 및 `LayerWnd.cpp` 생성
- [x] 4.2 상단 툴바 버튼 (`[+] 새 레이어`, `[▲] 위로`, `[▼] 아래로`, `[🗑] 삭제`)
- [x] 4.3 중앙 레이어-오브젝트 계층 트리 뷰 (`CTreeCtrl` 또는 커스텀 리스트)
  - 보기/가시성(👁) 아이콘 클릭 토글
  - 잠금(🔒) 아이콘 클릭 토글
  - 항목 더블클릭/F2 이름 인라인 편집
- [x] 4.4 하단 레이어 속성 바 (혼합 모드 Blend Mode 콤보박스 + 투명도 Opacity 슬라이더/에디트)
- [x] 4.5 `M2Draw.vcxproj` 및 `.filters`에 `LayerWnd.h / cpp` 등록

---

## [Task 5] MainFrame 도킹 연동 및 양방향 동기화
- [x] 5.1 `Resource.h` / `MainFrm.cpp`에 도킹 ID 정의
- [x] 5.2 `MainFrm.h` / `MainFrm.cpp`에 `CLayerWnd m_wndLayers` 추가 및 우측 탭 도킹 연동
- [x] 5.3 캔버스에서 도형 선택 ↔ 레이어 트리 항목 선택 실시간 동기화
- [x] 5.4 빌드 및 전체 동작 검증
