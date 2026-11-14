#pragma once

// 최근 사용 색 (스포이드로 뽑은 색, 팔레트에서 고른 색). 앱 설정(프로필)에 "RRGGBB,RRGGBB,..." 로 저장한다.
// 투명·검정·흰색은 기본 줄에 따로 있으므로 최근 색에는 넣지 않는다.
#include <d2d1.h>
#include <afxwin.h>
#include <vector>
#include <algorithm>
#include <cmath>

namespace RecentColors {

constexpr size_t kMax = 8;

inline unsigned ToByte(float v)
{
	return static_cast<unsigned>(std::lround((std::clamp)(v, 0.0f, 1.0f) * 255.0f));
}

inline bool IsSpecial(D2D1_COLOR_F c)
{
	const float sum = c.r + c.g + c.b;
	return c.a <= 0.001f || sum < 0.02f || sum > 2.98f;   // 투명 / 검정 / 흰색
}

inline std::vector<D2D1_COLOR_F> Load()
{
	std::vector<D2D1_COLOR_F> out;
	if (!AfxGetApp()) return out;
	CString s = AfxGetApp()->GetProfileString(_T("Palette"), _T("Recent"), _T(""));
	int pos = 0;
	while (pos < s.GetLength() && out.size() < kMax) {
		CString tok = s.Tokenize(_T(","), pos);
		if (tok.GetLength() != 6) continue;
		unsigned v = 0;
		if (_stscanf_s(tok, _T("%06x"), &v) != 1) continue;
		out.push_back(D2D1::ColorF(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, 1.0f));
	}
	return out;
}

// 색을 맨 앞에 넣는다 (같은 색이 있으면 그 자리를 옮김). 특수색은 무시
inline void Add(D2D1_COLOR_F c)
{
	if (IsSpecial(c) || !AfxGetApp()) return;
	const unsigned v = (ToByte(c.r) << 16) | (ToByte(c.g) << 8) | ToByte(c.b);
	std::vector<unsigned> list{ v };
	CString s = AfxGetApp()->GetProfileString(_T("Palette"), _T("Recent"), _T(""));
	int pos = 0;
	while (pos < s.GetLength() && list.size() < kMax) {
		CString tok = s.Tokenize(_T(","), pos);
		unsigned old = 0;
		if (tok.GetLength() == 6 && _stscanf_s(tok, _T("%06x"), &old) == 1 && old != v) list.push_back(old);
	}
	CString out;
	for (size_t i = 0; i < list.size(); ++i) {
		CString one;
		one.Format(_T("%06X"), list[i]);
		if (i) out += _T(",");
		out += one;
	}
	AfxGetApp()->WriteProfileString(_T("Palette"), _T("Recent"), out);
}

} // namespace RecentColors
