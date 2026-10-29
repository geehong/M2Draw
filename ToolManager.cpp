#include "pch.h"
#include "ToolManager.h"
#include "M2DrawDoc.h"
#include "M2DrawView.h"
#include "M2Draw.h"

// PanTool, ZoomInTool, ZoomOutTool은 ITool.cpp에 구현되어 있습니다.

// ---------------- PenTool ----------------
void PenTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;

	auto& currentPath = e.pDoc->GetCurrentPath();
	if (!currentPath) currentPath = std::make_unique<ge::PathNode>();

	D2D1_POINT_2F screenD2D = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));
	D2D1_POINT_2F snappedWorld = e.worldPt;

	D2D1::Matrix3x2F invShape = currentPath->GetTransform();
	if (!invShape.Invert()) return;
	D2D1_POINT_2F localPt = invShape.TransformPoint(snappedWorld);

	m_downScreenPt = screenD2D;
	m_state = State::Pressed;

	// 패스 폐합 검사 (첫 앵커와 8px 이내인지 비교)
	if (!m_cornerOnly && !m_needNewSubPath && !currentPath->GetPaths().empty() && !currentPath->GetPathsRef().back().closed && currentPath->GetPathsRef().back().nodes.size() >= 3)
	{
		auto& sp = currentPath->GetPathsRef().back();
		D2D1_POINT_2F firstScreenPt = CoordinateSpace::LocalToScreen(sp.nodes[0].anchor, currentPath->GetTransform(), e.view);
		float dx = firstScreenPt.x - screenD2D.x;
		float dy = firstScreenPt.y - screenD2D.y;

		if ((dx * dx + dy * dy) <= 64.0f)
		{
			sp.closed = true;
			currentPath->Invalidate();
			m_state = State::Idle;
			m_needNewSubPath = true;
			return;
		}
	}

	currentPath->AddAnchor(localPt, m_needNewSubPath);
	m_needNewSubPath = false;

	UpdateOptionBarCreationCoords(snappedWorld.x, snappedWorld.y, 0.0f, 0.0f, false);

	CString str;
	str.Format(_T("[펜 도구] 앵커 노드 추가: (%.1f, %.1f) px"), snappedWorld.x, snappedWorld.y);
	LogAppEvent(str);
}

void PenTool::OnMove(const ToolEvent& e)
{
	m_hoverWorld = e.worldPt;
	m_hasHover = true;

	// 상단 옵션바에 마우스 위치 실시간 표시 (하단 좌표와 일치)
	UpdateOptionBarCreationCoords(e.worldPt.x, e.worldPt.y, 0.0f, 0.0f, false);

	if (!e.pDoc) return;

	auto& currentPath = e.pDoc->GetCurrentPath();
	if (!currentPath || currentPath->GetPaths().empty()) return;
	auto& sp = currentPath->GetPathsRef().back();
	if (sp.nodes.empty()) return;

	D2D1_POINT_2F screenD2D = D2D1::Point2F(static_cast<float>(e.screenPt.x), static_cast<float>(e.screenPt.y));

	if (m_cornerOnly) return;   // 폴리라인: 핸들 드래그 없음

	if (m_state == State::Pressed)
	{
		float dx = screenD2D.x - m_downScreenPt.x;
		float dy = screenD2D.y - m_downScreenPt.y;
		if ((dx * dx + dy * dy) >= 16.0f)
		{
			m_state = State::DraggingHandle;
			sp.nodes.back().type = BezierNode::Type::Symmetric;
		}
	}

	if (m_state == State::DraggingHandle)
	{
		D2D1_POINT_2F snappedWorld = e.worldPt;
		D2D1::Matrix3x2F invShape = currentPath->GetTransform();
		if (!invShape.Invert()) return;
		D2D1_POINT_2F localPt = invShape.TransformPoint(snappedWorld);

		auto& node = sp.nodes.back();
		D2D1_POINT_2F offset = D2D1::Point2F(localPt.x - node.anchor.x, localPt.y - node.anchor.y);

		if (e.altDown)
		{
			// 드래그 중 Alt를 누르면 곡선 대칭 구속을 깨고 한쪽 핸들만 각지게 꺾음 (일러스트레이터 방식)
			node.type = BezierNode::Type::Corner;
			node.SetHandleOut(offset);
		}
		else
		{
			if (node.type == BezierNode::Type::Corner)
				node.type = BezierNode::Type::Symmetric;
			node.SetHandleOut(offset);
		}
		currentPath->Invalidate();
	}
}

void PenTool::OnUp(const ToolEvent& e)
{
	if (m_state == State::DraggingHandle || m_state == State::Pressed)
	{
		m_state = State::Placing;
	}
}

// 작성 중인 펜 패스를 문서 활성 레이어에 등록 (소유권 이동, 단일 모델)
static void CommitPenPath(CM2DrawDoc* pDoc)
{
	auto& path = pDoc->GetCurrentPath();
	if (!path) return;
	path->SetName(pDoc->GetDoc().GenerateNodeName(ge::ShapeKind::Path));
	path->SetKind(ge::ShapeKind::Path);
	path->Invalidate();
	ge::Node* pAdded = pDoc->GetDoc().AddShape(std::move(path));
	path = nullptr;
	if (pAdded) pDoc->GetDoc().SetSelection({ pAdded });
	pDoc->SetModifiedFlag(TRUE);
}
void PenTool::OnKeyDown(UINT nChar, CM2DrawDoc* pDoc)
{
	if (!pDoc) return;

	if (nChar == VK_RETURN)
	{
		// Enter 키: 현재 열린 패스 완료 후 문서 도형 목록에 보관
		if (pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->GetPaths().empty())
		{
			m_state = State::Idle;
			m_needNewSubPath = true;
			CommitPenPath(pDoc);
			pDoc->GetCurrentPath() = nullptr;
			LogAppEvent(_T("[펜 도구] 패스 작성 완료 (Enter)"));
		}
	}
	else if (nChar == VK_ESCAPE)
	{
		// Esc 키: 작성 중 취소
		pDoc->GetCurrentPath() = nullptr;
		m_state = State::Idle;
		m_needNewSubPath = true;
		LogAppEvent(_T("[펜 도구] 패스 작성 취소 (Escape)"));
	}
}

bool PenTool::HasActiveShape() const
{
	return m_state != State::Idle;
}

bool PenTool::OnDoubleClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return true;

	if (pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->GetPaths().empty())
	{
		auto& nodes = pDoc->GetCurrentPath()->GetPathsRef().back().nodes;

		// 더블클릭 시 방금 클릭된 직전 점과 좌표가 겹치면 중복점 1개 제거
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

		if (nodes.size() >= 2)
		{
			m_state = State::Idle;
			m_needNewSubPath = true;
			pDoc->GetCurrentPath()->Invalidate();
			CommitPenPath(pDoc);
			pDoc->GetCurrentPath() = nullptr;
			LogAppEvent(_T("[펜 도구] 패스 작성 완료 (더블클릭 -> 선택 도구 전환)"));
		}
		else
		{
			pDoc->GetCurrentPath() = nullptr;
			m_state = State::Idle;
			m_needNewSubPath = true;
		}
	}

	return true; // Select 툴로 전환 신호
}

bool PenTool::OnRightClicked(CM2DrawDoc* pDoc)
{
	if (!pDoc) return true;

	if (pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->GetPaths().empty())
	{
		auto& nodes = pDoc->GetCurrentPath()->GetPathsRef().back().nodes;
		if (nodes.size() >= 2)
		{
			m_state = State::Idle;
			m_needNewSubPath = true;
			pDoc->GetCurrentPath()->Invalidate();
			CommitPenPath(pDoc);
			pDoc->GetCurrentPath() = nullptr;
			LogAppEvent(_T("[펜 도구] 패스 작성 완료 (우클릭 -> 선택 도구 전환)"));
		}
		else
		{
			pDoc->GetCurrentPath() = nullptr;
			m_state = State::Idle;
			m_needNewSubPath = true;
		}
	}

	return true; // Select 툴로 전환 신호
}

void PenTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc)
{
	if (!rt || !brush) return;
	ViewportTransform view = { zoom, { pan.x, pan.y } };

	// 1. 패스가 이미 시작된 상태인 경우
	if (pDoc && pDoc->GetCurrentPath() && !pDoc->GetCurrentPath()->GetPaths().empty())
	{
		const auto& path = pDoc->GetCurrentPath();

		// 기존 패스 본체 실시간 렌더링
		ID2D1Factory* pFactory = nullptr;
		rt->GetFactory(&pFactory);
		ge::D2DRenderer renderer;
		if (renderer.Initialize(rt, pFactory))
		{
			renderer.DrawNode(*path, ge::Matrix2D::Identity(), view);
		}
		if (pFactory) pFactory->Release();

		// 모든 앵커 및 핸들 제어선 시각화
		D2D1_POINT_2F lastAnchorLocal = { 0.0f, 0.0f };
		D2D1_POINT_2F lastOutLocal = { 0.0f, 0.0f };
		bool hasLastOut = false;
		bool hasNodes = false;

		for (const auto& sp : path->GetPaths())
		{
			for (const auto& node : sp.nodes)
			{
				D2D1_POINT_2F aScreen = CoordinateSpace::LocalToScreen(node.anchor, path->GetTransform(), view);

				// 핸들 In 선 및 조절점
				if (node.HasIn())
				{
					D2D1_POINT_2F inScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), path->GetTransform(), view);
					brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.8f));
					rt->DrawLine(aScreen, inScreen, brush, 1.0f);
					brush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue));
					rt->FillEllipse(D2D1::Ellipse(inScreen, 3.0f, 3.0f), brush);
				}

				// 핸들 Out 선 및 조절점
				if (node.HasOut())
				{
					D2D1_POINT_2F outScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), path->GetTransform(), view);
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
			D2D1_POINT_2F pLastScreen = CoordinateSpace::LocalToScreen(lastAnchorLocal, path->GetTransform(), view);
			D2D1_POINT_2F pNextScreen = view.WorldToScreen(m_hoverWorld);

			brush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue));
			if (hasLastOut)
			{
				D2D1_POINT_2F pOutScreen = CoordinateSpace::LocalToScreen(lastOutLocal, path->GetTransform(), view);
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

// ---------------- 텍스트 입력 대화상자 / TextTool ----------------
namespace {

// 메모리 안에 대화상자 템플릿을 만들어 쓰는 입력창 (리소스 파일을 건드리지 않음)
// 컨트롤: 내용(1001), 글꼴 콤보(1002), 크기(1003), 굵게(1004), 기울임(1005), 정렬 콤보(1006)
class CTextEditDlg : public CDialog
{
public:
	TextEditProps m_props;

	INT_PTR Run(CWnd* parent)
	{
		std::vector<WORD> w;
		auto add32 = [&](DWORD v) { w.push_back(LOWORD(v)); w.push_back(HIWORD(v)); };
		auto addStr = [&](const wchar_t* s) { while (*s) w.push_back(*s++); w.push_back(0); };
		auto align = [&]() { if (w.size() % 2) w.push_back(0); };

		add32(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
		add32(0);
		w.push_back(10);                                  // 항목 수
		w.push_back(0); w.push_back(0); w.push_back(260); w.push_back(150);
		w.push_back(0); w.push_back(0);                   // 메뉴/클래스 없음
		addStr(L"텍스트 입력");
		w.push_back(9); addStr(L"Segoe UI");              // 글꼴

		auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t* title) {
			align();
			add32(style | WS_CHILD | WS_VISIBLE);
			add32(0);
			w.push_back(x); w.push_back(y); w.push_back(cx); w.push_back(cy);
			w.push_back(id);
			w.push_back(0xFFFF); w.push_back(atom);       // 사전 정의 클래스 (0x0080 Button, 0x0081 Edit, 0x0082 Static, 0x0085 ComboBox)
			addStr(title);
			w.push_back(0);                               // 생성 데이터 없음
		};
		item(WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 6, 6, 248, 70, 1001, 0x0081, L"");
		item(SS_LEFT, 6, 84, 30, 10, 0xFFFF, 0x0082, L"글꼴");
		item(CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 40, 82, 120, 120, 1002, 0x0085, L"");
		item(SS_LEFT, 168, 84, 24, 10, 0xFFFF, 0x0082, L"크기");
		item(WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 194, 82, 40, 12, 1003, 0x0081, L"");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 6, 102, 40, 12, 1004, 0x0080, L"굵게");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 52, 102, 46, 12, 1005, 0x0080, L"기울임");
		item(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 104, 100, 56, 60, 1006, 0x0085, L"");
		item(BS_DEFPUSHBUTTON | WS_TABSTOP, 146, 128, 50, 14, IDOK, 0x0080, L"확인");
		item(BS_PUSHBUTTON | WS_TABSTOP, 202, 128, 50, 14, IDCANCEL, 0x0080, L"취소");

		if (!InitModalIndirect(reinterpret_cast<LPCDLGTEMPLATE>(w.data()), parent)) return IDCANCEL;
		return DoModal();
	}

protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		CString s(m_props.text.c_str());
		s.Replace(_T("\n"), _T("\r\n"));
		SetDlgItemText(1001, s);

		if (CComboBox* fc = static_cast<CComboBox*>(GetDlgItem(1002)))
		{
			static const wchar_t* kFonts[] = { L"Segoe UI", L"Malgun Gothic", L"맑은 고딕", L"Arial", L"Times New Roman", L"Courier New", L"Consolas",
				L"Georgia", L"Verdana", L"Tahoma", L"Batang", L"Gulim", L"Dotum", L"Gungsuh", L"Impact", L"Comic Sans MS" };
			for (const wchar_t* f : kFonts) fc->AddString(f);
			fc->SetWindowText(m_props.family.c_str());
		}
		CString sz; sz.Format(_T("%g"), m_props.size);
		SetDlgItemText(1003, sz);
		CheckDlgButton(1004, m_props.bold ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(1005, m_props.italic ? BST_CHECKED : BST_UNCHECKED);
		if (CComboBox* ac = static_cast<CComboBox*>(GetDlgItem(1006)))
		{
			ac->AddString(_T("왼쪽")); ac->AddString(_T("가운데")); ac->AddString(_T("오른쪽"));
			ac->SetCurSel((std::max)(0, (std::min)(2, m_props.align)));
		}
		if (CEdit* e = static_cast<CEdit*>(GetDlgItem(1001))) { e->SetFocus(); e->SetSel(0, -1); }
		return FALSE;   // 포커스를 직접 지정
	}
	void OnOK() override
	{
		CString s;
		GetDlgItemText(1001, s);
		s.Replace(_T("\r\n"), _T("\n"));
		m_props.text = (LPCTSTR)s;
		GetDlgItemText(1002, s);
		s.Trim();
		if (!s.IsEmpty()) m_props.family = (LPCTSTR)s;
		GetDlgItemText(1003, s);
		const float v = static_cast<float>(_tstof(s));
		if (v >= 1.0f && v <= 2000.0f) m_props.size = v;
		m_props.bold = IsDlgButtonChecked(1004) == BST_CHECKED;
		m_props.italic = IsDlgButtonChecked(1005) == BST_CHECKED;
		if (CComboBox* ac = static_cast<CComboBox*>(GetDlgItem(1006))) m_props.align = (std::max)(0, ac->GetCurSel());
		CDialog::OnOK();
	}
};

} // namespace

bool EditTextPropsDialog(CWnd* parent, TextEditProps& props)
{
	CTextEditDlg dlg;
	dlg.m_props = props;
	if (dlg.Run(parent) != IDOK) return false;
	props = dlg.m_props;
	return true;
}

bool EditTextDialog(CWnd* parent, std::wstring& text)
{
	TextEditProps p;
	p.text = text;
	if (!EditTextPropsDialog(parent, p)) return false;
	text = p.text;
	return true;
}

void TextTool::OnDown(const ToolEvent& e)
{
	m_drag = true;
	m_startW = m_curW = e.worldPt;
	m_startS = m_curS = e.screenPt;
}

void TextTool::OnMove(const ToolEvent& e)
{
	if (!m_drag) return;
	m_curW = e.worldPt;
	m_curS = e.screenPt;
}

void TextTool::DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float, D2D1_POINT_2F, CM2DrawDoc*)
{
	if (!m_drag || !rt || !brush) return;
	if (abs(m_curS.x - m_startS.x) < 6 && abs(m_curS.y - m_startS.y) < 6) return;   // 클릭(점 텍스트)은 미리보기 없음
	const D2D1_RECT_F rc = D2D1::RectF(static_cast<float>((std::min)(m_startS.x, m_curS.x)), static_cast<float>((std::min)(m_startS.y, m_curS.y)),
		static_cast<float>((std::max)(m_startS.x, m_curS.x)), static_cast<float>((std::max)(m_startS.y, m_curS.y)));
	brush->SetColor(D2D1::ColorF(0.20f, 0.45f, 0.85f, 1.0f));
	rt->DrawRectangle(rc, brush, 1.0f);
}

void TextTool::OnUp(const ToolEvent& e)
{
	if (!m_drag) return;
	m_drag = false;
	m_curW = e.worldPt;
	if (!e.pDoc) return;

	// 일정 거리 이상 끌었으면 영역 텍스트 (폭 = 드래그 폭), 아니면 클릭 위치의 점 텍스트
	const bool area = abs(e.screenPt.x - m_startS.x) >= 6 && abs(e.screenPt.y - m_startS.y) >= 6;
	const float ax = (std::min)(m_startW.x, m_curW.x), ay = (std::min)(m_startW.y, m_curW.y);
	const float aw = std::fabs(m_curW.x - m_startW.x);

	std::wstring family = (LPCTSTR)AfxGetApp()->GetProfileString(_T("Text"), _T("Family"), _T("Segoe UI"));
	float size = static_cast<float>(AfxGetApp()->GetProfileInt(_T("Text"), _T("Size"), 24));

	auto tn = std::make_unique<ge::TextNode>();
	tn->SetTransform(ge::Matrix2D::Translation(area ? ax : m_startW.x, area ? ay : m_startW.y));
	tn->SetText(L"텍스트");
	tn->SetFontFamily(family);
	tn->SetFontSize(size);
	if (area) tn->SetBoxWidth(aw);
	ge::PathOp::MeasureText(*tn);
	tn->SetName(L"텍스트");

	ge::Node* added = e.pDoc->GetDoc().AddShape(std::move(tn));
	if (!added) return;

	e.pDoc->GetDoc().SetSelection({ added });
	e.pDoc->SetModifiedFlag(TRUE);
	e.pDoc->CommitUndoPoint(area ? L"영역 텍스트 추가" : L"텍스트 추가");
	LogAppEvent(area ? _T("[텍스트 도구] 영역 텍스트 추가") : _T("[텍스트 도구] 텍스트 추가"));

	CM2DrawDoc* pDoc = e.pDoc;
	GetApp()->GetToolManager().SetActiveTool(ToolType::Select);
	if (SelectTool* sel = GetApp()->GetToolManager().GetSelectTool()) sel->SelectNode(added, pDoc);
	pDoc->UpdateAllViews(nullptr);

	if (e.pView && e.pView->IsKindOf(RUNTIME_CLASS(CM2DrawView)))
	{
		static_cast<CM2DrawView*>(e.pView)->BeginTextInlineEdit(static_cast<ge::TextNode*>(added));
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
	m_tools.push_back(std::make_unique<PolylineTool>());
	// BoxShapeTool 단일 클래스 인스턴스 주입: 직선, 사각형, 타원 (2-Point 바운딩)
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Line, ToolType::Line, L"직선 도구 (Line)", L'L'));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Rect, ToolType::Rect, L"사각형 도구 (Rectangle)", L'M'));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Ellipse, ToolType::Ellipse, L"타원 도구 (Ellipse)", L'E'));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Triangle, ToolType::Triangle, L"삼각형 도구 (Triangle)", 0));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Star, ToolType::Star, L"별 도구 (Star)", 0));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Gear, ToolType::Gear, L"기어 도구 (Gear)", 0));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Spiral, ToolType::Spiral, L"스파이럴 도구 (Spiral)", 0));
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::Heart, ToolType::Heart, L"하트 도구 (Heart)", 0));
	// BoxShapeTool 정다각형 (Regular Polygon) 도구 주입
	m_tools.push_back(std::make_unique<BoxShapeTool>(BoxShapeTool::ShapeKind::RegularPoly, ToolType::Polygon, L"정다각형 도구 (Polygon)", L'G'));
	m_tools.push_back(std::make_unique<TextTool>());
	m_tools.push_back(std::make_unique<PaintBucketTool>());
	m_tools.push_back(std::make_unique<FrameTool>());
	m_tools.push_back(std::make_unique<ZoomInTool>());
	m_tools.push_back(std::make_unique<ZoomOutTool>());
	m_tools.push_back(std::make_unique<GradientTool>());
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

GradientTool* ToolManager::GetGradientTool()
{
	for (auto& tool : m_tools)
	{
		if (tool->GetType() == ToolType::Gradient)
			return dynamic_cast<GradientTool*>(tool.get());
	}
	return nullptr;
}
