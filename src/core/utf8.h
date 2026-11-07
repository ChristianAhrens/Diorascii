/// @file core/utf8.h
/// @brief UTF-8 encoding and decoding.

#pragma once
#include <string>
#include <string_view>

namespace dio {

/// Decodes UTF-8 into code points. Invalid or truncated sequences become a question mark.
std::u32string decodeUtf8(std::string_view s);
/// Appends code point cp to out, encoded as UTF-8.
void appendUtf8(std::string& out, char32_t cp);

}  // namespace dio
