#pragma once

// The version of the port, as the launcher shows it (launcher/src/app_version.h):
// the first line of the optional broken_bond.version next to the executable
// (the packages write it), else the build's BROKENBOND_VERSION definition, else
// "dev". Letters, digits, dots, dashes and pluses only ("0.1.0-beta").
#include <string>

namespace broken_bond {
std::string PortVersion();
// The window title: "Naruto: The Broken Bond - v<version>", or "- dev".
std::string WindowTitle();
}  // namespace broken_bond
