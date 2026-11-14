#pragma once
// 공통 문자열·인코딩 유틸 (CoreUtil.cpp)
#include <cstdint>
#include <string>
#include <vector>

std::string WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& s);
std::string Base64Encode(const std::vector<uint8_t>& in);
