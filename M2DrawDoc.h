#pragma once
#include "D2DGraphicEngine.h"

using Shape = ge::Node;
using PathShape = ge::PathNode;
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
	std::unique_ptr<PathShape>          m_currentPath;
	std::unique_ptr<PathShape>&          GetCurrentPath() { return m_currentPath; }

	// --- 스마트 삭제 연동 기능 (키보드 Del 키 & 레이어 패널 휴지통 공용) ---
	bool HasSelectedObjects() const;
	bool DeleteSelectedObjects();
	bool DeleteActiveLayer();
	bool PerformSmartDelete();

	// --- 복제 (Ctrl+D) ---
	bool DuplicateSelectedObjects();

// 재정의입니다.
public:
	virtual BOOL OnNewDocument();
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
