
// M2DrawDoc.cpp: CM2DrawDoc 클래스의 구현
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS는 미리 보기, 축소판 그림 및 검색 필터 처리기를 구현하는 ATL 프로젝트에서 정의할 수 있으며
// 해당 프로젝트와 문서 코드를 공유하도록 해 줍니다.
#ifndef SHARED_HANDLERS
#include "M2Draw.h"
#endif

#include "M2DrawDoc.h"
#include "GroupTransform.h"
#include "M2DrawView.h"
#include "MainFrm.h"
#include "ITool.h"
#include "SvgWriter.h"
#include "SvgReader.h"
#include "HtmlExporter.h"
#include "ClipboardSvg.h"
#include "CaptureFile.h"
#include "RasterExport.h"
#include "ClipboardImage.h"
#include "ReferenceImport.h"
#include "AgentHandoff.h"
#include "AgentGuide.h"
namespace { std::wstring AgentWide(const std::string& s); }
namespace {
// 클립보드 텍스트가 HTML 코드(예: 개발자도구의 outerHTML 복사)인지 대략 확인한다: 첫 글자가 '<' 이고 태그가 닫히는 '>' 가 있다
bool LooksLikeHtmlText(const std::wstring& t)
{
	const size_t first = t.find_first_not_of(L" \t\r\n");
	return first != std::wstring::npos && t[first] == L'<' && t.find(L'>') != std::wstring::npos;
}
} // namespace
#include "SceneJsonWriter.h"
#include "IconSet.h"
#include "LogoVariants.h"
#include "WebCapture.h"
#include "AnnotationNote.h"
#include "WebFontDlg.h"
#include <fstream>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>

#include <propkey.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CM2DrawDoc

IMPLEMENT_DYNCREATE(CM2DrawDoc, CDocument)

BEGIN_MESSAGE_MAP(CM2DrawDoc, CDocument)
	ON_COMMAND(ID_EDIT_UNDO, &CM2DrawDoc::OnEditUndo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, &CM2DrawDoc::OnUpdateEditUndo)
	ON_COMMAND(ID_EDIT_REDO, &CM2DrawDoc::OnEditRedo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, &CM2DrawDoc::OnUpdateEditRedo)
	ON_COMMAND(ID_OBJECT_GROUP, &CM2DrawDoc::OnObjectGroup)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_GROUP, &CM2DrawDoc::OnUpdateObjectGroup)
	ON_COMMAND(ID_OBJECT_UNGROUP, &CM2DrawDoc::OnObjectUngroup)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_UNGROUP, &CM2DrawDoc::OnUpdateObjectUngroup)
	ON_COMMAND(ID_OBJECT_WRAP_FRAME, &CM2DrawDoc::OnObjectWrapFrame)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_WRAP_FRAME, &CM2DrawDoc::OnUpdateObjectWrapFrame)
	ON_COMMAND(ID_OBJECT_MAKE_COMPOUND, &CM2DrawDoc::OnObjectMakeCompound)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_MAKE_COMPOUND, &CM2DrawDoc::OnUpdateObjectMakeCompound)
	ON_COMMAND(ID_OBJECT_RELEASE_COMPOUND, &CM2DrawDoc::OnObjectReleaseCompound)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_RELEASE_COMPOUND, &CM2DrawDoc::OnUpdateObjectReleaseCompound)
	ON_COMMAND_RANGE(ID_OBJECT_FLIP_H, ID_OBJECT_ROT_CCW, &CM2DrawDoc::OnObjectTransform)
	ON_UPDATE_COMMAND_UI_RANGE(ID_OBJECT_FLIP_H, ID_OBJECT_ROT_CCW, &CM2DrawDoc::OnUpdateObjectTransform)
	ON_COMMAND_RANGE(ID_OBJECT_FRONT, ID_OBJECT_BACK, &CM2DrawDoc::OnObjectArrange)
	ON_UPDATE_COMMAND_UI_RANGE(ID_OBJECT_FRONT, ID_OBJECT_BACK, &CM2DrawDoc::OnUpdateObjectArrange)
	ON_COMMAND_RANGE(ID_BOOLEAN_UNION, ID_BOOLEAN_TRIM, &CM2DrawDoc::OnBooleanOperation)
	ON_UPDATE_COMMAND_UI_RANGE(ID_BOOLEAN_UNION, ID_BOOLEAN_TRIM, &CM2DrawDoc::OnUpdateBooleanOperation)
	ON_COMMAND(ID_OBJECT_CLIP_MASK, &CM2DrawDoc::OnObjectClipMask)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_CLIP_MASK, &CM2DrawDoc::OnUpdateObjectClipMask)
	ON_COMMAND(ID_OBJECT_RELEASE_MASK, &CM2DrawDoc::OnObjectReleaseMask)
	ON_UPDATE_COMMAND_UI(ID_OBJECT_RELEASE_MASK, &CM2DrawDoc::OnUpdateObjectReleaseMask)
	ON_COMMAND_RANGE(ID_ALIGN_LEFT, ID_DISTRIBUTE_V, &CM2DrawDoc::OnAlignCommand)
	ON_UPDATE_COMMAND_UI_RANGE(ID_ALIGN_LEFT, ID_DISTRIBUTE_V, &CM2DrawDoc::OnUpdateAlignCommand)
	ON_COMMAND(ID_FILE_PLACE_IMAGE, &CM2DrawDoc::OnFilePlaceImage)
	ON_COMMAND(ID_FILE_PLACE_REFERENCE, &CM2DrawDoc::OnFilePlaceReference)
	ON_COMMAND(ID_FILE_EXPORT_AGENT, &CM2DrawDoc::OnFileExportAgent)
	ON_COMMAND(ID_FILE_IMPORT_AGENT_SVG, &CM2DrawDoc::OnFileImportAgentSvg)
	ON_COMMAND(ID_FILE_EXPORT_ICONSET, &CM2DrawDoc::OnFileExportIconSet)
	ON_COMMAND(ID_FILE_EXPORT_LOGO, &CM2DrawDoc::OnFileExportLogo)
	ON_COMMAND(ID_FILE_EXPORT_SVG_OPT, &CM2DrawDoc::OnFileExportSvgOptimized)
	ON_COMMAND(ID_FILE_EXPORT_WEB, &CM2DrawDoc::OnFileExportWebBundle)
	ON_COMMAND(ID_FILE_IMPORT_URL, &CM2DrawDoc::OnFileImportUrlReference)
	ON_COMMAND(ID_FILE_ADD_ANNOTATION, &CM2DrawDoc::OnFileAddAnnotation)
	ON_COMMAND(ID_FILE_WEBFONT, &CM2DrawDoc::OnFileWebFontLink)
	ON_COMMAND(ID_FILE_AGENT_START, &CM2DrawDoc::OnFileAgentStart)
	ON_COMMAND(ID_FILE_AGENT_SEND_FRAME, &CM2DrawDoc::OnFileAgentSendFrame)
	ON_COMMAND(ID_FILE_PAGE_NEW, &CM2DrawDoc::OnFilePageNew)
	ON_COMMAND(ID_FILE_PAGE_OPEN, &CM2DrawDoc::OnFilePageOpen)
	ON_COMMAND(ID_FILE_PAGE_RENAME, &CM2DrawDoc::OnFilePageRename)
	ON_COMMAND(ID_FILE_PROJECT_CLOSE, &CM2DrawDoc::OnFileProjectClose)
	ON_COMMAND(ID_FILE_PAGE_IMPORT, &CM2DrawDoc::OnFilePageImport)
	ON_COMMAND(ID_FILE_EXPORT_SVG, &CM2DrawDoc::OnFileExportSvg)
	ON_COMMAND(ID_FILE_EXPORT_HTML, &CM2DrawDoc::OnFileExportHtml)
	ON_COMMAND(ID_FILE_EXPORT_IMAGE, &CM2DrawDoc::OnFileExportImage)
	ON_COMMAND(ID_EDIT_COPY, &CM2DrawDoc::OnEditCopy)
	ON_COMMAND(ID_EDIT_CUT, &CM2DrawDoc::OnEditCut)
	ON_COMMAND(ID_EDIT_PASTE, &CM2DrawDoc::OnEditPaste)
	ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, &CM2DrawDoc::OnUpdateEditCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, &CM2DrawDoc::OnUpdateEditCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, &CM2DrawDoc::OnUpdateEditPaste)
	ON_COMMAND(ID_TEXT_TO_PATH, &CM2DrawDoc::OnTextToPath)
	ON_UPDATE_COMMAND_UI(ID_TEXT_TO_PATH, &CM2DrawDoc::OnUpdateTextToPath)
	ON_COMMAND(ID_PATH_OUTLINE, &CM2DrawDoc::OnPathOutline)
	ON_UPDATE_COMMAND_UI(ID_PATH_OUTLINE, &CM2DrawDoc::OnUpdatePathOutline)
	ON_COMMAND(ID_PATH_OFFSET, &CM2DrawDoc::OnPathOffset)
	ON_UPDATE_COMMAND_UI(ID_PATH_OFFSET, &CM2DrawDoc::OnUpdatePathOffset)
	ON_COMMAND(ID_PATH_SIMPLIFY, &CM2DrawDoc::OnPathSimplify)
	ON_UPDATE_COMMAND_UI(ID_PATH_SIMPLIFY, &CM2DrawDoc::OnUpdatePathSimplify)
	ON_COMMAND(ID_PATH_REVERSE, &CM2DrawDoc::OnPathReverse)
	ON_UPDATE_COMMAND_UI(ID_PATH_REVERSE, &CM2DrawDoc::OnUpdatePathReverse)
	ON_COMMAND(ID_PATH_JOIN, &CM2DrawDoc::OnPathJoin)
	ON_UPDATE_COMMAND_UI(ID_PATH_JOIN, &CM2DrawDoc::OnUpdatePathJoin)
END_MESSAGE_MAP()


// CM2DrawDoc 생성/소멸

CM2DrawDoc::CM2DrawDoc() noexcept
{
	// TODO: 여기에 일회성 생성 코드를 추가합니다.

}

CM2DrawDoc::~CM2DrawDoc()
{
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pFrame = static_cast<CMainFrame*>(pMainWnd);
		pFrame->GetPropertiesWnd().SetSelectedShape(nullptr, nullptr);
		pFrame->GetGridPropertiesWnd().SetSelectedShape(nullptr, nullptr);
		pFrame->GetCanvasPropertiesWnd().SetSelectedShape(nullptr, nullptr);
	}
}

BOOL CM2DrawDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	m_currentPath.reset();
	m_doc = ge::Document();
	m_undoMgr.Reset(m_doc);
	ResetToolInteraction();
	SetModifiedFlag(FALSE);

	return TRUE;
}

ge::Node* CM2DrawDoc::GetPrimarySelection() const
{
	const auto& sel = m_doc.GetSelection();
	return sel.empty() ? nullptr : sel.front();
}

bool CM2DrawDoc::PruneNonInteractiveSelection()
{
	const auto& sel = m_doc.GetSelection();
	std::vector<ge::Node*> kept;
	for (ge::Node* n : sel)
	{
		if (n && n->IsEffectivelyVisible() && !n->IsEffectivelyLocked())
			kept.push_back(n);
	}
	if (kept.size() == sel.size())
		return false;
	m_doc.SetSelection(kept);
	return true;
}




// CM2DrawDoc serialization (Phase 1: SVG 포맷 기반 역직렬화/직렬화)

void CM2DrawDoc::Serialize(CArchive& /*ar*/)
{
	// M2Draw 는 표준 SVG / M2Draw 확장 SVG 기반으로 파일 저장 및 열기를 처리하므로
	// 기본 CArchive 직렬화 대신 OnSaveDocument / OnOpenDocument 를 직접 사용합니다.
}

namespace {

// 프로젝트 상태: 인스턴스당 하나. 열린 문서들은 모두 이 프로젝트의 페이지가 된다
struct ProjectState
{
	std::wstring                            folder;     // 프로젝트 폴더 (이름\)
	std::wstring                            manifest;   // 목차 (이름\이름.m2svg)
	std::vector<ProjectPageEntry>           pages;
	std::vector<ProjectRefEntry>            refs;
	std::map<std::wstring, std::string>     refDoms;
};
ProjectState g_project;

// 프로젝트 파일 도우미 (목차 *.m2svg 읽기/쓰기)
std::string ProjUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}

std::string ProjEscape(const std::string& s)
{
	std::string out;
	for (char c : s) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		default: out += c;
		}
	}
	return out;
}

// name="값" 을 읽는다 (XML 이스케이프 되돌리기)
bool ProjAttr(const std::string& xml, const char* name, std::string& value)
{
	const std::string key = std::string(" ") + name + "=\"";
	const size_t at = xml.find(key);
	if (at == std::string::npos) return false;
	const size_t begin = at + key.size();
	const size_t end = xml.find('"', begin);
	if (end == std::string::npos) return false;
	value.clear();
	for (size_t i = begin; i < end; ++i) {
		if (xml.compare(i, 5, "&amp;") == 0) { value += '&'; i += 4; }
		else if (xml.compare(i, 4, "&lt;") == 0) { value += '<'; i += 3; }
		else if (xml.compare(i, 4, "&gt;") == 0) { value += '>'; i += 3; }
		else if (xml.compare(i, 6, "&quot;") == 0) { value += '"'; i += 5; }
		else value += xml[i];
	}
	return true;
}

bool AgentWriteFile(const std::filesystem::path& p, const std::string& data);   // 정의는 아래 에이전트 도우미 쪽

bool ProjReadAll(const std::filesystem::path& p, std::string& out)
{
	std::ifstream ifs(p, std::ios::binary);
	if (!ifs) return false;
	out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return true;
}

std::string ProjNow()
{
	CStringA s(CTime::GetCurrentTime().Format(_T("%Y-%m-%d %H:%M:%S")));
	return std::string((LPCSTR)s);
}

} // namespace

// 닫히기 전에 레이어 패널이 이 문서의 노드를 놓게 한다 (닫힌 뒤 옛 노드를 그리면 접근 위반이 난다)
void CM2DrawDoc::OnCloseDocument()
{
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
	{
		mf->GetLayerWnd().ReleaseDocument(this);
		mf->GetPropertiesWnd().ReleaseDocument(this);
		mf->GetOptionBar().ReleaseDocument(this);

	}
	CDocument::OnCloseDocument();
}

BOOL CM2DrawDoc::OnSaveDocument(LPCTSTR lpszPathName)
{
	if (!lpszPathName || lpszPathName[0] == _T('\0'))
		return FALSE;

	if (!SaveProject(lpszPathName))
		return FALSE;

	SetModifiedFlag(FALSE);

	CString strLog;
	strLog.Format(_T("[프로젝트 저장 완료] %s"), lpszPathName);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	return TRUE;
}

namespace {

// 상대 경로 (폴더 안이면 폴더 기준으로, 아니면 그대로)
std::wstring ProjRel(const std::wstring& path, const std::wstring& folder)
{
	const std::wstring prefix = folder + L"\\";
	if (!path.empty() && path.rfind(prefix, 0) == 0) return path.substr(prefix.size());
	return path;
}

// 목차에 적힌 경로를 실제 경로로 (상대 경로면 프로젝트 폴더 기준)
std::wstring ProjAbs(const std::wstring& path, const std::wstring& folder)
{
	if (path.empty()) return path;
	std::filesystem::path p(path);
	return p.is_relative() ? (std::filesystem::path(folder) / p).wstring() : path;
}

// 연결 경로(페이지 파일 기준)로 레퍼런스 그림을 다시 붙인다
void RelinkNode(ge::Node& node, const std::wstring& folder)
{
	if (auto* im = dynamic_cast<ge::ImageNode*>(&node)) {
		if (!im->GetData() && !im->GetLinkHref().empty()) {
			const std::filesystem::path file = (std::filesystem::path(folder) / L"pages" / AgentWide(im->GetLinkHref())).lexically_normal();
			std::string png;
			if (ProjReadAll(file, png) && png.size() >= 24) {
				auto data = std::make_shared<ge::ImageData>();
				data->bytes.assign(png.begin(), png.end());
				data->mime = "image/png";
				auto be32 = [&](size_t o) {
					return (static_cast<unsigned>(static_cast<unsigned char>(png[o])) << 24) | (static_cast<unsigned>(static_cast<unsigned char>(png[o + 1])) << 16) |
					       (static_cast<unsigned>(static_cast<unsigned char>(png[o + 2])) << 8) | static_cast<unsigned>(static_cast<unsigned char>(png[o + 3]));
				};
				data->pixelW = static_cast<int>(be32(16));
				data->pixelH = static_cast<int>(be32(20));
				im->SetData(data);
			}
		}
	}
	if (auto* g = dynamic_cast<ge::GroupNode*>(&node))
		for (const auto& c : g->GetChildren()) RelinkNode(*c, folder);
}

void RelinkDocument(ge::Document& doc, const std::wstring& folder)
{
	for (const auto& layer : doc.GetLayers()) RelinkNode(*layer, folder);
}

// 문서 안의 모든 이미지 노드 모으기
void CollectImages(ge::Node& node, std::vector<ge::ImageNode*>& out)
{
	if (auto* im = dynamic_cast<ge::ImageNode*>(&node)) out.push_back(im);
	if (auto* g = dynamic_cast<ge::GroupNode*>(&node))
		for (const auto& c : g->GetChildren()) CollectImages(*c, out);
}

// 파일 이름으로 쓸 수 없는 글자를 _ 로 바꾼다
std::wstring SafeFileBase(const std::wstring& name)
{
	std::wstring out = name;
	for (auto& ch : out)
		if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch < 0x20) ch = L'_';
	while (!out.empty() && (out.back() == L'.' || out.back() == L' ')) out.pop_back();
	return out.empty() ? std::wstring(L"reference") : out;
}

// 기본 레퍼런스 이름: 사이트이름_YYYYMMDDHHMM (예: https://www.webmd.com/ -> webmd_202611031257)
std::wstring DefaultReferenceName(const std::wstring& url)
{
	std::wstring host = url;
	if (const size_t scheme = host.find(L"://"); scheme != std::wstring::npos) host = host.substr(scheme + 3);
	host = host.substr(0, host.find(L'/'));
	if (host.rfind(L"www.", 0) == 0) host = host.substr(4);
	std::vector<std::wstring> labels;
	for (size_t s = 0, e = 0; s <= host.size(); s = e + 1) {
		e = host.find(L'.', s);
		if (e == std::wstring::npos) e = host.size();
		if (e > s) labels.push_back(host.substr(s, e - s));
	}
	std::wstring siteName = labels.empty() ? std::wstring(L"capture") : labels[labels.size() >= 2 ? labels.size() - 2 : 0];
	if (labels.size() >= 3 && labels.back().size() == 2 && labels[labels.size() - 2].size() <= 3)
		siteName = labels[labels.size() - 3];
	const CString stamp = CTime::GetCurrentTime().Format(_T("%Y%m%d%H%M"));
	return siteName + L"_" + std::wstring((LPCTSTR)stamp);
}
// 목차의 <m2:page ...> 와 <m2:reference ...> 태그를 하나씩 꺼낸다
std::vector<std::string> ProjTags(const std::string& xml, const char* tagName)
{
	std::vector<std::string> out;
	const std::string open = std::string("<") + tagName + " ";
	size_t pos = 0;
	while ((pos = xml.find(open, pos)) != std::string::npos) {
		const size_t end = xml.find('>', pos);
		if (end == std::string::npos) break;
		out.push_back(xml.substr(pos, end - pos + 1));
		pos = end + 1;
	}
	return out;
}

} // namespace

// 프로젝트 저장: 폴더 하나가 프로젝트다. 폴더 이름 = 프로젝트 이름
//   이름\ 이름.m2svg (목차) · pages\페이지.m2doc · references\NNN_이름\ · agent\페이지\
// 저장은 현재 페이지와 목차만 쓴다. 레퍼런스는 캡처할 때 따로 만들어진다.
BOOL CM2DrawDoc::SaveProject(LPCTSTR manifestPathLpsz)
{
	namespace fs = std::filesystem;
	const fs::path manifest((LPCTSTR)manifestPathLpsz);
	const fs::path folder = manifest.parent_path();
	std::error_code ec;
	fs::create_directories(folder / L"pages", ec);
	fs::create_directories(folder / L"references", ec);
	if (m_activePage.empty()) m_activePage = L"메인";

	// 현재 페이지 항목 (에이전트 위치 갱신)
	ProjectPageEntry* cur = nullptr;
	for (auto& pg : g_project.pages)
		if (pg.name == m_activePage) cur = &pg;
	if (!cur) {
		g_project.pages.push_back(ProjectPageEntry{ m_activePage, L"" });
		cur = &g_project.pages.back();
	}
	cur->agentDir = ProjRel(std::wstring((LPCTSTR)m_agentDir), folder.wstring());

	// 1) 현재 페이지 본체 (레퍼런스 그림은 번호 경로로 연결)
	ge::SvgWriteOptions opt(ge::SvgWriteMode::File);
	opt.linkReferenceImages = true;

	// 레퍼런스 그림: 레이어 이름이 파일 이름이 된다 (같은 이름이면 _2, _3 ...). 이름을 바꾸면 파일과 목차도 따라간다
	const fs::path refDir = folder / L"references";
	std::vector<ge::ImageNode*> images;
	for (const auto& layer : m_doc.GetLayers()) CollectImages(*layer, images);
	std::set<std::wstring> used;
	for (ge::ImageNode* im : images) {
		if (!ge::IsReferenceNode(*im)) continue;
		if (!im->GetData() && im->GetLinkHref().empty()) continue;
		const std::wstring oldBase = im->GetLinkHref().empty() ? std::wstring() : fs::path(AgentWide(im->GetLinkHref())).stem().wstring();
		const std::wstring base = SafeFileBase(im->GetName());
		// 같은 이름이 이미 있으면 _2, _3 ... 을 붙인다 (다른 레퍼런스의 파일은 덮어쓰지 않는다)
		std::wstring cand = base;
		for (int k = 2; used.count(cand) || (cand != oldBase && fs::exists(refDir / (cand + L".png")) ); ++k)
			cand = base + L"_" + std::to_wstring(k);
		used.insert(cand);

		const fs::path target = refDir / (cand + L".png");
		if (im->GetData()) {
			AgentWriteFile(target, std::string(im->GetData()->bytes.begin(), im->GetData()->bytes.end()));
		} else {
			const fs::path src = (folder / L"pages" / AgentWide(im->GetLinkHref())).lexically_normal();
			std::error_code copyEc;
			if (fs::exists(src) && !fs::exists(target)) fs::copy_file(src, target, copyEc);
		}

		// DOM 요약 (이름이 바뀌면 같이 옮긴다)
		const std::wstring key = oldBase.empty() ? base : oldBase;
		if (key != cand && g_project.refDoms.count(key)) {
			g_project.refDoms[cand] = g_project.refDoms[key];
			g_project.refDoms.erase(key);
		}
		if (g_project.refDoms.count(cand)) AgentWriteFile(refDir / (cand + L".json"), g_project.refDoms[cand]);

		// 목차의 레퍼런스 항목 이름도 맞춘다
		for (auto& r : g_project.refs)
			if (r.leaf == key) r.leaf = cand;

		const std::string rel = "../references/" + ProjUtf8(cand) + ".png";
		opt.linkedImagePaths[im] = rel;
		im->SetLinkHref(rel);
	}
	ge::SvgWriter writer;
	if (!writer.WriteToFile(m_doc, (folder / L"pages" / (m_activePage + L".m2doc")).wstring(), opt)) {
		AfxMessageBox(_T("페이지를 저장하는 중 오류가 발생했습니다."), MB_ICONERROR);
		return FALSE;
	}

	// 3-b) 어느 페이지에도 열린 문서에도 쓰이지 않는 레퍼런스 파일과 항목을 지운다
	CleanupUnusedReferences(folder.wstring());

	// 2) 목차
	std::string created;
	std::string previous;
	if (ProjReadAll(manifest, previous)) ProjAttr(previous, "m2:created", created);
	if (created.empty()) created = ProjNow();

	std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	xml += "<m2:project xmlns:m2=\"https://m2draw/ns\" m2:version=\"2\"";
	xml += " m2:name=\"" + ProjEscape(ProjUtf8(manifest.stem().wstring())) + "\"";
	xml += " m2:created=\"" + ProjEscape(created) + "\"";
	xml += " m2:modified=\"" + ProjEscape(ProjNow()) + "\"";
	xml += " m2:site=\"" + ProjEscape(ProjUtf8(m_agentSiteUrl)) + "\"";
	xml += " m2:active=\"" + ProjEscape(ProjUtf8(m_activePage)) + "\">\n";
	for (const auto& pg : g_project.pages) {
		xml += "  <m2:page name=\"" + ProjEscape(ProjUtf8(pg.name)) + "\" file=\"" + ProjEscape(ProjUtf8(L"pages\\" + pg.name + L".m2doc")) +
		       "\" agent=\"" + ProjEscape(ProjUtf8(pg.agentDir)) + "\"/>\n";
	}
	for (const auto& r : g_project.refs) {
		xml += "  <m2:reference id=\"" + std::to_string(r.id) + "\" name=\"" + ProjEscape(ProjUtf8(r.leaf)) + "\" url=\"" + ProjEscape(ProjUtf8(r.url)) +
		       "\" created=\"" + ProjEscape(r.created) + "\" width=\"" + std::to_string(r.width) + "\" height=\"" + std::to_string(r.height) + "\"/>\n";
	}
	xml += "</m2:project>\n";
	if (!AgentWriteFile(manifest, xml)) {
		AfxMessageBox(_T("프로젝트 목차(*.m2svg)를 쓰는 중 오류가 발생했습니다."), MB_ICONERROR);
		return FALSE;
	}

	g_project.folder = folder.wstring();
	g_project.manifest = manifest.wstring();
	return TRUE;
}

// 프로젝트의 레퍼런스를 쓰고 있는 이름 (열린 문서 + 디스크의 페이지 파일). UTF-8 이름, 확장자 없음
std::set<std::string> CM2DrawDoc::UsedReferenceNames(const std::wstring& folderPath)
{
	namespace fs = std::filesystem;
	const fs::path folder(folderPath);
	std::set<std::string> used;
	auto collect = [&](ge::Node& n, auto&& self) -> void {
		if (auto* im = dynamic_cast<ge::ImageNode*>(&n))
			if (!im->GetLinkHref().empty())
				used.insert(ProjUtf8(fs::path(AgentWide(im->GetLinkHref())).stem().wstring()));
		if (auto* g = dynamic_cast<ge::GroupNode*>(&n))
			for (const auto& c : g->GetChildren()) self(*c, self);
	};
	// 열린 문서 (메모리에 있는 최신 상태)
	if (CDocTemplate* tmpl = nullptr; true) {
		POSITION tp = AfxGetApp()->GetFirstDocTemplatePosition();
		while (tp) {
			tmpl = AfxGetApp()->GetNextDocTemplate(tp);
			POSITION dp = tmpl->GetFirstDocPosition();
			while (dp) {
				if (CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(dp)))
					for (const auto& layer : d->m_doc.GetLayers()) collect(*layer, collect);
			}
		}
	}
	// 디스크의 페이지 파일 (열려 있지 않은 페이지 포함)
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(folder / L"pages", ec)) {
		if (entry.path().extension() != L".m2doc") continue;
		std::string xml;
		if (!ProjReadAll(entry.path(), xml)) continue;
		const std::string key = "href=\"../references/";
		size_t pos = 0;
		while ((pos = xml.find(key, pos)) != std::string::npos) {
			pos += key.size();
			const size_t end = xml.find('"', pos);
			if (end == std::string::npos) break;
			// 목차 파일은 UTF-8 이므로 std::string 으로 경로를 만들면 코드 페이지 변환에서 예외가 난다 (한글 이름). 넓은 문자열로 바꿔 쓴다
			used.insert(ProjUtf8(fs::path(AgentWide(xml.substr(pos, end - pos))).stem().wstring()));
			pos = end;
		}
	}
	return used;
}

// 쓰이지 않는 레퍼런스 파일과 목차 항목을 지운다
void CM2DrawDoc::CleanupUnusedReferences(const std::wstring& folderPath)
{
	namespace fs = std::filesystem;
	const fs::path folder(folderPath);
	const std::set<std::string> used = UsedReferenceNames(folderPath);
	std::error_code ec;
	for (const auto& entry : fs::directory_iterator(folder / L"references", ec)) {
		const auto ext = entry.path().extension();
		if (ext != L".png" && ext != L".json") continue;
		if (used.count(ProjUtf8(entry.path().stem().wstring())) == 0)
		{
			std::error_code rmEc;
			fs::remove(entry.path(), rmEc);
		}
	}
	g_project.refs.erase(std::remove_if(g_project.refs.begin(), g_project.refs.end(), [&](const ProjectRefEntry& r) {
		return used.count(ProjUtf8(r.leaf)) == 0;
	}), g_project.refs.end());
}

// 현재 문서를 바꾸고 화면을 갱신한다
void CM2DrawDoc::ReplaceDocument(ge::Document&& loaded)
{
	m_currentPath.reset();
	m_doc = std::move(loaded);
	ResetToolInteraction();
	m_undoMgr.Reset(m_doc);
	UpdateAllViews(nullptr);
	UpdateShapePropertiesSelection(nullptr, this);
	if (CWnd* pMainWnd = AfxGetMainWnd()) {
		if (pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
			static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}
}

// 한 프로그램 창에는 프로젝트를 하나만 연다. 열린 프로젝트 문서가 없으면 그대로 열고, 있으면 막는다
BOOL CM2DrawDoc::AllowOpenProject(LPCTSTR manifestPathLpsz)
{
	namespace fs = std::filesystem;
	int openOthers = 0;
	POSITION tp = AfxGetApp()->GetFirstDocTemplatePosition();
	while (tp) {
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(tp);
		POSITION dp = tmpl->GetFirstDocPosition();
		while (dp) {
			CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(dp));
			if (d && d != this && (d->m_attached || !d->GetPathName().IsEmpty()))
				++openOthers;
		}
	}
	if (openOthers == 0) {
		g_project = ProjectState();   // 열린 프로젝트 문서가 없으면 이전 상태는 버린다
		return TRUE;
	}

	const fs::path wanted = fs::path((LPCTSTR)manifestPathLpsz).parent_path();
	std::error_code ec;
	if (!g_project.folder.empty() && fs::equivalent(fs::path(g_project.folder), wanted, ec)) {
		AfxMessageBox(_T("이미 열려 있는 프로젝트입니다."), MB_ICONINFORMATION);
		return FALSE;
	}
	AfxMessageBox(_T("한 창에는 프로젝트를 하나만 열 수 있습니다.\n현재 프로젝트의 문서를 모두 닫은 뒤 다시 여세요."), MB_ICONINFORMATION);
	return FALSE;
}
// 프로젝트 열기: 목차에서 페이지, 레퍼런스, 현재 페이지를 읽고 현재 페이지를 연다
BOOL CM2DrawDoc::OpenProject(LPCTSTR manifestPathLpsz)
{
	namespace fs = std::filesystem;
	const fs::path manifest((LPCTSTR)manifestPathLpsz);
	const fs::path folder = manifest.parent_path();

	std::string xml;
	if (!ProjReadAll(manifest, xml)) {
		AfxMessageBox(_T("프로젝트 파일을 읽을 수 없습니다."), MB_ICONERROR);
		return FALSE;
	}
	g_project.folder = folder.wstring();

	g_project.manifest = manifest.wstring();
	m_attached = false;

	// 목차: 페이지, 레퍼런스, 현재 페이지, 주소
	g_project.pages.clear();
	g_project.refs.clear();
	for (const auto& tag : ProjTags(xml, "m2:page")) {
		std::string n, ag;
		ProjAttr(tag, "name", n);
		ProjAttr(tag, "agent", ag);
		g_project.pages.push_back(ProjectPageEntry{ AgentWide(n), AgentWide(ag) });
	}
	for (const auto& tag : ProjTags(xml, "m2:reference")) {
		ProjectRefEntry r;
		std::string id, leaf, url, w, h;
		ProjAttr(tag, "id", id);
		ProjAttr(tag, "name", leaf);
		ProjAttr(tag, "url", url);
		ProjAttr(tag, "created", r.created);
		ProjAttr(tag, "width", w);
		ProjAttr(tag, "height", h);
		r.id = id.empty() ? 0 : std::stoi(id);
		r.leaf = AgentWide(leaf);
		r.url = AgentWide(url);
		r.width = w.empty() ? 0 : std::stoi(w);
		r.height = h.empty() ? 0 : std::stoi(h);
		g_project.refs.push_back(r);
	}
	std::string active, site, legacyAgent;
	ProjAttr(xml, "m2:active", active);
	ProjAttr(xml, "m2:site", site);
	m_agentSiteUrl = AgentWide(site);
	m_activePage = active.empty() ? std::wstring(L"메인") : AgentWide(active);
	if (g_project.pages.empty()) {
		// 예전 형식 (페이지 목록 없음): 본체 하나를 "메인" 페이지로 본다
		ProjAttr(xml, "m2:agent", legacyAgent);
		g_project.pages.push_back(ProjectPageEntry{ L"메인", AgentWide(legacyAgent) });
		m_activePage = L"메인";
	}

	// 현재 페이지 본체 (예전 형식은 document.m2doc 또는 이름.m2data\document.m2doc)
	fs::path pagePath = folder / L"pages" / (m_activePage + L".m2doc");
	if (!fs::exists(pagePath)) pagePath = folder / L"document.m2doc";
	if (!fs::exists(pagePath)) pagePath = folder / (manifest.stem().wstring() + L".m2data") / L"document.m2doc";
	ge::Document loaded;
	ge::SvgReader reader;
	if (!reader.ReadFromFile(pagePath.wstring(), loaded)) {
		AfxMessageBox(_T("프로젝트 페이지를 열 수 없습니다."), MB_ICONERROR);
		return FALSE;
	}
	RelinkDocument(loaded, folder.wstring());

	// 현재 페이지의 에이전트 위치
	m_agentDir.Empty();
	for (const auto& pg : g_project.pages)
		if (pg.name == m_activePage && !pg.agentDir.empty())
			m_agentDir = CString(ProjAbs(pg.agentDir, folder.wstring()).c_str());

	// 마지막 캡처의 DOM 요약 (예전 형식은 folder\dom.json)
	g_project.refDoms.clear();
	std::error_code domEc;
	for (const auto& entry : fs::directory_iterator(folder / L"references", domEc)) {
		if (entry.path().extension() != L".json") continue;
		std::string text;
		if (ProjReadAll(entry.path(), text)) g_project.refDoms[entry.path().stem().wstring()] = text;
	}
	m_agentLastDom.clear();
	if (!g_project.refs.empty() && g_project.refDoms.count(g_project.refs.back().leaf)) m_agentLastDom = AgentWide(g_project.refDoms[g_project.refs.back().leaf]);
	m_capturePng.clear();

	m_agentFrameId = 0;
	m_agentResultSeen.clear();
	m_agentResultNodes.clear();
	std::error_code ec;
	if (!m_agentDir.IsEmpty()) {
		for (const auto& entry : fs::directory_iterator(fs::path((LPCTSTR)m_agentDir) / L"result", ec)) {
			if (!entry.is_regular_file(ec)) continue;
			m_agentResultSeen[entry.path().filename().wstring()] = fs::last_write_time(entry.path(), ec).time_since_epoch().count();
		}
	}

	ReplaceDocument(std::move(loaded));
	// 열린 활성 페이지도 프로젝트 페이지 문서다 (다른 페이지 탭과 같게 attached). 제목은 페이지 이름이 된다
	m_attached = true;
	SetPathName(manifestPathLpsz);
	SetModifiedFlag(FALSE);

	// 나머지 페이지는 각각 탭(문서)으로 연다
	POSITION templatePos = AfxGetApp()->GetFirstDocTemplatePosition();
	if (templatePos != nullptr)
	{
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(templatePos);
		for (const auto& pg : g_project.pages)
		{
			if (pg.name == m_activePage) continue;
			CM2DrawDoc* pageDoc = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->CreateNewDocument());
			if (!pageDoc) continue;
			pageDoc->LoadPageAttached(pg.name);
			if (CFrameWnd* frame = tmpl->CreateNewFrame(pageDoc, nullptr))
				tmpl->InitialUpdateFrame(frame, pageDoc);
		}
	}

	// 파일 이름 없이 비어 있던 문서(시작할 때 열린 창 등)는 프로젝트 페이지가 아니므로 닫는다. 수정한 문서는 그대로 둔다
	{
		std::vector<CFrameWnd*> frames;
		POSITION utp = AfxGetApp()->GetFirstDocTemplatePosition();
		while (utp != nullptr) {
			CDocTemplate* t = AfxGetApp()->GetNextDocTemplate(utp);
			POSITION udp = t->GetFirstDocPosition();
			while (udp != nullptr) {
				CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, t->GetNextDoc(udp));
				if (!d || d->IsProjectDocForClose() || d->IsModified() || !d->GetPathName().IsEmpty()) continue;
				POSITION vp = d->GetFirstViewPosition();
				while (vp != nullptr)
					if (CView* v = d->GetNextView(vp))
						if (CFrameWnd* f = v->GetParentFrame()) frames.push_back(f);
			}
		}
		for (CFrameWnd* f : frames) f->SendMessage(WM_CLOSE);
	}

	CString strLog;
	strLog.Format(_T("[프로젝트 열기 완료] %s (페이지: %s)"), manifestPathLpsz, m_activePage.c_str());
	LogAppEvent(strLog);
	SetAppStatusText(strLog);
	return TRUE;
}

// 프로젝트의 페이지 하나를 이 문서에 붙여 연다 (열 때 탭으로 여는 페이지)
void CM2DrawDoc::LoadPageAttached(const std::wstring& name)
{
	namespace fs = std::filesystem;
	const fs::path folder(g_project.folder);
	ge::Document loaded;
	ge::SvgReader reader;
	const fs::path file = folder / L"pages" / (name + L".m2doc");
	if (!fs::exists(file) || !reader.ReadFromFile(file.wstring(), loaded))
		return;
	RelinkDocument(loaded, folder.wstring());

	m_doc = std::move(loaded);
	m_activePage = name;
	m_attached = true;
	m_agentDir.Empty();
	for (const auto& pg : g_project.pages)
		if (pg.name == name && !pg.agentDir.empty())
			m_agentDir = CString(ProjAbs(pg.agentDir, folder.wstring()).c_str());
	ResetToolInteraction();
	m_undoMgr.Reset(m_doc);
	SetPathName(CString(g_project.manifest.c_str()));
	SetModifiedFlag(FALSE);
}
// 저장 전(프로젝트 폴더가 없는) 문서의 페이지 이름: 문서 이름 그대로 (M2Draw1, M2Draw2 ...)
static std::wstring UntitledPageBase(const CM2DrawDoc* d)
{
	CString title = d->GetTitle();
	const int dot = title.ReverseFind(_T('.'));
	if (dot > 0) title = title.Left(dot);
	title.Replace(_T(" *"), _T(""));
	title.Trim();
	return SafeFileBase(std::wstring((LPCTSTR)title));
}

// 어느 프로젝트에도 속하지 않은 이름 없는 문서들 (만든 순서). 저장 전 "DefaultProject" 의 페이지로 본다
static std::vector<CM2DrawDoc*> UntitledDocs()
{
	std::vector<CM2DrawDoc*> out;
	POSITION tp = AfxGetApp()->GetFirstDocTemplatePosition();
	while (tp != nullptr) {
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(tp);
		POSITION dp = tmpl->GetFirstDocPosition();
		while (dp != nullptr)
			if (CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(dp)))
				if (!d->IsProjectDocForClose() && d->GetPathName().IsEmpty()) out.push_back(d);
	}
	return out;
}

bool CM2DrawDoc::HasUntitledDocs()
{
	return g_project.folder.empty() && !UntitledDocs().empty();
}

// 저장 안 된 문서들(M2Draw1, M2Draw2 ...)을 한 프로젝트로 묶어 저장한다: 이름을 한 번 묻고 모든 문서가 그 프로젝트의 페이지가 된다
BOOL CM2DrawDoc::SaveUntitledAsProject()
{
	std::vector<CM2DrawDoc*> untitled = UntitledDocs();
	if (untitled.empty() || !g_project.folder.empty()) return FALSE;

	CM2DrawDoc* first = untitled.front();
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
		if (CM2DrawDoc* active = mf->GetActiveM2DrawDoc())
			if (std::find(untitled.begin(), untitled.end(), active) != untitled.end()) first = active;

	// 문서가 여럿이면 첫 문서의 페이지 이름도 문서 이름으로 한다 (하나뿐이면 기본 "메인")
	const std::wstring prevPage = first->m_activePage;
	if (untitled.size() > 1) first->m_activePage = UntitledPageBase(first);
	if (!first->DoSave(nullptr, TRUE)) {   // 이름 묻기. 취소하면 아무것도 바뀌지 않는다
		first->m_activePage = prevPage;
		return FALSE;
	}
	for (CM2DrawDoc* d : untitled)
		if (d != first) d->AttachToProject();
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) mf->RefreshProjectTree();
	return TRUE;
}

// 저장 안 된 문서에서 처음 작업이 시작되면 한 번 묻는다. 예: 이름을 정해 프로젝트로 저장 (이미 만든 페이지 모두 포함)
void CM2DrawDoc::AskSaveProjectOnFirstEdit()
{
	static bool s_asked = false;
	if (s_asked || m_saveAsked || m_attached || !GetPathName().IsEmpty() || !g_project.folder.empty() || !IsModified()) return;
	s_asked = true;
	for (CM2DrawDoc* d : UntitledDocs()) d->m_saveAsked = true;   // 캡처 붙여넣기에서 같은 질문을 또 하지 않는다
	if (AfxMessageBox(_T("프로젝트를 저장하시겠습니까?\n\n예: 이름을 정해 지금까지 만든 모든 페이지를 프로젝트로 저장합니다.\n아니요: 나중에 저장합니다. (Ctrl+S 또는 파일 > 새 프로젝트)"),
		MB_YESNO | MB_ICONQUESTION) == IDYES)
		SaveUntitledAsProject();
}

// 페이지를 자기 탭(창)으로 연다. 이미 열린 페이지면 그 탭을 활성화해서 같은 페이지가 두 창에 뜨지 않게 한다
BOOL CM2DrawDoc::OpenPageTab(const std::wstring& name)
{
	// 저장 전(DefaultProject): 이름이 같은 열린 문서의 탭을 활성화한다
	if (g_project.folder.empty()) {
		for (CM2DrawDoc* d : UntitledDocs()) {
			if (UntitledPageBase(d) != name) continue;
			POSITION viewPos = d->GetFirstViewPosition();
			if (CView* view = d->GetNextView(viewPos))
				view->GetParentFrame()->ActivateFrame();
			return TRUE;
		}
		return FALSE;
	}
	if (GetPathName().IsEmpty()) return FALSE;
	POSITION templatePos = AfxGetApp()->GetFirstDocTemplatePosition();
	if (templatePos == nullptr) return FALSE;
	CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(templatePos);

	// 1) 이미 열려 있는 페이지면 그 탭을 활성화한다
	POSITION docPos = tmpl->GetFirstDocPosition();
	while (docPos != nullptr) {
		CM2DrawDoc* openDoc = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(docPos));
		if (!openDoc || !openDoc->m_attached || openDoc->m_activePage != name) continue;
		POSITION viewPos = openDoc->GetFirstViewPosition();
		if (CView* view = openDoc->GetNextView(viewPos))
			view->GetParentFrame()->ActivateFrame();
		return TRUE;
	}

	// 2) 현재 페이지를 저장한 뒤 새 탭으로 연다
	if (!SaveProject(GetPathName())) return FALSE;
	CM2DrawDoc* pageDoc = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->CreateNewDocument());
	if (!pageDoc) return FALSE;
	pageDoc->LoadPageAttached(name);
	if (CFrameWnd* frame = tmpl->CreateNewFrame(pageDoc, nullptr))
		tmpl->InitialUpdateFrame(frame, pageDoc);
	return TRUE;
}
// 프로젝트가 열려 있을 때 새로 만들기: 빈 페이지를 만들어 목차에 바로 등록하고 연다 (이름: 페이지, 페이지_2 ...)
CM2DrawDoc* CM2DrawDoc::AddPageToProject(const std::wstring& wantedName)
{
	namespace fs = std::filesystem;
	if (g_project.folder.empty() || g_project.manifest.empty()) return nullptr;
	const fs::path folder(g_project.folder);
	const CString manifest(g_project.manifest.c_str());
	const std::wstring base = wantedName.empty() ? std::wstring(L"페이지") : wantedName;
	std::wstring name = base;
	for (int k = 2; std::any_of(g_project.pages.begin(), g_project.pages.end(),
		[&](const ProjectPageEntry& pg) { return pg.name == name; }) || fs::exists(folder / L"pages" / (name + L".m2doc")); ++k)
		name = base + L"_" + std::to_wstring(k);

	POSITION templatePos = AfxGetApp()->GetFirstDocTemplatePosition();
	if (templatePos == nullptr) return nullptr;
	CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(templatePos);
	CM2DrawDoc* pageDoc = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->OpenDocumentFile(nullptr));
	if (!pageDoc) return nullptr;
	pageDoc->m_activePage = name;
	pageDoc->m_attached = true;
	pageDoc->m_agentDir.Empty();
	pageDoc->SetPathName(manifest, FALSE);
	pageDoc->SaveProject(manifest);   // 목차에 페이지가 없으면 SaveProject 가 넣고, 빈 페이지 파일을 만든다
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) mf->RefreshProjectTree();   // 타이머를 기다리지 않고 바로 트리에 보인다
	return pageDoc;
}
// 프로젝트 목차에서 페이지를 뺀다. 묻지 않고 바로 뺀다. 페이지 파일(pages\이름.m2doc)은 지우지 않는다 (다시 가져오기 가능)
// 그 페이지의 창이 열려 있으면 저장하지 않고 닫는다: 창을 닫을 때의 저장이 목차에 다시 넣기 때문
BOOL CM2DrawDoc::RemoveProjectPage(const std::wstring& name)
{
	if (!IsProjectDocForClose()) return FALSE;
	auto forEachDoc = [](auto&& fn) {
		POSITION tp = AfxGetApp()->GetFirstDocTemplatePosition();
		while (tp != nullptr) {
			CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(tp);
			POSITION dp = tmpl->GetFirstDocPosition();
			while (dp != nullptr)
				if (CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(dp))) fn(d);
		}
	};

	// 1) 빼는 페이지 말고 열린 페이지가 없으면, 다른 페이지를 먼저 연다 (목차를 쓸 문서가 남도록)
	bool otherOpen = false;
	forEachDoc([&](CM2DrawDoc* d) { if (d->m_attached && d->m_activePage != name) otherOpen = true; });
	if (!otherOpen) {
		std::wstring other;
		for (const auto& pg : g_project.pages)
			if (pg.name != name) { other = pg.name; break; }
		if (other.empty()) {
			AfxMessageBox(_T("마지막 페이지는 뺄 수 없습니다."), MB_ICONINFORMATION);
			return FALSE;
		}
		if (!OpenPageTab(other)) return FALSE;
	}

	// 2) 목차에서 뺀다
	g_project.pages.erase(std::remove_if(g_project.pages.begin(), g_project.pages.end(),
		[&](const ProjectPageEntry& pg) { return pg.name == name; }), g_project.pages.end());

	// 3) 그 페이지의 창을 저장 없이 닫는다 (수정 표시를 지워 저장 질문이 나오지 않게 한다)
	std::vector<CFrameWnd*> frames;
	forEachDoc([&](CM2DrawDoc* d) {
		if (!d->m_attached || d->m_activePage != name) return;
		d->SetModifiedFlag(FALSE);
		POSITION vp = d->GetFirstViewPosition();
		while (vp != nullptr)
			if (CView* v = d->GetNextView(vp))
				if (CFrameWnd* f = v->GetParentFrame()) frames.push_back(f);
	});
	for (CFrameWnd* f : frames) f->SendMessage(WM_CLOSE);

	// 4) 목차는 남아 있는 페이지 문서로 저장한다 (닫힌 문서는 쓰지 않는다)
	CM2DrawDoc* keeper = nullptr;
	forEachDoc([&](CM2DrawDoc* d) {
		if (!keeper && d->m_attached && d->m_activePage != name) keeper = d;
	});
	if (keeper) keeper->SaveProject(keeper->GetPathName());
	SetAppStatusText(CString(_T("프로젝트에서 뺐습니다: ")) + CString(name.c_str()));
	return TRUE;
}
// 페이지 전환: 현재 페이지를 저장하고 다른 페이지를 연다. 파일이 없으면 현재 대지 크기의 빈 페이지를 만든다
BOOL CM2DrawDoc::SwitchPage(const std::wstring& name)
{
	namespace fs = std::filesystem;
	if (g_project.folder.empty() || GetPathName().IsEmpty()) return FALSE;
	if (name == m_activePage) return TRUE;

	// 1) 현재 페이지 저장 (목차 포함)
	if (!SaveProject(GetPathName())) return FALSE;

	// 2) 대상 페이지 읽기 (없으면 빈 페이지)
	const fs::path folder(g_project.folder);
	const fs::path file = folder / L"pages" / (name + L".m2doc");
	ge::Document loaded;
	if (fs::exists(file)) {
		ge::SvgReader reader;
		if (!reader.ReadFromFile(file.wstring(), loaded)) {
			AfxMessageBox(_T("페이지를 열 수 없습니다."), MB_ICONERROR);
			return FALSE;
		}
		RelinkDocument(loaded, folder.wstring());
	} else {
		loaded.width = m_doc.width;
		loaded.height = m_doc.height;
		loaded.backgroundColor = m_doc.backgroundColor;
	}

	// 3) 목차에 올리고, 그 페이지의 에이전트 위치를 쓴다
	bool known = false;
	for (const auto& pg : g_project.pages)
		if (pg.name == name) known = true;
	if (!known) g_project.pages.push_back(ProjectPageEntry{ name, L"" });
	m_activePage = name;
	m_agentDir.Empty();
	for (const auto& pg : g_project.pages)
		if (pg.name == name && !pg.agentDir.empty())
			m_agentDir = CString(ProjAbs(pg.agentDir, folder.wstring()).c_str());

	ReplaceDocument(std::move(loaded));
	SaveProject(GetPathName());
	SetModifiedFlag(FALSE);

	CString log;
	log.Format(_T("[페이지] %s"), name.c_str());
	LogAppEvent(log);
	SetAppStatusText(log);
	return TRUE;
}

// 파일 > 새 페이지: pages 폴더에 이름을 정해 빈 페이지를 만들고 연다
void CM2DrawDoc::OnFilePageNew()
{
	if (g_project.folder.empty() || GetPathName().IsEmpty()) {
		AfxMessageBox(_T("먼저 프로젝트를 저장하세요."), MB_ICONINFORMATION);
		return;
	}
	const CString initial = std::filesystem::path(std::filesystem::path(g_project.folder) / L"pages").wstring().c_str();
	CFileDialog dlg(FALSE, nullptr, _T("새 페이지"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
		_T("페이지 (*.m2doc)|*.m2doc||"), AfxGetMainWnd());
	dlg.m_ofn.lpstrInitialDir = initial;
	dlg.m_ofn.lpstrTitle = _T("새 페이지 이름");
	if (dlg.DoModal() != IDOK) return;

	CString name = dlg.GetFileTitle();
	const int dot = name.ReverseFind(_T('.'));
	if (dot > 0) name = name.Left(dot);
	name.Trim();
	if (name.IsEmpty()) return;
	// 페이지마다 자기 탭: 이미 있는 페이지면 그 탭을 열고, 없으면 빈 페이지를 등록해 새 탭으로 연다
	const std::wstring pageName((LPCTSTR)name);
	const bool exists = std::any_of(g_project.pages.begin(), g_project.pages.end(),
		[&](const ProjectPageEntry& pg) { return pg.name == pageName; });
	if (exists) OpenPageTab(pageName);
	else AddPageToProject(pageName);
}

// 파일 > 페이지 이름 바꾸기: 파일 이름과 목차를 함께 바꾼다
void CM2DrawDoc::OnFilePageRename()
{
	if (g_project.folder.empty() || GetPathName().IsEmpty()) {
		AfxMessageBox(_T("먼저 프로젝트를 저장하세요."), MB_ICONINFORMATION);
		return;
	}
	const CString initial = std::filesystem::path(std::filesystem::path(g_project.folder) / L"pages").wstring().c_str();
	CFileDialog dlg(FALSE, nullptr, CString(m_activePage.c_str()), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
		_T("페이지 (*.m2doc)|*.m2doc||"), AfxGetMainWnd());
	dlg.m_ofn.lpstrInitialDir = initial;
	dlg.m_ofn.lpstrTitle = _T("페이지 새 이름");
	if (dlg.DoModal() != IDOK) return;

	CString name = dlg.GetFileTitle();
	const int dot = name.ReverseFind(_T('.'));
	if (dot > 0) name = name.Left(dot);
	name.Trim();
	if (!name.IsEmpty()) RenamePage(std::wstring((LPCTSTR)name));
}

void CM2DrawDoc::RenamePage(const std::wstring& newName)
{
	namespace fs = std::filesystem;
	const std::wstring oldName = m_activePage;
	if (newName.empty() || newName == oldName) return;
	for (const auto& pg : g_project.pages)
		if (pg.name == newName) {
			AfxMessageBox(_T("같은 이름의 페이지가 이미 있습니다."), MB_ICONERROR);
			return;
		}

	SaveProject(GetPathName());   // 지금 상태를 옛 이름으로 먼저 저장
	const fs::path dir = fs::path(g_project.folder) / L"pages";
	std::error_code ec;
	if (fs::exists(dir / (oldName + L".m2doc")))
		fs::rename(dir / (oldName + L".m2doc"), dir / (newName + L".m2doc"), ec);
	if (ec) {
		AfxMessageBox(_T("페이지 파일 이름을 바꾸지 못했습니다."), MB_ICONERROR);
		return;
	}
	for (auto& pg : g_project.pages)
		if (pg.name == oldName) pg.name = newName;
	m_activePage = newName;
	if (m_attached) SetTitle(CString(newName.c_str()));
	SaveProject(GetPathName());   // 목차와 새 파일 이름 반영
	SetModifiedFlag(FALSE);

	CString log;
	log.Format(_T("[페이지] 이름 변경: %s -> %s"), oldName.c_str(), newName.c_str());
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 프로젝트 트리에서 페이지 이름 바꾸기: 열려 있는 페이지는 그 문서가, 닫힌 페이지는 파일과 목차만 바꾼다
BOOL CM2DrawDoc::RenameProjectPage(const std::wstring& oldName, const std::wstring& newName)
{
	namespace fs = std::filesystem;
	if (!IsProjectDocForClose() || newName.empty() || newName == oldName) return FALSE;
	if (newName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
		AfxMessageBox(_T("페이지 이름에 쓸 수 없는 글자가 있습니다."), MB_ICONERROR);
		return FALSE;
	}
	auto hasPage = [](const std::wstring& n) {
		return std::any_of(g_project.pages.begin(), g_project.pages.end(), [&](const ProjectPageEntry& pg) { return pg.name == n; });
	};
	if (hasPage(newName)) {
		AfxMessageBox(_T("같은 이름의 페이지가 이미 있습니다."), MB_ICONERROR);
		return FALSE;
	}

	// 열려 있는 페이지면 그 문서가 바꾼다 (제목·저장 위치 갱신 포함)
	POSITION templatePos = AfxGetApp()->GetFirstDocTemplatePosition();
	while (templatePos != nullptr) {
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(templatePos);
		POSITION docPos = tmpl->GetFirstDocPosition();
		while (docPos != nullptr) {
			CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(docPos));
			if (d && d->m_attached && d->m_activePage == oldName) {
				d->RenamePage(newName);
				return hasPage(newName);
			}
		}
	}

	// 닫힌 페이지: 파일 이름과 목차만 바꾼다
	const fs::path dir = fs::path(g_project.folder) / L"pages";
	std::error_code ec;
	if (fs::exists(dir / (oldName + L".m2doc")))
		fs::rename(dir / (oldName + L".m2doc"), dir / (newName + L".m2doc"), ec);
	if (ec) {
		AfxMessageBox(_T("페이지 파일 이름을 바꾸지 못했습니다."), MB_ICONERROR);
		return FALSE;
	}
	for (auto& pg : g_project.pages)
		if (pg.name == oldName) pg.name = newName;
	SaveProject(GetPathName());
	SetAppStatusText(CString(_T("페이지 이름 변경: ")) + CString(oldName.c_str()) + _T(" -> ") + CString(newName.c_str()));
	return TRUE;
}

// 파일 > 페이지 열기: pages 폴더에서 고른 페이지로 바꾼다
void CM2DrawDoc::OnFilePageOpen()
{
	if (g_project.folder.empty() || GetPathName().IsEmpty()) {
		AfxMessageBox(_T("먼저 프로젝트를 저장하세요."), MB_ICONINFORMATION);
		return;
	}
	const CString initial = std::filesystem::path(std::filesystem::path(g_project.folder) / L"pages").wstring().c_str();
	CFileDialog dlg(TRUE, _T("m2doc"), nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
		_T("페이지 (*.m2doc)|*.m2doc||"), AfxGetMainWnd());
	dlg.m_ofn.lpstrInitialDir = initial;
	dlg.m_ofn.lpstrTitle = _T("열 페이지 고르기");
	if (dlg.DoModal() != IDOK) return;

	CString name = dlg.GetFileTitle();
	const int dot = name.ReverseFind(_T('.'));
	if (dot > 0) name = name.Left(dot);
	const std::wstring pageName((LPCTSTR)name);
	// 목차에 없는 파일(다른 프로젝트의 페이지)은 열지 않고 가져오기로 안내한다
	const bool inProject = std::any_of(g_project.pages.begin(), g_project.pages.end(),
		[&](const ProjectPageEntry& pg) { return pg.name == pageName; });
	if (!inProject) {
		AfxMessageBox(_T("이 프로젝트의 페이지가 아닙니다. 다른 프로젝트의 페이지는 '페이지 가져오기'로 들여 주세요."), MB_ICONINFORMATION);
		return;
	}
	OpenPageTab(pageName);
}
// 에이전트 패키지를 둘 폴더: 프로젝트가 있으면 그 안, 없으면 기본 작업 폴더
std::wstring CM2DrawDoc::AgentRootForDoc() const
{
	if (!g_project.folder.empty())
		return g_project.folder + L"\\agent\\" + m_activePage;
	return ge::AgentWorkspaceRoot();
}

// 프로젝트 안에 만든 패키지를 기본 작업 폴더에서도 보이게 바로가기(정션)를 둔다.
// MCP 서버는 기본 작업 폴더의 패키지만 읽고 쓰므로, 정션을 통해 결과(result\v1.svg)가 프로젝트 폴더에 떨어진다
void LinkPackageToWorkspace(const std::filesystem::path& pkgDir)
{
	namespace fs = std::filesystem;
	const fs::path ws(ge::AgentWorkspaceRoot());
	if (pkgDir.parent_path() == ws) return;
	std::error_code ec;
	fs::create_directories(ws, ec);
	const fs::path link = ws / pkgDir.filename();
	if (fs::exists(link, ec)) return;

	std::wstring cmd = L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + pkgDir.wstring() + L"\"";
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(L'\0');
	STARTUPINFOW si{ sizeof(si) };
	PROCESS_INFORMATION pi{};
	if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		WaitForSingleObject(pi.hProcess, 5000);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

CString CM2DrawDoc::GetAgentDirForChat() const { return m_agentDir; }
CString CM2DrawDoc::GetSiteForChat() const { return CString(m_agentSiteUrl.c_str()); }

BOOL CM2DrawDoc::OnOpenDocument(LPCTSTR lpszPathName)
{
	if (!lpszPathName || lpszPathName[0] == _T('\0'))
		return FALSE;

	// *.m2svg 는 프로젝트 목차이거나 캡처 파일이다. 내용으로 구분한다
	const CString path(lpszPathName);
	if (path.Right(6).CompareNoCase(_T(".m2svg")) == 0) {
		std::string head;
		if (ProjReadAll(std::filesystem::path((LPCTSTR)path), head)) {
			if (head.find("<m2:project") != std::string::npos)
			{
				if (!AllowOpenProject(lpszPathName))
					return FALSE;
				return OpenProject(lpszPathName);
			}
			if (head.find("m2:kind=\"capture\"") != std::string::npos || head.find("<m2:capture") != std::string::npos) {
				AfxMessageBox(_T("캡처 파일(.m2svg)입니다. 파일 > 캡처 불러오기로 여세요."), MB_ICONINFORMATION);
				return FALSE;
			}
		}
	}

	// 예전 SVG 문서 (이전 버전에서 저장한 파일을 계속 열 수 있게 둔다)
	// 임시 문서로 먼저 읽어 실패 시 현재 문서를 그대로 유지 (깨진 파일이 문서를 망가뜨리지 않도록)
	ge::Document loaded;
	ge::SvgReader reader;
	if (!reader.ReadFromFile(lpszPathName, loaded))
	{
		AfxMessageBox(_T("SVG 파일을 열거나 파싱하는 중 오류가 발생했습니다."), MB_ICONERROR);
		return FALSE;
	}

	m_currentPath.reset();
	m_doc = std::move(loaded);
	ResetToolInteraction();
	// 프로젝트 문서가 다른 탭에 아직 열려 있으면 프로젝트 상태를 지우지 않는다 (새 문서의 레퍼런스가 그 프로젝트의 페이지가 되도록)
	bool projectStillOpen = false;
	POSITION tp = AfxGetApp()->GetFirstDocTemplatePosition();
	while (tp && !projectStillOpen) {
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(tp);
		POSITION dp = tmpl->GetFirstDocPosition();
		while (dp) {
			CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(dp));
			if (d && d != this && d->m_attached) {
				projectStillOpen = true;
				break;
			}
		}
	}
	if (!projectStillOpen) g_project = ProjectState();
	m_attached = false;

	SetPathName(lpszPathName);
	SetModifiedFlag(FALSE);
	m_undoMgr.Reset(m_doc);

	UpdateAllViews(nullptr);
	UpdateShapePropertiesSelection(nullptr, this);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}

	CString strLog;
	strLog.Format(_T("[파일 열기 완료] %s"), lpszPathName);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	return TRUE;
}

// 열린 프로젝트의 페이지로 붙인다: 이름은 문서 이름(기본값), 저장은 프로젝트 목차로 간다 (묻지 않는다)
void CM2DrawDoc::AttachToProject()
{
	CString title = GetTitle();
	const int dot = title.ReverseFind(_T('.'));
	if (dot > 0) title = title.Left(dot);
	title.Replace(_T(" *"), _T(""));
	title.Trim();
	const std::wstring base = SafeFileBase(std::wstring((LPCTSTR)title));
	std::wstring cand = base;
	for (int k = 2; std::any_of(g_project.pages.begin(), g_project.pages.end(), [&](const ProjectPageEntry& pg) { return pg.name == cand; }); ++k)
		cand = base + L"_" + std::to_wstring(k);

	m_activePage = cand;
	g_project.pages.push_back(ProjectPageEntry{ cand, L"" });
	m_attached = true;
	SetPathName(CString(g_project.manifest.c_str()));
	SetTitle(CString(cand.c_str()));
	SaveProject(CString(g_project.manifest.c_str()));
	SetModifiedFlag(FALSE);
}
BOOL CM2DrawDoc::DoSave(LPCTSTR lpszPathName, BOOL bReplace)
{
	if (m_attached && !g_project.manifest.empty())
	{
		CWaitCursor attachedWait;
		return OnSaveDocument(CString(g_project.manifest.c_str()));
	}
	CString newName = lpszPathName;
	if (newName.IsEmpty())
	{
		// 기본 폴더: 열린 프로젝트가 있으면 그 프로젝트 폴더의 상위 폴더 (프로젝트 안의 references 를 기본으로 열지 않는다)
		CString initialDir;
		if (!g_project.folder.empty())
			initialDir = CString(std::filesystem::path(g_project.folder).parent_path().wstring().c_str());
		// 프로젝트가 아직 없으면 이름 기본값은 DefaultProject (바꿔서 저장)
		CFileDialog dlg(FALSE, nullptr, g_project.folder.empty() ? _T("DefaultProject") : nullptr, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, _T("M2Draw 프로젝트 (*.m2svg)|*.m2svg||"));
		dlg.m_ofn.lpstrTitle = g_project.folder.empty() ? _T("프로젝트 저장 (프로젝트 폴더 이름)") : _T("다른 이름으로 저장 (프로젝트 폴더 이름)");
		if (!initialDir.IsEmpty()) dlg.m_ofn.lpstrInitialDir = initialDir;
		if (dlg.DoModal() != IDOK)
			return FALSE;
		newName = dlg.GetPathName();
	}

	// 프로젝트 이름 = 폴더 이름. 폴더 안에 이름.m2svg 를 둔다 (확장자는 항상 .m2svg)
	const int slash = (std::max)(newName.ReverseFind(_T('\\')), newName.ReverseFind(_T('/')));
	CString parent = slash >= 0 ? newName.Left(slash) : CString();
	CString name = slash >= 0 ? newName.Mid(slash + 1) : newName;
	const int dot = name.ReverseFind(_T('.'));
	if (dot > 0) name = name.Left(dot);
	name.Trim();

	// 고른 폴더 이름과 프로젝트 이름이 같으면 한 겹 더 만들지 않는다 (…\TestProject1 + TestProject1 → …\TestProject1\TestProject1.m2svg)
	CString folderPath = parent + _T("\\") + name;
	const int parentSlash = parent.ReverseFind(_T('\\'));
	const CString parentLeaf = parentSlash >= 0 ? parent.Mid(parentSlash + 1) : parent;
	if (parentLeaf.CompareNoCase(name) == 0)
		folderPath = parent;

	// 열린 프로젝트 폴더 안에 또 프로젝트를 만들려는 경우에는 한 번 확인한다
	if (!g_project.folder.empty()) {
		const CString openFolder(g_project.folder.c_str());
		const int len = openFolder.GetLength();
		if (folderPath.GetLength() > len && folderPath.Left(len).CompareNoCase(openFolder) == 0 && folderPath[len] == _T('\\') &&
			AfxMessageBox(_T("열린 프로젝트 폴더 안에 새 프로젝트를 만들까요?"), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return FALSE;
	}
	const CString manifestPath = folderPath + _T("\\") + name + _T(".m2svg");

	// 다른 폴더로 저장하면 레퍼런스와 페이지를 새 폴더로 함께 복사한다
	if (!g_project.folder.empty()) {
		const std::filesystem::path oldFolder(g_project.folder);
		const std::filesystem::path newFolder(std::wstring((LPCTSTR)folderPath));
		std::error_code ec;
		if (!std::filesystem::equivalent(oldFolder, newFolder, ec)) {
			std::filesystem::create_directories(newFolder / L"pages", ec);
			std::filesystem::create_directories(newFolder / L"references", ec);
			std::filesystem::copy(oldFolder / L"references", newFolder / L"references", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
			std::filesystem::copy(oldFolder / L"pages", newFolder / L"pages", std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
		}
	}

	CWaitCursor wait;
	if (!OnSaveDocument(manifestPath))
		return FALSE;

	if (bReplace)
	{
		m_attached = true;   // 프로젝트를 만든 문서도 그 프로젝트의 첫 페이지 문서다 (제목은 페이지 이름, 프로젝트 이름이 아니다)
		SetPathName(manifestPath);
	}
	return TRUE;
}
// 저장 위치가 있는 문서가 수정된 뒤 30초가 지나면 자동으로 저장한다 (닫을 때 잊어도 되도록)
void CM2DrawDoc::AutoSaveIfDirty()
{
	if (!IsModified() || GetPathName().IsEmpty())
	{
		m_dirtyTick = 0;
		return;
	}
	const DWORD now = ::GetTickCount();
	if (m_dirtyTick == 0)
	{
		m_dirtyTick = now;   // 수정이 처음 보인 시각
		return;
	}
	if (now - m_dirtyTick < 30000)
		return;

	m_dirtyTick = 0;
	if (DoSave(GetPathName(), FALSE))
	{
		CString log;
		log.Format(_T("[자동 저장] %s"), (LPCTSTR)GetPathName());
		LogAppEvent(log);
	}
}

void CM2DrawDoc::UpdateTitleWithModified()
{
	CString strTitle = GetTitle();
	bool bHasStar = (strTitle.Right(2) == _T(" *") || strTitle.Right(1) == _T("*"));

	if (IsModified() && !bHasStar)
	{
		strTitle += _T(" *");
		SetTitle(strTitle);
	}
	else if (!IsModified() && bHasStar)
	{
		if (strTitle.Right(2) == _T(" *"))
			strTitle = strTitle.Left(strTitle.GetLength() - 2);
		else if (strTitle.Right(1) == _T("*"))
			strTitle = strTitle.Left(strTitle.GetLength() - 1);
		SetTitle(strTitle);
	}
}

void CM2DrawDoc::SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU)
{
	CDocument::SetPathName(lpszPathName, bAddToMRU);
	// 제목: 페이지 문서는 페이지 이름, 프로젝트 목차는 프로젝트 이름 (폴더 이름)
	if (m_attached)
	{
		SetTitle(CString(m_activePage.c_str()));
	}
	else if (lpszPathName && lpszPathName[0] != _T('\0'))
	{
		const CString path(lpszPathName);
		const int slash = (std::max)(path.ReverseFind(_T('\\')), path.ReverseFind(_T('/')));
		CString name = path.Mid(slash + 1);
		const int dot = name.ReverseFind(_T('.'));
		if (dot > 0) name = name.Left(dot);
		SetTitle(name);
	}
	UpdateTitleWithModified();
}

void CM2DrawDoc::SetModifiedFlag(BOOL bModified)
{
	CDocument::SetModifiedFlag(bModified);
	UpdateTitleWithModified();

	// 문서와 연결된 모든 뷰의 프레임 창 타이틀을 갱신하도록 통지
	POSITION pos = GetFirstViewPosition();
	while (pos != nullptr)
	{
		CView* pView = GetNextView(pos);
		if (pView)
		{
			CFrameWnd* pFrame = pView->GetParentFrame();
			if (pFrame)
			{
				pFrame->OnUpdateFrameTitle(TRUE);
			}
		}
	}
}

bool CM2DrawDoc::CommitUndoPoint(const std::wstring& actionName)
{
	m_doc.EnsureUniqueIds();   // 컴포넌트 Rebuild 등으로 새로 생긴 노드에 ID 발급 (스냅샷/선택 복원이 ID 기준)
	if (!m_undoMgr.CommitChange(actionName, m_doc))
		return false;
	SetModifiedFlag(TRUE);
	return true;
}

void CM2DrawDoc::ResetToolInteraction()
{
	// 문서가 통째로 교체되면(Undo/Redo/열기/새 문서) 도구가 들고 있던 노드 포인터가 무효가 되므로 진행 중 상태를 폐기
	CM2DrawApp* pApp = dynamic_cast<CM2DrawApp*>(AfxGetApp());
	SelectTool* pSelect = pApp ? pApp->GetToolManager().GetSelectTool() : nullptr;
	if (pSelect) pSelect->ResetInteraction();
	if (GradientTool* pGradient = pApp ? pApp->GetToolManager().GetGradientTool() : nullptr) pGradient->ResetInteraction();
	if (NodeTool* pNode = pApp ? pApp->GetToolManager().GetNodeTool() : nullptr) pNode->ResetInteraction();
}

void CM2DrawDoc::OnEditUndo()
{
	// 기록되지 않은 편집이 남아 있으면 먼저 확정해서 "방금 한 작업" 이 되돌려지도록 함
	CommitUndoPoint(L"편집");
	if (!m_undoMgr.CanUndo())
		return;

	std::wstring action = m_undoMgr.GetLastUndoActionName();
	if (m_undoMgr.Undo(m_doc))
	{
		ResetToolInteraction();
		SetModifiedFlag(TRUE);
		UpdateAllViews(nullptr);
		UpdateShapePropertiesSelection(GetPrimarySelection(), this);

		CWnd* pMainWnd = AfxGetMainWnd();
		if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		{
			static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		}

		CString strLog;
		strLog.Format(_T("[실행 취소] %s 취소됨"), action.c_str());
		LogAppEvent(strLog);
		SetAppStatusText(strLog);
	}
}

void CM2DrawDoc::OnUpdateEditUndo(CCmdUI* pCmdUI)
{
	if (pCmdUI)
		pCmdUI->Enable(m_undoMgr.CanUndo() ? TRUE : FALSE);
}

void CM2DrawDoc::OnEditRedo()
{
	if (!m_undoMgr.CanRedo())
		return;

	std::wstring action = m_undoMgr.GetLastRedoActionName();
	if (m_undoMgr.Redo(m_doc))
	{
		ResetToolInteraction();
		SetModifiedFlag(TRUE);
		UpdateAllViews(nullptr);
		UpdateShapePropertiesSelection(GetPrimarySelection(), this);

		CWnd* pMainWnd = AfxGetMainWnd();
		if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		{
			static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		}

		CString strLog;
		strLog.Format(_T("[다시 실행] %s 다시 실행됨"), action.c_str());
		LogAppEvent(strLog);
		SetAppStatusText(strLog);
	}
}

void CM2DrawDoc::OnUpdateEditRedo(CCmdUI* pCmdUI)
{
	if (pCmdUI)
		pCmdUI->Enable(m_undoMgr.CanRedo() ? TRUE : FALSE);
}

#ifdef SHARED_HANDLERS

// 축소판 그림을 지원합니다.
void CM2DrawDoc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
	// 문서의 데이터를 그리려면 이 코드를 수정하십시오.
	dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

	CString strText = _T("TODO: implement thumbnail drawing here");
	LOGFONT lf;

	CFont* pDefaultGUIFont = CFont::FromHandle((HFONT) GetStockObject(DEFAULT_GUI_FONT));
	pDefaultGUIFont->GetLogFont(&lf);
	lf.lfHeight = 36;

	CFont fontDraw;
	fontDraw.CreateFontIndirect(&lf);

	CFont* pOldFont = dc.SelectObject(&fontDraw);
	dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
	dc.SelectObject(pOldFont);
}

// 검색 처리기를 지원합니다.
void CM2DrawDoc::InitializeSearchContent()
{
	CString strSearchContent;
	// 문서의 데이터에서 검색 콘텐츠를 설정합니다.
	// 콘텐츠 부분은 ";"로 구분되어야 합니다.

	// 예: strSearchContent = _T("point;rectangle;circle;ole object;");
	SetSearchContent(strSearchContent);
}

void CM2DrawDoc::SetSearchContent(const CString& value)
{
	if (value.IsEmpty())
	{
		RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
	}
	else
	{
		CMFCFilterChunkValueImpl *pChunk = nullptr;
		ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
		if (pChunk != nullptr)
		{
			pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
			SetChunkValue(pChunk);
		}
	}
}

#endif // SHARED_HANDLERS

// CM2DrawDoc 진단

#ifdef _DEBUG
void CM2DrawDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CM2DrawDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CM2DrawDoc 명령

bool CM2DrawDoc::HasSelectedObjects() const
{
	return !m_doc.GetSelection().empty();
}

bool CM2DrawDoc::DeleteSelectedObjects()
{
	// 1. 선택된 노드들 수집 (복사본: RemoveNode 가 선택 목록을 수정함)
	const std::vector<ge::Node*> targetNodes = m_doc.GetSelection();

	if (targetNodes.empty())
		return false;


	// 2. 문서에서 노드 제거 (소유권 반환 -> 즉시 해제)
	for (auto* n : targetNodes)
	{
		if (n)
		{
			m_doc.RemoveNode(n);
		}
	}

	// 3. 선택 상태 초기화
	m_doc.ClearSelection();
	UpdateShapePropertiesSelection(nullptr, this);

	// 6. UI 및 뷰 동기화
	CString strLog;
	strLog.Format(_T("[도형 삭제] 선택된 %d개 오브젝트 삭제 완료"), static_cast<int>(targetNodes.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"도형 삭제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetLayerWnd().RefreshLayers();
	}

	return true;
}

bool CM2DrawDoc::DeleteActiveLayer()
{
	auto& layers = m_doc.GetLayers();
	if (layers.size() <= 1)
	{
		CString strMsg = _T("[레이어 삭제 제한] 최소 1개의 레이어는 항상 유지되어야 합니다.");
		LogAppEvent(strMsg);
		SetAppStatusText(strMsg);
		return false;
	}

	auto* pActive = m_doc.GetActiveLayer();
	if (!pActive) return false;


	CString layerName = pActive->GetName().c_str();

	const int childCount = static_cast<int>(pActive->GetChildren().size());

	// m_doc에서 레이어 제거 (하위 도형 포함)
	if (!m_doc.RemoveLayer(pActive))
		return false;

	// 선택 정리
	m_doc.ClearSelection();
	UpdateShapePropertiesSelection(nullptr, this);

	CString strLog;
	strLog.Format(_T("[레이어 패널] 레이어 삭제됨: '%s' (하위 도형 %d개 포함)"), (LPCTSTR)layerName, childCount);
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"레이어 삭제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		CMainFrame* pMainFrame = static_cast<CMainFrame*>(pMainWnd);
		pMainFrame->GetLayerWnd().RefreshLayers();
	}

	return true;
}

bool CM2DrawDoc::PerformSmartDelete()
{
	// 1. 오브젝트가 선택되어 있으면 오브젝트 삭제 우선!
	if (HasSelectedObjects())
	{
		return DeleteSelectedObjects();
	}

	// 2. 오브젝트 선택이 없다면 선택된(활성) 레이어 삭제 시도
	return DeleteActiveLayer();
}

bool CM2DrawDoc::DuplicateSelectedObjects()
{

	// 원본과 같은 부모(레이어) 안에서 바로 위에 복제, 복제본이 새 선택이 됨
	std::vector<ge::Node*> clones = m_doc.DuplicateSelection();
	if (clones.empty())
		return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);

	CString strLog;
	strLog.Format(_T("[도형 복제] %d개 오브젝트 복제 완료"), static_cast<int>(clones.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"도형 복제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
	{
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}
	return true;
}

// ---------------------------------------------------------------------------
// 그룹화 / 그룹 해제
// ---------------------------------------------------------------------------
bool CM2DrawDoc::GroupSelectedObjects()
{
	// 선택된 오브젝트 2개 이상을 하나의 그룹으로 (부모가 달라도 월드 위치 유지, 맨 위 오브젝트가 있던 자리에 생성)
	ge::GroupNode* group = m_doc.GroupSelection();
	if (!group)
		return false;

	UpdateShapePropertiesSelection(group, this);

	CString strLog;
	strLog.Format(_T("[그룹화] '%s' 생성 (오브젝트 %d개)"), group->GetName().c_str(), static_cast<int>(group->GetChildren().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"그룹");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

bool CM2DrawDoc::UngroupSelectedObjects()
{
	if (!m_doc.CanUngroupSelection())
		return false;

	m_doc.UngroupSelection();   // 자식들이 그룹 자리로 올라오고(월드 위치/불투명도 유지) 자식들이 새 선택이 됨
	UpdateShapePropertiesSelection(GetPrimarySelection(), this);

	CString strLog;
	strLog.Format(_T("[그룹 해제] 오브젝트 %d개 선택됨"), static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"그룹 해제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectGroup()               { GroupSelectedObjects(); }
void CM2DrawDoc::OnUpdateObjectGroup(CCmdUI* pCmdUI)   { pCmdUI->Enable(CanGroup()); }
void CM2DrawDoc::OnObjectUngroup()             { UngroupSelectedObjects(); }
void CM2DrawDoc::OnUpdateObjectUngroup(CCmdUI* pCmdUI) { pCmdUI->Enable(CanUngroup()); }

bool CM2DrawDoc::WrapSelectedInFrame()
{
	ge::FrameNode* frame = m_doc.WrapSelectionInFrame();
	if (!frame)
		return false;

	UpdateShapePropertiesSelection(frame, this);

	CString strLog;
	strLog.Format(_T("[프레임으로 감싸기] '%s' 생성 (자식 %d개)"), frame->GetName().c_str(), static_cast<int>(frame->GetChildren().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"프레임으로 감싸기");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectWrapFrame()               { WrapSelectedInFrame(); }
void CM2DrawDoc::OnUpdateObjectWrapFrame(CCmdUI* pCmdUI) { pCmdUI->Enable(m_doc.CanWrapInFrame()); }

bool CM2DrawDoc::MakeCompoundPath()
{
	ge::PathNode* compound = m_doc.MakeCompoundPath();
	if (!compound) return false;

	UpdateShapePropertiesSelection(compound, this);

	CString strLog;
	strLog.Format(_T("[복합 패스] '%s' 생성 (서브패스 %d개)"), compound->GetName().c_str(), static_cast<int>(compound->GetPaths().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"복합 패스 만들기");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

bool CM2DrawDoc::ReleaseCompoundPath()
{
	std::vector<ge::PathNode*> released = m_doc.ReleaseCompoundPath();
	if (released.empty()) return false;

	UpdateShapePropertiesSelection(released.empty() ? nullptr : released.front(), this);

	CString strLog;
	strLog.Format(_T("[복합 패스 풀기] 도형 %d개로 분리"), static_cast<int>(released.size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(L"복합 패스 풀기");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectMakeCompound() { MakeCompoundPath(); }
void CM2DrawDoc::OnUpdateObjectMakeCompound(CCmdUI* pCmdUI) { pCmdUI->Enable(m_doc.CanMakeCompoundPath()); }
void CM2DrawDoc::OnObjectReleaseCompound() { ReleaseCompoundPath(); }
void CM2DrawDoc::OnUpdateObjectReleaseCompound(CCmdUI* pCmdUI) { pCmdUI->Enable(m_doc.CanReleaseCompoundPath()); }

// ---------------------------------------------------------------------------
// 변형: 반전 / 90도 회전 / 방향키 이동 (Step 6.11.3)
// ---------------------------------------------------------------------------
static std::vector<ge::Node*> TransformableSelection(const ge::Document& d)
{
	std::vector<ge::Node*> out;
	const auto& sel = d.GetSelection();
	for (ge::Node* n : sel) {
		if (!n || n->IsEffectivelyLocked()) continue;
		bool ancestorSelected = false;
		for (ge::GroupNode* p = n->GetParent(); p; p = p->GetParent())
			if (std::find(sel.begin(), sel.end(), static_cast<ge::Node*>(p)) != sel.end()) { ancestorSelected = true; break; }
		if (!ancestorSelected) out.push_back(n);
	}
	return out;
}

static bool ApplyWorldToSelection(const std::vector<ge::Node*>& nodes, const ge::Matrix2D& T)
{
	if (nodes.empty()) return false;
	std::vector<ge::Matrix2D> init;
	for (ge::Node* n : nodes) init.push_back(n->GetTransform());
	ge::ApplyWorldTransform(nodes, init, T);
	return true;
}

bool CM2DrawDoc::ApplySelectionTransform(int op)
{
	const auto nodes = TransformableSelection(m_doc);
	ge::Rect2 u;
	if (nodes.empty() || !ge::UnionWorldBounds(nodes, u)) return false;
	const ge::Vec2 c{ (u.left + u.right) * 0.5f, (u.top + u.bottom) * 0.5f };
	ge::Matrix2D T;
	switch (op) {
	case 0: T = ge::MakeWorldScale(-1.0f, 1.0f, c); break;
	case 1: T = ge::MakeWorldScale(1.0f, -1.0f, c); break;
	case 2: T = ge::MakeWorldRotation(90.0f, c); break;
	case 3: T = ge::MakeWorldRotation(-90.0f, c); break;
	default: return false;
	}
	return ApplyWorldToSelection(nodes, T);
}

bool CM2DrawDoc::NudgeSelection(float dx, float dy)
{
	return ApplyWorldToSelection(TransformableSelection(m_doc), ge::Matrix2D::Translation(dx, dy));
}

void CM2DrawDoc::OnObjectTransform(UINT nID)
{
	const int op = static_cast<int>(nID - ID_OBJECT_FLIP_H);
	if (!ApplySelectionTransform(op)) return;
	static const wchar_t* kLabels[] = { L"좌우 반전", L"상하 반전", L"시계 방향 90도 회전", L"반시계 방향 90도 회전" };
	SetModifiedFlag(TRUE);
	CommitUndoPoint(kLabels[(std::max)(0, (std::min)(3, op))]);
	UpdateAllViews(nullptr);
	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
}

void CM2DrawDoc::OnUpdateObjectTransform(CCmdUI* pCmdUI) { pCmdUI->Enable(HasSelectedObjects()); }

// ---------------------------------------------------------------------------
// 순서(Z-order) 변경
// ---------------------------------------------------------------------------
bool CM2DrawDoc::ArrangeSelectedObjects(ge::Document::ZOrder order)
{
	if (!m_doc.CanArrangeSelection())
		return false;

	m_doc.ArrangeSelection(order);   // 선택 노드는 각자의 부모 안에서 이동, 선택 노드끼리의 상대 순서는 유지

	const wchar_t* label = L"순서 변경";
	switch (order)
	{
	case ge::Document::ZOrder::Front:    label = L"맨 앞으로"; break;
	case ge::Document::ZOrder::Forward:  label = L"앞으로";   break;
	case ge::Document::ZOrder::Backward: label = L"뒤로";     break;
	case ge::Document::ZOrder::Back:     label = L"맨 뒤로";  break;
	}

	CString strLog;
	strLog.Format(_T("[순서 변경] %s (선택 %d개)"), label, static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(label);   // 순서가 실제로 바뀌지 않았으면(이미 맨 앞 등) 기록되지 않음
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnObjectArrange(UINT nID)
{
	switch (nID)
	{
	case ID_OBJECT_FRONT:    ArrangeSelectedObjects(ge::Document::ZOrder::Front);    break;
	case ID_OBJECT_FORWARD:  ArrangeSelectedObjects(ge::Document::ZOrder::Forward);  break;
	case ID_OBJECT_BACKWARD: ArrangeSelectedObjects(ge::Document::ZOrder::Backward); break;
	case ID_OBJECT_BACK:     ArrangeSelectedObjects(ge::Document::ZOrder::Back);     break;
	}
}

void CM2DrawDoc::OnUpdateObjectArrange(CCmdUI* pCmdUI) { pCmdUI->Enable(CanArrange()); }

// ---------------------------------------------------------------------------
// 불리언 연산 / 패스파인더
// ---------------------------------------------------------------------------
bool CM2DrawDoc::PerformBooleanOperation(ge::BooleanMode mode)
{
	if (!CanBoolean())
		return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView))) {
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
		}
	}
	if (!pFactory)
		return false;

	ge::PathNode* result = m_doc.BooleanOperation(mode, pFactory);
	if (!result)
		return false;

	UpdateShapePropertiesSelection(result, this);

	const wchar_t* label = L"불리언 연산";
	switch (mode) {
	case ge::BooleanMode::Union:     label = L"패스 합치기"; break;
	case ge::BooleanMode::Subtract:  label = L"패스 빼기"; break;
	case ge::BooleanMode::Intersect: label = L"패스 교차"; break;
	case ge::BooleanMode::Exclude:   label = L"패스 제외"; break;
	}

	CString strLog;
	strLog.Format(_T("[%s] '%s' 생성 완료"), label, result->GetName().c_str());
	LogAppEvent(strLog);
	SetAppStatusText(strLog);

	CommitUndoPoint(label);
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	}
	return true;
}

void CM2DrawDoc::OnBooleanOperation(UINT nID)
{
	switch (nID) {
	case ID_BOOLEAN_UNION:     PerformBooleanOperation(ge::BooleanMode::Union);     break;
	case ID_BOOLEAN_SUBTRACT:  PerformBooleanOperation(ge::BooleanMode::Subtract);  break;
	case ID_BOOLEAN_INTERSECT: PerformBooleanOperation(ge::BooleanMode::Intersect); break;
	case ID_BOOLEAN_EXCLUDE:   PerformBooleanOperation(ge::BooleanMode::Exclude);   break;
	case ID_BOOLEAN_TRIM:
		GetApp()->GetToolManager().SetActiveTool(ToolType::Trim);   // 트림은 대화형 도구 (Step 6.21)
		break;
	}
}

void CM2DrawDoc::OnUpdateBooleanOperation(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanBoolean());
}

bool CM2DrawDoc::MakeClipMask()
{
	ge::GroupNode* g = m_doc.MakeClipMask();
	if (!g) return false;

	UpdateShapePropertiesSelection(g, this);
	CommitUndoPoint(L"클리핑 마스크 만들기");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

bool CM2DrawDoc::ReleaseClipMask()
{
	if (!CanReleaseClipMask()) return false;
	m_doc.ReleaseClipMask();

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"클리핑 마스크 해제");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

void CM2DrawDoc::OnObjectClipMask()
{
	MakeClipMask();
}

void CM2DrawDoc::OnUpdateObjectClipMask(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanMakeClipMask());
}

void CM2DrawDoc::OnObjectReleaseMask()
{
	ReleaseClipMask();
}

void CM2DrawDoc::OnUpdateObjectReleaseMask(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanReleaseClipMask());
}

bool CM2DrawDoc::OffsetPath(float distance)
{
	if (!CanOffsetPath()) return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView))) {
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
		}
	}
	if (!pFactory) return false;

	auto results = m_doc.OffsetPathSelection(distance, pFactory);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"Path Offset");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

bool CM2DrawDoc::OutlineStroke(float widthOverride)
{
	if (!CanOutlineStroke()) return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView))) {
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
		}
	}
	if (!pFactory) return false;

	auto results = m_doc.OutlineStrokeSelection(pFactory, widthOverride);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"윤곽선 만들기 (외곽선 확장)");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

bool CM2DrawDoc::ConvertTextToPath()
{
	if (!CanConvertTextToPath()) return false;

	ID2D1Factory* pFactory = nullptr;
	POSITION pos = GetFirstViewPosition();
	if (pos) {
		CView* pView = GetNextView(pos);
		if (pView && pView->IsKindOf(RUNTIME_CLASS(CM2DrawView)))
			pFactory = static_cast<CM2DrawView*>(pView)->GetD2DFactory();
	}
	if (!pFactory) return false;

	auto results = m_doc.TextToPathSelection(pFactory);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"텍스트를 윤곽선으로 변환");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

void CM2DrawDoc::OnTextToPath() { ConvertTextToPath(); }

// 이미지 파일을 읽어 ImageData 로 (실패하면 메시지 후 nullptr)
static std::shared_ptr<ge::ImageData> LoadImageFile(const CString& path)
{
	auto data = std::make_shared<ge::ImageData>();
	{
		CFile f;
		if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite)) { AfxMessageBox(_T("이미지 파일을 열 수 없습니다.")); return nullptr; }
		const ULONGLONG len = f.GetLength();
		if (len == 0 || len > 256ull * 1024 * 1024) { AfxMessageBox(_T("이미지 파일 크기가 올바르지 않습니다.")); return nullptr; }
		data->bytes.resize(static_cast<size_t>(len));
		f.Read(data->bytes.data(), static_cast<UINT>(len));
	}
	int pw = 0, ph = 0;
	if (!ge::PathOp::DecodeImageInfo(data->bytes, pw, ph)) { AfxMessageBox(_T("지원하지 않는 이미지 형식이거나 손상된 파일입니다.")); return nullptr; }
	data->pixelW = pw; data->pixelH = ph;
	CString ext = path.Mid(path.ReverseFind(_T('.')) + 1); ext.MakeLower();
	data->mime = (ext == _T("jpg") || ext == _T("jpeg")) ? "image/jpeg" : ext == _T("gif") ? "image/gif" : ext == _T("bmp") ? "image/bmp" : "image/png";
	return data;
}

static const TCHAR* kImageFilter = _T("이미지 파일 (*.png;*.jpg;*.jpeg;*.gif;*.bmp)|*.png;*.jpg;*.jpeg;*.gif;*.bmp|모든 파일 (*.*)|*.*||");

// 이미지 파일을 문서 중앙에(문서 크기의 60% 이내로 맞춰) ImageNode 로 추가
bool CM2DrawDoc::PlaceImageFile(const CString& path)
{
	auto data = LoadImageFile(path);
	if (!data) return false;

	float w = static_cast<float>(data->pixelW), h = static_cast<float>(data->pixelH);
	const float maxW = m_doc.width * 0.6f, maxH = m_doc.height * 0.6f;
	const float sc = (std::min)(1.0f, (std::min)(maxW / w, maxH / h));
	w *= sc; h *= sc;

	auto im = std::make_unique<ge::ImageNode>();
	im->SetData(data);
	im->SetSize(w, h);
	const int slash = (std::max)(path.ReverseFind(_T('\\')), path.ReverseFind(_T('/')));
	CString title = path.Mid(slash + 1);
	const int dot = title.ReverseFind(_T('.'));
	if (dot > 0) title = title.Left(dot);
	im->SetName((LPCTSTR)title);
	im->SetTransform(ge::Matrix2D::Translation((m_doc.width - w) * 0.5f, (m_doc.height - h) * 0.5f));
	ge::Node* added = m_doc.AddShape(std::move(im));
	if (!added) return false;
	m_doc.SetSelection({ added });
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"이미지 가져오기");
	LogAppEvent(_T("[이미지] 이미지 가져오기 완료"));
	UpdateShapePropertiesSelection(added, this);
	UpdateAllViews(nullptr);
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

// 내보내기 옵션 대화상자 (메모리 템플릿): 텍스트를 윤곽선으로 변환할지 선택
namespace {
class CExportOptionsDlg : public CDialog
{
public:
	bool m_outlineText = false;

	INT_PTR Run(CWnd* parent)
	{
		std::vector<WORD> w;
		auto add32 = [&](DWORD v) { w.push_back(LOWORD(v)); w.push_back(HIWORD(v)); };
		auto addStr = [&](const wchar_t* s) { while (*s) w.push_back(*s++); w.push_back(0); };
		auto align = [&]() { if (w.size() % 2) w.push_back(0); };
		add32(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
		add32(0);
		w.push_back(4);
		w.push_back(0); w.push_back(0); w.push_back(250); w.push_back(104);
		w.push_back(0); w.push_back(0);
		addStr(L"SVG 내보내기");
		w.push_back(9); addStr(L"Segoe UI");
		auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t* title) {
			align();
			add32(style | WS_CHILD | WS_VISIBLE);
			add32(0);
			w.push_back(x); w.push_back(y); w.push_back(cx); w.push_back(cy);
			w.push_back(id);
			w.push_back(0xFFFF); w.push_back(atom);
			addStr(title);
			w.push_back(0);
		};
		item(SS_LEFT, 8, 8, 234, 30, 0xFFFF, 0x0082, L"순수 SVG(Illustrator, Inkscape, Figma, 브라우저 호환)로 내보냅니다. M2Draw 전용 속성은 포함하지 않으며, 글꼴에는 대체 글꼴 목록이 붙습니다.");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 8, 46, 234, 12, 1001, 0x0080, L"텍스트를 윤곽선으로 변환하여 출력 (글꼴이 없어도 똑같이 보임)");
		item(BS_DEFPUSHBUTTON | WS_TABSTOP, 136, 80, 50, 14, IDOK, 0x0080, L"내보내기");
		item(BS_PUSHBUTTON | WS_TABSTOP, 192, 80, 50, 14, IDCANCEL, 0x0080, L"취소");
		if (!InitModalIndirect(reinterpret_cast<LPCDLGTEMPLATE>(w.data()), parent)) return IDCANCEL;
		return DoModal();
	}
protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		CheckDlgButton(1001, m_outlineText ? BST_CHECKED : BST_UNCHECKED);
		return TRUE;
	}
	void OnOK() override
	{
		m_outlineText = IsDlgButtonChecked(1001) == BST_CHECKED;
		CDialog::OnOK();
	}
};
} // namespace

void CM2DrawDoc::OnFileExportSvg()
{
	CExportOptionsDlg opt;
	opt.m_outlineText = AfxGetApp()->GetProfileInt(_T("Export"), _T("OutlineText"), 0) != 0;
	if (opt.Run(AfxGetMainWnd()) != IDOK) return;
	AfxGetApp()->WriteProfileInt(_T("Export"), _T("OutlineText"), opt.m_outlineText ? 1 : 0);

	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);
	CFileDialog dlg(FALSE, _T("svg"), suggested + _T(".svg"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("SVG 파일 (*.svg)|*.svg||"));
	if (dlg.DoModal() != IDOK) return;

	ge::SvgWriteOptions options(ge::SvgWriteMode::Clean);
	options.textAsOutlines = opt.m_outlineText;
	ge::SvgWriter writer;
	if (!writer.WriteToFile(m_doc, (LPCTSTR)dlg.GetPathName(), options))
	{
		AfxMessageBox(_T("SVG 파일을 저장하지 못했습니다."));
		return;
	}
	CString log;
	log.Format(_T("[내보내기] 순수 SVG 저장: %s%s"), (LPCTSTR)dlg.GetPathName(), opt.m_outlineText ? _T(" (텍스트 윤곽선)") : _T(""));
	LogAppEvent(log);
	SetAppStatusText(log);
}

// ============================================================================
// 클립보드 (Step 5.4): SVG 문자열로 교환. 형식: "image/svg+xml"(UTF-8 바이트) + CF_UNICODETEXT
// ============================================================================
static UINT SvgClipboardFormat()
{
	static const UINT f = ::RegisterClipboardFormatW(L"image/svg+xml");
	return f;
}

static std::string ReadClipboardSvgText()
{
	std::string out;
	CWnd* wnd = AfxGetMainWnd();
	if (!wnd || !wnd->OpenClipboard()) return out;
	auto readBytes = [&](UINT fmt, bool wide) -> std::string {
		HANDLE h = ::GetClipboardData(fmt);
		if (!h) return std::string();
		const void* p = ::GlobalLock(h);
		if (!p) return std::string();
		std::string r;
		if (wide) {
			const wchar_t* w = static_cast<const wchar_t*>(p);
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
			if (n > 1) { r.resize(static_cast<size_t>(n) - 1); ::WideCharToMultiByte(CP_UTF8, 0, w, -1, r.data(), n, nullptr, nullptr); }
		} else {
			r.assign(static_cast<const char*>(p));
		}
		::GlobalUnlock(h);
		return r;
	};
	if (::IsClipboardFormatAvailable(SvgClipboardFormat())) out = readBytes(SvgClipboardFormat(), false);
	if (!ge::LooksLikeSvg(out) && ::IsClipboardFormatAvailable(CF_UNICODETEXT)) out = readBytes(CF_UNICODETEXT, true);
	::CloseClipboard();
	return out;
}

bool CM2DrawDoc::CopySelectionToClipboard()
{
	const std::string svg = ge::SerializeNodesToSvg(m_doc, m_doc.GetSelection());
	if (svg.empty()) return false;
	CWnd* wnd = AfxGetMainWnd();
	if (!wnd || !wnd->OpenClipboard()) return false;
	::EmptyClipboard();

	HGLOBAL hSvg = ::GlobalAlloc(GMEM_MOVEABLE, svg.size() + 1);
	if (hSvg) {
		if (void* p = ::GlobalLock(hSvg)) { memcpy(p, svg.c_str(), svg.size() + 1); ::GlobalUnlock(hSvg); }
		if (!::SetClipboardData(SvgClipboardFormat(), hSvg)) ::GlobalFree(hSvg);
	}
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, svg.c_str(), -1, nullptr, 0);
	if (n > 0) {
		HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(n) * sizeof(wchar_t));
		if (hText) {
			if (void* p = ::GlobalLock(hText)) { ::MultiByteToWideChar(CP_UTF8, 0, svg.c_str(), -1, static_cast<wchar_t*>(p), n); ::GlobalUnlock(hText); }
			if (!::SetClipboardData(CF_UNICODETEXT, hText)) ::GlobalFree(hText);
		}
	}
	::CloseClipboard();
	m_pasteCount = 0;
	CString log;
	log.Format(_T("[클립보드] SVG 복사 (%d개 선택)"), static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(log);
	return true;
}

bool CM2DrawDoc::PasteFromClipboard()
{
	const std::string svg = ReadClipboardSvgText();
	if (!ge::LooksLikeSvg(svg)) {
		// 개발자도구의 outerHTML 복사는 구조 정보로 저장하고 자동 처리를 시작한다
		const std::wstring text = ReadClipboardText(AfxGetMainWnd());
		if (LooksLikeHtmlText(text)) return SaveStructureFromClipboard(text);
		// 그림 캡처는 사이트 주소가 있어야 웹뷰로 구조를 복사할 수 있다: 주소창이 비어 있으면 주소를 먼저 묻는다
		// 그림에는 주소창이 없으므로 주소창 값을 확인받는다 (이전 주소가 남아 있을 수 있다)
		if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) {
			CString url = mf->GetUrlText();
			url.Trim();
			if (url.IsEmpty()) {
				AfxMessageBox(_T("이 그림의 사이트 주소를 모르겠습니다.\n주소창에 사이트 주소를 입력한 뒤 다시 붙여넣으세요.\n(예: https://www.example.com)"), MB_ICONQUESTION);
				mf->FocusUrlEdit();
				return false;
			}
			CString ask;
			ask.Format(_T("이 그림의 사이트 주소가 맞습니까?\n\n%s\n\n예: 이 주소로 진행\n아니요: 주소창을 고쳐서 다시 붙여넣기"), (LPCTSTR)url);
			if (AfxMessageBox(ask, MB_YESNO | MB_ICONQUESTION) != IDYES) {
				mf->FocusUrlEdit();
				return false;
			}
		}
		return PasteBitmapAsReference();
	}
	return ImportSvgNodes(svg, true);
}

// SVG 텍스트의 노드를 문서에 넣는다. 선택이 프레임(또는 그 안)이면 그 프레임 안으로, 아니면 활성 레이어.
// isPaste: 연속 붙여넣기 때 겹치지 않도록 조금씩 밀어 놓는다. AI 산출물 가져오기는 밀지 않는다 (7.2.3)
bool CM2DrawDoc::ImportSvgNodes(const std::string& svg, bool isPaste)
{
	ge::Document tmp;
	std::vector<const ge::Node*> src;
	if (!ge::ParseSvgNodes(svg, tmp, src)) return false;

	ge::FrameNode* targetFrame = nullptr;
	if (ge::Node* sel = GetPrimarySelection())
		for (ge::Node* p = sel; p; p = p->GetParent())
			if (auto* fr = dynamic_cast<ge::FrameNode*>(p)) { targetFrame = fr; break; }

	std::vector<ge::Node*> imported = m_doc.ImportNodes(src);
	if (imported.empty()) return false;

	if (isPaste) {
		++m_pasteCount;
		const float off = 16.0f * static_cast<float>(((m_pasteCount - 1) % 6) + 1);   // 연속 붙여넣기 시 겹치지 않게
		for (ge::Node* n : imported)
			n->SetTransform(n->GetTransform() * ge::Matrix2D::Translation(off, off));
	}
	if (targetFrame)
		for (ge::Node* n : imported) {
			if (m_doc.MoveNodeTo(n, targetFrame, -1)) ge::FrameNode::ClampNode(n, false);
		}

	m_doc.SetSelection(imported);
	SetModifiedFlag(TRUE);
	CommitUndoPoint(isPaste ? L"붙여넣기" : L"AI 산출물 가져오기");
	LogAppEvent(isPaste ? _T("[클립보드] SVG 붙여넣기") : _T("[가져오기] AI 산출물 SVG 가져오기"));
	UpdateShapePropertiesSelection(imported.front(), this);
	UpdateAllViews(nullptr);
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

// AI 산출물 SVG 가져오기 (7.2.3): 파일을 고르면 선택한 프레임 안에 놓는다
void CM2DrawDoc::OnFileImportAgentSvg()
{
	CFileDialog dlg(TRUE, _T("svg"), nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("SVG 파일 (*.svg)|*.svg|모든 파일 (*.*)|*.*||"));
	if (dlg.DoModal() != IDOK) return;

	std::ifstream ifs(std::wstring((LPCTSTR)dlg.GetPathName()), std::ios::binary);
	const std::string svg((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	ge::NodeId frameId = 0;
	for (ge::Node* p = GetPrimarySelection(); p; p = p->GetParent())
		if (dynamic_cast<ge::FrameNode*>(p)) { frameId = p->GetId(); break; }
	if (svg.empty() || !ImportSvgNodes(svg, false)) {
		AfxMessageBox(_T("SVG 파일을 읽을 수 없습니다."), MB_ICONERROR);
		return;
	}
	RecordWatchedImport(dlg.GetPathName(), frameId, m_doc.GetSelection());
}

// 가져온 파일과 노드를 기록해 두어 외부 수정을 나중에 알아챌 수 있게 한다 (7.2.4)
void CM2DrawDoc::RecordWatchedImport(const CString& path, ge::NodeId frameId, const std::vector<ge::Node*>& nodes)
{
	m_watchPath = path;
	m_watchFrame = frameId;
	m_watchNodes.clear();
	for (ge::Node* n : nodes)
		if (n) m_watchNodes.push_back(n->GetId());
	std::error_code ec;
	m_watchMtime = std::filesystem::last_write_time((LPCTSTR)path, ec).time_since_epoch().count();
	CString log;
	log.Format(_T("[외부 수정 감시] %s 등록 (도형 %d개)"), (LPCTSTR)path, static_cast<int>(m_watchNodes.size()));
	LogAppEvent(log);
}

// 창이 활성화될 때: 가져온 AI 산출물 파일이 바뀌었으면 다시 불러올지 묻는다 (7.2.4)
void CM2DrawDoc::CheckExternalImportChange()
{
	static bool s_busy = false;   // 확인 창이 떠 있는 동안 다시 들어오지 않게
	if (s_busy || m_watchPath.IsEmpty() || m_watchNodes.empty()) return;
	std::error_code ec;
	const long long mt = std::filesystem::last_write_time((LPCTSTR)m_watchPath, ec).time_since_epoch().count();
	if (ec || mt == m_watchMtime) return;
	s_busy = true;
	struct BusyReset { bool& b; ~BusyReset() { b = false; } } busyReset{ s_busy };

	if (AfxMessageBox(_T("가져온 AI 산출물 파일이 외부에서 수정되었습니다.\n다시 불러올까요? (이전에 가져온 도형은 지워집니다)"), MB_YESNO | MB_ICONQUESTION) != IDYES) {
		m_watchMtime = mt;   // 이 수정은 무시
		return;
	}

	std::ifstream ifs(std::wstring((LPCTSTR)m_watchPath), std::ios::binary);
	const std::string svg((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	const ge::NodeId frameId = m_watchFrame;
	for (ge::NodeId id : m_watchNodes)
		if (ge::Node* n = m_doc.FindById(id)) m_doc.RemoveNode(n);   // 반환된 소유권은 바로 버린다 (삭제)
	ge::Node* frame = frameId ? m_doc.FindById(frameId) : nullptr;
	m_doc.SetSelection(frame ? std::vector<ge::Node*>{ frame } : std::vector<ge::Node*>{});
	if (svg.empty() || !ImportSvgNodes(svg, false)) {
		AfxMessageBox(_T("다시 불러오지 못했습니다. 파일을 확인하세요."), MB_ICONERROR);
		m_watchNodes.clear();
		UpdateAllViews(nullptr);
		return;
	}
	RecordWatchedImport(m_watchPath, frameId, m_doc.GetSelection());
	CString log;
	log.Format(_T("[가져오기] 외부 수정된 AI 산출물을 다시 불러옴: %s"), (LPCTSTR)m_watchPath);
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 좌표 소수 2자리 SVG (순수 SVG + 최적화) 내보내기 (7.4.4)
void CM2DrawDoc::OnFileExportSvgOptimized()
{
	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);
	CFileDialog dlg(FALSE, _T("svg"), suggested + _T("_opt.svg"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("SVG 파일 (*.svg)|*.svg||"));
	if (dlg.DoModal() != IDOK) return;

	ge::SvgWriteOptions opt(ge::SvgWriteMode::Clean);
	opt.decimals = 2;
	opt.optimize = true;
	ge::SvgWriter writer;
	if (!writer.WriteToFile(m_doc, (LPCTSTR)dlg.GetPathName(), opt)) {
		AfxMessageBox(_T("SVG 파일을 쓸 수 없습니다."), MB_ICONERROR);
		return;
	}
	CString log;
	log.Format(_T("[SVG] 최적화 SVG 저장 (좌표 2자리): %s"), (LPCTSTR)dlg.GetPathName());
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 주석(메모) 추가 (7.3.3): 선택한 도형의 오른쪽 위에 "메모" 글자를 놓는다. 더블클릭하면 글을 고친다
void CM2DrawDoc::OnFileAddAnnotation()
{
	ge::Vec2 at{ m_doc.width * 0.5f, m_doc.height * 0.5f };
	ge::Rect2 b;
	if (m_doc.GetSelectionBounds(b))
		at = ge::Vec2{ b.right + 20.0f, b.top };
	ge::TextNode* note = ge::AddAnnotation(m_doc, L"메모", at);
	if (!note) return;

	ge::Node* added = note;
	m_doc.SetSelection({ added });
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"주석 추가");
	LogAppEvent(_T("[주석] 주석 추가 (더블클릭해서 글을 고칩니다)"));
	UpdateShapePropertiesSelection(added, this);
	UpdateAllViews(nullptr);
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
}

// 클립보드의 웹 주소를 캡처해 레퍼런스로 가져온다 (7.1.6). 분석 JSON(7.1.7)은 원하면 폴더에 함께 저장한다
void CM2DrawDoc::OnFileImportUrlReference()
{
	ImportUrlReference(ReadClipboardText(AfxGetMainWnd()));
}

// 캡처 파일(*.m2svg)을 불러와 레퍼런스로 올리고, 주소와 DOM 요약을 되살린다
namespace {

bool ReadWholeFileA(const CString& path, std::string& out)
{
	std::ifstream ifs(std::filesystem::path((LPCTSTR)path), std::ios::binary);
	if (!ifs) return false;
	out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return true;
}

} // namespace

void CM2DrawDoc::LoadCaptureFile(const std::wstring& path)
{
	ge::CaptureFileData cap;
	std::wstring err;
	std::string head;
	const bool folderFormat = ReadWholeFileA(CString(path.c_str()), head) && ge::IsCaptureManifest(head);
	const bool ok = folderFormat ? ge::ReadCaptureFolder(path, cap, &err) : ge::ReadCaptureFile(path, cap, &err);
	if (!ok) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}
	m_agentLastDom = AgentWide(cap.domJson);
	m_agentSiteUrl = AgentWide(cap.url);

	auto data = std::make_shared<ge::ImageData>();
	data->bytes = cap.png;
	data->mime = "image/png";
	data->pixelW = cap.width;
	data->pixelH = cap.height;
	const CString title = CString(std::filesystem::path(path).stem().c_str());
	if (!PlaceReferenceData(data, title, true)) {
		AfxMessageBox(_T("레퍼런스 이미지를 놓지 못했습니다."), MB_ICONERROR);
		return;
	}

	CString log;
	log.Format(_T("[URL] 캡처 불러오기: %s (%d x %d) 주소: %s"), (LPCTSTR)title, cap.width, cap.height,
		(LPCTSTR)CString(m_agentSiteUrl.c_str()));
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 파일 > 캡처 불러오기 (*.m2svg)
void CM2DrawDoc::ImportReferenceFile(const std::wstring& pngPath)
{
	namespace fs = std::filesystem;
	const fs::path png(pngPath);
	std::string bytes;
	if (!ProjReadAll(png, bytes) || bytes.size() < 24) {
		AfxMessageBox(_T("그림 파일을 읽을 수 없습니다."), MB_ICONERROR);
		return;
	}
	auto data = std::make_shared<ge::ImageData>();
	data->bytes.assign(bytes.begin(), bytes.end());
	data->mime = "image/png";
	auto be32 = [&](size_t o) {
		return (static_cast<unsigned>(static_cast<unsigned char>(bytes[o])) << 24) | (static_cast<unsigned>(static_cast<unsigned char>(bytes[o + 1])) << 16) |
		       (static_cast<unsigned>(static_cast<unsigned char>(bytes[o + 2])) << 8) | static_cast<unsigned>(static_cast<unsigned char>(bytes[o + 3]));
	};
	data->pixelW = static_cast<int>(be32(16));
	data->pixelH = static_cast<int>(be32(20));

	const std::wstring name = png.stem().wstring();
	std::string dom;
	if (ProjReadAll(fs::path(png).replace_extension(L".json"), dom)) {
		m_agentLastDom = AgentWide(dom);
		g_project.refDoms[name] = dom;
	}
	if (!g_project.folder.empty()) {
		bool known = false;
		for (const auto& r : g_project.refs)
			if (r.leaf == name) known = true;
		if (!known) {
			ProjectRefEntry r;
			int id = 1;
			for (const auto& e : g_project.refs) id = (std::max)(id, e.id + 1);
			r.id = id;
			r.leaf = name;
			r.created = ProjNow();
			r.width = data->pixelW;
			r.height = data->pixelH;
			g_project.refs.push_back(r);
		}
	}
	if (!PlaceReferenceData(data, CString(name.c_str()), true)) {
		AfxMessageBox(_T("레퍼런스 이미지를 놓지 못했습니다."), MB_ICONERROR);
		return;
	}
	SetModifiedFlag(TRUE);
}
void CM2DrawDoc::ImportUrlReference(std::wstring url)
{
	while (!url.empty() && (url.back() == L'\r' || url.back() == L'\n' || url.back() == L' ')) url.pop_back();

	// 주소창에 캡처 파일(.m2svg) 경로를 넣으면 다시 불러온다 (따옴표 제거 후 확인)
	std::wstring path = url;
	if (!path.empty() && path.front() == L'"') path.erase(0, 1);
	if (!path.empty() && path.back() == L'"') path.pop_back();
	if (path.size() > 6 && _wcsicmp(path.c_str() + path.size() - 6, L".m2svg") == 0) {
		LoadCaptureFile(path);
		return;
	}
	const bool isWeb = url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0;
	if (!isWeb) {
		AfxMessageBox(_T("클립보드에 웹 주소(http:// 또는 https://)를 먼저 복사한 뒤 다시 실행하세요."), MB_ICONINFORMATION);
		return;
	}

	// 캡처가 끝난 뒤 에이전트로 자동 처리할지 먼저 묻는다 (예 = 구조 파악을 바로 요청, 아니오 = 수동)
	const bool autoAnalyze = AfxMessageBox(_T("에이전트로 자동 처리하시겠습니까?\n\n예: 캡처 후 구조 파악을 바로 요청합니다.\n아니오: 수동으로 처리합니다."), MB_YESNO | MB_ICONQUESTION) == IDYES;

	ge::WebCaptureResult res;
	std::wstring err;
	BeginWaitCursor();
	const bool ok = ge::CaptureWebPage(url, 1920, 45000, res, &err);
	EndWaitCursor();
	if (!ok) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}

	m_agentLastDom = AgentWide(res.domJson);   // 에이전트 작업 시작 때 dom.json 으로 함께 넣는다
	m_agentSiteUrl = url;
	auto data = std::make_shared<ge::ImageData>();
	data->bytes = res.png;
	data->mime = "image/png";
	data->pixelW = static_cast<int>(res.pixelW);
	data->pixelH = static_cast<int>(res.pixelH);
	// 1) 프로젝트가 아직 없으면 한 번 묻는다 (저장하면 이 캡처가 첫 레퍼런스가 된다)
	// 저장하지 않은 문서에서는 처음 한 번만 묻는다 (아니요를 골랐으면 다음 캡처에서 다시 묻지 않는다. 저장은 Ctrl+S 로)
	// 열린 프로젝트가 있으면 이 문서는 그 프로젝트의 페이지가 된다 (묻지 않는다)
	if (GetPathName().IsEmpty() && !g_project.folder.empty())
		AttachToProject();

	bool wantSave = false;
	if (GetPathName().IsEmpty() && !m_saveAsked)
	{
		m_saveAsked = true;
		wantSave = AfxMessageBox(_T("이 캡처를 프로젝트로 저장할까요?\n\n예: 폴더를 만들고 저장합니다. 이후 캡처와 저장은 묻지 않고 같은 폴더에 저장합니다.\n아니요: 지금은 저장하지 않습니다. (나중에 Ctrl+S 로 저장)"), MB_YESNO | MB_ICONQUESTION) == IDYES;
	}
	if (wantSave)
	{
		std::wstring host = url;
		if (const size_t scheme = host.find(L"://"); scheme != std::wstring::npos) host = host.substr(scheme + 3);
		host = host.substr(0, host.find(L'/'));
		if (host.rfind(L"www.", 0) == 0) host = host.substr(4);
		std::vector<std::wstring> labels;
		for (size_t s = 0, e = 0; s <= host.size(); s = e + 1)
		{
			e = host.find(L'.', s);
			if (e == std::wstring::npos) e = host.size();
			if (e > s) labels.push_back(host.substr(s, e - s));
		}
		std::wstring siteName = labels.empty() ? std::wstring(L"capture") : labels[labels.size() >= 2 ? labels.size() - 2 : 0];
		if (labels.size() >= 3 && labels.back().size() == 2 && labels[labels.size() - 2].size() <= 3)
			siteName = labels[labels.size() - 3];
		const CString suggested = CString(siteName.c_str()) + _T("_") + CTime::GetCurrentTime().Format(_T("%Y%m%d%H%M"));

		CFileDialog dlg(FALSE, nullptr, suggested, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
			_T("M2Draw 프로젝트 (*.m2svg)|*.m2svg||"), AfxGetMainWnd());
		dlg.m_ofn.lpstrTitle = _T("프로젝트 저장 (폴더 이름)");
		if (dlg.DoModal() == IDOK)
			DoSave(dlg.GetPathName(), TRUE);
	}

	// 2) 레퍼런스를 현재 문서에 올린다. 이름은 기본값(사이트_시각)이고, 저장할 때 레이어 이름이 파일 이름이 된다
	m_capturePng = res.png;
	m_agentLastDom = AgentWide(res.domJson);
	const std::wstring base = DefaultReferenceName(url);
	int id = 1;
	for (const auto& r : g_project.refs)
		id = (std::max)(id, r.id + 1);
	g_project.refDoms[base] = res.domJson;
	ProjectRefEntry r;
	r.id = id;
	r.leaf = base;
	r.url = url;
	r.created = ProjNow();
	r.width = static_cast<int>(res.pixelW);
	r.height = static_cast<int>(res.pixelH);
	g_project.refs.push_back(r);

	if (!PlaceReferenceData(data, CString(base.c_str()), true)) {
		AfxMessageBox(_T("레퍼런스 이미지를 놓지 못했습니다."), MB_ICONERROR);
		return;
	}
	if (!GetPathName().IsEmpty() && !g_project.folder.empty())
	{
		SaveProject(GetPathName());   // 프로젝트가 있으면 그 자리에 바로 저장한다
		SetModifiedFlag(FALSE);
	}
	else
	{
		SetModifiedFlag(TRUE);
	}
	CString log;
	log.Format(_T("[URL] 레퍼런스 가져오기: %s (%u x %u)"), (LPCTSTR)CString(url.c_str()), res.pixelW, res.pixelH);
	LogAppEvent(log);
	SetAppStatusText(log);

	// 자동 처리를 골랐으면 에이전트 작업을 시작하고 바로 구조 파악을 요청한다
	if (autoAnalyze) {
		OnFileAgentStart();
		if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
			mf->GetOutputWnd().GetChat().RunAutoChain();   // 구조 파악 → 웹페이지 생성
	}
}

// 웹폰트 링크 설정 (7.1.4): 입력한 HTML 을 문서에 저장한다 (웹 출력 <head> 에 들어감)
void CM2DrawDoc::OnFileWebFontLink()
{
	CWebFontDlg dlg(CString(m_doc.webFontHead.c_str()), AfxGetMainWnd());
	if (dlg.DoModal() != IDOK) return;
	m_doc.webFontHead = std::wstring((LPCTSTR)dlg.m_text);
	SetModifiedFlag(TRUE);
	LogAppEvent(_T("[웹폰트] 웹폰트 링크를 문서에 저장했습니다"));
}

namespace {
std::string WideToUtf8Text(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}
} // namespace
// ---- 에이전트 작업 연결 (7.2.6) ----------------------------------------------
namespace {

std::string AgentUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}

std::wstring AgentWide(const std::string& s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

// 파일 이름으로 쓸 수 없는 문자를 _ 로 바꾼다
std::wstring AgentSafeName(std::wstring s)
{
	const std::wstring bad = L"\\/:*?\"<>|";
	for (auto& ch : s) if (bad.find(ch) != std::wstring::npos || ch < 0x20) ch = L'_';
	while (!s.empty() && (s.back() == L' ' || s.back() == L'.')) s.pop_back();
	return s.empty() ? L"untitled" : s;
}

bool AgentWriteFile(const std::filesystem::path& p, const std::string& data)
{
	std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
	if (!ofs) return false;
	ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
	return static_cast<bool>(ofs);
}

} // namespace

// 에이전트 작업 시작: 패키지(scene, 그림, 지시문, 안내, DOM 요약)를 작업 폴더에 만들고, 결과를 받을 프레임을 정한다
// 선택한 프레임 하나만 에이전트 패키지로 보낸다 (7.2.11). 결과는 그 프레임 안에 다시 들어간다
void CM2DrawDoc::OnFileAgentSendFrame()
{
	namespace fs = std::filesystem;

	ge::FrameNode* frame = nullptr;
	if (ge::Node* sel = GetPrimarySelection())
		for (ge::Node* p = sel; p && !frame; p = p->GetParent())
			frame = dynamic_cast<ge::FrameNode*>(p);
	if (!frame) {
		AfxMessageBox(_T("먼저 캔버스에서 프레임을 선택하세요."), MB_ICONINFORMATION);
		return;
	}

	// 패키지 폴더: 작업 폴더\사이트host_frame_시각
	const fs::path root = AgentRootForDoc();
	std::wstring host = m_agentSiteUrl;
	if (const size_t scheme = host.find(L"://"); scheme != std::wstring::npos) host = host.substr(scheme + 3);
	host = host.substr(0, host.find(L'/'));
	const std::wstring stamp = std::wstring((LPCTSTR)CTime::GetCurrentTime().Format(_T("%Y%m%d_%H%M%S")));
	const std::wstring frameName = frame->GetName().empty() ? std::wstring(L"frame") : frame->GetName();
	const fs::path dir = root / AgentSafeName((host.empty() ? frameName : host + L"_" + frameName) + L"_" + stamp);

	ge::AgentHandoffOptions opt;
	opt.target = frame;
	std::wstring err;
	if (!ge::WriteAgentPackage(m_doc, dir.wstring(), opt, &err)) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}
	LinkPackageToWorkspace(dir);

	const std::string guide = ge::BuildAgentGuideUtf8();
	std::error_code ec;
	fs::create_directories(root, ec);
	AgentWriteFile(root / L"AGENT_GUIDE.md", guide);
	AgentWriteFile(dir / L"AGENT_GUIDE.md", guide);
	if (!m_agentLastDom.empty())
		AgentWriteFile(dir / L"dom.json", AgentUtf8(m_agentLastDom));

	// 연결: 결과 SVG 는 이 프레임에 다시 들어간다 (AgentCheckResults 가 m_agentFrameId 를 쓴다)
	m_agentDir = CString(dir.c_str());
	// 에이전트 위치를 프로젝트 목차에도 남긴다 (프로젝트가 저장된 문서라면)
	if (!GetPathName().IsEmpty() && !g_project.folder.empty())
		SaveProject(GetPathName());
	m_agentFrameId = frame->GetId();
	m_agentResultSeen.clear();
	m_agentResultNodes.clear();
	m_agentSeenVersion = ge::ChangeTracker::Value();
	m_agentSyncedVersion = ~0ull;
	m_agentChangedAt = 0;

	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) {
		CChatAgentWnd& chat = mf->GetOutputWnd().GetChat();
		chat.SetPackageDir(m_agentDir, CString(m_agentSiteUrl.c_str()));
		chat.RunFrameRequest(CString(frameName.c_str()), frame->GetWidth(), frame->GetHeight());
	}

	CString log;
	log.Format(_T("[에이전트] 선택 프레임 보내기: %s (%s)"), (LPCTSTR)CString(frameName.c_str()), (LPCTSTR)m_agentDir);
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 클립보드의 HTML(outerHTML) 을 구조 정보(dom.json)로 저장한다. 패키지가 있으면 그 안에도 바로 쓴다. 에이전트는 실행하지 않는다
bool CM2DrawDoc::SaveStructureFromClipboard(const std::wstring& html)
{
	m_agentLastDom = html;
	if (!m_agentDir.IsEmpty())
		AgentWriteFile(std::filesystem::path((LPCTSTR)m_agentDir) / L"dom.json", AgentUtf8(html));
	LogAppEvent(_T("[구조] 클립보드 HTML 을 구조 정보로 저장했습니다"));
	SetAppStatusText(_T("구조 정보 저장됨"));
	return true;
}

void CM2DrawDoc::OnFileAgentStart()
{
	namespace fs = std::filesystem;

	// 1. 결과를 받을 프레임: 선택한 프레임(없으면 대지 크기의 새 프레임)
	ge::FrameNode* frame = nullptr;
	if (ge::Node* sel = GetPrimarySelection())
		for (ge::Node* p = sel; p && !frame; p = p->GetParent())
			frame = dynamic_cast<ge::FrameNode*>(p);
	if (!frame) {
		auto fr = std::make_unique<ge::FrameNode>();
		fr->SetFrameSize(m_doc.width, m_doc.height);
		fr->SetName(L"에이전트 작업");
		frame = static_cast<ge::FrameNode*>(m_doc.AddShape(std::move(fr)));
		if (!frame) return;
		ge::Node* one = frame;
		m_doc.SetSelection({ one });
		UpdateAllViews(nullptr);
	}

	// 2. 패키지 폴더: 작업 폴더\사이트 host_시각 (시작할 때마다 새 폴더 = 새 대화, 이전 결과가 섞이지 않음)
	const fs::path root = AgentRootForDoc();
	CString title = GetTitle();
	const int dot = title.ReverseFind(_T('.'));
	if (dot > 0) title = title.Left(dot);
	title.Replace(_T(" *"), _T(""));   // 저장 안 됨 표시(*) 제거
	title.Trim();
	std::wstring host = m_agentSiteUrl;
	if (const size_t scheme = host.find(L"://"); scheme != std::wstring::npos) host = host.substr(scheme + 3);
	host = host.substr(0, host.find(L'/'));
	const std::wstring baseName = host.empty() ? std::wstring((LPCTSTR)title) : host;
	const std::wstring stamp = std::wstring((LPCTSTR)CTime::GetCurrentTime().Format(_T("%Y%m%d_%H%M%S")));
	const fs::path dir = root / AgentSafeName(baseName + L"_" + stamp);

	ge::AgentHandoffOptions opt;
	opt.target = frame;
	std::wstring err;
	if (!ge::WriteAgentPackage(m_doc, dir.wstring(), opt, &err)) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}
	LinkPackageToWorkspace(dir);

	// 3. 안내 (작업 폴더 루트와 패키지 안 둘 다) 와 원본 DOM 요약
	const std::string guide = ge::BuildAgentGuideUtf8();
	std::error_code ec;
	fs::create_directories(root, ec);
	AgentWriteFile(root / L"AGENT_GUIDE.md", guide);
	AgentWriteFile(dir / L"AGENT_GUIDE.md", guide);
	if (!m_agentLastDom.empty())
		AgentWriteFile(dir / L"dom.json", AgentUtf8(m_agentLastDom));

	// 4. 연결: 이후 결과 자동 가져오기와 현재 상태 자동 저장이 이 패키지를 대상으로 한다
	m_agentDir = CString(dir.c_str());
	// 에이전트 위치를 프로젝트 목차에도 남긴다 (프로젝트가 저장된 문서라면)
	if (!GetPathName().IsEmpty() && !g_project.folder.empty())
		SaveProject(GetPathName());
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
		mf->GetOutputWnd().GetChat().SetPackageDir(m_agentDir, CString(m_agentSiteUrl.c_str()));   // ChatAgent 탭을 이 패키지에 연결
	m_agentFrameId = frame->GetId();
	m_agentResultSeen.clear();
	m_agentResultNodes.clear();
	m_agentSeenVersion = ge::ChangeTracker::Value();
	m_agentSyncedVersion = ~0ull;   // 다음 확인 때 바로 current 를 쓴다
	m_agentChangedAt = 0;

	CString log;
	log.Format(_T("[에이전트] 작업 시작: %s (결과는 result 폴더에 SVG 로 저장하면 자동으로 표시됩니다)"), (LPCTSTR)m_agentDir);
	LogAppEvent(log);
	SetAppStatusText(log);
	AgentTick();
}

// 2초마다 호출된다 (메인 창 타이머)
void CM2DrawDoc::AgentTick()
{
	if (m_agentDir.IsEmpty()) return;
	AgentCheckResults();
	AgentSyncCurrent();
}

// result 폴더의 새 SVG 를 프레임 안에 가져온다. 같은 이름이면 이전 결과를 지우고 바꾼다
void CM2DrawDoc::AgentCheckResults()
{
	namespace fs = std::filesystem;
	const fs::path resDir = fs::path((LPCTSTR)m_agentDir) / L"result";
	std::error_code ec;
	if (!fs::is_directory(resDir, ec)) return;

	for (const auto& entry : fs::directory_iterator(resDir, ec)) {
		if (!entry.is_regular_file(ec) || entry.path().extension() != L".svg") continue;
		const std::wstring name = entry.path().filename().wstring();
		const long long mt = fs::last_write_time(entry.path(), ec).time_since_epoch().count();
		auto seen = m_agentResultSeen.find(name);
		if (seen != m_agentResultSeen.end() && seen->second == mt) continue;
		m_agentResultSeen[name] = mt;

		// 같은 이름의 이전 결과 도형 삭제
		for (ge::NodeId id : m_agentResultNodes[name])
			if (ge::Node* old = m_doc.FindById(id)) m_doc.RemoveNode(old);
		m_agentResultNodes[name].clear();

		std::ifstream ifs(entry.path(), std::ios::binary);
		const std::string svg((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		ge::Node* frame = m_agentFrameId ? m_doc.FindById(m_agentFrameId) : nullptr;
		m_doc.SetSelection(frame ? std::vector<ge::Node*>{ frame } : std::vector<ge::Node*>{});
		if (svg.empty() || !ImportSvgNodes(svg, false)) {
			CString log;
			log.Format(_T("[에이전트] 결과를 가져오지 못함: %s"), name.c_str());
			LogAppEvent(log);
			continue;
		}
		for (ge::Node* n : m_doc.GetSelection())
			if (n) m_agentResultNodes[name].push_back(n->GetId());

		CString log;
		log.Format(_T("[에이전트] 결과 가져옴: %s (도형 %d개)"), name.c_str(), static_cast<int>(m_agentResultNodes[name].size()));
		LogAppEvent(log);
	}
	// 에이전트 결과를 가져온 변경은 사용자 변경이 아니므로 변경됨 배지에서 뺀다
	m_agentQuietVersion = ge::ChangeTracker::Value();
}

// 문서가 바뀌고 2초 동안 가만히 있으면 current/scene.json 과 current/preview.png 를 쓴다
void CM2DrawDoc::AgentSyncCurrent()
{
	namespace fs = std::filesystem;
	const unsigned long long v = ge::ChangeTracker::Value();
	const DWORD now = ::GetTickCount();
	if (v != m_agentSeenVersion) {
		m_agentSeenVersion = v;
		m_agentChangedAt = now;
		return;
	}
	if (v == m_agentSyncedVersion || now - m_agentChangedAt < 2000) return;

	// 처음 동기화(패키지 시작 직후)는 사용자 변경이 아니므로 배지를 켜지 않는다
	const bool firstSync = (m_agentSyncedVersion == ~0ull);

	const fs::path cur = fs::path((LPCTSTR)m_agentDir) / L"current";
	std::error_code ec;
	fs::create_directories(cur, ec);
	AgentWriteFile(cur / L"scene.json", ge::SerializeSceneJson(m_doc));

	ge::Rect2 region;
	const ge::Rect2* regionPtr = nullptr;
	if (ge::Node* frame = m_agentFrameId ? m_doc.FindById(m_agentFrameId) : nullptr)
		if (ge::Document::GetWorldBounds(*frame, region)) regionPtr = &region;
	ge::RasterExportOptions ro;
	std::wstring rerr;
	ge::ExportRaster(m_doc, (cur / L"preview.png").wstring(), ro, regionPtr, &rerr);

	m_agentSyncedVersion = v;

	// 사용자가 고친 내용이 current 에 저장됐음을 채팅 탭에 알린다
	if (!firstSync && v != m_agentQuietVersion) {
		if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd()))
			mf->GetOutputWnd().GetChat().SetChangedBadge(true);
	}
}
// 웹 프로젝트 묶음 (7.4.5): 폴더 하나에 index.html, styles.css, assets/ 를 나눠 저장
// 여러 파일로 내보내는 경우 (아이콘, 로고, 웹 묶음): 예 = {프로젝트명}_{종류} 폴더를 만들어 저장, 아니요 = 고른 폴더에 바로 저장
bool CM2DrawDoc::ChooseExportFolder(const CString& kind, CString& folderOut)
{
	const int answer = AfxMessageBox(_T("폴더로 저장하시겠습니까?\n\n예: {프로젝트명}_") + kind + _T(" 폴더를 만들어 그 안에 저장합니다.\n아니요: 다음에 고르는 폴더에 바로 저장합니다."), MB_YESNOCANCEL | MB_ICONQUESTION);
	if (answer == IDCANCEL)
		return false;

	if (answer == IDYES)
	{
		// 프로젝트 이름: 문서 이름에서 확장자와 저장 안 됨 표시(*)를 뺀 것
		CString name = GetTitle();
		const int dot = name.ReverseFind(_T('.'));
		if (dot > 0) name = name.Left(dot);
		name.Replace(_T(" *"), _T(""));
		name.Trim();

		// 기준 폴더: 프로젝트가 저장된 폴더 (아직 저장 전이면 고르기)
		CString base;
		const CString pathName = GetPathName();
		if (!pathName.IsEmpty())
		{
			const int slash = (std::max)(pathName.ReverseFind(_T('\\')), pathName.ReverseFind(_T('/')));
			base = pathName.Left(slash);
		}
		else
		{
			CFolderPickerDialog pick(nullptr, OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR, AfxGetMainWnd());
			if (pick.DoModal() != IDOK)
				return false;
			base = pick.GetPathName();
		}

		folderOut = base + _T("\\") + name + _T("_") + kind;
		std::error_code ec;
		std::filesystem::create_directories((LPCTSTR)folderOut, ec);
		return true;
	}

	// 아니요: 사용자가 고른 폴더에 바로 저장
	CFolderPickerDialog pick(nullptr, OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR, AfxGetMainWnd());
	if (pick.DoModal() != IDOK)
		return false;
	folderOut = pick.GetPathName();
	return true;
}
void CM2DrawDoc::OnFileExportWebBundle()
{
	CString folder;
	if (!ChooseExportFolder(_T("웹"), folder)) return;

	// 웹폰트 링크 입력 창: 확인하면 문서에 저장하고, 취소하면 저장된 값을 그대로 쓴다 (7.1.4)
	CWebFontDlg fontDlg(CString(m_doc.webFontHead.c_str()), AfxGetMainWnd());
	if (fontDlg.DoModal() == IDOK) {
		m_doc.webFontHead = std::wstring((LPCTSTR)fontDlg.m_text);
		SetModifiedFlag(TRUE);
	}

	const std::filesystem::path dir((LPCTSTR)folder);
	ge::HtmlExportOptions opt;
	opt.headExtra = WideToUtf8Text(m_doc.webFontHead);
	opt.singleFile = false;            // index.html + styles.css + assets/
	opt.extractCssClasses = true;
	CString t = GetTitle();
	opt.title = (const char*)CStringA(t);

	ge::HtmlExporter exporter;
	if (!exporter.ExportToFile(m_doc, (dir / L"index.html").wstring(), opt)) {
		AfxMessageBox(_T("웹 프로젝트 파일을 쓸 수 없습니다."), MB_ICONERROR);
		return;
	}
	CString log;
	log.Format(_T("[웹] 웹 프로젝트 묶음 저장: %s (index.html, styles.css, assets/)"), (LPCTSTR)folder);
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 로고 색 변형 내보내기 (7.4.3): 문서 전체를 원본/검정/흰색 단색 SVG + PNG 로 저장
void CM2DrawDoc::OnFileExportLogo()
{
	CString folder;
	if (!ChooseExportFolder(_T("로고"), folder)) return;

	std::wstring err;
	if (!ge::ExportLogoVariants(m_doc, (LPCTSTR)folder, &err)) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}

	CString log;
	log.Format(_T("[로고] 로고 변형 저장: %s"), (LPCTSTR)folder);
	LogAppEvent(log);
	SetAppStatusText(log);
}

// 아이콘 세트 내보내기 (7.4.1 / 7.4.2): 선택한 도형(없으면 문서 전체)을 정사각형으로 잘라 여러 크기로 저장
void CM2DrawDoc::OnFileExportIconSet()
{
	CString folder;
	if (!ChooseExportFolder(_T("아이콘"), folder)) return;

	std::wstring err;
	if (!ge::ExportIconSet(m_doc, (LPCTSTR)folder, GetPrimarySelection(), &err)) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}

	CString log;
	log.Format(_T("[아이콘] 아이콘 세트 저장: %s"), (LPCTSTR)folder);
	LogAppEvent(log);
	SetAppStatusText(log);
}
void CM2DrawDoc::OnEditCopy() { CopySelectionToClipboard(); }

void CM2DrawDoc::OnEditCut()
{
	if (!CopySelectionToClipboard()) return;
	PerformSmartDelete();
	CommitUndoPoint(L"잘라내기");
	UpdateAllViews(nullptr);
}

void CM2DrawDoc::OnEditPaste() { PasteFromClipboard(); }

void CM2DrawDoc::OnUpdateEditCopy(CCmdUI* pCmdUI) { pCmdUI->Enable(HasSelectedObjects()); }

void CM2DrawDoc::OnUpdateEditPaste(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(::IsClipboardFormatAvailable(SvgClipboardFormat()) || ::IsClipboardFormatAvailable(CF_UNICODETEXT) || ::IsClipboardFormatAvailable(CF_BITMAP));
}

// ============================================================================
// PNG/JPG 내보내기 (Step 5.5)
// ============================================================================
namespace {
class CExportImageDlg : public CDialog
{
public:
	int   m_format = 0;          // 0 = PNG, 1 = JPG
	int   m_scaleIdx = 1;        // 0:1x 1:2x 2:4x 3:8x
	int   m_dpi = 96;
	bool  m_transparent = true;
	bool  m_selectionOnly = false;
	bool  m_hasSelection = false;
	CString m_sizeText;

	INT_PTR Run(CWnd* parent)
	{
		std::vector<WORD> w;
		auto add32 = [&](DWORD v) { w.push_back(LOWORD(v)); w.push_back(HIWORD(v)); };
		auto addStr = [&](const wchar_t* s) { while (*s) w.push_back(*s++); w.push_back(0); };
		auto align = [&]() { if (w.size() % 2) w.push_back(0); };
		add32(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
		add32(0);
		w.push_back(11);   // 항목 수: 정적 4 + 콤보 2 + 편집 1 + 체크 2 + 크기 문구 1(위 합산 포함) + 버튼 2 = 11
		w.push_back(0); w.push_back(0); w.push_back(250); w.push_back(152);
		w.push_back(0); w.push_back(0);
		addStr(L"이미지 내보내기 (PNG / JPG)");
		w.push_back(9); addStr(L"Segoe UI");
		auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const wchar_t* title) {
			align();
			add32(style | WS_CHILD | WS_VISIBLE);
			add32(0);
			w.push_back(x); w.push_back(y); w.push_back(cx); w.push_back(cy);
			w.push_back(id);
			w.push_back(0xFFFF); w.push_back(atom);
			addStr(title);
			w.push_back(0);
		};
		item(SS_LEFT, 8, 10, 40, 10, 0xFFFF, 0x0082, L"형식");
		item(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 52, 8, 100, 60, 1001, 0x0085, L"");
		item(SS_LEFT, 8, 30, 40, 10, 0xFFFF, 0x0082, L"배율");
		item(CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 52, 28, 100, 70, 1002, 0x0085, L"");
		item(SS_LEFT, 8, 50, 40, 10, 0xFFFF, 0x0082, L"해상도(DPI)");
		item(ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 52, 48, 50, 12, 1003, 0x0081, L"96");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 8, 68, 234, 12, 1004, 0x0080, L"투명 배경 (PNG 만 해당, JPG 는 문서 배경색)");
		item(BS_AUTOCHECKBOX | WS_TABSTOP, 8, 84, 234, 12, 1005, 0x0080, L"선택한 오브젝트 영역만 내보내기");
		item(SS_LEFT, 8, 102, 234, 10, 1006, 0x0082, L"");
		item(BS_DEFPUSHBUTTON | WS_TABSTOP, 136, 130, 50, 14, IDOK, 0x0080, L"내보내기");
		item(BS_PUSHBUTTON | WS_TABSTOP, 192, 130, 50, 14, IDCANCEL, 0x0080, L"취소");
		if (!InitModalIndirect(reinterpret_cast<LPCDLGTEMPLATE>(w.data()), parent)) return IDCANCEL;
		return DoModal();
	}
	float Scale() const { static const float k[] = { 1.0f, 2.0f, 4.0f, 8.0f }; return k[(std::max)(0, (std::min)(3, m_scaleIdx))]; }

protected:
	BOOL OnInitDialog() override
	{
		CDialog::OnInitDialog();
		CComboBox* f = static_cast<CComboBox*>(GetDlgItem(1001));
		f->AddString(_T("PNG (무손실, 투명 배경 가능)"));
		f->AddString(_T("JPG (불투명)"));
		f->SetCurSel(m_format);
		CComboBox* s = static_cast<CComboBox*>(GetDlgItem(1002));
		s->AddString(_T("1x"));
		s->AddString(_T("2x"));
		s->AddString(_T("4x"));
		s->AddString(_T("8x"));
		s->SetCurSel(m_scaleIdx);
		SetDlgItemInt(1003, m_dpi, FALSE);
		CheckDlgButton(1004, m_transparent ? BST_CHECKED : BST_UNCHECKED);
		CheckDlgButton(1005, (m_selectionOnly && m_hasSelection) ? BST_CHECKED : BST_UNCHECKED);
		GetDlgItem(1005)->EnableWindow(m_hasSelection);
		SetDlgItemText(1006, m_sizeText);
		return TRUE;
	}
	void OnOK() override
	{
		m_format = static_cast<CComboBox*>(GetDlgItem(1001))->GetCurSel();
		m_scaleIdx = static_cast<CComboBox*>(GetDlgItem(1002))->GetCurSel();
		BOOL ok = FALSE;
		const UINT d = GetDlgItemInt(1003, &ok, FALSE);
		m_dpi = ok ? static_cast<int>((std::max)(1u, (std::min)(d, 2400u))) : 96;
		m_transparent = IsDlgButtonChecked(1004) == BST_CHECKED;
		m_selectionOnly = m_hasSelection && IsDlgButtonChecked(1005) == BST_CHECKED;
		CDialog::OnOK();
	}
};
} // namespace

void CM2DrawDoc::OnFileExportImage()
{
	CExportImageDlg dlg;
	dlg.m_format = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Format"), 0);
	dlg.m_scaleIdx = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Scale"), 1);
	dlg.m_dpi = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Dpi"), 96);
	dlg.m_transparent = AfxGetApp()->GetProfileInt(_T("ExportImage"), _T("Transparent"), 1) != 0;
	dlg.m_hasSelection = HasSelectedObjects();
	dlg.m_sizeText.Format(_T("문서 크기 %d x %d px  (출력 = 크기 x 배율)"), static_cast<int>(m_doc.width), static_cast<int>(m_doc.height));
	if (dlg.Run(AfxGetMainWnd()) != IDOK) return;
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Format"), dlg.m_format);
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Scale"), dlg.m_scaleIdx);
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Dpi"), dlg.m_dpi);
	AfxGetApp()->WriteProfileInt(_T("ExportImage"), _T("Transparent"), dlg.m_transparent ? 1 : 0);

	ge::RasterExportOptions opt;
	opt.jpeg = dlg.m_format == 1;
	opt.scale = dlg.Scale();
	opt.dpi = static_cast<float>(dlg.m_dpi);
	opt.transparent = dlg.m_transparent;

	ge::Rect2 region{};
	const ge::Rect2* pRegion = nullptr;
	if (dlg.m_selectionOnly && m_doc.GetSelectionBounds(region)) pRegion = &region;

	unsigned pw = 0, ph = 0;
	if (!ge::RasterExportPixelSize(m_doc, opt, pRegion, pw, ph))
	{
		AfxMessageBox(_T("출력 크기가 너무 큽니다 (한 변 최대 16384px). 배율을 낮춰 주세요."));
		return;
	}

	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);
	const BOOL jpeg = opt.jpeg;
	CFileDialog fd(FALSE, jpeg ? _T("jpg") : _T("png"), suggested + (jpeg ? _T(".jpg") : _T(".png")), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
		jpeg ? _T("JPG 이미지 (*.jpg)|*.jpg||") : _T("PNG 이미지 (*.png)|*.png||"));
	if (fd.DoModal() != IDOK) return;

	CWaitCursor wait;
	std::wstring err;
	if (!ge::ExportRaster(m_doc, (LPCTSTR)fd.GetPathName(), opt, pRegion, &err))
	{
		CString msg;
		msg.Format(_T("이미지를 저장하지 못했습니다.\n%s"), err.c_str());
		AfxMessageBox(msg);
		return;
	}
	CString log;
	log.Format(_T("[내보내기] %s 저장: %s (%u x %u px, %.0fx, %d DPI)"), jpeg ? _T("JPG") : _T("PNG"), (LPCTSTR)fd.GetPathName(), pw, ph, static_cast<double>(opt.scale), dlg.m_dpi);
	LogAppEvent(log);
	SetAppStatusText(log);
}

ge::FrameNode* CM2DrawDoc::GetSelectedTopArtboard()
{
	auto* sel = GetPrimarySelection();
	for (ge::Node* p = sel; p; p = p->GetParent()) {
		if (auto* fr = dynamic_cast<ge::FrameNode*>(p)) {
			if (fr->GetParent() && fr->GetParent()->Kind() == ge::NodeKind::Layer && !fr->IsUiComponent()) return fr;
		}
	}
	return nullptr;
}

std::string CM2DrawDoc::BuildPreviewHtml()
{
	ge::HtmlExportOptions opt;
	opt.singleFile = true;
	opt.extractCssClasses = true;
	CString t = GetTitle();
	opt.title = (const char*)CStringA(t);
	opt.targetArtboard = GetSelectedTopArtboard();
	ge::HtmlExporter exporter;
	return exporter.Export(m_doc, opt).html;
}

void CM2DrawDoc::OnFileExportHtml()
{
	CString suggested = GetTitle();
	const int dot = suggested.ReverseFind(_T('.'));
	if (dot > 0) suggested = suggested.Left(dot);

	CFileDialog dlg(FALSE, _T("html"), suggested + _T(".html"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, _T("HTML 파일 (*.html)|*.html||"));
	if (dlg.DoModal() != IDOK) return;

	ge::HtmlExportOptions opt;
	opt.singleFile = true; // 단일 파일 기본 (인라인 CSS & 이미지)
	opt.extractCssClasses = true;
	opt.title = (const char*)CStringA(suggested);

	// 선택된 최상위 아트보드가 있다면 그것만 내보내기, 없으면 문서 전체
	auto* sel = GetPrimarySelection();
	if (sel) {
		for (ge::Node* p = sel; p; p = p->GetParent()) {
			if (auto* fr = dynamic_cast<ge::FrameNode*>(p)) {
				if (fr->GetParent() && fr->GetParent()->Kind() == ge::NodeKind::Layer) {
					opt.targetArtboard = fr;
					break;
				}
			}
		}
	}

	ge::HtmlExporter exporter;
	if (!exporter.ExportToFile(m_doc, (LPCTSTR)dlg.GetPathName(), opt))
	{
		AfxMessageBox(_T("HTML 파일을 저장하지 못했습니다."));
		return;
	}

	CString log;
	log.Format(_T("[내보내기] 시맨틱 HTML/CSS 저장: %s%s"), (LPCTSTR)dlg.GetPathName(),
		opt.targetArtboard ? _T(" (선택된 아트보드)") : _T(" (전체)"));
	LogAppEvent(log);
	SetAppStatusText(log);
}

void CM2DrawDoc::OnFilePlaceImage()
{
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, kImageFilter);
	if (dlg.DoModal() != IDOK) return;
	PlaceImageFile(dlg.GetPathName());
}

// 레퍼런스 이미지 가져오기 (7.1.1): 잠긴 반투명 레이어에 놓는다
void CM2DrawDoc::OnFilePlaceReference()
{
	// 프로젝트가 열려 있으면 references 폴더를 기본으로 연다 (다른 폴더의 그림도 고를 수 있다)
	const CString initial = g_project.folder.empty() ? CString() : CString(std::filesystem::path(std::filesystem::path(g_project.folder) / L"references").wstring().c_str());
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR, kImageFilter);
	if (!initial.IsEmpty()) dlg.m_ofn.lpstrInitialDir = initial;
	if (dlg.DoModal() != IDOK) return;

	// PNG 는 DOM 요약(같은 이름의 .json)까지 함께 불러온다
	const CString ext = dlg.GetFileName().Right(4);
	if (ext.CompareNoCase(_T(".png")) == 0) {
		ImportReferenceFile(std::wstring((LPCTSTR)dlg.GetPathName()));
		return;
	}
	auto data = LoadImageFile(dlg.GetPathName());
	if (!data) return;
	PlaceReferenceData(std::move(data), dlg.GetFileTitle());
}

// 레퍼런스 레이어에 이미지를 놓고 선택/Undo/패널 갱신까지 처리 (파일 가져오기와 붙여넣기 공용)
bool CM2DrawDoc::PlaceReferenceData(std::shared_ptr<ge::ImageData> data, const CString& title, bool fitWidth)
{
	// 프로젝트가 열려 있고 이 문서가 아직 저장되지 않았으면 그 프로젝트의 페이지가 된다 (묻지 않는다)
	if (GetPathName().IsEmpty() && !g_project.folder.empty())
		AttachToProject();

	ge::Node* added = ge::AddReferenceImage(m_doc, std::move(data), (LPCTSTR)title, 1.0f, fitWidth);
	if (!added) return false;
	m_doc.SetSelection({ added });
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"레퍼런스 가져오기");
	// 프로젝트 페이지이면 레퍼런스를 references 폴더에 바로 저장한다 (이름이 겹치면 _2, _3 ...)
	if (m_attached && !g_project.manifest.empty())
		SaveProject(CString(g_project.manifest.c_str()));
	LogAppEvent(_T("[레퍼런스] 레퍼런스 이미지 가져오기 완료"));
	UpdateShapePropertiesSelection(added, this);
	UpdateAllViews(nullptr);
	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame)))
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
	return true;
}

// 클립보드 비트맵(스크린샷 등)을 레퍼런스로 붙여넣기
bool CM2DrawDoc::PasteBitmapAsReference()
{
	auto data = ReadClipboardBitmapAsPng(AfxGetMainWnd());
	if (!data) return false;
	return PlaceReferenceData(std::move(data), _T("클립보드 이미지"));
}

// AI 에이전트 패키지 내보내기 (7.3.1): 선택 도형이 속한 가장 안쪽 프레임(없으면 문서 전체)을 폴더에 저장
void CM2DrawDoc::OnFileExportAgent()
{
	// 폴더 이름을 직접 입력한다 (없으면 만든다). 폴더 선택 창은 기존 폴더만 받기 때문
	CFileDialog dlg(FALSE, nullptr, _T("AI_package"), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
		_T("폴더 이름 입력 (*.*)|*.*||"), AfxGetMainWnd());
	dlg.m_ofn.lpstrTitle = _T("AI 에이전트 패키지 폴더 이름 입력");
	if (dlg.DoModal() != IDOK) return;

	ge::AgentHandoffOptions opt;
	for (ge::Node* p = GetPrimarySelection(); p; p = p->GetParent()) {
		if (dynamic_cast<ge::FrameNode*>(p)) { opt.target = p; break; }
	}

	// 역할(role)이 없는 프레임은 에이전트가 구조를 해석하기 어려우므로 저장 전에 알린다 (7.3.4)
	const std::vector<std::wstring> noRole = ge::FindFramesWithoutRole(m_doc, opt.target);
	if (!noRole.empty()) {
		std::wstring msg = L"역할(role)이 없는 프레임이 " + std::to_wstring(noRole.size()) + L"개 있습니다:\n";
		for (size_t i = 0; i < noRole.size() && i < 5; ++i) msg += L"  - " + noRole[i] + L"\n";
		msg += L"\n그래도 저장할까요?";
		if (AfxMessageBox(msg.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
	}

	std::wstring err;
	if (!ge::WriteAgentPackage(m_doc, (LPCTSTR)dlg.GetPathName(), opt, &err)) {
		AfxMessageBox(err.c_str(), MB_ICONERROR);
		return;
	}

	CString log;
	log.Format(_T("[에이전트] 패키지 저장: %s (%s)"), (LPCTSTR)dlg.GetPathName(),
		opt.target ? _T("선택한 프레임") : _T("문서 전체"));
	LogAppEvent(log);
	SetAppStatusText(log);
}
// 선택된 이미지의 데이터만 교체: 위치/폭/맞춤은 유지하고 높이는 새 이미지 비율로 맞춤
bool CM2DrawDoc::ReplaceSelectedImage()
{
	auto* im = dynamic_cast<ge::ImageNode*>(GetPrimarySelection());
	if (!im) return false;
	CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, kImageFilter);
	if (dlg.DoModal() != IDOK) return false;
	auto data = LoadImageFile(dlg.GetPathName());
	if (!data) return false;
	im->SetData(data);
	if (data->pixelW > 0) im->SetSize(im->GetWidth(), im->GetWidth() * static_cast<float>(data->pixelH) / static_cast<float>(data->pixelW));
	SetModifiedFlag(TRUE);
	CommitUndoPoint(L"이미지 교체");
	LogAppEvent(_T("[이미지] 이미지 교체 완료"));
	UpdateShapePropertiesSelection(im, this);
	UpdateAllViews(nullptr);
	return true;
}
void CM2DrawDoc::OnUpdateTextToPath(CCmdUI* pCmdUI) { pCmdUI->Enable(CanConvertTextToPath()); }

bool CM2DrawDoc::SmoothPath(float tolerance)
{
	if (!CanSimplify()) return false;

	auto results = m_doc.SmoothSelection(tolerance);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"패스 부드럽게");
	UpdateAllViews(nullptr);
	return true;
}

bool CM2DrawDoc::SimplifyPath(float tolerance)
{
	if (!CanSimplify()) return false;

	auto results = m_doc.SimplifySelection(tolerance);
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"패스 단순화");
	UpdateAllViews(nullptr);
	return true;
}

bool CM2DrawDoc::ReversePath()
{
	if (!CanReversePath()) return false;

	auto results = m_doc.ReversePathSelection();
	if (results.empty()) return false;

	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	CommitUndoPoint(L"패스 방향 반전");
	UpdateAllViews(nullptr);
	return true;
}

bool CM2DrawDoc::JoinPaths()
{
	if (!CanJoinPaths()) return false;

	ge::PathNode* joined = m_doc.JoinSelection();
	if (!joined) return false;

	UpdateShapePropertiesSelection(joined, this);
	CommitUndoPoint(L"패스 결합 (Join)");
	UpdateAllViews(nullptr);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd && pMainWnd->IsKindOf(RUNTIME_CLASS(CMainFrame))) {
		static_cast<CMainFrame*>(pMainWnd)->GetLayerWnd().RefreshLayers();
		static_cast<CMainFrame*>(pMainWnd)->UpdateSubToolBar(GetApp()->GetToolManager().GetActiveType());
	}
	return true;
}

void CM2DrawDoc::OnAlignCommand(UINT nID)
{
	const bool distribute = (nID == ID_DISTRIBUTE_H || nID == ID_DISTRIBUTE_V);
	if (m_doc.GetSelection().size() < (distribute ? 3u : 2u)) return;

	const wchar_t* label = L"정렬";
	switch (nID)
	{
	case ID_ALIGN_LEFT:    m_doc.AlignSelection(ge::Document::AlignMode::Left, false);    label = L"왼쪽 정렬"; break;
	case ID_ALIGN_CENTERH: m_doc.AlignSelection(ge::Document::AlignMode::CenterH, false); label = L"가로 가운데 정렬"; break;
	case ID_ALIGN_RIGHT:   m_doc.AlignSelection(ge::Document::AlignMode::Right, false);   label = L"오른쪽 정렬"; break;
	case ID_ALIGN_TOP:     m_doc.AlignSelection(ge::Document::AlignMode::Top, false);     label = L"위쪽 정렬"; break;
	case ID_ALIGN_CENTERV: m_doc.AlignSelection(ge::Document::AlignMode::CenterV, false); label = L"세로 가운데 정렬"; break;
	case ID_ALIGN_BOTTOM:  m_doc.AlignSelection(ge::Document::AlignMode::Bottom, false);  label = L"아래쪽 정렬"; break;
	case ID_DISTRIBUTE_H:  m_doc.DistributeSelection(true);  label = L"가로 균등 분배"; break;
	case ID_DISTRIBUTE_V:  m_doc.DistributeSelection(false); label = L"세로 균등 분배"; break;
	}

	CString strLog;
	strLog.Format(_T("[정렬] %s (선택 %d개)"), label, static_cast<int>(m_doc.GetSelection().size()));
	LogAppEvent(strLog);
	CommitUndoPoint(label);   // 위치가 실제로 바뀌지 않았으면 기록되지 않음
	UpdateShapePropertiesSelection(GetPrimarySelection(), this);
	UpdateAllViews(nullptr);
}

void CM2DrawDoc::OnUpdateAlignCommand(CCmdUI* pCmdUI)
{
	const bool distribute = (pCmdUI->m_nID == ID_DISTRIBUTE_H || pCmdUI->m_nID == ID_DISTRIBUTE_V);
	pCmdUI->Enable(m_doc.GetSelection().size() >= (distribute ? 3u : 2u));
}

void CM2DrawDoc::OnPathOutline()
{
	OutlineStroke();
}

void CM2DrawDoc::OnUpdatePathOutline(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanOutlineStroke());
}

void CM2DrawDoc::OnPathOffset()
{
	OffsetPath(10.0f);
}

void CM2DrawDoc::OnUpdatePathOffset(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanOffsetPath());
}

void CM2DrawDoc::OnPathSimplify()
{
	SimplifyPath(2.0f);
}

void CM2DrawDoc::OnUpdatePathSimplify(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanSimplify());
}

void CM2DrawDoc::OnPathReverse()
{
	ReversePath();
}

void CM2DrawDoc::OnUpdatePathReverse(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanReversePath());
}

void CM2DrawDoc::OnPathJoin()
{
	JoinPaths();
}

void CM2DrawDoc::OnUpdatePathJoin(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(CanJoinPaths());
}




// 프로젝트 트리 패널용 읽기 전용 도우미 (8.8). 이름 없는 네임스페이스 밖에 두어 ProjectTreeWnd.cpp 에서 쓴다
// 저장된 프로젝트가 있는가 (없으면 트리는 저장 전 DefaultProject 를 보여 준다)
bool ProjectTreeSaved() { return !g_project.folder.empty(); }
std::wstring ProjectTreeName()
{
	if (g_project.folder.empty())
		return UntitledDocs().empty() ? std::wstring() : std::wstring(L"DefaultProject (저장 안 됨)");
	std::filesystem::path p(g_project.folder);
	if (p.filename().empty()) p = p.parent_path();   // 끝에 \ 가 붙은 경우
	return p.filename().wstring();
}
std::vector<ProjectPageEntry> ProjectTreePages()
{
	if (!g_project.folder.empty()) return g_project.pages;
	std::vector<ProjectPageEntry> pages;   // 저장 전: 열려 있는 이름 없는 문서가 페이지
	for (CM2DrawDoc* d : UntitledDocs()) pages.push_back(ProjectPageEntry{ UntitledPageBase(d), L"" });
	return pages;
}
std::vector<ProjectRefEntry> ProjectTreeRefs() { return g_project.refs; }
// 트리를 다시 그릴지 정하는 값: 프로젝트 폴더, 페이지 이름, 레퍼런스 이름이 바뀌면 달라진다
std::wstring ProjectTreeSignature()
{
	std::wstring sig = g_project.folder + L"|";
	for (const auto& pg : ProjectTreePages()) sig += pg.name + L";";
	sig += L"|";
	for (const auto& r : g_project.refs) sig += r.leaf + L";";
	return sig;
}

// 파일 > 프로젝트 닫기: 열린 프로젝트의 페이지(문서)를 모두 닫는다. 저장 안 된 페이지는 CDocument 가 묻는다 (8.6)
void CM2DrawDoc::OnFileProjectClose()
{
	std::vector<CM2DrawDoc*> pages;
	POSITION tp = AfxGetApp()->GetFirstDocTemplatePosition();
	while (tp) {
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(tp);
		POSITION dp = tmpl->GetFirstDocPosition();
		while (dp) {
			if (CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(dp)))
				if (d->IsProjectDocForClose()) pages.push_back(d);
		}
	}
	// 각 페이지의 창을 정상 닫기 경로(WM_CLOSE)로 닫는다: 저장 질문과 창 제거가 함께 처리된다
	std::vector<CFrameWnd*> frames;
	for (CM2DrawDoc* d : pages) {
		POSITION vp = d->GetFirstViewPosition();
		while (vp) {
			if (CView* v = d->GetNextView(vp)) {
				if (CFrameWnd* f = v->GetParentFrame()) frames.push_back(f);
			}
		}
	}
	for (CFrameWnd* f : frames) {
		f->SendMessage(WM_CLOSE);
	}
	// 하나라도 닫히지 않았으면(저장 질문에서 취소) 프로젝트 상태는 그대로 둔다
	bool anyLeft = false;
	POSITION rtp = AfxGetApp()->GetFirstDocTemplatePosition();
	while (rtp && !anyLeft) {
		CDocTemplate* tmpl = AfxGetApp()->GetNextDocTemplate(rtp);
		POSITION rdp = tmpl->GetFirstDocPosition();
		while (rdp && !anyLeft) {
			if (CM2DrawDoc* d = DYNAMIC_DOWNCAST(CM2DrawDoc, tmpl->GetNextDoc(rdp)))
				if (d->IsProjectDocForClose()) anyLeft = true;
		}
	}
	if (!anyLeft) g_project = ProjectState();
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) mf->RefreshProjectTree();
}

// 프로젝트 닫기에서 쓰는 판정: 프로젝트 페이지로 연결된 문서 (첫 페이지는 목차 경로로, 나머지는 플래그로 식별된다)
bool CM2DrawDoc::IsProjectDocForClose() const
{
	if (m_attached) return true;
	return !g_project.manifest.empty() && GetPathName() == CString(g_project.manifest.c_str());
}

// 파일 > 페이지 가져오기: 다른 프로젝트(또는 아무 폴더)의 .m2doc 을 현재 프로젝트의 페이지로 복사한다 (8.7)
// 페이지가 쓰는 레퍼런스 그림(../references/...)도 함께 복사해서 링크가 깨지지 않게 한다. 이름이 겹치면 _2, _3 을 붙인다
void CM2DrawDoc::OnFilePageImport()
{
	namespace fs = std::filesystem;
	if (g_project.folder.empty() || GetPathName().IsEmpty()) {
		AfxMessageBox(_T("먼저 프로젝트를 저장하세요."), MB_ICONINFORMATION);
		return;
	}
	CFileDialog dlg(TRUE, _T("m2doc"), nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
		_T("페이지 (*.m2doc)|*.m2doc||"), AfxGetMainWnd());
	dlg.m_ofn.lpstrTitle = _T("가져올 페이지 고르기");
	if (dlg.DoModal() != IDOK) return;

	const fs::path src((LPCTSTR)dlg.GetPathName());
	const fs::path srcRoot = src.parent_path().parent_path();   // 원래 프로젝트 폴더 (pages 의 위)
	const fs::path dstRoot = g_project.folder;
	std::error_code ec;
	fs::create_directories(dstRoot / L"pages", ec);
	fs::create_directories(dstRoot / L"references", ec);

	// 이름 정하기 (겹치면 _2, _3 ...)
	const std::wstring base = src.stem().wstring();
	std::wstring cand = base;
	for (int k = 2;; ++k) {
		bool used = fs::exists(dstRoot / L"pages" / (cand + L".m2doc"));
		for (const auto& pg : g_project.pages)
			if (pg.name == cand) used = true;
		if (!used) break;
		cand = base + L"_" + std::to_wstring(k);
	}

	// 페이지 파일을 복사하고, 그 안의 레퍼런스 링크(../references/이름.png)를 찾아 그림과 DOM 요약을 함께 복사한다
	std::string xml;
	if (!ProjReadAll(src, xml)) {
		AfxMessageBox(_T("페이지 파일을 읽을 수 없습니다."), MB_ICONERROR);
		return;
	}
	{
		std::ofstream ofs(dstRoot / L"pages" / (cand + L".m2doc"), std::ios::binary | std::ios::trunc);
		ofs.write(xml.data(), static_cast<std::streamsize>(xml.size()));
	}
	const std::string key = "href=\"../references/";
	size_t pos = 0;
	while ((pos = xml.find(key, pos)) != std::string::npos) {
		pos += key.size();
		const size_t end = xml.find('"', pos);
		if (end == std::string::npos) break;
		const std::string rel = xml.substr(pos, end - pos);   // 예: 001_name.png
		const fs::path stem = fs::path(rel).stem();
		for (const wchar_t* ext : { L".png", L".json" }) {
			const fs::path from = srcRoot / L"references" / (stem.wstring() + ext);
			const fs::path to = dstRoot / L"references" / (stem.wstring() + ext);
			if (fs::exists(from) && !fs::exists(to)) fs::copy_file(from, to, ec);
		}
		pos = end;
	}

	// 목차에 페이지를 추가하고 저장한 뒤, 가져온 페이지를 연다
	g_project.pages.push_back(ProjectPageEntry{ cand, L"" });
	SaveProject(CString(g_project.manifest.c_str()));
	if (auto* mf = dynamic_cast<CMainFrame*>(AfxGetMainWnd())) mf->RefreshProjectTree();
	OpenPageTab(cand);
	CString msg;
	msg.Format(_T("페이지를 가져왔습니다: %s"), (LPCTSTR)CString(cand.c_str()));
	SetAppStatusText(msg);
}
