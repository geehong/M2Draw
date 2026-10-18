#include "pch.h"
#include "ITool.h"
#include "M2DrawDoc.h"
#include "M2Draw.h"

// ============================================================================
// SelectTool 구현 (도형 선택, 이동, 스케일, 회전, 에디트 포인트 편집)
// ============================================================================

bool SelectTool::IsOpenPath(Shape* shape)
{
	PathShape* ps = dynamic_cast<PathShape*>(shape);
	if (!ps || ps->paths.empty()) return false;
	for (const auto& sp : ps->paths)
	{
		if (sp.closed) return false;
	}
	return true;
}

HCURSOR SelectTool::GetCursor() const
{
	if (m_hoverIsNode || m_mode == Mode::EditNode)
	{
		return ::LoadCursor(nullptr, IDC_CROSS);
	}

	switch (m_hoverHandle)
	{
	case Handle::TL:
	case Handle::BR:
		return ::LoadCursor(nullptr, IDC_SIZENWSE);
	case Handle::TR:
	case Handle::BL:
		return ::LoadCursor(nullptr, IDC_SIZENESW);
	case Handle::T:
	case Handle::B:
		return ::LoadCursor(nullptr, IDC_SIZENS);
	case Handle::L:
	case Handle::R:
		return ::LoadCursor(nullptr, IDC_SIZEWE);
	case Handle::Rotate:
		return ::LoadCursor(nullptr, IDC_CROSS);
	default:
		return ::LoadCursor(nullptr, IDC_ARROW);
	}
}

SelectTool::Handle SelectTool::HitHandle(D2D1_POINT_2F screenPt, const D2D1_POINT_2F handles[9], float tolPx) const
{
	float tolSq = tolPx * tolPx;
	for (int i = 0; i < 9; ++i)
	{
		float dx = screenPt.x - handles[i].x;
		float dy = screenPt.y - handles[i].y;
		if (dx * dx + dy * dy <= tolSq)
		{
			return static_cast<Handle>(i + 1);
		}
	}
	return Handle::None;
}

bool SelectTool::GetHandleScreenPositions(Shape* shape, const ViewportTransform& view, ID2D1Factory* f,
                                          D2D1_POINT_2F outHandles[9], D2D1_RECT_F* outLocalBounds) const
{
	if (!shape || !f) return false;

	D2D1_RECT_F b;
	if (!shape->GetLocalBounds(f, b)) return false;

	if (outLocalBounds)
	{
		*outLocalBounds = b;
	}

	D2D1_POINT_2F localHandles[9];
	Shape::GetLocalHandlePoints(b, localHandles);

	for (int i = 0; i < 9; ++i)
	{
		outHandles[i] = CoordinateSpace::LocalToScreen(localHandles[i], shape->xf, view);
	}
	return true;
}

void SelectTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	auto& shapes = e.pDoc->GetShapes();
	m_dragStartWorld = e.worldPt;
	m_dragCurrentWorld = e.worldPt;
	D2D1_POINT_2F screenPt = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));

	// 1. 이미 선택된 패스 도형의 노드(앵커, 핸들 In/Out) 클릭 여부 최우선 검사
	for (int idx : m_selectedIndices)
	{
		if (idx >= 0 && idx < static_cast<int>(shapes.size()))
		{
			PathShape* ps = dynamic_cast<PathShape*>(shapes[idx].get());
			if (ps)
			{
				HitResult hr = ps->HitTestScreenSpace(screenPt, 8.0f, e.view, e.pD2DFactory);
				if (hr.part == HitResult::Part::Anchor || hr.part == HitResult::Part::HandleIn || hr.part == HitResult::Part::HandleOut)
				{
					m_mode = Mode::EditNode;
					m_editShapeIdx = idx;
					m_editHit = hr;
					m_handle = Handle::None;
					m_initialXf = ps->xf;

					CString partName = (hr.part == HitResult::Part::Anchor) ? _T("정점(Anchor)") :
					                   (hr.part == HitResult::Part::HandleIn) ? _T("핸들In") : _T("핸들Out");
					CString str;
					str.Format(_T("[선택 도구] %s 조절점 드래그 시작 (도형 #%d, 노드 #%d)"), (LPCTSTR)partName, idx + 1, hr.node + 1);
					LogAppEvent(str);
					return;
				}
			}
		}
	}

	// 2. 이미 선택된 도형 중 닫힌 도형(열린 곡선/직선 제외)의 바운딩 박스 핸들 클릭 여부 검사
	for (int idx : m_selectedIndices)
	{
		if (idx >= 0 && idx < static_cast<int>(shapes.size()))
		{
			Shape* selectedShape = shapes[idx].get();
			if (!IsOpenPath(selectedShape))
			{
				D2D1_POINT_2F handles[9];
				D2D1_RECT_F localBounds;

				if (GetHandleScreenPositions(selectedShape, e.view, e.pD2DFactory, handles, &localBounds))
				{
					Handle hitH = HitHandle(screenPt, handles, 8.0f);
					if (hitH != Handle::None)
					{
						m_handle = hitH;
						m_initialXf = selectedShape->xf;
						m_initialLocalBounds = localBounds;

						if (hitH == Handle::Rotate)
						{
							m_mode = Mode::Rotating;
							LogAppEvent(_T("[선택 도구] 회전 핸들 드래그 시작"));
						}
						else
						{
							m_mode = Mode::Scaling;
							LogAppEvent(_T("[선택 도구] 크기 조절 핸들 드래그 시작"));
						}
						return;
					}
				}
			}
		}
	}

	// 3. 새로운 도형 선택 히트 테스트 (역순: 최상단 도형부터)
	int clickedIndex = -1;
	for (int i = static_cast<int>(shapes.size()) - 1; i >= 0; --i)
	{
		PathShape* ps = dynamic_cast<PathShape*>(shapes[i].get());
		if (ps)
		{
			HitResult hr = ps->HitTestScreenSpace(screenPt, 6.0f, e.view, e.pD2DFactory);
			if (hr.part != HitResult::Part::None)
			{
				clickedIndex = i;
				break;
			}
		}
	}

	if (clickedIndex != -1)
	{
		if (e.ctrlDown)
		{
			if (m_selectedIndices.count(clickedIndex))
				m_selectedIndices.erase(clickedIndex);
			else
				m_selectedIndices.insert(clickedIndex);
		}
		else
		{
			if (!m_selectedIndices.count(clickedIndex))
			{
				m_selectedIndices.clear();
				m_selectedIndices.insert(clickedIndex);
			}
		}

		m_mode = Mode::Moving;
		m_handle = Handle::None;
		Shape* selectedShape = shapes[clickedIndex].get();
		m_initialXf = selectedShape->xf;
		selectedShape->GetLocalBounds(e.pD2DFactory, m_initialLocalBounds);

		CString str;
		str.Format(_T("[선택 도구] 도형 #%d 선택됨 (현재 선택 수: %d개) | 이동 모드 시작"), clickedIndex + 1, (int)m_selectedIndices.size());
		LogAppEvent(str);

		UpdateShapePropertiesSelection(GetSelectedIndex(), e.pDoc);
	}
	else
	{
		if (!e.ctrlDown)
		{
			m_selectedIndices.clear();
			UpdateShapePropertiesSelection(-1, e.pDoc);
		}
		m_mode = Mode::None;
		m_handle = Handle::None;
		LogAppEvent(_T("[선택 도구] 선택 해제"));
	}
}

void SelectTool::SetSelectedIndex(int index, CM2DrawDoc* pDoc)
{
	m_selectedIndices.clear();
	if (index >= 0)
		m_selectedIndices.insert(index);
	UpdateShapePropertiesSelection(index, pDoc);
}

void SelectTool::OnMove(const ToolEvent& e)
{
	if (!e.pDoc) return;

	auto& shapes = e.pDoc->GetShapes();
	m_dragCurrentWorld = e.worldPt;
	D2D1_POINT_2F screenPt = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));

	if (m_mode == Mode::None)
	{
		m_hoverIsNode = false;
		m_hoverHandle = Handle::None;

		for (int idx : m_selectedIndices)
		{
			if (idx >= 0 && idx < static_cast<int>(shapes.size()))
			{
				PathShape* ps = dynamic_cast<PathShape*>(shapes[idx].get());
				if (ps)
				{
					HitResult hr = ps->HitTestScreenSpace(screenPt, 8.0f, e.view, e.pD2DFactory);
					if (hr.part == HitResult::Part::Anchor || hr.part == HitResult::Part::HandleIn || hr.part == HitResult::Part::HandleOut)
					{
						m_hoverIsNode = true;
						return;
					}
				}

				if (!IsOpenPath(shapes[idx].get()))
				{
					D2D1_POINT_2F handles[9];
					if (GetHandleScreenPositions(shapes[idx].get(), e.view, e.pD2DFactory, handles))
					{
						Handle hit = HitHandle(screenPt, handles, 8.0f);
						if (hit != Handle::None)
						{
							m_hoverHandle = hit;
							return;
						}
					}
				}
			}
		}
		return;
	}

	if (m_mode == Mode::EditNode)
	{
		if (m_editShapeIdx >= 0 && m_editShapeIdx < static_cast<int>(shapes.size()))
		{
			PathShape* ps = dynamic_cast<PathShape*>(shapes[m_editShapeIdx].get());
			if (ps)
			{
				ApplyNodeEdit(ps, e);
			}
		}
		return;
	}

	if (m_selectedIndices.empty())
		return;

	int primaryIdx = *m_selectedIndices.begin();
	if (primaryIdx < 0 || primaryIdx >= static_cast<int>(shapes.size()))
		return;

	Shape* selectedShape = shapes[primaryIdx].get();

	switch (m_mode)
	{
	case Mode::Moving:
		ApplyMove(selectedShape);
		break;
	case Mode::Scaling:
		ApplyScale(selectedShape);
		break;
	case Mode::Rotating:
		ApplyRotate(selectedShape);
		break;
	default:
		break;
	}
}

// 엔진에 에디트 포인트 조작 위임!
void SelectTool::ApplyNodeEdit(PathShape* shape, const ToolEvent& e)
{
	if (!shape) return;
	shape->MoveNodeOrHandle(m_editHit.part, m_editHit.subPath, m_editHit.node, e.worldPt);
}

// 엔진에 평행 이동 위임!
void SelectTool::ApplyMove(Shape* shape)
{
	if (!shape) return;
	float dx = m_dragCurrentWorld.x - m_dragStartWorld.x;
	float dy = m_dragCurrentWorld.y - m_dragStartWorld.y;

	const auto& grid = GetApp()->GetGrid();
	const auto& snapMgr = GetApp()->GetSnapManager();
	if (snapMgr.IsEnabled() && HasSnapTarget(snapMgr.GetTargets(), SnapTargets::Grid) && grid.enabled)
	{
		PathShape* ps = dynamic_cast<PathShape*>(shape);
		if (ps && !ps->paths.empty() && !ps->paths[0].nodes.empty())
		{
			D2D1_POINT_2F p0 = m_initialXf.TransformPoint(ps->paths[0].nodes[0].anchor);
			D2D1_POINT_2F targetP0 = grid.SnapPoint(D2D1::Point2F(p0.x + dx, p0.y + dy));
			dx = targetP0.x - p0.x;
			dy = targetP0.y - p0.y;
		}
		else
		{
			float step = grid.GetStepPx();
			if (step > 0.001f)
			{
				dx = static_cast<float>(std::round(dx / step)) * step;
				dy = static_cast<float>(std::round(dy / step)) * step;
			}
		}
	}

	shape->ApplyTranslation(D2D1::Point2F(dx, dy), m_initialXf);
}

// 엔진에 스케일 변형 위임!
void SelectTool::ApplyScale(Shape* shape)
{
	if (!shape) return;
	shape->ScaleByHandle(static_cast<int>(m_handle), m_dragCurrentWorld, m_initialLocalBounds, m_initialXf);
}

// 엔진에 회전 변형 위임!
void SelectTool::ApplyRotate(Shape* shape)
{
	if (!shape) return;
	shape->RotateAboutCenter(m_dragStartWorld, m_dragCurrentWorld, m_initialLocalBounds, m_initialXf);
}

void SelectTool::OnUp(const ToolEvent& e)
{
	if (m_mode != Mode::None)
	{
		CString str;
		str.Format(_T("[선택 도구] 변형 완료 (모드: %d)"), static_cast<int>(m_mode));
		LogAppEvent(str);
	}
	m_mode = Mode::None;
	m_handle = Handle::None;
}

void SelectTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush || !pDoc || m_selectedIndices.empty()) return;

	ViewportTransform view = { zoom, pan };
	ID2D1Factory* f = nullptr;
	rt->GetFactory(&f);
	if (!f) return;

	for (int idx : m_selectedIndices)
	{
		if (idx < 0 || idx >= static_cast<int>(pDoc->GetShapes().size())) continue;

		Shape* shape = pDoc->GetShapes()[idx].get();
		D2D1_POINT_2F handles[9];
		D2D1_RECT_F localBounds;
		if (!GetHandleScreenPositions(shape, view, f, handles, &localBounds))
			continue;

		if (!IsOpenPath(shape))
		{
			D2D1_POINT_2F pTL = handles[0];
			D2D1_POINT_2F pTR = handles[2];
			D2D1_POINT_2F pBR = handles[4];
			D2D1_POINT_2F pBL = handles[6];

			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.85f));
			rt->DrawLine(pTL, pTR, brush, 1.2f);
			rt->DrawLine(pTR, pBR, brush, 1.2f);
			rt->DrawLine(pBR, pBL, brush, 1.2f);
			rt->DrawLine(pBL, pTL, brush, 1.2f);

			D2D1_POINT_2F pT = handles[1];
			D2D1_POINT_2F pRot = handles[8];
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
			rt->DrawLine(pT, pRot, brush, 1.0f);

			bool isRotActive = (m_handle == Handle::Rotate);
			brush->SetColor(isRotActive ? D2D1::ColorF(0.12f, 0.55f, 0.95f, 1.0f) : D2D1::ColorF(D2D1::ColorF::White));
			rt->FillEllipse(D2D1::Ellipse(pRot, 4.5f, 4.5f), brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawEllipse(D2D1::Ellipse(pRot, 4.5f, 4.5f), brush, 1.5f);

			const float hSize = 3.5f;
			for (int i = 0; i < 8; ++i)
			{
				Handle hType = static_cast<Handle>(i + 1);
				bool isSelectedHandle = (m_handle == hType);

				D2D1_RECT_F r = D2D1::RectF(
					handles[i].x - hSize, handles[i].y - hSize,
					handles[i].x + hSize, handles[i].y + hSize
				);

				if (isSelectedHandle)
				{
					brush->SetColor(D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f));
					rt->FillRectangle(r, brush);
					brush->SetColor(D2D1::ColorF(0.05f, 0.25f, 0.70f, 1.0f));
					rt->DrawRectangle(r, brush, 1.6f);

					D2D1_RECT_F outerR = D2D1::RectF(r.left - 2.0f, r.top - 2.0f, r.right + 2.0f, r.bottom + 2.0f);
					brush->SetColor(D2D1::ColorF(0.10f, 0.50f, 0.95f, 0.45f));
					rt->DrawRectangle(outerR, brush, 1.0f);
				}
				else
				{
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
					rt->FillRectangle(r, brush);
					brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
					rt->DrawRectangle(r, brush, 1.2f);
				}
			}
		}

		// 개별 정점(Anchor) 및 베이지어 핸들(Handle) 에디트 포인트 표시
		PathShape* pathShape = dynamic_cast<PathShape*>(shape);
		if (pathShape)
		{
			for (const auto& sp : pathShape->paths)
			{
				for (const auto& node : sp.nodes)
				{
					D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, pathShape->xf, view);

					if (node.HasIn())
					{
						D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), pathShape->xf, view);
						brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
						rt->DrawLine(aScreen, inScreen, brush, 1.0f);
						brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
						rt->FillEllipse(D2D1::Ellipse(inScreen, 3.0f, 3.0f), brush);
					}

					if (node.HasOut())
					{
						D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), pathShape->xf, view);
						brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
						rt->DrawLine(aScreen, outScreen, brush, 1.0f);
						brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
						rt->FillEllipse(D2D1::Ellipse(outScreen, 3.0f, 3.0f), brush);
					}

					D2D1_RECT_F ar = D2D1::RectF(aScreen.x - 3.5f, aScreen.y - 3.5f, aScreen.x + 3.5f, aScreen.y + 3.5f);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
					rt->FillRectangle(ar, brush);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
					rt->DrawRectangle(ar, brush, 1.5f);
				}
			}
		}
	}

	f->Release();
}

// ============================================================================
// BaseShapeTool 구현
// ============================================================================

void BaseShapeTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_previewShape)
	{
		UpdatePreviewGeometry(e.worldPt);
	}
}

void BaseShapeTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	// 1. 도형이 생성되어 드래그 중인 경우: 엔진의 Shape 렌더링 파이프라인 활용
	if (m_previewShape)
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);

		// Shape 본체 그리기 (엔진의 Fill, Stroke, Dash 렌더링 호출)
		m_previewShape->Draw(rt, brush, pFactory, view);

		if (pFactory)
		{
			pFactory->Release();
		}
	}
	// 2. 첫 클릭 전 마우스 호버 상태인 경우: 십자선 & 앵커 마커 안내 표시
	else if (m_hasHover)
	{
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
		DrawCrosshairAndAnchor(rt, brush, sp);
	}
}

bool BaseShapeTool::CommitShape(CM2DrawDoc* pDoc)
{
	if (!pDoc || !m_previewShape) return false;

	// 스타일 동기화
	m_previewShape->style.stroke = m_setting.strokeColor;
	m_previewShape->style.strokeWidth = m_setting.strokeWidth;
	m_previewShape->style.dashStyle = m_setting.dashStyle;
	m_previewShape->style.fillType = m_setting.fillType;
	m_previewShape->style.fill = m_setting.fillColor;

	m_previewShape->Invalidate();
	pDoc->GetShapes().push_back(std::move(m_previewShape));
	m_lastCommittedIndex = static_cast<int>(pDoc->GetShapes().size()) - 1;
	m_previewShape = nullptr;
	return true;
}

void BaseShapeTool::CancelShape()
{
	m_previewShape = nullptr;
}

bool BaseShapeTool::OnDoubleClicked(CM2DrawDoc* pDoc)
{
	CommitShape(pDoc);
	return true; // "도형 완료 후 Select 툴로 전환" 신호
}

bool BaseShapeTool::OnRightClicked(CM2DrawDoc* pDoc)
{
	CommitShape(pDoc);
	return true; // "도형 완료 후 Select 툴로 전환" 신호
}

void BaseShapeTool::DrawCrosshairAndAnchor(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                           D2D1_POINT_2F screenPt)
{
	const float cross = 9.0f;
	brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
	rt->DrawLine(D2D1::Point2F(screenPt.x - cross, screenPt.y), D2D1::Point2F(screenPt.x + cross, screenPt.y), brush, 1.0f);
	rt->DrawLine(D2D1::Point2F(screenPt.x, screenPt.y - cross), D2D1::Point2F(screenPt.x, screenPt.y + cross), brush, 1.0f);

	D2D1_RECT_F hr = D2D1::RectF(screenPt.x - 3.5f, screenPt.y - 3.5f, screenPt.x + 3.5f, screenPt.y + 3.5f);
	brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
	rt->FillRectangle(hr, brush);
	brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
	rt->DrawRectangle(hr, brush, 1.5f);
}

void BaseShapeTool::DrawCommittedShapeAnchors(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                             const ViewportTransform& view, CM2DrawDoc* pDoc)
{
	if (!rt || !brush || !pDoc) return;
	const auto& shapes = pDoc->GetShapes();
	if (m_lastCommittedIndex < 0 || m_lastCommittedIndex >= static_cast<int>(shapes.size())) return;

	PathShape* ps = dynamic_cast<PathShape*>(shapes[m_lastCommittedIndex].get());
	if (!ps) return;

	for (const auto& sp : ps->paths)
	{
		for (const auto& node : sp.nodes)
		{
			D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, ps->xf, view);

			if (node.HasIn())
			{
				D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), ps->xf, view);
				brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.6f));
				rt->DrawLine(aScreen, inScreen, brush, 1.0f);
				brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.8f));
				rt->FillEllipse(D2D1::Ellipse(inScreen, 2.5f, 2.5f), brush);
			}

			if (node.HasOut())
			{
				D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), ps->xf, view);
				brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.6f));
				rt->DrawLine(aScreen, outScreen, brush, 1.0f);
				brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.8f));
				rt->FillEllipse(D2D1::Ellipse(outScreen, 2.5f, 2.5f), brush);
			}

			// 활성 작업 도형 앵커 마커 (화이트 사각 박스 + 딥블루 테두리)
			D2D1_RECT_F ar = D2D1::RectF(aScreen.x - 3.5f, aScreen.y - 3.5f, aScreen.x + 3.5f, aScreen.y + 3.5f);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillRectangle(ar, brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawRectangle(ar, brush, 1.4f);
		}
	}
}

// ============================================================================
// BoxShapeTool 구현 (단일 클래스로 사각/원/직선 등 모든 2포인트 도형 처리)
// ============================================================================

void BoxShapeTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	// 2. 이미 1번째 클릭을 마친 상태(클릭-이동-클릭 모드)인 경우:
	// 이번 2번째 클릭으로 도형 확정 완료!
	if (m_waitingSecondClick)
	{
		float dx = e.worldPt.x - m_startPt.x;
		float dy = e.worldPt.y - m_startPt.y;

		if ((dx * dx + dy * dy) > 16.0f)
		{
			if (m_previewShape)
			{
				UpdateGeometry(m_previewShape.get(), m_startPt, e.worldPt);
			}
			CommitShape(e.pDoc);
		}
		else
		{
			CancelShape();
		}

		m_waitingSecondClick = false;
		m_isDragging = false;
		return;
	}

	// 1. 첫 번째 클릭(다운): 시작점 기록 및 미리보기 생성
	m_startPt = e.worldPt;
	m_isDragging = true;
	m_waitingSecondClick = false;

	// 엔진 팩토리를 통해 도형 인스턴스 생성
	m_previewShape = CreateShapeInstance(m_startPt, m_startPt);
	if (m_previewShape)
	{
		m_previewShape->style.stroke = m_setting.strokeColor;
		m_previewShape->style.strokeWidth = m_setting.strokeWidth;
		m_previewShape->style.dashStyle = m_setting.dashStyle;
		m_previewShape->style.fillType = m_setting.fillType;
		m_previewShape->style.fill = m_setting.fillColor;
		m_previewShape->Invalidate();
	}
}

void BoxShapeTool::OnUp(const ToolEvent& e)
{
	if (!m_isDragging) return;
	m_isDragging = false;

	float dx = e.worldPt.x - m_startPt.x;
	float dy = e.worldPt.y - m_startPt.y;

	// 1. 드래그 방식: 마우스를 누른 채 충분히 이동(4px 초과)하여 뗐다면 즉시 도형 완성!
	if ((dx * dx + dy * dy) > 16.0f)
	{
		if (m_previewShape)
		{
			UpdateGeometry(m_previewShape.get(), m_startPt, e.worldPt);
		}

		CommitShape(e.pDoc);
		m_waitingSecondClick = false;
		// 도구 유지: 연속 그리기 보장
	}
	// 2. 클릭-이동-클릭 방식: 제자리에서 다운 후 업(단순 클릭)한 경우
	// 도형을 취소하지 않고, 마우스 이동에 따라 미리보기를 보여주며 "2번째 클릭"을 대기함!
	else
	{
		m_waitingSecondClick = true;
	}
}

void BoxShapeTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (nChar == VK_ESCAPE)
	{
		CancelShape();
		m_isDragging = false;
		m_waitingSecondClick = false;
	}
}

void BoxShapeTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                               float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	// 1. 첫 번째 포인트가 찍혀서 미리보기가 생성된 상태 (드래그 중이거나 클릭-이동 대기 중)
	if (m_previewShape && (m_isDragging || m_waitingSecondClick))
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);

		// (1) 도형 본체 미리보기 렌더링 (엔진 기하학)
		m_previewShape->Draw(rt, brush, pFactory, view);

		if (pFactory) pFactory->Release();

		// (2) 첫 번째 앵커 포인트 고정 표시 (주황/빨강 강조 마커)
		D2D1_POINT_2F p0Screen = view.WorldToScreen(m_startPt);
		const float sz0 = 4.5f;
		D2D1_RECT_F hr0 = D2D1::RectF(p0Screen.x - sz0, p0Screen.y - sz0, p0Screen.x + sz0, p0Screen.y + sz0);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr0, brush);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::OrangeRed));
		rt->DrawRectangle(hr0, brush, 2.0f);

		// (3) 현재 마우스 위치(두 번째 앵커 포인트 미리보기)
		if (m_hasHover)
		{
			D2D1_POINT_2F p1Screen = view.WorldToScreen(m_hoverPt);
			const float sz1 = 3.5f;
			D2D1_RECT_F hr1 = D2D1::RectF(p1Screen.x - sz1, p1Screen.y - sz1, p1Screen.x + sz1, p1Screen.y + sz1);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillRectangle(hr1, brush);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
			rt->DrawRectangle(hr1, brush, 1.5f);

			// 직선 도구인 경우 시작점과 끝점을 잇는 대각 가이드선 강조
			if (m_kind == ShapeKind::Line)
			{
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
				rt->DrawLine(p0Screen, p1Screen, brush, 1.0f);
			}
		}
	}
	// 2. 첫 클릭 전 대기 상태 (동일 도형 연속 그리기 준비 상태)
	else
	{
		// 방금 완성된 직전 작업 도형의 앵커/핸들 표시
		DrawCommittedShapeAnchors(rt, brush, view, pDoc);

		// 마우스 위치에 십자선 & 앵커 마커 안내
		if (m_hasHover)
		{
			D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
			DrawCrosshairAndAnchor(rt, brush, sp);
		}
	}
}

void BoxShapeTool::UpdatePreviewGeometry(D2D1_POINT_2F curPt)
{
	if (m_previewShape)
	{
		UpdateGeometry(m_previewShape.get(), m_startPt, curPt);
	}
}

std::unique_ptr<PathShape> BoxShapeTool::CreateShapeInstance(D2D1_POINT_2F p0, D2D1_POINT_2F p1)
{
	auto shape = std::make_unique<PathShape>();

	if (m_kind == ShapeKind::Line)
	{
		shape->name = _T("직선");
		shape->AddAnchor(p0);
		shape->AddAnchor(p1);
	}
	else if (m_kind == ShapeKind::Rect)
	{
		shape->name = _T("사각형");
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->paths.back().closed = true;
	}
	else if (m_kind == ShapeKind::Ellipse)
	{
		shape->name = _T("타원");
		SubPath sp;
		for (int i = 0; i < 4; ++i)
		{
			BezierNode n;
			n.type = BezierNode::Type::Smooth;
			sp.nodes.push_back(n);
		}
		sp.closed = true;
		shape->paths.push_back(sp);
	}

	UpdateGeometry(shape.get(), p0, p1);
	return shape;
}

void BoxShapeTool::UpdateGeometry(PathShape* pShape, D2D1_POINT_2F p0, D2D1_POINT_2F p1)
{
	if (!pShape || pShape->paths.empty()) return;

	if (m_kind == ShapeKind::Line)
	{
		if (pShape->paths[0].nodes.size() < 2) return;
		pShape->paths[0].nodes[0].anchor = p0;
		pShape->paths[0].nodes[1].anchor = p1;
	}
	else if (m_kind == ShapeKind::Rect)
	{
		if (pShape->paths[0].nodes.size() < 4) return;
		float left = min(p0.x, p1.x);
		float top = min(p0.y, p1.y);
		float right = max(p0.x, p1.x);
		float bottom = max(p0.y, p1.y);

		pShape->paths[0].nodes[0].anchor = D2D1::Point2F(left, top);
		pShape->paths[0].nodes[1].anchor = D2D1::Point2F(right, top);
		pShape->paths[0].nodes[2].anchor = D2D1::Point2F(right, bottom);
		pShape->paths[0].nodes[3].anchor = D2D1::Point2F(left, bottom);
	}
	else if (m_kind == ShapeKind::Ellipse)
	{
		if (pShape->paths[0].nodes.size() < 4) return;
		float left = min(p0.x, p1.x);
		float top = min(p0.y, p1.y);
		float right = max(p0.x, p1.x);
		float bottom = max(p0.y, p1.y);

		float rx = (right - left) * 0.5f;
		float ry = (bottom - top) * 0.5f;
		float cx = left + rx;
		float cy = top + ry;

		const float kappa = 0.55228475f;
		float kx = rx * kappa;
		float ky = ry * kappa;

		auto& nodes = pShape->paths[0].nodes;
		nodes[0].anchor = D2D1::Point2F(cx, cy - ry);
		nodes[0].hIn = D2D1::Point2F(-kx, 0.0f);
		nodes[0].hOut = D2D1::Point2F(kx, 0.0f);

		nodes[1].anchor = D2D1::Point2F(cx + rx, cy);
		nodes[1].hIn = D2D1::Point2F(0.0f, -ky);
		nodes[1].hOut = D2D1::Point2F(0.0f, ky);

		nodes[2].anchor = D2D1::Point2F(cx, cy + ry);
		nodes[2].hIn = D2D1::Point2F(kx, 0.0f);
		nodes[2].hOut = D2D1::Point2F(-kx, 0.0f);

		nodes[3].anchor = D2D1::Point2F(cx - rx, cy);
		nodes[3].hIn = D2D1::Point2F(0.0f, ky);
		nodes[3].hOut = D2D1::Point2F(0.0f, -ky);
	}

	pShape->Invalidate();
}

// ============================================================================
// MultiPointShapeTool 구현 (다각형, 열린 폴리라인 등 다중 클릭 생성)
// ============================================================================

void MultiPointShapeTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	if (!m_previewShape)
	{
		m_previewShape = std::make_unique<PathShape>();
		m_previewShape->name = m_pName;
		m_previewShape->style.stroke = m_setting.strokeColor;
		m_previewShape->style.strokeWidth = m_setting.strokeWidth;
		m_previewShape->style.dashStyle = m_setting.dashStyle;
		m_previewShape->style.fillType = m_setting.fillType;
		m_previewShape->style.fill = m_setting.fillColor;
	}

	// 3개 이상 정점이 있을 때 첫 정점(8px 이내) 스냅 클릭 시 폐합 완료
	if (!m_previewShape->paths.empty() && !m_previewShape->paths.back().nodes.empty())
	{
		const auto& firstNode = m_previewShape->paths.back().nodes[0];
		D2D1_POINT_2F firstScreen = CoordinateSpace::LocalToScreen(firstNode.anchor, m_previewShape->xf, e.view);
		float dx = firstScreen.x - static_cast<float>(e.screenPt.x);
		float dy = firstScreen.y - static_cast<float>(e.screenPt.y);

		if (m_previewShape->paths.back().nodes.size() >= 3 && (dx * dx + dy * dy) <= 64.0f)
		{
			// 첫 점 스냅 클릭: 폐합 완료하고 문서에 확정 (도구는 유지!)
			m_previewShape->paths.back().closed = true;
			CommitShape(e.pDoc);
			LogAppEvent(_T("[다각형 폐합 완료] 첫 정점 스냅"));
			return;
		}
	}

	// 직전에 찍은 정점과 동일하거나 매우 가까우면(더블클릭의 두 번째 클릭 등) 중복 추가 방지
	if (!m_previewShape->paths.empty() && !m_previewShape->paths.back().nodes.empty())
	{
		const auto& lastNode = m_previewShape->paths.back().nodes.back();
		float ddx = lastNode.anchor.x - e.worldPt.x;
		float ddy = lastNode.anchor.y - e.worldPt.y;
		if ((ddx * ddx + ddy * ddy) < 1.0f)
		{
			return; // 동일 위치 클릭 중복 추가 무시
		}
	}

	m_previewShape->AddAnchor(e.worldPt);
}

void MultiPointShapeTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (!pDoc || !m_previewShape) return;

	if (nChar == VK_RETURN)
	{
		// Enter 키로 완료 (설정에 따라 열림/닫힘 반영, 도구는 유지)
		if (!m_previewShape->paths.empty() && m_previewShape->paths.back().nodes.size() >= 2)
		{
			m_previewShape->paths.back().closed = m_setting.bClosed;
			CommitShape(pDoc);
			LogAppEvent(_T("[다각형 생성 완료] (Enter)"));
		}
		else
		{
			CancelShape();
		}
	}
	else if (nChar == VK_ESCAPE)
	{
		CancelShape();
		LogAppEvent(_T("[다각형 작성 취소] (Escape)"));
	}
}

bool MultiPointShapeTool::OnDoubleClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return true;

	if (m_previewShape && !m_previewShape->paths.empty())
	{
		auto& nodes = m_previewShape->paths.back().nodes;

		// 1. 더블클릭 시 바로 직전 점과 동일한 좌표가 연속으로 들어갔다면 마지막 중복점 1개 제거
		if (nodes.size() >= 2)
		{
			const auto& last1 = nodes[nodes.size() - 1];
			const auto& last2 = nodes[nodes.size() - 2];
			float dx = last1.anchor.x - last2.anchor.x;
			float dy = last1.anchor.y - last2.anchor.y;
			if ((dx * dx + dy * dy) < 4.0f)
			{
				nodes.pop_back();
			}
		}

		// 2. 최소 2개 이상(폐곡선이면 3개 이상 권장) 점이 있으면 확정 등록
		if (nodes.size() >= 2)
		{
			m_previewShape->paths.back().closed = m_setting.bClosed;
			CommitShape(pDoc);
			LogAppEvent(_T("[다각형 생성 완료] (더블클릭 -> 선택 도구 전환)"));
		}
		else
		{
			CancelShape();
		}
	}
	else
	{
		CancelShape();
	}
	return true; // Select 툴로 전환 신호
}

bool MultiPointShapeTool::OnRightClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return true;

	// 우클릭 시: 작성 중인 정점들이 있으면 도형을 확정하고, 없으면 취소 후 선택 도구 전환
	if (m_previewShape && !m_previewShape->paths.empty() && m_previewShape->paths.back().nodes.size() >= 2)
	{
		m_previewShape->paths.back().closed = m_setting.bClosed;
		CommitShape(pDoc);
		LogAppEvent(_T("[다각형 생성 완료] (우클릭 -> 선택 도구 전환)"));
	}
	else
	{
		CancelShape();
	}
	return true; // Select 툴로 전환 신호
}

void MultiPointShapeTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                                      float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	if (m_previewShape && !m_previewShape->paths.empty())
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);
		m_previewShape->Draw(rt, brush, pFactory, view);
		if (pFactory) pFactory->Release();

		const auto& nodes = m_previewShape->paths.back().nodes;
		if (!nodes.empty())
		{
			// 1. 확정 정점 선 연결
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
			for (size_t i = 1; i < nodes.size(); ++i)
			{
				D2D1_POINT_2F pA = CoordinateSpace::LocalToScreen(nodes[i - 1].anchor, m_previewShape->xf, view);
				D2D1_POINT_2F pB = CoordinateSpace::LocalToScreen(nodes[i].anchor, m_previewShape->xf, view);
				rt->DrawLine(pA, pB, brush, 1.5f);
			}

			// 2. 마지막 점에서 현재 마우스 호버 위치로 실시간 연결선
			if (m_hasHover)
			{
				D2D1_POINT_2F pLast = CoordinateSpace::LocalToScreen(nodes.back().anchor, m_previewShape->xf, view);
				D2D1_POINT_2F pNext = view.WorldToScreen(m_hoverPt);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
				rt->DrawLine(pLast, pNext, brush, 1.5f);

				// 2개 이상이면 첫 정점으로의 은은한 가상 폐합 가이드선
				if (nodes.size() >= 2)
				{
					D2D1_POINT_2F pFirst = CoordinateSpace::LocalToScreen(nodes.front().anchor, m_previewShape->xf, view);
					brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.45f));
					rt->DrawLine(pNext, pFirst, brush, 1.0f);
				}
			}

			// 3. 앵커 점들 표시 (첫 정점 강조)
			for (size_t i = 0; i < nodes.size(); ++i)
			{
				D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(nodes[i].anchor, m_previewShape->xf, view);
				float sz = (i == 0) ? 4.5f : 3.5f;
				D2D1_RECT_F hr = D2D1::RectF(aScreen.x - sz, aScreen.y - sz, aScreen.x + sz, aScreen.y + sz);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->FillRectangle(hr, brush);
				brush->SetColor((i == 0) ? D2D1::ColorF(D2D1::ColorF::OrangeRed) : D2D1::ColorF(D2D1::ColorF::DarkBlue));
				rt->DrawRectangle(hr, brush, (i == 0) ? 1.8f : 1.2f);
			}
		}
	}
	else
	{
		DrawCommittedShapeAnchors(rt, brush, view, pDoc);

		if (m_hasHover)
		{
			D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
			DrawCrosshairAndAnchor(rt, brush, sp);
		}
	}
}
