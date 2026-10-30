// Keyboard button prompts (PC Options > Controls > Button Prompts).
//
// The game's prompts are tags in its text ("Press <a> to jump."), which its Flash interface turns
// into Xbox button icons. With keyboard prompts on, every line whose text has a button tag is
// pointed at a copy where the tags are the bound keys ("Press [Space] to jump.").
//
// sub_8275D500 loads a text unit (one per language file, such as _fd190232.bin for English). It
// returns a unit with the number of hash slots at +0 and the slot array at +4; each slot holds a
// chain of 12-byte lines {id, text, next}. The text pointers point into the loaded file. The hook
// lets the loader run, then walks the unit and swaps the text pointer of each line with a button
// tag; the loaded file itself is never changed, so turning the option off points the lines back.
//
// In Automatic the prompts follow the device used last: a key or mouse button in the game's window
// shows the keys, a controller button or stick shows the Xbox buttons.
//
// The rewritten copies live in a guest heap block of their own. The same text is only stored
// once, so loading the unit again or rebinding a key adds a few new strings at most.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include "generated/default/broken_bond_init.h"
#include "icon_library.h"
#include "keyboard_prompts_core.h"
#include "pad_input.h"

REXCVAR_DEFINE_STRING(button_prompts, "auto", "Controls",
                      "Button prompts in the game's text and icons: auto (the device used last), "
                      "keyboard or controller")
    .allowed({"auto", "keyboard", "controller"})
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(log_button_prompts, false, "Controls",
                    "Log the text lines the keyboard prompts rewrite (diagnostics)");

extern "C" void __imp__sub_8275D500(PPCContext& __restrict, uint8_t*);
extern "C" void __imp__sub_8274C3F0(PPCContext& __restrict, uint8_t*);  // interface file opener
extern "C" void __imp__sub_82120040(PPCContext& __restrict, uint8_t*);  // game malloc(size, 0)
extern "C" void __imp__sub_82120A70(PPCContext& __restrict, uint8_t*);  // game free(ptr)

namespace {

constexpr uint32_t kMaxSlots = 1u << 16;
constexpr uint32_t kMaxChain = 4096;
constexpr uint32_t kMaxText = 4096;

// A line whose text has a button tag: where its text pointer is, and what it pointed at.
struct Line {
  uint32_t entry;     // guest address of the {id, text, next} line
  uint32_t original;  // the game's own text
  uint32_t shown;     // what the line points at now (original or one of our copies)
};

std::mutex g_mutex;
uint8_t* g_base = nullptr;
rex::memory::Memory* g_memory = nullptr;
std::vector<Line> g_lines;
std::unordered_map<std::string, uint32_t> g_copies;  // rewritten text -> guest address
std::unordered_set<uint32_t> g_copy_addresses;       // the guest addresses of those copies
std::string g_state;                                  // bindings the copies were made for
std::atomic<bool> g_watcher_started{false};

bool ValidGuest(uint32_t address) { return address >= 0x40000000u && address < 0xA0000000u; }

// Whether a guest page is committed, so reading a line that may have been freed is safe.
bool Committed(uint32_t address) {
  if (!ValidGuest(address)) return false;
  auto* heap = g_memory->LookupHeap(address);
  rex::memory::HeapAllocationInfo info{};
  return heap && heap->QueryRegionInfo(address & ~0xFFFu, &info) &&
         (info.state & rex::memory::kMemoryAllocationCommit);
}

uint32_t Load32(uint32_t address) {
  uint32_t raw;
  std::memcpy(&raw, g_base + address, sizeof(raw));
  return std::byteswap(raw);
}

void Store32(uint32_t address, uint32_t value) {
  const uint32_t raw = std::byteswap(value);
  std::memcpy(g_base + address, &raw, sizeof(raw));
}

// The text at a guest address, or false when it is not a reasonable string.
bool ReadText(uint32_t address, std::string* out) {
  if (!ValidGuest(address)) return false;
  const char* p = reinterpret_cast<const char*>(g_base + address);
  const void* end = std::memchr(p, 0, kMaxText);
  if (!end) return false;
  out->assign(p, static_cast<const char*>(end));
  return true;
}

bool HasButtonTag(std::string_view text) {
  for (size_t open = text.find('<'); open != std::string_view::npos;
       open = text.find('<', open + 1)) {
    const size_t close = text.find('>', open);
    if (close == std::string_view::npos) return false;
    if (keyboard_prompts::IsButtonTag(text.substr(open, close - open + 1))) return true;
  }
  return false;
}

std::string Keybind(std::string_view cvar) { return rex::cvar::GetFlagByName(cvar); }

bool ControllerConnected() {
  for (int i = 0; i < 4; ++i) {
    bool connected = false;
    ReadPadRawAt(i, &connected);
    if (connected) return true;
  }
  return false;
}

// The device used last (Automatic): 0 not known yet, 1 keyboard and mouse, 2 controller.
std::atomic<int> g_last_device{0};

bool GameInFront() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

void NoteLastDevice() {
  const PadRaw pad = ReadPadRaw();
  constexpr int kStick = 12000;
  if (pad.buttons || pad.left_trigger > 40 || pad.right_trigger > 40 || std::abs(pad.left_x) > kStick ||
      std::abs(pad.left_y) > kStick || std::abs(pad.right_x) > kStick || std::abs(pad.right_y) > kStick) {
    g_last_device.store(2);
    return;
  }
  if (!GameInFront()) return;
  for (int vk = 0x01; vk <= 0xFE; ++vk) {
    if (vk == VK_PACKET) continue;
    if (GetAsyncKeyState(vk) & 0x8000) {
      g_last_device.store(1);
      return;
    }
  }
}

bool KeyboardPromptsWanted() {
  const std::string mode = REXCVAR_GET(button_prompts);
  if (mode == "keyboard") return true;
  if (mode == "controller") return false;
  if (rex::cvar::GetFlagByName("mnk_mode") == "false") return false;
  const int last = g_last_device.load();
  return last ? last == 1 : !ControllerConnected();
}

bool MouseLook() { return rex::cvar::GetFlagByName("mnk_mouse") == "true"; }

// Everything the rewritten text depends on, to notice when it has to be redone.
std::string CurrentState() {
  if (!KeyboardPromptsWanted()) return "controller";
  std::string state = MouseLook() ? "keyboard mouse" : "keyboard";
  for (const char* cvar :
       {"keybind_a", "keybind_b", "keybind_x", "keybind_y", "keybind_left_shoulder",
        "keybind_right_shoulder", "keybind_left_trigger", "keybind_right_trigger",
        "keybind_start", "keybind_back", "keybind_dpad_up", "keybind_dpad_down",
        "keybind_dpad_left", "keybind_dpad_right", "keybind_lstick_up", "keybind_lstick_down",
        "keybind_lstick_left", "keybind_lstick_right", "keybind_rstick_up",
        "keybind_rstick_down", "keybind_rstick_left", "keybind_rstick_right"}) {
    state += '|';
    state += Keybind(cvar);
  }
  return state;
}

// Guest copy of a rewritten text (one per distinct text). Caller holds g_mutex.
uint32_t CopyFor(const std::string& text) {
  if (auto it = g_copies.find(text); it != g_copies.end()) return it->second;
  const uint32_t size = static_cast<uint32_t>(text.size() + 1);
  const uint32_t address = g_memory->SystemHeapAlloc(size, 4);
  if (!address) return 0;
  std::memcpy(g_base + address, text.c_str(), size);
  g_copies.emplace(text, address);
  g_copy_addresses.insert(address);
  return address;
}

// Points one line at the text for the current state. Caller holds g_mutex.
void Apply(Line& line, bool keyboard, bool mouse_look) {
  // The line may have been freed with its unit; only touch it while it still shows our pointer.
  if (!Committed(line.entry) || Load32(line.entry + 4) != line.shown) return;
  uint32_t target = line.original;
  if (keyboard) {
    std::string text, expanded;
    if (!ReadText(line.original, &text)) return;
    if (keyboard_prompts::ExpandButtonTags(text, Keybind, mouse_look, &expanded)) {
      if (const uint32_t copy = CopyFor(expanded)) target = copy;
      if (REXCVAR_GET(log_button_prompts)) {
        REXLOG_INFO("[prompts] {:08X}: \"{}\" -> \"{}\"", line.entry, text, expanded);
      }
    }
  }
  if (target != line.shown) {
    Store32(line.entry + 4, target);
    line.shown = target;
  }
}

// The button icons (see the icon library part below). Each icon library the game loaded (the
// main one, and one per mini-game) has its own range of marker ids. When the device, the
// bindings or the option change, the guest heap is searched for the markers and the values after
// them are rewritten; a library that loads later starts with the values of that moment.
struct Library {
  std::string url;
  std::vector<icon_library::Icon> icons;
  std::string state;  // the state its values were made or last written for
  std::chrono::steady_clock::time_point loaded;
};
std::vector<Library> g_libraries;  // slot = marker id / kIdsPerLibrary
const uint8_t* g_font = nullptr;

// Whether a value follows "push marker; pop; push" there.
bool IsValueAt(uint32_t value) {
  const uint8_t* p = g_base + value - 13;
  return p[0] == 0x96 && p[1] == 5 && p[2] == 0 && p[3] == 7 && p[6] == 0x5E && p[7] == 0x7E &&
         p[8] == 0x17 && p[9] == 0x96 && p[10] == 5 && p[11] == 0 && p[12] == 7;
}

uint32_t StoredValue(uint32_t at) {
  const uint8_t* p = g_base + at;
  return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

// Values to write, found by the watcher and written by the game thread (WriteIconValues): the
// game frees and reuses memory while it runs, so each value is checked again right before it is
// written, on the thread that does the freeing.
std::mutex g_pending_mutex;
std::vector<std::pair<uint32_t, uint32_t>> g_pending;  // value address, value

// Finds every icon value in the guest heap for a state. Runs without g_mutex (the search takes
// a moment); libraries is a copy.
void ApplyIcons(const std::vector<Library>& libraries, const std::string& state) {
  const bool keyboard = state != "controller";
  const bool mouse = MouseLook();
  // Values by marker id, made when first needed.
  std::unordered_map<uint32_t, std::vector<int32_t>> values;
  auto value_for = [&](uint32_t id, int32_t* out) {
    const uint32_t slot = id / icon_library::kIdsPerLibrary;
    if (slot >= libraries.size()) return false;
    const uint32_t local = id % icon_library::kIdsPerLibrary;
    const uint32_t index = local / icon_library::kFields, field = local % icon_library::kFields;
    const auto& icons = libraries[slot].icons;
    if (index >= icons.size()) return false;
    const uint32_t key = slot * icon_library::kIdsPerLibrary + index;
    auto it = values.find(key);
    if (it == values.end()) {
      const icon_library::Icon& icon = icons[index];
      it = values.emplace(key, icon_library::IconValues(
                                   icon, keyboard_prompts::IconLabel(icon.name, Keybind, mouse),
                                   keyboard, g_font)).first;
    }
    if (field >= it->second.size()) return false;
    *out = it->second[field];
    return true;
  };
  // Readable host pages only (the guest heaps' own bookkeeping does not say what is mapped).
  auto readable = [](uint32_t page) {
    MEMORY_BASIC_INFORMATION mbi{};
    return VirtualQuery(g_base + page, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
           !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
  };
  std::vector<std::pair<uint32_t, uint32_t>> found;
  for (uint32_t page = 0x40000000u; page < 0x70000000u; page += 0x1000) {
    if (!readable(page)) continue;
    uint8_t* p = g_base + page;
    // A value may end on the next page.
    const uint32_t end = readable(page + 0x1000) ? 0x1000 + 13 : 0x1000 - 4;
    for (uint32_t i = 13; i < end; ++i) {
      if (p[i - 7] != 0x5E || p[i - 6] != 0x7E) continue;
      if (!IsValueAt(page + i)) continue;
      int32_t value;
      if (!value_for(p[i - 9] | (p[i - 8] << 8), &value)) continue;
      const uint32_t v = static_cast<uint32_t>(icon_library::kValueBias + value);
      const int32_t now = static_cast<int32_t>(StoredValue(page + i));
      if (now == static_cast<int32_t>(v) || now < icon_library::kValueBias - icon_library::kValueRange ||
          now > icon_library::kValueBias + icon_library::kValueRange)
        continue;
      found.push_back({page + i, v});
    }
  }
  REXLOG_INFO("[prompts] icons: {} values to write ({})", found.size(), keyboard ? "keyboard" : "controller");
  std::lock_guard lock(g_pending_mutex);
  g_pending = std::move(found);
}

void ApplyAll() {
  std::vector<Library> libraries;
  std::string icon_state;
  {
    std::lock_guard lock(g_mutex);
    if (!g_base) return;
    const std::string state = CurrentState();
    // Icons: when the state changed, once each library has had a moment to be read by the game.
    const auto now = std::chrono::steady_clock::now();
    bool due = false, settled = true;
    for (const Library& library : g_libraries) {
      if (library.state != state) due = true;
      if (now - library.loaded < std::chrono::seconds(2)) settled = false;
    }
    if (due && settled && g_font) {
      for (Library& library : g_libraries) library.state = state;
      libraries = g_libraries;
      icon_state = state;
    }
  }
  if (!libraries.empty()) ApplyIcons(libraries, icon_state);
  std::lock_guard lock(g_mutex);
  const std::string state = CurrentState();
  if (state == g_state) return;
  g_state = state;
  const bool keyboard = state != "controller";
  const bool mouse = MouseLook();
  // Forget lines whose unit was freed or reused; the rest get the new text.
  std::erase_if(g_lines, [](const Line& line) {
    return !Committed(line.entry) || Load32(line.entry + 4) != line.shown;
  });
  for (Line& line : g_lines) Apply(line, keyboard, mouse);
  REXLOG_INFO("[prompts] button prompts: {} ({} lines)", keyboard ? "keyboard" : "controller",
              g_lines.size());
}

// Finds the lines with button tags in a freshly loaded text unit.
void Collect(uint32_t unit) {
  if (!ValidGuest(unit)) return;
  const uint32_t slots = Load32(unit);
  const uint32_t array = Load32(unit + 4);
  if (slots == 0 || slots > kMaxSlots || !ValidGuest(array)) return;
  std::lock_guard lock(g_mutex);
  const bool keyboard = KeyboardPromptsWanted();
  const bool mouse = MouseLook();
  size_t found = 0;
  for (uint32_t i = 0; i < slots; ++i) {
    uint32_t entry = Load32(array + i * 4);
    for (uint32_t n = 0; entry && n < kMaxChain; ++n) {
      if (!ValidGuest(entry)) break;
      const uint32_t text = Load32(entry + 4);
      std::string value;
      if (!g_copy_addresses.contains(text) && ReadText(text, &value) && HasButtonTag(value)) {
        g_lines.push_back({entry, text, text});
        Apply(g_lines.back(), keyboard, mouse);
        ++found;
      }
      entry = Load32(entry + 8);
    }
  }
  if (found) {
    REXLOG_INFO("[prompts] text unit {:08X}: {} lines with button prompts", unit, found);
  }
}

}  // namespace

void WriteIconValues() {
  std::vector<std::pair<uint32_t, uint32_t>> pending;
  {
    std::lock_guard lock(g_pending_mutex);
    if (g_pending.empty()) return;
    pending.swap(g_pending);
  }
  for (const auto& [at, v] : pending) {
    // Still the script's value: its pattern, and a stored value (not something the game wrote
    // there since, such as a heap link in memory it freed).
    if (!IsValueAt(at)) continue;
    const int32_t now = static_cast<int32_t>(StoredValue(at));
    if (now < icon_library::kValueBias - icon_library::kValueRange ||
        now > icon_library::kValueBias + icon_library::kValueRange)
      continue;
    const uint8_t bytes[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    std::memcpy(g_base + at, bytes, 4);
  }
}

namespace {

// Picks up rebinding, the option itself and controllers being plugged in or out.
void StartWatcher() {
  if (g_watcher_started.exchange(true)) return;
  std::thread([] {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      NoteLastDevice();
      ApplyAll();
    }
  }).detach();
}

}  // namespace

REX_HOOK_RAW(sub_8275D500) {
  __imp__sub_8275D500(ctx, base);
  const uint32_t unit = ctx.r3.u32;
  if (!unit) return;
  if (!g_memory) {
    auto* ts = rex::runtime::ThreadState::Get();
    if (!ts) return;
    g_memory = ts->memory();
    g_base = base;
  }
  {
    std::lock_guard lock(g_mutex);
    if (g_state.empty()) g_state = CurrentState();
  }
  Collect(unit);
  StartWatcher();
}

// Button icons. The interface movies import their button icons from one library,
// _ExternalGenericIcons.gfx, which the game's file opener (sub_8274C3F0) loads whole into a memory
// file: buffer at +0x210, size at +0x214, read position at +0x218 (its Read copies from there and
// its Close frees the buffer). The buffer is swapped for a rewritten library where each still
// button icon holds both the button and a keycap whose letters its script places (icon_library.h).
// The library is loaded once when the game starts; the watcher then writes the icons' values into
// the game's copy of their scripts whenever the device, the bindings or the option change.
namespace {

constexpr uint32_t kFileBuffer = 0x210, kFileSize = 0x214, kFilePosition = 0x218;
constexpr int kLabelFontResource = 105;  // Titan One, the PC Options menu font

bool EndsWithNoCase(std::string_view text, std::string_view suffix) {
  if (text.size() < suffix.size()) return false;
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[text.size() - suffix.size() + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

void ReplaceIconLibrary(PPCContext& ctx, uint8_t* base, uint32_t file, const std::string& url) {
  g_base = base;
  const uint32_t data = Load32(file + kFileBuffer);
  const uint32_t size = Load32(file + kFileSize);
  if (!ValidGuest(data) || size < 8 || size > (16u << 20)) {
    REXLOG_WARN("[prompts] icon library: unexpected file buffer {:08X} ({} bytes)", data, size);
    return;
  }
  const void* font = nullptr;
  size_t font_size = 0;
  if (!ReadResource(kLabelFontResource, &font, &font_size)) {
    REXLOG_WARN("[prompts] icon library: the label font is missing");
    return;
  }
  const bool mouse = MouseLook();
  // The library's slot: the same file keeps its slot when the game loads it again.
  size_t slot;
  std::string state;
  {
    std::lock_guard lock(g_mutex);
    state = CurrentState();
    slot = std::find_if(g_libraries.begin(), g_libraries.end(),
                        [&](const Library& l) { return l.url == url; }) - g_libraries.begin();
    if (slot == g_libraries.size()) {
      if (slot >= 0x10000 / icon_library::kIdsPerLibrary) {
        REXLOG_WARN("[prompts] icon library {}: too many libraries", url);
        return;
      }
      g_libraries.push_back({url, {}, state, {}});
    }
  }
  const icon_library::Result result = icon_library::RewriteIconLibrary(
      g_base + data, size,
      [mouse](std::string_view icon) { return keyboard_prompts::IconLabel(icon, Keybind, mouse); },
      static_cast<const uint8_t*>(font), font_size, state != "controller",
      static_cast<uint16_t>(slot * icon_library::kIdsPerLibrary));
  if (result.movie.empty()) {
    REXLOG_WARN("[prompts] icon library kept as it is: {}", result.error);
    return;
  }
  // The new buffer comes from the game's own allocator, so the file's Close frees it as usual.
  const PPCContext saved = ctx;
  ctx.r3.u64 = result.movie.size();
  ctx.r4.u64 = 0;
  __imp__sub_82120040(ctx, base);
  const uint32_t copy = ctx.r3.u32;
  ctx = saved;
  if (!ValidGuest(copy)) {
    REXLOG_WARN("[prompts] icon library: cannot allocate {} bytes", result.movie.size());
    return;
  }
  std::memcpy(g_base + copy, result.movie.data(), result.movie.size());
  Store32(file + kFileBuffer, copy);
  Store32(file + kFileSize, static_cast<uint32_t>(result.movie.size()));
  Store32(file + kFilePosition, 0);
  ctx.r3.u64 = data;
  __imp__sub_82120A70(ctx, base);
  ctx = saved;
  {
    std::lock_guard lock(g_mutex);
    Library& library = g_libraries[slot];
    library.icons = result.list;
    library.state = state;
    library.loaded = std::chrono::steady_clock::now();
    g_font = static_cast<const uint8_t*>(font);
  }
  REXLOG_INFO("[prompts] icon library {}: {} button icons show keys ({} -> {} bytes)", url,
              result.icons, size, result.movie.size());
}

}  // namespace

REX_HOOK_RAW(sub_8274C3F0) {
  std::string url;
  if (ctx.r4.u32) {
    const char* p = reinterpret_cast<const char*>(base + ctx.r4.u32);
    url.assign(p, strnlen(p, 260));
  }
  __imp__sub_8274C3F0(ctx, base);
  const uint32_t file = ctx.r3.u32;
  if (!file || !EndsWithNoCase(url, "_ExternalGenericIcons.gfx")) return;
  if (!g_memory) {
    auto* ts = rex::runtime::ThreadState::Get();
    if (ts) g_memory = ts->memory();
  }
  const PPCContext saved = ctx;
  ReplaceIconLibrary(ctx, base, file, url);
  ctx = saved;
}
