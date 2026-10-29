#include "pch.h"
#include "ToolManager.h"
#include "ITool.h"
#include "M2Draw.h"
#include "M2DrawDoc.h"
#include "MainFrm.h"
#include "RegionFill.h"

// 페인트통 도구 (Step 5.6): 클릭한 지점을 둘러싼 닫힌 영역을 찾아 활성 채우기 색의 새 폐곡선 PathNode 로 만든다.
// 영역 탐지는 RegionFill.cpp (래스터 플러드 필 + 윤곽 추적). 도구는 클릭 처리와 문서 반영만 담당한다.
void PaintBucketTool::OnDown(const ToolEvent& e)
{
	if (!e.pDoc) return;
	CM2DrawDoc* pDoc = e.pDoc;

	std::vector<ge::Vec2> poly;
	std::wstring err;
	if (!ge::FindEnclosedRegion(pDoc->GetDoc(), ge::Vec2{ e.worldPt.x, e.worldPt.y }, poly, &err))
	{
		CString msg;
		msg.Format(_T("[페인트 통] %s"), err.c_str());
		LogAppEvent(msg);
		SetAppStatusText(msg);
		return;
	}

	// 활성 채우기 색: 속성창의 현재 채우기 색 (없으면 기본 색)
	D2D1_COLOR_F clr = D2D1::ColorF(0.2f, 0.6f, 1.0f, 1.0f);
	if (CWnd* pMain = AfxGetMainWnd())
		if (pMain->IsKindOf(RUNTIME_CLASS(CMainFrame)))
			clr = static_cast<CMainFrame*>(pMain)->GetPropertiesWnd().GetD2DPropView().GetActiveFillColor();

	auto node = std::make_unique<ge::PathNode>();
	ge::SubPath sp;
	sp.closed = true;
	for (const ge::Vec2& p : poly)
	{
		ge::BezierNode bn;
		bn.anchor = p;
		sp.nodes.push_back(bn);
	}
	node->SetPaths(std::vector<ge::SubPath>{ sp });
	node->SetKind(ge::ShapeKind::Path);
	ge::Style st;
	st.fill.type = ge::PaintType::Solid;
	st.fill.color = ge::Color(clr.r, clr.g, clr.b, clr.a);
	st.stroke.width = 0.0f;
	node->SetStyle(st);
	node->SetName(pDoc->GetDoc().GenerateNodeName(ge::ShapeKind::Path));

	ge::Node* added = pDoc->GetDoc().AddShapeAuto(std::move(node));
	if (!added) return;
	pDoc->GetDoc().SetSelection({ added });
	pDoc->SetModifiedFlag(TRUE);
	pDoc->CommitUndoPoint(L"페인트 통 채우기");
	CString log;
	log.Format(_T("[페인트 통] 영역 채우기 (꼭짓점 %d개, 면적 %.0f)"), static_cast<int>(poly.size()), static_cast<double>(ge::PolygonArea(poly)));
	LogAppEvent(log);
	UpdateShapePropertiesSelection(added, pDoc);
	pDoc->UpdateAllViews(nullptr);
}
