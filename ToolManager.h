#pragma once
#include <d2d1.h>
#include <vector>
#include <memory>
#include <set>
#include <wrl/client.h>
#include "ITool.h"

using Microsoft::WRL::ComPtr;

class CM2DrawDoc;



// PanTool, ZoomInTool, ZoomOutTool은 ITool.h에 선언되어 있습니다.

class PenTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Pen; }
	const wchar_t* GetName() const override { return L"펜 / 베이지어 도구 (Pen)"; }
	wchar_t GetShortcut() const override { return L'P'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }

	void OnDown(const ToolEvent& e) override;
	void OnMove(const ToolEvent& e) override;
	void OnUp(const ToolEvent& e) override;
	void OnKeyDown(UINT nChar, CM2DrawDoc* pDoc) override;
	void DrawOverlay(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float zoom, D2D1_POINT_2F pan, CM2DrawDoc* pDoc) override;

	// 현재 베이지어 패스 작성 중인지 여부
	bool HasActiveShape() const override;

	// 더블클릭 및 우클릭 시: 현재까지의 베이지어 노드로 패스를 완성하고 Select 툴로 전환
	bool OnDoubleClicked(CM2DrawDoc* pDoc) override;
	bool OnRightClicked(CM2DrawDoc* pDoc) override;

private:
	enum class State {
		Idle,
		Placing,
		Pressed,
		DraggingHandle
	};

	State         m_state = State::Idle;
	bool          m_needNewSubPath = true;
	D2D1_POINT_2F m_downScreenPt{};
	D2D1_POINT_2F m_hoverWorld{};
	bool          m_hasHover = false;

protected:
	bool          m_cornerOnly = false;   // true: 클릭만으로 각진 꼭짓점 추가 (드래그 핸들/첫 점 클릭 폐합 없음) -> 폴리라인
};

// 열린 다각형(폴리라인) 도구: 펜 도구의 각진 꼭짓점 전용 변형
class PolylineTool : public PenTool
{
public:
	PolylineTool() { m_cornerOnly = true; }
	ToolType GetType() const override { return ToolType::Polyline; }
	const wchar_t* GetName() const override { return L"열린 다각형 도구 (Polyline)"; }
	wchar_t GetShortcut() const override { return 0; }
};

// LineTool, RectTool, EllipseTool은 BoxShapeTool 인스턴스,
// PolygonTool은 MultiPointShapeTool 인스턴스로 일원화 처리됩니다.

// 텍스트 입력 대화상자 (한글 IME 는 표준 EDIT 컨트롤이 처리). 확인하면 true, text 에 \n 구분 문자열을 돌려줌
bool EditTextDialog(CWnd* parent, std::wstring& text);

class TextTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::Text; }
	const wchar_t* GetName() const override { return L"텍스트 도구 (Text)"; }
	wchar_t GetShortcut() const override { return L'T'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_IBEAM); }

	// 캔버스를 클릭하면 그 위치에 텍스트를 만들고 (입력 대화상자) 선택 도구로 돌아감
	void OnDown(const ToolEvent& e) override;
};

class PaintBucketTool : public ITool
{
public:
	ToolType GetType() const override { return ToolType::PaintBucket; }
	const wchar_t* GetName() const override { return L"페인트 통 도구 (Paint Bucket)"; }
	wchar_t GetShortcut() const override { return L'K'; }
	HCURSOR GetCursor() const override { return ::LoadCursor(nullptr, IDC_CROSS); }
};



// 도구 변경 리스너
class IToolChangeListener
{
public:
	virtual ~IToolChangeListener() = default;
	virtual void OnToolChanged(ToolType newTool) = 0;
};

// 도구 관리자
class ToolManager
{
private:
	ToolType m_activeType;
	ToolType m_prevType;
	bool     m_isTempPan;
	std::vector<std::unique_ptr<ITool>> m_tools;
	std::vector<IToolChangeListener*>   m_listeners;

public:
	ToolManager();

	void SetActiveTool(ToolType type);
	ToolType GetActiveType() const { return m_activeType; }
	ITool* GetActiveTool();

	void BeginTempPan();
	void EndTempPan();

	void AddListener(IToolChangeListener* pListener);
	void RemoveListener(IToolChangeListener* pListener);

	bool HandleShortcut(wchar_t key);
	SelectTool* GetSelectTool();
	GradientTool* GetGradientTool();
	std::vector<std::unique_ptr<ITool>>& GetTools() { return m_tools; }
	const std::vector<std::unique_ptr<ITool>>& GetTools() const { return m_tools; }
};
