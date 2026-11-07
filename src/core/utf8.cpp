/// @file core/utf8.cpp
/// @brief UTF-8 encoding and decoding.

#include "core/utf8.h"

namespace dio {

std::u32string decodeUtf8(std::string_view s) {
  std::u32string out;
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp;
    size_t n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 0x6) { cp = c & 0x1f; n = 2; }
    else if ((c >> 4) == 0xe) { cp = c & 0x0f; n = 3; }
    else if ((c >> 3) == 0x1e) { cp = c & 0x07; n = 4; }
    else { out.push_back(U'?'); ++i; continue; }
    if (i + n > s.size()) { out.push_back(U'?'); break; }
    for (size_t k = 1; k < n; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) { cp = U'?'; n = k; break; }
      cp = (cp << 6) | (cc & 0x3f);
    }
    out.push_back(cp);
    i += n;
  }
  return out;
}

void appendUtf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

}  // namespace dio
