#include "pch.h"
#include "CoreUtil.h"
#include "ChatAgentWnd.h"
#include "Resource.h"
#include "AgentGuide.h"
#include "MainFrm.h"
#include "PanelStyle.h"
#include <shellapi.h>
#include <afxtabctrl.h>

#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr UINT WM_CHAT_REPLY = WM_APP + 310;
constexpr UINT WM_CHAT_VERSION = WM_APP + 311;
constexpr UINT IDC_CHAT_SEND = 3001;
constexpr UINT IDC_CHAT_Q_FIRST = 3002;   // 퀵 액션 버튼 ID: 3002 ~ 3006 (Analyze, Generate, Apply, New Chat, Start)
constexpr UINT IDC_CHAT_Q_LAST = 3006;
constexpr UINT IDC_CHAT_Q_START = IDC_CHAT_Q_FIRST + 4;
constexpr UINT IDC_CHAT_STATUS = 3007;    // 작업 상태 줄
constexpr UINT_PTR IDC_CHAT_JOB_TIMER = 1;   // 작업 상태 깜빡임 타이머
constexpr UINT IDC_CHAT_Q_FRAME = 3008;   // 선택 프레임 보내기 버튼
constexpr UINT IDC_CHAT_Q_AUTO = 3009;    // 자동 처리 버튼 (시작 → 구조 파악 → 웹페이지 생성)

struct ChatReply {
	std::wstring text;
	std::string  session;
	bool         error = false;
};

std::wstring ChatWide(const std::string& s)
{
	return Utf8ToWide(s);
}

std::string ChatUtf8(const std::wstring& w)
{
	return WideToUtf8(w);
}

void AppendUtf8Code(std::string& out, unsigned cp)
{
	if (cp < 0x80) out += static_cast<char>(cp);
	else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
	else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
	else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}

// JSON 의 "key":"값" 에서 값을 꺼낸다 (이스케이프와 유니코드 처리)
bool JsonStringField(const std::string& json, const char* key, std::string& out)
{
	const std::string pat = std::string("\"") + key + "\"";
	size_t p = json.find(pat);
	if (p == std::string::npos) return false;
	p += pat.size();
	auto skipWs = [&]() { while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n')) ++p; };
	skipWs();
	if (p >= json.size() || json[p] != ':') return false;
	++p; skipWs();
	if (p >= json.size() || json[p] != '"') return false;
	++p;
	out.clear();
	for (; p < json.size(); ++p) {
		const char c = json[p];
		if (c == '"') return true;
		if (c != '\\' || p + 1 >= json.size()) { out += c; continue; }
		const char e = json[++p];
		switch (e) {
		case 'n': out += '\n'; break;
		case 't': out += '\t'; break;
		case 'r': break;
		case 'u': {
			if (p + 4 >= json.size()) return false;
			auto hex4 = [&](size_t at, unsigned& v) {
				char* end = nullptr;
				const std::string s = json.substr(at, 4);
				v = static_cast<unsigned>(std::strtoul(s.c_str(), &end, 16));
				return s.size() == 4 && end == s.c_str() + 4;
			};
			unsigned cp = 0;
			if (!hex4(p + 1, cp)) return false;   // 깨진 \u 이스케이프는 예외 없이 실패 처리
			p += 4;
			if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 < json.size() && json[p + 1] == '\\' && json[p + 2] == 'u') {
				unsigned lo = 0;
				if (hex4(p + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					p += 6;
				}
			}
			AppendUtf8Code(out, cp);
			break;
		}
		default: out += e; break;
		}
	}
	return false;
}

// 명령을 실행하고 표준 출력 전체를 읽는다 (표준 입력은 없음)
bool RunCapture(const std::wstring& cmdLine, std::string& out)
{
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE outR = nullptr, outW = nullptr;
	if (!CreatePipe(&outR, &outW, &sa, 0)) return false;
	SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = outW;
	si.hStdError = outW;
	PROCESS_INFORMATION pi{};
	std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
	buf.push_back(L'\0');
	const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(outW);
	if (!ok) {
		CloseHandle(outR);
		return false;
	}
	char chunk[1024];
	DWORD n = 0;
	while (ReadFile(outR, chunk, sizeof(chunk), &n, nullptr) && n > 0)
		out.append(chunk, n);
	CloseHandle(outR);
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

// 응답 JSON 객체가 끝까지 도착했는지 확인한다 (닫는 중괄호 + result 와 session_id 필드)
bool IsCompleteJsonReply(const std::string& out)
{
	size_t end = out.size();
	while (end > 0 && (out[end - 1] == '\n' || out[end - 1] == '\r' || out[end - 1] == ' ' || out[end - 1] == '\t')) --end;
	if (end == 0 || out[end - 1] != '}') return false;
	return out.find("\"result\":") != std::string::npos && out.find("\"session_id\":") != std::string::npos;
}

// Claude Code 를 print 모드로 실행한다. 프롬프트는 표준 입력으로 넘기고, 표준 출력(JSON)을 모두 읽는다
bool RunClaudeOnce(const std::string& prompt, const std::wstring& cwd, const std::string& session, std::string& out)
{
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
	if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) return false;
	SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

	std::wstring cmd = L"claude -p --output-format json --allowedTools"
		L" mcp__m2draw__m2draw_list_packages mcp__m2draw__m2draw_get_guide mcp__m2draw__m2draw_get_prompt"
		L" mcp__m2draw__m2draw_get_scene mcp__m2draw__m2draw_get_image_paths mcp__m2draw__m2draw_put_result";
	if (!session.empty()) cmd += L" --resume " + ChatWide(session);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = inR;
	si.hStdOutput = outW;
	si.hStdError = outW;
	PROCESS_INFORMATION pi{};
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(L'\0');
	const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
		cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
	CloseHandle(inR);
	CloseHandle(outW);
	if (!ok) {
		CloseHandle(inW);
		CloseHandle(outR);
		return false;
	}

	DWORD written = 0;
	WriteFile(inW, prompt.data(), static_cast<DWORD>(prompt.size()), &written, nullptr);
	CloseHandle(inW);

	char chunk[4096];
	// 출력 끝(EOF)을 기다리면 안 된다: MCP 하위 프로세스가 파이프를 잡고 있으면 Claude 가 끝나도 EOF 가 오지 않는다.
	// 대신 응답 JSON 이 완성되면 읽기를 끝내고 Claude 프로세스를 정리한다.
	for (;;) {
		DWORD avail = 0;
		if (!PeekNamedPipe(outR, nullptr, 0, nullptr, &avail, nullptr)) break;   // 파이프가 닫힘
		if (avail > 0) {
			DWORD n = 0;
			const DWORD want = (std::min)(avail, static_cast<DWORD>(sizeof(chunk)));
			if (!ReadFile(outR, chunk, want, &n, nullptr) || n == 0) break;
			out.append(chunk, n);
			continue;
		}
		if (IsCompleteJsonReply(out)) break;
		if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;   // Claude 가 먼저 끝남
		Sleep(100);
	}
	CloseHandle(outR);
	TerminateProcess(pi.hProcess, 0);   // 이미 끝났으면 아무 일도 없다
	WaitForSingleObject(pi.hProcess, 2000);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

} // namespace

// ---- 대화 기록의 마크다운 서식 출력 ----------------------------------------------
// 제목(#), 굵게(**), 기울임(*), 코드(`, ```), 목록(-), 인용(>), 구분선(---), 표(|) 를 글자 서식으로 바꿔 넣는다.
namespace {

struct TextStyle {
	bool bold = false;
	bool italic = false;
	bool mono = false;
	bool gray = false;
	long height = 200;   // 글자 크기 (twips, 200 = 10pt)
};

void InsertText(CRichEditCtrl& re, const TextStyle& st, const CString& text)
{
	CHARFORMAT2 cf{};
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_FACE | CFM_SIZE | CFM_COLOR;
	cf.dwEffects = (st.bold ? CFE_BOLD : 0) | (st.italic ? CFE_ITALIC : 0) | (st.gray ? 0 : CFE_AUTOCOLOR);
	cf.crTextColor = st.gray ? RGB(120, 120, 120) : 0;
	cf.yHeight = st.height;
	_tcscpy_s(cf.szFaceName, LF_FACESIZE, st.mono ? _T("Consolas") : _T("Malgun Gothic"));
	re.SetSel(-1, -1);
	re.SetSelectionCharFormat(cf);
	re.ReplaceSel(text);
}

// **굵게**, *기울임*, `코드` 를 서식으로 바꾼다
void InsertInline(CRichEditCtrl& re, const CString& s, TextStyle st)
{
	CString buf;
	auto flush = [&]() {
		if (!buf.IsEmpty()) {
			InsertText(re, st, buf);
			buf.Empty();
		}
	};
	for (int i = 0; i < s.GetLength();) {
		if (!st.mono && s.Mid(i, 2) == _T("**")) {
			flush();
			st.bold = !st.bold;
			i += 2;
		} else if (s[i] == _T('`')) {
			flush();
			st.mono = !st.mono;
			i++;
		} else if (!st.mono && s[i] == _T('*')) {
			flush();
			st.italic = !st.italic;
			i++;
		} else {
			buf += s[i];
			i++;
		}
	}
	flush();
}

// 제목 레벨 (# ~ ######). 제목이 아니면 0
int HeadingLevel(const CString& line)
{
	int n = 0;
	while (n < line.GetLength() && line[n] == _T('#')) n++;
	return (n >= 1 && n <= 6 && n < line.GetLength() && line[n] == _T(' ')) ? n : 0;
}

// 한글 등 넓은 글자는 2칸으로 세어 표 열 너비를 맞춘다
int DisplayWidth(const CString& s)
{
	int w = 0;
	for (int i = 0; i < s.GetLength(); ++i) w += (s[i] < 0x100) ? 1 : 2;
	return w;
}

// "| a | b |" 한 줄을 셀 목록으로
std::vector<CString> SplitRow(const CString& line)
{
	std::vector<CString> cells;
	CString t = line;
	t.Trim();
	if (!t.IsEmpty() && t[0] == _T('|')) t = t.Mid(1);
	if (!t.IsEmpty() && t[t.GetLength() - 1] == _T('|')) t = t.Left(t.GetLength() - 1);
	int start = 0;
	for (int i = 0; i <= t.GetLength(); ++i) {
		if (i == t.GetLength() || t[i] == _T('|')) {
			CString cell = t.Mid(start, i - start);
			cell.Trim();
			cells.push_back(cell);
			start = i + 1;
		}
	}
	return cells;
}

bool IsSeparatorRow(const std::vector<CString>& cells)
{
	for (const CString& c : cells) {
		if (c.IsEmpty()) return false;
		for (int i = 0; i < c.GetLength(); ++i)
			if (c[i] != _T('-') && c[i] != _T(':')) return false;
	}
	return !cells.empty();
}

// 모아 둔 표 줄을 고정폭 글자로 그린다 (첫 줄은 굵게, 아래에 구분선)
void FlushTable(CRichEditCtrl& re, std::vector<CString>& rows)
{
	if (rows.empty()) return;
	std::vector<std::vector<CString>> table;
	for (const CString& r : rows) {
		std::vector<CString> cells = SplitRow(r);
		if (!IsSeparatorRow(cells)) table.push_back(cells);
	}
	rows.clear();
	if (table.empty()) return;

	size_t cols = 0;
	for (const auto& row : table) cols = (std::max)(cols, row.size());
	std::vector<int> width(cols, 0);
	for (const auto& row : table)
		for (size_t c = 0; c < row.size(); ++c) width[c] = (std::max)(width[c], DisplayWidth(row[c]));

	int total = 0;
	for (size_t c = 0; c < cols; ++c) total += width[c] + 3;

	for (size_t r = 0; r < table.size(); ++r) {
		CString line = _T("  ");
		for (size_t c = 0; c < cols; ++c) {
			const CString cell = c < table[r].size() ? table[r][c] : CString();
			line += cell;
			for (int pad = DisplayWidth(cell); pad < width[c]; ++pad) line += _T(' ');
			if (c + 1 < cols) line += _T(" │ ");
		}
		TextStyle st;
		st.mono = true;
		st.bold = (r == 0);
		InsertText(re, st, line + _T("\r\n"));
		if (r == 0) {
			CString rule = _T("  ");
			for (int i = 0; i < total - 3; ++i) rule += _T('─');
			InsertText(re, st, rule + _T("\r\n"));
		}
	}
}

} // namespace

void CChatAgentWnd::AppendLabel(const CString& label)
{
	if (!m_transcript.GetSafeHwnd()) return;
	TextStyle st;
	st.bold = true;
	InsertText(m_transcript, st, label + _T("\r\n"));
	m_transcript.SendMessage(EM_SCROLLCARET, 0, 0);
}

void CChatAgentWnd::RenderMarkdown(const CString& md)
{
	if (!m_transcript.GetSafeHwnd()) return;

	CString text = md;
	text.Replace(_T("\r"), _T(""));
	std::vector<CString> lines;
	int start = 0;
	for (int i = 0; i <= text.GetLength(); ++i) {
		if (i == text.GetLength() || text[i] == _T('\n')) {
			lines.push_back(text.Mid(start, i - start));
			start = i + 1;
		}
	}

	CRichEditCtrl& re = m_transcript;
	TextStyle base;
	bool inCode = false;
	std::vector<CString> tableRows;
	for (const CString& line : lines) {
		CString t = line;
		t.TrimLeft();
		if (t.Left(3) == _T("```")) {
			FlushTable(re, tableRows);
			inCode = !inCode;
			continue;
		}
		if (inCode) {
			TextStyle code;
			code.mono = true;
			code.height = 180;
			InsertText(re, code, line + _T("\r\n"));
			continue;
		}
		if (t.Left(1) == _T("|")) {
			tableRows.push_back(t);
			continue;
		}
		FlushTable(re, tableRows);

		if (t.IsEmpty()) {
			InsertText(re, base, _T("\r\n"));
		} else if (const int lvl = HeadingLevel(t)) {
			TextStyle h;
			h.bold = true;
			h.height = 280 - (lvl - 1) * 20;   // # 는 크게, 뒤로 갈수록 작게
			InsertInline(re, t.Mid(lvl + 1), h);
			InsertText(re, base, _T("\r\n"));
		} else if (t == _T("---") || t == _T("***")) {
			TextStyle rule;
			rule.gray = true;
			InsertText(re, rule, _T("────────────────────────\r\n"));
		} else if (t.Left(2) == _T("- ") || t.Left(2) == _T("* ") || t.Left(2) == _T("+ ")) {
			InsertText(re, base, _T("  • "));
			InsertInline(re, t.Mid(2), base);
			InsertText(re, base, _T("\r\n"));
		} else if (t.Left(2) == _T("> ")) {
			TextStyle quote;
			quote.gray = true;
			InsertText(re, quote, _T("┃ "));
			InsertInline(re, t.Mid(2), quote);
			InsertText(re, base, _T("\r\n"));
		} else {
			InsertInline(re, t, base);
			InsertText(re, base, _T("\r\n"));
		}
	}
	FlushTable(re, tableRows);
	re.SendMessage(EM_SCROLLCARET, 0, 0);
}

BEGIN_MESSAGE_MAP(CChatAgentWnd, CWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_DRAWITEM()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_CHAT_SEND, &CChatAgentWnd::OnSendClicked)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_CHAT_Q_FIRST, IDC_CHAT_Q_LAST, &CChatAgentWnd::OnQuickClicked)
	ON_BN_CLICKED(IDC_CHAT_Q_FRAME, &CChatAgentWnd::OnFrameClicked)
	ON_BN_CLICKED(IDC_CHAT_Q_AUTO, &CChatAgentWnd::OnAutoClicked)
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_CHAT_REPLY, &CChatAgentWnd::OnChatReply)
	ON_MESSAGE(WM_CHAT_VERSION, &CChatAgentWnd::OnChatVersion)
END_MESSAGE_MAP()

CChatAgentWnd::CChatAgentWnd() {}
CChatAgentWnd::~CChatAgentWnd() {}

int CChatAgentWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;
	CFont* font = CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
	CRect rc(0, 0, 0, 0);
	m_transcript.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, rc, this, 1);
	m_input.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rc, this, 2);
	m_send.Create(_T("보내기"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, rc, this, IDC_CHAT_SEND);
	// 퀵 액션: 자주 쓰는 요청을 버튼 한 번으로 보낸다 (순서는 IDC_CHAT_Q_FIRST 부터)
	const CString quickLabels[7] = { _T("Analyze"), _T("Generate"), _T("Apply"), _T("New Chat"), _T("Start"), _T("Frame"), _T("Auto") };
	for (int i = 0; i < 7; ++i) {
		const UINT id = (i == 5) ? IDC_CHAT_Q_FRAME : (i == 6) ? IDC_CHAT_Q_AUTO : IDC_CHAT_Q_FIRST + i;
		m_quick[i].Create(quickLabels[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, rc, this, id);
		m_quick[i].SetFont(font);
		m_quickBase[i] = quickLabels[i];
	}
	m_status.Create(_T("대기 중"), WS_CHILD | WS_VISIBLE | SS_LEFT, rc, this, IDC_CHAT_STATUS);
	m_status.SetFont(font);
	m_transcript.SetFont(font);
	m_input.SetFont(font);
	m_send.SetFont(font);
	AppendLine(_T("먼저 파일 > 에이전트 작업 시작을 누르면 작업 패키지가 연결됩니다."));

	// Claude Code 버전을 읽어서 에이전트 이름을 정한다 (Claude <버전> Agent)
	const HWND hwnd = GetSafeHwnd();
	std::thread([hwnd]() {
		std::string out;
		std::wstring name = L"Claude Agent";
		if (RunCapture(L"claude --version", out)) {
			std::string ver;
			for (char c : out) {
				if (c == ' ' || c == '\r' || c == '\n') { if (!ver.empty()) break; continue; }
				ver += c;
			}
			if (!ver.empty()) name = L"Claude " + ChatWide(ver) + L" Agent";
		}
		::PostMessageW(hwnd, WM_CHAT_VERSION, 0, reinterpret_cast<LPARAM>(new std::wstring(name)));
	}).detach();
	return 0;
}

void CChatAgentWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	const int rowH = 22, gap = 2;
	const int inputY = (cy - rowH - gap) > 0 ? cy - rowH - gap : 0;
	// 맨 위 퀵 액션 버튼: 고정 크기로 왼쪽부터 정렬. Start 를 맨 앞에 둔다
	const int quickW = 84, quickGap = 4;
	const int order[7] = { 4, 0, 1, 2, 3, 5, 6 };
	for (int pos = 0; pos < 7; ++pos) {
		CButton& btn = m_quick[order[pos]];
		if (btn.GetSafeHwnd()) btn.MoveWindow(pos * (quickW + quickGap), 0, quickW, rowH);
	}
	// 작업 상태 줄은 입력줄 바로 위, 대화 기록은 그 위까지
	const int statusY = (inputY - gap - rowH) > 0 ? inputY - gap - rowH : 0;
	if (m_status.GetSafeHwnd()) m_status.MoveWindow(0, statusY, cx, rowH);
	const int transcriptY = rowH + gap;
	const int transcriptH = statusY - gap - transcriptY;
	if (m_transcript.GetSafeHwnd()) m_transcript.MoveWindow(0, transcriptY, cx, transcriptH > 0 ? transcriptH : 0);
	if (m_input.GetSafeHwnd()) m_input.MoveWindow(0, inputY, cx > 80 ? cx - 70 : 0, rowH);
	if (m_send.GetSafeHwnd()) m_send.MoveWindow(cx > 70 ? cx - 64 : 0, inputY, 64, rowH);
}

BOOL CChatAgentWnd::OnEraseBkgnd(CDC* pDC)
{
	// 창 크기가 바뀔 때 남는 영역을 패널 바탕색으로 칠한다 (칠하지 않으면 검은 띠가 남는다)
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(rc, ::GetSysColor(COLOR_3DFACE));
	return TRUE;
}

BOOL CChatAgentWnd::PreTranslateMessage(MSG* pMsg)
{
	// 입력칸에서 Enter 를 누르면 보낸다
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN && ::GetFocus() == m_input.GetSafeHwnd()) {
		SendToAgent();
		return TRUE;
	}
	// 메인 창의 단축키(Ctrl+C/V 등)가 가로채지 못하게, 입력칸과 대화 기록에서 직접 처리한다
	if (pMsg->message == WM_KEYDOWN && (::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
		const HWND focus = ::GetFocus();
		const bool isInput = focus == m_input.GetSafeHwnd();
		if (isInput || focus == m_transcript.GetSafeHwnd()) {
			switch (pMsg->wParam) {
			case 'C': ::SendMessageW(focus, WM_COPY, 0, 0); return TRUE;
			case 'A': ::SendMessageW(focus, EM_SETSEL, 0, -1); return TRUE;
			case 'V': if (isInput) ::SendMessageW(focus, WM_PASTE, 0, 0); return TRUE;
			case 'X': if (isInput) ::SendMessageW(focus, WM_CUT, 0, 0); return TRUE;
			case 'Z': if (isInput) ::SendMessageW(focus, WM_UNDO, 0, 0); return TRUE;
			default: break;
			}
		}
	}	return CWnd::PreTranslateMessage(pMsg);
}

void CChatAgentWnd::OnSendClicked()
{
	SendToAgent();
}

void CChatAgentWnd::SetPackageDir(const CString& dir, const CString& site)
{
	m_site = site;
	if (dir == m_packageDir) return;
	m_packageDir = dir;
	m_sessionId.clear();   // 다른 패키지면 새 대화로 시작
	SetChangedBadge(false);
	AppendLine(_T("작업 패키지 연결: ") + dir);
}

void CChatAgentWnd::UseDocument(const CString& dir, const CString& site)
{
	m_site = site;
	if (dir.IsEmpty())
	{
		if (!m_packageDir.IsEmpty())
		{
			m_packageDir.Empty();
			m_sessionId.clear();
			AppendLine(_T("이 페이지에는 아직 에이전트 작업이 없습니다. 파일 > 에이전트 작업 시작 또는 Start 버튼으로 시작하세요."));
		}
		return;
	}
	SetPackageDir(dir, site);
}

void CChatAgentWnd::AppendLine(const CString& line)
{
	if (!m_transcript.GetSafeHwnd()) return;
	TextStyle plain;
	InsertText(m_transcript, plain, line + _T("\r\n"));
	m_transcript.SendMessage(EM_SCROLLCARET, 0, 0);
}

void CChatAgentWnd::SendToAgent()
{
	if (m_busy) return;
	CString text;
	m_input.GetWindowText(text);
	text.Trim();
	if (text.IsEmpty()) return;

	// /지침 (7.3.2 진입점): 워크스페이스의 지침_템플릿.md 를 기본 편집기로 연다
	if (text.CompareNoCase(_T("/지침")) == 0) {
		const CString file = CString(ge::AgentWorkspaceRoot().c_str()) + _T("\\지침_템플릿.md");
		if (GetFileAttributes(file) == INVALID_FILE_ATTRIBUTES)
			AppendLine(_T("지침 파일이 아직 없습니다. 에이전트 패키지를 한 번 만들면 기본 지침 파일이 생깁니다: ") + file);
		else
			ShellExecute(GetSafeHwnd(), _T("open"), _T("notepad.exe"), file, nullptr, SW_SHOWNORMAL);   // .md 기본 연결 프로그램 대신 메모장
		m_input.SetWindowText(_T(""));
		return;
	}

	// 슬래시 명령 (7.2.13): 입력창에 정확히 /구조, /생성, /수정 을 넣으면 퀵 액션과 같은 동작을 한다
	static const struct { LPCTSTR cmd; UINT id; } kSlash[] = {
		{ _T("/구조"), IDC_CHAT_Q_FIRST },
		{ _T("/생성"), IDC_CHAT_Q_FIRST + 1 },
		{ _T("/수정"), IDC_CHAT_Q_FIRST + 2 },   // 결과 반영 (고친 장면을 다시 반영)
	};
	for (const auto& s : kSlash) {
		if (text.CompareNoCase(s.cmd) == 0) {
			OnQuickClicked(s.id);
			if (m_busy) m_input.SetWindowText(_T(""));   // 보내지 못했으면 입력을 남겨 둔다
			return;
		}
	}

	if (SendText(text, text)) m_input.SetWindowText(_T(""));
}

void CChatAgentWnd::RunAnalyze()
{
	OnQuickClicked(IDC_CHAT_Q_FIRST);
}

// Auto 버튼: 파일 > 에이전트 작업 시작 (패키지 생성) 후 구조 파악 → 웹페이지 생성을 차례로 보낸다
void CChatAgentWnd::OnAutoClicked()
{
	if (m_busy) return;
	if (CWnd* mainWnd = AfxGetMainWnd()) mainWnd->SendMessage(WM_COMMAND, ID_FILE_AGENT_START, 0);
	RunAutoChain();
}

// 구조 파악을 보내고, 끝나면 웹페이지 생성을 이어서 보낸다 (패키지는 이미 연결되어 있어야 한다)
void CChatAgentWnd::RunAutoChain()
{
	if (m_busy) return;
	m_chain.assign(1, 1);   // 다음 단계: 웹페이지 생성 (버튼 1)
	m_chainActive = true;
	OnQuickClicked(IDC_CHAT_Q_FIRST);   // 구조 파악 (버튼 0)
	if (!m_busy) {   // 보내지 못했으면 (패키지 없음 등) 체인을 끝낸다
		m_chain.clear();
		m_chainActive = false;
	}
}

void CChatAgentWnd::OnFrameClicked()
{
	// 파일 > 에이전트 작업 시작과 같은 방식으로 문서 명령을 보낸다 (선택 프레임 패키지 만들기)
	if (CWnd* mainWnd = AfxGetMainWnd()) mainWnd->PostMessage(WM_COMMAND, ID_FILE_AGENT_SEND_FRAME, 0);
}

void CChatAgentWnd::RunFrameRequest(const CString& frameName, float width, float height)
{
	CString prompt;
	prompt.Format(_T("이 패키지는 선택한 프레임 하나만 다룹니다 (프레임: %s, %d x %d px). ")
		_T("scene.json 의 root 가 그 프레임이고 preview.png 가 그 영역입니다. ")
		_T("이 프레임 안에 들어갈 내용만 결과 SVG 로 만들어 m2draw_put_result 로 저장하세요. ")
		_T("SVG 좌표는 프레임 왼쪽 위를 (0,0) 으로 쓰고, viewBox 는 0 0 %d %d 로 맞추세요."),
		(LPCTSTR)frameName, static_cast<int>(width), static_cast<int>(height), static_cast<int>(width), static_cast<int>(height));
	SendText(prompt, _T("선택 프레임 요청: ") + frameName, 5);
}

void CChatAgentWnd::OnQuickClicked(UINT id)
{
	// 자주 쓰는 요청: 지시문은 길게, 대화 기록에는 짧은 이름만 남긴다
	switch (id) {
	case IDC_CHAT_Q_FIRST:
		SendText(_T("작업 패키지의 scene.json 과 preview.png(있으면 reference.png)를 읽고 프레임 구조를 요약해 주세요. ")
			_T("계층, 역할(role), 레이아웃 방향, 크기 기준으로 정리하고, 아직 파일은 만들지 마세요. 확인 질문 없이 요약만 하세요."), _T("구조 파악"), 0);
		break;
	case IDC_CHAT_Q_FIRST + 1:
		SendText(_T("prompt.md 지침에 따라 웹페이지를 만들어 주세요. ")
			_T("결과는 m2draw_get_guide 의 결과 SVG 규칙대로 작성해 m2draw_put_result 로 저장하세요."), _T("웹페이지 생성"), 1);
		break;
	case IDC_CHAT_Q_FIRST + 2:
		SendText(_T("앱에서 고친 현재 장면(current/scene.json)을 다시 읽고, 바뀐 부분을 반영한 결과를 m2draw_put_result 로 저장해 주세요. ")
			_T("바뀐 점을 짧게 알려 주세요."), _T("결과 반영"), 2);
		break;
	case IDC_CHAT_Q_FIRST + 3:
		m_sessionId.clear();   // 다음 요청부터 새 대화 세션으로 처리
		AppendLine(_T("새 대화를 시작했습니다. 다음 요청은 새 세션으로 보냅니다."));
		break;
	case IDC_CHAT_Q_START:
		// 파일 > 에이전트 작업 시작과 같은 명령 (작업 패키지를 새로 만들고 이 탭에 연결)
		if (CWnd* mainWnd = AfxGetMainWnd()) mainWnd->PostMessage(WM_COMMAND, ID_FILE_AGENT_START, 0);
		break;
	default:
		break;
	}
}

bool CChatAgentWnd::SendText(const CString& promptText, const CString& shown, int button)
{
	if (m_busy) return false;
	if (m_packageDir.IsEmpty()) {
		AppendLine(_T("먼저 파일 > 에이전트 작업 시작을 눌러 주세요."));
		return false;
	}

	AppendLine(_T("나: ") + shown);
	SetChangedBadge(false);   // 요청을 보내면 에이전트가 current 를 읽는다
	m_busy = true;
	m_send.EnableWindow(FALSE);
	StartJob(m_chainActive ? 6 : button, shown);
	AppendLine(m_agentName + _T(": 생각 중..."));

	// 어떤 패키지를 다루는지 맥락으로 붙인다
	std::wstring prompt = std::wstring(L"[M2Draw 작업 패키지 폴더: ") + (LPCTSTR)m_packageDir + L"]\n";
	if (!m_site.IsEmpty()) prompt += std::wstring(L"[대상 사이트: ") + (LPCTSTR)m_site + L"]\n";
	prompt += L"앱 안내는 m2draw_get_guide 로, 패키지 구조는 m2draw_get_scene 로 읽고, 결과는 m2draw_put_result 로 저장하세요.\n\n";
	prompt += (LPCTSTR)promptText;
	const std::string promptUtf8 = ChatUtf8(prompt);
	const std::wstring cwd = (LPCTSTR)m_packageDir;
	const std::string session = m_sessionId;
	const HWND hwnd = GetSafeHwnd();

	std::thread([=]() {
		std::string out;
		ChatReply* reply = new ChatReply();
		if (!RunClaudeOnce(promptUtf8, cwd, session, out)) {
			reply->error = true;
			reply->text = L"Claude Code 를 실행하지 못했습니다. 설치되어 있는지 확인하세요 (claude 명령).";
		} else {
			std::string result, sid;
			JsonStringField(out, "session_id", sid);
			reply->session = sid;
			if (JsonStringField(out, "result", result) && !result.empty()) {
				reply->text = ChatWide(result);
			} else {
				reply->error = true;
				reply->text = L"응답을 읽지 못했습니다: " + ChatWide(out.substr(0, 300));
			}
		}
		::PostMessageW(hwnd, WM_CHAT_REPLY, 0, reinterpret_cast<LPARAM>(reply));
	}).detach();
	return true;
}

// 작업 상태: 실행 중이면 파란색으로 깜빡이고, 끝나면 빨간색으로 고정한다 (다음 작업이 시작될 때까지)
void CChatAgentWnd::StartJob(int button, const CString& label)
{
	if (m_jobButton >= 0 && m_quick[m_jobButton].GetSafeHwnd()) m_quick[m_jobButton].Invalidate(FALSE);
	m_jobButton = button;
	m_jobLabel = label;
	m_jobState = 1;
	m_jobStart = ::GetTickCount();
	m_blinkOn = true;
	SetTimer(IDC_CHAT_JOB_TIMER, 500, nullptr);
	UpdateJobUi();
}

void CChatAgentWnd::FinishJob(bool ok)
{
	SetTimer(IDC_CHAT_JOB_TIMER, 2000, nullptr);   // 완료 표시는 2초만 빨간색으로 두고, 그 뒤 일반 버튼으로 돌아간다
	m_jobState = 2;
	const DWORD secs = (::GetTickCount() - m_jobStart) / 1000;
	CString msg;
	msg.Format(_T("%s: %s (%u:%02u)"), ok ? _T("완료") : _T("실패"), (LPCTSTR)m_jobLabel, secs / 60, secs % 60);
	m_statusColor = RGB(200, 40, 40);
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) mf->NoteUnsavedDocument();   // 저장 안 된 문서면 한 줄 안내 (8.2)
	if (m_status.GetSafeHwnd()) {
		m_status.SetWindowText(msg);
		m_status.Invalidate(FALSE);
	}
	if (m_jobButton >= 0 && m_quick[m_jobButton].GetSafeHwnd()) m_quick[m_jobButton].Invalidate(FALSE);
}

void CChatAgentWnd::UpdateJobUi()
{
	const DWORD secs = (::GetTickCount() - m_jobStart) / 1000;
	const CString dot = m_blinkOn ? _T("●") : _T("○");
	CString msg;
	msg.Format(_T("%s 실행 중: %s (%u:%02u)"), (LPCTSTR)dot, (LPCTSTR)m_jobLabel, secs / 60, secs % 60);
	m_statusColor = RGB(30, 110, 255);
	if (m_status.GetSafeHwnd()) {
		m_status.SetWindowText(msg);
		m_status.Invalidate(FALSE);
	}
	if (m_jobButton >= 0 && m_quick[m_jobButton].GetSafeHwnd()) m_quick[m_jobButton].Invalidate(FALSE);
}

void CChatAgentWnd::OnTimer(UINT_PTR idEvent)
{
	if (idEvent == IDC_CHAT_JOB_TIMER && m_jobState == 2) {
		// 완료 표시 시간이 지나면 일반 버튼으로 되돌린다
		KillTimer(IDC_CHAT_JOB_TIMER);
		const int doneButton = m_jobButton;
		m_jobState = 0;
		m_jobButton = -1;
		if (doneButton >= 0 && m_quick[doneButton].GetSafeHwnd()) m_quick[doneButton].Invalidate(FALSE);
		return;
	}
	if (idEvent == IDC_CHAT_JOB_TIMER && m_jobState == 1) {
		m_blinkOn = !m_blinkOn;
		UpdateJobUi();
		return;
	}
	CWnd::OnTimer(idEvent);
}

// 퀵 버튼: 기본은 회색 버튼, 실행 중은 파란색 깜빡임, 끝나면 빨간색
void CChatAgentWnd::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	int idx = -1;
	for (int i = 0; i < 7; ++i)
		if (m_quick[i].GetDlgCtrlID() == nIDCtl) idx = i;
	if (idx < 0 || !lpDrawItemStruct) {
		CWnd::OnDrawItem(nIDCtl, lpDrawItemStruct);
		return;
	}

	CDC* dc = CDC::FromHandle(lpDrawItemStruct->hDC);
	CRect rc(lpDrawItemStruct->rcItem);
	COLORREF fill = ::GetSysColor(COLOR_BTNFACE);
	COLORREF text = ::GetSysColor(COLOR_BTNTEXT);
	if (idx == m_jobButton && m_jobState == 1) {
		fill = m_blinkOn ? PanelStyle::kRunFillOn : PanelStyle::kRunFillOff;
		text = m_blinkOn ? PanelStyle::kRunTextOn : PanelStyle::kRunTextOff;
	} else if (idx == m_jobButton && m_jobState == 2) {
		fill = PanelStyle::kDoneFill;
		text = PanelStyle::kDoneText;
	}
	if (lpDrawItemStruct->itemState & ODS_SELECTED)   // 누르는 동안 조금 어둡게
		fill = RGB(GetRValue(fill) * 3 / 4, GetGValue(fill) * 3 / 4, GetBValue(fill) * 3 / 4);

	dc->FillSolidRect(rc, fill);
	dc->Draw3dRect(rc, ::GetSysColor(COLOR_BTNHIGHLIGHT), ::GetSysColor(COLOR_BTNSHADOW));
	dc->SetBkMode(TRANSPARENT);
	dc->SetTextColor(text);
	CFont* old = dc->SelectObject(m_quick[idx].GetFont());
	dc->DrawText(m_quickBase[idx], rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc->SelectObject(old);
	if (lpDrawItemStruct->itemState & ODS_FOCUS) {
		CRect focus = rc;
		focus.DeflateRect(3, 3);
		dc->DrawFocusRect(focus);
	}
}

// 상태 줄 글자색 (실행 중 파랑, 끝 빨강)
HBRUSH CChatAgentWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (pWnd && pWnd->GetDlgCtrlID() == IDC_CHAT_STATUS) {
		pDC->SetTextColor(m_statusColor);
		pDC->SetBkMode(TRANSPARENT);
		return ::GetSysColorBrush(COLOR_3DFACE);
	}
	return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}
void CChatAgentWnd::SetAgentName(const CString& name)
{
	m_agentName = name;
	UpdateTabLabel();
}

void CChatAgentWnd::UpdateTabLabel()
{
	// 출력 창의 탭 이름도 같이 바꾼다 (변경됨이면 ● 를 붙인다)
	CMFCTabCtrl* tabs = DYNAMIC_DOWNCAST(CMFCTabCtrl, GetParent());
	if (!tabs) return;
	const CString label = m_changed ? m_agentName + _T(" ●") : m_agentName;
	for (int i = 0; i < tabs->GetTabsNum(); ++i) {
		if (tabs->GetTabWnd(i) == this) {
			tabs->SetTabLabel(i, label);
			break;
		}
	}
}

void CChatAgentWnd::SetChangedBadge(bool on)
{
	if (m_changed == on) return;
	m_changed = on;
	UpdateTabLabel();
}

LRESULT CChatAgentWnd::OnChatVersion(WPARAM, LPARAM lParam)
{
	std::unique_ptr<std::wstring> name(reinterpret_cast<std::wstring*>(lParam));
	SetAgentName(CString(name->c_str()));
	return 0;
}

LRESULT CChatAgentWnd::OnChatReply(WPARAM, LPARAM lParam)
{
	std::unique_ptr<ChatReply> reply(reinterpret_cast<ChatReply*>(lParam));
	m_busy = false;
	m_send.EnableWindow(TRUE);
	FinishJob(!reply->error);
	// 자동 처리 체인: 실패하면 중단, 성공하면 다음 단계
	if (reply->error) {
		m_chain.clear();
		m_chainActive = false;
	} else if (!m_chain.empty()) {
		const int next = m_chain.front();
		m_chain.erase(m_chain.begin());
		OnQuickClicked(IDC_CHAT_Q_FIRST + next);
	} else {
		m_chainActive = false;
	}
	if (!reply->session.empty()) m_sessionId = reply->session;
	// "생각 중..." 줄을 지우지 않고 아래에 답을 덧붙인다 (대화 기록을 그대로 보이게)
	if (reply->error) {
		AppendLine(_T("[오류] ") + CString(reply->text.c_str()));
	} else {
		// 답변은 마크다운으로 해석해 서식으로 보여 준다
		AppendLabel(m_agentName + _T(":"));
		RenderMarkdown(CString(reply->text.c_str()));
	}
	return 0;
}