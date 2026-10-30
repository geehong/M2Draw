#include "pch.h"
#include "ShapePresets.h"
#include "PathTrim.h"
#include <algorithm>
#include <cmath>

namespace ge {
namespace Shapes {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kKappa = 0.5522847498f;   // 1/4 원 베지에 근사 계수

Vec2 V(float x, float y) { return Vec2{ x, y }; }

Trim::Cubic Line(Vec2 a, Vec2 b) { return Trim::Cubic{ a, a, b, b }; }

// 원호 [a0, a1] (부호 있는 각도) 를 90도 이하 조각 3차 베지에로
void ArcCubics(float cx, float cy, float rx, float ry, float a0, float a1, std::vector<Trim::Cubic>& out)
{
	const float sweep = a1 - a0;
	const int n = (std::max)(1, static_cast<int>(std::ceil(std::fabs(sweep) / (kPi * 0.5f) - 1e-4f)));
	const float s = sweep / n;
	const float k = 4.0f / 3.0f * std::tan(s / 4.0f);
	float a = a0;
	for (int i = 0; i < n; ++i)
	{
		const float b = a + s;
		const Vec2 p0 = V(cx + rx * std::cos(a), cy + ry * std::sin(a));
		const Vec2 p3 = V(cx + rx * std::cos(b), cy + ry * std::sin(b));
		const Vec2 p1 = V(p0.x + k * rx * (-std::sin(a)), p0.y + k * ry * std::cos(a));
		const Vec2 p2 = V(p3.x - k * rx * (-std::sin(b)), p3.y - k * ry * std::cos(b));
		out.push_back(Trim::Cubic{ p0, p1, p2, p3 });
		a = b;
	}
}

float WrapAngle(float a)
{
	while (a < 0.0f) a += 2.0f * kPi;
	while (a >= 2.0f * kPi) a -= 2.0f * kPi;
	return a;
}

} // namespace

std::vector<SubPath> RoundedRect(float x, float y, float w, float h, float r)
{
	const float rr = (std::max)(0.0f, (std::min)(r, (std::min)(w, h) * 0.5f));
	std::vector<Trim::Cubic> c;
	if (rr <= 1e-4f)
	{
		const Vec2 p[4] = { V(x, y), V(x + w, y), V(x + w, y + h), V(x, y + h) };
		for (int i = 0; i < 4; ++i) c.push_back(Line(p[i], p[(i + 1) % 4]));
		return { Trim::FromCubics(c, true) };
	}
	const float k = kKappa * rr;
	// 시계 방향(화면 기준): 위 변 -> 오른쪽 위 모서리 -> 오른쪽 변 -> ...
	c.push_back(Line(V(x + rr, y), V(x + w - rr, y)));
	c.push_back(Trim::Cubic{ V(x + w - rr, y), V(x + w - rr + k, y), V(x + w, y + rr - k), V(x + w, y + rr) });
	c.push_back(Line(V(x + w, y + rr), V(x + w, y + h - rr)));
	c.push_back(Trim::Cubic{ V(x + w, y + h - rr), V(x + w, y + h - rr + k), V(x + w - rr + k, y + h), V(x + w - rr, y + h) });
	c.push_back(Line(V(x + w - rr, y + h), V(x + rr, y + h)));
	c.push_back(Trim::Cubic{ V(x + rr, y + h), V(x + rr - k, y + h), V(x, y + h - rr + k), V(x, y + h - rr) });
	c.push_back(Line(V(x, y + h - rr), V(x, y + rr)));
	c.push_back(Trim::Cubic{ V(x, y + rr), V(x, y + rr - k), V(x + rr - k, y), V(x + rr, y) });
	return { Trim::FromCubics(c, true) };
}

std::vector<SubPath> Diamond(float x, float y, float w, float h)
{
	const Vec2 p[4] = { V(x + w * 0.5f, y), V(x + w, y + h * 0.5f), V(x + w * 0.5f, y + h), V(x, y + h * 0.5f) };
	std::vector<Trim::Cubic> c;
	for (int i = 0; i < 4; ++i) c.push_back(Line(p[i], p[(i + 1) % 4]));
	return { Trim::FromCubics(c, true) };
}

std::vector<SubPath> Ellipse(float cx, float cy, float rx, float ry)
{
	std::vector<Trim::Cubic> c;
	ArcCubics(cx, cy, rx, ry, 0.0f, 2.0f * kPi, c);
	return { Trim::FromCubics(c, true) };
}

std::vector<SubPath> Donut(float cx, float cy, float rOuter, float innerRatio)
{
	const float ri = rOuter * (std::max)(0.0f, (std::min)(0.95f, innerRatio));
	std::vector<SubPath> out = Ellipse(cx, cy, rOuter, rOuter);
	// 안쪽 원은 반대 방향으로 감아 구멍이 되게 한다 (채우기 규칙과 무관하게 방향으로 구분)
	std::vector<Trim::Cubic> c;
	ArcCubics(cx, cy, ri, ri, 2.0f * kPi, 0.0f, c);
	out.push_back(Trim::FromCubics(c, true));
	return out;
}

std::vector<SubPath> Pie(float cx, float cy, float r, float a0, float a1)
{
	std::vector<Trim::Cubic> c;
	const Vec2 center = V(cx, cy);
	const Vec2 start = V(cx + r * std::cos(a0), cy + r * std::sin(a0));
	const Vec2 end = V(cx + r * std::cos(a1), cy + r * std::sin(a1));
	c.push_back(Line(center, start));
	ArcCubics(cx, cy, r, r, a0, a1, c);
	c.push_back(Line(end, center));
	return { Trim::FromCubics(c, true) };
}

bool CircleThrough3(Vec2 a, Vec2 b, Vec2 c, Vec2& center, float& radius)
{
	const float d = 2.0f * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
	if (std::fabs(d) < 1e-6f) return false;
	const float a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
	center.x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
	center.y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
	radius = std::sqrt((a.x - center.x) * (a.x - center.x) + (a.y - center.y) * (a.y - center.y));
	return radius > 1e-4f;
}

std::vector<SubPath> ArcThrough3(Vec2 a, Vec2 m, Vec2 b)
{
	Vec2 C{};
	float r = 0.0f;
	if (!CircleThrough3(a, m, b, C, r)) return {};
	const float ta = std::atan2(a.y - C.y, a.x - C.x);
	const float tm = std::atan2(m.y - C.y, m.x - C.x);
	const float tb = std::atan2(b.y - C.y, b.x - C.x);
	// a -> m -> b 순서로 지나도록 방향을 정한다
	const float amCcw = WrapAngle(tm - ta), abCcw = WrapAngle(tb - ta);
	const float sweep = amCcw < abCcw ? abCcw : abCcw - 2.0f * kPi;
	std::vector<Trim::Cubic> c;
	ArcCubics(C.x, C.y, r, r, ta, ta + sweep, c);
	if (c.empty()) return {};
	return { Trim::FromCubics(c, false) };
}

} // namespace Shapes
} // namespace ge
