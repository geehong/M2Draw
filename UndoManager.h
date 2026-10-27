#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace ge {

class Document;

// 문서 스냅샷 (SVG 직렬화 데이터 + Undo/Redo 후 복원할 선택/활성 레이어)
struct DocumentSnapshot {
	std::string           svgData;
	std::vector<uint64_t> selectionIds;   // 복원 후 선택할 노드 ID (NodeId 는 SVG 에 보존되므로 안정적)
	int                   activeLayer = 0;
	std::wstring          actionName;
};

// 스냅샷 기반 Undo / Redo 관리자
//
// "기준 상태(baseline) 비교" 방식: 마지막으로 확정한 문서 상태를 보관해 두고, 편집이 끝난 지점마다
// CommitChange() 를 호출하면 기준과 달라졌을 때만 이전 상태를 Undo 스택에 쌓는다.
//  - 편집 "전" 에 호출할 필요가 없어 어떤 경로(캔버스, 속성창, 레이어 패널, 단축키)의 편집이든 같은 방식으로 기록
//  - 바뀐 게 없으면 기록하지 않으므로 선택만 바꾼 클릭이 Undo 단계가 되지 않음
class UndoManager {
public:
	UndoManager() = default;
	~UndoManager() = default;

	void SetMaxHistory(size_t maxCount) { m_maxHistory = maxCount; }

	// 새 문서 / 파일 열기: 이력을 비우고 현재 상태를 기준으로 설정
	void Reset(Document& doc);

	// 편집이 끝난 시점에 호출. 기준 상태와 달라졌으면 기록하고 true 반환
	bool CommitChange(const std::wstring& actionName, Document& doc);

	bool CanUndo() const { return !m_undoStack.empty(); }
	bool CanRedo() const { return !m_redoStack.empty(); }

	// 실행 취소 / 다시 실행: 성공 시 true (복원할 SVG 가 깨졌으면 문서를 건드리지 않고 false)
	bool Undo(Document& doc);
	bool Redo(Document& doc);

	const std::wstring& GetLastUndoActionName() const;
	const std::wstring& GetLastRedoActionName() const;

	// 진단/검증용: 지금까지 문서를 실제로 직렬화한 횟수 (변경 카운터가 같을 때는 늘어나지 않아야 함)
	size_t GetSerializeCount() const { return m_serializeCount; }

private:
	static std::vector<uint64_t> SelectionIds(const Document& doc);
	// 변경 카운터로 잡히지 않는 Document 공개 필드(크기/배경/캔버스색/원점) 요약
	using DocMeta = std::array<float, 11>;
	static DocMeta CaptureMeta(const Document& doc);
	// 문서 직렬화 (Snapshot 모드) + 직렬화 횟수 집계
	std::string SerializeDoc(Document& doc);
	// 현재 문서 상태를 기준 시점으로 기록 (직렬화 결과 + 변경 카운터 + 메타)
	void SetBaseline(Document& doc, std::string svg);
	static bool RestoreSnapshot(const DocumentSnapshot& snapshot, Document& doc);
	void Trim(std::vector<DocumentSnapshot>& stack) const;

	std::vector<DocumentSnapshot> m_undoStack;
	std::vector<DocumentSnapshot> m_redoStack;
	std::string                   m_baselineSvg;      // 마지막으로 확정된 문서 상태
	bool                          m_hasBaseline = false;
	uint64_t                      m_baselineCounter = 0;   // 기준 확정 시점의 ChangeTracker 값
	DocMeta                       m_baselineMeta{};
	size_t                        m_serializeCount = 0;
	size_t                        m_maxHistory = 50;
};

} // namespace ge
