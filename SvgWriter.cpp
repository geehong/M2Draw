#include "pch.h"
#include "SvgWriter.h"
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"

#include <charconv>
#include <functional>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ge {

namespace {

// 로캘 독립적 부동소수점 포맷팅 (항상 '.' 소수점 보장, 불필요한 후행 0 및 소수점 제거)
std::string FormatFloat(float v) {
	char buf[64];
	auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 4);
	if (ec != std::errc()) {
		return "0";
	}
	char* end = ptr;
	char* dot = nullptr;
	for (char* p = buf; p < end; ++p) {
		if (*p == '.') {
			dot = p;
			break;
		}
	}
	if (dot) {
		while (end > dot && (*(end - 1) == '0')) {
			--end;
		}
		if (end > dot && *(end - 1) == '.') {
			--end;
		}
	}
	return std::string(buf, end);
}

// 색상을 #rrggbb 16진수 문자열로 변환
std::string ColorToHex(const Color& c) {
	int ir = std::clamp(static_cast<int>(c.r * 255.0f + 0.5f), 0, 255);
	int ig = std::clamp(static_cast<int>(c.g * 255.0f + 0.5f), 0, 255);
	int ib = std::clamp(static_cast<int>(c.b * 255.0f + 0.5f), 0, 255);
	char buf[16];
	sprintf_s(buf, "#%02x%02x%02x", ir, ig, ib);
	return buf;
}

// XML 특수 문자 이스케이프
std::string EscapeXml(const std::string& str) {
	std::string out;
	out.reserve(str.size());
	for (char c : str) {
		switch (c) {
		case '&':  out += "&amp;"; break;
		case '<':  out += "&lt;"; break;
		case '>':  out += "&gt;"; break;
		case '\"': out += "&quot;"; break;
		case '\'': out += "&apos;"; break;
		default:   out += c; break;
		}
	}
	return out;
}

// wstring -> UTF-8 string 변환
std::string WideToUtf8(const std::wstring& wstr) {
	if (wstr.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	if (len <= 0) return {};
	std::string res(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), res.data(), len, nullptr, nullptr);
	return res;
}

// 단위 행렬 여부 판별
bool IsIdentity(const Matrix2D& m) {
	return std::abs(m.m11 - 1.0f) < 0.0001f &&
	       std::abs(m.m12) < 0.0001f &&
	       std::abs(m.m21) < 0.0001f &&
	       std::abs(m.m22 - 1.0f) < 0.0001f &&
	       std::abs(m.dx) < 0.0001f &&
	       std::abs(m.dy) < 0.0001f;
}

// ShapeKind 열거형 -> 문자열 변환
std::string ShapeKindToString(ShapeKind k) {
	switch (k) {
	case ShapeKind::Rect:     return "rect";
	case ShapeKind::Triangle: return "triangle";
	case ShapeKind::Ellipse:  return "ellipse";
	case ShapeKind::Polygon:  return "polygon";
	case ShapeKind::Star:     return "star";
	case ShapeKind::Gear:     return "gear";
	case ShapeKind::Spiral:   return "spiral";
	case ShapeKind::Heart:    return "heart";
	default:                  return "";
	}
}

// 패스 데이터 (d 속성) 빌드
std::string BuildPathData(const PathNode& path) {
	std::string d;
	for (const auto& sp : path.GetPaths()) {
		const auto& nodes = sp.nodes;
		if (nodes.empty()) continue;

		if (!d.empty()) d += " ";
		d += "M ";
		d += FormatFloat(nodes[0].anchor.x) + " " + FormatFloat(nodes[0].anchor.y);

		for (size_t i = 1; i < nodes.size(); ++i) {
			const auto& prev = nodes[i - 1];
			const auto& curr = nodes[i];
			Vec2 c1 = prev.AbsOut();
			Vec2 c2 = curr.AbsIn();

			if (!prev.HasOut() && !curr.HasIn()) {
				d += " L ";
				d += FormatFloat(curr.anchor.x) + " " + FormatFloat(curr.anchor.y);
			} else {
				d += " C ";
				d += FormatFloat(c1.x) + " " + FormatFloat(c1.y) + " ";
				d += FormatFloat(c2.x) + " " + FormatFloat(c2.y) + " ";
				d += FormatFloat(curr.anchor.x) + " " + FormatFloat(curr.anchor.y);
			}
		}

		if (sp.closed && nodes.size() > 1) {
			const auto& prev = nodes.back();
			const auto& curr = nodes.front();
			if (prev.HasOut() || curr.HasIn()) {
				Vec2 c1 = prev.AbsOut();
				Vec2 c2 = curr.AbsIn();
				d += " C ";
				d += FormatFloat(c1.x) + " " + FormatFloat(c1.y) + " ";
				d += FormatFloat(c2.x) + " " + FormatFloat(c2.y) + " ";
				d += FormatFloat(curr.anchor.x) + " " + FormatFloat(curr.anchor.y);
			}
			d += " Z";
		}
	}
	return d;
}

// 노드 타입 메타데이터 문자열 빌드 (Inkscape sodipodi:nodetypes 스타일)
std::string BuildNodeTypes(const PathNode& path) {
	std::string res;
	for (size_t s = 0; s < path.GetPaths().size(); ++s) {
		if (s > 0) res += " ";
		for (const auto& n : path.GetPaths()[s].nodes) {
			switch (n.type) {
			case BezierNode::Type::Corner:    res += 'c'; break;
			case BezierNode::Type::Smooth:    res += 's'; break;
			case BezierNode::Type::Symmetric: res += 'z'; break;
			}
		}
	}
	return res;
}

// M2 전용 메타데이터(m2:visible / m2:locked / m2:blend). Clean 에서는 아무것도 출력하지 않음.
// 레이어는 기존 형식(항상 locked/visible 출력)을 유지하므로 includeVisibleLocked=false 로 호출
std::string BuildCommonState(const Node& n, bool includeVisibleLocked, bool meta) {
	std::string s;
	if (!meta) return s;
	if (includeVisibleLocked) {
		if (!n.IsVisible()) s += " m2:visible=\"false\"";
		if (n.IsLocked())   s += " m2:locked=\"true\"";
	}
	if (n.GetBlendMode() != LayerBlendMode::Normal) {
		s += " m2:blend=\"" + std::to_string(static_cast<int>(n.GetBlendMode())) + "\"";
	}
	return s;
}

// 표준 CSS style 속성: 숨김(display:none)과 혼합 모드(mix-blend-mode)는 모든 출력 모드에서 표준 형식으로 기록
// (브라우저/Inkscape 호환). 잠금은 표준에 대응이 없어 m2:locked 로만 저장.
std::string BuildCssStyle(const Node& n) {
	std::string css;
	if (!n.IsVisible()) css += "display:none";
	if (n.GetBlendMode() != LayerBlendMode::Normal) {
		if (!css.empty()) css += ";";
		css += std::string("mix-blend-mode:") + BlendModeCssName(n.GetBlendMode());
	}
	return css.empty() ? std::string() : " style=\"" + css + "\"";
}

// 그라디언트 정의 id: 도형마다 자기 정의를 가짐 (동일 그라디언트 통합은 Step 2.8.2)
std::string GradientId(const PathNode& p) { return "grad_" + std::to_string(p.GetId()); }

// 이 도형이 <defs> 의 그라디언트를 참조해야 하는지 (렌더링 가능한 그라디언트 채우기)
bool UsesGradientDef(const PathNode& p) {
	return p.GetStyle().fill.type == PaintType::Gradient && p.GetStyle().fill.gradient.IsRenderable();
}

} // anonymous namespace

SvgWriter::SvgWriter() = default;

void SvgWriter::AppendIndent() {
	for (int i = 0; i < m_indent; ++i) {
		m_output += "  ";
	}
}

// 렌더링 가능한 그라디언트 채우기를 가진 모든 도형의 <linearGradient>/<radialGradient> 정의를 <defs> 로 출력.
// 좌표는 objectBoundingBox(도형 박스 기준 비율, SVG 기본 gradientUnits)
std::string SvgWriter::GradientIdFor(const PathNode& p) const {
	auto it = m_gradIds.find(&p);
	return it != m_gradIds.end() ? it->second : GradientId(p);
}

void SvgWriter::WriteGradientDefs(Document& doc) {
	m_gradIds.clear();
	std::vector<const PathNode*> users;
	std::function<void(const Node&)> collect = [&](const Node& n) {
		if (n.Kind() == NodeKind::Path) {
			const PathNode& p = static_cast<const PathNode&>(n);
			if (UsesGradientDef(p)) users.push_back(&p);
		} else if (n.Kind() == NodeKind::Group || n.Kind() == NodeKind::Layer) {
			for (const auto& c : static_cast<const GroupNode&>(n).GetChildren())
				if (c) collect(*c);
		}
	};
	for (const auto& layer : doc.GetLayers()) if (layer) collect(*layer);
	if (users.empty()) return;

	std::unordered_map<std::string, std::string> byContent;   // 그라디언트 내용 -> id (중복 제거)
	for (const PathNode* p : users) {
		Gradient g = p->GetStyle().fill.gradient;
		g.NormalizeStops();   // SVG 는 stop offset 이 오름차순이어야 함 (편집 중 모델은 정렬되지 않았을 수 있음)

		std::string key = (g.kind == Gradient::Kind::Linear ? "L" : "R") + FormatFloat(g.start.x) + "," + FormatFloat(g.start.y) + "," +
			FormatFloat(g.end.x) + "," + FormatFloat(g.end.y) + "," + FormatFloat(g.radius);
		for (const GradientStop& st : g.stops)
			key += "|" + FormatFloat(st.offset) + ColorToHex(st.color) + FormatFloat(st.color.a);
		auto dup = byContent.find(key);
		if (dup != byContent.end()) { m_gradIds[p] = dup->second; continue; }
		byContent[key] = GradientId(*p);
		m_gradIds[p] = GradientId(*p);

		AppendIndent();
		if (g.kind == Gradient::Kind::Linear) {
			m_output += "<linearGradient id=\"" + GradientId(*p) + "\" x1=\"" + FormatFloat(g.start.x) + "\" y1=\"" + FormatFloat(g.start.y) +
			            "\" x2=\"" + FormatFloat(g.end.x) + "\" y2=\"" + FormatFloat(g.end.y) + "\">\n";
		} else {
			m_output += "<radialGradient id=\"" + GradientId(*p) + "\" cx=\"" + FormatFloat(g.start.x) + "\" cy=\"" + FormatFloat(g.start.y) +
			            "\" r=\"" + FormatFloat(g.radius) + "\" fx=\"" + FormatFloat(g.end.x) + "\" fy=\"" + FormatFloat(g.end.y) + "\">\n";
		}
		++m_indent;
		for (const GradientStop& st : g.stops) {
			AppendIndent();
			m_output += "<stop offset=\"" + FormatFloat(st.offset) + "\" stop-color=\"" + ColorToHex(st.color) + "\"";
			if (st.color.a < 0.999f) m_output += " stop-opacity=\"" + FormatFloat(st.color.a) + "\"";
			m_output += " />\n";
		}
		--m_indent;
		AppendIndent();
		m_output += (g.kind == Gradient::Kind::Linear) ? "</linearGradient>\n" : "</radialGradient>\n";
	}
}
void SvgWriter::WriteClipPathDefs(Document& doc) {
	std::vector<const GroupNode*> maskGroups;
	std::function<void(const Node&)> collect = [&](const Node& n) {
		// 레이어(LayerNode)도 GroupNode 를 상속하므로 dynamic_cast 로 레이어 안까지 순회
		if (const auto* g = dynamic_cast<const GroupNode*>(&n)) {
			if (g->IsClippingMask() && g->GetMaskNode() != nullptr) {
				maskGroups.push_back(g);
			}
			for (const auto& c : g->GetChildren()) {
				if (c) collect(*c);
			}
		}
	};
	for (const auto& layer : doc.GetLayers()) if (layer) collect(*layer);
	if (maskGroups.empty()) return;

	for (const GroupNode* g : maskGroups) {
		const Node* maskNode = g->GetMaskNode();
		if (!maskNode || maskNode->Kind() != NodeKind::Path) continue;
		const PathNode& pathMask = static_cast<const PathNode&>(*maskNode);

		AppendIndent();
		m_output += "<clipPath id=\"clip_group_" + std::to_string(g->GetId()) + "\">\n";
		++m_indent;
		AppendIndent();
		m_output += "<path d=\"" + BuildPathData(pathMask) + "\"";
		const Matrix2D& xf = pathMask.GetTransform();
		if (!IsIdentity(xf)) {
			m_output += " transform=\"matrix(" +
				FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " +
				FormatFloat(xf.m21) + " " + FormatFloat(xf.m22) + " " +
				FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
		}
		m_output += " />\n";
		--m_indent;
		AppendIndent();
		m_output += "</clipPath>\n";
	}
}

std::string SvgWriter::WriteString(Document& doc, const SvgWriteOptions& options) {
	m_options = options;
	m_output.clear();
	m_indent = 0;

	// XML 선언 및 SVG 루트 시작
	m_output += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	m_output += "<svg xmlns=\"http://www.w3.org/2000/svg\"\n";
	if (Meta()) {
		m_output += "     xmlns:m2=\"https://m2draw/ns\"\n";
		m_output += "     xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\"\n";
		m_output += "     xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\"\n";
	}
	m_output += "     width=\"" + FormatFloat(doc.width) + "\" height=\"" + FormatFloat(doc.height) + "\"\n";
	m_output += "     viewBox=\"0 0 " + FormatFloat(doc.width) + " " + FormatFloat(doc.height) + "\"";
	if (Meta()) {
		m_output += "\n";
		m_output += "     m2:backgroundColor=\"" + ColorToHex(doc.backgroundColor) + "\"\n";
		if (doc.backgroundColor.a < 0.999f) {
			m_output += "     m2:backgroundOpacity=\"" + FormatFloat(doc.backgroundColor.a) + "\"\n";
		}
		m_output += "     m2:canvasColor=\"" + ColorToHex(doc.canvasColor) + "\"\n";
		m_output += std::string("     m2:originMode=\"") + (doc.originMode == CanvasOriginMode::BottomLeft ? "bottomLeft" : "topLeft") + "\"\n";
		m_output += "     m2:version=\"2.0\"";
	}
	m_output += ">\n";

	++m_indent;

	if (Meta()) {
		AppendIndent();
		m_output += "<sodipodi:namedview id=\"namedview_canvas\" pagecolor=\"" + ColorToHex(doc.backgroundColor) + "\" />\n";
	}

	// defs 블록 출력: 그라디언트 정의 및 클리핑 마스크 정의
	std::vector<const PathNode*> gradUsers;
	std::vector<const GroupNode*> maskGroups;
	std::function<void(const Node&)> collect = [&](const Node& n) {
		if (n.Kind() == NodeKind::Path) {
			const PathNode& p = static_cast<const PathNode&>(n);
			if (UsesGradientDef(p)) gradUsers.push_back(&p);
		} else {
			// GroupNode 또는 LayerNode (LayerNode는 GroupNode를 상속)
			const auto* g = dynamic_cast<const GroupNode*>(&n);
			if (g) {
				if (g->IsClippingMask() && g->GetMaskNode() != nullptr) {
					maskGroups.push_back(g);
				}
				for (const auto& c : g->GetChildren()) if (c) collect(*c);
			}
		}
	};
	for (const auto& layer : doc.GetLayers()) if (layer) collect(*layer);

	if (!gradUsers.empty() || !maskGroups.empty()) {
		AppendIndent();
		m_output += "<defs>\n";
		++m_indent;
		WriteGradientDefs(doc);
		WriteClipPathDefs(doc);
		--m_indent;
		AppendIndent();
		m_output += "</defs>\n";
	}

	// 레이어 순회
	for (const auto& layer : doc.GetLayers()) {
		if (layer) {
			layer->Accept(*this);
		}
	}

	--m_indent;
	m_output += "</svg>\n";

	return m_output;
}

bool SvgWriter::WriteToFile(Document& doc, const std::wstring& filePath, const SvgWriteOptions& options) {
	std::string content = WriteString(doc, options);
	FILE* fp = nullptr;
	errno_t err = _wfopen_s(&fp, filePath.c_str(), L"wb");
	if (err != 0 || !fp) {
		return false;
	}

	size_t written = fwrite(content.data(), 1, content.size(), fp);
	fclose(fp);
	return written == content.size();
}

void SvgWriter::Visit(LayerNode& node) {
	AppendIndent();
	m_output += "<g id=\"layer_" + std::to_string(node.GetId()) + "\"";

	if (Meta()) {
		m_output += " inkscape:groupmode=\"layer\"";

		std::string utfName = WideToUtf8(node.GetName());
		if (!utfName.empty()) {
			m_output += " inkscape:label=\"" + EscapeXml(utfName) + "\"";
		}

		m_output += std::string(" m2:locked=\"") + (node.IsLocked() ? "true" : "false") + "\"";
		m_output += std::string(" m2:visible=\"") + (node.IsVisible() ? "true" : "false") + "\"";
	}

	if (node.GetOpacity() < 0.999f) {
		m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	}
	m_output += BuildCommonState(node, false, Meta());
	m_output += BuildCssStyle(node);

	m_output += ">\n";

	++m_indent;
	for (const auto& child : node.GetChildren()) {
		if (child) {
			child->Accept(*this);
		}
	}
	--m_indent;

	AppendIndent();
	m_output += "</g>\n";
}

void SvgWriter::Visit(GroupNode& node) {
	AppendIndent();
	m_output += "<g id=\"group_" + std::to_string(node.GetId()) + "\"";

	if (Meta()) {
		std::string utfName = WideToUtf8(node.GetName());
		if (!utfName.empty()) {
			m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
		}
	}

	if (node.GetOpacity() < 0.999f) {
		m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	}
	m_output += BuildCommonState(node, true, Meta());
	m_output += BuildCssStyle(node);

	const bool isMask = node.IsClippingMask() && (node.GetMaskNode() != nullptr);
	if (isMask) {
		m_output += " clip-path=\"url(#clip_group_" + std::to_string(node.GetId()) + ")\"";
		if (Meta()) {
			m_output += " m2:clipMask=\"true\"";
		}
	}

	const Matrix2D& xf = node.GetTransform();
	if (!IsIdentity(xf)) {
		m_output += " transform=\"matrix(" +
			FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " +
			FormatFloat(xf.m21) + " " + FormatFloat(xf.m22) + " " +
			FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
	}

	m_output += ">\n";

	++m_indent;
	const Node* maskNode = isMask ? node.GetMaskNode() : nullptr;
	for (const auto& child : node.GetChildren()) {
		// 마스크 도형은 <clipPath> 정의로만 출력 (본문에도 쓰면 다른 뷰어에서 보이는 도형으로 그려짐). 읽을 때 clipPath 에서 복원
		if (child && child.get() != maskNode) {
			child->Accept(*this);
		}
	}
	--m_indent;

	AppendIndent();
	m_output += "</g>\n";
}

void SvgWriter::Visit(TextNode& node) {
	AppendIndent();
	m_output += "<text id=\"text_" + std::to_string(node.GetId()) + "\"";
	if (Meta()) {
		std::string utfName = WideToUtf8(node.GetName());
		if (!utfName.empty()) m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
	}
	const float size = node.GetFontSize();
	m_output += " x=\"0\" y=\"" + FormatFloat(size * TextNode::kAscentRatio) + "\"";
	m_output += " font-family=\"" + EscapeXml(WideToUtf8(node.GetFontFamily())) + "\" font-size=\"" + FormatFloat(size) + "\"";
	if (node.IsBold()) m_output += " font-weight=\"bold\"";
	if (node.IsItalic()) m_output += " font-style=\"italic\"";
	if (node.GetAlign() == TextAlign::Center) m_output += " text-anchor=\"middle\"";
	else if (node.GetAlign() == TextAlign::Right) m_output += " text-anchor=\"end\"";

	const Style& st = node.GetStyle();
	if (st.fill.type == PaintType::None) {
		m_output += " fill=\"none\"";
	} else {
		m_output += " fill=\"" + ColorToHex(st.fill.color) + "\"";
		if (st.fill.color.a < 0.999f) m_output += " fill-opacity=\"" + FormatFloat(st.fill.color.a) + "\"";
	}
	if (node.GetOpacity() < 0.999f) m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	m_output += BuildCommonState(node, true, Meta());
	m_output += BuildCssStyle(node);
	const Matrix2D& xf = node.GetTransform();
	if (!IsIdentity(xf)) {
		m_output += " transform=\"matrix(" + FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " + FormatFloat(xf.m21) + " " +
			FormatFloat(xf.m22) + " " + FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
	}
	m_output += " xml:space=\"preserve\">";

	// 여러 줄은 <tspan x dy> 로 (첫 줄은 본문)
	const std::string utf = WideToUtf8(node.GetText());
	size_t pos = 0;
	bool first = true;
	while (pos <= utf.size()) {
		size_t nl = utf.find('\n', pos);
		if (nl == std::string::npos) nl = utf.size();
		const std::string line = EscapeXml(utf.substr(pos, nl - pos));
		if (first) m_output += line;
		else m_output += "<tspan x=\"0\" dy=\"" + FormatFloat(size * TextNode::kLineHeightRatio) + "\">" + line + "</tspan>";
		first = false;
		pos = nl + 1;
		if (nl == utf.size()) break;
	}
	m_output += "</text>\n";
}

void SvgWriter::Visit(PathNode& node) {
	AppendIndent();

	// 네이티브 도형 태그 (옵션): Rect -> <rect rx ry>, Ellipse -> <ellipse>/<circle>. 위치/크기는 로컬 경계 상자에서 얻고 transform 은 그대로 출력
	std::string nativeAttrs;
	const char* nativeTag = nullptr;
	if (m_options.nativeShapes && node.GetPaths().size() == 1 && node.GetPaths()[0].closed) {
		Rect2 lb;
		const ShapeKind k = node.GetShapeKind();
		if ((k == ShapeKind::Rect || k == ShapeKind::Ellipse) && node.GetLocalBounds(lb) && !lb.IsEmpty()) {
			if (k == ShapeKind::Rect) {
				float rx = 0.0f, ry = 0.0f;
				if (const auto* rp = node.GetParam<RectParams>()) { rx = rp->rx; ry = rp->ry; }
				rx = (std::max)(0.0f, (std::min)(rx, lb.Width() / 2)); ry = (std::max)(0.0f, (std::min)(ry, lb.Height() / 2));
				nativeTag = "rect";
				nativeAttrs = " x=\"" + FormatFloat(lb.left) + "\" y=\"" + FormatFloat(lb.top) + "\" width=\"" + FormatFloat(lb.Width()) +
					"\" height=\"" + FormatFloat(lb.Height()) + "\"";
				if (rx > 0.0f || ry > 0.0f) nativeAttrs += " rx=\"" + FormatFloat(rx) + "\" ry=\"" + FormatFloat(ry) + "\"";
			} else {
				const float rx = lb.Width() / 2, ry = lb.Height() / 2;
				if (std::abs(rx - ry) < 1e-3f) {
					nativeTag = "circle";
					nativeAttrs = " cx=\"" + FormatFloat(lb.MidX()) + "\" cy=\"" + FormatFloat(lb.MidY()) + "\" r=\"" + FormatFloat(rx) + "\"";
				} else {
					nativeTag = "ellipse";
					nativeAttrs = " cx=\"" + FormatFloat(lb.MidX()) + "\" cy=\"" + FormatFloat(lb.MidY()) + "\" rx=\"" + FormatFloat(rx) +
						"\" ry=\"" + FormatFloat(ry) + "\"";
				}
			}
		}
	}
	m_output += std::string("<") + (nativeTag ? nativeTag : "path") + " id=\"path_" + std::to_string(node.GetId()) + "\"";

	if (Meta()) {
		std::string utfName = WideToUtf8(node.GetName());
		if (!utfName.empty()) {
			m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
		}
	}

	// 패스 데이터 d
	if (nativeTag) {
		m_output += nativeAttrs;
	} else {
		std::string d = BuildPathData(node);
		m_output += " d=\"" + d + "\"";
	}

	// 스타일
	const Style& st = node.GetStyle();

	// 채우기: 렌더링 가능한 Gradient 는 <defs> 정의를 url(#id) 로 참조, 스톱이 2개 미만이면 렌더러와 같은 대체 색(첫 스톱)으로 출력
	Color fillColor;
	bool filled = false;
	if (st.fill.type == PaintType::Solid)         { fillColor = st.fill.color; filled = true; }
	else if (st.fill.type == PaintType::Gradient) { fillColor = st.fill.gradient.FallbackColor(); filled = true; }
	if (UsesGradientDef(node)) {
		m_output += " fill=\"url(#" + GradientIdFor(node) + ")\"";
	} else if (!filled) {
		m_output += " fill=\"none\"";
	} else {
		m_output += " fill=\"" + ColorToHex(fillColor) + "\"";
		if (fillColor.a < 0.999f) {
			m_output += " fill-opacity=\"" + FormatFloat(fillColor.a) + "\"";
		}
	}

	// Stroke (SVG 속성과 1:1: width/linecap/linejoin/miterlimit/dasharray/dashoffset). 기본값은 생략
	const Stroke& strk = st.stroke;
	if (strk.width > 0.0001f && strk.color.a > 0.0001f) {
		m_output += " stroke=\"" + ColorToHex(strk.color) + "\"";
		m_output += " stroke-width=\"" + FormatFloat(strk.width) + "\"";
		if (strk.color.a < 0.999f) {
			m_output += " stroke-opacity=\"" + FormatFloat(strk.color.a) + "\"";
		}
		if (strk.cap == LineCap::Round)       m_output += " stroke-linecap=\"round\"";
		else if (strk.cap == LineCap::Square) m_output += " stroke-linecap=\"square\"";
		if (strk.join == LineJoin::Round)      m_output += " stroke-linejoin=\"round\"";
		else if (strk.join == LineJoin::Bevel) m_output += " stroke-linejoin=\"bevel\"";
		if (std::abs(strk.miterLimit - 4.0f) > 0.0001f) {
			m_output += " stroke-miterlimit=\"" + FormatFloat(strk.miterLimit) + "\"";
		}
		if (IsValidDashArray(strk.dashArray)) {
			std::string dash;
			for (size_t i = 0; i < strk.dashArray.size(); ++i) {
				if (i) dash += " ";
				dash += FormatFloat(strk.dashArray[i]);
			}
			m_output += " stroke-dasharray=\"" + dash + "\"";
			if (std::abs(strk.dashOffset) > 0.0001f) {
				m_output += " stroke-dashoffset=\"" + FormatFloat(strk.dashOffset) + "\"";
			}
		}
	} else {
		m_output += " stroke=\"none\"";
	}

	if (st.fillMode == FillMode::Alternate) {
		m_output += " fill-rule=\"evenodd\"";
	} else {
		m_output += " fill-rule=\"nonzero\"";
	}

	if (node.GetOpacity() < 0.999f) {
		m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	}
	m_output += BuildCommonState(node, true, Meta());
	m_output += BuildCssStyle(node);

	const Matrix2D& xf = node.GetTransform();
	if (!IsIdentity(xf)) {
		m_output += " transform=\"matrix(" +
			FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " +
			FormatFloat(xf.m21) + " " + FormatFloat(xf.m22) + " " +
			FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
	}

	// Clean(순수 SVG) 은 여기서 종료: 파라메트릭/노드 타입 메타데이터는 M2 전용
	if (!Meta()) {
		m_output += " />\n";
		return;
	}

	// 파라메트릭 도형 메타데이터
	ShapeKind sk = node.GetShapeKind();
	if (sk != ShapeKind::Path) {
		m_output += " m2:shape=\"" + ShapeKindToString(sk) + "\"";

		std::visit([this](const auto& p) {
			using T = std::decay_t<decltype(p)>;
			if constexpr (std::is_same_v<T, RectParams>) {
				m_output += " m2:rx=\"" + FormatFloat(p.rx) + "\"";
				m_output += " m2:ry=\"" + FormatFloat(p.ry) + "\"";
				m_output += std::string(" m2:linkRxRy=\"") + (p.linkRxRy ? "true" : "false") + "\"";
			} else if constexpr (std::is_same_v<T, TriangleParams>) {
				m_output += std::string(" m2:equilateral=\"") + (p.equilateral ? "true" : "false") + "\"";
			} else if constexpr (std::is_same_v<T, EllipseParams>) {
				m_output += std::string(" m2:linkRxRy=\"") + (p.linkRxRy ? "true" : "false") + "\"";
			} else if constexpr (std::is_same_v<T, PolygonParams>) {
				m_output += " m2:sides=\"" + std::to_string(p.sides) + "\"";
				m_output += " m2:radius=\"" + FormatFloat(p.radius) + "\"";
				m_output += " m2:cx=\"" + FormatFloat(p.cx) + "\"";
				m_output += " m2:cy=\"" + FormatFloat(p.cy) + "\"";
			} else if constexpr (std::is_same_v<T, StarParams>) {
				m_output += " m2:points=\"" + std::to_string(p.points) + "\"";
				m_output += " m2:innerRatio=\"" + FormatFloat(p.innerRatio) + "\"";
				m_output += " m2:radius=\"" + FormatFloat(p.radius) + "\"";
				m_output += " m2:cx=\"" + FormatFloat(p.cx) + "\"";
				m_output += " m2:cy=\"" + FormatFloat(p.cy) + "\"";
			} else if constexpr (std::is_same_v<T, GearParams>) {
				m_output += " m2:teeth=\"" + std::to_string(p.teeth) + "\"";
				m_output += " m2:depthRatio=\"" + FormatFloat(p.depthRatio) + "\"";
				m_output += " m2:boreRatio=\"" + FormatFloat(p.boreRatio) + "\"";
				m_output += " m2:toothWidthRatio=\"" + FormatFloat(p.toothWidthRatio) + "\"";
				m_output += " m2:radius=\"" + FormatFloat(p.radius) + "\"";
				m_output += " m2:cx=\"" + FormatFloat(p.cx) + "\"";
				m_output += " m2:cy=\"" + FormatFloat(p.cy) + "\"";
			} else if constexpr (std::is_same_v<T, SpiralParams>) {
				m_output += " m2:turns=\"" + FormatFloat(p.turns) + "\"";
				m_output += " m2:innerRatio=\"" + FormatFloat(p.innerRatio) + "\"";
				m_output += " m2:radius=\"" + FormatFloat(p.radius) + "\"";
				m_output += " m2:cx=\"" + FormatFloat(p.cx) + "\"";
				m_output += " m2:cy=\"" + FormatFloat(p.cy) + "\"";
			}
		}, node.GetParams());
	}

	// 노드 타입
	std::string nodeTypes = BuildNodeTypes(node);
	if (!nodeTypes.empty()) {
		m_output += " m2:node-types=\"" + nodeTypes + "\"";
	}

	m_output += " />\n";
}

} // namespace ge
