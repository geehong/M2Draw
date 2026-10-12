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
};

struct SubPath {
    std::vector<BezierNode> nodes;
    bool closed = false;
};

struct ShapeStyle {
    D2D1_COLOR_F fill   = D2D1::ColorF(0, 0, 0, 0.0f);
    D2D1_COLOR_F stroke = D2D1::ColorF(D2D1::ColorF::DodgerBlue);
    float strokeWidth   = 2.0f;
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

    D2D1::Matrix3x2F xf = D2D1::Matrix3x2F::Identity();
    ShapeStyle style;

    void Draw(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, ID2D1Factory* f, const ViewportTransform& view) {
        auto* geo = GetGeometry(f);
        if (!geo || !rt || !brush) return;

        D2D1_MATRIX_3X2_F previous;
        rt->GetTransform(&previous);

        // 합성 순서: Shape Local Transform -> Viewport Transform
        D2D1::Matrix3x2F composite = xf * view.ToMatrix();
        rt->SetTransform(composite);

        if (style.fill.a > 0.0f) {
            brush->SetColor(style.fill);
            rt->FillGeometry(geo, brush);
        }
        if (style.stroke.a > 0.0f && style.strokeWidth > 0.0f) {
            brush->SetColor(style.stroke);
            rt->DrawGeometry(geo, brush, style.strokeWidth);
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

protected:
    bool m_dirty = true;
};

class PathShape : public Shape {
public:
    std::vector<SubPath> paths;

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

            sink->BeginFigure(sp.nodes[0].anchor, sp.closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);

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

// ============================================================================
// 4. Undo/Redo 트랜잭션 매니저
// ============================================================================
class UndoManager {
public:
    static constexpr size_t kMaxHistory = 50;

    void BeginSnapshot(const PathShape* shape) {
        if (shape) {
            m_pendingSnapshot = std::unique_ptr<PathShape>(static_cast<PathShape*>(shape->Clone().release()));
        }
    }

    void CommitSnapshot() {
        if (m_pendingSnapshot) {
            m_undoStack.push_back(std::move(m_pendingSnapshot));
            if (m_undoStack.size() > kMaxHistory) m_undoStack.pop_front();
            m_redoStack.clear();
        }
    }

    void RollbackPending(std::unique_ptr<PathShape>& shape) {
        if (m_pendingSnapshot) {
            shape = std::move(m_pendingSnapshot);
        }
    }

    bool HasPending() const { return m_pendingSnapshot != nullptr; }
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    void PerformUndo(std::unique_ptr<PathShape>& shape) {
        if (!CanUndo() || !shape) return;
        m_redoStack.push_back(std::move(shape));
        shape = std::move(m_undoStack.back());
        m_undoStack.pop_back();
    }

    void PerformRedo(std::unique_ptr<PathShape>& shape) {
        if (!CanRedo() || !shape) return;
        m_undoStack.push_back(std::move(shape));
        shape = std::move(m_redoStack.back());
        m_redoStack.pop_back();
    }

private:
    std::unique_ptr<PathShape>             m_pendingSnapshot;
    std::deque<std::unique_ptr<PathShape>> m_undoStack;
    std::deque<std::unique_ptr<PathShape>> m_redoStack;
};

// ============================================================================
// 5. PenTool 상태 머신 (패스 수명 주기 및 롤백 완결)
// ============================================================================
class PenStateMachine {
public:
    enum class State {
        Idle,            // 도구 대기 상태
        Placing,         // 연속 앵커 클릭 대기 (SubPath 진행 중)
        Pressed,         // 마우스 클릭 후 드래그 임계값(4px) 판정 대기
        DraggingHandle   // 베지에 핸들 드래그 연장 중
    };

    void OnLButtonDown(std::unique_ptr<PathShape>& shape, const GridRenderer& grid, UndoManager& undo,
                       D2D1_POINT_2F screenPt, const ViewportTransform& view)
    {
        if (!shape) shape = std::make_unique<PathShape>();

        D2D1_POINT_2F worldPt = view.ScreenToWorld(screenPt);
        D2D1_POINT_2F snappedWorld = worldPt; // 그리드 스냅 연동

        D2D1::Matrix3x2F invShape = shape->xf;
        if (!invShape.Invert()) {
            return;
        }
        D2D1_POINT_2F localPt = invShape.TransformPoint(snappedWorld);

        undo.BeginSnapshot(shape.get());

        m_downScreenPt = screenPt;
        m_state = State::Pressed;

        // 패스 폐합 검사 (첫 앵커와 8px 이내인지 비교)
        if (!m_needNewSubPath && !shape->paths.empty() && !shape->paths.back().closed && shape->paths.back().nodes.size() >= 3) {
            auto& sp = shape->paths.back();
            D2D1_POINT_2F firstScreenPt = CoordinateSpace::LocalToScreen(sp.nodes[0].anchor, shape->xf, view);
            float dx = firstScreenPt.x - screenPt.x;
            float dy = firstScreenPt.y - screenPt.y;

            if ((dx * dx + dy * dy) <= 64.0f) {
                sp.closed = true;
                shape->Invalidate();
                undo.CommitSnapshot();
                m_state = State::Idle;
                m_needNewSubPath = true;
                return;
            }
        }

        shape->AddAnchor(localPt, m_needNewSubPath);
        m_needNewSubPath = false;
    }

    void OnMouseMove(std::unique_ptr<PathShape>& shape, D2D1_POINT_2F screenPt, const ViewportTransform& view) {
        if (!shape || shape->paths.empty()) return;
        auto& sp = shape->paths.back();
        if (sp.nodes.empty()) return;

        if (m_state == State::Pressed) {
            float dx = screenPt.x - m_downScreenPt.x;
            float dy = screenPt.y - m_downScreenPt.y;
            if ((dx * dx + dy * dy) >= 16.0f) {
                m_state = State::DraggingHandle;
                sp.nodes.back().type = BezierNode::Type::Symmetric;
            }
        }

        if (m_state == State::DraggingHandle) {
            auto localPtOpt = CoordinateSpace::ScreenToLocal(screenPt, shape->xf, view);
            if (!localPtOpt.has_value()) return;

            auto& node = sp.nodes.back();
            D2D1_POINT_2F offset = D2D1::Point2F(localPtOpt->x - node.anchor.x, localPtOpt->y - node.anchor.y);

            node.SetHandleOut(offset);
            shape->Invalidate();
        }
    }

    void OnLButtonUp(UndoManager& undo) {
        if (m_state == State::DraggingHandle || m_state == State::Pressed) {
            undo.CommitSnapshot();
            m_state = State::Placing;
        }
    }

    void FinishCurrentPath() {
        m_state = State::Idle;
        m_needNewSubPath = true;
    }

    void CancelOperation(std::unique_ptr<PathShape>& shape, UndoManager& undo) {
        if (undo.HasPending()) {
            undo.RollbackPending(shape);
        }
        m_state = State::Idle;
        m_needNewSubPath = true;
    }

    State GetState() const { return m_state; }

private:
    State         m_state = State::Idle;
    bool          m_needNewSubPath = true;
    D2D1_POINT_2F m_downScreenPt{};
};
