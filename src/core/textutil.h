/// @file core/textutil.h
/// @brief Tiny text helpers shared by the file format parsers.

#pragma once
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/color.h"

namespace dio {

/// Returns s without leading and trailing spaces, tabs and carriage returns.
inline std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return std::string(s.substr(a, b - a));
}

/// ASCII lower-case copy of s.
inline std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// Splits s on runs of spaces and tabs. Empty tokens are dropped.
inline std::vector<std::string> splitWs(std::string_view s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\r') {
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

/// "#rrggbb" -> Rgb
inline bool parseHexColor(std::string_view s, Rgb& out) {
  if (s.size() != 7 || s[0] != '#') return false;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int v[6];
  for (int i = 0; i < 6; ++i) {
    v[i] = hex(s[1 + i]);
    if (v[i] < 0) return false;
  }
  out = {static_cast<uint8_t>(v[0] * 16 + v[1]), static_cast<uint8_t>(v[2] * 16 + v[3]),
         static_cast<uint8_t>(v[4] * 16 + v[5])};
  return true;
}

}  // namespace dio
