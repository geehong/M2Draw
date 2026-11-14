// 공통 문자열·인코딩 유틸 (UTF-8 ↔ UTF-16, Base64). 여러 파일에 복사되어 있던 것을 한 곳으로 모았다
#include "pch.h"
#include "CoreUtil.h"

std::string WideToUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	if (n <= 0) return {};
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0) return {};
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}

std::string Base64Encode(const std::vector<uint8_t>& in)
{
	static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < in.size(); i += 3) {
		const unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
		out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
	}
	if (i + 1 == in.size()) {
		const unsigned v = in[i] << 16;
		out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += "==";
	} else if (i + 2 == in.size()) {
		const unsigned v = (in[i] << 16) | (in[i + 1] << 8);
		out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += '=';
	}
	return out;
}