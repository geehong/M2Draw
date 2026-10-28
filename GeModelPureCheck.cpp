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
				if (n.Kind() == NodeKind::Group || n.Kind() == NodeKind::Layer)
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

		// 11-7. 그라디언트로 렌더링할 수 없는 경우(스톱 1개)는 첫 스톱 단색으로 출력 (Gradient 정상 출력은 섹션 14)
		// (Undo/Redo 는 문서를 통째로 교체하므로 이전에 얻은 노드 포인터는 무효 -> ID 로 다시 찾음)
		PathNode* livePlain = static_cast<PathNode*>(d.FindById(plainId));
		GE_CHECK(livePlain != nullptr, L"Redo 후 도형을 찾지 못함");
		Style gs = livePlain->GetStyle();
		gs.fill.type = PaintType::Gradient;
		gs.fill.gradient.stops = { { 0.0f, Color(1.0f, 0.0f, 0.0f, 1.0f) } };
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

	// ---- 13. mix-blend-mode / display:none 표준 CSS 저장·복원 (2.2.3) ---------------------------------
	{
		for (int i = 0; i < kBlendModeCount; ++i) {
			LayerBlendMode back = LayerBlendMode::Normal;
			GE_CHECK(BlendModeFromCssName(BlendModeCssName(static_cast<LayerBlendMode>(i)), back) && back == static_cast<LayerBlendMode>(i), L"CSS 혼합 모드 이름 왕복 실패");
		}
		LayerBlendMode unchanged = LayerBlendMode::Multiply;
		GE_CHECK(!BlendModeFromCssName("not-a-mode", unchanged) && unchanged == LayerBlendMode::Multiply, L"알 수 없는 CSS 혼합 모드가 거부되지 않음");

		Document d;
		auto gu = std::make_unique<GroupNode>();
		GroupNode* g = static_cast<GroupNode*>(d.AddShape(std::move(gu)));
		PathNode* p = static_cast<PathNode*>(g->AddChild(MakePath(L"p", 0.0f, 0.0f)));
		d.AssignIds(*g);
		g->SetBlendMode(LayerBlendMode::ColorBurn);
		g->SetVisible(false);
		p->SetBlendMode(LayerBlendMode::SoftLight);
		d.GetActiveLayer()->SetBlendMode(LayerBlendMode::Screen);

		SvgWriter w;
		const std::string full = w.WriteString(d);
		GE_CHECK(full.find("mix-blend-mode:color-burn") != std::string::npos && full.find("mix-blend-mode:soft-light") != std::string::npos
			&& full.find("mix-blend-mode:screen") != std::string::npos, L"mix-blend-mode 가 SVG 에 표준 CSS 로 출력되지 않음");
		GE_CHECK(full.find("display:none;mix-blend-mode:color-burn") != std::string::npos, L"숨김+혼합 모드가 하나의 style 속성으로 합쳐지지 않음");

		// Clean(순수 SVG) 에서도 표준 CSS 는 유지되고 m2: 는 없음. 다시 읽으면 혼합 모드/숨김 복원
		const std::string clean = w.WriteString(d, SvgWriteOptions(SvgWriteMode::Clean));
		GE_CHECK(clean.find("m2:") == std::string::npos && clean.find("mix-blend-mode:color-burn") != std::string::npos, L"Clean 출력의 혼합 모드가 잘못됨");
		Document back;
		GE_CHECK(reader.ReadFromString(clean, back), L"Clean SVG(혼합 모드 포함)를 읽지 못함");
		// Clean 에는 inkscape:groupmode 가 없어 Reader 는 레이어를 일반 그룹으로 읽음(임시 레이어 아래) -> ID 가 아닌 구조로 탐색
		GE_CHECK(!back.GetLayers().empty() && !back.GetLayers()[0]->GetChildren().empty(), L"Clean SVG 의 최상위 구조가 비어 있음");
		const GroupNode* layerAsGroup = static_cast<const GroupNode*>(back.GetLayers()[0]->GetChildren()[0].get());
		GE_CHECK(layerAsGroup->GetBlendMode() == LayerBlendMode::Screen, L"Clean 왕복 후 레이어 혼합 모드가 달라짐");
		GE_CHECK(!layerAsGroup->GetChildren().empty(), L"Clean SVG 의 그룹이 비어 있음");
		const GroupNode* rg = static_cast<const GroupNode*>(layerAsGroup->GetChildren()[0].get());
		GE_CHECK(rg->GetBlendMode() == LayerBlendMode::ColorBurn && !rg->IsVisible(), L"Clean 왕복 후 그룹 혼합 모드/숨김이 달라짐");
		GE_CHECK(!rg->GetChildren().empty() && rg->GetChildren()[0]->GetBlendMode() == LayerBlendMode::SoftLight, L"Clean 왕복 후 도형 혼합 모드가 달라짐");

		// 전체 형식 왕복도 동일 (m2:blend 와 CSS 가 같이 있어도 충돌 없음)
		Document back2;
		GE_CHECK(reader.ReadFromString(full, back2) && w.WriteString(back2) == full, L"혼합 모드가 포함된 SVG 가 재직렬화에서 달라짐");

		// 외부 SVG: m2: 없이 CSS 만 있는 경우
		const std::string ext =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">\n"
			"  <g style=\"fill:red; mix-blend-mode: multiply ;display:none\">\n"
			"    <path d=\"M 0 0 L 5 5\" />\n"
			"  </g>\n"
			"</svg>\n";
		Document extDoc;
		GE_CHECK(reader.ReadFromString(ext, extDoc), L"CSS style 만 있는 외부 SVG 를 읽지 못함");
		const Node* eg = extDoc.GetLayers()[0]->GetChildren()[0].get();
		GE_CHECK(eg->GetBlendMode() == LayerBlendMode::Multiply && !eg->IsVisible(), L"외부 SVG 의 style 속성(mix-blend-mode/display)을 해석하지 못함");
	}

	// ---- 14. 그라디언트: 모델 헬퍼 / SVG(<defs>) 왕복 / Undo (2.3) -----------------------------------
	{
		auto near = [](float a, float b) { return std::abs(a - b) < 0.002f; };

		// 14-1. 모델 헬퍼
		Gradient g = Gradient::Make(Gradient::Kind::Linear, Color(1, 0, 0, 1), Color(0, 0, 1, 1));
		GE_CHECK(g.IsRenderable() && g.start.x == 0.0f && g.end.x == 1.0f, L"선형 그라디언트 기본값이 틀림");
		const Color mid = g.SampleColor(0.5f);
		GE_CHECK(near(mid.r, 0.5f) && near(mid.b, 0.5f), L"그라디언트 중간색 보간이 틀림");
		GE_CHECK(g.SampleColor(-1.0f).r == 1.0f && g.SampleColor(2.0f).b == 1.0f, L"범위 밖 색은 양 끝 색이어야 함");
		g.stops.push_back({ 0.25f, Color(0, 1, 0, 1) });
		g.NormalizeStops();
		GE_CHECK(g.stops.size() == 3 && near(g.stops[1].offset, 0.25f), L"스톱 정렬이 틀림");
		g.Reverse();
		GE_CHECK(near(g.stops[0].offset, 0.0f) && g.stops[0].color.b == 1.0f && near(g.stops[1].offset, 0.75f) && g.stops[2].color.r == 1.0f, L"그라디언트 반전이 틀림");
		g.SetKind(Gradient::Kind::Radial);
		GE_CHECK(g.kind == Gradient::Kind::Radial && near(g.start.x, 0.5f) && near(g.radius, 0.5f) && g.stops.size() == 3, L"종류 전환 시 기하는 초기화되고 스톱은 유지되어야 함");
		Gradient one;
		one.stops = { { 0.0f, Color(0.2f, 0.4f, 0.6f, 1.0f) } };
		GE_CHECK(!one.IsRenderable() && near(one.FallbackColor().g, 0.4f), L"스톱 1개 그라디언트의 렌더링 가능 판정/대체 색이 틀림");

		// 14-2. SVG 왕복: 선형(알파 포함 3스톱) + 방사형(초점) + 스톱 1개(단색 대체)
		Document d;
		PathNode* lin = static_cast<PathNode*>(d.AddShape(MakePath(L"lin", 0.0f, 0.0f)));
		PathNode* rad = static_cast<PathNode*>(d.AddShape(MakePath(L"rad", 100.0f, 0.0f)));
		PathNode* single = static_cast<PathNode*>(d.AddShape(MakePath(L"single", 200.0f, 0.0f)));
		Style ls = lin->GetStyle();
		ls.fill.type = PaintType::Gradient;
		ls.fill.gradient = Gradient::Make(Gradient::Kind::Linear, Color(1, 0, 0, 1), Color(0, 0, 1, 0.5f));
		ls.fill.gradient.start = { 0.1f, 0.2f }; ls.fill.gradient.end = { 0.9f, 0.8f };
		ls.fill.gradient.stops.push_back({ 0.4f, Color(0, 1, 0, 1) });
		ls.fill.gradient.NormalizeStops();
		lin->SetStyle(ls);
		Style rs = rad->GetStyle();
		rs.fill.type = PaintType::Gradient;
		rs.fill.gradient = Gradient::Make(Gradient::Kind::Radial, Color(1, 1, 0, 1), Color(0, 0, 0, 1));
		rs.fill.gradient.start = { 0.4f, 0.6f }; rs.fill.gradient.end = { 0.3f, 0.5f }; rs.fill.gradient.radius = 0.35f;
		rad->SetStyle(rs);
		Style ss = single->GetStyle();
		ss.fill.type = PaintType::Gradient;
		ss.fill.gradient.stops = { { 0.0f, Color(0.2f, 0.4f, 0.6f, 1.0f) } };
		single->SetStyle(ss);

		SvgWriter w;
		const std::string svg = w.WriteString(d);
		GE_CHECK(svg.find("<defs>") != std::string::npos && svg.find("<linearGradient") != std::string::npos && svg.find("<radialGradient") != std::string::npos, L"그라디언트 <defs> 가 출력되지 않음");
		GE_CHECK(svg.find("fill=\"url(#grad_") != std::string::npos, L"그라디언트 도형이 url(#id) 로 참조하지 않음");
		GE_CHECK(svg.find("grad_" + std::to_string(single->GetId())) == std::string::npos, L"스톱 1개 그라디언트는 정의를 만들면 안 됨 (단색 대체)");
		GE_CHECK(svg.find("stop-opacity=\"0.5\"") != std::string::npos, L"스톱 알파가 stop-opacity 로 출력되지 않음");

		Document back;
		GE_CHECK(reader.ReadFromString(svg, back), L"그라디언트 SVG 를 읽지 못함");
		const PathNode* bl = static_cast<const PathNode*>(back.FindById(lin->GetId()));
		const PathNode* br = static_cast<const PathNode*>(back.FindById(rad->GetId()));
		const PathNode* bs = static_cast<const PathNode*>(back.FindById(single->GetId()));
		GE_CHECK(bl && br && bs, L"그라디언트 도형을 ID 로 찾지 못함");
		const Gradient& lg = bl->GetStyle().fill.gradient;
		GE_CHECK(bl->GetStyle().fill.type == PaintType::Gradient && lg.kind == Gradient::Kind::Linear && lg.stops.size() == 3, L"선형 그라디언트가 복원되지 않음");
		GE_CHECK(near(lg.start.x, 0.1f) && near(lg.start.y, 0.2f) && near(lg.end.x, 0.9f) && near(lg.end.y, 0.8f), L"선형 그라디언트 시작/끝점이 달라짐");
		GE_CHECK(near(lg.stops[1].offset, 0.4f) && near(lg.stops[1].color.g, 1.0f) && near(lg.stops[2].color.a, 0.5f), L"그라디언트 스톱(위치/색/알파)이 달라짐");
		const Gradient& rg = br->GetStyle().fill.gradient;
		GE_CHECK(rg.kind == Gradient::Kind::Radial && near(rg.start.x, 0.4f) && near(rg.start.y, 0.6f) && near(rg.end.x, 0.3f) && near(rg.end.y, 0.5f) && near(rg.radius, 0.35f), L"방사형 그라디언트(중심/초점/반지름)가 달라짐");
		GE_CHECK(bs->GetStyle().fill.type == PaintType::Solid && near(bs->GetStyle().fill.color.g, 0.4f), L"스톱 1개 그라디언트가 단색으로 복원되지 않음");
		GE_CHECK(w.WriteString(back) == w.WriteString(d) || w.WriteString(back).find("<radialGradient") != std::string::npos, L"그라디언트 재직렬화 실패");

		// Clean 에서도 표준 그라디언트는 유지
		const std::string clean = w.WriteString(d, SvgWriteOptions(SvgWriteMode::Clean));
		GE_CHECK(clean.find("<linearGradient") != std::string::npos && clean.find("m2:") == std::string::npos, L"Clean 출력의 그라디언트가 잘못됨");

		// 14-3. Undo/Redo 로 그라디언트 편집이 왕복됨
		UndoManager um;
		um.Reset(d);
		Style ls2 = lin->GetStyle();
		ls2.fill.gradient.Reverse();
		lin->SetStyle(ls2);
		GE_CHECK(um.CommitChange(L"그라디언트 반전", d), L"그라디언트 편집이 Undo 로 기록되지 않음");
		const NodeId linId = lin->GetId();
		GE_CHECK(um.Undo(d), L"그라디언트 Undo 실패");
		const PathNode* ul = static_cast<const PathNode*>(d.FindById(linId));
		GE_CHECK(ul && ul->GetStyle().fill.type == PaintType::Gradient && near(ul->GetStyle().fill.gradient.stops[0].color.r, 1.0f), L"Undo 후 그라디언트가 복원되지 않음");
		GE_CHECK(um.Redo(d), L"그라디언트 Redo 실패");
		const PathNode* rl = static_cast<const PathNode*>(d.FindById(linId));
		GE_CHECK(rl && near(rl->GetStyle().fill.gradient.stops[0].color.b, 1.0f) && near(rl->GetStyle().fill.gradient.stops[0].color.a, 0.5f), L"Redo 후 반전된 그라디언트가 복원되지 않음");

		// 14-4. 외부 SVG: 퍼센트 좌표, style 로 지정한 stop-color, userSpaceOnUse(단색 대체), 알 수 없는 참조
		const std::string ext =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">\n"
			"  <defs>\n"
			"    <linearGradient id=\"a\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"50%\">\n"
			"      <stop offset=\"0%\" style=\"stop-color:#ff0000;stop-opacity:1\"/>\n"
			"      <stop offset=\"100%\" style=\"stop-color:#0000ff;stop-opacity:0.25\"/>\n"
			"    </linearGradient>\n"
			"    <linearGradient id=\"u\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" y1=\"0\" x2=\"50\" y2=\"0\">\n"
			"      <stop offset=\"0\" stop-color=\"#00ff00\"/><stop offset=\"1\" stop-color=\"#000000\"/>\n"
			"    </linearGradient>\n"
			"  </defs>\n"
			"  <g>\n"
			"    <path d=\"M 0 0 L 10 10 L 0 10 Z\" fill=\"url(#a)\"/>\n"
			"    <path d=\"M 0 0 L 10 10 L 0 10 Z\" fill=\"url(#u)\"/>\n"
			"    <path d=\"M 0 0 L 10 10 L 0 10 Z\" fill=\"url(#missing)\"/>\n"
			"  </g>\n"
			"</svg>\n";
		Document extDoc;
		GE_CHECK(reader.ReadFromString(ext, extDoc), L"외부 SVG 그라디언트를 읽지 못함");
		const auto& ek = static_cast<GroupNode*>(extDoc.GetLayers()[0]->GetChildren()[0].get())->GetChildren();
		GE_CHECK(ek.size() == 3, L"외부 SVG 도형 수가 틀림");
		const Style& e0 = static_cast<PathNode*>(ek[0].get())->GetStyle();
		GE_CHECK(e0.fill.type == PaintType::Gradient && near(e0.fill.gradient.end.x, 1.0f) && near(e0.fill.gradient.end.y, 0.5f), L"퍼센트 좌표 그라디언트 해석이 틀림");
		GE_CHECK(e0.fill.gradient.stops.size() == 2 && near(e0.fill.gradient.stops[1].offset, 1.0f) && near(e0.fill.gradient.stops[1].color.b, 1.0f) && near(e0.fill.gradient.stops[1].color.a, 0.25f), L"style 로 지정한 stop-color/opacity 해석이 틀림");
		const Style& e1 = static_cast<PathNode*>(ek[1].get())->GetStyle();
		GE_CHECK(e1.fill.type == PaintType::Solid && near(e1.fill.color.g, 1.0f), L"userSpaceOnUse 그라디언트가 첫 스톱 단색으로 대체되지 않음");
		GE_CHECK(static_cast<PathNode*>(ek[2].get())->GetStyle().fill.type == PaintType::None, L"알 수 없는 fill 참조가 채움 없음으로 처리되지 않음");
	}

	// ---- 15. 그룹화 / 그룹 해제 (2.7.1) -----------------------------------------------------------
	{
		auto approx = [](Vec2 a, Vec2 b) { return std::abs(a.x - b.x) < 0.001f && std::abs(a.y - b.y) < 0.001f; };
		auto worldOrigin = [](const Node* n) { return n->GetWorldTransform().TransformPoint(Vec2{ 0.0f, 0.0f }); };

		// 15-1. 같은 레이어: 선택 2개 이상일 때만 가능, 맨 위 노드 자리에 생성, 이름은 group1, group2 ...
		Document d;
		Node* a = d.AddShape(MakePath(L"a", 0.0f, 0.0f));
		Node* b = d.AddShape(MakePath(L"b", 10.0f, 0.0f));
		Node* c = d.AddShape(MakePath(L"c", 20.0f, 0.0f));
		Node* e = d.AddShape(MakePath(L"e", 30.0f, 0.0f));
		LayerNode* layer = d.GetActiveLayer();
		d.SetSelection({ a });
		GE_CHECK(!d.CanGroupSelection() && d.GroupSelection() == nullptr, L"선택 1개는 그룹화되면 안 됨");
		d.SetSelection({ a, c });
		GE_CHECK(d.CanGroupSelection() && !d.CanUngroupSelection(), L"선택 2개는 그룹화 가능해야 하고 해제 대상은 없어야 함");
		const Vec2 aW = worldOrigin(a), cW = worldOrigin(c);
		GroupNode* g1 = d.GroupSelection();
		GE_CHECK(g1 != nullptr && g1->GetName() == L"group1", L"첫 그룹 이름이 group1 이 아님");
		GE_CHECK(g1->GetChildren().size() == 2 && g1->GetChildren()[0].get() == a && g1->GetChildren()[1].get() == c, L"그룹 자식 순서(z-order)가 유지되지 않음");
		// a,b,c,e -> b, [a,c], e : 그룹은 맨 위 노드(c)가 있던 자리, 나머지 노드 순서 유지
		GE_CHECK(layer->GetChildren().size() == 3 && layer->GetChildren()[0].get() == b && layer->GetChildren()[1].get() == g1 && layer->GetChildren()[2].get() == e, L"그룹이 맨 위 노드 자리에 만들어지지 않음");
		GE_CHECK(d.GetSelection().size() == 1 && d.GetSelection()[0] == g1, L"그룹화 후 그룹이 선택되어야 함");
		GE_CHECK(approx(worldOrigin(a), aW) && approx(worldOrigin(c), cW), L"그룹화 후 월드 위치가 바뀜");
		d.SetSelection({ b, e });
		GroupNode* g2 = d.GroupSelection();
		GE_CHECK(g2 != nullptr && g2->GetName() == L"group2", L"두 번째 그룹 이름이 group2 가 아님");

		// 15-2. 서로 다른 레이어: 맨 위 노드의 레이어에 생성, 월드 위치 유지
		Document d2;
		Node* low = d2.AddShape(MakePath(L"low", 5.0f, 5.0f));
		LayerNode* upper = d2.AddLayer(L"Upper");
		d2.SetActiveLayer(1);
		Node* high = d2.AddShape(MakePath(L"high", 50.0f, 50.0f));
		GE_CHECK(low->GetParent() != high->GetParent(), L"테스트 전제: 서로 다른 레이어");
		d2.SetSelection({ low, high });
		const Vec2 lowW = worldOrigin(low), highW = worldOrigin(high);
		GroupNode* cross = d2.GroupSelection();
		GE_CHECK(cross != nullptr && cross->GetParent() == upper, L"다른 레이어 노드들의 그룹이 맨 위 레이어에 만들어지지 않음");
		GE_CHECK(d2.GetLayers()[0]->GetChildren().empty(), L"그룹화한 노드가 원래 레이어에 남아 있음");
		GE_CHECK(approx(worldOrigin(low), lowW) && approx(worldOrigin(high), highW), L"레이어를 넘어 그룹화한 뒤 월드 위치가 바뀜");

		// 15-3. 변환된 부모 그룹 안의 노드와 바깥 노드를 묶어도 화면 위치 유지
		Document d3;
		auto tg = std::make_unique<GroupNode>();
		tg->SetTransform(Matrix2D::Translation(100.0f, 50.0f));
		GroupNode* moved = static_cast<GroupNode*>(d3.AddShape(std::move(tg)));
		Node* inner = moved->AddChild(MakePath(L"inner", 0.0f, 0.0f));
		d3.AssignIds(*moved);
		Node* outer = d3.AddShape(MakePath(L"outer", 7.0f, 7.0f));
		d3.SetSelection({ inner, outer });
		const Vec2 innerW = worldOrigin(inner), outerW = worldOrigin(outer);
		GroupNode* mix = d3.GroupSelection();
		GE_CHECK(mix != nullptr, L"변환된 그룹 안팎의 노드 그룹화 실패");
		GE_CHECK(approx(worldOrigin(inner), innerW) && approx(worldOrigin(outer), outerW), L"부모 변환이 다른 노드를 그룹화한 뒤 월드 위치가 바뀜");

		// 15-4. 조상이 함께 선택되면 조상에 포함 (자식은 따로 옮기지 않음)
		Document d4;
		auto pg = std::make_unique<GroupNode>();
		GroupNode* parentGroup = static_cast<GroupNode*>(d4.AddShape(std::move(pg)));
		Node* child = parentGroup->AddChild(MakePath(L"child", 0.0f, 0.0f));
		d4.AssignIds(*parentGroup);
		Node* sibling = d4.AddShape(MakePath(L"sibling", 9.0f, 0.0f));
		d4.SetSelection({ parentGroup, child });
		GE_CHECK(!d4.CanGroupSelection(), L"그룹과 그 자식만 선택하면 그룹화 대상은 1개(그룹)뿐이므로 불가해야 함");
		d4.SetSelection({ parentGroup, child, sibling });
		GroupNode* outerGroup = d4.GroupSelection();
		GE_CHECK(outerGroup != nullptr && outerGroup->GetChildren().size() == 2 && child->GetParent() == parentGroup, L"조상이 선택된 자식이 따로 옮겨짐");

		// 15-5. 그룹 해제: 자식이 그룹 자리로 올라오고 월드 위치 유지, 자식들이 선택됨, Undo 로 복원
		Document d5;
		Node* u1 = d5.AddShape(MakePath(L"u1", 0.0f, 0.0f));
		Node* u2 = d5.AddShape(MakePath(L"u2", 20.0f, 0.0f));
		d5.SetSelection({ u1, u2 });
		GroupNode* ug = d5.GroupSelection();
		ug->SetTransform(Matrix2D::Translation(30.0f, 40.0f));
		const Vec2 u1W = worldOrigin(u1), u2W = worldOrigin(u2);
		UndoManager um;
		um.Reset(d5);
		d5.SetSelection({ ug });
		GE_CHECK(d5.CanUngroupSelection(), L"그룹 선택은 해제 가능해야 함");
		const NodeId u1Id = u1->GetId();
		d5.UngroupSelection();
		GE_CHECK(d5.GetSelection().size() == 2 && d5.GetActiveLayer()->GetChildren().size() == 2, L"그룹 해제 후 자식이 레이어로 올라오지 않음");
		GE_CHECK(approx(worldOrigin(d5.FindById(u1Id)), u1W), L"그룹 해제 후 월드 위치가 바뀜 (그룹 변환이 자식에 합쳐져야 함)");
		(void)u2W;
		GE_CHECK(um.CommitChange(L"그룹 해제", d5), L"그룹 해제가 Undo 로 기록되지 않음");
		GE_CHECK(um.Undo(d5) && d5.FindById(u1Id) != nullptr && d5.FindById(u1Id)->GetParent()->Kind() == NodeKind::Group, L"그룹 해제 Undo 후 그룹이 복원되지 않음");
	}

	// ---- 16. 순서(Z-order) 변경 (2.7.2) ----------------------------------------------------------
	{
		Document d;
		Node* a = d.AddShape(MakePath(L"a", 0.0f, 0.0f));
		Node* b = d.AddShape(MakePath(L"b", 10.0f, 0.0f));
		Node* c = d.AddShape(MakePath(L"c", 20.0f, 0.0f));
		Node* e = d.AddShape(MakePath(L"e", 30.0f, 0.0f));
		LayerNode* layer = d.GetActiveLayer();
		auto order = [&](GroupNode* g) {
			std::wstring s;
			for (const auto& ch : g->GetChildren()) s += ch->GetName();
			return s;
		};
		GE_CHECK(order(layer) == L"abce", L"테스트 전제: 초기 순서");

		d.ClearSelection();
		GE_CHECK(!d.CanArrangeSelection(), L"선택이 없으면 순서 변경 불가여야 함");
		d.SetSelection({ b });
		GE_CHECK(d.CanArrangeSelection(), L"도형 선택이면 순서 변경 가능해야 함");

		d.ArrangeSelection(Document::ZOrder::Front);
		GE_CHECK(order(layer) == L"aceb", L"맨 앞으로 결과가 틀림");
		d.ArrangeSelection(Document::ZOrder::Back);
		GE_CHECK(order(layer) == L"bace", L"맨 뒤로 결과가 틀림");
		d.ArrangeSelection(Document::ZOrder::Forward);
		GE_CHECK(order(layer) == L"abce", L"앞으로(한 단계) 결과가 틀림");
		d.ArrangeSelection(Document::ZOrder::Backward);
		GE_CHECK(order(layer) == L"bace", L"뒤로(한 단계) 결과가 틀림");
		d.ArrangeSelection(Document::ZOrder::Backward);
		GE_CHECK(order(layer) == L"bace", L"맨 뒤에서 더 뒤로 보내도 순서가 바뀌면 안 됨");

		// 여러 개 선택: 선택된 노드끼리의 상대 순서 유지
		d.SetSelection({ a, c });
		d.ArrangeSelection(Document::ZOrder::Front);
		GE_CHECK(order(layer) == L"beac", L"다중 선택 맨 앞으로: 선택 노드의 상대 순서가 유지되어야 함");
		d.ArrangeSelection(Document::ZOrder::Back);
		GE_CHECK(order(layer) == L"acbe", L"다중 선택 맨 뒤로: 선택 노드의 상대 순서가 유지되어야 함");

		// 그룹 안의 노드는 자기 부모 안에서만 이동
		auto gu = std::make_unique<GroupNode>();
		GroupNode* grp = static_cast<GroupNode*>(d.AddShape(std::move(gu)));
		Node* g1 = grp->AddChild(MakePath(L"g1", 0.0f, 0.0f));
		Node* g2 = grp->AddChild(MakePath(L"g2", 5.0f, 0.0f));
		d.AssignIds(*grp);
		d.SetSelection({ g1 });
		d.ArrangeSelection(Document::ZOrder::Front);
		GE_CHECK(order(grp) == L"g2g1", L"그룹 안 노드의 맨 앞으로가 틀림");
		GE_CHECK(layer->GetChildren().back().get() == grp, L"그룹 안 노드를 옮겼는데 그룹의 레이어 내 순서가 바뀜");
		(void)g2;

		// 변경 카운터/Undo: 실제로 바뀌면 기록, 바뀌지 않으면 기록 안 함
		UndoManager um;
		um.Reset(d);
		d.SetSelection({ b });
		d.ArrangeSelection(Document::ZOrder::Front);
		GE_CHECK(um.CommitChange(L"맨 앞으로", d), L"순서 변경이 Undo 로 기록되지 않음");
		d.ArrangeSelection(Document::ZOrder::Front);   // 이미 맨 앞
		GE_CHECK(!um.CommitChange(L"맨 앞으로", d), L"바뀌지 않은 순서 변경이 Undo 로 기록됨");
		const NodeId bId = b->GetId();
		GE_CHECK(um.Undo(d), L"순서 변경 Undo 실패");
		Node* ub = d.FindById(bId);
		GE_CHECK(ub != nullptr && ub->GetParent()->GetChildren().back().get() != ub, L"Undo 후 순서가 복원되지 않음");
		(void)e;

		// 레이어 밖의 최상위 <path> 들은 하나의 레이어에 모여야 함 (요소마다 레이어를 만들면 Z-order 가 레이어 경계에 막힘)
		const std::string flat =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">\n"
			"  <path d=\"M 0 0 L 10 10\" />\n"
			"  <path d=\"M 5 5 L 20 20\" />\n"
			"  <g><path d=\"M 1 1 L 2 2\" /></g>\n"
			"</svg>\n";
		Document flatDoc;
		GE_CHECK(reader.ReadFromString(flat, flatDoc), L"레이어 없는 SVG 읽기 실패");
		GE_CHECK(flatDoc.GetLayers().size() == 1 && flatDoc.GetLayers()[0]->GetChildren().size() == 3, L"레이어 밖의 최상위 요소들이 하나의 레이어에 모이지 않음");
	}

	// ---- 17. 노드 이동 (레이어 패널 드래그) ------------------------------------------------------
	{
		Document d;
		Node* a = d.AddShape(MakePath(L"a", 0.0f, 0.0f));
		Node* b = d.AddShape(MakePath(L"b", 10.0f, 0.0f));
		Node* c = d.AddShape(MakePath(L"c", 20.0f, 0.0f));
		LayerNode* l1 = d.GetActiveLayer();
		auto order = [&](GroupNode* g) {
			std::wstring s;
			for (const auto& ch : g->GetChildren()) s += ch->GetName();
			return s;
		};
		GE_CHECK(order(l1) == L"abc", L"테스트 전제: 초기 순서");

		// 같은 부모: a 를 c 위(index = c 인덱스 + 1 = 3)로 -> 제거로 밀리는 만큼 보정되어 맨 앞
		GE_CHECK(d.MoveNodeTo(a, l1, 3), L"같은 부모 이동 실패");
		GE_CHECK(order(l1) == L"bca", L"같은 부모 맨 위 이동 결과가 틀림");
		// c 를 b 아래(index = b 인덱스 = 0)로 -> 맨 뒤
		GE_CHECK(d.MoveNodeTo(c, l1, 0), L"같은 부모 아래 이동 실패");
		GE_CHECK(order(l1) == L"cba", L"같은 부모 맨 아래 이동 결과가 틀림");

		// 다른 레이어로 이동 (최상단)
		LayerNode* l2 = d.AddLayer(L"Layer 2");
		GE_CHECK(d.MoveNodeTo(b, l2, -1), L"다른 레이어로 이동 실패");
		GE_CHECK(order(l1) == L"ca" && order(l2) == L"b" && b->GetParent() == l2, L"다른 레이어 이동 결과가 틀림");

		// 변환 있는 그룹으로 이동해도 화면 위치(월드 변환)는 유지
		auto gu = std::make_unique<GroupNode>();
		GroupNode* grp = static_cast<GroupNode*>(l2->AddChild(std::move(gu)));
		d.AssignIds(*grp);
		grp->SetTransform(Matrix2D::Translation(100.0f, 50.0f));
		const Matrix2D before = a->GetWorldTransform();
		GE_CHECK(d.MoveNodeTo(a, grp, -1), L"그룹 안으로 이동 실패");
		const Matrix2D after = a->GetWorldTransform();
		GE_CHECK(std::abs(before.TransformPoint(Vec2{ 1.0f, 2.0f }).x - after.TransformPoint(Vec2{ 1.0f, 2.0f }).x) < 1e-3f &&
			std::abs(before.TransformPoint(Vec2{ 1.0f, 2.0f }).y - after.TransformPoint(Vec2{ 1.0f, 2.0f }).y) < 1e-3f,
			L"그룹으로 옮긴 뒤 월드 위치가 바뀜");

		// 거부: 자기 자신/후손 안으로, 레이어 자체
		GE_CHECK(!d.MoveNodeTo(grp, grp, -1), L"그룹을 자기 자신 안으로 이동할 수 있으면 안 됨");
		GE_CHECK(!d.MoveNodeTo(l1, l2, -1), L"레이어를 노드 이동으로 옮길 수 있으면 안 됨");
	}

	// ---- 18. 클리핑 마스크 SVG 왕복 (2.4.6) ------------------------------------------------------
	{
		auto countOf = [](const std::string& s, const std::string& key) {
			size_t n = 0;
			for (size_t pos = s.find(key); pos != std::string::npos; pos = s.find(key, pos + key.size())) ++n;
			return n;
		};
		Document d;
		auto gu = std::make_unique<GroupNode>();
		GroupNode* grp = static_cast<GroupNode*>(d.AddShape(std::move(gu)));
		grp->AddChild(MakePath(L"c1", 0.0f, 0.0f));
		grp->AddChild(MakePath(L"c2", 5.0f, 0.0f));
		Node* maskNode = grp->AddChild(MakePath(L"mask", 20.0f, 0.0f));
		grp->SetClippingMask(true);
		d.AssignIds(*grp);
		GE_CHECK(grp->GetMaskNode() == maskNode, L"테스트 전제: 마지막 자식이 마스크");

		SvgWriter cw;
		const std::string svg = cw.WriteString(d, SvgWriteOptions(SvgWriteMode::File));
		GE_CHECK(svg.find("<clipPath") != std::string::npos && svg.find("clip-path=\"url(#") != std::string::npos, L"클리핑 그룹이 clipPath 로 출력되지 않음");
		GE_CHECK(countOf(svg, "<path") == 3, L"마스크 도형이 clipPath 정의 외에 본문에도 출력됨 (다른 뷰어에서 보이게 됨)");

		Document back;
		GE_CHECK(reader.ReadFromString(svg, back), L"클리핑 SVG 를 읽지 못함");
		GE_CHECK(back.GetLayers().size() == 1 && back.GetLayers()[0]->GetChildren().size() == 1, L"clipPath 안의 도형이 화면 도형으로 읽힘");
		auto* g2 = dynamic_cast<GroupNode*>(back.GetLayers()[0]->GetChildren()[0].get());
		GE_CHECK(g2 && g2->IsClippingMask() && g2->GetChildren().size() == 3, L"클리핑 그룹/마스크 복원 실패 (자식 3개: 내용 2 + 마스크)");
		GE_CHECK(g2->GetMaskNode() != nullptr && g2->GetMaskNode() != g2->GetChildren()[0].get(), L"마스크 노드가 마지막 자식으로 복원되지 않음");

		// 외부 SVG(다른 도구가 만든 clipPath): 정의 안의 도형은 마스크, 나머지 자식은 내용
		const std::string ext =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">\n"
			"  <defs><clipPath id=\"c1\"><path d=\"M 10 10 L 50 10 L 30 50 Z\" /></clipPath></defs>\n"
			"  <g clip-path=\"url(#c1)\"><path d=\"M 0 0 L 100 0 L 100 100 Z\" fill=\"#ff0000\" /></g>\n"
			"  <g clip-path=\"url(#missing)\"><path d=\"M 0 0 L 10 10\" /></g>\n"
			"</svg>\n";
		Document extDoc;
		GE_CHECK(reader.ReadFromString(ext, extDoc), L"외부 clipPath SVG 읽기 실패");
		const auto& top = extDoc.GetLayers()[0]->GetChildren();
		GE_CHECK(top.size() == 2, L"외부 SVG 최상위 요소 수가 틀림");
		auto* e1 = dynamic_cast<GroupNode*>(top[0].get());
		auto* e2 = dynamic_cast<GroupNode*>(top[1].get());
		GE_CHECK(e1 && e1->IsClippingMask() && e1->GetChildren().size() == 2, L"외부 clipPath 그룹: 내용 1 + 마스크 1 이어야 함");
		GE_CHECK(e2 && !e2->IsClippingMask() && e2->GetChildren().size() == 1, L"없는 clipPath 를 참조하면 마스크 없이 내용만 유지되어야 함");
	}

	// ---- 19. 클리핑 그룹의 마스크 고정 (Z-order/드래그로 마스크가 바뀌지 않음) -------------------------
	{
		Document d;
		auto gu = std::make_unique<GroupNode>();
		GroupNode* grp = static_cast<GroupNode*>(d.AddShape(std::move(gu)));
		Node* c1 = grp->AddChild(MakePath(L"c1", 0.0f, 0.0f));
		grp->AddChild(MakePath(L"c2", 5.0f, 0.0f));
		Node* mask = grp->AddChild(MakePath(L"mask", 20.0f, 0.0f));
		grp->SetClippingMask(true);
		d.AssignIds(*grp);

		d.SetSelection({ c1 });
		d.ArrangeSelection(Document::ZOrder::Front);
		GE_CHECK(grp->GetMaskNode() == mask, L"일반 자식을 맨 앞으로 보내도 마스크는 그대로여야 함");
		d.ArrangeSelection(Document::ZOrder::Forward);
		GE_CHECK(grp->GetMaskNode() == mask, L"일반 자식을 앞으로 보내도 마스크는 그대로여야 함");

		LayerNode* layer = d.GetActiveLayer();
		Node* other = layer->AddChild(MakePath(L"other", 50.0f, 0.0f));
		d.AssignIds(*layer);
		GE_CHECK(d.MoveNodeTo(other, grp, -1), L"클리핑 그룹 안으로 이동 실패");
		GE_CHECK(grp->GetMaskNode() == mask, L"클리핑 그룹 안으로 들어온 노드가 마스크가 되면 안 됨");

		GE_CHECK(d.MoveNodeTo(mask, layer, -1), L"마스크 도형을 그룹 밖으로 이동 실패");
		GE_CHECK(!grp->IsClippingMask(), L"마스크 도형을 빼면 클리핑이 해제되어야 함");
	}

	// ---- 20. 외부 SVG 가져오기 (2.9): 상대 명령/H/V/호, 기본 도형, 속성 상속, CSS, 색상, transform 리스트, viewBox ----
	{
		const std::string ext =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\" viewBox=\"0 0 400 200\">\n"
			"  <style>.r{fill:#00ff00} circle{stroke:blue}</style>\n"
			"  <g fill=\"red\" stroke=\"black\" stroke-width=\"3\" transform=\"translate(10,20) scale(2)\">\n"
			"    <rect x=\"1\" y=\"2\" width=\"30\" height=\"40\"/>\n"
			"    <rect class=\"r\" x=\"0\" y=\"0\" width=\"5\" height=\"5\" style=\"stroke:rgb(255,0,0)\"/>\n"
			"    <circle cx=\"50\" cy=\"50\" r=\"10\"/>\n"
			"  </g>\n"
			"  <path d=\"m 10 10 h 20 v 20 l -20 0 z\" fill=\"none\"/>\n"
			"  <path d=\"M0 0 A 10 10 0 0 1 20 0\" fill=\"none\" stroke=\"#000\"/>\n"
			"  <polygon points=\"0,0 10,0 10,10\"/>\n"
			"</svg>\n";
		Document e;
		GE_CHECK(reader.ReadFromString(ext, e), L"외부 SVG(2.9) 읽기 실패");
		GE_CHECK(e.width == 200.0f && e.height == 100.0f, L"width/height 가 문서 크기로 반영되지 않음");
		const auto& top = e.GetLayers()[0]->GetChildren();
		GE_CHECK(top.size() == 4, L"최상위 요소 수가 틀림 (g, path, path, polygon)");

		// viewBox(400x200) -> 문서(200x100): 레이어에 0.5 배 스케일
		const Vec2 vp = e.GetLayers()[0]->GetTransform().TransformPoint(Vec2{ 100.0f, 100.0f });
		GE_CHECK(std::abs(vp.x - 50.0f) < 1e-3f && std::abs(vp.y - 50.0f) < 1e-3f, L"viewBox 스케일이 적용되지 않음");

		auto* g = dynamic_cast<GroupNode*>(top[0].get());
		GE_CHECK(g && g->GetChildren().size() == 3, L"그룹 자식 수가 틀림");
		const Vec2 gp = g->GetTransform().TransformPoint(Vec2{ 1.0f, 1.0f });
		GE_CHECK(std::abs(gp.x - 12.0f) < 1e-3f && std::abs(gp.y - 22.0f) < 1e-3f, L"transform 리스트 합성(translate scale)이 틀림");

		auto* r0 = dynamic_cast<PathNode*>(g->GetChildren()[0].get());
		auto* r1 = dynamic_cast<PathNode*>(g->GetChildren()[1].get());
		auto* c0 = dynamic_cast<PathNode*>(g->GetChildren()[2].get());
		GE_CHECK(r0 && r1 && c0, L"기본 도형이 PathNode 로 변환되지 않음");
		GE_CHECK(r0->GetStyle().fill.color.r == 1.0f && r0->GetStyle().stroke.width == 3.0f, L"그룹의 fill/stroke-width 상속이 안 됨");
		GE_CHECK(r1->GetStyle().fill.color.g == 1.0f && r1->GetStyle().fill.color.r == 0.0f, L"<style> 클래스 규칙(.r)이 적용되지 않음");
		GE_CHECK(r1->GetStyle().stroke.color.r == 1.0f && r1->GetStyle().stroke.color.g == 0.0f, L"style 속성의 rgb() 색상이 적용되지 않음");
		GE_CHECK(c0->GetStyle().stroke.color.b == 1.0f, L"<style> 태그 규칙(circle)이 적용되지 않음");
		GE_CHECK(c0->GetPaths().size() == 1 && c0->GetPaths()[0].closed && c0->GetPaths()[0].nodes.size() >= 3, L"circle 이 닫힌 패스로 변환되지 않음");

		auto* rel = dynamic_cast<PathNode*>(top[1].get());
		GE_CHECK(rel && rel->GetPaths().size() == 1 && rel->GetPaths()[0].closed && rel->GetPaths()[0].nodes.size() == 4, L"상대 명령(m h v l z) 패스의 노드 수가 틀림");
		GE_CHECK(rel->GetPaths()[0].nodes[2].anchor.x == 30.0f && rel->GetPaths()[0].nodes[2].anchor.y == 30.0f, L"상대 좌표 누적이 틀림");

		auto* arc = dynamic_cast<PathNode*>(top[2].get());
		GE_CHECK(arc && arc->GetPaths().size() == 1 && arc->GetPaths()[0].nodes.size() >= 3, L"호(A) 가 베지에 곡선으로 변환되지 않음");
		const Vec2 arcEnd = arc->GetPaths()[0].nodes.back().anchor;
		GE_CHECK(std::abs(arcEnd.x - 20.0f) < 1e-3f && std::abs(arcEnd.y) < 1e-3f, L"호의 끝점이 틀림");
		const BezierNode& mid = arc->GetPaths()[0].nodes[1];   // 반원 중간 점: 위쪽(sweep=1 -> y 증가 방향)으로 반지름 10
		GE_CHECK(std::abs(mid.anchor.x - 10.0f) < 1e-2f && std::abs(std::abs(mid.anchor.y) - 10.0f) < 1e-2f, L"호의 중간 점이 반지름 위에 있지 않음");

		auto* poly = dynamic_cast<PathNode*>(top[3].get());
		GE_CHECK(poly && poly->GetPaths()[0].closed && poly->GetPaths()[0].nodes.size() == 3, L"polygon 변환이 틀림");
		GE_CHECK(poly->GetStyle().fill.type == PaintType::Solid && poly->GetStyle().fill.color.r == 0.0f && poly->GetStyle().stroke.width == 0.0f,
			L"SVG 기본값(fill 검정, stroke 없음)이 적용되지 않음");
	}

	// ---- 21. SVG 출력 다듬기 (2.8): 네이티브 도형 태그 / 그라디언트 중복 제거 / Clean 네임스페이스 ----
	{
		Document d;
		PathNode* rc = static_cast<PathNode*>(d.AddShape(MakePath(L"r", 0.0f, 0.0f)));
		rc->SetShapeKind(ShapeKind::Rect);
		rc->SetParams(RectParams{ 3.0f, 3.0f, true });
		PathNode* g1 = static_cast<PathNode*>(d.AddShape(MakePath(L"g1", 10.0f, 0.0f)));
		PathNode* g2 = static_cast<PathNode*>(d.AddShape(MakePath(L"g2", 20.0f, 0.0f)));
		Gradient gr;
		gr.kind = Gradient::Kind::Linear;
		gr.stops = { GradientStop{ 0.0f, Color(1, 0, 0, 1) }, GradientStop{ 1.0f, Color(0, 0, 1, 1) } };
		for (PathNode* gp : { g1, g2 }) {
			Style st = gp->GetStyle();
			st.fill.type = PaintType::Gradient;
			st.fill.gradient = gr;
			gp->SetStyle(st);
		}
		SvgWriter nw;
		const std::string clean = nw.WriteString(d, SvgWriteOptions(SvgWriteMode::Clean));
		GE_CHECK(clean.find("<rect ") != std::string::npos && clean.find("rx=\"") != std::string::npos, L"Clean 출력에서 Rect 가 <rect rx ry> 로 나오지 않음");
		GE_CHECK(clean.find("xmlns:inkscape") == std::string::npos && clean.find("xmlns:sodipodi") == std::string::npos && clean.find("xmlns:m2") == std::string::npos,
			L"Clean 출력에 M2/Inkscape 네임스페이스 선언이 남음");
		size_t gradDefs = 0;
		for (size_t pos = clean.find("<linearGradient"); pos != std::string::npos; pos = clean.find("<linearGradient", pos + 1)) ++gradDefs;
		GE_CHECK(gradDefs == 1, L"같은 그라디언트가 <defs> 에 중복 정의됨");
		size_t refs = 0;
		for (size_t pos = clean.find("fill=\"url(#"); pos != std::string::npos; pos = clean.find("fill=\"url(#", pos + 1)) ++refs;
		GE_CHECK(refs == 2, L"두 도형이 공유 그라디언트를 참조하지 않음");
		Document back;
		GE_CHECK(reader.ReadFromString(clean, back), L"네이티브 도형 Clean SVG 를 다시 읽지 못함");

		// File 출력은 M2 메타데이터를 읽는 <path> 를 유지
		const std::string file = nw.WriteString(d, SvgWriteOptions(SvgWriteMode::File));
		GE_CHECK(file.find("<rect ") == std::string::npos && file.find("m2:shape=\"rect\"") != std::string::npos, L"File 출력은 Rect 를 <path m2:shape> 로 유지해야 함");
	}

	// ---- 22. 텍스트 노드 (3.1): 모델, 복제, SVG 왕복, 외부 <text> 읽기 ----------------------------
	{
		Document d;
		auto tn = std::make_unique<TextNode>();
		tn->SetText(L"가나다\nABC");
		tn->SetFontSize(20.0f);
		tn->SetBold(true);
		tn->SetAlign(TextAlign::Center);
		tn->SetTransform(Matrix2D::Translation(30.0f, 40.0f));
		TextNode* t0 = static_cast<TextNode*>(d.AddShape(std::move(tn)));
		GE_CHECK(t0 && t0->GetId() != 0 && t0->Kind() == NodeKind::Text, L"TextNode 를 문서에 추가하지 못함");
		GE_CHECK(t0->LineCount() == 2, L"줄 수 계산이 틀림");
		Rect2 b;
		GE_CHECK(t0->GetLocalBounds(b) && b.left < 0.0f && b.right > 0.0f && b.Height() >= 40.0f, L"가운데 정렬 텍스트의 경계가 기준점을 가운데에 두지 않음");

		auto clone = t0->Clone();
		GE_CHECK(clone->Kind() == NodeKind::Text && static_cast<TextNode*>(clone.get())->GetText() == L"가나다\nABC" && clone->GetId() == 0, L"TextNode 복제가 틀림");

		SvgWriter tw;
		const std::string svg = tw.WriteString(d, SvgWriteOptions(SvgWriteMode::File));
		GE_CHECK(svg.find("<text ") != std::string::npos && svg.find("<tspan") != std::string::npos && svg.find("text-anchor=\"middle\"") != std::string::npos, L"텍스트가 <text>/<tspan> 으로 출력되지 않음");
		Document back;
		GE_CHECK(reader.ReadFromString(svg, back), L"텍스트 SVG 를 다시 읽지 못함");
		const auto& kids = back.GetLayers()[0]->GetChildren();
		GE_CHECK(kids.size() == 1 && kids[0]->Kind() == NodeKind::Text, L"읽은 문서에 텍스트 노드가 없음");
		auto* t1 = static_cast<TextNode*>(kids[0].get());
		GE_CHECK(t1->GetText() == L"가나다\nABC" && t1->IsBold() && t1->GetAlign() == TextAlign::Center && std::abs(t1->GetFontSize() - 20.0f) < 1e-3f, L"텍스트 속성 왕복이 틀림");
		const Vec2 o = t1->GetWorldTransform().TransformPoint(Vec2{ 0.0f, 0.0f });
		GE_CHECK(std::abs(o.x - 30.0f) < 1e-3f && std::abs(o.y - 40.0f) < 1e-3f, L"텍스트 위치 왕복이 틀림");

		const std::string ext =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"60\">\n"
			"  <g fill=\"#336699\"><text x=\"10\" y=\"30\" font-family=\"'Arial', sans-serif\" font-size=\"16\" font-weight=\"700\" font-style=\"italic\">\n"
			"     Hello   <tspan>World</tspan>\n  </text></g>\n"
			"</svg>\n";
		Document ed;
		GE_CHECK(reader.ReadFromString(ext, ed), L"외부 <text> SVG 읽기 실패");
		auto* eg = dynamic_cast<GroupNode*>(ed.GetLayers()[0]->GetChildren()[0].get());
		GE_CHECK(eg && eg->GetChildren().size() == 1 && eg->GetChildren()[0]->Kind() == NodeKind::Text, L"외부 <text> 가 TextNode 로 읽히지 않음");
		auto* et = static_cast<TextNode*>(eg->GetChildren()[0].get());
		GE_CHECK(et->GetText() == L"Hello\nWorld", L"공백 정규화/tspan 줄바꿈이 틀림");
		GE_CHECK(et->GetFontFamily() == L"Arial" && et->IsBold() && et->IsItalic(), L"외부 글꼴 속성이 틀림");
		GE_CHECK(et->GetStyle().fill.color.b > 0.5f && et->GetStyle().fill.color.r < 0.3f, L"그룹의 fill 이 텍스트에 상속되지 않음");
		const Vec2 eo = et->GetWorldTransform().TransformPoint(Vec2{ 0.0f, 0.0f });
		GE_CHECK(std::abs(eo.x - 10.0f) < 1e-3f && std::abs(eo.y - (30.0f - 16.0f * TextNode::kAscentRatio)) < 1e-3f, L"기준선 y 가 상자 위쪽으로 환산되지 않음");
	}

	// ---- 23. 이미지 노드 (3.4): data URI 읽기/쓰기 왕복, 복제, Undo ------------------------------------
	{
		// 1x1 빨강 PNG
		const char* kPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
		const std::string svg = std::string(
			"<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"100\">\n"
			"  <image x=\"10\" y=\"20\" width=\"40\" height=\"30\" xlink:href=\"data:image/png;base64,") + kPng + "\"/>\n</svg>\n";
		Document d;
		GE_CHECK(reader.ReadFromString(svg, d), L"이미지 SVG 읽기 실패");
		const auto& kids = d.GetLayers()[0]->GetChildren();
		GE_CHECK(kids.size() == 1 && kids[0]->Kind() == NodeKind::Image, L"<image> 가 ImageNode 로 읽히지 않음");
		auto* im = static_cast<ImageNode*>(kids[0].get());
		GE_CHECK(im->GetData() && im->GetData()->bytes.size() > 50 && im->GetData()->mime == "image/png", L"이미지 바이트/MIME 복원이 틀림");
		GE_CHECK(im->GetWidth() == 40.0f && im->GetHeight() == 30.0f, L"이미지 크기가 틀림");
		Rect2 b;
		GE_CHECK(im->GetLocalBounds(b) && b.right == 40.0f && b.bottom == 30.0f, L"이미지 경계가 틀림");
		const Vec2 o = im->GetWorldTransform().TransformPoint(Vec2{ 0.0f, 0.0f });
		GE_CHECK(o.x == 10.0f && o.y == 20.0f, L"이미지 위치(x,y)가 틀림");

		auto clone = im->Clone();
		GE_CHECK(clone->Kind() == NodeKind::Image && static_cast<ImageNode*>(clone.get())->GetData() == im->GetData(), L"이미지 복제는 데이터를 공유해야 함");

		SvgWriter iw;
		for (SvgWriteMode m : { SvgWriteMode::File, SvgWriteMode::Clean, SvgWriteMode::Snapshot }) {
			const std::string out = iw.WriteString(d, SvgWriteOptions(m));
			GE_CHECK(out.find("<image ") != std::string::npos && out.find("base64,") != std::string::npos, L"이미지가 data URI 로 출력되지 않음");
			Document back;
			GE_CHECK(reader.ReadFromString(out, back), L"이미지 SVG 왕복 읽기 실패");
			// Clean 은 레이어 표식(inkscape:groupmode)이 없어 레이어가 일반 그룹으로 읽히므로 재귀로 이미지 노드를 찾음
			ImageNode* found = nullptr;
			std::function<void(Node&)> find = [&](Node& nn) {
				if (nn.Kind() == NodeKind::Image) { found = static_cast<ImageNode*>(&nn); return; }
				if (nn.Kind() == NodeKind::Group || nn.Kind() == NodeKind::Layer)
					for (const auto& c : static_cast<GroupNode&>(nn).GetChildren()) if (c && !found) find(*c);
			};
			find(*back.GetLayers()[0]);
			GE_CHECK(found != nullptr, L"왕복 후 이미지 노드가 없음");
			GE_CHECK(found->GetData() && found->GetData()->bytes == im->GetData()->bytes, L"왕복 후 이미지 바이트가 달라짐");
		}

		// Undo: 스냅샷으로 복원해도 이미지가 남아야 함
		UndoManager um;
		um.Reset(d);
		kids[0]->SetTransform(Matrix2D::Translation(99.0f, 99.0f));
		GE_CHECK(um.CommitChange(L"이동", d), L"이미지 이동이 Undo 로 기록되지 않음");
		GE_CHECK(um.Undo(d), L"이미지 문서 Undo 실패");
		const auto& after = d.GetLayers()[0]->GetChildren();
		GE_CHECK(after.size() == 1 && after[0]->Kind() == NodeKind::Image && static_cast<ImageNode*>(after[0].get())->GetData() &&
			!static_cast<ImageNode*>(after[0].get())->GetData()->bytes.empty(), L"Undo 후 이미지 데이터가 사라짐");
	}

#undef GE_CHECK

	return true;
}

} // namespace test
} // namespace ge
