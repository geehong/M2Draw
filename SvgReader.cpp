#include "pch.h"
#include "SvgReader.h"
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"

#include <xmllite.h>
#include <shlwapi.h>
#include <atlbase.h>

#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>
#include <cwctype>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "shlwapi.lib")

namespace ge {

namespace {

// 공백 및 쉼표 건너뛰기
void SkipSeparators(const wchar_t*& p, const wchar_t* end) {
	while (p < end && (iswspace(*p) || *p == L',')) {
		++p;
	}
}

// 실수 1개 파싱
bool ParseNextFloat(const wchar_t*& p, const wchar_t* end, float& val) {
	SkipSeparators(p, end);
	if (p >= end) return false;

	wchar_t* next = nullptr;
	val = std::wcstof(p, &next);
	if (next == p) return false;
	p = next;
	return true;
}

// 16진수 색상 (#rrggbb) 파싱
Color ParseColorHex(const std::wstring& hexStr) {
	std::wstring str = hexStr;
	if (!str.empty() && str[0] == L'#') {
		str = str.substr(1);
	}
	if (str.length() == 6) {
		unsigned int hexVal = 0;
		std::wstringstream ss;
		ss << std::hex << str;
		ss >> hexVal;
		float r = ((hexVal >> 16) & 0xFF) / 255.0f;
		float g = ((hexVal >> 8) & 0xFF) / 255.0f;
		float b = (hexVal & 0xFF) / 255.0f;
		return Color(r, g, b, 1.0f);
	}
	return Color(0.0f, 0.0f, 0.0f, 1.0f);
}

// transform="matrix(m11 m12 m21 m22 dx dy)" 파싱
Matrix2D ParseTransformMatrix(const std::wstring& xfStr) {
	size_t openParen = xfStr.find(L'(');
	size_t closeParen = xfStr.find(L')', openParen);
	if (openParen == std::wstring::npos || closeParen == std::wstring::npos) {
		return Matrix2D::Identity();
	}

	std::wstring content = xfStr.substr(openParen + 1, closeParen - openParen - 1);
	const wchar_t* p = content.c_str();
	const wchar_t* end = p + content.length();

	float m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;
	if (ParseNextFloat(p, end, m11) &&
		ParseNextFloat(p, end, m12) &&
		ParseNextFloat(p, end, m21) &&
		ParseNextFloat(p, end, m22) &&
		ParseNextFloat(p, end, dx) &&
		ParseNextFloat(p, end, dy)) {
		return Matrix2D(m11, m12, m21, m22, dx, dy);
	}

	return Matrix2D::Identity();
}

// 패스 d 속성 파싱
bool ParsePathD(const std::wstring& dStr, std::vector<SubPath>& outPaths) {
	outPaths.clear();
	const wchar_t* p = dStr.c_str();
	const wchar_t* end = p + dStr.length();

	SubPath currentSubPath;
	wchar_t lastCmd = 0;

	while (p < end) {
		SkipSeparators(p, end);
		if (p >= end) break;

		wchar_t cmd = *p;
		if (iswalpha(cmd)) {
			lastCmd = cmd;
			++p;
		} else {
			// 이전 커맨드 반복 (축약 표기)
			cmd = lastCmd;
		}

		if (cmd == L'M' || cmd == L'm') {
			if (!currentSubPath.nodes.empty()) {
				outPaths.push_back(std::move(currentSubPath));
				currentSubPath = SubPath();
			}
			float x = 0, y = 0;
			if (ParseNextFloat(p, end, x) && ParseNextFloat(p, end, y)) {
				BezierNode node;
				node.anchor = { x, y };
				currentSubPath.nodes.push_back(node);
			}
			// M 이후 추가 숫자는 L 명령으로 간주
			lastCmd = (cmd == L'M') ? L'L' : L'l';
		}
		else if (cmd == L'L' || cmd == L'l') {
			float x = 0, y = 0;
			if (ParseNextFloat(p, end, x) && ParseNextFloat(p, end, y)) {
				BezierNode node;
				if (cmd == L'l' && !currentSubPath.nodes.empty()) {
					node.anchor = { currentSubPath.nodes.back().anchor.x + x, currentSubPath.nodes.back().anchor.y + y };
				} else {
					node.anchor = { x, y };
				}
				currentSubPath.nodes.push_back(node);
			}
		}
		else if (cmd == L'C' || cmd == L'c') {
			float c1x = 0, c1y = 0, c2x = 0, c2y = 0, x = 0, y = 0;
			if (ParseNextFloat(p, end, c1x) && ParseNextFloat(p, end, c1y) &&
				ParseNextFloat(p, end, c2x) && ParseNextFloat(p, end, c2y) &&
				ParseNextFloat(p, end, x) && ParseNextFloat(p, end, y)) {

				if (!currentSubPath.nodes.empty()) {
					auto& prev = currentSubPath.nodes.back();
					prev.hOut = { c1x - prev.anchor.x, c1y - prev.anchor.y };
				}

				// 만약 도착점이 첫 정점 앵커와 거의 동일하다면(닫는 곡선), 새 노드를 추가하지 않고 첫 정점의 hIn 설정
				if (!currentSubPath.nodes.empty()) {
					const auto& first = currentSubPath.nodes.front();
					float distSq = (x - first.anchor.x) * (x - first.anchor.x) + (y - first.anchor.y) * (y - first.anchor.y);
					if (distSq < 0.001f) {
						currentSubPath.nodes.front().hIn = { c2x - first.anchor.x, c2y - first.anchor.y };
						continue;
					}
				}

				BezierNode node;
				node.anchor = { x, y };
				node.hIn = { c2x - x, c2y - y };
				currentSubPath.nodes.push_back(node);
			}
		}
		else if (cmd == L'Z' || cmd == L'z') {
			currentSubPath.closed = true;
			outPaths.push_back(std::move(currentSubPath));
			currentSubPath = SubPath();
			lastCmd = 0;
		}
		else {
			// 지원하지 않는 기타 명령(Q, A 등)은 다음 공백/문자까지 건너뜀
			while (p < end && !iswspace(*p) && *p != L',') {
				++p;
			}
		}
	}

	if (!currentSubPath.nodes.empty()) {
		outPaths.push_back(std::move(currentSubPath));
	}

	return !outPaths.empty();
}

// 노드 타입 메타데이터 복원
void ApplyNodeTypes(PathNode& pathNode, const std::wstring& nodeTypesStr) {
	const wchar_t* p = nodeTypesStr.c_str();
	const wchar_t* end = p + nodeTypesStr.length();

	auto& paths = pathNode.GetPathsRef();
	size_t spIdx = 0;
	size_t nIdx = 0;

	while (p < end) {
		SkipSeparators(p, end);
		if (p >= end) break;

		wchar_t t = *p++;
		if (spIdx >= paths.size()) break;

		if (nIdx >= paths[spIdx].nodes.size()) {
			++spIdx;
			nIdx = 0;
			if (spIdx >= paths.size()) break;
		}

		auto& node = paths[spIdx].nodes[nIdx];
		switch (t) {
		case L'c': node.type = BezierNode::Type::Corner; break;
		case L's': node.type = BezierNode::Type::Smooth; break;
		case L'z': node.type = BezierNode::Type::Symmetric; break;
		}
		++nIdx;
	}
}

using AttrMap = std::unordered_map<std::wstring, std::wstring>;

// SvgWriter 가 출력한 id ("layer_12", "group_3", "path_7") 에서 NodeId 복원. 형식이 다르면(외부 SVG) 0
NodeId ParseNodeId(const AttrMap& attrs) {
	auto it = attrs.find(L"id");
	if (it == attrs.end()) return 0;
	const std::wstring& id = it->second;
	static const wchar_t* kPrefixes[] = { L"layer_", L"group_", L"path_" };
	for (const wchar_t* prefix : kPrefixes) {
		const size_t len = wcslen(prefix);
		if (id.compare(0, len, prefix) == 0 && id.size() > len) {
			wchar_t* end = nullptr;
			unsigned long long v = std::wcstoull(id.c_str() + len, &end, 10);
			if (end && *end == L'\0') return static_cast<NodeId>(v);
		}
	}
	return 0;
}

// 보이기/잠금/혼합 모드 복원 (SvgWriter::BuildCommonState 와 대응)
void ApplyCommonState(Node& n, const AttrMap& attrs) {
	auto get = [&](const wchar_t* key) -> const std::wstring* {
		auto it = attrs.find(key);
		return it == attrs.end() ? nullptr : &it->second;
	};
	if (const auto* v = get(L"m2:visible")) n.SetVisible(*v != L"false");
	else if (const auto* s = get(L"style")) { if (s->find(L"display:none") != std::wstring::npos) n.SetVisible(false); }
	if (const auto* l = get(L"m2:locked")) n.SetLocked(*l == L"true");
	if (const auto* b = get(L"m2:blend")) {
		const long m = std::wcstol(b->c_str(), nullptr, 10);
		if (m >= 0 && m <= static_cast<long>(LayerBlendMode::Luminosity))
			n.SetBlendMode(static_cast<LayerBlendMode>(m));
	}
}

// XML 스트림으로부터 Document 파싱
bool ParseXmlStream(IStream* pStream, Document& outDoc) {
	if (!pStream) return false;

	CComPtr<IXmlReader> pReader;
	HRESULT hr = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&pReader), nullptr);
	if (FAILED(hr) || !pReader) return false;

	hr = pReader->SetInput(pStream);
	if (FAILED(hr)) return false;

	outDoc.Clear(false);

	std::vector<GroupNode*> groupStack;
	XmlNodeType nodeType;
	bool sawSvgRoot = false;
	std::wstring fileVersion;

	// Read 는 문서 끝에서 S_FALSE, XML 오류에서는 실패 HRESULT 를 반환 -> 오류 시 부분 문서를 성공으로 취급하지 않음
	while (S_OK == (hr = pReader->Read(&nodeType))) {
		if (nodeType == XmlNodeType_Element) {
			LPCWSTR localName = nullptr;
			UINT nameLen = 0;
			pReader->GetLocalName(&localName, &nameLen);
			if (!localName) continue;
			std::wstring elemName(localName, nameLen);

			bool isEmpty = (pReader->IsEmptyElement() == TRUE);

			// 속성 수집
			std::unordered_map<std::wstring, std::wstring> attrs;
			if (S_OK == pReader->MoveToFirstAttribute()) {
				do {
					LPCWSTR aLocal = nullptr;
					UINT aLocalLen = 0;
					pReader->GetLocalName(&aLocal, &aLocalLen);

					LPCWSTR aPrefix = nullptr;
					UINT aPrefixLen = 0;
					pReader->GetPrefix(&aPrefix, &aPrefixLen);

					LPCWSTR aVal = nullptr;
					UINT aValLen = 0;
					pReader->GetValue(&aVal, &aValLen);

					std::wstring key;
					if (aPrefixLen > 0) {
						key = std::wstring(aPrefix, aPrefixLen) + L":" + std::wstring(aLocal, aLocalLen);
					} else {
						key = std::wstring(aLocal, aLocalLen);
					}
					attrs[key] = std::wstring(aVal, aValLen);
				} while (S_OK == pReader->MoveToNextAttribute());
				pReader->MoveToElement();
			}

			if (elemName == L"svg") {
				sawSvgRoot = true;
				if (attrs.count(L"viewBox")) {
					const wchar_t* p = attrs[L"viewBox"].c_str();
					const wchar_t* end = p + attrs[L"viewBox"].length();
					float minX = 0, minY = 0, vbW = 1920, vbH = 1080;
					if (ParseNextFloat(p, end, minX) && ParseNextFloat(p, end, minY) &&
						ParseNextFloat(p, end, vbW) && ParseNextFloat(p, end, vbH)) {
						outDoc.width = vbW;
						outDoc.height = vbH;
					}
				} else {
					if (attrs.count(L"width")) outDoc.width = std::wcstof(attrs[L"width"].c_str(), nullptr);
					if (attrs.count(L"height")) outDoc.height = std::wcstof(attrs[L"height"].c_str(), nullptr);
				}
				if (attrs.count(L"m2:backgroundColor")) {
					outDoc.backgroundColor = ParseColorHex(attrs[L"m2:backgroundColor"]);
				}
				if (attrs.count(L"m2:backgroundOpacity")) {
					outDoc.backgroundColor.a = std::wcstof(attrs[L"m2:backgroundOpacity"].c_str(), nullptr);
				}
				if (attrs.count(L"m2:canvasColor")) {
					outDoc.canvasColor = ParseColorHex(attrs[L"m2:canvasColor"]);
				}
				if (attrs.count(L"m2:originMode")) {
					if (attrs[L"m2:originMode"] == L"bottomLeft") {
						outDoc.originMode = CanvasOriginMode::BottomLeft;
					} else {
						outDoc.originMode = CanvasOriginMode::TopLeft;
					}
				}
				if (attrs.count(L"m2:version")) {
					fileVersion = attrs[L"m2:version"];
				}
			}
			else if (elemName == L"sodipodi:namedview") {
				if (attrs.count(L"pagecolor")) {
					outDoc.backgroundColor = ParseColorHex(attrs[L"pagecolor"]);
				}
			}
			else if (elemName == L"g") {
				bool isLayer = (attrs.count(L"inkscape:groupmode") && attrs[L"inkscape:groupmode"] == L"layer");
				if (isLayer) {
					std::wstring label = L"Layer";
					if (attrs.count(L"inkscape:label")) label = attrs[L"inkscape:label"];
					else if (attrs.count(L"id")) label = attrs[L"id"];

					LayerNode* layer = outDoc.AddLayer(label);
					outDoc.RestoreNodeId(*layer, ParseNodeId(attrs));
					ApplyCommonState(*layer, attrs);
					if (attrs.count(L"opacity")) {
						layer->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
					}

					if (!isEmpty) {
						groupStack.push_back(layer);
					}
				} else {
					auto group = std::make_unique<GroupNode>();
					outDoc.RestoreNodeId(*group, ParseNodeId(attrs));
					ApplyCommonState(*group, attrs);
					if (attrs.count(L"m2:name")) group->SetName(attrs[L"m2:name"]);
					if (attrs.count(L"opacity")) group->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
					if (attrs.count(L"transform")) group->SetTransform(ParseTransformMatrix(attrs[L"transform"]));

					GroupNode* rawGroup = group.get();
					if (!groupStack.empty()) {
						groupStack.back()->AddChild(std::move(group));
					} else {
						LayerNode* fallbackLayer = outDoc.AddLayer(L"Layer 1");
						fallbackLayer->AddChild(std::move(group));
					}

					if (!isEmpty) {
						groupStack.push_back(rawGroup);
					}
				}
			}
			else if (elemName == L"path") {
				auto pathNode = std::make_unique<PathNode>();
				outDoc.RestoreNodeId(*pathNode, ParseNodeId(attrs));
				ApplyCommonState(*pathNode, attrs);
				if (attrs.count(L"m2:name")) pathNode->SetName(attrs[L"m2:name"]);

				// d 속성 파싱
				if (attrs.count(L"d")) {
					std::vector<SubPath> paths;
					if (ParsePathD(attrs[L"d"], paths)) {
						pathNode->SetPaths(paths);
					}
				}

				// 스타일
				Style st = pathNode->GetStyle();
				if (attrs.count(L"fill")) {
					if (attrs[L"fill"] == L"none") {
						st.fill.type = PaintType::None;
					} else {
						st.fill.type = PaintType::Solid;
						st.fill.color = ParseColorHex(attrs[L"fill"]);
					}
				}
				if (attrs.count(L"fill-opacity")) {
					st.fill.color.a = std::wcstof(attrs[L"fill-opacity"].c_str(), nullptr);
				}
				if (attrs.count(L"fill-rule")) {
					if (attrs[L"fill-rule"] == L"evenodd") {
						st.fillMode = FillMode::Alternate;
					} else {
						st.fillMode = FillMode::Winding;
					}
				}

				if (attrs.count(L"stroke")) {
					if (attrs[L"stroke"] == L"none") {
						st.stroke.width = 0.0f;
					} else {
						st.stroke.color = ParseColorHex(attrs[L"stroke"]);
					}
				}
				if (attrs.count(L"stroke-width")) {
					st.stroke.width = std::wcstof(attrs[L"stroke-width"].c_str(), nullptr);
				}
				if (attrs.count(L"stroke-opacity")) {
					st.stroke.color.a = std::wcstof(attrs[L"stroke-opacity"].c_str(), nullptr);
				}
				if (attrs.count(L"stroke-linecap")) {
					const std::wstring& v = attrs[L"stroke-linecap"];
					st.stroke.cap = (v == L"round") ? LineCap::Round : (v == L"square") ? LineCap::Square : LineCap::Butt;
				}
				if (attrs.count(L"stroke-linejoin")) {
					const std::wstring& v = attrs[L"stroke-linejoin"];
					st.stroke.join = (v == L"round") ? LineJoin::Round : (v == L"bevel") ? LineJoin::Bevel : LineJoin::Miter;
				}
				if (attrs.count(L"stroke-miterlimit")) {
					const float ml = std::wcstof(attrs[L"stroke-miterlimit"].c_str(), nullptr);
					st.stroke.miterLimit = (ml >= 1.0f) ? ml : 4.0f;   // SVG: 1 미만은 오류 -> 기본값
				}
				if (attrs.count(L"stroke-dasharray")) {
					st.stroke.dashArray = ParseDashArrayText(attrs[L"stroke-dasharray"]);
				}
				if (attrs.count(L"stroke-dashoffset")) {
					st.stroke.dashOffset = std::wcstof(attrs[L"stroke-dashoffset"].c_str(), nullptr);
				}

				pathNode->SetStyle(st);

				if (attrs.count(L"opacity")) {
					pathNode->SetOpacity(std::wcstof(attrs[L"opacity"].c_str(), nullptr));
				}
				if (attrs.count(L"transform")) {
					pathNode->SetTransform(ParseTransformMatrix(attrs[L"transform"]));
				}

				// 파라메트릭 정보 복원
				if (attrs.count(L"m2:shape")) {
					const std::wstring& sk = attrs[L"m2:shape"];
					if (sk == L"rect") {
						RectParams p;
						if (attrs.count(L"m2:rx")) p.rx = std::wcstof(attrs[L"m2:rx"].c_str(), nullptr);
						if (attrs.count(L"m2:ry")) p.ry = std::wcstof(attrs[L"m2:ry"].c_str(), nullptr);
						if (attrs.count(L"m2:linkRxRy")) p.linkRxRy = (attrs[L"m2:linkRxRy"] == L"true");
						pathNode->SetShapeKind(ShapeKind::Rect);
						pathNode->SetParams(p);
					} else if (sk == L"triangle") {
						TriangleParams p;
						if (attrs.count(L"m2:equilateral")) p.equilateral = (attrs[L"m2:equilateral"] == L"true");
						pathNode->SetShapeKind(ShapeKind::Triangle);
						pathNode->SetParams(p);
					} else if (sk == L"ellipse") {
						EllipseParams p;
						if (attrs.count(L"m2:linkRxRy")) p.linkRxRy = (attrs[L"m2:linkRxRy"] == L"true");
						pathNode->SetShapeKind(ShapeKind::Ellipse);
						pathNode->SetParams(p);
					} else if (sk == L"polygon") {
						PolygonParams p;
						if (attrs.count(L"m2:sides")) p.sides = std::wcstol(attrs[L"m2:sides"].c_str(), nullptr, 10);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Polygon);
						pathNode->SetParams(p);
					} else if (sk == L"star") {
						StarParams p;
						if (attrs.count(L"m2:points")) p.points = std::wcstol(attrs[L"m2:points"].c_str(), nullptr, 10);
						if (attrs.count(L"m2:innerRatio")) p.innerRatio = std::wcstof(attrs[L"m2:innerRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Star);
						pathNode->SetParams(p);
					} else if (sk == L"gear") {
						GearParams p;
						if (attrs.count(L"m2:teeth")) p.teeth = std::wcstol(attrs[L"m2:teeth"].c_str(), nullptr, 10);
						if (attrs.count(L"m2:depthRatio")) p.depthRatio = std::wcstof(attrs[L"m2:depthRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:boreRatio")) p.boreRatio = std::wcstof(attrs[L"m2:boreRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:toothWidthRatio")) p.toothWidthRatio = std::wcstof(attrs[L"m2:toothWidthRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Gear);
						pathNode->SetParams(p);
					} else if (sk == L"spiral") {
						SpiralParams p;
						if (attrs.count(L"m2:turns")) p.turns = std::wcstof(attrs[L"m2:turns"].c_str(), nullptr);
						if (attrs.count(L"m2:innerRatio")) p.innerRatio = std::wcstof(attrs[L"m2:innerRatio"].c_str(), nullptr);
						if (attrs.count(L"m2:radius")) p.radius = std::wcstof(attrs[L"m2:radius"].c_str(), nullptr);
						if (attrs.count(L"m2:cx")) p.cx = std::wcstof(attrs[L"m2:cx"].c_str(), nullptr);
						if (attrs.count(L"m2:cy")) p.cy = std::wcstof(attrs[L"m2:cy"].c_str(), nullptr);
						pathNode->SetShapeKind(ShapeKind::Spiral);
						pathNode->SetParams(p);
					} else if (sk == L"heart") {
						pathNode->SetShapeKind(ShapeKind::Heart);
						pathNode->SetParams(HeartParams{});
					}
				}

				// 노드 타입 복원
				if (attrs.count(L"m2:node-types")) {
					ApplyNodeTypes(*pathNode, attrs[L"m2:node-types"]);
				}

				if (!groupStack.empty()) {
					groupStack.back()->AddChild(std::move(pathNode));
				} else {
					LayerNode* fallbackLayer = outDoc.AddLayer(L"Layer 1");
					fallbackLayer->AddChild(std::move(pathNode));
				}
			}
		}
		else if (nodeType == XmlNodeType_EndElement) {
			LPCWSTR localName = nullptr;
			UINT nameLen = 0;
			pReader->GetLocalName(&localName, &nameLen);
			if (localName) {
				std::wstring elemName(localName, nameLen);
				if (elemName == L"g") {
					if (!groupStack.empty()) {
						groupStack.pop_back();
					}
				}
			}
		}
	}

	// XML 오류(깨진 파일)이거나 SVG 문서가 아니면 실패 (호출 측이 기존 문서를 유지할 수 있도록)
	if (FAILED(hr) || !sawSvgRoot) return false;

	// ID 무결성: 복원된 ID 는 유지하고, 없는/중복된 ID(외부 SVG 등)에만 새 ID 발급
	outDoc.EnsureUniqueIds();

	// 구버전(버전 없는 레거시 파일) 중 bottomLeft 로 저장되었던 도형 좌표 y' = H - y 보정
	if (fileVersion.empty() && outDoc.originMode == CanvasOriginMode::BottomLeft) {
		const float H = outDoc.height;
		std::function<void(Node&)> invertNodeY = [&](Node& n) {
			if (n.Kind() == NodeKind::Path) {
				auto& pNode = static_cast<PathNode&>(n);
				std::vector<SubPath> sps = pNode.GetPaths();
				for (auto& sp : sps) {
					for (auto& pt : sp.nodes) {
						pt.anchor.y = H - pt.anchor.y;
						pt.hIn.y = -pt.hIn.y;
						pt.hOut.y = -pt.hOut.y;
					}
				}
				pNode.SetPaths(sps);
			} else if (n.Kind() == NodeKind::Group || n.Kind() == NodeKind::Layer) {
				auto& gNode = static_cast<GroupNode&>(n);
				for (const auto& child : gNode.GetChildren()) {
					if (child) {
						invertNodeY(*child);
					}
				}
			}
		};
		for (auto& layer : outDoc.GetLayers()) {
			if (layer) invertNodeY(*layer);
		}
	}

	if (outDoc.GetLayers().empty()) {
		outDoc.AddLayer(L"Layer 1");
	}
	outDoc.SetActiveLayer(0);

	return true;
}

} // anonymous namespace

bool SvgReader::ReadFromString(const std::string& svgContent, Document& outDoc) {
	if (svgContent.empty()) return false;

	CComPtr<IStream> pStream;
	pStream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(svgContent.data()), static_cast<UINT>(svgContent.size())));
	if (!pStream) return false;

	return ParseXmlStream(pStream, outDoc);
}

bool SvgReader::ReadFromFile(const std::wstring& filePath, Document& outDoc) {
	if (filePath.empty()) return false;

	CComPtr<IStream> pStream;
	HRESULT hr = SHCreateStreamOnFileEx(filePath.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &pStream);
	if (FAILED(hr) || !pStream) return false;

	return ParseXmlStream(pStream, outDoc);
}

} // namespace ge
