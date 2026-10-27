#include "pch.h"
#include "SvgWriter.h"
#include "D2DGraphicEngine.h"
#include "D2DLayer.h"

#include <charconv>
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

// 기본값이 아닌 공통 상태(보이기/잠금/혼합 모드)를 속성 문자열로 출력.
// 레이어는 기존 형식(항상 locked/visible 출력)을 유지하므로 includeVisibleLocked=false 로 호출
std::string BuildCommonState(const Node& n, bool includeVisibleLocked) {
	std::string s;
	if (includeVisibleLocked) {
		if (!n.IsVisible()) s += " m2:visible=\"false\" style=\"display:none\"";
		if (n.IsLocked())   s += " m2:locked=\"true\"";
	}
	if (n.GetBlendMode() != LayerBlendMode::Normal) {
		s += " m2:blend=\"" + std::to_string(static_cast<int>(n.GetBlendMode())) + "\"";
	}
	return s;
}

} // anonymous namespace

SvgWriter::SvgWriter() = default;

void SvgWriter::AppendIndent() {
	for (int i = 0; i < m_indent; ++i) {
		m_output += "  ";
	}
}

std::string SvgWriter::WriteString(Document& doc) {
	m_output.clear();
	m_indent = 0;

	// XML 선언 및 SVG 루트 시작
	m_output += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	m_output += "<svg xmlns=\"http://www.w3.org/2000/svg\"\n";
	m_output += "     xmlns:m2=\"https://m2draw/ns\"\n";
	m_output += "     xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\"\n";
	m_output += "     xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\"\n";
	m_output += "     width=\"" + FormatFloat(doc.width) + "\" height=\"" + FormatFloat(doc.height) + "\"\n";
	m_output += "     viewBox=\"0 0 " + FormatFloat(doc.width) + " " + FormatFloat(doc.height) + "\"\n";
	m_output += "     m2:backgroundColor=\"" + ColorToHex(doc.backgroundColor) + "\"\n";
	if (doc.backgroundColor.a < 0.999f) {
		m_output += "     m2:backgroundOpacity=\"" + FormatFloat(doc.backgroundColor.a) + "\"\n";
	}
	m_output += "     m2:canvasColor=\"" + ColorToHex(doc.canvasColor) + "\"\n";
	m_output += std::string("     m2:originMode=\"") + (doc.originMode == CanvasOriginMode::BottomLeft ? "bottomLeft" : "topLeft") + "\">\n";

	++m_indent;

	AppendIndent();
	m_output += "<sodipodi:namedview id=\"namedview_canvas\" pagecolor=\"" + ColorToHex(doc.backgroundColor) + "\" />\n";

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

bool SvgWriter::WriteToFile(Document& doc, const std::wstring& filePath) {
	std::string content = WriteString(doc);
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
	m_output += " inkscape:groupmode=\"layer\"";

	std::string utfName = WideToUtf8(node.GetName());
	if (!utfName.empty()) {
		m_output += " inkscape:label=\"" + EscapeXml(utfName) + "\"";
	}

	m_output += std::string(" m2:locked=\"") + (node.IsLocked() ? "true" : "false") + "\"";
	m_output += std::string(" m2:visible=\"") + (node.IsVisible() ? "true" : "false") + "\"";

	if (node.IsClippingMask()) {
		m_output += " m2:clipMask=\"true\"";
	}

	if (!node.IsVisible()) {
		m_output += " style=\"display:none\"";
	}

	if (node.GetOpacity() < 0.999f) {
		m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	}
	m_output += BuildCommonState(node, false);

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

	std::string utfName = WideToUtf8(node.GetName());
	if (!utfName.empty()) {
		m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
	}

	if (node.GetOpacity() < 0.999f) {
		m_output += " opacity=\"" + FormatFloat(node.GetOpacity()) + "\"";
	}
	m_output += BuildCommonState(node, true);

	const Matrix2D& xf = node.GetTransform();
	if (!IsIdentity(xf)) {
		m_output += " transform=\"matrix(" +
			FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " +
			FormatFloat(xf.m21) + " " + FormatFloat(xf.m22) + " " +
			FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
	}

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

void SvgWriter::Visit(PathNode& node) {
	AppendIndent();
	m_output += "<path id=\"path_" + std::to_string(node.GetId()) + "\"";

	std::string utfName = WideToUtf8(node.GetName());
	if (!utfName.empty()) {
		m_output += " m2:name=\"" + EscapeXml(utfName) + "\"";
	}

	// 패스 데이터 d
	std::string d = BuildPathData(node);
	m_output += " d=\"" + d + "\"";

	// 스타일
	const Style& st = node.GetStyle();
	if (st.fillType == FillType::None) {
		m_output += " fill=\"none\"";
	} else {
		m_output += " fill=\"" + ColorToHex(st.fill) + "\"";
		if (st.fill.a < 0.999f) {
			m_output += " fill-opacity=\"" + FormatFloat(st.fill.a) + "\"";
		}
	}

	// Stroke
	if (st.strokeWidth > 0.0001f && st.stroke.a > 0.0001f) {
		m_output += " stroke=\"" + ColorToHex(st.stroke) + "\"";
		m_output += " stroke-width=\"" + FormatFloat(st.strokeWidth) + "\"";
		if (st.stroke.a < 0.999f) {
			m_output += " stroke-opacity=\"" + FormatFloat(st.stroke.a) + "\"";
		}

		switch (st.dashStyle.ToDashStyle()) {
		case DashStyle::Dash:
			m_output += " stroke-dasharray=\"8 4\"";
			break;
		case DashStyle::Dot:
			m_output += " stroke-dasharray=\"2 2\"";
			break;
		case DashStyle::DashDot:
			m_output += " stroke-dasharray=\"8 4 2 4\"";
			break;
		case DashStyle::DashDotDot:
			m_output += " stroke-dasharray=\"8 4 2 4 2 4\"";
			break;
		default:
			break;
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
	m_output += BuildCommonState(node, true);

	const Matrix2D& xf = node.GetTransform();
	if (!IsIdentity(xf)) {
		m_output += " transform=\"matrix(" +
			FormatFloat(xf.m11) + " " + FormatFloat(xf.m12) + " " +
			FormatFloat(xf.m21) + " " + FormatFloat(xf.m22) + " " +
			FormatFloat(xf.dx) + " " + FormatFloat(xf.dy) + ")\"";
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
