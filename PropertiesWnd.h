#pragma once
#include <d2d1.h>

#include "D2DPropertyView.h"

class CM2DrawDoc;
namespace ge { class Node; }

// 우측 속성 도킹 창. 실제 내용은 D2D 속성 뷰(CD2DPropertyView)가 그리며, 옛 속성 목록(CMFCPropertyGridCtrl)은 제거했다 (Step 6.20 리뷰 2단계)
class CPropertiesWnd : public CDockablePane
{
public:
	CPropertiesWnd(CD2DPropertyView::PaneMode mode = CD2DPropertyView::MODE_SHAPE) noexcept;

	void AdjustLayout();
	void SetSelectedShape(ge::Node* pShape, CM2DrawDoc* pDoc);
	// 닫히는 문서가 선택 대상이었으면 선택을 비운다 (옛 문서를 가리키지 않게)
	void ReleaseDocument(const CM2DrawDoc* pDoc) { if (m_pActiveDoc == pDoc) SetSelectedShape(nullptr, nullptr); }
	CD2DPropertyView::PaneMode GetPaneMode() const { return m_paneMode; }
	CD2DPropertyView& GetD2DPropView() { return m_wndD2DProp; }

	virtual ~CPropertiesWnd();

	// 최소/최대 도킹 크기 제약
	void GetMinSize(CSize& size) const override
	{
		size.cx = 240; // 최소 너비 240px
		size.cy = 180; // 최소 높이 180px
	}

protected:
	CD2DPropertyView::PaneMode m_paneMode;
	CD2DPropertyView m_wndD2DProp;

	uint64_t m_selectedNodeId = 0;          // 대상 도형 NodeId (0 = 없음)
	CM2DrawDoc* m_pActiveDoc = nullptr;
	ge::Node* GetSelectedShape() const;

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg LRESULT OnTabChanged(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
};