#include "pch.h"
#include "UiComponents.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ge {
namespace UiComp {

namespace {

const Color kInk(0.13f, 0.14f, 0.17f, 1.0f);
const Color kMuted(0.55f, 0.57f, 0.62f, 1.0f);
const Color kBorder(0.78f, 0.80f, 0.84f, 1.0f);
const Color kPanel(0.96f, 0.97f, 0.98f, 1.0f);
const Color kWhite(1.0f, 1.0f, 1.0f, 1.0f);
const Color kAccent(0.12f, 0.56f, 1.0f, 1.0f);
const Color kClear(0.0f, 0.0f, 0.0f, 0.0f);

struct KindInfo { const char* id; const wchar_t* label; float w, h; };
const KindInfo kInfo[kKindCount] = {
	{ "textbox", L"텍스트 박스", 320.0f, 60.0f },
	{ "input",   L"입력 필드",   260.0f, 40.0f },
	{ "check",   L"체크/라디오/스위치", 180.0f, 28.0f },
	{ "combo",   L"콤보 박스",   220.0f, 40.0f },
	{ "list",    L"목록",        240.0f, 110.0f },
	{ "bar",     L"진행/구분 막대", 260.0f, 24.0f },
	{ "tabs",    L"탭",          320.0f, 44.0f },
	{ "badge",   L"배지",        90.0f, 28.0f },
	{ "card",    L"카드",        280.0f, 300.0f },
	{ "pager",   L"페이지/경로", 300.0f, 36.0f },
};

Kind KindOf(const FrameNode& f)
{
	Kind k = Kind::TextBox;
	KindFromId(f.GetUiKind(), k);
	return k;
}

std::string Prop(const FrameNode& f, const char* key, const char* def = "") { return f.GetUiProp(key, def); }

Style FillStyle(const Color& fill, const Color& stroke, float sw)
{
	Style s;
	if (fill.a > 0.0f) { s.fill.type = PaintType::Solid; s.fill.color = fill; } else { s.fill.type = PaintType::None; }
	s.stroke.width = (stroke.a > 0.0f) ? sw : 0.0f;
	s.stroke.color = stroke;
	s.stroke.cap = LineCap::Round;
	s.stroke.join = LineJoin::Round;
	return s;
}

PathNode* AddPath(FrameNode& root, const std::vector<BezierNode>& nodes, bool closed, const Style& st, const wchar_t* name)
{
	SubPath sp; sp.nodes = nodes; sp.closed = closed;
	auto pn = std::make_unique<PathNode>();
	pn->SetPaths(std::vector<SubPath>{ sp });
	pn->SetStyle(st);
	pn->SetName(name);
	PathNode* raw = pn.get();
	root.AddChild(std::move(pn));
	return raw;
}

PathNode* AddRRect(FrameNode& root, float x, float y, float w, float h, float r, const Color& fill, const Color& stroke, float sw, const wchar_t* name)
{
	r = (std::max)(0.0f, (std::min)(r, (std::min)(w, h) * 0.5f));
	const float k = 0.5523f * r;
	auto mk = [](float ax, float ay, float ix, float iy, float ox, float oy) {
		BezierNode b; b.anchor = Vec2{ ax, ay }; b.hIn = Vec2{ ix, iy }; b.hOut = Vec2{ ox, oy }; return b;
	};
	std::vector<BezierNode> n = {
		mk(x + r, y, -k, 0, 0, 0), mk(x + w - r, y, 0, 0, k, 0),
		mk(x + w, y + r, 0, -k, 0, 0), mk(x + w, y + h - r, 0, 0, 0, k),
		mk(x + w - r, y + h, k, 0, 0, 0), mk(x + r, y + h, 0, 0, -k, 0),
		mk(x, y + h - r, 0, k, 0, 0), mk(x, y + r, 0, 0, 0, -k),
	};
	return AddPath(root, n, true, FillStyle(fill, stroke, sw), name);
}

PathNode* AddCircle(FrameNode& root, float cx, float cy, float r, const Color& fill, const Color& stroke, float sw, const wchar_t* name)
{
	const float k = 0.5523f * r;
	auto mk = [](float ax, float ay, float ix, float iy, float ox, float oy) {
		BezierNode b; b.anchor = Vec2{ ax, ay }; b.hIn = Vec2{ ix, iy }; b.hOut = Vec2{ ox, oy }; return b;
	};
	std::vector<BezierNode> n = {
		mk(cx, cy - r, -k, 0, k, 0), mk(cx + r, cy, 0, -k, 0, k),
		mk(cx, cy + r, k, 0, -k, 0), mk(cx - r, cy, 0, k, 0, -k),
	};
	return AddPath(root, n, true, FillStyle(fill, stroke, sw), name);
}

PathNode* AddPoly(FrameNode& root, const std::vector<Vec2>& pts, bool closed, const Color& fill, const Color& stroke, float sw, const wchar_t* name)
{
	std::vector<BezierNode> n;
	for (const auto& p : pts) { BezierNode b; b.anchor = p; n.push_back(b); }
	return AddPath(root, n, closed, FillStyle(fill, stroke, sw), name);
}

float TextWidth(const std::wstring& s, float size)
{
	TextNode t; t.SetText(s); t.SetFontSize(size);
	return t.MeasuredWidth();
}

// 텍스트 추가. align 에 따라 x 는 왼쪽/가운데/오른쪽 기준점. boxW>0 이면 영역 텍스트(x 는 상자 왼쪽)
TextNode* AddText(FrameNode& root, const std::wstring& text, float x, float y, float size, bool bold, const Color& color,
	TextAlign align = TextAlign::Left, float boxW = 0.0f, const wchar_t* name = L"텍스트", bool italic = false)
{
	auto tn = std::make_unique<TextNode>();
	tn->SetText(text);
	tn->SetFontSize(size);
	tn->SetBold(bold);
	tn->SetItalic(italic);
	tn->SetAlign(align);
	if (boxW > 0.0f) tn->SetBoxWidth(boxW);
	Style s = tn->GetStyle();
	s.fill.type = PaintType::Solid; s.fill.color = color; s.stroke.width = 0.0f;
	tn->SetStyle(s);
	tn->SetTransform(Matrix2D::Translation(x, y));
	tn->SetName(name);
	TextNode* raw = tn.get();
	root.AddChild(std::move(tn));
	return raw;
}

// 한 줄 글자를 세로 가운데에 놓는 y
float MidY(float h, float size) { return (h - size * TextNode::kLineHeightRatio) * 0.5f; }

// 영역 텍스트 높이 추정 (렌더 전 근사: 글자 폭 0.6/1.0 으로 줄 수 계산)
float EstimateTextHeight(const std::wstring& text, float size, float boxW)
{
	int lines = 0;
	float w = 0.0f;
	bool any = false;
	for (wchar_t c : text) {
		any = true;
		if (c == L'\n') { ++lines; w = 0.0f; continue; }
		const float cw = size * (c > 0x2E80 ? 1.0f : 0.6f);
		if (w + cw > boxW && w > 0.0f) { ++lines; w = 0.0f; }
		w += cw;
	}
	if (any || lines == 0) ++lines;
	return lines * size * TextNode::kLineHeightRatio;
}

std::vector<std::wstring> WideList(const FrameNode& f, const char* key)
{
	std::vector<std::wstring> out;
	for (const auto& s : PropList(f, key)) out.push_back(Utf8ToWide(s));
	return out;
}

// ---------------- 종류별 그리기 ----------------
void BuildTextBox(FrameNode& f, float w, float /*h*/)
{
	const std::string tag = Prop(f, "tag", "p");
	const std::string al = Prop(f, "align", "left");
	const float size = PropNum(f, "size", 16.0f);
	const bool bold = PropBool(f, "bold", false);
	const bool quote = (tag == "blockquote");
	const float padL = quote ? 16.0f : 0.0f;
	if (quote) {
		auto* bar = AddRRect(f, 0.0f, 0.0f, 4.0f, f.GetHeight(), 2.0f, kAccent, kClear, 0.0f, L"인용 막대");
		(void)bar;
	}
	const TextAlign a = al == "center" ? TextAlign::Center : al == "right" ? TextAlign::Right : TextAlign::Left;
	const float bw = (std::max)(2.0f, w - padL);
	// 영역 텍스트: 정렬은 상자 폭 안에서 적용되고 원점은 상자 왼쪽 위
	AddText(f, Utf8ToWide(Prop(f, "text", "텍스트")), padL, 0.0f, size, bold, quote ? Color(0.30f, 0.33f, 0.38f, 1.0f) : kInk, a, bw, L"텍스트", quote);
}

void BuildInput(FrameNode& f, float w, float h)
{
	const std::string type = Prop(f, "type", "text");
	const bool disabled = PropBool(f, "disabled", false);
	const bool area = (type == "textarea");
	AddRRect(f, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, disabled ? Color(0.93f, 0.94f, 0.95f, 1.0f) : kWhite, kBorder, 1.0f, L"입력 상자");
	std::wstring value = Utf8ToWide(Prop(f, "value"));
	const std::wstring ph = Utf8ToWide(Prop(f, "placeholder"));
	if (type == "password" && !value.empty()) value = std::wstring(value.size(), L'•');
	const bool showPh = value.empty();
	const std::wstring shown = showPh ? ph : value;
	const float size = 15.0f;
	const float rightPad = (type == "search") ? 34.0f : 12.0f;
	if (!shown.empty())
		AddText(f, shown, 12.0f, area ? 10.0f : MidY(h, size), size, false, (showPh || disabled) ? kMuted : kInk, TextAlign::Left,
			area ? (std::max)(2.0f, w - 24.0f) : 0.0f, L"입력 글자");
	if (type == "search") {
		const float cx = w - 20.0f, cy = h * 0.5f - 1.0f;
		AddCircle(f, cx, cy, 5.0f, kClear, kMuted, 1.6f, L"돋보기");
		AddPoly(f, { Vec2{ cx + 3.6f, cy + 3.6f }, Vec2{ cx + 8.0f, cy + 8.0f } }, false, kClear, kMuted, 1.6f, L"돋보기 손잡이");
	}
	(void)rightPad;
	if (area) {   // 크기 조절 손잡이
		AddPoly(f, { Vec2{ w - 5.0f, h - 14.0f }, Vec2{ w - 14.0f, h - 5.0f } }, false, kClear, kBorder, 1.4f, L"손잡이1");
		AddPoly(f, { Vec2{ w - 5.0f, h - 9.0f }, Vec2{ w - 9.0f, h - 5.0f } }, false, kClear, kBorder, 1.4f, L"손잡이2");
	}
}

void BuildCheck(FrameNode& f, float w, float h)
{
	const std::string kind = Prop(f, "kind", "checkbox");
	const bool on = PropBool(f, "checked", false);
	const Color accent = ColorFromHex(Prop(f, "accent", "#1f8fff"), kAccent);
	const std::wstring label = Utf8ToWide(Prop(f, "label", "라벨"));
	const float s = (std::min)(20.0f, h);
	const float cy = h * 0.5f;
	float ctlW = s;
	if (kind == "radio") {
		AddCircle(f, s * 0.5f, cy, s * 0.5f - 0.75f, kWhite, on ? accent : kBorder, on ? 2.0f : 1.5f, L"라디오");
		if (on) AddCircle(f, s * 0.5f, cy, s * 0.25f, accent, kClear, 0.0f, L"라디오 점");
	} else if (kind == "switch") {
		ctlW = s * 1.8f;
		AddRRect(f, 0.0f, cy - s * 0.5f, ctlW, s, s * 0.5f, on ? accent : Color(0.78f, 0.80f, 0.84f, 1.0f), kClear, 0.0f, L"스위치 트랙");
		const float kx = on ? ctlW - s * 0.5f : s * 0.5f;
		AddCircle(f, kx, cy, s * 0.5f - 2.0f, kWhite, kClear, 0.0f, L"스위치 노브");
	} else {
		AddRRect(f, 0.75f, cy - s * 0.5f + 0.75f, s - 1.5f, s - 1.5f, 4.0f, on ? accent : kWhite, on ? accent : kBorder, 1.5f, L"체크박스");
		if (on) AddPoly(f, { Vec2{ s * 0.25f, cy }, Vec2{ s * 0.43f, cy + s * 0.2f }, Vec2{ s * 0.76f, cy - s * 0.2f } }, false, kClear, kWhite, 2.0f, L"체크 표시");
	}
	const float size = 15.0f;
	if (!label.empty()) AddText(f, label, ctlW + 8.0f, MidY(h, size), size, false, kInk, TextAlign::Left, 0.0f, L"라벨");
	(void)w;
}

void BuildCombo(FrameNode& f, float w, float h)
{
	const std::string kind = Prop(f, "kind", "dropdown");
	const auto opts = WideList(f, "options");
	int sel = static_cast<int>(PropNum(f, "selected", -1.0f));
	if (kind == "listbox") {
		AddRRect(f, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, kWhite, kBorder, 1.0f, L"리스트 상자");
		const int rows = (std::max)(2, static_cast<int>(PropNum(f, "rows", 4.0f)));
		const float rowH = (std::max)(22.0f, (h - 8.0f) / static_cast<float>(rows));
		const float size = 14.0f;
		const size_t maxVis = (std::min)(opts.size(), static_cast<size_t>(rows));
		for (size_t i = 0; i < maxVis; ++i) {
			const float ry = 4.0f + static_cast<float>(i) * rowH;
			const bool isSel = (static_cast<int>(i) == sel);
			if (isSel) {
				AddRRect(f, 4.0f, ry + 1.0f, (std::max)(8.0f, w - 8.0f), rowH - 2.0f, 4.0f,
					Color(0.12f, 0.56f, 1.0f, 0.14f), kClear, 0.0f, L"선택 배경");
			}
			AddText(f, opts[i], 12.0f, ry + MidY(rowH, size), size, isSel,
				isSel ? kAccent : kInk, TextAlign::Left, 0.0f, L"항목 글자");
		}
		return;
	}
	std::wstring shown;
	bool ph = true;
	if (sel >= 0 && sel < static_cast<int>(opts.size())) { shown = opts[sel]; ph = false; }
	else shown = Utf8ToWide(Prop(f, "placeholder", "선택하세요"));
	AddRRect(f, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, kWhite, kBorder, 1.0f, L"콤보 상자");
	const float size = 15.0f;
	if (!shown.empty()) AddText(f, shown, 12.0f, MidY(h, size), size, false, ph ? kMuted : kInk, TextAlign::Left, 0.0f, L"선택값");
	const float cx = w - 18.0f, cy = h * 0.5f;
	AddPoly(f, { Vec2{ cx - 5.0f, cy - 2.5f }, Vec2{ cx, cy + 2.5f }, Vec2{ cx + 5.0f, cy - 2.5f } }, false, kClear, kMuted, 1.8f, L"화살표");
}

void BuildList(FrameNode& f, float w, float /*h*/)
{
	const std::string kind = Prop(f, "kind", "ul");
	const std::string marker = Prop(f, "marker", kind == "ol" ? "decimal" : "disc");
	const float size = PropNum(f, "size", 15.0f);
	const float gap = PropNum(f, "gap", 6.0f);
	const auto items = WideList(f, "items");
	float y = 0.0f;
	int idx = 0;
	for (const auto& it : items) {
		const float rowH = size * TextNode::kLineHeightRatio;
		if (kind == "box") {
			const float bh = rowH + 14.0f;
			AddRRect(f, 0.5f, y + 0.5f, w - 1.0f, bh - 1.0f, 6.0f, kWhite, kBorder, 1.0f, L"목록 칸");
			AddText(f, it, 12.0f, y + 7.0f, size, false, kInk, TextAlign::Left, 0.0f, L"목록 글자");
			y += bh + gap;
		} else {
			const float cy = y + rowH * 0.5f;
			if (kind == "ol") {
				std::wstring m;
				if (marker == "alpha") m = std::wstring(1, static_cast<wchar_t>(L'a' + (idx % 26))) + L".";
				else if (marker == "roman") { static const wchar_t* r[] = { L"i", L"ii", L"iii", L"iv", L"v", L"vi", L"vii", L"viii", L"ix", L"x" }; m = std::wstring(r[idx % 10]) + L"."; }
				else m = std::to_wstring(idx + 1) + L".";
				AddText(f, m, 0.0f, y, size, false, kMuted, TextAlign::Left, 0.0f, L"번호");
			} else if (marker == "square") {
				AddRRect(f, 3.0f, cy - 2.5f, 5.0f, 5.0f, 0.0f, kInk, kClear, 0.0f, L"글머리");
			} else if (marker == "circle") {
				AddCircle(f, 5.5f, cy, 3.0f, kClear, kInk, 1.2f, L"글머리");
			} else {
				AddCircle(f, 5.5f, cy, 3.0f, kInk, kClear, 0.0f, L"글머리");
			}
			AddText(f, it, 24.0f, y, size, false, kInk, TextAlign::Left, 0.0f, L"목록 글자");
			y += rowH + gap;
		}
		++idx;
	}
}

void BuildBar(FrameNode& f, float w, float h)
{
	const std::string kind = Prop(f, "kind", "progress");
	const float th = (std::max)(1.0f, PropNum(f, "thickness", 8.0f));
	const Color track = ColorFromHex(Prop(f, "trackColor", "#dde1e7"), Color(0.87f, 0.88f, 0.91f, 1.0f));
	const Color fill = ColorFromHex(Prop(f, "fillColor", "#1f8fff"), kAccent);
	const float cy = h * 0.5f;
	if (kind == "divider") {
		AddPoly(f, { Vec2{ 0.0f, cy }, Vec2{ w, cy } }, false, kClear, track, th, L"구분선");
		return;
	}
	const float mn = PropNum(f, "min", 0.0f), mx = (std::max)(mn + 0.0001f, PropNum(f, "max", 100.0f));
	const float v = (std::max)(mn, (std::min)(mx, PropNum(f, "value", 40.0f)));
	const float ratio = (v - mn) / (mx - mn);
	const bool label = PropBool(f, "showLabel", true);
	const float labelW = label ? 44.0f : 0.0f;
	const float tw = (std::max)(4.0f, w - labelW);
	const float ty = cy - th * 0.5f;
	AddRRect(f, 0.0f, ty, tw, th, th * 0.5f, track, kClear, 0.0f, L"트랙");
	const float fw = tw * ratio;
	if (fw > 0.5f) AddRRect(f, 0.0f, ty, (std::max)(th, fw), th, th * 0.5f, fill, kClear, 0.0f, L"채움");
	if (kind == "range") AddCircle(f, (std::max)(th * 0.5f, (std::min)(tw - th * 0.5f, fw)), cy, th * 0.5f + 4.0f, kWhite, fill, 2.0f, L"손잡이");
	if (label) {
		const std::wstring txt = (kind == "progress") ? std::to_wstring(static_cast<int>(ratio * 100.0f + 0.5f)) + L"%" : std::to_wstring(static_cast<int>(v + 0.5f));
		AddText(f, txt, w - labelW + 8.0f, MidY(h, 14.0f), 14.0f, false, kMuted, TextAlign::Left, 0.0f, L"값");
	}
}

void BuildTabs(FrameNode& f, float w, float h)
{
	const auto tabs = WideList(f, "tabs");
	const int active = static_cast<int>(PropNum(f, "active", 0.0f));
	const Color accent = ColorFromHex(Prop(f, "color", "#1f8fff"), kAccent);
	const float size = 15.0f;
	AddPoly(f, { Vec2{ 0.0f, h - 1.0f }, Vec2{ w, h - 1.0f } }, false, kClear, kBorder, 1.0f, L"탭 바닥선");
	float x = 0.0f;
	for (size_t i = 0; i < tabs.size(); ++i) {
		const float tw = TextWidth(tabs[i], size) + 28.0f;
		const bool on = (static_cast<int>(i) == active);
		AddText(f, tabs[i], x + tw * 0.5f, MidY(h - 2.0f, size), size, on, on ? accent : kMuted, TextAlign::Center, 0.0f, L"탭");
		if (on) AddPoly(f, { Vec2{ x, h - 1.5f }, Vec2{ x + tw, h - 1.5f } }, false, kClear, accent, 3.0f, L"활성 밑줄");
		x += tw;
	}
}

void BuildBadge(FrameNode& f, float w, float h)
{
	const Color c = ColorFromHex(Prop(f, "color", "#1f8fff"), kAccent);
	const bool closable = PropBool(f, "closable", false);
	AddRRect(f, 0.0f, 0.0f, w, h, h * 0.5f, c, kClear, 0.0f, L"배지 바탕");
	const float size = 13.0f;
	const float closeW = closable ? 16.0f : 0.0f;
	AddText(f, Utf8ToWide(Prop(f, "text", "배지")), (w - closeW) * 0.5f, MidY(h, size), size, true, kWhite, TextAlign::Center, 0.0f, L"배지 글자");
	if (closable) {
		const float cx = w - 14.0f, cy = h * 0.5f;
		AddPoly(f, { Vec2{ cx - 3.0f, cy - 3.0f }, Vec2{ cx + 3.0f, cy + 3.0f } }, false, kClear, kWhite, 1.6f, L"닫기1");
		AddPoly(f, { Vec2{ cx + 3.0f, cy - 3.0f }, Vec2{ cx - 3.0f, cy + 3.0f } }, false, kClear, kWhite, 1.6f, L"닫기2");
	}
}

void BuildCard(FrameNode& f, float w, float h)
{
	const float imgH = (std::max)(0.0f, PropNum(f, "imageH", 120.0f));
	AddRRect(f, 0.5f, 0.5f, w - 1.0f, h - 1.0f, 12.0f, kWhite, kBorder, 1.0f, L"카드 바탕");
	float y = 0.0f;
	if (imgH > 0.0f) {
		AddRRect(f, 1.0f, 1.0f, w - 2.0f, (std::min)(imgH, h - 2.0f), 11.0f, Color(0.90f, 0.92f, 0.95f, 1.0f), kClear, 0.0f, L"이미지 영역");
		const float cx = w * 0.5f, cy = 1.0f + imgH * 0.5f;
		AddPoly(f, { Vec2{ cx - 24.0f, cy + 14.0f }, Vec2{ cx - 8.0f, cy - 6.0f }, Vec2{ cx + 4.0f, cy + 6.0f }, Vec2{ cx + 12.0f, cy - 2.0f }, Vec2{ cx + 24.0f, cy + 14.0f } },
			false, kClear, Color(0.65f, 0.69f, 0.76f, 1.0f), 2.0f, L"이미지 아이콘");
		y = imgH;
	}
	y += 16.0f;
	const float bw = (std::max)(2.0f, w - 32.0f);
	const std::wstring title = Utf8ToWide(Prop(f, "title", "카드 제목"));
	const std::wstring body = Utf8ToWide(Prop(f, "body", "카드 내용을 입력하세요."));
	const std::wstring btn = Utf8ToWide(Prop(f, "button", "자세히"));
	AddText(f, title, 16.0f, y, 18.0f, true, kInk, TextAlign::Left, bw, L"카드 제목");
	y += EstimateTextHeight(title, 18.0f, bw) + 6.0f;
	AddText(f, body, 16.0f, y, 14.0f, false, Color(0.38f, 0.40f, 0.45f, 1.0f), TextAlign::Left, bw, L"카드 내용");
	if (!btn.empty()) {
		const float bh = 36.0f, by = h - bh - 16.0f;
		AddRRect(f, 16.0f, by, (std::min)(bw, TextWidth(btn, 14.0f) + 36.0f), bh, 8.0f, kAccent, kClear, 0.0f, L"카드 버튼");
		const float bwid = (std::min)(bw, TextWidth(btn, 14.0f) + 36.0f);
		AddText(f, btn, 16.0f + bwid * 0.5f, by + MidY(bh, 14.0f), 14.0f, true, kWhite, TextAlign::Center, 0.0f, L"카드 버튼 글자");
	}
}

void BuildPager(FrameNode& f, float /*w*/, float h)
{
	const std::string kind = Prop(f, "kind", "pagination");
	const auto items = WideList(f, "items");
	const int active = static_cast<int>(PropNum(f, "active", 0.0f));
	const float size = 14.0f;
	float x = 0.0f;
	if (kind == "breadcrumb") {
		for (size_t i = 0; i < items.size(); ++i) {
			const bool last = (i + 1 == items.size());
			AddText(f, items[i], x, MidY(h, size), size, last, last ? kInk : kAccent, TextAlign::Left, 0.0f, L"경로");
			x += TextWidth(items[i], size);
			if (!last) { AddText(f, L"/", x + 8.0f, MidY(h, size), size, false, kMuted, TextAlign::Left, 0.0f, L"구분"); x += 26.0f; }
		}
		return;
	}
	const float cell = (std::min)(h, 32.0f);
	for (size_t i = 0; i < items.size(); ++i) {
		const bool on = (static_cast<int>(i) == active);
		const float cw = (std::max)(cell, TextWidth(items[i], size) + 16.0f);
		const float cy0 = (h - cell) * 0.5f;
		AddRRect(f, x + 0.5f, cy0 + 0.5f, cw - 1.0f, cell - 1.0f, 6.0f, on ? kAccent : kWhite, on ? kAccent : kBorder, 1.0f, L"페이지 칸");
		AddText(f, items[i], x + cw * 0.5f, cy0 + MidY(cell, size), size, on, on ? kWhite : kInk, TextAlign::Center, 0.0f, L"페이지 글자");
		x += cw + 6.0f;
	}
}

void RebuildHook(FrameNode& f) { Rebuild(f); }

struct HookRegistrar {
	HookRegistrar() { FrameNode::s_uiRebuildHook = &RebuildHook; }
};
HookRegistrar g_hookRegistrar;

// 종류별 기본 속성
FrameNode::UiProps Defaults(Kind k)
{
	using P = FrameNode::UiProps;
	switch (k) {
	case Kind::TextBox: return P{ {"tag","p"}, {"text","텍스트를 입력하세요."}, {"align","left"}, {"lineHeight","1.5"}, {"size","16"}, {"bold","0"}, {"autoPreset","1"} };
	case Kind::Input:   return P{ {"type","text"}, {"placeholder","내용을 입력하세요"}, {"value",""}, {"disabled","0"} };
	case Kind::Check:   return P{ {"kind","checkbox"}, {"checked","1"}, {"label","동의합니다"}, {"accent","#1f8fff"} };
	case Kind::Combo:   return P{ {"kind","dropdown"}, {"options","옵션 1|옵션 2|옵션 3"}, {"selected","-1"}, {"placeholder","선택하세요"}, {"rows","4"}, {"multiple","0"} };
	case Kind::List:    return P{ {"kind","ul"}, {"marker","disc"}, {"items","첫 번째 항목|두 번째 항목|세 번째 항목"}, {"gap","6"}, {"size","15"} };
	case Kind::Bar:     return P{ {"kind","progress"}, {"value","40"}, {"min","0"}, {"max","100"}, {"showLabel","1"}, {"trackColor","#dde1e7"}, {"fillColor","#1f8fff"}, {"thickness","8"} };
	case Kind::Tabs:    return P{ {"tabs","홈|소개|연락처"}, {"active","0"}, {"color","#1f8fff"} };
	case Kind::Badge:   return P{ {"text","NEW"}, {"color","#1f8fff"}, {"closable","0"} };
	case Kind::Card:    return P{ {"title","카드 제목"}, {"body","카드 내용을 입력하세요."}, {"button","자세히"}, {"imageH","120"} };
	case Kind::Pager:   return P{ {"kind","pagination"}, {"items","1|2|3|4|5"}, {"active","0"} };
	}
	return P{};
}

void ApplyTextPreset(FrameNode& f)
{
	const std::string tag = f.GetUiProp("tag", "p");
	float size = 16.0f; bool bold = false;
	if (tag == "h1") { size = 32.0f; bold = true; }
	else if (tag == "h2") { size = 28.0f; bold = true; }
	else if (tag == "h3") { size = 24.0f; bold = true; }
	else if (tag == "h4") { size = 20.0f; bold = true; }
	else if (tag == "h5") { size = 18.0f; bold = true; }
	else if (tag == "h6") { size = 16.0f; bold = true; }
	else if (tag == "blockquote") { size = 18.0f; }
	f.SetUiPropRaw("size", std::to_string(static_cast<int>(size)));
	f.SetUiPropRaw("bold", bold ? "1" : "0");
}

void FitTextBoxHeight(FrameNode& f)
{
	const float padL = (f.GetUiProp("tag") == "blockquote") ? 16.0f : 0.0f;
	const float h = EstimateTextHeight(Utf8ToWide(f.GetUiProp("text")), PropNum(f, "size", 16.0f), (std::max)(2.0f, f.GetWidth() - padL));
	f.SetFrameSize(f.GetWidth(), (std::max)(h, 8.0f));
}

} // namespace

// ---------------- 공개 API ----------------
const char* KindId(Kind k) { return kInfo[static_cast<int>(k)].id; }
const wchar_t* KindLabel(Kind k) { return kInfo[static_cast<int>(k)].label; }

bool KindFromId(const std::string& id, Kind& out)
{
	for (int i = 0; i < kKindCount; ++i) if (id == kInfo[i].id) { out = static_cast<Kind>(i); return true; }
	return false;
}

void DefaultSize(Kind k, float& w, float& h) { w = kInfo[static_cast<int>(k)].w; h = kInfo[static_cast<int>(k)].h; }

std::unique_ptr<FrameNode> Create(Kind k, float x, float y, float w, float h)
{
	float dw, dh;
	DefaultSize(k, dw, dh);
	if (w < 6.0f) w = dw;
	if (h < 6.0f) h = dh;
	auto fr = std::make_unique<FrameNode>();
	fr->SetUiKind(KindId(k));
	fr->SetUiProps(Defaults(k));
	fr->SetFrameSize(w, h);
	fr->SetClip(k == Kind::Card);
	Style fs;
	fs.fill.type = PaintType::None;
	fs.fill.color.a = 1.0f;   // Paint 기본 알파(0.25)가 속성창 불투명도 슬라이더로 새어 나가지 않게
	fs.stroke.width = 0.0f;
	fr->SetStyle(fs);
	fr->SetName(KindLabel(k));
	fr->SetTransform(Matrix2D::Translation(x, y));
	if (k == Kind::TextBox) FitTextBoxHeight(*fr);   // 높이는 글 내용에 맞춤 (사용자가 끌어 키우면 그 값 유지)
	if (k == Kind::TextBox && h != dh) fr->SetFrameSize(w, h);
	if (k == Kind::Badge) {
		const float bw = TextWidth(Utf8ToWide(fr->GetUiProp("text")), 13.0f) + 28.0f;
		if (w == dw) fr->SetFrameSize(bw, h);
	}
	Rebuild(*fr);
	return fr;
}

void Rebuild(FrameNode& root)
{
	if (!root.IsUiComponent()) return;
	while (!root.GetChildren().empty()) root.RemoveChild(root.GetChildren().back().get());
	const float w = root.GetWidth(), h = root.GetHeight();
	switch (KindOf(root)) {
	case Kind::TextBox: BuildTextBox(root, w, h); break;
	case Kind::Input:   BuildInput(root, w, h); break;
	case Kind::Check:   BuildCheck(root, w, h); break;
	case Kind::Combo:   BuildCombo(root, w, h); break;
	case Kind::List:    BuildList(root, w, h); break;
	case Kind::Bar:     BuildBar(root, w, h); break;
	case Kind::Tabs:    BuildTabs(root, w, h); break;
	case Kind::Badge:   BuildBadge(root, w, h); break;
	case Kind::Card:    BuildCard(root, w, h); break;
	case Kind::Pager:   BuildPager(root, w, h); break;
	}
	root.Invalidate();
}

bool SetProperty(FrameNode& root, const std::string& key, const std::string& value)
{
	if (!root.IsUiComponent()) return false;
	if (root.GetUiProp(key, "\x01") == value) return false;
	root.SetUiPropRaw(key, value);
	const Kind k = KindOf(root);
	if (k == Kind::TextBox) {
		if (key == "tag" && PropBool(root, "autoPreset", true)) ApplyTextPreset(root);
		if (key == "size" || key == "bold") root.SetUiPropRaw("autoPreset", "0");
		if (key == "tag" || key == "text" || key == "size") FitTextBoxHeight(root);
	}
	if (k == Kind::Check && key == "kind") {
		float dw, dh; DefaultSize(k, dw, dh);
	}
	if (k == Kind::Badge && (key == "text" || key == "closable")) {
		const float bw = TextWidth(Utf8ToWide(root.GetUiProp("text")), 13.0f) + 28.0f + (PropBool(root, "closable") ? 16.0f : 0.0f);
		root.SetFrameSize(bw, root.GetHeight());
	}
	Rebuild(root);
	return true;
}

std::vector<PropDesc> Describe(const FrameNode& f)
{
	std::vector<PropDesc> out;
	auto choice = [&](const char* key, const wchar_t* label, std::vector<std::pair<std::string, std::wstring>> ch) {
		PropDesc d; d.type = PropDesc::Choice; d.key = key; d.label = label; d.choices = std::move(ch); out.push_back(d);
	};
	auto text = [&](const char* key, const wchar_t* label) { PropDesc d; d.type = PropDesc::Text; d.key = key; d.label = label; out.push_back(d); };
	auto multi = [&](const char* key, const wchar_t* label) { PropDesc d; d.type = PropDesc::Multi; d.key = key; d.label = label; out.push_back(d); };
	auto flag = [&](const char* key, const wchar_t* label) { PropDesc d; d.type = PropDesc::Bool; d.key = key; d.label = label; out.push_back(d); };
	auto num = [&](const char* key, const wchar_t* label, float mn, float mx, float st) {
		PropDesc d; d.type = PropDesc::Number; d.key = key; d.label = label; d.minV = mn; d.maxV = mx; d.step = st; out.push_back(d);
	};
	auto color = [&](const char* key, const wchar_t* label) { PropDesc d; d.type = PropDesc::ColorHex; d.key = key; d.label = label; out.push_back(d); };

	switch (KindOf(f)) {
	case Kind::TextBox:
		choice("tag", L"종류", { {"p", L"문단 (p)"}, {"h1", L"제목 1 (h1)"}, {"h2", L"제목 2 (h2)"}, {"h3", L"제목 3 (h3)"}, {"h4", L"제목 4 (h4)"},
			{"h5", L"제목 5 (h5)"}, {"h6", L"제목 6 (h6)"}, {"blockquote", L"인용 (blockquote)"} });
		text("text", L"내용");
		choice("align", L"정렬", { {"left", L"왼쪽"}, {"center", L"가운데"}, {"right", L"오른쪽"} });
		num("size", L"글자 크기", 8.0f, 200.0f, 1.0f);
		flag("bold", L"굵게");
		num("lineHeight", L"줄 높이(배)", 0.8f, 4.0f, 0.1f);
		break;
	case Kind::Input:
		choice("type", L"유형", { {"text", L"텍스트"}, {"password", L"비밀번호"}, {"email", L"이메일"}, {"search", L"검색"}, {"number", L"숫자"}, {"textarea", L"여러 줄 (textarea)"} });
		text("placeholder", L"안내 문구");
		text("value", L"값");
		flag("disabled", L"비활성");
		break;
	case Kind::Check:
		choice("kind", L"종류", { {"checkbox", L"체크박스"}, {"radio", L"라디오"}, {"switch", L"스위치"} });
		flag("checked", L"선택됨");
		text("label", L"라벨");
		color("accent", L"강조 색");
		break;
	case Kind::Combo:
		choice("kind", L"표시 방식", { {"dropdown", L"드롭다운 (콤보)"}, {"listbox", L"리스트박스 (다중 행)"} });
		multi("options", L"옵션(| 로 구분)");
		num("selected", L"선택 번호(-1=없음)", -1.0f, 99.0f, 1.0f);
		text("placeholder", L"안내 문구");
		num("rows", L"표시 행 수", 2.0f, 12.0f, 1.0f);
		flag("multiple", L"다중 선택");
		break;
	case Kind::List:
		choice("kind", L"종류", { {"ul", L"글머리 (ul)"}, {"ol", L"번호 (ol)"}, {"box", L"상자 목록"} });
		choice("marker", L"표시", { {"disc", L"● 원 / 1."}, {"circle", L"○ 빈 원 / a."}, {"square", L"■ 사각 / i."}, {"decimal", L"숫자"}, {"alpha", L"알파벳"}, {"roman", L"로마자"} });
		multi("items", L"항목(| 로 구분)");
		num("gap", L"항목 간격", 0.0f, 60.0f, 1.0f);
		num("size", L"글자 크기", 8.0f, 60.0f, 1.0f);
		break;
	case Kind::Bar:
		choice("kind", L"종류", { {"progress", L"진행 (progress)"}, {"range", L"슬라이더 (range)"}, {"divider", L"구분선 (hr)"} });
		num("value", L"값", -100000.0f, 100000.0f, 1.0f);
		num("min", L"최소", -100000.0f, 100000.0f, 1.0f);
		num("max", L"최대", -100000.0f, 100000.0f, 1.0f);
		flag("showLabel", L"값 표시");
		color("trackColor", L"트랙 색");
		color("fillColor", L"채움 색");
		num("thickness", L"두께", 1.0f, 60.0f, 1.0f);
		break;
	case Kind::Tabs:
		multi("tabs", L"탭(| 로 구분)");
		num("active", L"활성 번호", 0.0f, 99.0f, 1.0f);
		color("color", L"강조 색");
		break;
	case Kind::Badge:
		text("text", L"문구");
		color("color", L"색");
		flag("closable", L"닫기 버튼");
		break;
	case Kind::Card:
		text("title", L"제목");
		text("body", L"내용");
		text("button", L"버튼 문구(빈칸=없음)");
		num("imageH", L"이미지 높이(0=없음)", 0.0f, 600.0f, 4.0f);
		break;
	case Kind::Pager:
		choice("kind", L"종류", { {"pagination", L"페이지 번호"}, {"breadcrumb", L"경로 (breadcrumb)"} });
		multi("items", L"항목(| 로 구분)");
		num("active", L"활성 번호", 0.0f, 99.0f, 1.0f);
		break;
	}
	return out;
}

bool PropBool(const FrameNode& f, const std::string& key, bool def)
{
	const std::string v = f.GetUiProp(key, def ? "1" : "0");
	return v == "1" || v == "true";
}

float PropNum(const FrameNode& f, const std::string& key, float def)
{
	const std::string v = f.GetUiProp(key, "");
	if (v.empty()) return def;
	return static_cast<float>(std::atof(v.c_str()));
}

std::vector<std::string> PropList(const FrameNode& f, const std::string& key)
{
	std::vector<std::string> out;
	const std::string v = f.GetUiProp(key, "");
	size_t i = 0;
	while (i <= v.size()) {
		size_t j = v.find('|', i);
		if (j == std::string::npos) j = v.size();
		std::string it = v.substr(i, j - i);
		if (!it.empty()) out.push_back(it);
		i = j + 1;
	}
	return out;
}

std::string EncodeProps(const FrameNode::UiProps& p)
{
	auto esc = [](const std::string& s) {
		std::string o;
		for (unsigned char c : s) {
			if (c == '%' || c == ';' || c == '=' || c == '\n' || c == '\r' || c == '"' || c == '<' || c == '>' || c == '&') {
				char b[4]; sprintf_s(b, "%%%02X", c); o += b;
			} else o += static_cast<char>(c);
		}
		return o;
	};
	std::string out;
	for (const auto& kv : p) {
		if (!out.empty()) out += ';';
		out += esc(kv.first) + "=" + esc(kv.second);
	}
	return out;
}

FrameNode::UiProps DecodeProps(const std::string& s)
{
	auto unesc = [](const std::string& in) {
		std::string o;
		for (size_t i = 0; i < in.size(); ++i) {
			if (in[i] == '%' && i + 2 < in.size() + 0 && std::isxdigit(static_cast<unsigned char>(in[i + 1])) && std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
				o += static_cast<char>(std::strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
				i += 2;
			} else o += in[i];
		}
		return o;
	};
	FrameNode::UiProps out;
	size_t i = 0;
	while (i <= s.size() && !s.empty()) {
		size_t j = s.find(';', i);
		if (j == std::string::npos) j = s.size();
		const std::string pair = s.substr(i, j - i);
		const size_t eq = pair.find('=');
		if (eq != std::string::npos) out.emplace_back(unesc(pair.substr(0, eq)), unesc(pair.substr(eq + 1)));
		i = j + 1;
	}
	return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return std::wstring();
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

std::string WideToUtf8(const std::wstring& s)
{
	if (s.empty()) return std::string();
	const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
	std::string o(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &o[0], n, nullptr, nullptr);
	return o;
}

Color ColorFromHex(const std::string& hex, const Color& def)
{
	if (hex.size() != 7 || hex[0] != '#') return def;
	char* end = nullptr;
	const long v = std::strtol(hex.c_str() + 1, &end, 16);
	if (!end || *end != '\0') return def;
	return Color(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, 1.0f);
}

std::string HexFromColor(const Color& c)
{
	auto b = [](float v) { return static_cast<int>((std::max)(0.0f, (std::min)(1.0f, v)) * 255.0f + 0.5f); };
	char buf[16];
	sprintf_s(buf, "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
	return buf;
}

} // namespace UiComp
} // namespace ge
