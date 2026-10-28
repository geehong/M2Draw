#pragma once
#include "D2DGraphicEngine.h"
#include "UndoManager.h"

using ShapeKind = ge::ShapeKind;
using RectParams = ge::RectParams;
using TriangleParams = ge::TriangleParams;
using EllipseParams = ge::EllipseParams;
using PolygonParams = ge::PolygonParams;
using StarParams = ge::StarParams;
using GearParams = ge::GearParams;
using SpiralParams = ge::SpiralParams;
using HeartParams = ge::HeartParams;
using ShapeParams = ge::ShapeParams;
using BezierNode = ge::BezierNode;
using SubPath = ge::SubPath;

class CM2DrawDoc : public CDocument
{
protected:
	CM2DrawDoc() noexcept;
	DECLARE_DYNCREATE(CM2DrawDoc)

// 특성입니다.
public:
	// --- 신형 그래픽 엔진 (D2DGraphicEngine) Scene 루트 ---
	ge::Document                    m_doc;
	std::unique_ptr<ge::PathNode>   m_currentPathNode;

	ge::Document&                   GetDoc()             { return m_doc; }
	const ge::Document&             GetDoc() const       { return m_doc; }
	std::unique_ptr<ge::PathNode>&  GetCurrentPathNode() { return m_currentPathNode; }

	// 선택의 단일 출처는 ge::Document::GetSelection(). 대표(첫 번째) 선택 노드, 없으면 nullptr
	ge::Node* GetPrimarySelection() const;
	// 숨김/잠금(조상 포함) 상태가 된 노드를 선택에서 제외. 선택이 바뀌었으면 true (패널 갱신은 호출 측)
	bool PruneNonInteractiveSelection();

	// 펜 도구가 작성 중인 패스 (문서에 아직 등록되지 않은 임시 도형)
	std::unique_ptr<ge::PathNode>          m_currentPath;
	std::unique_ptr<ge::PathNode>&          GetCurrentPath() { return m_currentPath; }

	// --- 스마트 삭제 연동 기능 (키보드 Del 키 & 레이어 패널 휴지통 공용) ---
	bool HasSelectedObjects() const;
	bool DeleteSelectedObjects();
	bool DeleteActiveLayer();
	bool PerformSmartDelete();

	// --- 복제 (Ctrl+D) ---
	bool DuplicateSelectedObjects();

	// --- 그룹화 / 그룹 해제 (Ctrl+G / Ctrl+Shift+G, 오브젝트 메뉴, 속성창 버튼 공용) ---
	bool CanGroup() const   { return m_doc.CanGroupSelection(); }
	bool CanUngroup() const { return m_doc.CanUngroupSelection(); }
	bool GroupSelectedObjects();
	bool UngroupSelectedObjects();

	// --- 순서(Z-order) 변경 (오브젝트 메뉴 / Ctrl+[ ] / 도구상자 공용). 각 노드는 자기 부모 안에서 이동 ---
	bool CanArrange() const { return m_doc.CanArrangeSelection(); }
	bool ArrangeSelectedObjects(ge::Document::ZOrder order);

	// --- 불리언 연산 / 패스파인더 (오브젝트 메뉴 / 패스파인더 단축키) ---
	bool CanBoolean() const { return m_doc.CanBooleanOperation(); }
	bool PerformBooleanOperation(ge::BooleanMode mode);

	// --- 클리핑 마스크 (Ctrl+7 / Ctrl+Alt+7) ---
	bool CanMakeClipMask() const    { return m_doc.CanMakeClipMask(); }
	bool CanReleaseClipMask() const { return m_doc.CanReleaseClipMask(); }
	bool MakeClipMask();
	bool ReleaseClipMask();

	// --- 패스 연산 (Step 2.6: Outline, Simplify, Reverse, Join) ---
	bool CanOutlineStroke() const { return m_doc.CanOutlineStroke(); }
	bool CanSimplify() const      { return m_doc.CanSimplify(); }
	bool CanReversePath() const   { return m_doc.CanReversePath(); }
	bool CanJoinPaths() const     { return m_doc.CanJoinPaths(); }
	bool OutlineStroke(float widthOverride = -1.0f);
	bool SimplifyPath(float tolerance = 2.0f);
	bool ReversePath();
	bool JoinPaths();

	// --- Undo / Redo (Ctrl+Z / Ctrl+Y) ---
	ge::UndoManager& GetUndoManager() { return m_undoMgr; }
	// 편집이 끝난 지점에서 호출: 마지막 확정 상태와 달라졌으면 Undo 이력에 기록하고 수정 표시를 켬 (true = 변경됨)
	bool CommitUndoPoint(const std::wstring& actionName);
	// 문서 교체(Undo/Redo/열기/새 문서) 후 선택 도구의 진행 중 상태(노드 포인터) 폐기
	void ResetToolInteraction();

	afx_msg void OnEditUndo();
	afx_msg void OnUpdateEditUndo(CCmdUI* pCmdUI);
	afx_msg void OnEditRedo();
	afx_msg void OnUpdateEditRedo(CCmdUI* pCmdUI);
	afx_msg void OnObjectGroup();
	afx_msg void OnUpdateObjectGroup(CCmdUI* pCmdUI);
	afx_msg void OnObjectUngroup();
	afx_msg void OnUpdateObjectUngroup(CCmdUI* pCmdUI);
	afx_msg void OnObjectArrange(UINT nID);
	afx_msg void OnAlignCommand(UINT nID);
	afx_msg void OnUpdateAlignCommand(CCmdUI* pCmdUI);
	afx_msg void OnUpdateObjectArrange(CCmdUI* pCmdUI);
	afx_msg void OnBooleanOperation(UINT nID);
	afx_msg void OnUpdateBooleanOperation(CCmdUI* pCmdUI);
	afx_msg void OnObjectClipMask();
	afx_msg void OnUpdateObjectClipMask(CCmdUI* pCmdUI);
	afx_msg void OnObjectReleaseMask();
	afx_msg void OnUpdateObjectReleaseMask(CCmdUI* pCmdUI);
	afx_msg void OnPathOutline();
	afx_msg void OnUpdatePathOutline(CCmdUI* pCmdUI);
	afx_msg void OnPathSimplify();
	afx_msg void OnUpdatePathSimplify(CCmdUI* pCmdUI);
	afx_msg void OnPathReverse();
	afx_msg void OnUpdatePathReverse(CCmdUI* pCmdUI);
	afx_msg void OnPathJoin();
	afx_msg void OnUpdatePathJoin(CCmdUI* pCmdUI);

private:
	ge::UndoManager m_undoMgr;

// 재정의입니다.
public:
	virtual BOOL OnNewDocument();
	virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
	virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
	virtual BOOL DoSave(LPCTSTR lpszPathName, BOOL bReplace = TRUE);
	virtual void SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU = TRUE);
	void SetModifiedFlag(BOOL bModified = TRUE);   // CDocument::SetModifiedFlag 는 가상 함수가 아니므로 숨김(hiding) 임에 주의
	void UpdateTitleWithModified();
	virtual void Serialize(CArchive& ar);
#ifdef SHARED_HANDLERS
	virtual void InitializeSearchContent();
	virtual void OnDrawThumbnail(CDC& dc, LPRECT lprcBounds);
#endif

// 구현입니다.
public:
	virtual ~CM2DrawDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	DECLARE_MESSAGE_MAP()

#ifdef SHARED_HANDLERS
	void SetSearchContent(const CString& value);
#endif
};
