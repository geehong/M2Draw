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

#include <algorithm>
#include <functional>
#include <unordered_set>

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

	// ---- 7. 변경 카운터 + SvgWriteOptions (2.0.4) -----------------------------------------------
	{
		Document d;
		LayerNode* l = d.GetActiveLayer();
		auto grp = std::make_unique<GroupNode>();
		grp->SetName(L"group1");
		Node* g = d.AddShape(std::move(grp));
		GE_CHECK(g != nullptr, L"그룹 추가 실패");
		GroupNode* gn = static_cast<GroupNode*>(g);
		PathNode* a = static_cast<PathNode*>(gn->AddChild(MakePath(L"a", 0.0f, 0.0f)));
		PathNode* b = static_cast<PathNode*>(gn->AddChild(MakePath(L"b", 50.0f, 0.0f)));
		d.AssignIds(*g);
		a->SetShapeKind(ShapeKind::Rect);
		a->SetParams(RectParams{ 2.0f, 2.0f, true });
		(void)l;

		UndoManager um;
		um.Reset(d);

		// 7-1. 아무것도 바꾸지 않으면 직렬화 없이 "변경 없음"
		const size_t before = um.GetSerializeCount();
		GE_CHECK(!um.CommitChange(L"변경 없음", d), L"변경이 없는데 기록됨(카운터)");
		GE_CHECK(!um.CommitChange(L"변경 없음", d), L"변경이 없는데 기록됨(카운터, 2회)");
		GE_CHECK(um.GetSerializeCount() == before, L"변경 카운터가 같은데 문서를 직렬화함");

		// 7-2. 선택만 바꿔도 직렬화하지 않음 (선택은 문서 내용이 아님)
		d.SetSelection({ a });
		GE_CHECK(!um.CommitChange(L"선택", d) && um.GetSerializeCount() == before, L"선택 변경이 직렬화/기록을 유발함");

		// 7-3. 모든 수정 경로가 카운터를 올려 Undo 로 기록되어야 함 (누락 시 Undo 가 변경을 놓침)
		auto undoable = [&](auto&& mutate) {
			mutate();
			return um.CommitChange(L"수정", d);
		};
		GE_CHECK(undoable([&] { b->SetName(L"renamed"); }), L"SetName 이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->SetTransform(Matrix2D::Translation(7.0f, 9.0f)); }), L"SetTransform 이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->SetVisible(false); }), L"SetVisible 이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->SetLocked(true); }), L"SetLocked 이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->SetOpacity(0.5f); }), L"SetOpacity 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->SetBlendMode(LayerBlendMode::Screen); }), L"SetBlendMode 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->GetStyleRef().stroke.width = 7.0f; }), L"GetStyleRef 수정이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { Style st = b->GetStyle(); st.fill.type = PaintType::Solid; st.fill.color = Color(0.1f, 0.2f, 0.3f, 1.0f); b->SetStyle(st); }), L"SetStyle 이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { b->GetPathsRef()[0].nodes[0].anchor.x += 11.0f; }), L"GetPathsRef 수정이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { a->GetParamRef<RectParams>()->rx = 9.0f; }), L"GetParamRef 수정이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { a->GetParamsRef() = RectParams{ 4.0f, 4.0f, true }; }), L"GetParamsRef 수정이 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { a->SetShapeKind(ShapeKind::Ellipse); a->SetParams(EllipseParams{}); }), L"SetShapeKind/SetParams 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { gn->BringToFront(a); }), L"BringToFront 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { gn->SendBackward(a); }), L"SendBackward 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { auto owned = gn->RemoveChild(b); gn->AddChild(std::move(owned), 0); }), L"RemoveChild/AddChild 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { d.AddShape(MakePath(L"c", 5.0f, 5.0f)); }), L"AddShape 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { d.width = 640.0f; }), L"문서 너비 변경이 기록되지 않음");
		GE_CHECK(undoable([&] { d.backgroundColor = Color(0.5f, 0.5f, 0.5f, 0.5f); }), L"문서 배경색 변경이 기록되지 않음");
		GE_CHECK(undoable([&] { d.originMode = CanvasOriginMode::BottomLeft; }), L"원점 모드 변경이 기록되지 않음");
		LayerNode* nl = nullptr;
		GE_CHECK(undoable([&] { nl = d.AddLayer(L"Layer X"); }), L"AddLayer 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { nl->SetOpacity(0.6f); }), L"레이어 SetOpacity 가 카운터를 올리지 않음");
		GE_CHECK(undoable([&] { d.MoveLayer(nl, 0); }), L"MoveLayer 가 카운터를 올리지 않음");

		// 7-4. 같은 값을 다시 설정하면 카운터는 올라가도 내용이 같으므로 기록하지 않음
		GE_CHECK(!undoable([&] { b->SetOpacity(0.5f); }), L"같은 값 설정이 Undo 로 기록됨");
		GE_CHECK(!um.CommitChange(L"변경 없음", d), L"같은 값 설정 뒤 재호출이 기록됨");

		// 7-5. Undo 후에도 카운터 기반 건너뛰기가 정상 동작 (기준이 복원 결과로 갱신됨)
		GE_CHECK(um.Undo(d), L"Undo 실패");
		const size_t afterUndo = um.GetSerializeCount();
		GE_CHECK(!um.CommitChange(L"변경 없음", d) && um.GetSerializeCount() == afterUndo, L"Undo 직후 불필요한 직렬화/기록 발생");

		// 7-6. SvgWriteOptions: Snapshot/File 은 동일(이미지 노드 도입 전), 기본값은 File
		SvgWriter w;
		const std::string fileSvg = w.WriteString(d, SvgWriteOptions(SvgWriteMode::File));
		GE_CHECK(w.WriteString(d) == fileSvg, L"기본 옵션이 File 모드와 다름");
		GE_CHECK(w.WriteString(d, SvgWriteOptions(SvgWriteMode::Snapshot)) == fileSvg, L"Snapshot 과 File 출력이 (이미지 없이) 다름");
		GE_CHECK(fileSvg.find("m2:") != std::string::npos && fileSvg.find("inkscape:") != std::string::npos, L"File 모드에 M2/Inkscape 메타데이터가 없음");

		// Clean: 순수 SVG (m2:/inkscape:/sodipodi: 전부 없음), 도형/숨김은 표준 속성으로 유지, 다시 읽을 수 있음
		const std::string cleanSvg = w.WriteString(d, SvgWriteOptions(SvgWriteMode::Clean));
		GE_CHECK(cleanSvg.find("m2:") == std::string::npos, L"Clean 출력에 m2: 가 남아 있음");
		GE_CHECK(cleanSvg.find("inkscape") == std::string::npos, L"Clean 출력에 inkscape 가 남아 있음");
		GE_CHECK(cleanSvg.find("sodipodi") == std::string::npos, L"Clean 출력에 sodipodi 가 남아 있음");
		GE_CHECK(cleanSvg.find("<path") != std::string::npos && cleanSvg.find(" d=\"") != std::string::npos, L"Clean 출력에 패스가 없음");
		GE_CHECK(cleanSvg.find("display:none") != std::string::npos, L"Clean 출력에서 숨김 상태가 사라짐");
		Document cleanDoc;
		GE_CHECK(reader.ReadFromString(cleanSvg, cleanDoc), L"Clean SVG 를 다시 읽지 못함");
		GE_CHECK(CountPaths(cleanDoc) == CountPaths(d), L"Clean SVG 왕복 후 도형 개수가 다름");
	}

	// ---- 8. ImportNodes: 가져온 노드의 NodeId 유일성 (2.0.5) --------------------------------------
	{
		// 트리 전체 ID 수집: 0 이거나 중복이면 실패
		auto collectIds = [](Document& d, std::unordered_set<NodeId>& ids) {
			bool ok = true;
			std::function<void(const Node&)> walk = [&](const Node& n) {
				if (n.GetId() == 0 || !ids.insert(n.GetId()).second) ok = false;
				if (n.Kind() != NodeKind::Path)
					for (const auto& c : static_cast<const GroupNode&>(n).GetChildren()) if (c) walk(*c);
			};
			for (const auto& l : d.GetLayers()) if (l) walk(*l);
			return ok;
		};

		// 소스 문서: 레이어 > 그룹 > 패스 2개 (ID 가 1, 2, 3, 4 부근으로 대상 문서와 겹침)
		Document src;
		auto g = std::make_unique<GroupNode>();
		g->SetName(L"srcGroup");
		GroupNode* sg = static_cast<GroupNode*>(src.AddShape(std::move(g)));
		sg->AddChild(MakePath(L"s1", 0.0f, 0.0f));
		sg->AddChild(MakePath(L"s2", 20.0f, 0.0f));
		src.AssignIds(*sg);
		std::unordered_set<NodeId> srcIdsBefore;
		GE_CHECK(collectIds(src, srcIdsBefore), L"소스 문서 ID 가 유일하지 않음(테스트 전제)");

		// 임시 문서로 읽은 것처럼: SVG 로 직렬화 -> 읽기 (Reader 는 NodeId 를 복원하므로 ID 가 그대로 나옴)
		SvgWriter w;
		Document readBack;
		GE_CHECK(reader.ReadFromString(w.WriteString(src), readBack), L"임포트 테스트용 SVG 읽기 실패");
		GE_CHECK(readBack.FindById(sg->GetId()) != nullptr, L"임포트 테스트 전제: 읽은 문서가 원본 ID 를 유지해야 함");

		// 대상 문서에 이미 같은 ID 대역의 노드가 있음 (충돌 상황)
		Document dst;
		dst.AddShape(MakePath(L"d1", 0.0f, 100.0f));
		dst.AddShape(MakePath(L"d2", 30.0f, 100.0f));
		dst.AddShape(MakePath(L"d3", 60.0f, 100.0f));
		std::unordered_set<NodeId> dummy;
		GE_CHECK(collectIds(dst, dummy), L"대상 문서 ID 가 유일하지 않음(테스트 전제)");
		const size_t dstCountBefore = dummy.size();

		// 8-1. 읽은 문서의 그룹을 활성 레이어로 가져오기
		const Node* readGroup = readBack.GetLayers()[0]->GetChildren()[0].get();
		std::vector<Node*> imported = dst.ImportNodes({ readGroup });
		GE_CHECK(imported.size() == 1 && imported[0]->Kind() == NodeKind::Group, L"그룹 가져오기 결과가 틀림");
		GE_CHECK(imported[0]->GetParent() == dst.GetActiveLayer(), L"가져온 그룹이 활성 레이어에 들어가지 않음");
		std::unordered_set<NodeId> dstIds;
		GE_CHECK(collectIds(dst, dstIds), L"가져온 뒤 대상 문서에 중복/0 ID 가 생김");
		GE_CHECK(dstIds.size() == dstCountBefore + 3, L"가져온 노드 수(그룹 + 자식 2)가 틀림");
		GE_CHECK(dst.IssueId() > *std::max_element(dstIds.begin(), dstIds.end()), L"가져온 뒤 ID 발급 카운터가 기존 ID 와 충돌할 수 있음");
		for (const Node* n : { imported[0], static_cast<const GroupNode*>(imported[0])->GetChildren()[0].get() })
			GE_CHECK(dst.FindById(n->GetId()) == n, L"가져온 노드를 ID 로 찾지 못함");
		GE_CHECK(imported[0]->GetName() == L"srcGroup", L"가져온 그룹 이름이 보존되지 않음");

		// 소스는 변경되지 않음 (ID, 소속, 개수)
		std::unordered_set<NodeId> readIds;
		GE_CHECK(collectIds(readBack, readIds) && readIds == srcIdsBefore, L"가져오기가 소스 문서의 ID 를 바꿈");
		GE_CHECK(readGroup->GetParent() == readBack.GetLayers()[0].get(), L"가져오기가 소스 노드를 옮김(복제여야 함)");

		// 8-2. 같은 소스를 한 번 더, 이번에는 위치 지정(index 0) -> 여전히 유일
		GroupNode* layer0 = dst.GetActiveLayer();
		std::vector<Node*> again = dst.ImportNodes({ readGroup, readGroup }, layer0, 0);
		GE_CHECK(again.size() == 2 && layer0->IndexOf(again[0]) == 0 && layer0->IndexOf(again[1]) == 1, L"index 지정 삽입 순서가 틀림");
		std::unordered_set<NodeId> dstIds2;
		GE_CHECK(collectIds(dst, dstIds2) && dstIds2.size() == dstCountBefore + 3 + 6, L"같은 소스를 반복 가져온 뒤 ID 가 유일하지 않음");

		// 8-3. 자기 문서 안에서 가져오기(복제) + target 이 소스의 후손인 경우
		GroupNode* inner = static_cast<GroupNode*>(imported[0]);
		std::vector<Node*> self = dst.ImportNodes({ inner }, inner);
		GE_CHECK(self.size() == 1 && self[0]->GetParent() == inner && self[0] != inner, L"자기 후손 그룹으로의 가져오기가 실패");
		std::unordered_set<NodeId> dstIds3;
		GE_CHECK(collectIds(dst, dstIds3), L"자기 문서에서 가져온 뒤 ID 가 유일하지 않음");

		// 8-4. 레이어 소스: target 없으면 새 레이어, target 있으면 자식만 펼침
		const size_t layersBefore = dst.GetLayers().size();
		std::vector<Node*> asLayer = dst.ImportNodes({ readBack.GetLayers()[0].get() });
		GE_CHECK(asLayer.size() == 1 && asLayer[0]->Kind() == NodeKind::Layer && dst.GetLayers().size() == layersBefore + 1, L"레이어 가져오기가 새 레이어를 만들지 않음");
		std::unordered_set<NodeId> dstIds4;
		GE_CHECK(collectIds(dst, dstIds4), L"레이어를 가져온 뒤 ID 가 유일하지 않음");
		std::vector<Node*> flat = dst.ImportNodes({ readBack.GetLayers()[0].get() }, layer0);
		GE_CHECK(flat.size() == 1 && flat[0]->Kind() == NodeKind::Group && dst.GetLayers().size() == layersBefore + 1, L"target 지정 시 레이어가 자식으로 펼쳐지지 않음");

		// 8-5. 다른 문서 소속의 target 은 거부 (아무것도 바뀌지 않음)
		std::unordered_set<NodeId> beforeForeign;
		collectIds(dst, beforeForeign);
		std::vector<Node*> rejected = dst.ImportNodes({ readGroup }, readBack.GetActiveLayer());
		std::unordered_set<NodeId> afterForeign;
		GE_CHECK(rejected.empty() && collectIds(dst, afterForeign) && afterForeign == beforeForeign, L"다른 문서의 target 으로 가져오기가 거부되지 않음");

		// 8-6. 가져오기도 변경 카운터를 올려 Undo 로 기록됨
		UndoManager um2;
		um2.Reset(dst);
		dst.ImportNodes({ readGroup });
		GE_CHECK(um2.CommitChange(L"가져오기", dst), L"ImportNodes 가 변경 카운터를 올리지 않음");
	}

	// ---- 9. 레이어 클리핑 마스크 플래그 제거 후 구버전 파일 호환 (2.0.6) ----------------------------
	{
		const std::string oldSvg =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:m2=\"https://m2draw/ns\" xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\" "
			"width=\"100\" height=\"100\" viewBox=\"0 0 100 100\" m2:version=\"2.0\">\n"
			"  <g id=\"layer_1\" inkscape:groupmode=\"layer\" inkscape:label=\"Old\" m2:clipMask=\"true\" opacity=\"0.5\">\n"
			"    <path d=\"M 0 0 L 10 10\" />\n"
			"  </g>\n"
			"</svg>\n";
		Document oldDoc;
		GE_CHECK(reader.ReadFromString(oldSvg, oldDoc), L"m2:clipMask 가 있는 구버전 파일을 읽지 못함");
		GE_CHECK(oldDoc.GetLayers().size() == 1 && oldDoc.GetLayers()[0]->GetOpacity() == 0.5f && CountPaths(oldDoc) == 1, L"구버전 파일의 레이어 내용이 보존되지 않음");
		SvgWriter w2;
		GE_CHECK(w2.WriteString(oldDoc).find("clipMask") == std::string::npos, L"저장 시 제거된 m2:clipMask 가 다시 출력됨");
	}

	// ---- 10. 그룹 계층: 월드 변환/불투명도/가시성·잠금 상속/ID 검색/좌표 변환 왕복/SVG 왕복 (2.0.7) ----
	{
		auto approx = [](Vec2 a, float x, float y) { return std::abs(a.x - x) < 0.001f && std::abs(a.y - y) < 0.001f; };

		Document d;
		auto g1u = std::make_unique<GroupNode>();
		g1u->SetName(L"G1");
		g1u->SetTransform(Matrix2D::Translation(100.0f, 0.0f));
		g1u->SetOpacity(0.5f);
		GroupNode* g1 = static_cast<GroupNode*>(d.AddShape(std::move(g1u)));

		auto g2u = std::make_unique<GroupNode>();
		g2u->SetName(L"G2");
		g2u->SetTransform(Matrix2D::Translation(0.0f, 50.0f));
		g2u->SetOpacity(0.5f);
		GroupNode* g2 = static_cast<GroupNode*>(g1->AddChild(std::move(g2u)));
		PathNode* p = static_cast<PathNode*>(g2->AddChild(MakePath(L"deep", 0.0f, 0.0f)));
		d.AssignIds(*g1);

		// 10-1. 월드 변환/불투명도는 조상 체인을 모두 반영
		GE_CHECK(p->GetParent() == g2 && g2->GetParent() == g1 && g1->GetParent() == d.GetActiveLayer(), L"중첩 그룹의 부모 체인이 틀림");
		GE_CHECK(approx(p->GetWorldTransform().TransformPoint(Vec2{ 1.0f, 1.0f }), 101.0f, 51.0f), L"중첩 그룹의 월드 변환이 조상 변환을 합치지 않음");
		GE_CHECK(std::abs(p->GetEffectiveOpacity() - 0.25f) < 0.0001f, L"중첩 그룹의 최종 불투명도가 조상 곱이 아님");

		// 10-2. 가시성/잠금은 임의 깊이의 조상에서 상속
		GE_CHECK(p->IsEffectivelyVisible() && !p->IsEffectivelyLocked(), L"초기 상태의 유효 가시성/잠금이 틀림");
		g1->SetVisible(false);
		GE_CHECK(!p->IsEffectivelyVisible() && p->IsVisible(), L"조상이 숨겨졌는데 후손이 보이는 것으로 판정됨");
		g1->SetVisible(true);
		g2->SetLocked(true);
		GE_CHECK(p->IsEffectivelyLocked() && !p->IsLocked() && !g1->IsEffectivelyLocked(), L"조상 잠금이 후손에만 전파되어야 함");
		g2->SetLocked(false);

		// 10-3. 임의 깊이 노드를 ID 로 검색
		GE_CHECK(d.FindById(p->GetId()) == p && d.FindById(g2->GetId()) == g2, L"중첩 노드를 ID 로 찾지 못함");

		// 10-4. 로컬 -> 화면 -> 로컬 왕복 (중첩 월드 변환 + 줌/팬)
		Viewport vp{ 3.0f, Vec2{ -40.0f, 25.0f } };
		const Matrix2D worldXf = p->GetWorldTransform();
		Vec2 local{ 12.5f, -7.25f };
		Vec2 scr = CoordinateSpace::LocalToScreen(local, worldXf, vp);
		auto back = CoordinateSpace::ScreenToLocal(scr, worldXf, vp);
		GE_CHECK(back.has_value() && approx(*back, local.x, local.y), L"중첩 그룹 아래에서 좌표 변환 왕복 오차 발생");

		// 10-5. SVG 왕복: 계층/이름/변환/불투명도 보존, 재직렬화 결과 동일
		SvgWriter w;
		const std::string svg1 = w.WriteString(d);
		Document back2;
		GE_CHECK(reader.ReadFromString(svg1, back2), L"중첩 그룹 SVG 를 읽지 못함");
		Node* rp = back2.FindById(p->GetId());
		GE_CHECK(rp != nullptr && rp->GetParent() != nullptr && rp->GetParent()->GetParent() != nullptr, L"중첩 그룹 구조가 복원되지 않음");
		GE_CHECK(approx(rp->GetWorldTransform().TransformPoint(Vec2{ 1.0f, 1.0f }), 101.0f, 51.0f), L"왕복 후 월드 변환이 달라짐");
		GE_CHECK(std::abs(rp->GetEffectiveOpacity() - 0.25f) < 0.0001f, L"왕복 후 최종 불투명도가 달라짐");
		GE_CHECK(w.WriteString(back2) == svg1, L"중첩 그룹이 포함된 SVG 가 재직렬화에서 달라짐");

		// 10-6. 그룹화/해제: 해제해도 월드 위치 유지 (그룹 변환이 자식에 합쳐짐)
		Document d2;
		Node* a = d2.AddShape(MakePath(L"a", 0.0f, 0.0f));
		Node* b = d2.AddShape(MakePath(L"b", 40.0f, 0.0f));
		d2.SetSelection({ a, b });
		GroupNode* grp = d2.GroupSelection();
		GE_CHECK(grp != nullptr && grp->GetChildren().size() == 2 && a->GetParent() == grp && b->GetParent() == grp, L"그룹화 결과가 틀림");
		const NodeId aId = a->GetId();
		grp->SetTransform(Matrix2D::Translation(10.0f, 20.0f));
		const Vec2 aWorld = a->GetWorldTransform().TransformPoint(Vec2{ 0.0f, 0.0f });
		d2.SetSelection({ grp });
		d2.UngroupSelection();
		Node* a2 = d2.FindById(aId);
		GE_CHECK(a2 != nullptr && a2->GetParent() == d2.GetActiveLayer(), L"그룹 해제 후 자식이 부모 레이어로 올라오지 않음");
		GE_CHECK(approx(a2->GetWorldTransform().TransformPoint(Vec2{ 0.0f, 0.0f }), aWorld.x, aWorld.y), L"그룹 해제 후 월드 위치가 바뀜");
	}

	// ---- 11. Style 모델 (Paint/Stroke): 대시 변환 헬퍼, 파서, SVG/Undo 왕복 (2.1) --------------------
	{
		auto sameArr = [](const std::vector<float>& a, const std::vector<float>& b) {
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 0.0005f) return false;
			return true;
		};

		// 11-1. 프리셋 -> dashArray: 기본 두께(2px)에서 예전 SVG 출력값과 동일, 두께에 비례
		GE_CHECK(sameArr(MakeDashArray(DashStyle::Dash, 2.0f), { 8.0f, 4.0f }), L"Dash 프리셋(2px)이 '8 4' 가 아님");
		GE_CHECK(sameArr(MakeDashArray(DashStyle::Dot, 2.0f), { 2.0f, 2.0f }), L"Dot 프리셋(2px)이 '2 2' 가 아님");
		GE_CHECK(sameArr(MakeDashArray(DashStyle::DashDot, 2.0f), { 8.0f, 4.0f, 2.0f, 4.0f }), L"DashDot 프리셋(2px)이 '8 4 2 4' 가 아님");
		GE_CHECK(sameArr(MakeDashArray(DashStyle::DashDotDot, 2.0f), { 8.0f, 4.0f, 2.0f, 4.0f, 2.0f, 4.0f }), L"DashDotDot 프리셋(2px)이 '8 4 2 4 2 4' 가 아님");
		GE_CHECK(sameArr(MakeDashArray(DashStyle::Dash, 5.0f), { 20.0f, 10.0f }), L"대시가 선 두께에 비례하지 않음");
		GE_CHECK(MakeDashArray(DashStyle::Solid, 2.0f).empty() && MakeDashArray(DashStyle::Custom, 2.0f).empty(), L"Solid/Custom 프리셋은 빈 배열이어야 함");

		// 11-2. dashArray -> 프리셋 분류 (프리셋 왕복 + 사용자 정의 판별)
		for (DashStyle p : { DashStyle::Solid, DashStyle::Dash, DashStyle::Dot, DashStyle::DashDot, DashStyle::DashDotDot })
			for (float w : { 0.5f, 1.0f, 2.0f, 7.5f })
				GE_CHECK(ClassifyDashArray(MakeDashArray(p, w), w) == p, L"프리셋 -> dashArray -> 프리셋 왕복이 일치하지 않음");
		GE_CHECK(ClassifyDashArray({ 5.0f, 3.0f, 1.0f, 3.0f }, 2.0f) == DashStyle::Custom, L"사용자 정의 대시가 프리셋으로 오분류됨");
		GE_CHECK(ClassifyDashArray({ 8.0f, 4.0f }, 5.0f) == DashStyle::Custom, L"두께가 다르면 같은 절대값도 프리셋이 아님");

		// 11-3. 대시 문자열 파서 (SVG 와 속성창 입력이 공유)
		GE_CHECK(sameArr(ParseDashArrayText(L"8 4"), { 8.0f, 4.0f }), L"'8 4' 파싱 실패");
		GE_CHECK(sameArr(ParseDashArrayText(L" 8,4 , 2 4 "), { 8.0f, 4.0f, 2.0f, 4.0f }), L"쉼표/공백 혼합 구분 파싱 실패");
		GE_CHECK(sameArr(ParseDashArrayText(L"5 3 1"), { 5.0f, 3.0f, 1.0f, 5.0f, 3.0f, 1.0f }), L"홀수 개 목록이 반복되어 짝수가 되지 않음 (SVG 규격)");
		GE_CHECK(sameArr(ParseDashArrayText(L"2.5 1.5"), { 2.5f, 1.5f }), L"소수 파싱 실패");
		GE_CHECK(ParseDashArrayText(L"none").empty() && ParseDashArrayText(L"").empty(), L"none/빈 문자열은 실선이어야 함");
		GE_CHECK(ParseDashArrayText(L"-1 2").empty() && ParseDashArrayText(L"0 0").empty() && ParseDashArrayText(L"4 abc").empty(), L"음수/합 0/숫자 아님은 실선으로 거부해야 함");
		GE_CHECK(ParseDashArrayText(L"1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1").empty(), L"과도하게 긴 대시 목록을 거부하지 않음");
		GE_CHECK(sameArr(ParseDashArrayText(FormatDashArrayText({ 8.0f, 4.0f, 2.5f, 4.0f })), { 8.0f, 4.0f, 2.5f, 4.0f }), L"대시 문자열 포맷 -> 파싱 왕복 실패");

		// 11-4. SVG 왕복: cap/join/miter/dash/offset + 채우기 보존, 기본값은 출력 생략, 재직렬화 동일
		Document d;
		PathNode* a = static_cast<PathNode*>(d.AddShape(MakePath(L"styled", 0.0f, 0.0f)));
		Style st = a->GetStyle();
		st.fill.type = PaintType::Solid;
		st.fill.color = Color(0.2f, 0.4f, 0.6f, 0.5f);
		st.stroke.width = 3.0f;
		st.stroke.color = Color(0.9f, 0.1f, 0.1f, 1.0f);
		st.stroke.cap = LineCap::Round;
		st.stroke.join = LineJoin::Bevel;
		st.stroke.miterLimit = 7.0f;
		st.stroke.dashArray = { 6.0f, 2.0f, 1.0f, 2.0f };
		st.stroke.dashOffset = 1.5f;
		a->SetStyle(st);
		PathNode* plain = static_cast<PathNode*>(d.AddShape(MakePath(L"plain", 50.0f, 0.0f)));

		SvgWriter w;
		const std::string svg = w.WriteString(d);
		GE_CHECK(svg.find("stroke-linecap=\"round\"") != std::string::npos && svg.find("stroke-linejoin=\"bevel\"") != std::string::npos
			&& svg.find("stroke-miterlimit=\"7\"") != std::string::npos && svg.find("stroke-dasharray=\"6 2 1 2\"") != std::string::npos
			&& svg.find("stroke-dashoffset=\"1.5\"") != std::string::npos, L"stroke 속성(cap/join/miter/dash/offset)이 SVG 에 출력되지 않음");
		const size_t capCount = [&] { size_t n = 0, pos = 0; while ((pos = svg.find("stroke-linecap", pos)) != std::string::npos) { ++n; ++pos; } return n; }();
		GE_CHECK(capCount == 1, L"기본값(Butt)인 도형에도 stroke-linecap 이 출력됨");

		Document back;
		GE_CHECK(reader.ReadFromString(svg, back), L"스타일 SVG 를 읽지 못함");
		const PathNode* ra = static_cast<const PathNode*>(back.FindById(a->GetId()));
		GE_CHECK(ra != nullptr, L"스타일 도형을 ID 로 찾지 못함");
		const Style& rs = ra->GetStyle();
		GE_CHECK(rs.fill.type == PaintType::Solid && NearByte(rs.fill.color.r, 0.2f) && NearByte(rs.fill.color.a, 0.5f), L"채우기가 왕복 후 달라짐");
		GE_CHECK(rs.stroke.cap == LineCap::Round && rs.stroke.join == LineJoin::Bevel && std::abs(rs.stroke.miterLimit - 7.0f) < 0.001f, L"cap/join/miterLimit 이 왕복 후 달라짐");
		GE_CHECK(sameArr(rs.stroke.dashArray, { 6.0f, 2.0f, 1.0f, 2.0f }) && std::abs(rs.stroke.dashOffset - 1.5f) < 0.001f, L"dashArray/dashOffset 이 왕복 후 달라짐");
		const PathNode* rplain = static_cast<const PathNode*>(back.FindById(plain->GetId()));
		GE_CHECK(rplain != nullptr && rplain->GetStyle().stroke.cap == LineCap::Butt && rplain->GetStyle().stroke.dashArray.empty(), L"기본 스타일 도형이 왕복 후 달라짐");
		GE_CHECK(w.WriteString(back) == svg, L"스타일이 포함된 SVG 가 재직렬화에서 달라짐");

		// Clean(순수 SVG) 에서도 표준 stroke 속성은 유지
		const std::string clean = w.WriteString(d, SvgWriteOptions(SvgWriteMode::Clean));
		GE_CHECK(clean.find("stroke-linecap=\"round\"") != std::string::npos && clean.find("stroke-dasharray=\"6 2 1 2\"") != std::string::npos, L"Clean 출력에서 표준 stroke 속성이 사라짐");

		// 11-5. 구버전(2.0 이전) 파일: 고정 대시 문자열이 숫자로 정확히 읽힘 (예전의 부분 문자열 일치 해석 제거)
		const std::string oldSvg =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">\n"
			"  <g>\n"
			"    <path id=\"p1\" d=\"M 0 0 L 10 10\" stroke=\"#000000\" stroke-width=\"2\" stroke-dasharray=\"8 4 2 4\" />\n"
			"    <path id=\"p2\" d=\"M 0 0 L 10 10\" stroke=\"#000000\" stroke-width=\"2\" stroke-dasharray=\"3 5 7\" />\n"
			"    <path id=\"p3\" d=\"M 0 0 L 10 10\" stroke=\"#000000\" stroke-width=\"2\" stroke-dasharray=\"none\" />\n"
			"  </g>\n"
			"</svg>\n";
		Document oldDoc;
		GE_CHECK(reader.ReadFromString(oldSvg, oldDoc) && CountPaths(oldDoc) == 3, L"대시가 있는 구버전 SVG 를 읽지 못함");
		// Reader 는 레이어 밖의 <path> 마다 새 레이어를 만들므로(2.9 에서 개선) <g> 로 묶어 한 곳에서 읽음
		const auto& kids = static_cast<GroupNode*>(oldDoc.GetLayers()[0]->GetChildren()[0].get())->GetChildren();
		GE_CHECK(kids.size() == 3, L"구버전 SVG 의 그룹 자식 수가 틀림");
		GE_CHECK(sameArr(static_cast<PathNode*>(kids[0].get())->GetStyle().stroke.dashArray, { 8.0f, 4.0f, 2.0f, 4.0f }), L"'8 4 2 4' 대시 해석이 틀림");
		GE_CHECK(sameArr(static_cast<PathNode*>(kids[1].get())->GetStyle().stroke.dashArray, { 3.0f, 5.0f, 7.0f, 3.0f, 5.0f, 7.0f }), L"사용자 정의 대시('3 5 7')가 부분 일치가 아닌 숫자로 해석되어야 함");
		GE_CHECK(static_cast<PathNode*>(kids[2].get())->GetStyle().stroke.dashArray.empty(), L"stroke-dasharray=none 이 실선이 아님");

		// 11-6. 스타일 변경이 Undo/Redo 로 왕복됨 (cap/join/dash 포함)
		UndoManager um;
		um.Reset(d);
		Style st2 = plain->GetStyle();
		st2.stroke.cap = LineCap::Square;
		st2.stroke.dashArray = MakeDashArray(DashStyle::Dash, st2.stroke.width);
		plain->SetStyle(st2);
		GE_CHECK(um.CommitChange(L"스타일", d), L"stroke cap/dash 변경이 Undo 로 기록되지 않음");
		const NodeId plainId = plain->GetId();
		GE_CHECK(um.Undo(d), L"스타일 Undo 실패");
		const PathNode* up = static_cast<const PathNode*>(d.FindById(plainId));
		GE_CHECK(up != nullptr && up->GetStyle().stroke.cap == LineCap::Butt && up->GetStyle().stroke.dashArray.empty(), L"Undo 후 스타일이 복원되지 않음");
		GE_CHECK(um.Redo(d), L"스타일 Redo 실패");
		const PathNode* rp2 = static_cast<const PathNode*>(d.FindById(plainId));
		GE_CHECK(rp2 != nullptr && rp2->GetStyle().stroke.cap == LineCap::Square && ClassifyDashArray(rp2->GetStyle().stroke.dashArray, rp2->GetStyle().stroke.width) == DashStyle::Dash, L"Redo 후 스타일이 복원되지 않음");

		// 11-7. Gradient 는 2.3 전까지 첫 스톱 색으로 출력 (모델만 존재해도 직렬화가 깨지지 않음)
		// (Undo/Redo 는 문서를 통째로 교체하므로 이전에 얻은 노드 포인터는 무효 -> ID 로 다시 찾음)
		PathNode* livePlain = static_cast<PathNode*>(d.FindById(plainId));
		GE_CHECK(livePlain != nullptr, L"Redo 후 도형을 찾지 못함");
		Style gs = livePlain->GetStyle();
		gs.fill.type = PaintType::Gradient;
		gs.fill.gradient.stops = { { 0.0f, Color(1.0f, 0.0f, 0.0f, 1.0f) }, { 1.0f, Color(0.0f, 0.0f, 1.0f, 1.0f) } };
		livePlain->SetStyle(gs);
		GE_CHECK(w.WriteString(d).find("fill=\"#ff0000\"") != std::string::npos, L"그라디언트 대체 색(첫 스톱)이 출력되지 않음");
	}

	// ---- 12. 혼합 모드 공용 이름 헬퍼 / 그룹 노드 불투명도·혼합 모드 왕복 (2.1b) ------------------
	{
		GE_CHECK(kBlendModeCount == static_cast<int>(LayerBlendMode::Luminosity) + 1, L"혼합 모드 개수 상수가 enum 과 다름");
		for (int i = 0; i < kBlendModeCount; ++i)
			GE_CHECK(BlendModeName(static_cast<LayerBlendMode>(i)) != nullptr && BlendModeName(static_cast<LayerBlendMode>(i))[0] != L'\0', L"혼합 모드 이름이 비어 있음");
		GE_CHECK(std::wstring(BlendModeName(LayerBlendMode::Multiply)) == L"Multiply" && std::wstring(BlendModeName(LayerBlendMode::ColorBurn)) == L"Color Burn", L"혼합 모드 이름이 enum 순서와 맞지 않음");
		GE_CHECK(std::wstring(BlendModeName(static_cast<LayerBlendMode>(200))) == L"Normal", L"범위를 벗어난 혼합 모드는 Normal 이어야 함");

		// 그룹에 설정한 불투명도/혼합 모드가 SVG 와 Undo 로 왕복됨
		Document d;
		auto gu = std::make_unique<GroupNode>();
		gu->SetName(L"G");
		GroupNode* g = static_cast<GroupNode*>(d.AddShape(std::move(gu)));
		g->AddChild(MakePath(L"inner", 0.0f, 0.0f));
		d.AssignIds(*g);
		UndoManager um;
		um.Reset(d);
		g->SetOpacity(0.4f);
		g->SetBlendMode(LayerBlendMode::Overlay);
		GE_CHECK(um.CommitChange(L"그룹 모양", d), L"그룹 불투명도/혼합 모드 변경이 Undo 로 기록되지 않음");
		SvgWriter w;
		Document back;
		GE_CHECK(reader.ReadFromString(w.WriteString(d), back), L"그룹 SVG 를 읽지 못함");
		const Node* rg = back.FindById(g->GetId());
		GE_CHECK(rg != nullptr && std::abs(rg->GetOpacity() - 0.4f) < 0.001f && rg->GetBlendMode() == LayerBlendMode::Overlay, L"그룹 불투명도/혼합 모드가 왕복 후 달라짐");
	}

#undef GE_CHECK
	return true;
}

} // namespace test
} // namespace ge
