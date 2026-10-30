#include "pch.h"
#include "UndoManager.h"
#include "D2DGraphicEngine.h"
#include "SvgWriter.h"
#include "SvgReader.h"

namespace ge {

namespace {
// 원자 컴포넌트(Button / 표준 UI 컴포넌트) 내부 자식이 선택 목록에 들어온 경우 컴포넌트 루트로 승격
const Node* PromoteToAtomicRootConst(const Node* n) {
	if (!n) return nullptr;
	const Node* res = n;
	for (const GroupNode* p = n->GetParent(); p; p = p->GetParent()) {
		if (IsAtomicComponent(p)) res = p;
	}
	return res;
}

Node* PromoteToAtomicRoot(Node* n) {
	if (!n) return nullptr;
	Node* res = n;
	for (GroupNode* p = n->GetParent(); p; p = p->GetParent()) {
		if (IsAtomicComponent(p)) res = p;
	}
	return res;
}
} // anonymous namespace

std::vector<uint64_t> UndoManager::SelectionIds(const Document& doc) {
	std::vector<uint64_t> ids;
	for (const auto* node : doc.GetSelection()) {
		const Node* target = PromoteToAtomicRootConst(node);
		if (target && std::find(ids.begin(), ids.end(), target->GetId()) == ids.end()) {
			ids.push_back(target->GetId());
		}
	}
	return ids;
}

UndoManager::DocMeta UndoManager::CaptureMeta(const Document& doc) {
	return DocMeta{
		doc.width, doc.height,
		doc.backgroundColor.r, doc.backgroundColor.g, doc.backgroundColor.b, doc.backgroundColor.a,
		doc.canvasColor.r, doc.canvasColor.g, doc.canvasColor.b, doc.canvasColor.a,
		static_cast<float>(doc.originMode)
	};
}

std::string UndoManager::SerializeDoc(Document& doc) {
	++m_serializeCount;
	SvgWriter writer;
	// Snapshot: 이미지는 리소스 참조 ID 만 기록 (Undo 스택 50개에 이미지 데이터가 중복 보관되지 않도록)
	return writer.WriteString(doc, SvgWriteOptions(SvgWriteMode::Snapshot));
}

void UndoManager::SetBaseline(Document& doc, std::string svg) {
	m_baselineSvg = std::move(svg);
	m_baselineCounter = ChangeTracker::Value();
	m_baselineMeta = CaptureMeta(doc);
	m_hasBaseline = true;
}

void UndoManager::Trim(std::vector<DocumentSnapshot>& stack) const {
	while (stack.size() > m_maxHistory) {
		stack.erase(stack.begin());
	}
}

void UndoManager::Reset(Document& doc) {
	m_undoStack.clear();
	m_redoStack.clear();
	SetBaseline(doc, SerializeDoc(doc));
}

bool UndoManager::RestoreSnapshot(const DocumentSnapshot& snapshot, Document& doc) {
	// 임시 문서로 먼저 파싱 -> 실패하면 현재 문서를 그대로 유지
	Document restored;
	SvgReader reader;
	if (!reader.ReadFromString(snapshot.svgData, restored)) {
		return false;
	}
	const NodeId prevMaxId = doc.PeekMaxId();
	doc = std::move(restored);
	doc.AdvanceMaxId(prevMaxId);   // Undo/Redo 이후에도 ID 카운터는 단조 증가 유지 (재빌드된 내부 자식과 이전 ID 충돌 방지)
	ChangeTracker::Bump();   // 문서가 통째로 교체됨

	const int layerCount = static_cast<int>(doc.GetLayers().size());
	doc.SetActiveLayer(layerCount > 0 ? (std::min)((std::max)(snapshot.activeLayer, 0), layerCount - 1) : 0);

	std::vector<Node*> restoredSel;
	for (uint64_t id : snapshot.selectionIds) {
		if (Node* n = doc.FindById(id)) {
			Node* target = PromoteToAtomicRoot(n);
			if (target && std::find(restoredSel.begin(), restoredSel.end(), target) == restoredSel.end()) {
				restoredSel.push_back(target);
			}
		}
	}
	doc.SetSelection(restoredSel);
	return true;
}

bool UndoManager::CommitChange(const std::wstring& actionName, Document& doc) {
	// 빠른 경로: 모델 변경 카운터와 문서 메타가 기준 시점과 같으면 아무것도 바뀌지 않음 -> 직렬화 생략
	if (m_hasBaseline && m_baselineCounter == ChangeTracker::Value() && m_baselineMeta == CaptureMeta(doc)) {
		return false;
	}

	std::string current = SerializeDoc(doc);
	if (!m_hasBaseline) {
		SetBaseline(doc, std::move(current));
		return false;
	}
	if (current == m_baselineSvg) {
		// 카운터만 올라가고 내용은 같음(같은 값으로 setter 호출 등): 기준 시점만 갱신해 다음 호출의 직렬화를 피함
		m_baselineCounter = ChangeTracker::Value();
		m_baselineMeta = CaptureMeta(doc);
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

	SetBaseline(doc, std::move(current));
	return true;
}

bool UndoManager::Undo(Document& doc) {
	if (!CanUndo()) return false;

	const DocumentSnapshot& target = m_undoStack.back();

	// Redo 항목: 현재 상태 + 같은 선택/활성 레이어 힌트
	DocumentSnapshot redoEntry;
	redoEntry.svgData = SerializeDoc(doc);
	redoEntry.selectionIds = target.selectionIds;
	redoEntry.activeLayer = target.activeLayer;
	redoEntry.actionName = target.actionName;

	if (!RestoreSnapshot(target, doc)) {
		return false;
	}

	m_redoStack.push_back(std::move(redoEntry));
	Trim(m_redoStack);
	m_undoStack.pop_back();
	// 복원 결과를 새 기준으로 삼음 (재직렬화해야 Reader 가 정규화한 형태와 이후 비교가 일치)
	SetBaseline(doc, SerializeDoc(doc));
	return true;
}

bool UndoManager::Redo(Document& doc) {
	if (!CanRedo()) return false;

	const DocumentSnapshot& target = m_redoStack.back();

	DocumentSnapshot undoEntry;
	undoEntry.svgData = SerializeDoc(doc);
	undoEntry.selectionIds = target.selectionIds;
	undoEntry.activeLayer = target.activeLayer;
	undoEntry.actionName = target.actionName;

	if (!RestoreSnapshot(target, doc)) {
		return false;
	}

	m_undoStack.push_back(std::move(undoEntry));
	Trim(m_undoStack);
	m_redoStack.pop_back();
	SetBaseline(doc, SerializeDoc(doc));
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
