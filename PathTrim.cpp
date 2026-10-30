#include "pch.h"
#include "PathTrim.h"
#include <algorithm>
#include <cmath>

namespace ge {
namespace Trim {

namespace {

constexpr int kFlat = 24;      // 교차 계산용 세그먼트당 평탄화 단계
constexpr int kNearest = 64;   // 가장 가까운 점 계산용 평탄화 단계

Vec2 Sub(Vec2 a, Vec2 b) { return Vec2{ a.x - b.x, a.y - b.y }; }
float Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
float Len(Vec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }
float Dist(Vec2 a, Vec2 b) { return Len(Sub(a, b)); }
Vec2 Lerp(Vec2 a, Vec2 b, float t) { return Vec2{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }

Vec2 Bez(const Cubic& c, float t)
{
	const float u = 1.0f - t;
	const float a = u * u * u, b = 3.0f * u * u * t, cc = 3.0f * u * t * t, d = t * t * t;
	return Vec2{ a * c.p0.x + b * c.p1.x + cc * c.p2.x + d * c.p3.x, a * c.p0.y + b * c.p1.y + cc * c.p2.y + d * c.p3.y };
}

// de Casteljau 분할
void Split(const Cubic& c, float t, Cubic& L, Cubic& R)
{
	const Vec2 p01 = Lerp(c.p0, c.p1, t), p12 = Lerp(c.p1, c.p2, t), p23 = Lerp(c.p2, c.p3, t);
	const Vec2 a = Lerp(p01, p12, t), b = Lerp(p12, p23, t), m = Lerp(a, b, t);
	L = Cubic{ c.p0, p01, a, m };
	R = Cubic{ m, b, p23, c.p3 };
}

// 두 선분 p-q, r-s 의 교차. u 는 p-q 위치 (0..1), v 는 r-s 위치. infB 면 r-s 는 무한 직선
bool SegInter(Vec2 p, Vec2 q, Vec2 r, Vec2 s, bool infB, float& u, float& v)
{
	const Vec2 d1 = Sub(q, p), d2 = Sub(s, r);
	const float den = Cross(d1, d2);
	if (std::fabs(den) < 1e-9f) return false;
	const Vec2 w = Sub(r, p);
	u = Cross(w, d2) / den;
	v = Cross(w, d1) / den;
	if (u < -1e-5f || u > 1.0f + 1e-5f) return false;
	if (!infB && (v < -1e-5f || v > 1.0f + 1e-5f)) return false;
	return true;
}

// 구간 [lo, hi] 의 세그먼트 조각을 out 에 추가 (lo, hi 는 파라미터, 0 <= lo < hi <= N)
void ExtractRange(const std::vector<Cubic>& segs, float lo, float hi, std::vector<Cubic>& out)
{
	const int N = static_cast<int>(segs.size());
	for (int i = (std::max)(0, static_cast<int>(std::floor(lo))); i < N && static_cast<float>(i) < hi; ++i)
	{
		const float s = (std::max)(lo, static_cast<float>(i));
		const float e = (std::min)(hi, static_cast<float>(i + 1));
		if (e - s < 1e-6f) continue;
		const float t0 = s - static_cast<float>(i);
		float t1 = e - static_cast<float>(i);
		Cubic c = segs[i], L, R;
		if (t0 > 1e-6f)
		{
			Split(c, t0, L, R);
			c = R;
			t1 = (t1 - t0) / (1.0f - t0);
		}
		if (t1 < 1.0f - 1e-6f)
		{
			Split(c, t1, L, R);
			c = L;
		}
		out.push_back(c);
	}
}

} // namespace

std::vector<Cubic> ToCubics(const SubPath& sp)
{
	std::vector<Cubic> out;
	const auto& n = sp.nodes;
	const size_t m = n.size();
	if (m < 2) return out;
	const size_t segs = sp.closed ? m : m - 1;
	for (size_t i = 0; i < segs; ++i)
	{
		const BezierNode& a = n[i];
		const BezierNode& b = n[(i + 1) % m];
		out.push_back(Cubic{ a.anchor, a.AbsOut(), b.AbsIn(), b.anchor });
	}
	return out;
}

SubPath FromCubics(const std::vector<Cubic>& segs, bool closed)
{
	SubPath sp;
	sp.closed = closed;
	const size_t m = segs.size();
	if (m == 0) return sp;
	if (closed)
	{
		for (size_t k = 0; k < m; ++k)
		{
			const Cubic& c = segs[k];
			const Cubic& prev = segs[(k + m - 1) % m];
			BezierNode bn;
			bn.anchor = c.p0;
			bn.hOut = Sub(c.p1, c.p0);
			bn.hIn = Sub(prev.p2, c.p0);
			sp.nodes.push_back(bn);
		}
		return sp;
	}
	for (size_t k = 0; k < m; ++k)
	{
		const Cubic& c = segs[k];
		BezierNode bn;
		bn.anchor = c.p0;
		bn.hOut = Sub(c.p1, c.p0);
		bn.hIn = (k > 0) ? Sub(segs[k - 1].p2, c.p0) : Vec2{ 0.0f, 0.0f };
		sp.nodes.push_back(bn);
	}
	const Cubic& last = segs[m - 1];
	BezierNode end;
	end.anchor = last.p3;
	end.hIn = Sub(last.p2, last.p3);
	sp.nodes.push_back(end);
	return sp;
}

std::vector<Cubic> Extract(const std::vector<Cubic>& segs, float a, float b, bool closed)
{
	std::vector<Cubic> out;
	const float N = static_cast<float>(segs.size());
	if (N <= 0.0f) return out;
	if (!closed)
	{
		const float lo = std::clamp((std::min)(a, b), 0.0f, N);
		const float hi = std::clamp((std::max)(a, b), 0.0f, N);
		ExtractRange(segs, lo, hi, out);
		return out;
	}
	float fa = std::fmod(a, N), fb = std::fmod(b, N);
	if (fa < 0.0f) fa += N;
	if (fb < 0.0f) fb += N;
	if (fa <= fb) ExtractRange(segs, fa, fb, out);
	else
	{
		ExtractRange(segs, fa, N, out);
		ExtractRange(segs, 0.0f, fb, out);
	}
	return out;
}

bool IsStraight(const Cubic& c)
{
	const Vec2 d = Sub(c.p3, c.p0);
	const float len = Len(d);
	if (len < 1e-6f) return false;
	const float tol = 1e-3f * len + 1e-4f;
	auto off = [&](Vec2 q) { return std::fabs(Cross(d, Sub(q, c.p0))) / len; };
	return off(c.p1) < tol && off(c.p2) < tol;
}

std::vector<Cut> FindCuts(const std::vector<Cubic>& target, const std::vector<Cubic>& cutters, bool extendCutters)
{
	struct Piece { Vec2 p, q; bool inf; };
	std::vector<Piece> cp;
	for (const Cubic& c : cutters)
	{
		if (IsStraight(c))
		{
			cp.push_back(Piece{ c.p0, c.p3, extendCutters });
			continue;
		}
		Vec2 prev = c.p0;
		for (int k = 1; k <= kFlat; ++k)
		{
			const Vec2 cur = Bez(c, static_cast<float>(k) / kFlat);
			cp.push_back(Piece{ prev, cur, false });
			prev = cur;
		}
	}

	std::vector<Cut> out;
	for (size_t i = 0; i < target.size(); ++i)
	{
		Vec2 prev = Bez(target[i], 0.0f);
		for (int k = 1; k <= kFlat; ++k)
		{
			const Vec2 cur = Bez(target[i], static_cast<float>(k) / kFlat);
			for (const Piece& pc : cp)
			{
				float u = 0.0f, v = 0.0f;
				if (!SegInter(prev, cur, pc.p, pc.q, pc.inf, u, v)) continue;
				const float g = static_cast<float>(i) + (static_cast<float>(k - 1) + std::clamp(u, 0.0f, 1.0f)) / kFlat;
				out.push_back(Cut{ g, Lerp(prev, cur, std::clamp(u, 0.0f, 1.0f)) });
			}
			prev = cur;
		}
	}
	std::sort(out.begin(), out.end(), [](const Cut& a, const Cut& b) { return a.g < b.g; });
	std::vector<Cut> uniq;
	for (const Cut& c : out)
		if (uniq.empty() || c.g - uniq.back().g > 1e-4f) uniq.push_back(c);
	return uniq;
}

float NearestParam(const std::vector<Cubic>& target, Vec2 p, float* outDist)
{
	float best = 0.0f, bestD = 1e30f;
	for (size_t i = 0; i < target.size(); ++i)
	{
		Vec2 prev = Bez(target[i], 0.0f);
		for (int k = 1; k <= kNearest; ++k)
		{
			const Vec2 cur = Bez(target[i], static_cast<float>(k) / kNearest);
			const Vec2 d = Sub(cur, prev);
			const float dd = d.x * d.x + d.y * d.y;
			float u = dd > 1e-12f ? ((p.x - prev.x) * d.x + (p.y - prev.y) * d.y) / dd : 0.0f;
			u = std::clamp(u, 0.0f, 1.0f);
			const float dist = Dist(p, Lerp(prev, cur, u));
			if (dist < bestD)
			{
				bestD = dist;
				best = static_cast<float>(i) + (static_cast<float>(k - 1) + u) / kNearest;
			}
			prev = cur;
		}
	}
	if (outDist) *outDist = bestD;
	return best;
}

TrimResult TrimAtClick(const SubPath& target, const std::vector<Cubic>& cutters, bool extendCutters, Vec2 click)
{
	TrimResult res;
	const std::vector<Cubic> segs = ToCubics(target);
	if (segs.empty()) return res;
	const float N = static_cast<float>(segs.size());
	const std::vector<Cut> cuts = FindCuts(segs, cutters, extendCutters);
	if (cuts.empty()) return res;

	const float gc = NearestParam(segs, click);
	bool hasPrev = false, hasNext = false;
	float prev = 0.0f, next = 0.0f;
	for (const Cut& c : cuts)
	{
		if (c.g < gc) { prev = c.g; hasPrev = true; }
		else if (!hasNext) { next = c.g; hasNext = true; }
	}

	if (target.closed)
	{
		// 닫힌 경로: 클릭한 구간(prev..next, 감싸는 쪽 포함)을 빼고 나머지를 열린 경로 하나로 남긴다
		if (cuts.size() < 2) return res;
		if (!hasPrev) prev = cuts.back().g;
		if (!hasNext) next = cuts.front().g;
		const std::vector<Cubic> kept = Extract(segs, next, prev, true);
		if (kept.empty()) return res;
		res.pieces.push_back(FromCubics(kept, false));
		res.ok = true;
		return res;
	}

	// 열린 경로: 클릭한 구간을 빼고 양쪽 끝 조각을 남긴다
	if (hasPrev)
	{
		const std::vector<Cubic> a = Extract(segs, 0.0f, prev, false);
		if (!a.empty()) res.pieces.push_back(FromCubics(a, false));
	}
	if (hasNext)
	{
		const std::vector<Cubic> b = Extract(segs, next, N, false);
		if (!b.empty()) res.pieces.push_back(FromCubics(b, false));
	}
	res.ok = true;
	return res;
}

bool ExtendEnd(const SubPath& target, const std::vector<Cubic>& cutters, bool virtualCuts, Vec2 click, float tol, SubPath& out)
{
	if (target.closed) return false;
	const std::vector<Cubic> segs = ToCubics(target);
	if (segs.empty()) return false;
	const Cubic& first = segs.front();
	const Cubic& last = segs.back();
	const float dS = Dist(click, first.p0), dE = Dist(click, last.p3);
	const bool atEnd = dE <= dS;
	if ((std::min)(dS, dE) > tol) return false;

	// 끝에서 바깥쪽 방향 (제어점이 끝과 같으면 반대쪽 제어점을 쓴다)
	Vec2 origin = atEnd ? last.p3 : first.p0;
	Vec2 dir = atEnd ? Sub(last.p3, last.p2) : Sub(first.p0, first.p1);
	if (Len(dir) < 1e-6f) dir = atEnd ? Sub(last.p3, last.p0) : Sub(first.p0, first.p3);
	const float dl = Len(dir);
	if (dl < 1e-6f) return false;
	dir = Vec2{ dir.x / dl, dir.y / dl };
	const float kBig = 1.0e6f;
	const Vec2 farPt{ origin.x + dir.x * kBig, origin.y + dir.y * kBig };

	// 절단 개체 조각 (직선은 virtualCuts 면 무한 직선)
	float bestS = -1.0f;
	auto consider = [&](Vec2 p, Vec2 q, bool inf) {
		float u = 0.0f, v = 0.0f;
		if (!SegInter(origin, farPt, p, q, inf, u, v)) return;
		const float s = u * kBig;
		if (s > 1e-3f && (bestS < 0.0f || s < bestS)) bestS = s;
	};
	for (const Cubic& c : cutters)
	{
		if (IsStraight(c))
		{
			consider(c.p0, c.p3, virtualCuts);
			continue;
		}
		Vec2 prev = c.p0;
		for (int k = 1; k <= kFlat; ++k)
		{
			const Vec2 cur = Bez(c, static_cast<float>(k) / kFlat);
			consider(prev, cur, false);
			prev = cur;
		}
	}
	if (bestS < 0.0f) return false;

	const Vec2 hit{ origin.x + dir.x * bestS, origin.y + dir.y * bestS };
	out = target;
	BezierNode nb;
	nb.anchor = hit;   // hIn/hOut 는 0 -> 직선 연장
	if (atEnd)
	{
		out.nodes.back().hOut = Vec2{ 0.0f, 0.0f };
		out.nodes.push_back(nb);
	}
	else
	{
		out.nodes.front().hIn = Vec2{ 0.0f, 0.0f };
		out.nodes.insert(out.nodes.begin(), nb);
	}
	return true;
}

} // namespace Trim
} // namespace ge
