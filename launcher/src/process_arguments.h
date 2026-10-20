#pragma once

#include <string>
#include <string_view>

// Quote one CreateProcess argument, including paths ending in a backslash.
inline std::wstring QuoteProcessArgument(std::wstring_view value) {
  std::wstring result = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    result.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
    if (character == L'"') result += L'\\';
    result += character;
    backslashes = 0;
  }
  result.append(backslashes * 2, L'\\');
  result += L'"';
  return result;
}
