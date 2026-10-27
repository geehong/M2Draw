// GeModelPureCheck.cpp
//
// 순수 모델 검증용 TU:
//  1) GE_NO_D2D 를 정의한 상태에서 모델 헤더가 Direct2D / Windows / MFC 의존 없이 컴파일되는지 빌드 시 확인
//  2) RunPureModelVerification() 이 실제로 실행되어 SVG 왕복, NodeId 보존, 보이기/잠금/혼합 모드 보존,
//     Undo/Redo, 깨진 파일 거부를 검증 (Debug 빌드 시작 시 호출)

#define GE_NO_D2D
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"
#include "GeNodeVisitor.h"
#include "SvgWriter.h"
#include "SvgReader.h"
#include "UndoManager.h"
#include "GeModelPureCheck.h"

namespace ge {
namespace test {

namespace {

// Visitor 패턴 동작 검증용 카운팅 방문자
class CountingVisitor : public INodeVisitor {
public:
	int pathCount = 0;
	int groupCount = 0;
	int layerCount = 0;

	void Visit(PathNode& /*node*/) override { ++pathCount; }
	void Visit(GroupNode& node) override {
		++groupCount;
		for (const auto& child : node.GetChildren()) {
			if (child) child->Accept(*this);
		}
	}
	void Visit(LayerNode& node) override {
		++layerCount;
		for (const auto& child : node.GetChildren()) {
			if (child) child->Accept(*this);
		}
	}
};

int CountPaths(Document& doc) {
	CountingVisitor v;
	for (const auto& layer : doc.GetLayers()) {
		if (layer) layer->Accept(v);
	}
	return v.pathCount;
}

std::unique_ptr<PathNode> MakePath(const wchar_t* name, float x, float y) {
	auto p = std::make_unique<PathNode>();
	p->SetName(name);
	p->AddAnchor(Vec2{ x, y });
	p->AddAnchor(Vec2{ x + 90.0f, y + 180.0f });
	p->AddAnchor(Vec2{ x + 90.0f, y });
	p->GetPathsRef()[0].closed = true;
	return p;
}

bool NearByte(float a, float b) { return std::abs(a - b) <= 1.0f / 255.0f; }

} // namespace

bool RunPureModelVerification(std::wstring* failure)
{
#define GE_CHECK(cond, text) do { if (!(cond)) { if (failure) *failure = (text); return false; } } while (0)

	// ---- 1. 모델 구성: 파라메트릭 도형 + 보이기/잠금/혼합 모드 -------------------------------
	Document doc;
	LayerNode* layer = doc.GetActiveLayer();
	GE_CHECK(layer != nullptr, L"기본 활성 레이어가 없음");

	auto rect = MakePath(L"rect1", 10.0f, 20.0f);
	rect->SetShapeKind(ShapeKind::Rect);
	rect->SetParams(RectParams{ 5.0f, 5.0f, true });
	rect->SetVisible(false);
	rect->SetLocked(true);
	rect->SetBlendMode(LayerBlendMode::Multiply);
	Node* rectNode = doc.AddShape(std::move(rect));
	GE_CHECK(rectNode != nullptr && rectNode->GetId() != 0, L"AddShape 가 ID 를 발급하지 못함");
	const NodeId rectId = rectNode->GetId();
	const NodeId layerId = layer->GetId();

	Matrix2D mat = Matrix2D::Translation(5.0f, 5.0f);
	Vec2 pt = mat.TransformPoint(Vec2{ 1.0f, 2.0f });
	GE_CHECK(pt.x == 6.0f && pt.y == 7.0f, L"Matrix2D 변환 결과가 틀림");

	CountingVisitor counter;
	layer->Accept(counter);
	GE_CHECK(counter.layerCount == 1 && counter.pathCount == 1 && counter.groupCount == 0, L"Visitor 순회 결과가 틀림");

	// ---- 2. SVG 왕복: ID / 상태 / 파라메트릭 보존 ---------------------------------------------
	SvgWriter writer;
	const std::string svg = writer.WriteString(doc);

	SvgReader reader;
	Document doc2;
	GE_CHECK(reader.ReadFromString(svg, doc2), L"자체 출력 SVG 를 다시 읽지 못함");
	GE_CHECK(doc2.FindById(layerId) != nullptr, L"레이어 NodeId 가 보존되지 않음");
	Node* r2 = doc2.FindById(rectId);
	GE_CHECK(r2 != nullptr, L"도형 NodeId 가 보존되지 않음");
	GE_CHECK(r2->GetName() == L"rect1", L"도형 이름이 보존되지 않음");
	GE_CHECK(!r2->IsVisible(), L"도형 숨김 상태가 보존되지 않음");
	GE_CHECK(r2->IsLocked(), L"도형 잠금 상태가 보존되지 않음");
	GE_CHECK(r2->GetBlendMode() == LayerBlendMode::Multiply, L"혼합 모드가 보존되지 않음");
	GE_CHECK(static_cast<PathNode*>(r2)->GetShapeKind() == ShapeKind::Rect, L"파라메트릭 도형 종류가 보존되지 않음");
	GE_CHECK(doc2.IssueId() > rectId, L"복원 후 ID 발급 카운터가 기존 ID 와 충돌할 수 있음");
	GE_CHECK(writer.WriteString(doc2) == svg, L"읽고 다시 쓴 SVG 가 원본과 다름 (직렬화가 안정적이지 않음)");

	// ---- 3. Undo / Redo ------------------------------------------------------------------------
	UndoManager undo;
	undo.Reset(doc2);
	GE_CHECK(!undo.CommitChange(L"변경 없음", doc2), L"변경이 없는데 Undo 이력이 기록됨");

	Node* extra = doc2.AddShape(MakePath(L"extra", 200.0f, 50.0f));
	GE_CHECK(extra != nullptr, L"도형 추가 실패");
	doc2.SetSelection({ extra });
	const NodeId extraId = extra->GetId();
	GE_CHECK(CountPaths(doc2) == 2, L"도형 추가 후 개수가 틀림");
	GE_CHECK(undo.CommitChange(L"도형 추가", doc2), L"변경이 있는데 Undo 이력이 기록되지 않음");
	GE_CHECK(!undo.CommitChange(L"도형 추가", doc2), L"같은 상태가 두 번 기록됨");
	GE_CHECK(undo.CanUndo() && !undo.CanRedo(), L"Undo/Redo 가능 상태가 틀림");

	GE_CHECK(undo.Undo(doc2), L"Undo 실패");
	GE_CHECK(CountPaths(doc2) == 1, L"Undo 후 도형 개수가 틀림");
	GE_CHECK(doc2.FindById(extraId) == nullptr, L"Undo 후에도 추가된 도형이 남아 있음");
	Node* r3 = doc2.FindById(rectId);
	GE_CHECK(r3 != nullptr, L"Undo 후 기존 도형의 NodeId 가 바뀜");
	GE_CHECK(!r3->IsVisible() && r3->IsLocked(), L"Undo 후 숨김/잠금 상태가 사라짐");
	GE_CHECK(undo.CanRedo(), L"Undo 후 Redo 가 불가능");

	GE_CHECK(undo.Redo(doc2), L"Redo 실패");
	GE_CHECK(CountPaths(doc2) == 2, L"Redo 후 도형 개수가 틀림");
	Node* e2 = doc2.FindById(extraId);
	GE_CHECK(e2 != nullptr, L"Redo 후 도형의 NodeId 가 바뀜");
	GE_CHECK(doc2.GetSelection().size() == 1 && doc2.GetSelection()[0] == e2, L"Redo 후 선택이 복원되지 않음");

	Node* another = doc2.AddShape(MakePath(L"another", 300.0f, 80.0f));
	GE_CHECK(another != nullptr && another->GetId() > extraId, L"Undo/Redo 이후 새 도형의 NodeId 가 기존과 겹칠 수 있음");

	// ---- 4. 깨진 파일 / SVG 가 아닌 문서는 거부 -----------------------------------------------
	Document scratch;
	GE_CHECK(!reader.ReadFromString("<svg xmlns=\"http://www.w3.org/2000/svg\"><g></svg>", scratch), L"깨진 XML 을 성공으로 처리함");
	GE_CHECK(!reader.ReadFromString("<html/>", scratch), L"SVG 가 아닌 문서를 성공으로 처리함");

	// ---- 6. 좌표계 변환 왕복 및 BottomLeft 레거시 호환 검증 -------------------
	Viewport vp{ 2.5f, Vec2{ 150.0f, -50.0f } };
	Vec2 worldOrig{ 123.4f, 567.8f };
	Vec2 screenPt = vp.WorldToScreen(worldOrig);
	Vec2 roundTripWorld = vp.ScreenToWorld(screenPt);
	GE_CHECK(std::abs(roundTripWorld.x - worldOrig.x) < 0.001f && std::abs(roundTripWorld.y - worldOrig.y) < 0.001f, L"Viewport ScreenToWorld/WorldToScreen 왕복 오차 발생");

	// 구버전(m2:version 없는) bottomLeft 파일 로드 시 y' = H - y 변환 검증
	const std::string legacySvg =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:m2=\"https://m2draw/ns\" "
		"width=\"1000\" height=\"800\" viewBox=\"0 0 1000 800\" m2:originMode=\"bottomLeft\">\n"
		"  <path d=\"M 100 200 L 300 200\" />\n"
		"</svg>\n";
	Document docLegacy;
	GE_CHECK(reader.ReadFromString(legacySvg, docLegacy), L"레거시 bottomLeft SVG 읽기 실패");
	GE_CHECK(docLegacy.originMode == CanvasOriginMode::BottomLeft, L"레거시 bottomLeft 모드 인식 실패");
	GE_CHECK(!docLegacy.GetLayers().empty(), L"레거시 SVG 에 레이어가 없음");
	auto* legacyLayer = docLegacy.GetLayers()[0].get();
	GE_CHECK(!legacyLayer->GetChildren().empty(), L"레거시 레이어에 자식이 없음");
	auto* legacyPath = dynamic_cast<PathNode*>(legacyLayer->GetChildren()[0].get());
	GE_CHECK(legacyPath != nullptr && !legacyPath->GetPaths().empty(), L"레거시 패스 노드 검증 실패");
	// 레거시의 y=200 은 H=800 기준 y' = 800 - 200 = 600 이어야 함
	GE_CHECK(std::abs(legacyPath->GetPaths()[0].nodes[0].anchor.y - 600.0f) < 0.01f, L"레거시 bottomLeft y' = H - y 변환 실패");

#undef GE_CHECK
	return true;
}

} // namespace test
} // namespace ge
