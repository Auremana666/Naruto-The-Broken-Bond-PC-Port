// Where the game is (first start). The player picks one of:
// - a disc image (.iso, or any file with an Xbox 360 disc file system), mounted as it is;
// - default.xex of extracted game files: its folder is mounted;
// - an archive (.zip, .rar, .7z or .zar): unpacked once into "<name> (unpacked)" next to it,
//   then the disc image or default.xex inside is used. ZIP, RAR and 7z go through the tar.exe
//   that comes with Windows; .zar (ZArchive, as Xenia uses) is read here.
// BrokenBondApp saves the result as game_data_root in naruto.toml, so the window shows once.

#include "pc/game_location.h"

#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <zstd.h>

#include <rex/logging.h>

#include "pc/pad_input.h"

namespace {

namespace fs = std::filesystem;

std::wstring Lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), ::towlower);
  return s;
}

// The file the player picks, or empty.
fs::path AskForFile() {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  fs::path picked;
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    const COMDLG_FILTERSPEC filters[] = {
        {L"Naruto: The Broken Bond (ISO, XEX, ZIP, RAR, 7Z, ZAR)",
         L"*.iso;*.xex;*.zip;*.rar;*.7z;*.zar"},
        {L"All files", L"*.*"},
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(L"Where is Naruto: The Broken Bond? Pick the ISO, default.xex or an archive");
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR name = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
        picked = name;
        CoTaskMemFree(name);
      }
      item->Release();
    }
    dialog->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return picked;
}

// Whether a file holds an Xbox 360 disc file system ("MICROSOFT*XBOX*MEDIA" at sector 32 of
// one of the partitions disc images start at).
bool IsDiscImage(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  static constexpr uint64_t kPartitions[] = {0, 0xFD90000, 0x2080000, 0x18300000};
  static constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
  for (uint64_t partition : kPartitions) {
    char magic[20] = {};
    in.clear();
    in.seekg(static_cast<std::streamoff>(partition + 32 * 2048));
    if (in.read(magic, 20) && std::equal(magic, magic + 20, kMagic)) return true;
  }
  return false;
}

// The game inside unpacked files: the folder of a default.xex, else a disc image.
fs::path FindGameIn(const fs::path& folder) {
  std::error_code ec;
  fs::path image;
  for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::end(it);
       it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::wstring name = Lower(it->path().filename().wstring());
    if (name == L"default.xex") return it->path().parent_path();
    if (image.empty() && Lower(it->path().extension().wstring()) == L".iso") image = it->path();
  }
  return image;
}

// A small window that says the game is being unpacked, until Close.
class UnpackWindow {
 public:
  explicit UnpackWindow(std::wstring archive) {
    thread_ = std::thread([this, archive = std::move(archive)] { Run(archive); });
  }
  ~UnpackWindow() {
    done_ = true;
    if (thread_.joinable()) thread_.join();
  }
  void SetProgress(int percent) { percent_ = percent; }

 private:
  void Run(const std::wstring& archive) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NarutoUnpackWindow";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32514));  // IDC_WAIT
    RegisterClassW(&wc);
    const int w = 520, h = 140;
    HWND window = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Naruto The Broken Bond PC",
                                  WS_POPUP | WS_CAPTION | WS_VISIBLE,
                                  (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                                  (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    HWND text = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 16, 16, w - 40, h - 60,
                                window, nullptr, wc.hInstance, nullptr);
    SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    int shown = -2;
    while (!done_) {
      if (percent_ != shown) {
        shown = percent_;
        std::wstring line = L"Unpacking the game, this only happens once and can take a few minutes.\n\n" +
                            fs::path(archive).filename().wstring();
        if (shown >= 0) line += L"  " + std::to_wstring(shown) + L"%";
        SetWindowTextW(text, line.c_str());
      }
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      Sleep(50);
    }
    DestroyWindow(window);
  }

  std::thread thread_;
  std::atomic<bool> done_{false};
  std::atomic<int> percent_{-1};
};

// ZIP, RAR and 7z: Windows' own tar.exe (bsdtar) unpacks them.
bool UnpackWithTar(const fs::path& archive, const fs::path& folder) {
  wchar_t system[MAX_PATH] = {};
  GetSystemDirectoryW(system, MAX_PATH);
  const fs::path tar = fs::path(system) / L"tar.exe";
  std::error_code ec;
  if (!fs::exists(tar, ec)) return false;
  std::wstring command = L"\"" + tar.wstring() + L"\" -xf \"" + archive.wstring() + L"\" -C \"" +
                         folder.wstring() + L"\"";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      nullptr, &si, &pi))
    return false;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  REXLOG_INFO("[game location] tar exit code {}", code);
  return code == 0;
}

// Paths past MAX_PATH need the "\\?\" prefix.
fs::path Long(const fs::path& path) {
  const std::wstring& text = path.native();
  if (text.size() < 240 || text.rfind(LR"(\\?\)", 0) == 0) return path;
  return fs::path(LR"(\\?\)" + fs::absolute(path).wstring());
}

// ZArchive (.zar): 64 KiB blocks, each stored raw or as a zstd frame, and a file tree; all
// numbers big-endian. The footer is the last 144 bytes.
uint64_t BE(const uint8_t* p, int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
  return v;
}

bool UnpackZar(const fs::path& archive, const fs::path& folder, UnpackWindow& window) {
  constexpr uint32_t kBlock = 64 * 1024, kMagic = 0x169F52D6, kVersion = 0x61BF3A01;
  std::ifstream in(archive, std::ios::binary);
  std::error_code ec;
  const uint64_t file_size = fs::file_size(archive, ec);
  if (!in || ec || file_size <= 144) return false;
  uint8_t footer[144];
  in.seekg(static_cast<std::streamoff>(file_size - 144));
  if (!in.read(reinterpret_cast<char*>(footer), 144)) return false;
  auto section = [&](int i, uint64_t* offset, uint64_t* size) {
    *offset = BE(footer + i * 16, 8);
    *size = BE(footer + i * 16 + 8, 8);
    return *offset + *size <= file_size;
  };
  if (BE(footer + 140, 4) != kMagic || BE(footer + 136, 4) != kVersion ||
      BE(footer + 128, 8) != file_size) {
    REXLOG_WARN("[game location] not a ZArchive: {}", archive.string());
    return false;
  }
  uint64_t data_offset, data_size, records_offset, records_size, names_offset, names_size,
      tree_offset, tree_size;
  if (!section(0, &data_offset, &data_size) || !section(1, &records_offset, &records_size) ||
      !section(2, &names_offset, &names_size) || !section(3, &tree_offset, &tree_size) ||
      records_size % 40 || tree_size % 16 || records_size > 0xFFFFFFFF ||
      names_size > 0x7FFFFFFF || tree_size > 0xFFFFFFFF) {
    REXLOG_WARN("[game location] ZArchive sections out of range");
    return false;
  }
  auto read = [&](uint64_t offset, uint64_t size) {
    std::vector<uint8_t> bytes(size);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    if (size && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
      bytes.clear();
    return bytes;
  };
  const std::vector<uint8_t> records = read(records_offset, records_size);
  const std::vector<uint8_t> names = read(names_offset, names_size);
  const std::vector<uint8_t> tree = read(tree_offset, tree_size);
  if (records.size() != records_size || names.size() != names_size || tree.size() != tree_size ||
      tree.size() < 16)
    return false;

  auto name_of = [&](uint32_t offset) {
    if (offset == 0x7FFFFFFF || offset >= names.size()) return std::string();
    uint32_t length = names[offset] & 0x7F;
    if (names[offset] & 0x80) {
      if (offset + 1 >= names.size()) return std::string();
      length |= uint32_t(names[offset + 1]) << 7;
      offset += 2;
    } else {
      ++offset;
    }
    if (offset + length > names.size()) return std::string();
    return std::string(reinterpret_cast<const char*>(names.data()) + offset, length);
  };

  // One block of the uncompressed payload, cached.
  std::vector<uint8_t> block(kBlock), packed(kBlock);
  uint64_t cached = UINT64_MAX;
  auto load_block = [&](uint64_t index) {
    if (index == cached) return true;
    const uint64_t record = index / 16, sub = index % 16;
    if ((record + 1) * 40 > records.size()) return false;
    const uint8_t* r = records.data() + record * 40;
    uint64_t offset = BE(r, 8);
    for (uint64_t i = 0; i < sub; ++i) offset += BE(r + 8 + i * 2, 2) + 1;
    const uint32_t size = static_cast<uint32_t>(BE(r + 8 + sub * 2, 2) + 1);
    if (offset + size > data_size) return false;
    in.clear();
    in.seekg(static_cast<std::streamoff>(data_offset + offset));
    if (size == kBlock) {
      if (!in.read(reinterpret_cast<char*>(block.data()), kBlock)) return false;
    } else {
      if (!in.read(reinterpret_cast<char*>(packed.data()), size)) return false;
      if (ZSTD_decompress(block.data(), kBlock, packed.data(), size) != kBlock) return false;
    }
    cached = index;
    return true;
  };

  uint64_t total = 0, written = 0;
  for (size_t i = 0; i < tree.size() / 16; ++i) {
    const uint8_t* e = tree.data() + i * 16;
    if (BE(e, 4) & 0x80000000) total += BE(e + 8, 4) | ((BE(e + 12, 4) & 0xFFFF0000) << 16);
  }

  // Walks the tree from the root (entry 0); a directory's children are a contiguous range.
  bool ok = true;
  auto walk = [&](auto&& self, uint32_t index, const fs::path& path, int depth) -> void {
    if (!ok || depth > 64 || (index + 1) * 16ull > tree.size()) {
      ok = false;
      return;
    }
    const uint8_t* e = tree.data() + index * 16;
    const uint32_t name_and_flag = static_cast<uint32_t>(BE(e, 4));
    if (!(name_and_flag & 0x80000000)) {
      fs::create_directories(Long(path), ec);
      const uint32_t first = static_cast<uint32_t>(BE(e + 4, 4)), count = static_cast<uint32_t>(BE(e + 8, 4));
      for (uint32_t c = 0; c < count && ok; ++c) {
        const uint8_t* child = tree.data() + uint64_t(first + c) * 16;
        if (uint64_t(first + c + 1) * 16 > tree.size()) {
          ok = false;
          return;
        }
        const std::string name = name_of(static_cast<uint32_t>(BE(child, 4)) & 0x7FFFFFFF);
        if (name.empty() || name == "." || name == ".." ||
            name.find_first_of("/\\:") != std::string::npos) {
          ok = false;
          return;
        }
        self(self, first + c, path / fs::u8path(name), depth + 1);
      }
      return;
    }
    const uint64_t high = BE(e + 12, 4);
    const uint64_t offset = BE(e + 4, 4) | ((high & 0xFFFF) << 32);
    const uint64_t size = BE(e + 8, 4) | ((high & 0xFFFF0000) << 16);
    std::ofstream out(Long(path), std::ios::binary | std::ios::trunc);
    if (!out) {
      ok = false;
      return;
    }
    for (uint64_t done = 0; done < size && ok;) {
      const uint64_t at = offset + done;
      if (!load_block(at / kBlock)) {
        ok = false;
        return;
      }
      const uint64_t in_block = at % kBlock;
      const uint64_t step = std::min<uint64_t>(size - done, kBlock - in_block);
      out.write(reinterpret_cast<const char*>(block.data() + in_block), static_cast<std::streamsize>(step));
      done += step;
      written += step;
      if (total) window.SetProgress(static_cast<int>(written * 100 / total));
    }
    if (!out) ok = false;
  };
  if (BE(tree.data(), 4) & 0x80000000) return false;  // the root must be a directory
  walk(walk, 0, folder, 0);
  if (!ok) REXLOG_WARN("[game location] ZArchive unpacking failed after {} bytes", written);
  return ok;
}

// The game the picked file stands for, unpacking an archive first. Empty with an error shown
// when it is not the game.
fs::path Resolve(const fs::path& picked) {
  const std::wstring extension = Lower(picked.extension().wstring());
  std::error_code ec;
  if (extension == L".xex") {
    if (Lower(picked.filename().wstring()) != L"default.xex") {
      ShowGameError("Pick the game's default.xex file (in the folder of the extracted game files).");
      return {};
    }
    return picked.parent_path();
  }
  if (extension == L".zip" || extension == L".rar" || extension == L".7z" || extension == L".zar") {
    const fs::path folder = picked.parent_path() / (picked.stem().wstring() + L" (unpacked)");
    if (fs::path game = FindGameIn(folder); !game.empty()) return game;  // unpacked before
    fs::create_directories(folder, ec);
    bool unpacked;
    {
      UnpackWindow window(picked.wstring());
      unpacked = extension == L".zar" ? UnpackZar(picked, folder, window) : UnpackWithTar(picked, folder);
    }
    fs::path game = unpacked ? FindGameIn(folder) : fs::path();
    REXLOG_INFO("[game location] unpacked {} -> {} ({})", picked.string(), game.string(), unpacked);
    if (game.empty()) {
      ShowGameError(unpacked
                        ? "The archive was unpacked, but it has no ISO file or default.xex in it."
                        : "The archive could not be unpacked. Unpack it yourself (for example with "
                          "7-Zip), then pick the ISO file or default.xex.");
    }
    return game;
  }
  if (!IsDiscImage(picked)) {
    ShowGameError("This file is not an Xbox 360 disc image. Pick the game's ISO file, its "
                  "default.xex, or an archive (ZIP, RAR, 7Z, ZAR) that has one of them.");
    return {};
  }
  return picked;
}

}  // namespace

fs::path PickGameLocation() {
  for (;;) {
    const fs::path picked = AskForFile();
    if (picked.empty()) return {};
    REXLOG_INFO("[game location] picked {}", picked.string());
    if (fs::path game = Resolve(picked); !game.empty()) return game;
  }
}
