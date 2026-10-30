#include "pch.h"
#include "SvgWriter.h"
#include "HtmlComponents/UiComponents.h"
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
	if (!n.GetHtmlTag().empty())     s += " m2:htmlTag=\"" + EscapeXml(n.GetHtmlTag()) + "\"";
	if (!n.GetHref().empty())        s += " m2:href=\"" + EscapeXml(n.GetHref()) + "\"";
	if (!n.GetAlt().empty())         s += " m2:alt=\"" + EscapeXml(n.GetAlt()) + "\"";
	if (!n.GetAriaLabel().empty())   s += " m2:ariaLabel=\"" + EscapeXml(n.GetAriaLabel()) + "\"";
	if (!n.GetCustomClass().empty()) s += " m2:customClass=\"" + EscapeXml(n.GetCustomClass()) + "\"";
	if (n.GetConstraintH() != ConstraintH::Left) s += " m2:cx=\"" + std::to_string(static_cast<int>(n.GetConstraintH())) + "\"";
	if (n.GetConstraintV() != ConstraintV::Top)  s += " m2:cy=\"" + std::to_string(static_cast<int>(n.GetConstraintV())) + "\"";
	if (n.GetShadow().enabled) {
		const Shadow& sd = n.GetShadow();
		s += " m2:shadow=\"" + FormatFloat(sd.x) + "," + FormatFloat(sd.y) + "," + FormatFloat(sd.blur) + "," + FormatFloat(sd.spread) + "," + ColorToHex(sd.color) + "," + FormatFloat(sd.color.a) + "," + (sd.inset ? "1" : "0") + "\"";
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
	if (n.GetShadow().enabled && !n.GetShadow().inset) {
		// 표준 뷰어용 그림자 (안쪽 그림자는 표준 filter 로 표현할 수 없어 m2:shadow 에만 저장) (읽을 때 m2:shadow 가 없으면 이 값을 사용)
		const Shadow& sd = n.GetShadow();
		const int r = static_cast<int>(std::round(sd.color.r * 255.0f)), g = static_cast<int>(std::round(sd.color.g * 255.0f)), b = static_cast<int>(std::round(sd.color.b * 255.0f));
		if (!css.empty()) css += ";";
		css += "filter:drop-shadow(" + FormatFloat(sd.x) + "px " + FormatFloat(sd.y) + "px " + FormatFloat(sd.blur) + "px rgba(" +
			std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + FormatFloat(sd.color.a) + "))";
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
	m_output += "     xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n";   // <image xlink:href> 용 (표준 네임스페이스라 Clean 출력에도 포함)
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

	// 테이블(4.5.7.1): 칸 크기와 테두리 속성 저장 (복원용). 칸 내용은 자식 노드로 저장됨
	if (const auto* table = dynamic_cast<const TableNode*>(&node)) {
		if (Meta()) {
			std::string cols, rows;
			for (size_t i = 0; i < table->ColWidths().size(); ++i) cols += std::string(i ? "," : "") + FormatFloat(table->ColWidths()[i]);
			for (size_t i = 0; i < table->RowHeights().size(); ++i) rows += std::string(i ? "," : "") + FormatFloat(table->RowHeights()[i]);
			m_output += " m2:component=\"table\" m2:cols=\"" + cols + "\" m2:rows=\"" + rows + "\" m2:border=\"" + FormatFloat(table->GetBorderWidth()) +
				"\" m2:pad=\"" + FormatFloat(table->GetCellPadding()) + "\" m2:bcolor=\"" + ColorToHex(table->GetBorderColor()) + "\"";
				if (table->GetBorderStyle() != 0) m_output += " m2:bstyle=\"" + std::to_string(table->GetBorderStyle()) + "\"";
				m_output += " m2:header=\"" + std::to_string(table->GetHeaderRows()) + "\" m2:calign=\"" + std::to_string(table->GetCellAlign()) + "\"";
				if (!table->Merges().empty()) {
					std::string mg;
					for (size_t i = 0; i < table->Merges().size(); ++i) {
						const auto& sp = table->Merges()[i];
						mg += std::string(i ? ";" : "") + std::to_string(sp.r) + "," + std::to_string(sp.c) + "," + std::to_string(sp.rs) + "," + std::to_string(sp.cs);
					}
					m_output += " m2:merges=\"" + mg + "\"";
				}
		}
	}

	const FrameNode* frame = dynamic_cast<const FrameNode*>(&node);
	if (frame) {
		if (Meta()) {
			m_output += " m2:frame=\"true\" m2:w=\"" + FormatFloat(frame->GetWidth()) + "\" m2:h=\"" + FormatFloat(frame->GetHeight()) +
				"\" m2:clip=\"" + std::string(frame->IsClip() ? "true" : "false") + "\" m2:radius=\"" + FormatFloat(frame->GetCornerRadius()) + "\"";
				if (frame->IsUiComponent())
					m_output += " m2:ui=\"" + frame->GetUiKind() + "\" m2:uiProps=\"" + ge::UiComp::EncodeProps(frame->GetUiProps()) + "\"";
				if (!frame->GetCornerRadii().IsUniform()) {
					const CornerRadii& cr4 = frame->GetCornerRadii();
					m_output += " m2:radius4=\"" + FormatFloat(cr4.tl) + "," + FormatFloat(cr4.tr) + "," + FormatFloat(cr4.br) + "," + FormatFloat(cr4.bl) + "\"";
				}
				if (frame->GetBorderSides() != BorderAll) m_output += " m2:borderSides=\"" + std::to_string(static_cast<int>(frame->GetBorderSides())) + "\"";
				if (frame->GetSizingW() == FrameSizingMode::Fill) m_output += " m2:sizingW=\"fill\"";
				if (frame->GetSizingH() == FrameSizingMode::Fill) m_output += " m2:sizingH=\"fill\"";
			if (frame->GetLayoutMode() != FrameLayoutMode::None) {
				m_output += " m2:layout=\"" + std::string(frame->GetLayoutMode() == FrameLayoutMode::Row ? "row" : "column") + "\" m2:gap=\"" +
					FormatFloat(frame->GetGap()) + "\" m2:pad=\"" + FormatFloat(frame->GetPadding()) + "\"";
					if (!frame->GetPaddingEdges().IsUniform()) {
						const PaddingEdges& pe = frame->GetPaddingEdges();
						m_output += " m2:padding4=\"" + FormatFloat(pe.top) + "," + FormatFloat(pe.right) + "," + FormatFloat(pe.bottom) + "," + FormatFloat(pe.left) + "\"";
					}
				static const char* kMainAlignStr[] = { "start", "center", "end", "space-between" };
				static const char* kCrossAlignStr[] = { "start", "center", "end" };
				m_output += " m2:mainAlign=\"" + std::string(kMainAlignStr[static_cast<int>(frame->GetMainAlign())]) + "\"";
				m_output += " m2:crossAlign=\"" + std::string(kCrossAlignStr[static_cast<int>(frame->GetCrossAlign())]) + "\"";
				if (frame->GetSizingW() == FrameSizingMode::Hug) m_output += " m2:sizingW=\"hug\"";
				if (frame->GetSizingH() == FrameSizingMode::Hug) m_output += " m2:sizingH=\"hug\"";
			}
		}
		if (frame->IsClip()) m_output += " clip-path=\"url(#frame_clip_" + std::to_string(node.GetId()) + ")\"";
	}

	const bool isMask = !frame && node.IsClippingMask() && (node.GetMaskNode() != nullptr);
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
	if (const auto* table = dynamic_cast<const TableNode*>(&node)) {
		// 격자선은 표준 path 로 출력 (다른 뷰어에서도 보이게). M2 저장본은 m2:tableGrid 로 표시해 읽을 때 무시
		Rect2 lb{ 0.0f, 0.0f, 0.0f, 0.0f };   // 표 크기 = 열 너비 합 x 행 높이 합 (const 에서 계산)
		for (float v : table->ColWidths()) lb.right += v;
		for (float v : table->RowHeights()) lb.bottom += v;
		{
			std::string d = "M " + FormatFloat(lb.left) + " " + FormatFloat(lb.top) + " H " + FormatFloat(lb.right) + " V " + FormatFloat(lb.bottom) +
				" H " + FormatFloat(lb.left) + " Z";
			std::vector<TableNode::Seg> segs;   // 병합 칸 안쪽 선은 제외된 안쪽 격자선
			table->GridSegments(segs);
			for (const auto& sg : segs)
				d += " M " + FormatFloat(sg.x0) + " " + FormatFloat(sg.y0) + (sg.x0 == sg.x1 ? " V " + FormatFloat(sg.y1) : " H " + FormatFloat(sg.x1));
			if (table->GetHeaderRows() > 0) {
				const Rect2 hl = table->CellRect((std::min)(table->GetHeaderRows(), table->Rows()) - 1, 0);
				AppendIndent();
				m_output += std::string("<path") + (Meta() ? " m2:tableGrid=\"true\"" : "") + " d=\"M 0 0 H " + FormatFloat(lb.right) + " V " + FormatFloat(hl.bottom) +
					" H 0 Z\" fill=\"#eef0f5\" stroke=\"none\" />\n";
			}
			AppendIndent();
			m_output += std::string("<path") + (Meta() ? " m2:tableGrid=\"true\"" : "") + " d=\"" + d + "\" fill=\"none\" stroke=\"" +
				ColorToHex(table->GetBorderColor()) + "\" stroke-width=\"" + FormatFloat(table->GetBorderWidth()) + "\"" +
				(table->GetBorderStyle() == 0 ? std::string() : " stroke-dasharray=\"" + FormatFloat(table->GetBorderWidth() * (table->GetBorderStyle() == 1 ? 4.0f : 1.0f)) + " " +
					FormatFloat(table->GetBorderWidth() * (table->GetBorderStyle() == 1 ? 2.0f : 1.0f)) + "\"") + " />\n";
		}
	}
	if (frame) {
		// 표준 뷰어에서도 보이도록: 클립 영역 + 배경 도형을 그룹 안 첫 요소로 출력 (읽을 때 m2:frameBg 로 프레임 스타일 복원)
		// 4모서리 반경이 같으면 <rect>, 다르면 <path> (4.5.10). 일부 변만 테두리면 배경은 fill 만, 테두리는 별도 <path>
		const CornerRadii cr = frame->ClampedRadii();
		const float FW = frame->GetWidth(), FH = frame->GetHeight();
		const bool partial = frame->GetBorderSides() != BorderAll;
		auto shapeOpen = [&](const std::string& extra) -> std::string {
			if (cr.IsUniform()) {
				return "<rect" + extra + " x=\"0\" y=\"0\" width=\"" + FormatFloat(FW) + "\" height=\"" + FormatFloat(FH) + "\"" +
					(cr.tl > 0.0f ? " rx=\"" + FormatFloat(cr.tl) + "\" ry=\"" + FormatFloat(cr.tl) + "\"" : std::string());
			}
			auto arc = [&](float r, float x, float y) { return r > 0.0f ? " A " + FormatFloat(r) + " " + FormatFloat(r) + " 0 0 1 " + FormatFloat(x) + " " + FormatFloat(y) : std::string(); };
			std::string d = "M " + FormatFloat(cr.tl) + " 0 H " + FormatFloat(FW - cr.tr) + arc(cr.tr, FW, cr.tr) +
				" V " + FormatFloat(FH - cr.br) + arc(cr.br, FW - cr.br, FH) +
				" H " + FormatFloat(cr.bl) + arc(cr.bl, 0.0f, FH - cr.bl) +
				" V " + FormatFloat(cr.tl) + arc(cr.tl, cr.tl, 0.0f) + " Z";
			return "<path" + extra + " d=\"" + d + "\"";
		};
		if (frame->IsClip()) {
			AppendIndent();
			m_output += "<clipPath id=\"frame_clip_" + std::to_string(node.GetId()) + "\">" + shapeOpen("") + " /></clipPath>\n";
		}
		const Style& fs = frame->GetStyle();
		const bool hasFill = fs.fill.type == PaintType::Solid && fs.fill.color.a > 0.0f;
		const bool hasStroke = fs.stroke.width > 0.0f && fs.stroke.color.a > 0.0f && frame->GetBorderSides() != 0;
		if (hasFill || hasStroke || Meta()) {
			AppendIndent();
			m_output += shapeOpen(Meta() ? " m2:frameBg=\"true\"" : "");
			m_output += hasFill ? " fill=\"" + ColorToHex(fs.fill.color) + "\"" + (fs.fill.color.a < 0.999f ? " fill-opacity=\"" + FormatFloat(fs.fill.color.a) + "\"" : std::string()) : std::string(" fill=\"none\"");
			if (hasStroke && !partial) m_output += " stroke=\"" + ColorToHex(fs.stroke.color) + "\" stroke-width=\"" + FormatFloat(fs.stroke.width) + "\"";
			else m_output += " stroke=\"none\"";
			if (Meta() && partial && fs.stroke.width > 0.0f) m_output += " m2:bstroke=\"" + ColorToHex(fs.stroke.color) + "\" m2:bwidth=\"" + FormatFloat(fs.stroke.width) + "\"";
			m_output += " />\n";
		}
		if (hasStroke && partial) {
			const uint8_t sd = frame->GetBorderSides();
			std::string d;
			if (sd & BorderTop)    d += " M 0 0 H " + FormatFloat(FW);
			if (sd & BorderRight)  d += " M " + FormatFloat(FW) + " 0 V " + FormatFloat(FH);
			if (sd & BorderBottom) d += " M 0 " + FormatFloat(FH) + " H " + FormatFloat(FW);
			if (sd & BorderLeft)   d += " M 0 0 V " + FormatFloat(FH);
			AppendIndent();
			m_output += std::string("<path") + (Meta() ? " m2:frameBorder=\"true\"" : "") + " d=\"" + d.substr(1) + "\" fill=\"none\" stroke=\"" + ColorToHex(fs.stroke.color) +
				"\" stroke-width=\"" + FormatFloat(fs.stroke.width) + "\" />\n";
		}
	}
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

static std::string Base64Encode(const std::vector<uint8_t>& in)
{
	static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < in.size(); i += 3) {
		const unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
		out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
	}
	if (i + 1 == in.size()) {
		const unsigned v = in[i] << 16;
		out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += "==";
	} else if (i + 2 == in.size()) {
		const unsigned v = (in[i] << 16) | (in[i + 1] << 8);
		out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += '=';
	}
	return out;
}

void SvgWriter::Visit(ImageNode& node) {
	if (!node.GetData()) return;   // 데이터가 없는 이미지는 출력하지 않음
	AppendIndent();
	m_output += "<image id=\"image_" + std::to_string(node.GetId()) + "\"";
	if (Meta()) {
		std::string utfName = WideToUtf8(node.GetName());
		if (!utfName.empty()) m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
	}
	m_output += " x=\"0\" y=\"0\" width=\"" + FormatFloat(node.GetWidth()) + "\" height=\"" + FormatFloat(node.GetHeight()) + "\" preserveAspectRatio=\"" +
		std::string(node.GetFit() == ImageFit::Fit ? "xMidYMid meet" : node.GetFit() == ImageFit::Fill ? "xMidYMid slice" : "none") + "\"";
	if (node.GetOpacity() < 0.999f) m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	m_output += BuildCommonState(node, true, Meta());
	m_output += BuildCssStyle(node);
	const Matrix2D& xf = node.GetTransform();
	if (!IsIdentity(xf)) {
		m_output += " transform=\"matrix(" + FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " + FormatFloat(xf.m21) + " " +
			FormatFloat(xf.m22) + " " + FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
	}
	m_output += " xlink:href=\"data:" + node.GetData()->mime + ";base64," + Base64Encode(node.GetData()->bytes) + "\" />\n";
}

static ID2D1Factory* SharedD2DFactory()
{
	static ComPtr<ID2D1Factory> f;
	if (!f) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, f.GetAddressOf());
	return f.Get();
}

// 글꼴 스택: 지정한 글꼴이 없는 환경에서 대체되도록 마지막에 일반 계열(serif/monospace/sans-serif)을 덧붙임
static std::string FontStack(const std::wstring& family)
{
	std::wstring lower = family;
	for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
	static const wchar_t* serifs[] = { L"times", L"georgia", L"garamond", L"batang", L"gungsuh", L"palatino", L"cambria", L"serif" };
	static const wchar_t* monos[] = { L"courier", L"consolas", L"mono", L"lucida console", L"gulimche", L"monospace" };
	const char* generic = "sans-serif";
	for (const wchar_t* k : monos) if (lower.find(k) != std::wstring::npos) generic = "monospace";
	if (std::string(generic) == "sans-serif")
		for (const wchar_t* k : serifs) if (lower.find(k) != std::wstring::npos && lower.find(L"sans") == std::wstring::npos) generic = "serif";
	return "'" + EscapeXml(WideToUtf8(family)) + "', " + generic;   // 작은따옴표는 큰따옴표 속성 안이라 그대로 둠 (이름 안의 특수문자만 이스케이프)
}

void SvgWriter::Visit(TextNode& node) {
	// 텍스트 윤곽선 출력: 글리프를 패스로 바꿔 그 패스를 출력 (글꼴/DirectWrite 와 무관하게 같은 모양)
	if (m_options.textAsOutlines) {
		if (auto path = PathOp::TextToPath(node, SharedD2DFactory())) {
			path->SetName(node.GetName());
			path->SetVisible(node.IsVisible());
			path->SetLocked(node.IsLocked());
			path->SetBlendMode(node.GetBlendMode());
			const size_t from = m_output.size();
			path->Accept(*this);
			// 임시 패스의 id(path_0) 가 여러 텍스트에서 겹치지 않도록 텍스트 id 로 교체
			const size_t at = m_output.find("path_0\"", from);
			if (at != std::string::npos) m_output.replace(at, 6, "text_" + std::to_string(node.GetId()));
			return;
		}
	}
	AppendIndent();
	m_output += "<text id=\"text_" + std::to_string(node.GetId()) + "\"";
	if (Meta()) {
		std::string utfName = WideToUtf8(node.GetName());
		if (!utfName.empty()) m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
	}
	const float size = node.GetFontSize();
	// 영역 텍스트: 정렬 기준 x 는 상자 안(왼쪽 0 / 가운데 boxW/2 / 오른쪽 boxW), 점 텍스트는 0
	const bool area = node.IsAreaText();
	const float anchorX = !area ? 0.0f : node.GetAlign() == TextAlign::Center ? node.GetBoxWidth() * 0.5f : node.GetAlign() == TextAlign::Right ? node.GetBoxWidth() : 0.0f;
	const std::string axs = FormatFloat(anchorX);
	if (area && Meta()) {
		// 원본 문자열(자동 줄바꿈 전)을 보관: 읽을 때 이것으로 복원 (개행은 &#10; 로 보존)
		std::string raw = EscapeXml(WideToUtf8(node.GetText()));
		std::string enc;
		for (char c : raw) { if (c == '\n') enc += "&#10;"; else enc += c; }
		m_output += " m2:boxW=\"" + FormatFloat(node.GetBoxWidth()) + "\" m2:text=\"" + enc + "\"";
	}
	m_output += " x=\"" + axs + "\" y=\"" + FormatFloat(size * TextNode::kAscentRatio) + "\"";
	m_output += " font-family=\"" + FontStack(node.GetFontFamily()) + "\" font-size=\"" + FormatFloat(size) + "\"";
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

	// 여러 줄은 <tspan x dy> 로 (첫 줄은 본문). 영역 텍스트는 자동 줄바꿈 결과(줄 단위)로 출력해 SVG 뷰어에서도 같은 줄 나눔
	std::vector<std::string> lines;
	if (area) {
		for (const auto& l : PathOp::WrapLines(node)) lines.push_back(WideToUtf8(l));
	} else {
		const std::string utf = WideToUtf8(node.GetText());
		size_t pos = 0;
		while (pos <= utf.size()) {
			size_t nl = utf.find('\n', pos);
			if (nl == std::string::npos) nl = utf.size();
			lines.push_back(utf.substr(pos, nl - pos));
			pos = nl + 1;
			if (nl == utf.size()) break;
		}
	}
	for (size_t i = 0; i < lines.size(); ++i) {
		const std::string line = EscapeXml(lines[i]);
		if (i == 0) m_output += line;
		else m_output += "<tspan x=\"" + axs + "\" dy=\"" + FormatFloat(size * TextNode::kLineHeightRatio) + "\">" + line + "</tspan>";
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
