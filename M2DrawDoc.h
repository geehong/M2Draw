#pragma once

#include <map>
#include <set>
#include <vector>
#include "D2DGraphicEngine.h"
#include "UndoManager.h"

using ShapeKind = ge::ShapeKind;
using RectParams = ge::RectParams;
using TriangleParams = ge::TriangleParams;
using EllipseParams = ge::EllipseParams;
using PolygonParams = ge::PolygonParams;
using StarParams = ge::StarParams;
using GearParams = ge::GearParams;
using SpiralParams = ge::SpiralParams;
using HeartParams = ge::HeartParams;
using ShapeParams = ge::ShapeParams;
using BezierNode = ge::BezierNode;
using SubPath = ge::SubPath;

// 프로젝트 안의 페이지 (pages\\이름.m2doc)
struct ProjectPageEntry
{
	std::wstring name;        // 페이지 이름 (파일 이름과 같다)
	std::wstring agentDir;    // 이 페이지의 에이전트 패키지 폴더 (프로젝트 기준 상대 경로 또는 절대 경로)
};

// 프로젝트 안의 레퍼런스 (references\\NNN_이름\\reference.png)
struct ProjectRefEntry
{
	int          id = 0;      // 번호 (001, 002 ...). 덮어쓰지 않고 계속 늘린다
	std::wstring leaf;        // 폴더 이름 (예: 001_webmd_202611031329)
	std::wstring url;         // 캡처한 주소
	std::string  created;     // 캡처한 날짜
	int          width = 0;
	int          height = 0;
};

class CM2DrawDoc : public CDocument
{
protected:
	CM2DrawDoc() noexcept;
	DECLARE_DYNCREATE(CM2DrawDoc)

// 특성입니다.
public:
	// --- 신형 그래픽 엔진 (D2DGraphicEngine) Scene 루트 ---
	ge::Document                    m_doc;

	ge::Document&                   GetDoc()             { return m_doc; }
	const ge::Document&             GetDoc() const       { return m_doc; }

	// 선택의 단일 출처는 ge::Document::GetSelection(). 대표(첫 번째) 선택 노드, 없으면 nullptr
	ge::Node* GetPrimarySelection() const;
	// 숨김/잠금(조상 포함) 상태가 된 노드를 선택에서 제외. 선택이 바뀌었으면 true (패널 갱신은 호출 측)
	bool PruneNonInteractiveSelection();

	// 펜 도구가 작성 중인 패스 (문서에 아직 등록되지 않은 임시 도형)
	std::unique_ptr<ge::PathNode>          m_currentPath;
	std::unique_ptr<ge::PathNode>&          GetCurrentPath() { return m_currentPath; }

	// --- 스마트 삭제 연동 기능 (키보드 Del 키 & 레이어 패널 휴지통 공용) ---
	bool HasSelectedObjects() const;
	bool DeleteSelectedObjects();
	bool DeleteActiveLayer();
	bool PerformSmartDelete();

	// --- 복제 (Ctrl+D) ---
	bool DuplicateSelectedObjects();

	// --- 그룹화 / 그룹 해제 (Ctrl+G / Ctrl+Shift+G, 오브젝트 메뉴, 속성창 버튼 공용) ---
	bool CanGroup() const   { return m_doc.CanGroupSelection(); }
	bool CanUngroup() const { return m_doc.CanUngroupSelection(); }
	bool CanWrapInFrame() const { return m_doc.CanWrapInFrame(); }
	bool GroupSelectedObjects();
	bool UngroupSelectedObjects();

	// --- 순서(Z-order) 변경 (오브젝트 메뉴 / Ctrl+[ ] / 도구상자 공용). 각 노드는 자기 부모 안에서 이동 ---
	bool CanArrange() const { return m_doc.CanArrangeSelection(); }
	bool ArrangeSelectedObjects(ge::Document::ZOrder order);

	// --- 불리언 연산 / 패스파인더 (오브젝트 메뉴 / 패스파인더 단축키) ---
	bool CanBoolean() const { return m_doc.CanBooleanOperation(); }
	bool PerformBooleanOperation(ge::BooleanMode mode);

	// --- 클리핑 마스크 (Ctrl+7 / Ctrl+Alt+7) ---
	bool CanMakeClipMask() const    { return m_doc.CanMakeClipMask(); }
	bool CanReleaseClipMask() const { return m_doc.CanReleaseClipMask(); }
	bool MakeClipMask();
	bool ReleaseClipMask();

	// --- 패스 연산 (Step 2.6: Outline, Simplify, Reverse, Join) ---
	bool CanOutlineStroke() const { return m_doc.CanOutlineStroke(); }
	bool CanOffsetPath() const { return m_doc.CanOffsetPath(); }
	bool CanSimplify() const      { return m_doc.CanSimplify(); }
	bool CanReversePath() const   { return m_doc.CanReversePath(); }
	bool CanJoinPaths() const     { return m_doc.CanJoinPaths(); }
	bool OutlineStroke(float widthOverride = -1.0f);
	bool OffsetPath(float distance = 10.0f);
	bool SmoothPath(float tolerance);
	bool ConvertTextToPath();
	afx_msg void OnFilePlaceImage();
	afx_msg void OnFilePlaceReference();   // 레퍼런스 이미지 가져오기 (잠긴 반투명 레이어, 7.1.1)
	afx_msg void OnFileExportAgent();      // AI 에이전트 패키지 내보내기 (7.3.1)
	afx_msg void OnFileImportAgentSvg();   // AI 산출물 SVG 가져오기 (7.2.3)
	afx_msg void OnFileExportIconSet();    // 아이콘 세트 내보내기 (7.4.1 / 7.4.2)
	afx_msg void OnFileExportLogo();       // 로고 색 변형 내보내기 (7.4.3)
	afx_msg void OnFileExportSvgOptimized(); // 좌표 2자리 최적화 SVG 내보내기 (7.4.4)
	afx_msg void OnFileExportWebBundle();    // 웹 프로젝트 묶음 (index.html + styles.css + assets/) (7.4.5)
	afx_msg void OnFileImportUrlReference(); // 클립보드의 웹 주소를 캡처해 레퍼런스로 가져오기 (7.1.6 / 7.1.7)
	void ImportUrlReference(std::wstring url);   // 주소를 받아 캡처 후 레퍼런스로 가져오기 (주소창/클립보드 공용)
	void LoadCaptureFile(const std::wstring& path);   // 캡처 파일(*.m2svg)을 불러와 레퍼런스로 올리기
	afx_msg void OnFileCaptureOpen();                 // 파일 > 캡처 불러오기 (*.m2svg)
	afx_msg void OnFileAddAnnotation();      // 주석(메모) 추가: 선택 도형 옆에 주석 레이어 텍스트 (7.3.3)
	afx_msg void OnFileWebFontLink();        // 웹폰트 링크 설정 (문서에 저장, 7.1.4)
	bool ImportSvgNodes(const std::string& svg, bool isPaste);   // SVG 노드를 선택 프레임 안(또는 활성 레이어)에 넣는다
	// AI 산출물 외부 수정 감지 (7.2.4): 마지막으로 가져온 SVG 를 기록해 두고, 창이 활성화될 때 수정 여부를 본다
	void RecordWatchedImport(const CString& path, ge::NodeId frameId, const std::vector<ge::Node*>& nodes);
	CString                 m_watchPath;
	long long               m_watchMtime = 0;
	std::vector<ge::NodeId> m_watchNodes;
	ge::NodeId              m_watchFrame = 0;
	// 에이전트 작업 연결 (7.2.6): 작업 폴더의 패키지와 결과 자동 가져오기, 현재 상태 자동 저장
	CString                                  m_agentDir;                 // 작업 패키지 폴더 (비어 있으면 연결 안 됨)
	ge::NodeId                               m_agentFrameId = 0;         // 결과를 넣을 프레임
	std::map<std::wstring, long long>        m_agentResultSeen;          // 결과 파일 -> 마지막 수정 시각
	std::map<std::wstring, std::vector<ge::NodeId>> m_agentResultNodes;  // 결과 파일 -> 가져온 도형
	unsigned long long                       m_agentSeenVersion = 0;     // 마지막으로 본 변경 번호
	unsigned long long                       m_agentSyncedVersion = 0;   // 마지막으로 current 에 쓴 변경 번호
	unsigned long long                       m_agentQuietVersion = 0;    // 에이전트 결과를 가져온 직후의 변경 번호 (변경됨 배지 제외용)
	unsigned long                            m_agentChangedAt = 0;       // 변경을 처음 본 시각 (ms)
	DWORD                                    m_dirtyTick = 0;            // 수정이 처음 감지된 시각 (자동 저장 기준)
	bool                                     m_attached = false;         // 열린 프로젝트의 페이지로 붙은 문서 (저장은 프로젝트 목차로)
	bool                                     m_saveAsked = false;        // 저장하지 않은 문서에서 프로젝트 저장을 이미 물었는지
	std::wstring                             m_activePage = L"메인";     // 지금 열려 있는 페이지
	std::vector<uint8_t>                     m_capturePng;               // 마지막 URL 캡처의 그림 (프로젝트 폴더의 reference.png)
	std::wstring                             m_agentLastDom;             // 마지막 URL 캡처의 DOM 요약 JSON
	std::wstring                             m_agentSiteUrl;             // 마지막 URL 캡처의 주소 (패키지 이름·지시문의 대상 사이트)
	void AgentCheckResults();
	void AgentSyncCurrent();	void CheckExternalImportChange();   // 앱 창이 활성화될 때 호출 (7.2.4)
	void AgentTick();                   // 2초마다 호출: 결과 가져오기 + 현재 상태 저장 (7.2.6)
	afx_msg void OnFileAgentStart();    // 에이전트 작업 시작 (7.2.6)
	afx_msg void OnFileAgentSendFrame(); // 선택한 프레임만 에이전트 패키지로 보내기 (7.2.11)
	bool PlaceReferenceData(std::shared_ptr<ge::ImageData> data, const CString& title, bool fitWidth = false);
	bool PasteBitmapAsReference();         // 클립보드 비트맵을 레퍼런스로 붙여넣기
	afx_msg void OnFileExportSvg();   // 순수 SVG 내보내기 (옵션: 텍스트 윤곽선 변환)
	// HTML 미리보기(Step 4.6)용: 선택된 최상위 아트보드(없으면 nullptr = 문서 전체)와 현재 문서의 HTML
	ge::FrameNode* GetSelectedTopArtboard();
	std::string BuildPreviewHtml();
	afx_msg void OnFileExportHtml();  // 시맨틱 HTML/CSS 내보내기 (Step 4.5)
	afx_msg void OnFileExportImage(); // PNG/JPG 래스터 내보내기 (Step 5.5)
	// --- 클립보드 (Step 5.4): SVG 로 교환 (CF_UNICODETEXT + image/svg+xml) ---
	bool CopySelectionToClipboard();
	bool PasteFromClipboard();
	bool SaveStructureFromClipboard(const std::wstring& html);   // 클립보드 HTML → 구조 정보(dom.json) 저장 (에이전트는 버튼으로 실행)
	afx_msg void OnEditCopy();
	afx_msg void OnEditCut();
	afx_msg void OnEditPaste();
	afx_msg void OnUpdateEditCopy(CCmdUI* pCmdUI);
	afx_msg void OnUpdateEditPaste(CCmdUI* pCmdUI);
	bool PlaceImageFile(const CString& path);          // 이미지 파일을 문서 중앙에 배치 (메뉴/드래그 앤 드롭 공용)
	bool ReplaceSelectedImage();                       // 선택된 이미지의 내용을 다른 파일로 교체 (크기/위치/맞춤 유지)
	bool CanConvertTextToPath() const { return m_doc.CanConvertTextToPath(); }
	bool SimplifyPath(float tolerance = 2.0f);
	bool ReversePath();
	bool JoinPaths();

	// --- Undo / Redo (Ctrl+Z / Ctrl+Y) ---
	ge::UndoManager& GetUndoManager() { return m_undoMgr; }
	// 편집이 끝난 지점에서 호출: 마지막 확정 상태와 달라졌으면 Undo 이력에 기록하고 수정 표시를 켬 (true = 변경됨)
	bool CommitUndoPoint(const std::wstring& actionName);
	// 문서 교체(Undo/Redo/열기/새 문서) 후 선택 도구의 진행 중 상태(노드 포인터) 폐기
	void ResetToolInteraction();

	afx_msg void OnEditUndo();
	afx_msg void OnUpdateEditUndo(CCmdUI* pCmdUI);
	afx_msg void OnEditRedo();
	afx_msg void OnUpdateEditRedo(CCmdUI* pCmdUI);
	bool WrapSelectedInFrame();
	afx_msg void OnObjectGroup();
	afx_msg void OnUpdateObjectGroup(CCmdUI* pCmdUI);
	afx_msg void OnObjectUngroup();
	afx_msg void OnUpdateObjectUngroup(CCmdUI* pCmdUI);
	afx_msg void OnObjectWrapFrame();
	afx_msg void OnUpdateObjectWrapFrame(CCmdUI* pCmdUI);
	bool MakeCompoundPath();
	bool ReleaseCompoundPath();
	afx_msg void OnObjectMakeCompound();
	afx_msg void OnUpdateObjectMakeCompound(CCmdUI* pCmdUI);
	afx_msg void OnObjectReleaseCompound();
	afx_msg void OnUpdateObjectReleaseCompound(CCmdUI* pCmdUI);
	afx_msg void OnTextToPath();
	afx_msg void OnUpdateTextToPath(CCmdUI* pCmdUI);
	// 선택에 월드 변환 적용 (결합 경계 상자 중심 기준 반전/90도 회전, 평행 이동). 잠긴 노드와 선택된 조상이 있는 노드는 제외. Undo 기록은 호출자 (Step 6.11.3)
	bool ApplySelectionTransform(int op);                 // 0 좌우반전, 1 상하반전, 2 시계 90, 3 반시계 90
	bool NudgeSelection(float dx, float dy);
	afx_msg void OnObjectTransform(UINT nID);
	afx_msg void OnUpdateObjectTransform(CCmdUI* pCmdUI);
	afx_msg void OnObjectArrange(UINT nID);
	afx_msg void OnAlignCommand(UINT nID);
	afx_msg void OnUpdateAlignCommand(CCmdUI* pCmdUI);
	afx_msg void OnUpdateObjectArrange(CCmdUI* pCmdUI);
	afx_msg void OnBooleanOperation(UINT nID);
	afx_msg void OnUpdateBooleanOperation(CCmdUI* pCmdUI);
	afx_msg void OnObjectClipMask();
	afx_msg void OnUpdateObjectClipMask(CCmdUI* pCmdUI);
	afx_msg void OnObjectReleaseMask();
	afx_msg void OnUpdateObjectReleaseMask(CCmdUI* pCmdUI);
	afx_msg void OnPathOutline();
	afx_msg void OnUpdatePathOutline(CCmdUI* pCmdUI);
	afx_msg void OnPathOffset();
	afx_msg void OnUpdatePathOffset(CCmdUI* pCmdUI);
	afx_msg void OnPathSimplify();
	afx_msg void OnUpdatePathSimplify(CCmdUI* pCmdUI);
	afx_msg void OnPathReverse();
	afx_msg void OnUpdatePathReverse(CCmdUI* pCmdUI);
	afx_msg void OnPathJoin();
	afx_msg void OnUpdatePathJoin(CCmdUI* pCmdUI);

private:
	ge::UndoManager m_undoMgr;
	int m_pasteCount = 0;   // 같은 내용을 연속 붙여넣을 때 겹치지 않게 위치를 어긋나게 하는 횟수

// 재정의입니다.
public:
	virtual BOOL OnNewDocument();
	virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
	virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
	virtual void OnCloseDocument();   // 닫히기 전에 레이어 패널이 이 문서의 노드를 놓게 한다
	// 프로젝트 (7.2.13 / 문서 형식): 목차 *.m2svg + 같은 이름의 *.m2data 폴더
	BOOL SaveProject(LPCTSTR manifestPathLpsz);
	BOOL OpenProject(LPCTSTR manifestPathLpsz);
	BOOL AllowOpenProject(LPCTSTR manifestPathLpsz);   // 다른 프로젝트가 열려 있으면 막는다
	void ReplaceDocument(ge::Document&& loaded);   // 현재 문서를 바꾸고 화면을 갱신한다
	BOOL SwitchPage(const std::wstring& name);
	void AttachToProject();
	void AutoSaveIfDirty();
	void LoadPageAttached(const std::wstring& name);   // 프로젝트의 페이지 하나를 이 문서에 연다 (탭으로 여는 페이지용)
	BOOL OpenPageTab(const std::wstring& name);        // 페이지를 자기 탭(창)으로 연다. 이미 열려 있으면 그 탭을 활성화한다
	static bool HasUntitledDocs();                      // 프로젝트가 없고 저장 안 된 문서가 열려 있는가 (저장 전 DefaultProject 상태)
	static BOOL SaveUntitledAsProject();               // 저장 안 된 문서들을 한 프로젝트로 묶어 저장한다 (이름을 한 번 묻는다)
	void AskSaveProjectOnFirstEdit();                   // 저장 안 된 문서에서 처음 작업이 시작되면 프로젝트 저장 여부를 한 번 묻는다
	static CM2DrawDoc* AddPageToProject(const std::wstring& wantedName = std::wstring());   // 프로젝트가 열려 있을 때 새로 만들기: 빈 페이지를 목차에 바로 등록하고 연다 (이름이 비면 "페이지"; 프로젝트가 없으면 nullptr)
	afx_msg void OnFilePageNew();                  // 새 페이지 (pages 폴더에 이름을 정해 만든다)
	afx_msg void OnFilePageOpen();
	afx_msg void OnFilePageRename();               // 페이지 이름 바꾸기 (파일 이름과 목차를 함께)
	afx_msg void OnFileProjectClose();
	afx_msg void OnFilePageImport();               // 페이지 가져오기: 다른 프로젝트의 페이지를 현재 프로젝트로 복사한다             // 프로젝트 닫기: 열린 프로젝트의 페이지를 모두 닫는다
	bool IsProjectDocForClose() const;   // 프로젝트에 속한 페이지인지 (플래그 또는 프로젝트 목차 경로)
	void RenamePage(const std::wstring& newName);
	void CleanupUnusedReferences(const std::wstring& folderPath);   // 아무도 쓰지 않는 레퍼런스 파일 정리
	std::set<std::string> UsedReferenceNames(const std::wstring& folderPath);   // 쓰이고 있는 레퍼런스 이름 (UTF-8, 확장자 없음)
	BOOL RenameProjectPage(const std::wstring& oldName, const std::wstring& newName);   // 프로젝트 트리에서 페이지 이름 바꾸기 (열린 페이지도, 닫힌 페이지도)
	BOOL RemoveProjectPage(const std::wstring& name);   // 프로젝트 목차에서 페이지를 뺀다 (페이지 파일은 남긴다)
	void ImportReferenceFile(const std::wstring& pngPath);              // 레퍼런스 그림 파일을 레퍼런스로 올리기
	CString GetAgentDirForChat() const;                                 // 탭별 채팅 연결용: 이 문서의 에이전트 패키지 폴더
	CString GetSiteForChat() const;                                     // 탭별 채팅 연결용: 이 문서의 대상 사이트                 // 페이지 열기 (pages 폴더에서 고른다)
	bool ChooseExportFolder(const CString& kind, CString& folderOut);   // 여러 파일 내보내기: 폴더로 저장할지 묻고 폴더를 정한다
	std::wstring AgentRootForDoc() const;   // 에이전트 패키지를 둘 폴더 (프로젝트 안이면 <프로젝트>.m2data\agent)
	virtual BOOL DoSave(LPCTSTR lpszPathName, BOOL bReplace = TRUE);
	virtual void SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU = TRUE);
	void SetModifiedFlag(BOOL bModified = TRUE);   // CDocument::SetModifiedFlag 는 가상 함수가 아니므로 숨김(hiding) 임에 주의
	void UpdateTitleWithModified();
	virtual void Serialize(CArchive& ar);
#ifdef SHARED_HANDLERS
	virtual void InitializeSearchContent();
	virtual void OnDrawThumbnail(CDC& dc, LPRECT lprcBounds);
#endif

// 구현입니다.
public:
	virtual ~CM2DrawDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	DECLARE_MESSAGE_MAP()

#ifdef SHARED_HANDLERS
	void SetSearchContent(const CString& value);
#endif
};
