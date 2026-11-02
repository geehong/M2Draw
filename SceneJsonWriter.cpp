#include "pch.h"
#include "SceneJsonWriter.h"
#include "D2DLayer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace ge {

namespace {

using Fields = std::vector<std::pair<std::string, std::string>>;   // 키, 이미 JSON 으로 만든 값

std::string Utf8(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}

// JSON 문자열 리터럴 (입력은 UTF-8)
std::string Quote(const std::string& s)
{
	std::string out = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n";  break;
		case '\r': out += "\\r";  break;
		case '\t': out += "\\t";  break;
		default:
			if (c < 0x20) {
				char b[8];
				std::snprintf(b, sizeof(b), "\\u%04x", static_cast<unsigned>(c));
				out += b;
			} else {
				out += static_cast<char>(c);
			}
		}
	}
	out += '"';
	return out;
}

// 숫자: 소수 셋째 자리까지, 뒤의 0 제거 (NaN/Inf 는 0)
std::string Num(double v)
{
	if (!std::isfinite(v)) return "0";
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%.3f", v);
	std::string s = buf;
	if (s.find('.') != std::string::npos) {
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
	}
	if (s == "-0" || s.empty()) s = "0";
	return s;
}

std::string Bool(bool b) { return b ? "true" : "false"; }

std::string Object(const Fields& f)
{
	std::string s = "{";
	for (size_t i = 0; i < f.size(); ++i) {
		if (i) s += ',';
		s += Quote(f[i].first);
		s += ':';
		s += f[i].second;
	}
	s += '}';
	return s;
}

std::string Array(const std::vector<std::string>& items)
{
	std::string s = "[";
	for (size_t i = 0; i < items.size(); ++i) {
		if (i) s += ',';
		s += items[i];
	}
	s += ']';
	return s;
}

std::string Hex(const Color& c)
{
	auto byte = [](float v) {
		const int x = static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
		char b[4];
		std::snprintf(b, sizeof(b), "%02x", x);
		return std::string(b);
	};
	return Quote("#" + byte(c.r) + byte(c.g) + byte(c.b));
}

const char* KindName(NodeKind k)
{
	switch (k) {
	case NodeKind::Path:  return "path";
	case NodeKind::Group: return "group";
	case NodeKind::Layer: return "layer";
	case NodeKind::Text:  return "text";
	case NodeKind::Image: return "image";
	}
	return "unknown";
}

const char* LayoutName(FrameLayoutMode m)
{
	switch (m) {
	case FrameLayoutMode::Row:    return "row";
	case FrameLayoutMode::Column: return "column";
	default:                      return "none";
	}
}

const char* SizingName(FrameSizingMode m)
{
	switch (m) {
	case FrameSizingMode::Hug:  return "hug";
	case FrameSizingMode::Fill: return "fill";
	default:                    return "fixed";
	}
}

const char* MainAlignName(LayoutMainAlign a)
{
	switch (a) {
	case LayoutMainAlign::Center:       return "center";
	case LayoutMainAlign::End:          return "end";
	case LayoutMainAlign::SpaceBetween: return "space-between";
	default:                            return "start";
	}
}

const char* CrossAlignName(LayoutCrossAlign a)
{
	switch (a) {
	case LayoutCrossAlign::Center: return "center";
	case LayoutCrossAlign::End:    return "end";
	default:                       return "start";
	}
}

const char* FitName(ImageFit f)
{
	switch (f) {
	case ImageFit::Fit:  return "fit";
	case ImageFit::Fill: return "fill";
	default:             return "stretch";
	}
}

const char* TextAlignName(TextAlign a)
{
	switch (a) {
	case TextAlign::Center: return "center";
	case TextAlign::Right:  return "right";
	default:                return "left";
	}
}

std::string Rect(const Rect2& r)
{
	return Object({ { "left", Num(r.left) }, { "top", Num(r.top) }, { "right", Num(r.right) }, { "bottom", Num(r.bottom) } });
}

std::string Padding(const PaddingEdges& p)
{
	return Object({ { "top", Num(p.top) }, { "right", Num(p.right) }, { "bottom", Num(p.bottom) }, { "left", Num(p.left) } });
}

std::string StyleJson(const Node& node)
{
	const Style& st = node.GetStyle();
	Fields f;
	if (st.fill.type == PaintType::Solid) {
		f.push_back({ "fill", Object({ { "type", Quote("solid") },
		                               { "color", Hex(st.fill.color) },
		                               { "alpha", Num(st.fill.color.a) } }) });
	} else if (st.fill.type == PaintType::Gradient) {
		f.push_back({ "fill", Object({ { "type", Quote("gradient") } }) });
	}
	if (st.stroke.width > 0.0f) {
		f.push_back({ "stroke", Object({ { "color", Hex(st.stroke.color) },
		                                 { "width", Num(st.stroke.width) } }) });
	}
	return Object(f);
}

// 노드 하나를 JSON 객체로 (자식은 재귀)
std::string NodeJson(Node& node)
{
	Fields f;
	f.push_back({ "id", Num(static_cast<double>(node.GetId())) });
	f.push_back({ "kind", Quote(KindName(node.Kind())) });
	f.push_back({ "name", Quote(Utf8(node.GetName())) });
	f.push_back({ "visible", Bool(node.IsVisible()) });
	f.push_back({ "locked", Bool(node.IsLocked()) });
	f.push_back({ "opacity", Num(node.GetOpacity()) });

	Rect2 bounds;
	if (Document::GetWorldBounds(node, bounds))
		f.push_back({ "bounds", Rect(bounds) });

	// 역할: HTML 태그로 지정된 의미 (header, nav, button 등). 없으면 생략
	if (!node.GetHtmlTag().empty())
		f.push_back({ "role", Quote(node.GetHtmlTag()) });

	if (const auto* frame = dynamic_cast<const FrameNode*>(&node)) {
		f.push_back({ "frame", Object({ { "width", Num(frame->GetWidth()) },
		                                { "height", Num(frame->GetHeight()) },
		                                { "clip", Bool(frame->IsClip()) },
		                                { "cornerRadii", Object({ { "tl", Num(frame->GetCornerRadii().tl) },
		                                                          { "tr", Num(frame->GetCornerRadii().tr) },
		                                                          { "br", Num(frame->GetCornerRadii().br) },
		                                                          { "bl", Num(frame->GetCornerRadii().bl) } }) },
		                                { "borderSides", Num(frame->GetBorderSides()) } }) });
		f.push_back({ "layout", Object({ { "mode", Quote(LayoutName(frame->GetLayoutMode())) },
		                                 { "gap", Num(frame->GetGap()) },
		                                 { "padding", Padding(frame->GetPaddingEdges()) },
		                                 { "mainAlign", Quote(MainAlignName(frame->GetMainAlign())) },
		                                 { "crossAlign", Quote(CrossAlignName(frame->GetCrossAlign())) } }) });
		f.push_back({ "sizing", Object({ { "w", Quote(SizingName(frame->GetSizingW())) },
		                                 { "h", Quote(SizingName(frame->GetSizingH())) } }) });
	}

	if (node.Kind() == NodeKind::Text) {
		const auto& t = static_cast<const TextNode&>(node);
		f.push_back({ "text", Object({ { "content", Quote(Utf8(t.GetText())) },
		                               { "fontFamily", Quote(Utf8(t.GetFontFamily())) },
		                               { "fontSize", Num(t.GetFontSize()) },
		                               { "bold", Bool(t.IsBold()) },
		                               { "italic", Bool(t.IsItalic()) },
		                               { "align", Quote(TextAlignName(t.GetAlign())) },
		                               { "boxWidth", Num(t.GetBoxWidth()) } }) });
	}

	if (node.Kind() == NodeKind::Image) {
		const auto& im = static_cast<const ImageNode&>(node);
		const auto data = im.GetData();
		f.push_back({ "image", Object({ { "width", Num(im.GetWidth()) },
		                                { "height", Num(im.GetHeight()) },
		                                { "pixelWidth", Num(data ? data->pixelW : 0) },
		                                { "pixelHeight", Num(data ? data->pixelH : 0) },
		                                { "mime", Quote(data ? data->mime : std::string()) },
		                                { "fit", Quote(FitName(im.GetFit())) } }) });
	}

	f.push_back({ "style", StyleJson(node) });

	if (const auto* group = dynamic_cast<const GroupNode*>(&node)) {
		std::vector<std::string> kids;
		for (const auto& child : group->GetChildren())
			kids.push_back(NodeJson(*child));
		f.push_back({ "children", Array(kids) });
	}

	return Object(f);
}

} // namespace

std::string SerializeSceneJson(const Document& doc, const SceneJsonOptions& options)
{
	Fields top;
	top.push_back({ "version", "1" });
	top.push_back({ "document", Object({ { "width", Num(doc.width) },
	                                     { "height", Num(doc.height) },
	                                     { "background", Hex(doc.backgroundColor) },
	                                     { "backgroundAlpha", Num(doc.backgroundColor.a) } }) });

	if (options.root) {
		top.push_back({ "root", NodeJson(*options.root) });
	} else {
		std::vector<std::string> layers;
		for (const auto& layer : doc.GetLayers())
			layers.push_back(NodeJson(*layer));
		top.push_back({ "layers", Array(layers) });
	}

	return Object(top) + "\n";
}

} // namespace ge
