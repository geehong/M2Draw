#include "pch.h"
#include "HtmlExporter.h"
#include "SvgWriter.h"
#include "HtmlComponents/TableComponent.h"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>

namespace ge {

static std::string EscapeHtml(const std::string& str) {
	std::string res;
	res.reserve(str.size());
	for (char c : str) {
		switch (c) {
		case '&':  res += "&amp;"; break;
		case '<':  res += "&lt;"; break;
		case '>':  res += "&gt;"; break;
		case '"':  res += "&quot;"; break;
		case '\'': res += "&#39;"; break;
		default:   res += c; break;
		}
	}
	return res;
}

static std::string WideToUtf8(const std::wstring& w) {
	if (w.empty()) return "";
	int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(size, 0);
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), size, nullptr, nullptr);
	return s;
}

static std::string SanitizeClassName(const std::string& name) {
	std::string res;
	for (char c : name) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
			res += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		} else if (c == ' ' || c == '.' || c == '/') {
			if (!res.empty() && res.back() != '-') res += '-';
		}
	}
	while (!res.empty() && res.back() == '-') res.pop_back();
	if (res.empty() || std::isdigit(static_cast<unsigned char>(res[0]))) {
		res = "cls-" + res;
	}
	return res;
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

// 테두리 종류: 대시 설정을 CSS border-style 로 (4.5.17)
static const char* CssBorderStyleName(const Stroke& s)
{
	switch (ClassifyDashArray(s.dashArray, s.width)) {
	case DashStyle::Solid: return "solid";
	case DashStyle::Dot:   return "dotted";
	default:               return "dashed";
	}
}

HtmlExporter::HtmlExporter() = default;

std::string HtmlExporter::FormatUnit(float pxVal) const {
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2);
	if (m_options.useRemUnits && m_options.baseFontSize > 0.001f) {
		oss << (pxVal / m_options.baseFontSize) << "rem";
	} else {
		oss << pxVal << "px";
	}
	std::string s = oss.str();
	// 소수점 끝 .00 정리
	if (s.find('.') != std::string::npos) {
		size_t pos = s.find("rem");
		if (pos == std::string::npos) pos = s.find("px");
		std::string unit = s.substr(pos);
		std::string num = s.substr(0, pos);
		while (num.back() == '0') num.pop_back();
		if (num.back() == '.') num.pop_back();
		s = num + unit;
	}
	return s;
}

std::string HtmlExporter::ColorToCss(const Color& c) const {
	std::ostringstream oss;
	if (c.a >= 0.999f) {
		char buf[16];
		snprintf(buf, sizeof(buf), "#%02x%02x%02x",
			(int)std::clamp(std::round(c.r * 255.0f), 0.0f, 255.0f),
			(int)std::clamp(std::round(c.g * 255.0f), 0.0f, 255.0f),
			(int)std::clamp(std::round(c.b * 255.0f), 0.0f, 255.0f));
		return std::string(buf);
	}
	oss << "rgba("
		<< (int)std::clamp(std::round(c.r * 255.0f), 0.0f, 255.0f) << ", "
		<< (int)std::clamp(std::round(c.g * 255.0f), 0.0f, 255.0f) << ", "
		<< (int)std::clamp(std::round(c.b * 255.0f), 0.0f, 255.0f) << ", "
		<< std::fixed << std::setprecision(2) << c.a << ")";
	return oss.str();
}

void HtmlExporter::AppendIndent() {
	for (int i = 0; i < m_indent; ++i) m_htmlStream << "  ";
}

std::string HtmlExporter::RegisterCssRule(const std::string& desiredPrefix, const std::string& decls, const Node* pNode) {
	if (!m_options.extractCssClasses) {
		return ""; // 인라인 모드
	}
	// 중복 스타일 선언이 있는지 확인
	auto it = m_styleToClass.find(decls);
	if (it != m_styleToClass.end()) {
		return it->second;
	}

	std::string cls;
	if (pNode && !pNode->GetCustomClass().empty()) {
		cls = SanitizeClassName(pNode->GetCustomClass());
	} else if (pNode && !pNode->GetName().empty()) {
		cls = SanitizeClassName(WideToUtf8(pNode->GetName()));
	} else {
		cls = desiredPrefix + "-" + std::to_string(m_classSeq++);
	}

	// 클래스 중복 방지 접미사
	std::string uniqueCls = cls;
	int dupSeq = 1;
	for (const auto& pair : m_cssRules) {
		if (pair.first == uniqueCls) {
			uniqueCls = cls + "-" + std::to_string(++dupSeq);
		}
	}

	m_styleToClass[decls] = uniqueCls;
	m_cssRules.push_back({ uniqueCls, decls });
	return uniqueCls;
}

std::string HtmlExporter::PositionDecls(const Node& node, bool& omitW, bool& omitH)
{
	omitW = omitH = false;
	auto* pf = dynamic_cast<FrameNode*>(node.GetParent());
	if (!pf || pf->GetLayoutMode() != FrameLayoutMode::None) return "";

	const Matrix2D& xf = node.GetTransform();
	Rect2 lb{};
	Rect2 b{ xf.dx, xf.dy, xf.dx, xf.dy };
	if (const_cast<Node&>(node).GetLocalBounds(lb)) b = xf.TransformRect(lb);
	const float pw = pf->GetWidth(), ph = pf->GetHeight();

	std::ostringstream s;
	s << "position: absolute; ";
	std::string tfm;
	switch (node.GetConstraintH()) {
	case ConstraintH::Left:      s << "left: " << FormatUnit(xf.dx) << "; "; break;
	case ConstraintH::Right:     s << "right: " << FormatUnit(pw - b.right) << "; "; break;
	case ConstraintH::LeftRight: s << "left: " << FormatUnit(b.left) << "; right: " << FormatUnit(pw - b.right) << "; "; omitW = true; break;
	case ConstraintH::Center:    s << "left: calc(50% + " << FormatUnit(b.left + b.Width() * 0.5f - pw * 0.5f) << "); "; tfm += "translateX(-50%) "; break;
	}
	switch (node.GetConstraintV()) {
	case ConstraintV::Top:       s << "top: " << FormatUnit(xf.dy) << "; "; break;
	case ConstraintV::Bottom:    s << "bottom: " << FormatUnit(ph - b.bottom) << "; "; break;
	case ConstraintV::TopBottom: s << "top: " << FormatUnit(b.top) << "; bottom: " << FormatUnit(ph - b.bottom) << "; "; omitH = true; break;
	case ConstraintV::Center:    s << "top: calc(50% + " << FormatUnit(b.top + b.Height() * 0.5f - ph * 0.5f) << "); "; tfm += "translateY(-50%) "; break;
	}
	if (!tfm.empty()) s << "transform: " << tfm << "; ";
	return s.str();
}

std::string HtmlExporter::BuildCommonStyleDecls(const Node& node) {
	std::ostringstream ss;
	if (!node.IsVisible()) ss << "display: none; ";
	if (node.GetOpacity() < 0.999f) {
		ss << "opacity: " << std::fixed << std::setprecision(2) << node.GetOpacity() << "; ";
	}
	if (node.GetBlendMode() != LayerBlendMode::Normal) {
		static const char* kBlends[] = { "normal", "multiply", "screen", "overlay", "darken", "lighten",
			"color-dodge", "color-burn", "hard-light", "soft-light", "difference", "exclusion", "hue", "saturation", "color", "luminosity" };
		int bIdx = static_cast<int>(node.GetBlendMode());
		if (bIdx >= 0 && bIdx < 16) {
			ss << "mix-blend-mode: " << kBlends[bIdx] << "; ";
		}
	}

	// 그림자 (4.5.17): 요소 종류에 맞는 CSS 로 변환 — 글자 = text-shadow, 벡터 <svg> = filter: drop-shadow, 그 외(프레임/이미지/표/사각형/원) = box-shadow
	const Shadow& sd = node.GetShadow();
	if (sd.enabled && sd.color.a > 0.001f) {
		const std::string off = FormatUnit(sd.x) + " " + FormatUnit(sd.y) + " " + FormatUnit(sd.blur);
		const std::string col = ColorToCss(sd.color);
		const auto* pn = dynamic_cast<const PathNode*>(&node);
		const bool vectorSvg = pn && pn->GetShapeKind() != ShapeKind::Rect && pn->GetShapeKind() != ShapeKind::Ellipse;
		if (node.Kind() == NodeKind::Text) ss << "text-shadow: " << off << " " << col << "; ";
		else if (vectorSvg) ss << "filter: drop-shadow(" << off << " " << col << "); ";
		else ss << "box-shadow: " << (sd.inset ? "inset " : "") << off << " " << FormatUnit(sd.spread) << " " << col << "; ";
	}
	return ss.str();
}

HtmlExportResult HtmlExporter::Export(Document& doc, const HtmlExportOptions& options) {
	m_options = options;
	m_result = HtmlExportResult();
	m_styleToClass.clear();
	m_cssRules.clear();
	m_classSeq = 1;
	m_indent = 0;
	m_htmlStream.str("");
	m_htmlStream.clear();

	// 1. 트리 순회 및 본문 태그 생성
	if (options.targetArtboard) {
		const_cast<FrameNode*>(options.targetArtboard)->Accept(*this);
	} else {
		for (const auto& layer : doc.GetLayers()) {
			if (layer) layer->Accept(*this);
		}
	}

	std::string bodyHtml = m_htmlStream.str();

	// 2. CSS 스타일 시트 생성
	std::ostringstream cssStream;
	// 리셋 및 기본 반응형 박스 사이징
	cssStream << "*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }\n";
	cssStream << "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }\n\n";

	for (const auto& r : m_cssRules) {
		cssStream << "." << r.first << " {\n";
		std::istringstream declStream(r.second);
		std::string decl;
		while (std::getline(declStream, decl, ';')) {
			// 앞뒤 공백 제거
			while (!decl.empty() && std::isspace(static_cast<unsigned char>(decl.front()))) decl.erase(decl.begin());
			while (!decl.empty() && std::isspace(static_cast<unsigned char>(decl.back()))) decl.pop_back();
			if (!decl.empty()) {
				cssStream << "  " << decl << ";\n";
			}
		}
		cssStream << "}\n\n";
	}

	m_result.css = cssStream.str();

	// 3. 완전한 HTML 문서 조합
	std::ostringstream fullDoc;
	fullDoc << "<!DOCTYPE html>\n";
	fullDoc << "<html lang=\"ko\">\n<head>\n";
	fullDoc << "  <meta charset=\"UTF-8\">\n";
	fullDoc << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
	fullDoc << "  <title>" << EscapeHtml(m_options.title) << "</title>\n";

	if (m_options.singleFile) {
		fullDoc << "  <style>\n" << m_result.css << "  </style>\n";
	} else {
		fullDoc << "  <link rel=\"stylesheet\" href=\"styles.css\">\n";
	}
	fullDoc << "</head>\n<body>\n";
	fullDoc << bodyHtml;
	fullDoc << "</body>\n</html>\n";

	m_result.html = fullDoc.str();
	return m_result;
}

std::string HtmlExporter::ExportToString(Document& doc, const HtmlExportOptions& options) {
	return Export(doc, options).html;
}

bool HtmlExporter::ExportToFile(Document& doc, const std::wstring& htmlFilePath, const HtmlExportOptions& options) {
	HtmlExportResult res = Export(doc, options);
	std::filesystem::path outPath(htmlFilePath);
	std::filesystem::create_directories(outPath.parent_path());

	// HTML 저장
	std::ofstream hFile(outPath, std::ios::out | std::ios::binary);
	if (!hFile.is_open()) return false;
	hFile.write(res.html.data(), res.html.size());
	hFile.close();

	// 분리 모드일 때 CSS 및 assets 저장
	if (!options.singleFile) {
		std::filesystem::path cssPath = outPath.parent_path() / "styles.css";
		std::ofstream cFile(cssPath, std::ios::out | std::ios::binary);
		if (cFile.is_open()) {
			cFile.write(res.css.data(), res.css.size());
			cFile.close();
		}
		if (!res.assets.empty()) {
			std::filesystem::path assetsDir = outPath.parent_path() / "assets";
			std::filesystem::create_directories(assetsDir);
			for (const auto& ast : res.assets) {
				std::ofstream aFile(assetsDir / ast.first, std::ios::out | std::ios::binary);
				if (aFile.is_open()) {
					aFile.write(reinterpret_cast<const char*>(ast.second.data()), ast.second.size());
					aFile.close();
				}
			}
		}
	}
	return true;
}

void HtmlExporter::Visit(LayerNode& node) {
	if (!node.IsVisible()) return;
	for (const auto& c : node.GetChildren()) {
		if (c) c->Accept(*this);
	}
}

void HtmlExporter::Visit(GroupNode& node) {
	if (!node.IsVisible()) return;

	// 테이블 컴포넌트: 시맨틱 <table> 로 변환 (4.5.7.6)
	if (auto* table = dynamic_cast<TableNode*>(&node)) {
		ExportTable(*table);
		return;
	}

	// FrameNode 인 경우 시맨틱 Flexbox/컨테이너로 변환
	if (auto* frame = dynamic_cast<FrameNode*>(&node)) {
		std::string tag = !frame->GetHtmlTag().empty() ? frame->GetHtmlTag() : "div";
		std::ostringstream style;
		style << BuildCommonStyleDecls(*frame);

		// 자유 배치 부모 안이면 absolute + 제약, 자동 배치(flex) 부모 안이면 Fill 을 flex/stretch 로 출력 (4.5.9, 4.5.11)
		bool omitW = false, omitH = false;
		const std::string pos = PositionDecls(*frame, omitW, omitH);
		style << pos;
		auto* parentFrame = dynamic_cast<FrameNode*>(frame->GetParent());
		const bool inFlex = parentFrame && parentFrame->GetLayoutMode() != FrameLayoutMode::None;
		const bool parentRow = inFlex && parentFrame->GetLayoutMode() == FrameLayoutMode::Row;
		const bool fillW = frame->GetSizingW() == FrameSizingMode::Fill, fillH = frame->GetSizingH() == FrameSizingMode::Fill;
		if (inFlex) {
			if (fillW) style << (parentRow ? "flex: 1 1 0%; min-width: 0; " : "align-self: stretch; ");
			if (fillH) style << (parentRow ? "align-self: stretch; " : "flex: 1 1 0%; min-height: 0; ");
		}
		else if (pos.empty()) {
			if (fillW) style << "width: 100%; ";
			if (fillH) style << "height: 100%; ";
		}
		// 크기 및 클리핑
		if (!omitW && !(fillW && (inFlex || pos.empty()))) style << "width: " << FormatUnit(frame->GetWidth()) << "; ";
		if (!omitH && !(fillH && (inFlex || pos.empty()))) style << "height: " << FormatUnit(frame->GetHeight()) << "; ";
		if (frame->IsClip()) style << "overflow: hidden; ";
		if (!frame->GetCornerRadii().IsZero()) {
			const CornerRadii& cr = frame->GetCornerRadii();
			if (cr.IsUniform()) style << "border-radius: " << FormatUnit(cr.tl) << "; ";
			else style << "border-radius: " << FormatUnit(cr.tl) << " " << FormatUnit(cr.tr) << " " << FormatUnit(cr.br) << " " << FormatUnit(cr.bl) << "; ";
		}

		// 배경 및 테두리 (일부 변만 지정되면 border-top/right/bottom/left)
		const Style& st = frame->GetStyle();
		if (st.fill.type == PaintType::Solid && st.fill.color.a > 0.001f) {
			style << "background-color: " << ColorToCss(st.fill.color) << "; ";
		}
		if (st.stroke.width > 0.001f && st.stroke.color.a > 0.001f && frame->GetBorderSides() != 0) {
			const std::string bdr = FormatUnit(st.stroke.width) + " " + CssBorderStyleName(st.stroke) + " " + ColorToCss(st.stroke.color) + "; ";
			const uint8_t sd = frame->GetBorderSides();
			if (sd == BorderAll) style << "border: " << bdr;
			else {
				if (sd & BorderTop)    style << "border-top: " << bdr;
				if (sd & BorderRight)  style << "border-right: " << bdr;
				if (sd & BorderBottom) style << "border-bottom: " << bdr;
				if (sd & BorderLeft)   style << "border-left: " << bdr;
			}
		}

		// Flexbox Auto Layout 또는 Free Layout
		if (frame->GetLayoutMode() != FrameLayoutMode::None) {
			style << "display: flex; ";
			style << (frame->GetLayoutMode() == FrameLayoutMode::Row ? "flex-direction: row; " : "flex-direction: column; ");
			style << "gap: " << FormatUnit(frame->GetGap()) << "; ";
			const PaddingEdges& pe = frame->GetPaddingEdges();
			if (pe.IsUniform()) style << "padding: " << FormatUnit(pe.top) << "; ";
			else style << "padding: " << FormatUnit(pe.top) << " " << FormatUnit(pe.right) << " " << FormatUnit(pe.bottom) << " " << FormatUnit(pe.left) << "; ";

			// 주축 정렬 (justify-content)
			switch (frame->GetMainAlign()) {
			case LayoutMainAlign::Start:        style << "justify-content: flex-start; "; break;
			case LayoutMainAlign::Center:       style << "justify-content: center; "; break;
			case LayoutMainAlign::End:          style << "justify-content: flex-end; "; break;
			case LayoutMainAlign::SpaceBetween: style << "justify-content: space-between; "; break;
			}
			// 교차축 정렬 (align-items)
			switch (frame->GetCrossAlign()) {
			case LayoutCrossAlign::Start:       style << "align-items: flex-start; "; break;
			case LayoutCrossAlign::Center:      style << "align-items: center; "; break;
			case LayoutCrossAlign::End:         style << "align-items: flex-end; "; break;
			}
		} else {
			// Free Layout: 자식들은 absolute 좌표로 배치되므로 컨테이너는 relative
			if (pos.empty()) style << "position: relative; ";
		}

		std::string cls = RegisterCssRule("frame", style.str(), frame);

		AppendIndent();
		bool isLink = !frame->GetHref().empty();
		if (isLink) {
			m_htmlStream << "<a href=\"" << EscapeHtml(frame->GetHref()) << "\" style=\"text-decoration: none; color: inherit;\">\n";
			++m_indent;
			AppendIndent();
		}

		m_htmlStream << "<" << tag;
		if (!cls.empty()) m_htmlStream << " class=\"" << cls << "\"";
		else if (!style.str().empty()) m_htmlStream << " style=\"" << style.str() << "\"";
		if (!frame->GetAriaLabel().empty()) m_htmlStream << " aria-label=\"" << EscapeHtml(frame->GetAriaLabel()) << "\"";
		m_htmlStream << ">\n";

		++m_indent;
		for (const auto& c : frame->GetChildren()) {
			if (c) c->Accept(*this);
		}
		--m_indent;

		AppendIndent();
		m_htmlStream << "</" << tag << ">\n";
		if (isLink) {
			--m_indent;
			AppendIndent();
			m_htmlStream << "</a>\n";
		}
		return;
	}

	// 일반 그룹: 내부 도형들을 인라인 SVG 로 묶거나 position:relative <div> 컨테이너로 순회
	AppendIndent();
	m_htmlStream << "<div class=\"group\" style=\"position: relative; " << BuildCommonStyleDecls(node) << "\">\n";
	++m_indent;
	for (const auto& c : node.GetChildren()) {
		if (c) c->Accept(*this);
	}
	--m_indent;
	AppendIndent();
	m_htmlStream << "</div>\n";
}

std::string HtmlExporter::StyleOrClassAttr(const std::string& prefix, const std::string& decls, const Node* node)
{
	if (decls.empty()) return "";
	const std::string cls = RegisterCssRule(prefix, decls, node);
	if (!cls.empty()) return " class=\"" + cls + "\"";
	return " style=\"" + decls + "\"";
}

// 칸 안의 글자들을 HTML 로 (줄바꿈 \n -> <br>, 여러 글자 노드는 <br> 로 구분)
static std::string CellContentHtml(const std::vector<TextNode*>& texts)
{
	std::string out;
	for (size_t i = 0; i < texts.size(); ++i) {
		if (i) out += "<br>";
		const std::string u = WideToUtf8(texts[i]->GetText());
		for (char c : u) {
			if (c == '\n') out += "<br>";
			else if (c == '&') out += "&amp;";
			else if (c == '<') out += "&lt;";
			else if (c == '>') out += "&gt;";
			else out += c;
		}
	}
	return out;
}

void HtmlExporter::ExportTable(TableNode& t)
{
	const int R = t.Rows(), C = t.Cols();
	float totalW = 0.0f, totalH = 0.0f;
	for (float v : t.ColWidths()) totalW += v;
	for (float v : t.RowHeights()) totalH += v;
	(void)totalH;

	static const char* kAlign[] = { "left", "center", "right" };
	const Color bc = t.GetBorderColor();
	std::ostringstream cell;
	cell << "padding: " << FormatUnit(t.GetCellPadding()) << "; text-align: " << kAlign[t.GetCellAlign()] << "; vertical-align: top; ";
	if (t.GetBorderWidth() > 0.001f) cell << "border: " << FormatUnit(t.GetBorderWidth()) << " " << (t.GetBorderStyle() == 1 ? "dashed" : t.GetBorderStyle() == 2 ? "dotted" : "solid") << " " << ColorToCss(bc) << "; ";
	const std::string cellAttr = StyleOrClassAttr("cell", cell.str(), nullptr);

	std::ostringstream hcell;
	hcell << cell.str() << "background-color: #eef0f5; font-weight: bold; ";
	const std::string headAttr = StyleOrClassAttr("cell-head", hcell.str(), nullptr);

	if (m_options.tableAsGrid) {
		// CSS Grid: 칸은 div, 병합은 grid-column / grid-row span
		std::ostringstream gs;
		gs << BuildCommonStyleDecls(t) << "display: grid; width: " << FormatUnit(totalW) << "; grid-template-columns:";
		for (float w : t.ColWidths()) gs << " " << FormatUnit(w);
		gs << "; grid-template-rows:";
		for (float h : t.RowHeights()) gs << " " << FormatUnit(h);
		gs << "; ";
		AppendIndent();
		m_htmlStream << "<div role=\"table\"" << StyleOrClassAttr("table", gs.str(), &t) << ">\n";
		++m_indent;
		for (int r = 0; r < R; ++r) {
			for (int c = 0; c < C; ++c) {
				const TableNode::Span sp = t.SpanOf(r, c);
				if (sp.r != r || sp.c != c) continue;
				const bool head = r < t.GetHeaderRows();
				std::ostringstream pos;
				pos << " style=\"grid-column: " << (c + 1) << " / span " << sp.cs << "; grid-row: " << (r + 1) << " / span " << sp.rs << ";\"";
				AppendIndent();
				m_htmlStream << "<div role=\"" << (head ? "columnheader" : "cell") << "\"" << (head ? headAttr : cellAttr) << pos.str() << ">"
					<< CellContentHtml(TableComponent::CellTexts(t, r, c)) << "</div>\n";
			}
		}
		--m_indent;
		AppendIndent();
		m_htmlStream << "</div>\n";
		return;
	}

	std::ostringstream ts;
	ts << BuildCommonStyleDecls(t) << "border-collapse: collapse; table-layout: fixed; width: " << FormatUnit(totalW) << "; ";
	AppendIndent();
	m_htmlStream << "<table" << StyleOrClassAttr("table", ts.str(), &t) << ">\n";
	++m_indent;
	AppendIndent();
	m_htmlStream << "<colgroup>";
	for (float w : t.ColWidths()) m_htmlStream << "<col style=\"width: " << FormatUnit(w) << ";\">";
	m_htmlStream << "</colgroup>\n";

	const int headRows = (std::min)(t.GetHeaderRows(), R);
	for (int sec = 0; sec < 2; ++sec) {
		const int r0 = sec == 0 ? 0 : headRows, r1 = sec == 0 ? headRows : R;
		if (r0 >= r1) continue;
		AppendIndent();
		m_htmlStream << (sec == 0 ? "<thead>\n" : "<tbody>\n");
		++m_indent;
		for (int r = r0; r < r1; ++r) {
			AppendIndent();
			m_htmlStream << "<tr style=\"height: " << FormatUnit(t.RowHeights()[r]) << ";\">\n";
			++m_indent;
			for (int c = 0; c < C; ++c) {
				const TableNode::Span sp = t.SpanOf(r, c);
				if (sp.r != r || sp.c != c) continue;   // 병합으로 덮인 칸은 출력하지 않음
				const char* tag = (sec == 0) ? "th" : "td";
				AppendIndent();
				m_htmlStream << "<" << tag << (sec == 0 ? headAttr : cellAttr);
				if (sp.cs > 1) m_htmlStream << " colspan=\"" << sp.cs << "\"";
				if (sp.rs > 1) m_htmlStream << " rowspan=\"" << sp.rs << "\"";
				m_htmlStream << ">" << CellContentHtml(TableComponent::CellTexts(t, sp.r, sp.c)) << "</" << tag << ">\n";
			}
			--m_indent;
			AppendIndent();
			m_htmlStream << "</tr>\n";
		}
		--m_indent;
		AppendIndent();
		m_htmlStream << (sec == 0 ? "</thead>\n" : "</tbody>\n");
	}
	--m_indent;
	AppendIndent();
	m_htmlStream << "</table>\n";
}

void HtmlExporter::Visit(TextNode& node) {
	if (!node.IsVisible()) return;

	std::string tag = !node.GetHtmlTag().empty() ? node.GetHtmlTag() : "p";
	std::ostringstream style;
	style << BuildCommonStyleDecls(node);

	// Free layout 부모 안에 있으면 position: absolute 로 배치
	bool omitW = false, omitH = false;
		style << PositionDecls(node, omitW, omitH);

	// 폰트 속성
	if (!node.GetFontFamily().empty()) {
		style << "font-family: '" << WideToUtf8(node.GetFontFamily()) << "', sans-serif; ";
	}
	style << "font-size: " << FormatUnit(node.GetFontSize()) << "; ";
	if (node.IsBold()) style << "font-weight: bold; ";
	if (node.IsItalic()) style << "font-style: italic; ";

	switch (node.GetAlign()) {
	case TextAlign::Left:   style << "text-align: left; "; break;
	case TextAlign::Center: style << "text-align: center; "; break;
	case TextAlign::Right:  style << "text-align: right; "; break;
	}

	const Style& st = node.GetStyle();
	if (st.fill.type == PaintType::Solid) {
		style << "color: " << ColorToCss(st.fill.color) << "; ";
	}

	if (node.IsAreaText() && node.GetBoxWidth() > 0.001f) {
		style << "width: " << FormatUnit(node.GetBoxWidth()) << "; ";
		style << "word-wrap: break-word; overflow-wrap: break-word; ";
	}

	std::string cls = RegisterCssRule("text", style.str(), &node);

	AppendIndent();
	bool isLink = !node.GetHref().empty();
	if (isLink) {
		m_htmlStream << "<a href=\"" << EscapeHtml(node.GetHref()) << "\" style=\"color: inherit; text-decoration: none;\">";
	}

	m_htmlStream << "<" << tag;
	if (!cls.empty()) m_htmlStream << " class=\"" << cls << "\"";
	else if (!style.str().empty()) m_htmlStream << " style=\"" << style.str() << "\"";
	if (!node.GetAriaLabel().empty()) m_htmlStream << " aria-label=\"" << EscapeHtml(node.GetAriaLabel()) << "\"";
	m_htmlStream << ">";

	// 줄바꿈 \n 을 <br> 로 변환
	std::string textContent = WideToUtf8(node.GetText());
	std::string htmlEscaped;
	for (char c : textContent) {
		if (c == '\n') htmlEscaped += "<br>";
		else if (c == '&') htmlEscaped += "&amp;";
		else if (c == '<') htmlEscaped += "&lt;";
		else if (c == '>') htmlEscaped += "&gt;";
		else htmlEscaped += c;
	}
	m_htmlStream << htmlEscaped;
	m_htmlStream << "</" << tag << ">";
	if (isLink) m_htmlStream << "</a>";
	m_htmlStream << "\n";
}

void HtmlExporter::Visit(ImageNode& node) {
	if (!node.IsVisible()) return;

	std::ostringstream style;
	style << BuildCommonStyleDecls(node);

	bool omitW = false, omitH = false;
		style << PositionDecls(node, omitW, omitH);
	if (!omitW) style << "width: " << FormatUnit(node.GetWidth()) << "; ";
	if (!omitH) style << "height: " << FormatUnit(node.GetHeight()) << "; ";

	static const char* kFits[] = { "contain", "cover", "fill" };
	style << "object-fit: " << kFits[static_cast<int>(node.GetFit())] << "; ";

	std::string cls = RegisterCssRule("img", style.str(), &node);

	std::string src;
	if (node.GetData()) {
		if (m_options.singleFile) {
			// 인라인 Data URL
			src = "data:" + node.GetData()->mime + ";base64," + Base64Encode(node.GetData()->bytes);
		} else {
			// 외부 assets 파일
			std::string ext = ".png";
			if (node.GetData()->mime.find("jpeg") != std::string::npos) ext = ".jpg";
			else if (node.GetData()->mime.find("webp") != std::string::npos) ext = ".webp";
			else if (node.GetData()->mime.find("svg") != std::string::npos) ext = ".svg";
			std::string fileName = "image_" + std::to_string(node.GetId()) + ext;
			src = "assets/" + fileName;
			m_result.assets.push_back({ fileName, node.GetData()->bytes });
		}
	}

	AppendIndent();
	bool isLink = !node.GetHref().empty();
	if (isLink) m_htmlStream << "<a href=\"" << EscapeHtml(node.GetHref()) << "\">";
	m_htmlStream << "<img src=\"" << src << "\"";
	if (!node.GetAlt().empty()) m_htmlStream << " alt=\"" << EscapeHtml(node.GetAlt()) << "\"";
	if (!cls.empty()) m_htmlStream << " class=\"" << cls << "\"";
	else if (!style.str().empty()) m_htmlStream << " style=\"" << style.str() << "\"";
	m_htmlStream << " />";
	if (isLink) m_htmlStream << "</a>";
	m_htmlStream << "\n";
}

void HtmlExporter::Visit(PathNode& node) {
	if (!node.IsVisible()) return;

	// 사각형(Rect) 및 타원(Ellipse)인 경우 깔끔한 CSS <div> 로 변환
	ShapeKind sk = node.GetShapeKind();
	if (sk == ShapeKind::Rect || sk == ShapeKind::Ellipse) {
		Rect2 b{};
		if (node.GetLocalBounds(b)) {
			const float w = b.right - b.left;
			const float h = b.bottom - b.top;

			std::ostringstream style;
			style << BuildCommonStyleDecls(node);

			bool omitW = false, omitH = false;
		style << PositionDecls(node, omitW, omitH);
			if (!omitW) style << "width: " << FormatUnit(w) << "; ";
			if (!omitH) style << "height: " << FormatUnit(h) << "; ";

			const Style& st = node.GetStyle();
			if (st.fill.type == PaintType::Solid && st.fill.color.a > 0.001f) {
				style << "background-color: " << ColorToCss(st.fill.color) << "; ";
			}
			if (st.stroke.width > 0.001f && st.stroke.color.a > 0.001f) {
				style << "border: " << FormatUnit(st.stroke.width) << " " << CssBorderStyleName(st.stroke) << " " << ColorToCss(st.stroke.color) << "; ";
			}

			if (sk == ShapeKind::Ellipse) {
				style << "border-radius: 50%; ";
			} else if (auto* rp = std::get_if<RectParams>(&node.GetParams())) {
				if (rp->rx > 0.001f) style << "border-radius: " << FormatUnit(rp->rx) << "; ";
			}

			std::string cls = RegisterCssRule(sk == ShapeKind::Ellipse ? "ellipse" : "rect", style.str(), &node);

			AppendIndent();
			bool isLink = !node.GetHref().empty();
			if (isLink) m_htmlStream << "<a href=\"" << EscapeHtml(node.GetHref()) << "\">";
			m_htmlStream << "<div";
			if (!cls.empty()) m_htmlStream << " class=\"" << cls << "\"";
			else if (!style.str().empty()) m_htmlStream << " style=\"" << style.str() << "\"";
			if (!node.GetAriaLabel().empty()) m_htmlStream << " aria-label=\"" << EscapeHtml(node.GetAriaLabel()) << "\"";
			m_htmlStream << "></div>";
			if (isLink) m_htmlStream << "</a>";
			m_htmlStream << "\n";
			return;
		}
	}

	// 그 외 복합 벡터 패스: SvgWriter 를 이용해 깔끔한 인라인 <svg> 로 삽입
	Rect2 b{};
	if (!node.GetLocalBounds(b)) return;
	const float w = (std::max)(1.0f, b.right - b.left);
	const float h = (std::max)(1.0f, b.bottom - b.top);

	std::ostringstream style;
	style << BuildCommonStyleDecls(node);

	bool omitW = false, omitH = false;
		style << PositionDecls(node, omitW, omitH);
	if (!omitW) style << "width: " << FormatUnit(w) << "; ";
			if (!omitH) style << "height: " << FormatUnit(h) << "; ";

	std::string cls = RegisterCssRule("vector", style.str(), &node);

	// 노드 1개만 담은 가상 Document 로 SvgWriter 실행
	Document miniDoc;
	miniDoc.width = w;
	miniDoc.height = h;
	miniDoc.AddLayer(L"TempLayer");
	auto clone = node.Clone();
	clone->SetTransform(Matrix2D::Translation(-b.left, -b.top)); // 원점 0,0 기준
	miniDoc.GetActiveLayer()->AddChild(std::move(clone));

	SvgWriteOptions svgOpt(SvgWriteMode::Clean);
	SvgWriter writer;
	std::string svgCode = writer.WriteString(miniDoc, svgOpt);

	// <svg> 태그에 클래스 적용
	if (!cls.empty()) {
		size_t pos = svgCode.find("<svg ");
		if (pos != std::string::npos) {
			svgCode.insert(pos + 5, "class=\"" + cls + "\" ");
		}
	}

	AppendIndent();
	bool isLink = !node.GetHref().empty();
	if (isLink) m_htmlStream << "<a href=\"" << EscapeHtml(node.GetHref()) << "\">\n";
	m_htmlStream << svgCode;
	if (isLink) {
		AppendIndent();
		m_htmlStream << "</a>\n";
	}
}

} // namespace ge
