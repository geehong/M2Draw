#pragma once

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <optional>
#include "GridRenderer.h"

#pragma comment(lib, "d2d1.lib")

using Microsoft::WRL::ComPtr;

// ============================================================================
// 1. 뷰포트 및 좌표 변환 시스템
// ============================================================================
struct ViewportTransform {
    float         zoom = 1.0f;
    D2D1_POINT_2F pan  = { 0.0f, 0.0f };

    D2D1::Matrix3x2F ToMatrix() const {
        // Direct2D의 행렬 곱셈: (P * A) * B
        // 월드 좌표 P에 대해 먼저 Zoom(Scale) 적용 후 Pan(Translation) 적용
        return D2D1::Matrix3x2F::Scale(zoom, zoom) *
               D2D1::Matrix3x2F::Translation(pan.x, pan.y);
    }

    D2D1_POINT_2F ScreenToWorld(D2D1_POINT_2F screenPt) const {
        if (zoom <= 0.0001f) return screenPt;
        return D2D1::Point2F((screenPt.x - pan.x) / zoom, (screenPt.y - pan.y) / zoom);
    }

    D2D1_POINT_2F WorldToScreen(D2D1_POINT_2F worldPt) const {
        return D2D1::Point2F(worldPt.x * zoom + pan.x, worldPt.y * zoom + pan.y);
    }
};

class CoordinateSpace {
public:
    static D2D1_POINT_2F LocalToScreen(D2D1_POINT_2F localPt, const D2D1::Matrix3x2F& shapeXf, const ViewportTransform& view) {
        D2D1_POINT_2F worldPt = shapeXf.TransformPoint(localPt);
        return view.WorldToScreen(worldPt);
    }

    static std::optional<D2D1_POINT_2F> ScreenToLocal(D2D1_POINT_2F screenPt, const D2D1::Matrix3x2F& shapeXf, const ViewportTransform& view) {
        D2D1_POINT_2F worldPt = view.ScreenToWorld(screenPt);
        D2D1::Matrix3x2F invShape = shapeXf;
        if (!invShape.Invert()) {
            return std::nullopt;
        }
        return invShape.TransformPoint(worldPt);
    }
};

// ============================================================================
// 2. 데이터 모델 (Source of Truth)
// ============================================================================
struct BezierNode {
    enum class Type : uint8_t { Corner, Smooth, Symmetric };

    D2D1_POINT_2F anchor{};
    D2D1_POINT_2F hIn{};    // 앵커 기준 상대 오프셋
    D2D1_POINT_2F hOut{};
    Type type = Type::Corner;

    D2D1_POINT_2F AbsIn()  const { return D2D1::Point2F(anchor.x + hIn.x, anchor.y + hIn.y); }
    D2D1_POINT_2F AbsOut() const { return D2D1::Point2F(anchor.x + hOut.x, anchor.y + hOut.y); }

    bool HasIn()  const { return (hIn.x * hIn.x + hIn.y * hIn.y) > 0.0001f; }
    bool HasOut() const { return (hOut.x * hOut.x + hOut.y * hOut.y) > 0.0001f; }

    void SetHandleOut(D2D1_POINT_2F offset) {
        hOut = offset;
        if (type == Type::Corner) return;

        float lenOut = std::sqrt(offset.x * offset.x + offset.y * offset.y);
        if (lenOut < 0.0001f) return;

        D2D1_POINT_2F oppDir = D2D1::Point2F(-offset.x / lenOut, -offset.y / lenOut);

        if (type == Type::Symmetric) {
            hIn = D2D1::Point2F(oppDir.x * lenOut, oppDir.y * lenOut);
        } else if (type == Type::Smooth) {
            float lenIn = std::sqrt(hIn.x * hIn.x + hIn.y * hIn.y);
            if (lenIn < 0.0001f) lenIn = lenOut;
            hIn = D2D1::Point2F(oppDir.x * lenIn, oppDir.y * lenIn);
        }
    }

    void SetHandleIn(D2D1_POINT_2F offset) {
        hIn = offset;
        if (type == Type::Corner) return;

        float lenIn = std::sqrt(offset.x * offset.x + offset.y * offset.y);
        if (lenIn < 0.0001f) return;

        D2D1_POINT_2F oppDir = D2D1::Point2F(-offset.x / lenIn, -offset.y / lenIn);

        if (type == Type::Symmetric) {
            hOut = D2D1::Point2F(oppDir.x * lenIn, oppDir.y * lenIn);
        } else if (type == Type::Smooth) {
            float lenOut = std::sqrt(hOut.x * hOut.x + hOut.y * hOut.y);
            if (lenOut < 0.0001f) lenOut = lenIn;
            hOut = D2D1::Point2F(oppDir.x * lenOut, oppDir.y * lenOut);
        }
    }
};

struct SubPath {
    std::vector<BezierNode> nodes;
    bool closed = false;
};

enum class FillType : uint8_t {
    None = 0,    // 채움 없음 (투명)
    Solid = 1,   // 단색 채움
    Pattern = 2  // 무늬 / 패턴 (향후 확장)
};

struct ShapeStyle {
    FillType     fillType    = FillType::None;
    D2D1_COLOR_F fill        = D2D1::ColorF(0.2f, 0.6f, 1.0f, 0.25f); // 기본 채움색
    D2D1_COLOR_F stroke      = D2D1::ColorF(D2D1::ColorF::DodgerBlue);
    float        strokeWidth = 2.0f;
    D2D1_DASH_STYLE dashStyle = D2D1_DASH_STYLE_SOLID;
};

struct HitResult {
    enum class Part { None, Fill, Stroke, Anchor, HandleIn, HandleOut } part = Part::None;
    int subPath = -1;
    int node    = -1;
};

// ============================================================================
// 3. Shape 및 PathShape 클래스
// ============================================================================
class Shape {
public:
    virtual ~Shape() = default;
    virtual std::unique_ptr<Shape> Clone() const = 0;
    virtual ID2D1Geometry* GetGeometry(ID2D1Factory* f) = 0;
    virtual const wchar_t* GetShapeTypeName() const { return L"쉐이프"; }

    // Getter / Setter
    const CString& GetName() const { return m_name; }
    void SetName(const CString& val) { m_name = val; }

    const D2D1::Matrix3x2F& GetTransform() const { return m_xf; }
    void SetTransform(const D2D1::Matrix3x2F& val) { m_xf = val; Invalidate(); }

    const ShapeStyle& GetStyle() const { return m_style; }
    ShapeStyle& GetStyleRef() { return m_style; } // Invalidate는 스타일 변경 후 직접 호출하거나 SetStyle 사용
    void SetStyle(const ShapeStyle& val) { m_style = val; Invalidate(); }

    // 기존 멤버 접근 호환용 레퍼런스 (하위 호환성 유지)
    CString& name = m_name;
    D2D1::Matrix3x2F& xf = m_xf;
    ShapeStyle& style = m_style;

protected:
    CString m_name; // 개별 쉐이프 이름 (예: "직선 1", "사각형 2")
    D2D1::Matrix3x2F m_xf = D2D1::Matrix3x2F::Identity();
    ShapeStyle m_style;

public:

    void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, ID2D1Factory* f, const ViewportTransform& view) {
        if (!rt || !brush) return;
        // 팩토리가 nullptr이면 렌더 타깃의 팩토리를 사용 (nullptr 시 Rebuild 실패 → 아무것도 안 그려지는 버그 방지)
        ComPtr<ID2D1Factory> rtFactory;
        if (!f) { rt->GetFactory(&rtFactory); f = rtFactory.Get(); }
        auto* geo = GetGeometry(f);
        if (!geo) return;

        D2D1_MATRIX_3X2_F previous;
        rt->GetTransform(&previous);

        // 합성 순서: Shape Local Transform -> Viewport Transform
        D2D1::Matrix3x2F composite = xf * view.ToMatrix();
        rt->SetTransform(composite);

        // 1. 채움 렌더링
        if (style.fillType == FillType::Solid && style.fill.a > 0.0f) {
            brush->SetColor(style.fill);
            rt->FillGeometry(geo, brush);
        } else if (style.fillType == FillType::Pattern) {
            // 향후 무늬 / 패턴 브러시 렌더링 확장 슬롯 (임시 반투명 미리보기)
            D2D1_COLOR_F patternPreview = style.fill;
            patternPreview.a = 0.35f;
            brush->SetColor(patternPreview);
            rt->FillGeometry(geo, brush);
        }

        // 2. 외곽선(스트로크) 렌더링 (D2D ID2D1StrokeStyle 지원)
        if (style.stroke.a > 0.0f && style.strokeWidth > 0.0f) {
            brush->SetColor(style.stroke);
            if (f && style.dashStyle != D2D1_DASH_STYLE_SOLID) {
                D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
                    D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_ROUND,
                    D2D1_LINE_JOIN_MITER, 10.0f, style.dashStyle, 0.0f);
                ComPtr<ID2D1StrokeStyle> strokeStyle;
                if (SUCCEEDED(f->CreateStrokeStyle(sp, nullptr, 0, &strokeStyle))) {
                    rt->DrawGeometry(geo, brush, style.strokeWidth, strokeStyle.Get());
                } else {
                    rt->DrawGeometry(geo, brush, style.strokeWidth);
                }
            } else {
                rt->DrawGeometry(geo, brush, style.strokeWidth);
            }
        }

        rt->SetTransform(previous);
    }

    virtual bool GetLocalBounds(ID2D1Factory* f, D2D1_RECT_F& outBounds) {
        auto* geo = GetGeometry(f);
        if (!geo) return false;
        HRESULT hr = geo->GetBounds(nullptr, &outBounds);
        return SUCCEEDED(hr);
    }

    void Invalidate() { m_dirty = true; }

    // ------------------------------------------------------------------------
    // 공용 기하학 변형 엔진 API (SelectTool, 속성창, 메뉴 등에서 공용 호출)
    // ------------------------------------------------------------------------

    // 1. 평행 이동 변형
    void ApplyTranslation(D2D1_POINT_2F deltaWorld, const D2D1::Matrix3x2F& initialXf) {
        xf = initialXf * D2D1::Matrix3x2F::Translation(deltaWorld.x, deltaWorld.y);
    }

    // 2. 바운딩 박스 8개 핸들 기준 스케일 변형
    //    handleIdx: 1=TL, 2=T, 3=TR, 4=R, 5=BR, 6=B, 7=BL, 8=L
    void ScaleByHandle(int handleIdx, D2D1_POINT_2F curWorld, const D2D1_RECT_F& initialLocalBounds, const D2D1::Matrix3x2F& initialXf) {
        D2D1::Matrix3x2F invInitial = initialXf;
        if (!invInitial.Invert()) return;

        D2D1_POINT_2F curLocal = invInitial.TransformPoint(curWorld);
        float origW = initialLocalBounds.right - initialLocalBounds.left;
        float origH = initialLocalBounds.bottom - initialLocalBounds.top;
        if (origW <= 0.001f || origH <= 0.001f) return;

        D2D1_POINT_2F anchorLocal = { 0.0f, 0.0f };
        float sx = 1.0f;
        float sy = 1.0f;

        switch (handleIdx) {
        case 5: // BR -> 고정점 TL
            anchorLocal = D2D1::Point2F(initialLocalBounds.left, initialLocalBounds.top);
            sx = (curLocal.x - anchorLocal.x) / origW;
            sy = (curLocal.y - anchorLocal.y) / origH;
            break;
        case 1: // TL -> 고정점 BR
            anchorLocal = D2D1::Point2F(initialLocalBounds.right, initialLocalBounds.bottom);
            sx = (anchorLocal.x - curLocal.x) / origW;
            sy = (anchorLocal.y - curLocal.y) / origH;
            break;
        case 3: // TR -> 고정점 BL
            anchorLocal = D2D1::Point2F(initialLocalBounds.left, initialLocalBounds.bottom);
            sx = (curLocal.x - anchorLocal.x) / origW;
            sy = (anchorLocal.y - curLocal.y) / origH;
            break;
        case 7: // BL -> 고정점 TR
            anchorLocal = D2D1::Point2F(initialLocalBounds.right, initialLocalBounds.top);
            sx = (anchorLocal.x - curLocal.x) / origW;
            sy = (curLocal.y - anchorLocal.y) / origH;
            break;
        case 4: // R -> 고정점 L
            anchorLocal = D2D1::Point2F(initialLocalBounds.left, (initialLocalBounds.top + initialLocalBounds.bottom) * 0.5f);
            sx = (curLocal.x - anchorLocal.x) / origW;
            sy = 1.0f;
            break;
        case 8: // L -> 고정점 R
            anchorLocal = D2D1::Point2F(initialLocalBounds.right, (initialLocalBounds.top + initialLocalBounds.bottom) * 0.5f);
            sx = (anchorLocal.x - curLocal.x) / origW;
            sy = 1.0f;
            break;
        case 6: // B -> 고정점 T
            anchorLocal = D2D1::Point2F((initialLocalBounds.left + initialLocalBounds.right) * 0.5f, initialLocalBounds.top);
            sx = 1.0f;
            sy = (curLocal.y - anchorLocal.y) / origH;
            break;
        case 2: // T -> 고정점 B
            anchorLocal = D2D1::Point2F((initialLocalBounds.left + initialLocalBounds.right) * 0.5f, initialLocalBounds.bottom);
            sx = 1.0f;
            sy = (anchorLocal.y - curLocal.y) / origH;
            break;
        default:
            return;
        }

        if (std::abs(sx) < 0.01f) sx = 0.01f;
        if (std::abs(sy) < 0.01f) sy = 0.01f;

        D2D1::Matrix3x2F scaleMat = D2D1::Matrix3x2F::Scale(sx, sy, anchorLocal);
        xf = scaleMat * initialXf;
    }

    // 3. 중심점 기준 회전 변형
    void RotateAboutCenter(D2D1_POINT_2F startWorld, D2D1_POINT_2F curWorld, const D2D1_RECT_F& initialLocalBounds, const D2D1::Matrix3x2F& initialXf) {
        D2D1_POINT_2F centerLocal = D2D1::Point2F(
            (initialLocalBounds.left + initialLocalBounds.right) * 0.5f,
            (initialLocalBounds.top + initialLocalBounds.bottom) * 0.5f
        );
        D2D1_POINT_2F centerWorld = initialXf.TransformPoint(centerLocal);

        float angleStart = std::atan2(startWorld.y - centerWorld.y, startWorld.x - centerWorld.x);
        float angleCur   = std::atan2(curWorld.y - centerWorld.y, curWorld.x - centerWorld.x);
        float angleDeg   = (angleCur - angleStart) * (180.0f / 3.14159265f);

        xf = initialXf * D2D1::Matrix3x2F::Rotation(angleDeg, centerWorld);
    }

    // 4. 로컬 사각 바운딩박스 기준 8개 핸들 및 상단 회전 핸들 로컬 좌표 반환
    static void GetLocalHandlePoints(const D2D1_RECT_F& b, D2D1_POINT_2F outHandles[9]) {
        float midX = (b.left + b.right) * 0.5f;
        float midY = (b.top + b.bottom) * 0.5f;

        outHandles[0] = D2D1::Point2F(b.left,  b.top);         // TL (인덱스 0 / Handle::TL은 1 매핑)
        outHandles[1] = D2D1::Point2F(midX,    b.top);         // T
        outHandles[2] = D2D1::Point2F(b.right, b.top);         // TR
        outHandles[3] = D2D1::Point2F(b.right, midY);          // R
        outHandles[4] = D2D1::Point2F(b.right, b.bottom);      // BR
        outHandles[5] = D2D1::Point2F(midX,    b.bottom);      // B
        outHandles[6] = D2D1::Point2F(b.left,  b.bottom);      // BL
        outHandles[7] = D2D1::Point2F(b.left,  midY);          // L
        outHandles[8] = D2D1::Point2F(midX,    b.top - 20.0f); // Rotate
    }

protected:
    bool m_dirty = true;
};

class PathShape : public Shape {
public:
    // Path Vector Getter / Setter
    const std::vector<SubPath>& GetPaths() const { return m_paths; }
    std::vector<SubPath>& GetPathsRef() { return m_paths; }
    void SetPaths(const std::vector<SubPath>& newPaths) { m_paths = newPaths; Invalidate(); }

    void ClearPaths() { m_paths.clear(); Invalidate(); }
    void AddSubPath(const SubPath& sp) { m_paths.push_back(sp); Invalidate(); }

    // 기존 멤버 접근 호환용 레퍼런스 (하위 호환성 유지)
    std::vector<SubPath>& paths = m_paths;

protected:
    std::vector<SubPath> m_paths;

public:

    std::unique_ptr<Shape> Clone() const override {
        auto copy = std::make_unique<PathShape>(*this);
        copy->m_geo.Reset();
        copy->m_dirty = true;
        return copy;
    }

    ID2D1Geometry* GetGeometry(ID2D1Factory* f) override {
        if (m_dirty || !m_geo) {
            Rebuild(f);
        }
        return m_geo.Get();
    }

    void AddAnchor(D2D1_POINT_2F localPt, bool forceNewSubPath = false) {
        if (paths.empty() || forceNewSubPath || paths.back().closed) {
            paths.emplace_back();
        }
        BezierNode node;
        node.anchor = localPt;
        node.type = BezierNode::Type::Corner;
        paths.back().nodes.push_back(node);
        Invalidate();
    }

    bool GetLocalBounds(ID2D1Factory* f, D2D1_RECT_F& outBounds) override {
        // 1. D2D Geometry Bounds 시도
        auto* geo = GetGeometry(f);
        if (geo) {
            D2D1_RECT_F b{};
            if (SUCCEEDED(geo->GetBounds(nullptr, &b))) {
                if (b.right > b.left || b.bottom > b.top) {
                    outBounds = b;
                    return true;
                }
            }
        }

        // 2. Geometry Bounds가 비어있거나 실패한 경우 (열린 직선/곡선 등), 노드/핸들 좌표 기반 AABB 직접 계산
        bool hasAnyPoint = false;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;

        for (const auto& sp : paths) {
            for (const auto& node : sp.nodes) {
                auto updateBox = [&](D2D1_POINT_2F pt) {
                    minX = (std::min)(minX, pt.x);
                    minY = (std::min)(minY, pt.y);
                    maxX = (std::max)(maxX, pt.x);
                    maxY = (std::max)(maxY, pt.y);
                    hasAnyPoint = true;
                };
                updateBox(node.anchor);
                if (node.HasIn()) updateBox(node.AbsIn());
                if (node.HasOut()) updateBox(node.AbsOut());
            }
        }

        if (hasAnyPoint) {
            // 완전 수평선이나 수직선이어도 0 크기가 되지 않도록 최소 1px 여백 부여
            if (maxX - minX < 2.0f) { minX -= 1.0f; maxX += 1.0f; }
            if (maxY - minY < 2.0f) { minY -= 1.0f; maxY += 1.0f; }
            outBounds = D2D1::RectF(minX, minY, maxX, maxY);
            return true;
        }

        return false;
    }

    HitResult HitTestScreenSpace(D2D1_POINT_2F screenPt, float tolPx, const ViewportTransform& view, ID2D1Factory* f) {
        const float tolSq = tolPx * tolPx;

        // 1순위: 앵커 및 핸들
        for (int spIdx = 0; spIdx < static_cast<int>(paths.size()); ++spIdx) {
            const auto& sp = paths[spIdx];
            for (int nIdx = 0; nIdx < static_cast<int>(sp.nodes.size()); ++nIdx) {
                const auto& node = sp.nodes[nIdx];
                D2D1_POINT_2F anchorScreen = CoordinateSpace::LocalToScreen(node.anchor, xf, view);
                float dx = screenPt.x - anchorScreen.x;
                float dy = screenPt.y - anchorScreen.y;
                if ((dx * dx + dy * dy) <= tolSq) return { HitResult::Part::Anchor, spIdx, nIdx };

                if (node.HasOut()) {
                    D2D1_POINT_2F hScreen = CoordinateSpace::LocalToScreen(node.AbsOut(), xf, view);
                    if (((screenPt.x - hScreen.x)*(screenPt.x - hScreen.x) + (screenPt.y - hScreen.y)*(screenPt.y - hScreen.y)) <= tolSq)
                        return { HitResult::Part::HandleOut, spIdx, nIdx };
                }
                if (node.HasIn()) {
                    D2D1_POINT_2F hScreen = CoordinateSpace::LocalToScreen(node.AbsIn(), xf, view);
                    if (((screenPt.x - hScreen.x)*(screenPt.x - hScreen.x) + (screenPt.y - hScreen.y)*(screenPt.y - hScreen.y)) <= tolSq)
                        return { HitResult::Part::HandleIn, spIdx, nIdx };
                }
            }
        }

        // 2순위: 스트로크 및 채우기
        auto localPtOpt = CoordinateSpace::ScreenToLocal(screenPt, xf, view);
        if (!localPtOpt.has_value()) return {};

        auto* g = GetGeometry(f);
        if (!g) return {};

        BOOL hit = FALSE;
        float hitWidth = style.strokeWidth + 2.0f * (tolPx / view.zoom);
        if (style.stroke.a > 0.0f && hitWidth > 0.0f) {
            HRESULT hr = g->StrokeContainsPoint(*localPtOpt, hitWidth, nullptr, nullptr, 0.25f / view.zoom, &hit);
            if (SUCCEEDED(hr) && hit) return { HitResult::Part::Stroke };
        }

        if (style.fill.a > 0.0f) {
            hit = FALSE;
            HRESULT hr = g->FillContainsPoint(*localPtOpt, nullptr, 0.25f / view.zoom, &hit);
            if (SUCCEEDED(hr) && hit) return { HitResult::Part::Fill };
        }

        return {};
    }

    // ------------------------------------------------------------------------
    // 공용 에디트 포인트 (정점 앵커 및 베이지어 핸들) 조작 API
    // ------------------------------------------------------------------------
    void MoveNodeOrHandle(HitResult::Part part, int subPathIdx, int nodeIdx, D2D1_POINT_2F worldPt, bool breakTangent = false) {
        if (subPathIdx < 0 || subPathIdx >= static_cast<int>(paths.size())) return;
        auto& sp = paths[subPathIdx];
        if (nodeIdx < 0 || nodeIdx >= static_cast<int>(sp.nodes.size())) return;

        D2D1::Matrix3x2F invXf = xf;
        if (!invXf.Invert()) return;
        D2D1_POINT_2F curLocal = invXf.TransformPoint(worldPt);
        auto& node = sp.nodes[nodeIdx];

        if (breakTangent && (part == HitResult::Part::HandleIn || part == HitResult::Part::HandleOut)) {
            node.type = BezierNode::Type::Corner;
        }

        if (part == HitResult::Part::Anchor) {
            node.anchor = curLocal;
            Invalidate();
        } else if (part == HitResult::Part::HandleIn) {
            D2D1_POINT_2F offset = D2D1::Point2F(curLocal.x - node.anchor.x, curLocal.y - node.anchor.y);
            node.SetHandleIn(offset);
            Invalidate();
        } else if (part == HitResult::Part::HandleOut) {
            D2D1_POINT_2F offset = D2D1::Point2F(curLocal.x - node.anchor.x, curLocal.y - node.anchor.y);
            node.SetHandleOut(offset);
            Invalidate();
        }
    }

private:
    ComPtr<ID2D1PathGeometry> m_geo;

    void Rebuild(ID2D1Factory* f) {
        m_geo.Reset();
        if (!f) { m_dirty = true; return; }

        HRESULT hr = f->CreatePathGeometry(&m_geo);
        if (FAILED(hr)) { m_dirty = true; return; }

        ComPtr<ID2D1GeometrySink> sink;
        hr = m_geo->Open(&sink);
        if (FAILED(hr)) { m_geo.Reset(); m_dirty = true; return; }

        for (auto& sp : paths) {
            if (sp.nodes.size() < 2) continue;

            sink->BeginFigure(sp.nodes[0].anchor, D2D1_FIGURE_BEGIN_FILLED);

            size_t segCount = sp.closed ? sp.nodes.size() : sp.nodes.size() - 1;
            for (size_t i = 0; i < segCount; ++i) {
                const auto& a = sp.nodes[i];
                const auto& b = sp.nodes[(i + 1) % sp.nodes.size()];

                if (!a.HasOut() && !b.HasIn()) {
                    sink->AddLine(b.anchor);
                } else {
                    sink->AddBezier(D2D1::BezierSegment(a.AbsOut(), b.AbsIn(), b.anchor));
                }
            }
            sink->EndFigure(sp.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        }

        hr = sink->Close();
        if (FAILED(hr)) {
            m_geo.Reset();
            m_dirty = true;
            return;
        }
        m_dirty = false;
    }
};


