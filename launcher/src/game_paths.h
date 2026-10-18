#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace game_paths {

inline std::filesystem::path FromUtf8(std::string_view text) {
  return std::filesystem::path(std::u8string_view(
      reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

inline bool IsRepositoryBuild(const std::filesystem::path& base_dir) {
  // Match this repository's build/game layout without searching other projects.
  std::error_code ec;
  return base_dir.filename() == "game" && base_dir.parent_path().filename() == "build" &&
         std::filesystem::is_regular_file(
             base_dir.parent_path().parent_path() / "broken_bond_manifest.toml", ec);
}

inline std::filesystem::path Find(const std::filesystem::path& base_dir,
                                  const std::string& custom_path) {
  namespace fs = std::filesystem;
  const auto resolve = [](const fs::path& path) -> fs::path {
    std::error_code ec;
    if (!fs::is_regular_file(path / "default.xex", ec)) return {};
    auto canonical = fs::canonical(path, ec);
    return ec ? fs::path{} : canonical;
  };
  // A selected folder is authoritative, even if it becomes unavailable.
  if (!custom_path.empty()) {
    auto path = FromUtf8(custom_path);
    return resolve(path.is_absolute() ? path : base_dir / path);
  }
  if (auto local = resolve(base_dir / "game"); !local.empty()) return local;
  if (IsRepositoryBuild(base_dir))
    return resolve(base_dir.parent_path().parent_path() / "game");
  return {};
}

inline std::filesystem::path UserDataRoot(const std::filesystem::path& base_dir) {
  return IsRepositoryBuild(base_dir) ? base_dir.parent_path() / "user" : base_dir / "user";
}

}  // namespace game_paths
