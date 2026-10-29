#include "pch.h"
#include "GroupTransform.h"
#include <cmath>

namespace ge {

bool UnionWorldBounds(const std::vector<Node*>& nodes, Rect2& out)
{
	bool any = false;
	for (Node* n : nodes) {
		if (!n) continue;
		Rect2 lb;
		if (!n->GetLocalBounds(lb)) continue;
		const Rect2 wb = n->GetWorldTransform().TransformRect(lb);
		if (!any) { out = wb; any = true; } else out.Union(wb);
	}
	return any;
}

Matrix2D MakeWorldScale(float sx, float sy, Vec2 anchor) { return Matrix2D::Scale(sx, sy, anchor); }

Matrix2D MakeWorldRotation(float degrees, Vec2 center) { return Matrix2D::Rotation(degrees, center); }

Matrix2D MakeWorldSkewX(float k, Vec2 anchor)
{
	// [x y 1] * M : x' = x*m11 + y*m21 + dx  ->  m21 = k, dx = -k*anchor.y
	return Matrix2D(1.0f, 0.0f, k, 1.0f, -k * anchor.y, 0.0f);
}

Matrix2D MakeWorldSkewY(float k, Vec2 anchor)
{
	// y' = x*m12 + y*m22 + dy  ->  m12 = k, dy = -k*anchor.x
	return Matrix2D(1.0f, k, 0.0f, 1.0f, 0.0f, -k * anchor.x);
}

void ApplyWorldTransform(const std::vector<Node*>& nodes, const std::vector<Matrix2D>& initialLocal, const Matrix2D& T)
{
	for (size_t i = 0; i < nodes.size() && i < initialLocal.size(); ++i) {
		Node* n = nodes[i];
		if (!n) continue;
		Matrix2D pw = Matrix2D::Identity();
		if (GroupNode* p = n->GetParent()) pw = p->GetWorldTransform();
		Matrix2D pwInv = pw;
		if (!pwInv.Invert()) continue;
		n->SetTransform(initialLocal[i] * (pw * T * pwInv));
		if (n->Kind() != NodeKind::Text) n->Invalidate();   // 텍스트 배치 캐시 보존
	}
}

} // namespace ge
