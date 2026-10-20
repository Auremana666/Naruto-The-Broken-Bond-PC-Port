#include "app_version.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace {

// A version is short and made of letters, digits, dots, dashes and pluses
// ("0.1.0-beta.3"); anything else in the file is not shown.
bool IsVersion(const std::string& text) {
  return !text.empty() && text.size() <= 64 &&
         std::all_of(text.begin(), text.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '.' || c == '-' || c == '+';
         });
}

}  // namespace

std::string AppVersion(const std::filesystem::path& base_dir) {
  std::ifstream file(base_dir / "broken_bond.version");
  std::string line;
  if (file && std::getline(file, line)) {
    const auto first = line.find_first_not_of(" \t\r\n");
    const auto last = line.find_last_not_of(" \t\r\n");
    if (first != std::string::npos) line = line.substr(first, last - first + 1);
    if (IsVersion(line)) return line;
  }
#ifdef BROKENBOND_VERSION
  return BROKENBOND_VERSION;
#else
  return {};
#endif
}
