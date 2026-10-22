#include "pch.h"
#include "ITool.h"
#include "M2DrawDoc.h"
#include "M2DrawView.h"
#include "M2Draw.h"
#include "MainFrm.h"

// ============================================================================
// SelectTool 구현 (도형 선택, 이동, 스케일, 회전, 에디트 포인트 편집)
// ============================================================================

static void SyncShapeToGeDoc(CM2DrawDoc* pDoc, int index)
{
	if (!pDoc || index < 0 || index >= static_cast<int>(pDoc->GetShapes().size())) return;
	auto* shape = pDoc->GetShapes()[index].get();
	auto* ps = dynamic_cast<PathShape*>(shape);
	if (!ps) return;

	// ge::Document 에서 활성 레이어 내 대응 노드 찾기
	auto* layer = pDoc->GetDoc().GetActiveLayer();
	if (!layer) return;
	const auto& children = layer->GetChildren();
	if (index < static_cast<int>(children.size())) {
		auto* node = dynamic_cast<ge::PathNode*>(children[index].get());
		if (node) {
			const auto& mx = ps->xf;
			node->SetTransform({ mx._11, mx._12, mx._21, mx._22, mx._31, mx._32 });
			std::vector<ge::SubPath> gePaths;
			for (const auto& sp : ps->paths) {
				ge::SubPath gsp;
				gsp.closed = sp.closed;
				for (const auto& bn : sp.nodes) {
					ge::BezierNode gbn;
					gbn.anchor = { bn.anchor.x, bn.anchor.y };
					gbn.hIn = { bn.hIn.x, bn.hIn.y };
					gbn.hOut = { bn.hOut.x, bn.hOut.y };
					gbn.type = static_cast<ge::BezierNode::Type>(bn.type);
					gsp.nodes.push_back(gbn);
				}
				gePaths.push_back(std::move(gsp));
			}
			node->SetPaths(gePaths);
			ge::Style st;
			st.fillType = static_cast<ge::FillType>(ps->style.fillType);
			st.fill = ge::Color::FromD2D(ps->style.fill);
			st.stroke = ge::Color::FromD2D(ps->style.stroke);
			st.strokeWidth = ps->style.strokeWidth;
			st.dashStyle = static_cast<ge::DashStyle>(ps->style.dashStyle);
			node->SetStyle(st);
		}
	}
}

static void SyncDocSelection(CM2DrawDoc* pDoc, const std::set<int>& indices)
{
	if (!pDoc) return;
	auto* layer = pDoc->GetDoc().GetActiveLayer();
	if (!layer) return;
	const auto& children = layer->GetChildren();
	std::vector<ge::Node*> selNodes;
	for (int idx : indices) {
		if (idx >= 0 && idx < static_cast<int>(children.size())) {
			selNodes.push_back(children[idx].get());
		}
	}
	pDoc->GetDoc().SetSelection(selNodes);
}

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
					str.Format(_T("[선택 도구] %s 조절점 드래그 시작 (도형 #%d, 노드 #%d)"), (LPCTSTR)partName, idx + 1, hr.nodeIdx + 1);
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

		// 다중 선택된 모든 도형의 시작 Transform 캐싱
		m_initialXfs.clear();
		for (int idx : m_selectedIndices)
		{
			if (idx >= 0 && idx < static_cast<int>(shapes.size()))
			{
				m_initialXfs[idx] = shapes[idx]->xf;
			}
		}

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
		m_mode = Mode::Marquee; // 빈 공간 드래그 시 마키(고무줄) 선택 모드 진입
		m_handle = Handle::None;
		LogAppEvent(_T("[선택 도구] 마키(Marquee) 영역 선택 시작"));
	}
}

void SelectTool::SetSelectedIndex(int index, CM2DrawDoc* pDoc)
{
	m_selectedIndices.clear();
	if (index >= 0)
		m_selectedIndices.insert(index);
	SyncDocSelection(pDoc, m_selectedIndices);
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

	if (m_mode == Mode::Marquee)
	{
		// 마키(영역 선택) 드래그 중: 화면 갱신 유도
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

				static DWORD s_lastNodeTick = 0;
				DWORD curTick = ::GetTickCount();
				if (curTick - s_lastNodeTick >= 30)
				{
					s_lastNodeTick = curTick;
					UpdateShapePropertiesSelection(m_editShapeIdx, e.pDoc);
				}
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
		ApplyMove(shapes);
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

	// 변형(이동, 크기 조절, 회전) 중 상단 옵션바 수치 실시간 동기화 (30ms 쓰로틀링으로 메시지 큐 지연 차단)
	if (m_mode == Mode::Moving || m_mode == Mode::Scaling || m_mode == Mode::Rotating)
	{
		static DWORD s_lastDragTick = 0;
		DWORD curTick = ::GetTickCount();
		if (curTick - s_lastDragTick >= 30)
		{
			s_lastDragTick = curTick;
			UpdateShapePropertiesSelection(primaryIdx, e.pDoc);
		}
	}
}

// 엔진에 에디트 포인트 조작 위임!
void SelectTool::ApplyNodeEdit(PathShape* shape, const ToolEvent& e)
{
	if (!shape) return;
	// Alt 키가 눌려있으면 대칭/스무스 구속을 깨고 한쪽 핸들만 독립적으로 꺾음 (일러스트레이터 방식)
	shape->MoveNodeOrHandle(m_editHit.part, m_editHit.subPath, m_editHit.nodeIdx, e.worldPt, e.altDown);
}

// 엔진에 평행 이동 위임! (선택된 모든 도형 일괄 이동)
void SelectTool::ApplyMove(const std::vector<std::unique_ptr<Shape>>& shapes)
{
	float dx = m_dragCurrentWorld.x - m_dragStartWorld.x;
	float dy = m_dragCurrentWorld.y - m_dragStartWorld.y;

	const auto& grid = GetApp()->GetGrid();
	const auto& snapMgr = GetApp()->GetSnapManager();
	if (snapMgr.IsEnabled() && HasSnapTarget(snapMgr.GetTargets(), SnapTargets::Grid) && grid.enabled)
	{
		// 기준 도형으로 스냅 거리 계산
		int primaryIdx = m_selectedIndices.empty() ? -1 : *m_selectedIndices.begin();
		if (primaryIdx >= 0 && primaryIdx < static_cast<int>(shapes.size()))
		{
			PathShape* ps = dynamic_cast<PathShape*>(shapes[primaryIdx].get());
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
	}

	// 선택된 모든 도형에 동일한 이동량 적용
	for (int idx : m_selectedIndices)
	{
		if (idx >= 0 && idx < static_cast<int>(shapes.size()))
		{
			auto it = m_initialXfs.find(idx);
			D2D1::Matrix3x2F baseInitXf = (it != m_initialXfs.end()) ? it->second : shapes[idx]->xf;
			shapes[idx]->ApplyTranslation(D2D1::Point2F(dx, dy), baseInitXf);
		}
	}
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
	if (m_mode == Mode::Marquee && e.pDoc)
	{
		// 드래그한 마키 사각형 영역 계산 (월드 좌표)
		float minX = (std::min)(m_dragStartWorld.x, m_dragCurrentWorld.x);
		float maxX = (std::max)(m_dragStartWorld.x, m_dragCurrentWorld.x);
		float minY = (std::min)(m_dragStartWorld.y, m_dragCurrentWorld.y);
		float maxY = (std::max)(m_dragStartWorld.y, m_dragCurrentWorld.y);

		// 단순 클릭이 아니라 일정 크기 이상 드래그되었을 때만 영역 선택 판정
		if ((maxX - minX) > 2.0f || (maxY - minY) > 2.0f)
		{
			auto& shapes = e.pDoc->GetShapes();
			if (!e.ctrlDown)
			{
				m_selectedIndices.clear();
			}

			ID2D1Factory* f = e.pD2DFactory;
			for (int i = 0; i < static_cast<int>(shapes.size()); ++i)
			{
				D2D1_RECT_F localB{};
				if (shapes[i]->GetLocalBounds(f, localB))
				{
					// 도형의 4개 코너를 월드 변환하여 AABB 산출
					D2D1_POINT_2F pts[4] = {
						shapes[i]->xf.TransformPoint(D2D1::Point2F(localB.left, localB.top)),
						shapes[i]->xf.TransformPoint(D2D1::Point2F(localB.right, localB.top)),
						shapes[i]->xf.TransformPoint(D2D1::Point2F(localB.right, localB.bottom)),
						shapes[i]->xf.TransformPoint(D2D1::Point2F(localB.left, localB.bottom))
					};

					float sMinX = pts[0].x, sMaxX = pts[0].x;
					float sMinY = pts[0].y, sMaxY = pts[0].y;
					for (int k = 1; k < 4; ++k)
					{
						sMinX = (std::min)(sMinX, pts[k].x);
						sMaxX = (std::max)(sMaxX, pts[k].x);
						sMinY = (std::min)(sMinY, pts[k].y);
						sMaxY = (std::max)(sMaxY, pts[k].y);
					}

					// 사각형 교차(Intersect) 판정
					bool intersects = !(sMaxX < minX || sMinX > maxX || sMaxY < minY || sMinY > maxY);
					if (intersects)
					{
						m_selectedIndices.insert(i);
					}
				}
			}

			UpdateShapePropertiesSelection(GetSelectedIndex(), e.pDoc);
			SyncDocSelection(e.pDoc, m_selectedIndices);
			CString str;
			str.Format(_T("[선택 도구] 마키 영역 선택 완료 (선택된 도형: %d개)"), static_cast<int>(m_selectedIndices.size()));
			LogAppEvent(str);
		}
	}
	else if (m_mode != Mode::None)
	{
		// 변형/편집된 도형들을 ge::Document 로 동기화
		if (m_mode == Mode::EditNode && m_editShapeIdx >= 0)
		{
			SyncShapeToGeDoc(e.pDoc, m_editShapeIdx);
		}
		else
		{
			for (int idx : m_selectedIndices)
			{
				SyncShapeToGeDoc(e.pDoc, idx);
			}
		}

		UpdateShapePropertiesSelection(GetSelectedIndex(), e.pDoc);
		SyncDocSelection(e.pDoc, m_selectedIndices);
		CString str;
		str.Format(_T("[선택 도구] 변형 완료 (모드: %d)"), static_cast<int>(m_mode));
		LogAppEvent(str);
	}

	m_mode = Mode::None;
	m_handle = Handle::None;
}

void SelectTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush || !pDoc) return;

	ViewportTransform view = { zoom, pan };

	// 1. 마키(Marquee) 드래그 선택 사각형 렌더링
	if (m_mode == Mode::Marquee)
	{
		D2D1_POINT_2F p1 = view.WorldToScreen(m_dragStartWorld);
		D2D1_POINT_2F p2 = view.WorldToScreen(m_dragCurrentWorld);
		D2D1_RECT_F mr = D2D1::RectF(
			(std::min)(p1.x, p2.x), (std::min)(p1.y, p2.y),
			(std::max)(p1.x, p2.x), (std::max)(p1.y, p2.y)
		);

		// 반투명 파란색 채우기
		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.15f));
		rt->FillRectangle(mr, brush);

		// 점선/실선 테두리
		brush->SetColor(D2D1::ColorF(0.12f, 0.55f, 0.95f, 0.85f));
		rt->DrawRectangle(mr, brush, 1.2f);
	}

	if (m_selectedIndices.empty()) return;

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
	if (!pDoc) return false;

	// (A) 신형 엔진 단독 미리보기 노드가 있는 경우 (사각 도구 신엔진 테스트)
	if (m_previewNode)
	{
		m_previewNode->SetStyle(m_setting.ToGeStyle());
		m_previewNode->Invalidate();

		// 신형 엔진에 직접 추가
		ge::Node* added = pDoc->GetDoc().AddShape(std::move(m_previewNode));
		m_previewNode = nullptr;

		// 구형 브릿지 shape도 목록에 함께 등록 (속성창/도구 옵션바가 구형 인덱스를 참조하므로 연동 보장)
		if (m_previewShape)
		{
			m_previewShape->style.stroke = m_setting.strokeColor;
			m_previewShape->style.strokeWidth = m_setting.strokeWidth;
			m_previewShape->style.dashStyle = m_setting.dashStyle;
			m_previewShape->style.fillType = m_setting.fillType;
			m_previewShape->style.fill = m_setting.fillColor;
			m_previewShape->Invalidate();
			pDoc->GetShapes().push_back(std::move(m_previewShape));
			m_lastCommittedIndex = static_cast<int>(pDoc->GetShapes().size()) - 1;
			m_previewShape = nullptr;
		}

		if (added)
		{
			pDoc->GetDoc().SetSelection({ added });
		}

		UpdateShapePropertiesSelection(m_lastCommittedIndex, pDoc);
		SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
		if (pSelect)
		{
			pSelect->SetSelectedIndex(m_lastCommittedIndex, pDoc);
		}

		CString strEngineLog;
		strEngineLog.Format(_T("★ [신형 그래픽 엔진 (ge::Document)] 사각형 노드 생성 성공! (NodeId: %llu, 활성레이어: %s)"),
			added ? added->GetId() : 0,
			pDoc->GetDoc().GetActiveLayer() ? pDoc->GetDoc().GetActiveLayer()->GetName().c_str() : L"None");
		LogAppEvent(strEngineLog);
		SetAppStatusText(strEngineLog);

		pDoc->UpdateAllViews(nullptr);
		return true;
	}

	if (!m_previewShape) return false;

	// 1. 신형 그래픽 엔진(ge::Document) 노드 생성 및 파라미터 복사
	auto node = std::make_unique<ge::PathNode>();
	node->SetName(std::wstring((LPCTSTR)m_previewShape->name));
	node->SetKind(static_cast<ge::ShapeKind>(m_previewShape->GetKind()));
	std::visit([&node](auto&& arg) {
		using T = std::decay_t<decltype(arg)>;
		if constexpr (std::is_same_v<T, RectParams>) {
			ge::RectParams p; p.rx = arg.rx; p.ry = arg.ry; p.linkRxRy = arg.linkRxRy;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, TriangleParams>) {
			ge::TriangleParams p; p.equilateral = arg.equilateral;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, EllipseParams>) {
			ge::EllipseParams p; p.linkRxRy = arg.linkRxRy;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, PolygonParams>) {
			ge::PolygonParams p; p.sides = arg.sides; p.radius = arg.radius; p.cx = arg.cx; p.cy = arg.cy;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, StarParams>) {
			ge::StarParams p; p.points = arg.points; p.innerRatio = arg.innerRatio; p.radius = arg.radius; p.cx = arg.cx; p.cy = arg.cy;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, GearParams>) {
			ge::GearParams p; p.teeth = arg.teeth; p.depthRatio = arg.depthRatio; p.boreRatio = arg.boreRatio;
			p.toothWidthRatio = arg.toothWidthRatio; p.radius = arg.radius; p.cx = arg.cx; p.cy = arg.cy;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, SpiralParams>) {
			ge::SpiralParams p; p.turns = arg.turns; p.innerRatio = arg.innerRatio; p.radius = arg.radius; p.cx = arg.cx; p.cy = arg.cy;
			node->SetParams(p);
		} else if constexpr (std::is_same_v<T, HeartParams>) {
			node->SetParams(ge::HeartParams{});
		}
	}, m_previewShape->GetParams());

	std::vector<ge::SubPath> gePaths;
	for (const auto& sp : m_previewShape->paths) {
		ge::SubPath gsp;
		gsp.closed = sp.closed;
		for (const auto& bn : sp.nodes) {
			ge::BezierNode gbn;
			gbn.anchor = { bn.anchor.x, bn.anchor.y };
			gbn.hIn = { bn.hIn.x, bn.hIn.y };
			gbn.hOut = { bn.hOut.x, bn.hOut.y };
			gbn.type = static_cast<ge::BezierNode::Type>(bn.type);
			gsp.nodes.push_back(gbn);
		}
		gePaths.push_back(std::move(gsp));
	}
	node->SetPaths(gePaths);

	const auto& mx = m_previewShape->xf;
	node->SetTransform({ mx._11, mx._12, mx._21, mx._22, mx._31, mx._32 });
	node->SetStyle(m_setting.ToGeStyle());

	ge::Node* pAdded = pDoc->GetDoc().AddShape(std::move(node));
	if (pAdded)
	{
		pDoc->GetDoc().SetSelection({ pAdded });
	}

	CString strEngineLog;
	strEngineLog.Format(_T("★ [신형 그래픽 엔진 (ge::Document)] %s 노드 생성 성공! (NodeId: %llu, 활성레이어: %s)"),
		(LPCTSTR)m_previewShape->name,
		pAdded ? pAdded->GetId() : 0,
		pDoc->GetDoc().GetActiveLayer() ? pDoc->GetDoc().GetActiveLayer()->GetName().c_str() : L"None");
	LogAppEvent(strEngineLog);
	SetAppStatusText(strEngineLog);

	pDoc->GetShapes().push_back(std::move(m_previewShape));
	m_lastCommittedIndex = static_cast<int>(pDoc->GetShapes().size()) - 1;
	m_previewShape = nullptr;

	// 방금 등록된 도형 정보 표시 (속성창 + 상단 옵션바 수정 모드)
	UpdateShapePropertiesSelection(m_lastCommittedIndex, pDoc);
	SelectTool* pSelect = GetApp()->GetToolManager().GetSelectTool();
	if (pSelect)
	{
		pSelect->SetSelectedIndex(m_lastCommittedIndex, pDoc);
	}

	pDoc->UpdateAllViews(nullptr);
	return true;
}

void BaseShapeTool::CancelShape()
{
	m_previewShape = nullptr;
	m_previewNode = nullptr;
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

void BoxShapeTool::CalculateExtents(D2D1_POINT_2F curPt, bool shiftDown, bool altDown, D2D1_POINT_2F& outP0, D2D1_POINT_2F& outP1) const
{
	bool fromCenter = m_setting.bDrawFromCenter || altDown;

	if (m_kind == ShapeKind::Line)
	{
		if (fromCenter)
		{
			float dx = curPt.x - m_startPt.x;
			float dy = curPt.y - m_startPt.y;
			if (shiftDown)
			{
				float len = std::sqrt(dx * dx + dy * dy);
				float angle = std::atan2(dy, dx);
				const float snap = 3.14159265f / 4.0f;
				angle = std::round(angle / snap) * snap;
				dx = len * std::cos(angle);
				dy = len * std::sin(angle);
			}
			outP0 = D2D1::Point2F(m_startPt.x - dx, m_startPt.y - dy);
			outP1 = D2D1::Point2F(m_startPt.x + dx, m_startPt.y + dy);
		}
		else
		{
			outP0 = m_startPt;
			float dx = curPt.x - m_startPt.x;
			float dy = curPt.y - m_startPt.y;
			if (shiftDown)
			{
				float len = std::sqrt(dx * dx + dy * dy);
				float angle = std::atan2(dy, dx);
				const float snap = 3.14159265f / 4.0f;
				angle = std::round(angle / snap) * snap;
				dx = len * std::cos(angle);
				dy = len * std::sin(angle);
			}
			outP1 = D2D1::Point2F(m_startPt.x + dx, m_startPt.y + dy);
		}
		return;
	}

	float dx = std::abs(curPt.x - m_startPt.x);
	float dy = std::abs(curPt.y - m_startPt.y);

	bool forceSquare = shiftDown || 
	                   (m_kind == ShapeKind::RegularPoly ||
	                    m_kind == ShapeKind::Star ||
	                    m_kind == ShapeKind::Gear ||
	                    m_kind == ShapeKind::Spiral);

	if (forceSquare)
	{
		float maxD = (std::max)(dx, dy);
		dx = maxD;
		dy = maxD;
	}

	if (fromCenter)
	{
		outP0 = D2D1::Point2F(m_startPt.x - dx, m_startPt.y - dy);
		outP1 = D2D1::Point2F(m_startPt.x + dx, m_startPt.y + dy);
	}
	else
	{
		float signX = (curPt.x >= m_startPt.x) ? 1.0f : -1.0f;
		float signY = (curPt.y >= m_startPt.y) ? 1.0f : -1.0f;
		outP0 = m_startPt;
		outP1 = D2D1::Point2F(m_startPt.x + signX * dx, m_startPt.y + signY * dy);
	}
}

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
				D2D1_POINT_2F p0, p1;
				CalculateExtents(e.worldPt, e.shiftDown, e.altDown, p0, p1);
				UpdateGeometry(m_previewShape.get(), p0, p1);
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

	// 상단 옵션바를 "만들기" 모드로 설정하고 시작점 고정
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetOptionBar().SetMode(m_toolType, false);
	}
	UpdateOptionBarCreationCoords(m_startPt.x, m_startPt.y, 0.0f, 0.0f, true);

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

	// [신형 엔진 테스트] 사각형(Rect) 도구인 경우 신형 ge::PathNode 인스턴스도 직접 동시 생성
	if (m_kind == ShapeKind::Rect)
	{
		m_previewNode = std::make_unique<ge::PathNode>();
		m_previewNode->SetName(L"사각형");
		m_previewNode->SetKind(ge::ShapeKind::Rect);
		ge::RectParams rp;
		rp.rx = m_setting.cornerRx;
		rp.ry = m_setting.cornerRy;
		rp.linkRxRy = m_setting.bLinkRxRy;
		m_previewNode->SetParams(rp);
		m_previewNode->SetStyle(m_setting.ToGeStyle());
		m_previewNode->Invalidate();
	}
}

void BoxShapeTool::OnMove(const ToolEvent& e)
{
	m_hoverPt = e.worldPt;
	m_hasHover = true;

	if (m_isDragging || m_waitingSecondClick)
	{
		D2D1_POINT_2F p0, p1;
		CalculateExtents(e.worldPt, e.shiftDown, e.altDown, p0, p1);

		if (m_previewShape)
		{
			UpdateGeometry(m_previewShape.get(), p0, p1);
		}

		// [신형 엔진 테스트] 사각형(Rect) 신엔진 노드 기하학 직접 갱신
		if (m_previewNode && m_kind == ShapeKind::Rect)
		{
			float left = min(p0.x, p1.x);
			float top = min(p0.y, p1.y);
			float right = max(p0.x, p1.x);
			float bottom = max(p0.y, p1.y);
			float rx = (right - left) * 0.5f;
			float ry = (bottom - top) * 0.5f;
			float crx = (std::min)(rx, (std::max)(0.0f, m_setting.cornerRx));
			float cry = (std::min)(ry, (std::max)(0.0f, m_setting.cornerRy));

			ge::SubPath sp;
			sp.closed = true;
			if (crx > 0.001f || cry > 0.001f)
			{
				const float kx = crx * 0.55228475f;
				const float ky = cry * 0.55228475f;
				ge::BezierNode n[8];
				n[0].anchor = { left + crx, top };      n[0].hIn = { -kx, 0.0f }; n[0].hOut = { 0, 0 };
				n[1].anchor = { right - crx, top };     n[1].hIn = { 0, 0 };      n[1].hOut = { kx, 0.0f };
				n[2].anchor = { right, top + cry };     n[2].hIn = { 0.0f, -ky }; n[2].hOut = { 0, 0 };
				n[3].anchor = { right, bottom - cry };  n[3].hIn = { 0, 0 };      n[3].hOut = { 0.0f, ky };
				n[4].anchor = { right - crx, bottom };  n[4].hIn = { kx, 0.0f };  n[4].hOut = { 0, 0 };
				n[5].anchor = { left + crx, bottom };   n[5].hIn = { 0, 0 };      n[5].hOut = { -kx, 0.0f };
				n[6].anchor = { left, bottom - cry };   n[6].hIn = { 0.0f, ky };  n[6].hOut = { 0, 0 };
				n[7].anchor = { left, top + cry };      n[7].hIn = { 0, 0 };      n[7].hOut = { 0.0f, -ky };
				for (int i = 0; i < 8; ++i) sp.nodes.push_back(n[i]);
			}
			else
			{
				ge::BezierNode n[4];
				n[0].anchor = { left, top };
				n[1].anchor = { right, top };
				n[2].anchor = { right, bottom };
				n[3].anchor = { left, bottom };
				for (int i = 0; i < 4; ++i) sp.nodes.push_back(n[i]);
			}
			m_previewNode->SetPaths({ sp });
			m_previewNode->Invalidate();
		}

		float left = (std::min)(p0.x, p1.x);
		float top = (std::min)(p0.y, p1.y);
		float w = std::abs(p1.x - p0.x);
		float h = std::abs(p1.y - p0.y);

		UpdateOptionBarCreationCoords(left, top, w, h, true);
	}
	else
	{
		// 첫 포인트 찍기 전: 마우스 위치를 X, Y에 실시간 표시 (하단 좌표와 동일)
		UpdateOptionBarCreationCoords(e.worldPt.x, e.worldPt.y, 0.0f, 0.0f, false);
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
			D2D1_POINT_2F p0, p1;
			CalculateExtents(e.worldPt, e.shiftDown, e.altDown, p0, p1);
			UpdateGeometry(m_previewShape.get(), p0, p1);
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
		UpdateOptionBarCreationCoords(m_startPt.x, m_startPt.y, 0.0f, 0.0f, true);
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
	if ((m_previewShape || m_previewNode) && (m_isDragging || m_waitingSecondClick))
	{
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);

		// [신형 엔진 테스트] 사각형인 경우 신형 D2DRenderer로 직접 렌더링 검증!
		if (m_previewNode && m_kind == ShapeKind::Rect)
		{
			ge::D2DRenderer renderer;
			if (renderer.Initialize(rt, pFactory))
			{
				ge::Viewport geView = { zoom, { pan.x, pan.y } };
				renderer.DrawNode(*m_previewNode, ge::Matrix2D::Identity(), geView);
			}
		}
		else if (m_previewShape)
		{
			// (1) 도형 본체 미리보기 렌더링 (엔진 기하학)
			m_previewShape->Draw(rt, brush, pFactory, view);
		}

		if (pFactory) pFactory->Release();

		// (2) 첫 번째 앵커 포인트 고정 표시 (주황/빨강 강조 마커)
		D2D1_POINT_2F p0Screen = view.WorldToScreen(m_startPt);
		const float sz0 = 4.5f;
		D2D1_RECT_F hr0 = D2D1::RectF(p0Screen.x - sz0, p0Screen.y - sz0, p0Screen.x + sz0, p0Screen.y + sz0);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
		rt->FillRectangle(hr0, brush);
		brush->SetColor(D2D1::ColorF(D2D1::ColorF::OrangeRed));
		rt->DrawRectangle(hr0, brush, 2.0f);

		// 중심 그리기 모드일 때: 중심점(m_startPt)에 십자선 가이드 표시
		if (m_setting.bDrawFromCenter)
		{
			brush->SetColor(D2D1::ColorF(D2D1::ColorF::OrangeRed, 0.85f));
			rt->DrawLine(D2D1::Point2F(p0Screen.x - 7.0f, p0Screen.y), D2D1::Point2F(p0Screen.x + 7.0f, p0Screen.y), brush, 1.2f);
			rt->DrawLine(D2D1::Point2F(p0Screen.x, p0Screen.y - 7.0f), D2D1::Point2F(p0Screen.x, p0Screen.y + 7.0f), brush, 1.2f);
		}

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
		D2D1_POINT_2F p0, p1;
		CalculateExtents(curPt, false, false, p0, p1);
		UpdateGeometry(m_previewShape.get(), p0, p1);
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
		shape->SetKind(::ShapeKind::Rect);
		RectParams rp;
		rp.rx = m_setting.cornerRx;
		rp.ry = m_setting.cornerRy;
		rp.linkRxRy = m_setting.bLinkRxRy;
		shape->SetParams(rp);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->paths.back().closed = true;
	}
	else if (m_kind == ShapeKind::Ellipse)
	{
		shape->name = _T("타원");
		shape->SetKind(::ShapeKind::Ellipse);
		EllipseParams ep;
		ep.linkRxRy = m_setting.bLinkRxRy;
		shape->SetParams(ep);
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
	else if (m_kind == ShapeKind::Triangle)
	{
		shape->name = _T("삼각형");
		shape->SetKind(::ShapeKind::Triangle);
		TriangleParams tp;
		tp.equilateral = false;
		shape->SetParams(tp);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->AddAnchor(p0);
		shape->paths.back().closed = true;
	}
	else if (m_kind == ShapeKind::Star)
	{
		shape->name = _T("별");
		shape->SetKind(::ShapeKind::Star);
		StarParams sp;
		sp.points = (std::max)(3, (std::min)(32, m_setting.starPoints));
		sp.innerRatio = (std::max)(0.05f, (std::min)(0.95f, m_setting.starInnerRatio));
		shape->SetParams(sp);
		int n = sp.points;
		for (int i = 0; i < n * 2; ++i) shape->AddAnchor(p0);
		shape->paths.back().closed = true;
	}
	else if (m_kind == ShapeKind::RegularPoly)
	{
		shape->name = _T("정다각형");
		shape->SetKind(::ShapeKind::Polygon);
		PolygonParams pp;
		pp.sides = (std::max)(3, (std::min)(32, m_setting.polySides));
		shape->SetParams(pp);
		int n = pp.sides;
		for (int i = 0; i < n; ++i) shape->AddAnchor(p0);
		shape->paths.back().closed = true;
	}
	else if (m_kind == ShapeKind::Gear)
	{
		shape->name = _T("기어");
		shape->SetKind(::ShapeKind::Gear);
		GearParams gp;
		gp.teeth = (std::max)(4, (std::min)(48, m_setting.gearTeeth));
		gp.depthRatio = (std::max)(0.05f, (std::min)(0.50f, m_setting.gearDepthRatio));
		gp.boreRatio = (std::max)(0.05f, (std::min)(0.80f, m_setting.gearHoleRatio));
		shape->SetParams(gp);
		int n = gp.teeth;
		for (int i = 0; i < n * 4; ++i) shape->AddAnchor(p0);
		shape->paths.back().closed = true;

		// 2번째 서브패스: 축 구멍 4개 노드
		shape->paths.emplace_back();
		for (int i = 0; i < 4; ++i) {
			BezierNode bn; bn.type = BezierNode::Type::Smooth;
			shape->paths.back().nodes.push_back(bn);
		}
		shape->paths.back().closed = true;
	}
	else if (m_kind == ShapeKind::Spiral)
	{
		shape->name = _T("스파이럴");
		shape->SetKind(::ShapeKind::Spiral);
		SpiralParams sp;
		sp.turns = (std::max)(0.5f, (std::min)(10.0f, m_setting.spiralTurns));
		sp.innerRatio = 0.1f;
		shape->SetParams(sp);
		int segs = static_cast<int>(sp.turns * 4.0f);
		if (segs < 4) segs = 4;
		SubPath sub;
		for (int i = 0; i <= segs; ++i) {
			BezierNode n; n.type = BezierNode::Type::Smooth;
			sub.nodes.push_back(n);
		}
		sub.closed = false;
		shape->paths.push_back(sub);
	}
	else if (m_kind == ShapeKind::Heart)
	{
		shape->name = _T("하트");
		shape->SetKind(::ShapeKind::Heart);
		HeartParams hp;
		shape->SetParams(hp);
		SubPath sp;
		for (int i = 0; i < 4; ++i) {
			BezierNode n; n.type = BezierNode::Type::Smooth;
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

	float left = min(p0.x, p1.x);
	float top = min(p0.y, p1.y);
	float right = max(p0.x, p1.x);
	float bottom = max(p0.y, p1.y);
	float w = max(1.0f, right - left);
	float h = max(1.0f, bottom - top);
	float cx = left + w * 0.5f;
	float cy = top + h * 0.5f;
	float rx = w * 0.5f;
	float ry = h * 0.5f;

	if (m_kind == ShapeKind::Line)
	{
		if (pShape->paths[0].nodes.size() < 2) return;
		pShape->paths[0].nodes[0].anchor = p0;
		pShape->paths[0].nodes[1].anchor = p1;
	}
	else if (m_kind == ShapeKind::Rect)
	{
		float crx = (std::min)(rx, (std::max)(0.0f, m_setting.cornerRx));
		float cry = (std::min)(ry, (std::max)(0.0f, m_setting.cornerRy));

		if (crx > 0.001f || cry > 0.001f)
		{
			// 둥근 사각형 (8개 앵커 + 베이지어 4분원)
			auto& nodes = pShape->paths[0].nodes;
			if (nodes.size() < 8) {
				nodes.clear();
				for (int i = 0; i < 8; ++i) {
					BezierNode bn; bn.type = BezierNode::Type::Corner;
					nodes.push_back(bn);
				}
			}
			const float kx = crx * 0.55228475f;
			const float ky = cry * 0.55228475f;

			// Top-Left to Top-Right
			nodes[0].anchor = D2D1::Point2F(left + crx, top);
			nodes[0].hIn = D2D1::Point2F(-kx, 0.0f); nodes[0].hOut = D2D1::Point2F(0, 0);

			nodes[1].anchor = D2D1::Point2F(right - crx, top);
			nodes[1].hIn = D2D1::Point2F(0, 0); nodes[1].hOut = D2D1::Point2F(kx, 0.0f);

			// Right edge
			nodes[2].anchor = D2D1::Point2F(right, top + cry);
			nodes[2].hIn = D2D1::Point2F(0.0f, -ky); nodes[2].hOut = D2D1::Point2F(0, 0);

			nodes[3].anchor = D2D1::Point2F(right, bottom - cry);
			nodes[3].hIn = D2D1::Point2F(0, 0); nodes[3].hOut = D2D1::Point2F(0.0f, ky);

			// Bottom edge
			nodes[4].anchor = D2D1::Point2F(right - crx, bottom);
			nodes[4].hIn = D2D1::Point2F(kx, 0.0f); nodes[4].hOut = D2D1::Point2F(0, 0);

			nodes[5].anchor = D2D1::Point2F(left + crx, bottom);
			nodes[5].hIn = D2D1::Point2F(0, 0); nodes[5].hOut = D2D1::Point2F(-kx, 0.0f);

			// Left edge
			nodes[6].anchor = D2D1::Point2F(left, bottom - cry);
			nodes[6].hIn = D2D1::Point2F(0.0f, ky); nodes[6].hOut = D2D1::Point2F(0, 0);

			nodes[7].anchor = D2D1::Point2F(left, top + cry);
			nodes[7].hIn = D2D1::Point2F(0, 0); nodes[7].hOut = D2D1::Point2F(0.0f, -ky);
		}
		else
		{
			// 일반 직각 사각형 (4개 정점)
			auto& nodes = pShape->paths[0].nodes;
			if (nodes.size() != 4) {
				nodes.clear();
				for (int i = 0; i < 4; ++i) nodes.emplace_back();
			}
			nodes[0].anchor = D2D1::Point2F(left, top);
			nodes[0].hIn = nodes[0].hOut = { 0, 0 };
			nodes[1].anchor = D2D1::Point2F(right, top);
			nodes[1].hIn = nodes[1].hOut = { 0, 0 };
			nodes[2].anchor = D2D1::Point2F(right, bottom);
			nodes[2].hIn = nodes[2].hOut = { 0, 0 };
			nodes[3].anchor = D2D1::Point2F(left, bottom);
			nodes[3].hIn = nodes[3].hOut = { 0, 0 };
		}
	}
	else if (m_kind == ShapeKind::Ellipse)
	{
		if (pShape->paths[0].nodes.size() < 4) return;
		const float kappa = 0.55228475f;
		float kx = rx * kappa;
		float ky = ry * kappa;

		auto& nodes = pShape->paths[0].nodes;
		nodes[0].anchor = D2D1::Point2F(cx, cy - ry);
		nodes[0].hIn = D2D1::Point2F(-kx, 0.0f); nodes[0].hOut = D2D1::Point2F(kx, 0.0f);

		nodes[1].anchor = D2D1::Point2F(cx + rx, cy);
		nodes[1].hIn = D2D1::Point2F(0.0f, -ky); nodes[1].hOut = D2D1::Point2F(0.0f, ky);

		nodes[2].anchor = D2D1::Point2F(cx, cy + ry);
		nodes[2].hIn = D2D1::Point2F(kx, 0.0f); nodes[2].hOut = D2D1::Point2F(-kx, 0.0f);

		nodes[3].anchor = D2D1::Point2F(cx - rx, cy);
		nodes[3].hIn = D2D1::Point2F(0.0f, ky); nodes[3].hOut = D2D1::Point2F(0.0f, -ky);
	}
	else if (m_kind == ShapeKind::Triangle)
	{
		// 이등변 삼각형: (상단 중앙, 우하단, 좌하단)
		auto& nodes = pShape->paths[0].nodes;
		if (nodes.size() < 3) return;
		nodes[0].anchor = D2D1::Point2F(cx, top);
		nodes[0].hIn = nodes[0].hOut = { 0, 0 };
		nodes[1].anchor = D2D1::Point2F(right, bottom);
		nodes[1].hIn = nodes[1].hOut = { 0, 0 };
		nodes[2].anchor = D2D1::Point2F(left, bottom);
		nodes[2].hIn = nodes[2].hOut = { 0, 0 };
	}
	else if (m_kind == ShapeKind::Star)
	{
		// 다각 별: 외경(R), 내경(r) 삼각함수 교차점
		StarParams* sp = pShape->GetParamRef<StarParams>();
		int n = sp ? sp->points : (std::max)(3, (std::min)(32, m_setting.starPoints));
		if (sp) { sp->radius = rx; sp->cx = cx; sp->cy = cy; }
		auto& nodes = pShape->paths[0].nodes;
		if (static_cast<int>(nodes.size()) != n * 2) {
			nodes.clear();
			for (int i = 0; i < n * 2; ++i) nodes.emplace_back();
		}
		const float PI = 3.14159265358979323846f;
		float innerRatio = sp ? sp->innerRatio : (std::max)(0.05f, (std::min)(0.95f, m_setting.starInnerRatio));
		for (int i = 0; i < n * 2; ++i) {
			float angle = -PI * 0.5f + (i * PI / n);
			float curR = (i % 2 == 0) ? 1.0f : innerRatio;
			nodes[i].anchor = D2D1::Point2F(cx + rx * curR * std::cos(angle), cy + ry * curR * std::sin(angle));
			nodes[i].hIn = nodes[i].hOut = { 0, 0 };
		}
	}
	else if (m_kind == ShapeKind::RegularPoly)
	{
		// 정다각형: 원주 상의 N개 정점
		PolygonParams* pp = pShape->GetParamRef<PolygonParams>();
		int n = pp ? pp->sides : (std::max)(3, (std::min)(32, m_setting.polySides));
		if (pp) { pp->radius = rx; pp->cx = cx; pp->cy = cy; }
		auto& nodes = pShape->paths[0].nodes;
		if (static_cast<int>(nodes.size()) != n) {
			nodes.clear();
			for (int i = 0; i < n; ++i) nodes.emplace_back();
		}
		const float PI = 3.14159265358979323846f;
		for (int i = 0; i < n; ++i) {
			float angle = -PI * 0.5f + (i * 2.0f * PI / n);
			nodes[i].anchor = D2D1::Point2F(cx + rx * std::cos(angle), cy + ry * std::sin(angle));
			nodes[i].hIn = nodes[i].hOut = { 0, 0 };
		}
	}
	else if (m_kind == ShapeKind::Gear)
	{
		// 기어: N개 톱니 (외경 톱니 상단 2점, 내경 홈 2점) + 중앙 축 구멍(Bore Hole)
		GearParams* gp = pShape->GetParamRef<GearParams>();
		int n = gp ? gp->teeth : (std::max)(4, (std::min)(48, m_setting.gearTeeth));
		if (gp) { gp->radius = rx; gp->cx = cx; gp->cy = cy; }
		if (pShape->paths.empty()) {
			pShape->paths.emplace_back();
		}
		auto& outerNodes = pShape->paths[0].nodes;
		if (static_cast<int>(outerNodes.size()) != n * 4) {
			outerNodes.clear();
			for (int i = 0; i < n * 4; ++i) outerNodes.emplace_back();
		}
		pShape->paths[0].closed = true;

		const float PI = 3.14159265358979323846f;
		float depth = gp ? gp->depthRatio : (std::max)(0.05f, (std::min)(0.5f, m_setting.gearDepthRatio));
		float rInner = 1.0f - depth;
		float boreRatio = gp ? gp->boreRatio : (std::max)(0.05f, (std::min)(0.80f, m_setting.gearHoleRatio));
		// 축 구멍 반경은 루트 반경보다 작아야 함 (Clamp)
		boreRatio = (std::min)(boreRatio, rInner - 0.05f);
		if (boreRatio < 0.05f) boreRatio = 0.05f;

		for (int i = 0; i < n * 4; ++i) {
			float angle = -PI * 0.5f + (i * 2.0f * PI / (n * 4));
			int mod = i % 4;
			float curR = (mod == 1 || mod == 2) ? 1.0f : rInner;
			outerNodes[i].anchor = D2D1::Point2F(cx + rx * curR * std::cos(angle), cy + ry * curR * std::sin(angle));
			outerNodes[i].hIn = outerNodes[i].hOut = { 0, 0 };
		}

		// 중앙 축 구멍(Bore Hole) 서브패스: 4-노드 3차 베지어 원 (κ = 0.55228475f)
		if (pShape->paths.size() < 2) {
			pShape->paths.emplace_back();
		}
		auto& holeNodes = pShape->paths[1].nodes;
		if (holeNodes.size() != 4) {
			holeNodes.clear();
			for (int i = 0; i < 4; ++i) {
				BezierNode bn;
				bn.type = BezierNode::Type::Smooth;
				holeNodes.push_back(bn);
			}
		}
		pShape->paths[1].closed = true;

		const float KAPPA = 0.5522847498f;
		float hx = rx * boreRatio;
		float hy = ry * boreRatio;
		float kx = hx * KAPPA;
		float ky = hy * KAPPA;

		// 4개 제어점: Top, Right, Bottom, Left
		holeNodes[0].anchor = D2D1::Point2F(cx, cy - hy);
		holeNodes[0].hIn    = D2D1::Point2F(-kx, 0.0f);
		holeNodes[0].hOut   = D2D1::Point2F(kx, 0.0f);

		holeNodes[1].anchor = D2D1::Point2F(cx + hx, cy);
		holeNodes[1].hIn    = D2D1::Point2F(0.0f, -ky);
		holeNodes[1].hOut   = D2D1::Point2F(0.0f, ky);

		holeNodes[2].anchor = D2D1::Point2F(cx, cy + hy);
		holeNodes[2].hIn    = D2D1::Point2F(kx, 0.0f);
		holeNodes[2].hOut   = D2D1::Point2F(-kx, 0.0f);

		holeNodes[3].anchor = D2D1::Point2F(cx - hx, cy);
		holeNodes[3].hIn    = D2D1::Point2F(0.0f, ky);
		holeNodes[3].hOut   = D2D1::Point2F(0.0f, -ky);
	}
	else if (m_kind == ShapeKind::Spiral)
	{
		// 아르키메데스 나선 (Open Path)
		SpiralParams* sp = pShape->GetParamRef<SpiralParams>();
		float turns = sp ? sp->turns : m_setting.spiralTurns;
		if (sp) { sp->radius = rx; sp->cx = cx; sp->cy = cy; }
		int segs = static_cast<int>(turns * 4.0f);
		if (segs < 4) segs = 4;
		auto& nodes = pShape->paths[0].nodes;
		if (static_cast<int>(nodes.size()) != segs + 1) {
			nodes.clear();
			for (int i = 0; i <= segs; ++i) {
				BezierNode bn; bn.type = BezierNode::Type::Smooth;
				nodes.push_back(bn);
			}
		}
		const float PI = 3.14159265358979323846f;
		const float kappa = 0.55228475f;
		for (int i = 0; i <= segs; ++i) {
			float t = static_cast<float>(i) / segs;
			float angle = t * turns * 2.0f * PI;
			float curR = t; // 중심에서 바깥으로 팽창
			nodes[i].anchor = D2D1::Point2F(cx + rx * curR * std::cos(angle), cy + ry * curR * std::sin(angle));
			// 접선 방향 베이지어 핸들
			float tanX = -std::sin(angle) * rx * curR * (PI * 0.5f / segs);
			float tanY = std::cos(angle) * ry * curR * (PI * 0.5f / segs);
			nodes[i].hIn = D2D1::Point2F(-tanX, -tanY);
			nodes[i].hOut = D2D1::Point2F(tanX, tanY);
		}
	}
	else if (m_kind == ShapeKind::Heart)
	{
		// 하트 (상단 골짜기, 우측 볼륨, 하단 뾰족점, 좌측 볼륨)
		auto& nodes = pShape->paths[0].nodes;
		if (nodes.size() < 4) {
			nodes.clear();
			for (int i = 0; i < 4; ++i) {
				BezierNode bn; bn.type = BezierNode::Type::Smooth;
				nodes.push_back(bn);
			}
		}
		// 0: 상단 중심 골짜기 (Corner)
		nodes[0].type = BezierNode::Type::Corner;
		nodes[0].anchor = D2D1::Point2F(cx, top + h * 0.28f);
		nodes[0].hIn = D2D1::Point2F(-rx * 0.5f, -h * 0.28f);
		nodes[0].hOut = D2D1::Point2F(rx * 0.5f, -h * 0.28f);

		// 1: 우측 볼륨
		nodes[1].type = BezierNode::Type::Smooth;
		nodes[1].anchor = D2D1::Point2F(right, top + h * 0.42f);
		nodes[1].hIn = D2D1::Point2F(0, -h * 0.2f);
		nodes[1].hOut = D2D1::Point2F(0, h * 0.25f);

		// 2: 하단 끝점 (Corner)
		nodes[2].type = BezierNode::Type::Corner;
		nodes[2].anchor = D2D1::Point2F(cx, bottom);
		nodes[2].hIn = D2D1::Point2F(rx * 0.4f, -h * 0.25f);
		nodes[2].hOut = D2D1::Point2F(-rx * 0.4f, -h * 0.25f);

		// 3: 좌측 볼륨
		nodes[3].type = BezierNode::Type::Smooth;
		nodes[3].anchor = D2D1::Point2F(left, top + h * 0.42f);
		nodes[3].hIn = D2D1::Point2F(0, h * 0.25f);
		nodes[3].hOut = D2D1::Point2F(0, -h * 0.2f);
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

// ============================================================================
// 5. 손 / 화면 이동 도구 (PanTool)
// ============================================================================
void PanTool::OnDown(const ToolEvent& e)
{
	m_isPanning = true;
	m_lastScreenPt = e.screenPt;
}

void PanTool::OnMove(const ToolEvent& e)
{
	if (!m_isPanning || !e.pView) return;

	float dx = static_cast<float>(e.screenPt.x - m_lastScreenPt.x);
	float dy = static_cast<float>(e.screenPt.y - m_lastScreenPt.y);
	m_lastScreenPt = e.screenPt;

	D2D1_POINT_2F curPan = e.pView->GetPan();
	curPan.x += dx;
	curPan.y += dy;
	e.pView->SetPan(curPan);
}

void PanTool::OnUp(const ToolEvent& e)
{
	m_isPanning = false;
}

// ============================================================================
// 6. 돋보기 도구 (ZoomInTool / ZoomOutTool)
// ============================================================================
void ZoomInTool::OnDown(const ToolEvent& e)
{
	m_isDragging = true;
	m_dragStartScreen = e.screenPt;
	m_dragCurScreen = e.screenPt;
}

void ZoomInTool::OnMove(const ToolEvent& e)
{
	if (m_isDragging)
	{
		m_dragCurScreen = e.screenPt;
	}
}

void ZoomInTool::OnUp(const ToolEvent& e)
{
	if (!m_isDragging) return;
	m_isDragging = false;

	if (!e.pView) return;

	int dx = std::abs(m_dragCurScreen.x - m_dragStartScreen.x);
	int dy = std::abs(m_dragCurScreen.y - m_dragStartScreen.y);

	// 5px 미만 클릭인 경우: 마우스 휠과 동일하게 클릭 위치 기준 1.25배 확대
	if (dx < 5 && dy < 5)
	{
		e.pView->ZoomAtPoint(e.screenPt, 1.25f);
	}
	else
	{
		// 드래그 영역(Marquee) 줌: 드래그한 사각형 영역을 뷰포트에 맞춤 확대
		CRect marqueeRc(
			min(m_dragStartScreen.x, m_dragCurScreen.x),
			min(m_dragStartScreen.y, m_dragCurScreen.y),
			max(m_dragStartScreen.x, m_dragCurScreen.x),
			max(m_dragStartScreen.y, m_dragCurScreen.y)
		);
		e.pView->ZoomAtRect(marqueeRc);
	}
}

void ZoomInTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
                             float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!m_isDragging || !rt || !brush) return;

	float left = static_cast<float>(min(m_dragStartScreen.x, m_dragCurScreen.x));
	float top = static_cast<float>(min(m_dragStartScreen.y, m_dragCurScreen.y));
	float right = static_cast<float>(max(m_dragStartScreen.x, m_dragCurScreen.x));
	float bottom = static_cast<float>(max(m_dragStartScreen.y, m_dragCurScreen.y));

	if (right - left > 2.0f && bottom - top > 2.0f)
	{
		D2D1_RECT_F r = D2D1::RectF(left, top, right, bottom);

		// 반투명 파란색 채우기 박스
		brush->SetColor(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.15f));
		rt->FillRectangle(r, brush);

		// 외곽선
		brush->SetColor(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.9f));
		rt->DrawRectangle(r, brush, 1.2f);
	}
}

void ZoomOutTool::OnDown(const ToolEvent& e)
{
	if (!e.pView) return;

	// 마우스 휠과 동일하게 클릭 지점을 기준으로 0.8배 축소
	e.pView->ZoomAtPoint(e.screenPt, 0.8f);
}

void ZoomOutTool::OnMove(const ToolEvent& e)
{
}

void ZoomOutTool::OnUp(const ToolEvent& e)
{
}

