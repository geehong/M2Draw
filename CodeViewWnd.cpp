#include "pch.h"
#include "CodeViewWnd.h"
#include "M2DrawDoc.h"
#include "SvgWriter.h"
#include "HtmlExporter.h"

#include <string>

namespace {

constexpr UINT IDC_CODE_KIND = 3101;
constexpr UINT IDC_CODE_REFRESH = 3102;
constexpr UINT IDC_CODE_COPY = 3103;
constexpr UINT IDC_CODE_VIEW = 3104;

enum CodeKind { kSvg = 0, kDomTags = 1, kHtmlExport = 2 };

// UTF-8 -> 와이드 문자열
std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

// 편집 창은 줄바꿈을 CRLF 로 보여 줘야 한다 (LF 만 있으면 한 줄로 붙어 보인다)
CString ToEditText(const std::wstring& w)
{
	std::wstring out;
	out.reserve(w.size() + 64);
	for (wchar_t c : w) {
		if (c == L'\n') out += L"\r\n";
		else if (c != L'\r') out += c;
	}
	return CString(out.c_str());
}

bool IsBase64Char(wchar_t c)
{
	return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'+' || c == L'/' || c == L'=';
}

// data:image/...;base64,... 를 "레퍼런스 이미지 생략{N KB}" 로 줄인다 (화면 표시용. 복사는 원문 그대로)
// N 은 디코딩했을 때의 대략적인 크기(KB)
std::wstring AbbreviateImageData(const std::wstring& s)
{
	const std::wstring key = L"data:image/";
	const std::wstring marker = L";base64,";
	std::wstring out;
	size_t pos = 0;
	while (true) {
		const size_t at = s.find(key, pos);
		if (at == std::wstring::npos) {
			out.append(s, pos, std::wstring::npos);
			break;
		}
		const size_t marked = s.find(marker, at);
		if (marked == std::wstring::npos || marked - at > 40) {
			// base64 가 아닌 data URI 는 그대로 둔다
			out.append(s, pos, at + key.size() - pos);
			pos = at + key.size();
			continue;
		}
		const size_t dataStart = marked + marker.size();
		size_t dataEnd = dataStart;
		while (dataEnd < s.size() && IsBase64Char(s[dataEnd])) ++dataEnd;
		const size_t bytes = (dataEnd - dataStart) * 3 / 4;
		out.append(s, pos, at - pos);
		out += L"레퍼런스 이미지 생략{" + std::to_wstring(bytes / 1024) + L"KB}";
		pos = dataEnd;
	}
	return out;
}

// 클립보드에 텍스트를 넣는다
void CopyTextToClipboard(const CString& text)
{
	if (!OpenClipboard(nullptr)) return;
	EmptyClipboard();
	const size_t bytes = (text.GetLength() + 1) * sizeof(TCHAR);
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (mem) {
		memcpy(GlobalLock(mem), static_cast<LPCTSTR>(text), bytes);
		GlobalUnlock(mem);
		if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
	}
	CloseClipboard();
}

// 현재 활성 MDI 자식의 문서 (없으면 nullptr)
CM2DrawDoc* ActiveDoc()
{
	CMDIFrameWnd* mainFrame = DYNAMIC_DOWNCAST(CMDIFrameWnd, AfxGetMainWnd());
	CFrameWnd* child = mainFrame ? mainFrame->GetActiveFrame() : nullptr;
	return child ? DYNAMIC_DOWNCAST(CM2DrawDoc, child->GetActiveDocument()) : nullptr;
}

} // namespace

BEGIN_MESSAGE_MAP(CCodeViewWnd, CWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_ERASEBKGND()
	ON_CBN_SELCHANGE(IDC_CODE_KIND, &CCodeViewWnd::OnKindChanged)
	ON_BN_CLICKED(IDC_CODE_REFRESH, &CCodeViewWnd::OnRefreshClicked)
	ON_BN_CLICKED(IDC_CODE_COPY, &CCodeViewWnd::OnCopyClicked)
END_MESSAGE_MAP()

CCodeViewWnd::CCodeViewWnd() {}
CCodeViewWnd::~CCodeViewWnd() {}

int CCodeViewWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;

	CFont* font = CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
	CRect rc(0, 0, 0, 0);
	m_kind.Create(WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, rc, this, IDC_CODE_KIND);
	m_refresh.Create(_T("Refresh"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, rc, this, IDC_CODE_REFRESH);
	m_copy.Create(_T("Copy"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, rc, this, IDC_CODE_COPY);
	m_view.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
		ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY, rc, this, IDC_CODE_VIEW);

	m_kind.SetFont(font);
	m_refresh.SetFont(font);
	m_copy.SetFont(font);
	m_view.SetFont(font);
	// 기본 글자 수 제한(32K)을 풀어 큰 문서의 코드도 전부 보이게 한다
	m_view.SendMessage(EM_LIMITTEXT, 0x7FFFFFFE, 0);

	m_kind.AddString(_T("SVG Code"));
	m_kind.AddString(_T("HTML Tags (Site DOM)"));
	m_kind.AddString(_T("HTML Export Code"));
	m_kind.SetCurSel(kSvg);
	return 0;
}

BOOL CCodeViewWnd::OnEraseBkgnd(CDC* pDC)
{
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(rc, ::GetSysColor(COLOR_3DFACE));
	return TRUE;
}

void CCodeViewWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	// 툴바는 왼쪽부터 고정 크기로 정렬 (드롭다운 목록 높이는 200)
	const int rowH = 22, gap = 2;
	const int kindW = 200, btnW = 84;
	if (m_kind.GetSafeHwnd()) m_kind.MoveWindow(0, 0, kindW, 200);
	if (m_refresh.GetSafeHwnd()) m_refresh.MoveWindow(kindW + gap, 0, btnW, rowH);
	if (m_copy.GetSafeHwnd()) m_copy.MoveWindow(kindW + gap + btnW + gap, 0, btnW, rowH);
	const int viewY = rowH + gap;
	if (m_view.GetSafeHwnd()) m_view.MoveWindow(0, viewY, cx, cy - viewY > 0 ? cy - viewY : 0);
}

void CCodeViewWnd::OnKindChanged()
{
	Refresh();
}

void CCodeViewWnd::OnRefreshClicked()
{
	Refresh();
}

// 복사는 화면에 줄인 값이 아니라 원문 전체를 넣는다
void CCodeViewWnd::OnCopyClicked()
{
	CopyTextToClipboard(m_fullText);
}

// 선택한 종류에 맞춰 활성 문서에서 내용을 만든다 (자동 갱신하지 않고 필요할 때만 생성)
void CCodeViewWnd::Refresh()
{
	CM2DrawDoc* doc = ActiveDoc();
	if (!doc) {
		m_fullText = _T("열린 문서가 없습니다.");
		m_view.SetWindowText(m_fullText);
		return;
	}

	std::wstring body;
	switch (m_kind.GetCurSel()) {
	case kSvg: {
		ge::SvgWriter writer;
		body = Utf8ToWide(writer.WriteString(doc->GetDoc()));
		break;
	}
	case kDomTags: {
		if (doc->m_agentLastDom.empty())
			body = L"가져온 사이트 정보가 없습니다. 주소창에 URL 을 넣고 Get Ref 를 누르면 여기에 표시됩니다.";
		else
			body = doc->m_agentLastDom;
		break;
	}
	case kHtmlExport: {
		ge::HtmlExportOptions opt;
		opt.singleFile = false;   // index.html + styles.css 로 나눠서 보여 준다 (웹 프로젝트 묶음과 같은 구성)
		ge::HtmlExporter exporter;
		const ge::HtmlExportResult result = exporter.Export(doc->GetDoc(), opt);
		std::string all = result.html;
		if (!result.css.empty()) all += "\n/* styles.css */\n" + result.css;
		body = Utf8ToWide(all);
		break;
	}
	default:
		break;
	}

	m_fullText = ToEditText(body);                       // 복사용 원문
	m_view.SetWindowText(ToEditText(AbbreviateImageData(body)));   // 화면용: 이미지 데이터는 줄여 보인다
}
