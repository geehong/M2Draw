#include "pch.h"
#include "ToolManager.h"
#include "M2DrawDoc.h"
#include "M2Draw.h"

namespace
{
	// 앵커(대변/대각 정점)를 고정점으로 하여 스케일하는 합성 행렬 계산 (순수 수학 함수)
	D2D1::Matrix3x2F ScaleAboutAnchor(D2D1_POINT_2F anchorLocal, float sx, float sy)
	{
		return D2D1::Matrix3x2F::Translation(-anchorLocal.x, -anchorLocal.y) *
		       D2D1::Matrix3x2F::Scale(sx, sy) *
		       D2D1::Matrix3x2F::Translation(anchorLocal.x, anchorLocal.y);
	}

	// 로컬 사각 바운딩박스의 8개 핸들(TL, T, TR, R, BR, B, BL, L) 및 회전 핸들(Rotate) 로컬 좌표 계산
	void GetLocalHandlePoints(const D2D1_RECT_F& b, D2D1_POINT_2F outHandles[9])
	{
		float midX = (b.left + b.right) * 0.5f;
		float midY = (b.top + b.bottom) * 0.5f;

		outHandles[0] = D2D1::Point2F(b.left,  b.top);     // TL
		outHandles[1] = D2D1::Point2F(midX,    b.top);     // T
		outHandles[2] = D2D1::Point2F(b.right, b.top);     // TR
		outHandles[3] = D2D1::Point2F(b.right, midY);      // R
		outHandles[4] = D2D1::Point2F(b.right, b.bottom);  // BR
		outHandles[5] = D2D1::Point2F(midX,    b.bottom);  // B
		outHandles[6] = D2D1::Point2F(b.left,  b.bottom);  // BL
		outHandles[7] = D2D1::Point2F(b.left,  midY);      // L
		outHandles[8] = D2D1::Point2F(midX,    b.top - 20.0f); // Rotate (상단 20px 위)
	}
}

// ---------------- SelectTool ----------------
bool SelectTool::IsOpenPath(Shape* shape)
{
	PathShape* ps = dynamic_cast<PathShape*>(shape);
	if (!ps || ps->paths.empty()) return false;
	// 모든 서브패스가 열려있으면 열린 패스
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
	GetLocalHandlePoints(b, localHandles);

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
		// Ctrl 키를 누른 상태이면 다중 선택 토글
		if (e.ctrlDown)
		{
			if (m_selectedIndices.count(clickedIndex))
			{
				m_selectedIndices.erase(clickedIndex);
			}
			else
			{
				m_selectedIndices.insert(clickedIndex);
			}
		}
		else
		{
			// 이미 선택된 그룹 안에 포함된 항목을 클릭한 게 아니라면 단일 선택으로 교체
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
		// 빈 공간 클릭 (Ctrl 키가 아니면 선택 해제)
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
		// 마우스 호버 감지 (노드 에디트 포인트 우선, 바운딩 핸들 다음)
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

	// 주 조작 대상 도형 변형 적용
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

void SelectTool::ApplyNodeEdit(PathShape* shape, const ToolEvent& e)
{
	if (!shape) return;

	int spIdx = m_editHit.subPath;
	int nIdx = m_editHit.node;
	if (spIdx < 0 || spIdx >= static_cast<int>(shape->paths.size())) return;
	auto& sp = shape->paths[spIdx];
	if (nIdx < 0 || nIdx >= static_cast<int>(sp.nodes.size())) return;

	auto& node = sp.nodes[nIdx];

	// 마우스 현재 좌표를 쉐이프 로컬 좌표로 변환
	D2D1::Matrix3x2F invXf = shape->xf;
	if (!invXf.Invert()) return;

	D2D1_POINT_2F curLocal = invXf.TransformPoint(e.worldPt);

	if (m_editHit.part == HitResult::Part::Anchor)
	{
		// 앵커 이동 (그리드 스냅 반영된 worldPt 기준)
		node.anchor = curLocal;
		shape->Invalidate();
	}
	else if (m_editHit.part == HitResult::Part::HandleIn)
	{
		// 핸들 In 오프셋 업데이트 (node.anchor 기준 상대 좌표)
		D2D1_POINT_2F offset = D2D1::Point2F(curLocal.x - node.anchor.x, curLocal.y - node.anchor.y);
		node.SetHandleIn(offset);
		shape->Invalidate();
	}
	else if (m_editHit.part == HitResult::Part::HandleOut)
	{
		// 핸들 Out 오프셋 업데이트 (node.anchor 기준 상대 좌표)
		D2D1_POINT_2F offset = D2D1::Point2F(curLocal.x - node.anchor.x, curLocal.y - node.anchor.y);
		node.SetHandleOut(offset);
		shape->Invalidate();
	}
}

void SelectTool::ApplyMove(Shape* shape)
{
	float dx = m_dragCurrentWorld.x - m_dragStartWorld.x;
	float dy = m_dragCurrentWorld.y - m_dragStartWorld.y;

	const auto& grid = GetApp()->GetGrid();
	if (grid.snapToGrid && shape)
	{
		PathShape* ps = dynamic_cast<PathShape*>(shape);
		if (ps && !ps->paths.empty() && !ps->paths[0].nodes.empty())
		{
			// 첫 번째 앵커의 원래 월드 좌표
			D2D1_POINT_2F p0 = m_initialXf.TransformPoint(ps->paths[0].nodes[0].anchor);
			// 이동 예상 위치를 그리드 교차점에 스냅
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

	// 초기 변환 행렬에 평행이동 합성
	shape->xf = m_initialXf * D2D1::Matrix3x2F::Translation(dx, dy);
}

void SelectTool::ApplyScale(Shape* shape)
{
	// 로컬 공간 상에서 드래그 변위 계산
	D2D1::Matrix3x2F invInitial = m_initialXf;
	if (!invInitial.Invert()) return;

	D2D1_POINT_2F startLocal = invInitial.TransformPoint(m_dragStartWorld);
	D2D1_POINT_2F curLocal   = invInitial.TransformPoint(m_dragCurrentWorld);

	float origW = m_initialLocalBounds.right - m_initialLocalBounds.left;
	float origH = m_initialLocalBounds.bottom - m_initialLocalBounds.top;
	if (origW <= 0.001f || origH <= 0.001f) return;

	// 반대편 고정점(Anchor) 및 스케일 비율 계산
	D2D1_POINT_2F anchorLocal = { 0.0f, 0.0f };
	float sx = 1.0f;
	float sy = 1.0f;

	switch (m_handle)
	{
	case Handle::BR:
		anchorLocal = D2D1::Point2F(m_initialLocalBounds.left, m_initialLocalBounds.top);
		sx = (curLocal.x - anchorLocal.x) / origW;
		sy = (curLocal.y - anchorLocal.y) / origH;
		break;
	case Handle::TL:
		anchorLocal = D2D1::Point2F(m_initialLocalBounds.right, m_initialLocalBounds.bottom);
		sx = (anchorLocal.x - curLocal.x) / origW;
		sy = (anchorLocal.y - curLocal.y) / origH;
		break;
	case Handle::TR:
		anchorLocal = D2D1::Point2F(m_initialLocalBounds.left, m_initialLocalBounds.bottom);
		sx = (curLocal.x - anchorLocal.x) / origW;
		sy = (anchorLocal.y - curLocal.y) / origH;
		break;
	case Handle::BL:
		anchorLocal = D2D1::Point2F(m_initialLocalBounds.right, m_initialLocalBounds.top);
		sx = (anchorLocal.x - curLocal.x) / origW;
		sy = (anchorLocal.y - curLocal.y) / origH;
		break;
	case Handle::R:
		anchorLocal = D2D1::Point2F(m_initialLocalBounds.left, (m_initialLocalBounds.top + m_initialLocalBounds.bottom) * 0.5f);
		sx = (curLocal.x - anchorLocal.x) / origW;
		sy = 1.0f;
		break;
	case Handle::L:
		anchorLocal = D2D1::Point2F(m_initialLocalBounds.right, (m_initialLocalBounds.top + m_initialLocalBounds.bottom) * 0.5f);
		sx = (anchorLocal.x - curLocal.x) / origW;
		sy = 1.0f;
		break;
	case Handle::B:
		anchorLocal = D2D1::Point2F((m_initialLocalBounds.left + m_initialLocalBounds.right) * 0.5f, m_initialLocalBounds.top);
		sx = 1.0f;
		sy = (curLocal.y - anchorLocal.y) / origH;
		break;
	case Handle::T:
		anchorLocal = D2D1::Point2F((m_initialLocalBounds.left + m_initialLocalBounds.right) * 0.5f, m_initialLocalBounds.bottom);
		sx = 1.0f;
		sy = (anchorLocal.y - curLocal.y) / origH;
		break;
	default:
		break;
	}

	if (std::abs(sx) < 0.01f) sx = 0.01f;
	if (std::abs(sy) < 0.01f) sy = 0.01f;

	shape->xf = ScaleAboutAnchor(anchorLocal, sx, sy) * m_initialXf;
}

void SelectTool::ApplyRotate(Shape* shape)
{
	// 중심점 구하기
	D2D1_POINT_2F centerLocal = D2D1::Point2F(
		(m_initialLocalBounds.left + m_initialLocalBounds.right) * 0.5f,
		(m_initialLocalBounds.top + m_initialLocalBounds.bottom) * 0.5f
	);
	D2D1_POINT_2F centerWorld = m_initialXf.TransformPoint(centerLocal);

	float angleStart = std::atan2(m_dragStartWorld.y - centerWorld.y, m_dragStartWorld.x - centerWorld.x);
	float angleCur   = std::atan2(m_dragCurrentWorld.y - centerWorld.y, m_dragCurrentWorld.x - centerWorld.x);
	float angleDeg   = (angleCur - angleStart) * (180.0f / 3.14159265f);

	shape->xf = m_initialXf * D2D1::Matrix3x2F::Rotation(angleDeg, centerWorld);
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

		// 열린 패스(곡선, 직선)인 경우 사각 변형 박스를 표시하지 않고 정점 에디트 포인트로만 변형
		if (!IsOpenPath(shape))
		{
			// 1. 바운딩 박스 외곽선 (스크린 좌표)
			D2D1_POINT_2F pTL = handles[0];
			D2D1_POINT_2F pTR = handles[2];
			D2D1_POINT_2F pBR = handles[4];
			D2D1_POINT_2F pBL = handles[6];

			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.85f));
			rt->DrawLine(pTL, pTR, brush, 1.2f);
			rt->DrawLine(pTR, pBR, brush, 1.2f);
			rt->DrawLine(pBR, pBL, brush, 1.2f);
			rt->DrawLine(pBL, pTL, brush, 1.2f);

			// 2. 상단 회전 핸들 연결선 및 점
			D2D1_POINT_2F pT = handles[1];
			D2D1_POINT_2F pRot = handles[8];
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
			rt->DrawLine(pT, pRot, brush, 1.0f);

			// 회전 핸들 (원)
			bool isRotActive = (m_handle == Handle::Rotate);
			brush->SetColor(isRotActive ? D2D1::ColorF(0.12f, 0.55f, 0.95f, 1.0f) : D2D1::ColorF(D2D1::ColorF::White));
			rt->FillEllipse(D2D1::Ellipse(pRot, 4.5f, 4.5f), brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawEllipse(D2D1::Ellipse(pRot, 4.5f, 4.5f), brush, 1.5f);

			// 3. 8개 크기 조절 핸들 (정사각형)
			const float hSize = 3.5f;
			for (int i = 0; i < 8; ++i)
			{
				Handle hType = static_cast<Handle>(i + 1);
				bool isSelectedHandle = (m_handle == hType);

				D2D1_RECT_F r = D2D1::RectF(
					handles[i].x - hSize, handles[i].y - hSize,
					handles[i].x + hSize, handles[i].y + hSize
				);

				// 사용자가 선택/조작 중인 핸들은 가시적인 파란색 채우기 + 진한 파란색 테두리로 강조
				if (isSelectedHandle)
				{
					brush->SetColor(D2D1::ColorF(0.10f, 0.50f, 0.95f, 1.0f));
					rt->FillRectangle(r, brush);
					brush->SetColor(D2D1::ColorF(0.05f, 0.25f, 0.70f, 1.0f));
					rt->DrawRectangle(r, brush, 1.6f);

					// 외곽 강조 포커스 링
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

		// 4. 패스 도형(직선, 곡선, 다각형 등)인 경우 개별 정점(Anchor) 및 베이지어 핸들(Handle) 에디트 포인트 항상 표시
		PathShape* pathShape = dynamic_cast<PathShape*>(shape);
		if (pathShape)
		{
			for (const auto& sp : pathShape->paths)
			{
				for (const auto& node : sp.nodes)
				{
					D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, pathShape->xf, view);

					// 베이지어 핸들 In 선 및 조절점
					if (node.HasIn())
					{
						D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), pathShape->xf, view);
						brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
						rt->DrawLine(aScreen, inScreen, brush, 1.0f);
						brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
						rt->FillEllipse(D2D1::Ellipse(inScreen, 3.0f, 3.0f), brush);
					}

					// 베이지어 핸들 Out 선 및 조절점
					if (node.HasOut())
					{
						D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), pathShape->xf, view);
						brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
						rt->DrawLine(aScreen, outScreen, brush, 1.0f);
						brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
						rt->FillEllipse(D2D1::Ellipse(outScreen, 3.0f, 3.0f), brush);
					}

					// 개별 정점 앵커 에디트 포인트 (정사각형)
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

// ---------------- PanTool ----------------
void PanTool::OnDown(const ToolEvent& e)
{
}

void PanTool::OnMove(const ToolEvent& e)
{
}

void PanTool::OnUp(const ToolEvent& e)
{
}

// ---------------- PenTool ----------------
void PenTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	D2D1_POINT_2F screenD2D = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
	e.pDoc->GetPenState().OnLButtonDown(
		e.pDoc->GetCurrentPath(),
		GetApp()->GetGrid(),
		e.pDoc->GetUndo(),
		screenD2D,
		e.view
	);

	CString str;
	str.Format(_T("[펜 도구] 앵커 노드 추가: (%.1f, %.1f) px"), e.worldPt.x, e.worldPt.y);
	LogAppEvent(str);
}

void PenTool::OnMove(const ToolEvent& e)
{
	m_hoverWorld = e.worldPt;
	m_hasHover = true;

	if (!e.pDoc) return;

	D2D1_POINT_2F screenD2D = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
	e.pDoc->GetPenState().OnMouseMove(
		e.pDoc->GetCurrentPath(),
		GetApp()->GetGrid(),
		screenD2D,
		e.view
	);
}

void PenTool::OnUp(const ToolEvent& e)
{
	if (!e.pDoc) return;

	e.pDoc->GetPenState().OnLButtonUp(e.pDoc->GetUndo());
}

void PenTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (!pDoc) return;

	if (nChar == VK_RETURN)
	{
		// Enter 키: 현재 열린 패스 완료 후 문서 도형 목록에 보관
		if (pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->paths.empty())
		{
			pDoc->GetPenState().FinishCurrentPath();
			pDoc->GetShapes().push_back(std::move(pDoc->GetCurrentPath()));
			pDoc->GetCurrentPath() = nullptr;
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect)
			{
				pSelect->SetSelectedIndex(static_cast<int>(pDoc->GetShapes().size()) - 1, pDoc);
			}
			LogAppEvent(_T("[펜 도구] 패스 작성 완료 (Enter)"));
		}
	}
	else if (nChar == VK_ESCAPE)
	{
		// Esc 키: 취소 및 롤백
		pDoc->GetPenState().CancelOperation(pDoc->GetCurrentPath(), pDoc->GetUndo());
		LogAppEvent(_T("[펜 도구] 패스 작성 취소 (Escape)"));
	}
}

void PenTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	// 1. 패스가 이미 시작된 상태인 경우
	if (pDoc && pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->paths.empty())
	{
		const auto& path = pDoc->GetCurrentPath();

		// 기존 패스 본체 실시간 렌더링
		path->Draw(rt, brush, nullptr, view);

		// 모든 앵커 및 핸들 제어선 시각화
		D2D1_POINT_2F lastAnchorLocal = { 0.0f, 0.0f };
		D2D1_POINT_2F lastOutLocal = { 0.0f, 0.0f };
		bool hasLastOut = false;
		bool hasNodes = false;

		for (const auto& sp : path->paths)
		{
			for (const auto& node : sp.nodes)
			{
				D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, path->xf, view);

				// 핸들 In 선 및 조절점
				if (node.HasIn())
				{
					D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), path->xf, view);
					brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
					rt->DrawLine(aScreen, inScreen, brush, 1.0f);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
					rt->FillEllipse(D2D1::Ellipse(inScreen, 3.0f, 3.0f), brush);
				}

				// 핸들 Out 선 및 조절점
				if (node.HasOut())
				{
					D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), path->xf, view);
					brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
					rt->DrawLine(aScreen, outScreen, brush, 1.0f);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
					rt->FillEllipse(D2D1::Ellipse(outScreen, 3.0f, 3.0f), brush);
				}

				// 앵커 점 (사각형)
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->FillRectangle(D2D1::RectF(aScreen.x - 3.5f, aScreen.y - 3.5f, aScreen.x + 3.5f, aScreen.y + 3.5f), brush);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::DarkBlue));
				rt->DrawRectangle(D2D1::RectF(aScreen.x - 3.5f, aScreen.y - 3.5f, aScreen.x + 3.5f, aScreen.y + 3.5f), brush, 1.0f);

				lastAnchorLocal = node.anchor;
				hasLastOut = node.HasOut();
				if (hasLastOut) lastOutLocal = node.AbsOut();
				hasNodes = true;
			}
		}

		// 마지막 노드에서 다음 마우스(호버/스냅) 위치까지 베이지어/직선 실시간 연결선 및 포인트 미리보기
		if (m_hasHover && hasNodes)
		{
			D2D1_POINT_2F pLastScreen = CoordinateSpace::LocalToScreen(lastAnchorLocal, path->xf, view);
			D2D1_POINT_2F pNextScreen = view.WorldToScreen(m_hoverWorld);

			brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
			if (hasLastOut)
			{
				D2D1_POINT_2F pOutScreen = CoordinateSpace::LocalToScreen(lastOutLocal, path->xf, view);
				// 마지막 핸들 Out이 있을 경우 3차 베지에 곡선으로 다음 지점 미리보기
				ID2D1Factory* pFactory = nullptr;
				rt->GetFactory(&pFactory);
				if (pFactory)
				{
					ComPtr<ID2D1PathGeometry> tempGeo;
					if (SUCCEEDED(pFactory->CreatePathGeometry(&tempGeo)))
					{
						ComPtr<ID2D1GeometrySink> sink;
						if (SUCCEEDED(tempGeo->Open(&sink)))
						{
							sink->BeginFigure(pLastScreen, D2D1_FIGURE_BEGIN_HOLLOW);
							sink->AddBezier(D2D1::BezierSegment(pOutScreen, pNextScreen, pNextScreen));
							sink->EndFigure(D2D1_FIGURE_END_OPEN);
							sink->Close();
							rt->DrawGeometry(tempGeo.Get(), brush, 1.5f);
						}
					}
					pFactory->Release();
				}
			}
			else
			{
				rt->DrawLine(pLastScreen, pNextScreen, brush, 1.5f);
			}

			// 다음 작업 포인트 실시간 에디트 마커 표시 (동심 링)
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillEllipse(D2D1::Ellipse(pNextScreen, 3.5f, 3.5f), brush);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
			rt->DrawEllipse(D2D1::Ellipse(pNextScreen, 3.5f, 3.5f), brush, 1.5f);
			brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.35f));
			rt->DrawEllipse(D2D1::Ellipse(pNextScreen, 7.5f, 7.5f), brush, 1.0f);
		}
	}
	else if (m_hasHover)
	{
		// 첫 노드 생성 전: 마우스(그리드 스냅 반영) 첫 에디터 포인터 십자선 & 앵커 미리보기
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverWorld);
		const float cross = 9.0f;
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
		rt->DrawLine(D2D1::Point2F(sp.x - cross, sp.y), D2D1::Point2F(sp.x + cross, sp.y), brush, 1.0f);
		rt->DrawLine(D2D1::Point2F(sp.x, sp.y - cross), D2D1::Point2F(sp.x, sp.y + cross), brush, 1.0f);

		D2D1_RECT_F hr = D2D1::RectF(sp.x - 3.5f, sp.y - 3.5f, sp.x + 3.5f, sp.y + 3.5f);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr, brush);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
		rt->DrawRectangle(hr, brush, 1.5f);
	}
}

// ---------------- LineTool (직선: 클릭-클릭 또는 드래그 모두 지원) ----------------
void LineTool::OnDown(const ToolEvent& e)
{
	m_isDragging = true;
	if (!m_hasFirstPoint)
	{
		m_hasFirstPoint = true;
		m_startPt = e.worldPt;
		m_currentPt = e.worldPt;
	}
	else
	{
		m_currentPt = e.worldPt;
	}
}

void LineTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_hasFirstPoint)
	{
		m_currentPt = e.worldPt;
	}
}

void LineTool::OnUp(const ToolEvent& e)
{
	m_isDragging = false;
	if (!m_hasFirstPoint || !e.pDoc) return;

	float dx = e.worldPt.x - m_startPt.x;
	float dy = e.worldPt.y - m_startPt.y;
	float distSq = dx * dx + dy * dy;

	// 첫 번째 다운 후 상당 거리(4px 이상) 드래그 후 업했거나, 이미 첫 점 확정 후 두 번째 클릭인 경우 완성
	if (distSq > 16.0f)
	{
		m_hasFirstPoint = false;
		m_currentPt = e.worldPt;

		auto lineShape = std::make_unique<PathShape>();
		lineShape->name.Format(_T("직선 #%d"), (int)e.pDoc->GetShapes().size() + 1);
		lineShape->AddAnchor(m_startPt);
		lineShape->AddAnchor(m_currentPt);
		e.pDoc->GetShapes().push_back(std::move(lineShape));
		SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
		if (pSelect)
		{
			pSelect->SetSelectedIndex(static_cast<int>(e.pDoc->GetShapes().size()) - 1, e.pDoc);
		}
		GetApp()->GetToolManager().SetActiveTool(ToolType::Select);

		CString str;
		str.Format(_T("[직선 생성] (%.1f, %.1f) -> (%.1f, %.1f)"), m_startPt.x, m_startPt.y, m_currentPt.x, m_currentPt.y);
		LogAppEvent(str);
	}
	// distSq <= 16.0f 이면 첫 점 클릭(다운-업)으로 판정하고 두 번째 클릭을 대기
}

void LineTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (nChar == VK_ESCAPE)
	{
		m_hasFirstPoint = false;
		m_isDragging = false;
		LogAppEvent(_T("[직선 도구] 작성 취소 (Escape)"));
	}
}

void LineTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	if (m_hasFirstPoint)
	{
		D2D1_POINT_2F p0 = view.WorldToScreen(m_startPt);
		D2D1_POINT_2F p1 = view.WorldToScreen(m_currentPt);

		// 1. 실시간 선분 미리보기
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
		rt->DrawLine(p0, p1, brush, 1.5f);

		// 2. 첫 번째 앵커 포인트 표시 (고정됨)
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(D2D1::RectF(p0.x - 3.5f, p0.y - 3.5f, p0.x + 3.5f, p0.y + 3.5f), brush);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
		rt->DrawRectangle(D2D1::RectF(p0.x - 3.5f, p0.y - 3.5f, p0.x + 3.5f, p0.y + 3.5f), brush, 1.5f);

		// 3. 두 번째 작업 포인트 실시간 에디트 마커 미리보기
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillEllipse(D2D1::Ellipse(p1, 3.5f, 3.5f), brush);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
		rt->DrawEllipse(D2D1::Ellipse(p1, 3.5f, 3.5f), brush, 1.5f);
		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.35f));
		rt->DrawEllipse(D2D1::Ellipse(p1, 7.5f, 7.5f), brush, 1.0f);
	}
	else if (m_hasHover)
	{
		// 첫 에디터 포인트(그리드 스냅 마우스 위치) 미리보기
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
		const float cross = 9.0f;

		// 십자선 가이드
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
		rt->DrawLine(D2D1::Point2F(sp.x - cross, sp.y), D2D1::Point2F(sp.x + cross, sp.y), brush, 1.0f);
		rt->DrawLine(D2D1::Point2F(sp.x, sp.y - cross), D2D1::Point2F(sp.x, sp.y + cross), brush, 1.0f);

		// 스냅 앵커 포인트 사각형 핸들
		D2D1_RECT_F hr = D2D1::RectF(sp.x - 3.5f, sp.y - 3.5f, sp.x + 3.5f, sp.y + 3.5f);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr, brush);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
		rt->DrawRectangle(hr, brush, 1.5f);
	}
}

// ---------------- RectTool (사각형: 클릭-클릭 또는 드래그 모두 지원) ----------------
void RectTool::OnDown(const ToolEvent& e)
{
	m_isDragging = true;
	if (!m_hasFirstPoint)
	{
		m_hasFirstPoint = true;
		m_startPt = e.worldPt;
		m_currentPt = e.worldPt;
	}
	else
	{
		m_currentPt = e.worldPt;
	}
}

void RectTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_hasFirstPoint)
	{
		m_currentPt = e.worldPt;
	}
}

void RectTool::OnUp(const ToolEvent& e)
{
	m_isDragging = false;
	if (!m_hasFirstPoint || !e.pDoc) return;

	float left = min(m_startPt.x, e.worldPt.x);
	float top = min(m_startPt.y, e.worldPt.y);
	float right = max(m_startPt.x, e.worldPt.x);
	float bottom = max(m_startPt.y, e.worldPt.y);

	// 드래그로 넓은 사각형이 완성되었거나 두 번째 클릭인 경우
	if (right - left > 4.0f && bottom - top > 4.0f)
	{
		m_hasFirstPoint = false;
		m_currentPt = e.worldPt;

		auto rectShape = std::make_unique<PathShape>();
		rectShape->name.Format(_T("사각형 #%d"), (int)e.pDoc->GetShapes().size() + 1);
		rectShape->AddAnchor(D2D1::Point2F(left, top));
		rectShape->AddAnchor(D2D1::Point2F(right, top));
		rectShape->AddAnchor(D2D1::Point2F(right, bottom));
		rectShape->AddAnchor(D2D1::Point2F(left, bottom));
		rectShape->paths.back().closed = true;

		e.pDoc->GetShapes().push_back(std::move(rectShape));
		SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
		if (pSelect)
		{
			pSelect->SetSelectedIndex(static_cast<int>(e.pDoc->GetShapes().size()) - 1, e.pDoc);
		}
		GetApp()->GetToolManager().SetActiveTool(ToolType::Select);

		CString str;
		str.Format(_T("[사각형 생성] 폭=%.1f px, 높이=%.1f px"), right - left, bottom - top);
		LogAppEvent(str);
	}
	// 폭이나 높이가 4px 이하이면 첫 코너 클릭(다운-업)으로 판정하고 대각선 두 번째 코너 클릭 대기
}

void RectTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (nChar == VK_ESCAPE)
	{
		m_hasFirstPoint = false;
		m_isDragging = false;
		LogAppEvent(_T("[사각형 도구] 작성 취소 (Escape)"));
	}
}

void RectTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	if (m_hasFirstPoint)
	{
		D2D1_POINT_2F p0 = view.WorldToScreen(m_startPt);
		D2D1_POINT_2F p1 = view.WorldToScreen(m_currentPt);

		float left = min(p0.x, p1.x);
		float top = min(p0.y, p1.y);
		float right = max(p0.x, p1.x);
		float bottom = max(p0.y, p1.y);
		float midX = (left + right) * 0.5f;
		float midY = (top + bottom) * 0.5f;

		D2D1_RECT_F r = D2D1::RectF(left, top, right, bottom);

		// 도형 반투명 채움 + 테두리 미리보기
		brush->SetColor(D2D1::ColorF(0.2f, 0.5f, 0.9f, 0.15f));
		rt->FillRectangle(r, brush);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
		rt->DrawRectangle(r, brush, 1.5f);

		// 8개 에디트 포인트 핸들 실시간 미리보기
		D2D1_POINT_2F pts[8] = {
			D2D1::Point2F(left, top),    D2D1::Point2F(midX, top),    D2D1::Point2F(right, top),
			D2D1::Point2F(right, midY),                               D2D1::Point2F(right, bottom),
			D2D1::Point2F(midX, bottom), D2D1::Point2F(left, bottom), D2D1::Point2F(left, midY)
		};

		const float hSize = 3.5f;
		for (int i = 0; i < 8; ++i)
		{
			D2D1_RECT_F hr = D2D1::RectF(pts[i].x - hSize, pts[i].y - hSize, pts[i].x + hSize, pts[i].y + hSize);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillRectangle(hr, brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawRectangle(hr, brush, 1.2f);
		}

		// 두 번째 대각선 작업 포인트 강조
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
		rt->DrawEllipse(D2D1::Ellipse(p1, 6.0f, 6.0f), brush, 1.5f);
	}
	else if (m_hasHover)
	{
		// 첫 에디터 포인트(그리드 스냅 마우스 위치) 미리보기
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
		const float cross = 10.0f;

		// 십자 정렬 가이드선
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
		rt->DrawLine(D2D1::Point2F(sp.x - cross, sp.y), D2D1::Point2F(sp.x + cross, sp.y), brush, 1.0f);
		rt->DrawLine(D2D1::Point2F(sp.x, sp.y - cross), D2D1::Point2F(sp.x, sp.y + cross), brush, 1.0f);

		// 사각형 시작 코너 앵커 포인트 표시
		const float hSize = 4.0f;
		D2D1_RECT_F hr = D2D1::RectF(sp.x - hSize, sp.y - hSize, sp.x + hSize, sp.y + hSize);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr, brush);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
		rt->DrawRectangle(hr, brush, 1.5f);

		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.35f));
		rt->DrawEllipse(D2D1::Ellipse(sp, 7.5f, 7.5f), brush, 1.0f);
	}
}

// ---------------- EllipseTool (타원: 클릭-클릭 또는 드래그 모두 지원) ----------------
void EllipseTool::OnDown(const ToolEvent& e)
{
	m_isDragging = true;
	if (!m_hasFirstPoint)
	{
		m_hasFirstPoint = true;
		m_startPt = e.worldPt;
		m_currentPt = e.worldPt;
	}
	else
	{
		m_currentPt = e.worldPt;
	}
}

void EllipseTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_hasFirstPoint)
	{
		m_currentPt = e.worldPt;
	}
}

void EllipseTool::OnUp(const ToolEvent& e)
{
	m_isDragging = false;
	if (!m_hasFirstPoint || !e.pDoc) return;

	float left = min(m_startPt.x, e.worldPt.x);
	float top = min(m_startPt.y, e.worldPt.y);
	float right = max(m_startPt.x, e.worldPt.x);
	float bottom = max(m_startPt.y, e.worldPt.y);

	float rx = (right - left) * 0.5f;
	float ry = (bottom - top) * 0.5f;
	float cx = left + rx;
	float cy = top + ry;

	if (rx > 3.0f && ry > 3.0f)
	{
		m_hasFirstPoint = false;
		m_currentPt = e.worldPt;

		// 원 근사 상수 kappa = 0.55228475
		const float kappa = 0.55228475f;
		float kx = rx * kappa;
		float ky = ry * kappa;

		auto ellipseShape = std::make_unique<PathShape>();
		SubPath sp;

		// 노드 0 (Top: cx, cy - ry)
		BezierNode n0;
		n0.anchor = D2D1::Point2F(cx, cy - ry);
		n0.hIn = D2D1::Point2F(-kx, 0.0f);
		n0.hOut = D2D1::Point2F(kx, 0.0f);
		n0.type = BezierNode::Type::Smooth;
		sp.nodes.push_back(n0);

		// 노드 1 (Right: cx + rx, cy)
		BezierNode n1;
		n1.anchor = D2D1::Point2F(cx + rx, cy);
		n1.hIn = D2D1::Point2F(0.0f, -ky);
		n1.hOut = D2D1::Point2F(0.0f, ky);
		n1.type = BezierNode::Type::Smooth;
		sp.nodes.push_back(n1);

		// 노드 2 (Bottom: cx, cy + ry)
		BezierNode n2;
		n2.anchor = D2D1::Point2F(cx, cy + ry);
		n2.hIn = D2D1::Point2F(kx, 0.0f);
		n2.hOut = D2D1::Point2F(-kx, 0.0f);
		n2.type = BezierNode::Type::Smooth;
		sp.nodes.push_back(n2);

		// 노드 3 (Left: cx - rx, cy)
		BezierNode n3;
		n3.anchor = D2D1::Point2F(cx - rx, cy);
		n3.hIn = D2D1::Point2F(0.0f, ky);
		n3.hOut = D2D1::Point2F(0.0f, -ky);
		n3.type = BezierNode::Type::Smooth;
		sp.nodes.push_back(n3);

		sp.closed = true;
		ellipseShape->name.Format(_T("타원 #%d"), (int)e.pDoc->GetShapes().size() + 1);
		ellipseShape->paths.push_back(sp);

		e.pDoc->GetShapes().push_back(std::move(ellipseShape));
		SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
		if (pSelect)
		{
			pSelect->SetSelectedIndex(static_cast<int>(e.pDoc->GetShapes().size()) - 1, e.pDoc);
		}
		GetApp()->GetToolManager().SetActiveTool(ToolType::Select);

		CString str;
		str.Format(_T("[타원 생성] 반경 X=%.1f px, Y=%.1f px (kappa 4노드)"), rx, ry);
		LogAppEvent(str);
	}
}

void EllipseTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (nChar == VK_ESCAPE)
	{
		m_hasFirstPoint = false;
		m_isDragging = false;
		LogAppEvent(_T("[타원 도구] 작성 취소 (Escape)"));
	}
}

void EllipseTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	if (m_hasFirstPoint)
	{
		D2D1_POINT_2F p0 = view.WorldToScreen(m_startPt);
		D2D1_POINT_2F p1 = view.WorldToScreen(m_currentPt);

		float left = min(p0.x, p1.x);
		float top = min(p0.y, p1.y);
		float right = max(p0.x, p1.x);
		float bottom = max(p0.y, p1.y);
		float midX = (left + right) * 0.5f;
		float midY = (top + bottom) * 0.5f;
		float rx = (right - left) * 0.5f;
		float ry = (bottom - top) * 0.5f;

		D2D1_ELLIPSE ell = D2D1::Ellipse(D2D1::Point2F(midX, midY), rx, ry);
		brush->SetColor(D2D1::ColorF(0.2f, 0.5f, 0.9f, 0.15f));
		rt->FillEllipse(ell, brush);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
		rt->DrawEllipse(ell, brush, 1.5f);

		// 외곽 바운딩 박스 가이드선
		D2D1_RECT_F r = D2D1::RectF(left, top, right, bottom);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.4f));
		rt->DrawRectangle(r, brush, 1.0f);

		// 8개 에디트 포인트 핸들 실시간 미리보기
		D2D1_POINT_2F pts[8] = {
			D2D1::Point2F(left, top),    D2D1::Point2F(midX, top),    D2D1::Point2F(right, top),
			D2D1::Point2F(right, midY),                               D2D1::Point2F(right, bottom),
			D2D1::Point2F(midX, bottom), D2D1::Point2F(left, bottom), D2D1::Point2F(left, midY)
		};

		const float hSize = 3.5f;
		for (int i = 0; i < 8; ++i)
		{
			D2D1_RECT_F hr = D2D1::RectF(pts[i].x - hSize, pts[i].y - hSize, pts[i].x + hSize, pts[i].y + hSize);
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
			rt->FillRectangle(hr, brush);
			brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
			rt->DrawRectangle(hr, brush, 1.2f);
		}

		// 두 번째 대각선 작업 포인트 강조
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
		rt->DrawEllipse(D2D1::Ellipse(p1, 6.0f, 6.0f), brush, 1.5f);
	}
	else if (m_hasHover)
	{
		// 첫 에디터 포인트 미리보기
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverPt);
		const float cross = 10.0f;

		// 십자 정렬 가이드선
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
		rt->DrawLine(D2D1::Point2F(sp.x - cross, sp.y), D2D1::Point2F(sp.x + cross, sp.y), brush, 1.0f);
		rt->DrawLine(D2D1::Point2F(sp.x, sp.y - cross), D2D1::Point2F(sp.x, sp.y + cross), brush, 1.0f);

		// 원형 시작 포인트
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillEllipse(D2D1::Ellipse(sp, 4.0f, 4.0f), brush);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
		rt->DrawEllipse(D2D1::Ellipse(sp, 4.0f, 4.0f), brush, 1.5f);
		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.35f));
		rt->DrawEllipse(D2D1::Ellipse(sp, 8.0f, 8.0f), brush, 1.0f);
	}
}

// ---------------- PolygonTool (다각형 도구: 클릭할 때마다 정점 추가, 마우스 호버에 따라 도형 및 다음 선분 실시간 미리보기) ----------------
void PolygonTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	if (!e.pDoc->GetCurrentPath())
	{
		e.pDoc->GetCurrentPath() = std::make_unique<PathShape>();
	}

	// 첫 노드 또는 노드가 3개 이상일 때 첫 노드(8px 반경) 근처를 다시 클릭하면 폐합 완료
	auto& path = e.pDoc->GetCurrentPath();
	if (!path->paths.empty() && !path->paths.back().nodes.empty())
	{
		const auto& firstNode = path->paths.back().nodes[0];
		D2D1_POINT_2F firstScreen = CoordinateSpace::LocalToScreen(firstNode.anchor, path->xf, e.view);
		float dx = firstScreen.x - static_cast<float>(e.screenPt.x);
		float dy = firstScreen.y - static_cast<float>(e.screenPt.y);

		if (path->paths.back().nodes.size() >= 3 && (dx * dx + dy * dy) <= 64.0f)
		{
			// 첫 점을 다시 클릭하여 다각형 폐합 완료!
			path->paths.back().closed = true;
			path->Invalidate();
			e.pDoc->GetShapes().push_back(std::move(path));
			e.pDoc->GetCurrentPath() = nullptr;
			SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
			if (pSelect)
			{
				pSelect->SetSelectedIndex(static_cast<int>(e.pDoc->GetShapes().size()) - 1, e.pDoc);
			}
			LogAppEvent(_T("[다각형 폐합 완료] 첫 정점 스냅 클릭"));
			return;
		}
	}

	e.pDoc->GetCurrentPath()->AddAnchor(e.worldPt);

	CString str;
	str.Format(_T("[다각형 정점 추가] #%d: (%.1f, %.1f) px"),
		(int)e.pDoc->GetCurrentPath()->paths.back().nodes.size(), e.worldPt.x, e.worldPt.y);
	LogAppEvent(str);
}

void PolygonTool::OnMove(const ToolEvent& e)
{
	m_hoverWorld = e.worldPt;
	m_hasHover = true;
}

void PolygonTool::OnUp(const ToolEvent& e)
{
}

void PolygonTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (!pDoc || !pDoc->GetCurrentPath()) return;

	if (nChar == VK_RETURN)
	{
		// Enter 키로 다각형 폐합 후 저장
		if (!pDoc->GetCurrentPath()->paths.empty())
		{
			auto& sp = pDoc->GetCurrentPath()->paths.back();
			if (sp.nodes.size() >= 2)
			{
				sp.closed = true;
				pDoc->GetCurrentPath()->Invalidate();
				pDoc->GetShapes().push_back(std::move(pDoc->GetCurrentPath()));
				pDoc->GetCurrentPath() = nullptr;
				SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
				if (pSelect)
				{
					pSelect->SetSelectedIndex(static_cast<int>(pDoc->GetShapes().size()) - 1, pDoc);
				}
				LogAppEvent(_T("[다각형 생성 완료] (Enter)"));
			}
			else
			{
				pDoc->GetCurrentPath() = nullptr;
			}
		}
	}
	else if (nChar == VK_ESCAPE)
	{
		pDoc->GetCurrentPath() = nullptr;
		LogAppEvent(_T("[다각형 작성 취소] (Escape)"));
	}
}

void PolygonTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, pan };

	if (pDoc && pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->paths.empty())
	{
		const auto& path = pDoc->GetCurrentPath();
		const auto& sp = path->paths.back();
		const auto& nodes = sp.nodes;

		if (!nodes.empty())
		{
			// 1. 기존 확정된 정점들 사이의 선분 직접 그리기
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
			for (size_t i = 1; i < nodes.size(); ++i)
			{
				D2D1_POINT_2F pA = CoordinateSpace::LocalToScreen(nodes[i - 1].anchor, path->xf, view);
				D2D1_POINT_2F pB = CoordinateSpace::LocalToScreen(nodes[i].anchor, path->xf, view);
				rt->DrawLine(pA, pB, brush, 1.5f);
			}

			// 2. 마지막 정점에서 마우스 호버(스냅된 다음 정점)까지의 실시간 연결선 및 가상 폐합선 미리보기
			if (m_hasHover)
			{
				D2D1_POINT_2F pLast = CoordinateSpace::LocalToScreen(nodes.back().anchor, path->xf, view);
				D2D1_POINT_2F pNext = view.WorldToScreen(m_hoverWorld);
				D2D1_POINT_2F pFirst = CoordinateSpace::LocalToScreen(nodes.front().anchor, path->xf, view);

				// 다음 선분 실시간 가이드선
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
				rt->DrawLine(pLast, pNext, brush, 1.5f);

				// 2개 이상의 점이 있다면, 다음 정점과 첫 정점을 잇는 은은한 폐합 가이드선(점선 효과/반투명) 표시
				if (nodes.size() >= 2)
				{
					brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.45f));
					rt->DrawLine(pNext, pFirst, brush, 1.0f);
				}

				// 다음 작업 포인트 실시간 에디트 마커
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->FillEllipse(D2D1::Ellipse(pNext, 3.5f, 3.5f), brush);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
				rt->DrawEllipse(D2D1::Ellipse(pNext, 3.5f, 3.5f), brush, 1.5f);
				brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.35f));
				rt->DrawEllipse(D2D1::Ellipse(pNext, 7.5f, 7.5f), brush, 1.0f);
			}

			// 3. 모든 확정 정점 앵커 사각형 그리기
			for (size_t i = 0; i < nodes.size(); ++i)
			{
				D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(nodes[i].anchor, path->xf, view);
				bool isFirst = (i == 0);

				// 첫 정점은 폐합 타겟이므로 살짝 더 크고 눈에 띄게 강조
				float sz = isFirst ? 4.5f : 3.5f;
				D2D1_RECT_F hr = D2D1::RectF(aScreen.x - sz, aScreen.y - sz, aScreen.x + sz, aScreen.y + sz);
				brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
				rt->FillRectangle(hr, brush);
				brush->SetColor(isFirst ? D2D1::ColorF(D2D1::ColorF::OrangeRed) : D2D1::ColorF(D2D1::ColorF::DarkBlue));
				rt->DrawRectangle(hr, brush, isFirst ? 1.8f : 1.2f);
			}
		}
	}
	else if (m_hasHover)
	{
		// 첫 정점 찍기 전: 마우스(그리드 스냅 반영) 첫 에디터 포인트 미리보기
		D2D1_POINT_2F sp = view.WorldToScreen(m_hoverWorld);
		const float cross = 9.0f;
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 0.6f));
		rt->DrawLine(D2D1::Point2F(sp.x - cross, sp.y), D2D1::Point2F(sp.x + cross, sp.y), brush, 1.0f);
		rt->DrawLine(D2D1::Point2F(sp.x, sp.y - cross), D2D1::Point2F(sp.x, sp.y + cross), brush, 1.0f);

		D2D1_RECT_F hr = D2D1::RectF(sp.x - 3.5f, sp.y - 3.5f, sp.x + 3.5f, sp.y + 3.5f);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr, brush);
		brush->SetColor(D2D1::ColorF(0.12f, 0.45f, 0.95f, 1.0f));
		rt->DrawRectangle(hr, brush, 1.5f);
	}
}

// ---------------- ToolManager ----------------
ToolManager::ToolManager()
	: m_activeType(ToolType::Select)
	, m_prevType(ToolType::Select)
	, m_isTempPan(false)
{
	m_tools.push_back(std::make_unique<SelectTool>());
	m_tools.push_back(std::make_unique<PanTool>());
	m_tools.push_back(std::make_unique<PenTool>());
	m_tools.push_back(std::make_unique<LineTool>());
	m_tools.push_back(std::make_unique<RectTool>());
	m_tools.push_back(std::make_unique<EllipseTool>());
	m_tools.push_back(std::make_unique<PolygonTool>());
	m_tools.push_back(std::make_unique<TextTool>());
	m_tools.push_back(std::make_unique<PaintBucketTool>());
	m_tools.push_back(std::make_unique<ZoomInTool>());
	m_tools.push_back(std::make_unique<ZoomOutTool>());
}

void ToolManager::SetActiveTool(ToolType type)
{
	if (m_activeType == type)
		return;

	m_activeType = type;

	for (auto* listener : m_listeners)
	{
		if (listener)
			listener->OnToolChanged(m_activeType);
	}

	ITool* pTool = GetActiveTool();
	if (pTool)
	{
		CString str;
		str.Format(_T("[활성 도구 전환] %s (단축키: %c)"), pTool->GetName(), pTool->GetShortcut());
		LogAppEvent(str);
	}
}

ITool* ToolManager::GetActiveTool()
{
	for (auto& tool : m_tools)
	{
		if (tool->GetType() == m_activeType)
			return tool.get();
	}
	return m_tools.empty() ? nullptr : m_tools[0].get();
}

SelectTool* ToolManager::GetSelectTool()
{
	for (auto& tool : m_tools)
	{
		if (tool->GetType() == ToolType::Select)
			return dynamic_cast<SelectTool*>(tool.get());
	}
	return nullptr;
}

void ToolManager::BeginTempPan()
{
	if (!m_isTempPan)
	{
		m_prevType = m_activeType;
		m_isTempPan = true;
		SetActiveTool(ToolType::Pan);
	}
}

void ToolManager::EndTempPan()
{
	if (m_isTempPan)
	{
		m_isTempPan = false;
		SetActiveTool(m_prevType);
	}
}

void ToolManager::AddListener(IToolChangeListener* pListener)
{
	if (pListener)
		m_listeners.push_back(pListener);
}

void ToolManager::RemoveListener(IToolChangeListener* pListener)
{
	m_listeners.erase(
		std::remove(m_listeners.begin(), m_listeners.end(), pListener),
		m_listeners.end()
	);
}

bool ToolManager::HandleShortcut(wchar_t key)
{
	key = towupper(key);
	for (auto& tool : m_tools)
	{
		if (towupper(tool->GetShortcut()) == key)
		{
			SetActiveTool(tool->GetType());
			return true;
		}
	}
	return false;
}
