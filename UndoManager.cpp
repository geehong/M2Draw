#include "pch.h"
#include "UndoManager.h"
#include "D2DGraphicEngine.h"
#include "SvgWriter.h"
#include "SvgReader.h"

namespace ge {

namespace {
std::string Serialize(Document& doc) {
	SvgWriter writer;
	return writer.WriteString(doc);
}
} // namespace

std::vector<uint64_t> UndoManager::SelectionIds(const Document& doc) {
	std::vector<uint64_t> ids;
	for (const auto* node : doc.GetSelection()) {
		if (node) ids.push_back(node->GetId());
	}
	return ids;
}

void UndoManager::Trim(std::vector<DocumentSnapshot>& stack) const {
	while (stack.size() > m_maxHistory) {
		stack.erase(stack.begin());
	}
}

void UndoManager::Reset(Document& doc) {
	m_undoStack.clear();
	m_redoStack.clear();
	m_baselineSvg = Serialize(doc);
	m_hasBaseline = true;
}

bool UndoManager::RestoreSnapshot(const DocumentSnapshot& snapshot, Document& doc) {
	// 임시 문서로 먼저 파싱 -> 실패하면 현재 문서를 그대로 유지
	Document restored;
	SvgReader reader;
	if (!reader.ReadFromString(snapshot.svgData, restored)) {
		return false;
	}
	doc = std::move(restored);

	const int layerCount = static_cast<int>(doc.GetLayers().size());
	doc.SetActiveLayer(layerCount > 0 ? (std::min)((std::max)(snapshot.activeLayer, 0), layerCount - 1) : 0);

	std::vector<Node*> restoredSel;
	for (uint64_t id : snapshot.selectionIds) {
		if (Node* n = doc.FindById(id)) {
			restoredSel.push_back(n);
		}
	}
	doc.SetSelection(restoredSel);
	return true;
}

bool UndoManager::CommitChange(const std::wstring& actionName, Document& doc) {
	std::string current = Serialize(doc);
	if (!m_hasBaseline) {
		m_baselineSvg = std::move(current);
		m_hasBaseline = true;
		return false;
	}
	if (current == m_baselineSvg) {
		return false;
	}

	// 변경 전 상태(기준)를 Undo 항목으로 보관. 선택/활성 레이어는 "변경 후" 값을 힌트로 저장해
	// Undo 와 Redo 모두 변경 대상이 된 도형이 선택되도록 한다.
	DocumentSnapshot entry;
	entry.svgData = std::move(m_baselineSvg);
	entry.selectionIds = SelectionIds(doc);
	entry.activeLayer = doc.GetActiveLayerIndex();
	entry.actionName = actionName;

	m_undoStack.push_back(std::move(entry));
	Trim(m_undoStack);
	m_redoStack.clear();

	m_baselineSvg = std::move(current);
	return true;
}

bool UndoManager::Undo(Document& doc) {
	if (!CanUndo()) return false;

	const DocumentSnapshot& target = m_undoStack.back();

	// Redo 항목: 현재 상태 + 같은 선택/활성 레이어 힌트
	DocumentSnapshot redoEntry;
	redoEntry.svgData = Serialize(doc);
	redoEntry.selectionIds = target.selectionIds;
	redoEntry.activeLayer = target.activeLayer;
	redoEntry.actionName = target.actionName;

	if (!RestoreSnapshot(target, doc)) {
		return false;
	}

	m_redoStack.push_back(std::move(redoEntry));
	Trim(m_redoStack);
	m_undoStack.pop_back();
	m_baselineSvg = Serialize(doc);
	return true;
}

bool UndoManager::Redo(Document& doc) {
	if (!CanRedo()) return false;

	const DocumentSnapshot& target = m_redoStack.back();

	DocumentSnapshot undoEntry;
	undoEntry.svgData = Serialize(doc);
	undoEntry.selectionIds = target.selectionIds;
	undoEntry.activeLayer = target.activeLayer;
	undoEntry.actionName = target.actionName;

	if (!RestoreSnapshot(target, doc)) {
		return false;
	}

	m_undoStack.push_back(std::move(undoEntry));
	Trim(m_undoStack);
	m_redoStack.pop_back();
	m_baselineSvg = Serialize(doc);
	return true;
}

static const std::wstring s_emptyName;

const std::wstring& UndoManager::GetLastUndoActionName() const {
	return m_undoStack.empty() ? s_emptyName : m_undoStack.back().actionName;
}

const std::wstring& UndoManager::GetLastRedoActionName() const {
	return m_redoStack.empty() ? s_emptyName : m_redoStack.back().actionName;
}

} // namespace ge
