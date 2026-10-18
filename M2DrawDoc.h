#pragma once
#include "D2DVectorEngine.h"

class CM2DrawDoc : public CDocument
{
protected:
	CM2DrawDoc() noexcept;
	DECLARE_DYNCREATE(CM2DrawDoc)

// 특성입니다.
public:
	std::vector<std::unique_ptr<Shape>> m_shapes;
	std::unique_ptr<PathShape>          m_currentPath;
	std::vector<std::unique_ptr<Shape>>& GetShapes() { return m_shapes; }
	std::unique_ptr<PathShape>&          GetCurrentPath() { return m_currentPath; }

// 재정의입니다.
public:
	virtual BOOL OnNewDocument();
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
