#pragma once

#include "../D2DGraphicEngine.h"
#include <memory>
#include <string>
#include <vector>

namespace ge {
namespace UiComp {

// 표준 HTML UI 컴포넌트 모듈 (Step 4.5.17).
// 컴포넌트 = FrameNode(uiKind + uiProps) 하나. 자식 도형은 Rebuild 가 속성으로 다시 만든다 (모델 코드, D2D 의존 없음).
// 도구/속성창/내보내기는 속성(GetUiProp)만 읽고 쓰며, 그리기는 일반 도형 렌더러가 자식을 그린다.

enum class Kind { TextBox, Input, Check, Combo, List, Bar, Tabs, Badge, Card, Pager };
constexpr int kKindCount = 10;

const char*    KindId(Kind k);                       // "textbox" ...
bool           KindFromId(const std::string& id, Kind& out);
const wchar_t* KindLabel(Kind k);                    // 한글 이름
void           DefaultSize(Kind k, float& w, float& h);

// 새 컴포넌트 생성 (w/h 가 0 이하면 기본 크기). 위치는 부모 기준 좌표 (x,y)
std::unique_ptr<FrameNode> Create(Kind k, float x, float y, float w = 0.0f, float h = 0.0f);

// 속성으로 자식을 다시 만든다 (프레임 크기는 유지; 텍스트 박스는 SetProp 이 높이를 추정해 맞춤)
void Rebuild(FrameNode& root);

// 속성 변경 (부수 효과 포함) 후 Rebuild. 바뀌었으면 true
bool SetProperty(FrameNode& root, const std::string& key, const std::string& value);

// 속성창 기술자
struct PropDesc {
	enum Type { Choice, Text, Multi, Bool, Number, ColorHex } type = Text;
	std::string key;
	std::wstring label;
	std::vector<std::pair<std::string, std::wstring>> choices;   // Choice: (값, 표시)
	float minV = 0.0f, maxV = 100.0f, step = 1.0f;               // Number
};
std::vector<PropDesc> Describe(const FrameNode& root);

// 속성 읽기 보조
bool        PropBool(const FrameNode& f, const std::string& key, bool def = false);
float       PropNum(const FrameNode& f, const std::string& key, float def = 0.0f);
std::vector<std::string> PropList(const FrameNode& f, const std::string& key);   // '|' 구분 목록(UTF-8)

// 직렬화 ("k=v;k=v", 구분자는 %xx 이스케이프) / UTF-8 <-> UTF-16
std::string EncodeProps(const FrameNode::UiProps& p);
FrameNode::UiProps DecodeProps(const std::string& s);
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

// "#rrggbb" <-> Color
Color  ColorFromHex(const std::string& hex, const Color& def);
std::string HexFromColor(const Color& c);

} // namespace UiComp
} // namespace ge
