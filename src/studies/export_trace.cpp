// broken_bond - ReXGlue Recompiled Project
//
// Study: the file reads and the XMA voices the title starts and stops, to find
// what a scripted scene waits for when its animations stop while the game keeps
// running (the coffins of Orochimaru's summoning, on both renderers). With
// REX_EXPORT_TRACE=<file>, the native host's export guard (host/exports.cpp)
// hands every kernel export call here before and after the SDK runs it; this
// writes the file opens with their paths, the reads with handle, offset, length
// and status, the closes of traced files, and the XMA calls that create,
// initialize, feed and release contexts. From the polls of the input buffers it
// writes how long the decoder took to consume each buffer the title marked
// valid, and every 5 s the buffers still valid after 5 s. Times are
// milliseconds since the first record. Native mode only (the SDK units come in
// through kernel_io_exports.cpp and kernel_xma_exports.cpp).

#include "studies/export_trace.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/system/thread_state.h>

namespace studies {
namespace {

struct Trace {
  std::mutex mutex;
  std::FILE* out = nullptr;
  std::chrono::steady_clock::time_point start;
  std::unordered_map<uint32_t, std::string> files;  // Open file handles -> path.
};
std::string dump_dir;  // <REX_EXPORT_TRACE>.packets: the last packets of ended streams.

Trace* Open() {
  static Trace* trace = [] {
    const char* path = std::getenv("REX_EXPORT_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* t = new Trace;
    t->out = std::fopen(path, "w");
    if (!t->out) {
      delete t;
      return static_cast<Trace*>(nullptr);
    }
    t->start = std::chrono::steady_clock::now();
    dump_dir = std::string(path) + ".packets";
    std::error_code error;
    std::filesystem::create_directories(dump_dir, error);
    return t;
  }();
  return trace;
}

uint8_t* Guest(uint32_t address) {
  auto* state = rex::runtime::ThreadState::Get();
  return state && address ? state->memory()->TranslateVirtual<uint8_t*>(address) : nullptr;
}
uint32_t Load32(uint32_t address) {
  const uint8_t* p = Guest(address);
  return p ? _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p)) : 0;
}
uint16_t Load16(uint32_t address) {
  const uint8_t* p = Guest(address);
  return p ? _byteswap_ushort(*reinterpret_cast<const uint16_t*>(p)) : 0;
}
uint64_t Load64(uint32_t address) {
  const uint8_t* p = Guest(address);
  return p ? _byteswap_uint64(*reinterpret_cast<const uint64_t*>(p)) : 0;
}

// X_OBJECT_ATTRIBUTES: +4 the X_ANSI_STRING name (length, maximum, buffer).
std::string ObjectName(uint32_t attributes) {
  const uint32_t name = Load32(attributes + 4);
  if (!name) return {};
  const uint16_t length = Load16(name);
  const uint8_t* text = Guest(Load32(name + 4));
  return text ? std::string(reinterpret_cast<const char*>(text), length) : std::string();
}

uint32_t Thread() {
  auto* state = rex::runtime::ThreadState::Get();
  return state ? state->thread_id() : 0;
}

void Write(Trace& t, const std::string& line) {
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t.start).count();
  std::fprintf(t.out, "%lld thread=%u %s\n", ms, Thread(), line.c_str());
  std::fflush(t.out);
}

// XMAEnableContext and XMADisableContext are left out: the title's audio engine
// calls them thousands of times per second.
bool IsXmaControl(std::string_view name) {
  return name == "XMACreateContext" || name == "XMAReleaseContext" || name == "XMAInitializeContext" ||
         name == "XMASetLoopData" || name == "XMASetInputBuffer0" || name == "XMASetInputBuffer1" ||
         name == "XMASetInputBuffer0Valid" || name == "XMASetInputBuffer1Valid" || name == "XMABlockWhileInUse" ||
         name == "XMASetInputBufferReadOffset";
}
bool IsXmaBufferPoll(std::string_view name) {
  return name == "XMAIsInputBuffer0Valid" || name == "XMAIsInputBuffer1Valid";
}
// The other queries a title makes of a context, counted (with their last
// result) only for a context that ended its input without being released.
bool IsXmaQuery(std::string_view name) {
  return name == "XMAIsOutputBufferValid" || name == "XMAGetOutputBufferWriteOffset" ||
         name == "XMAGetOutputBufferReadOffset" || name == "XMAGetInputBufferReadOffset" ||
         name == "XMAGetPacketMetadata" || name == "XMASetOutputBufferValid" ||
         name == "XMASetOutputBufferReadOffset" || name == "XMAEnableContext" || name == "XMADisableContext";
}

// Per context: the last input buffers the title marked valid (their packets,
// copied at the call), and whether it ended its input without a release.
struct ContextStream {
  long long last_input_ms = 0;
  bool released = false;
  bool dumped = false;
  std::vector<std::vector<uint8_t>> buffers;  // The last kKeptBuffers, oldest first.
  std::map<std::string, std::pair<uint64_t, uint32_t>> queries;  // While ended: name -> (calls, last result).
};
constexpr size_t kKeptBuffers = 3;
constexpr long long kEndedMs = 3000;
std::unordered_map<uint32_t, ContextStream> streams;

uint8_t* GuestPhysical(uint32_t address) {
  auto* state = rex::runtime::ThreadState::Get();
  return state && address ? state->memory()->TranslatePhysical<uint8_t*>(address) : nullptr;
}

// The context as the decoder sees it (XMA_CONTEXT_DATA, rex/audio/xma/context.h:
// big-endian dwords, bit fields from the least significant bit).
std::string ContextState(uint32_t context) {
  const uint32_t d0 = Load32(context), d1 = Load32(context + 4), d2 = Load32(context + 8),
                 d3 = Load32(context + 12), d4 = Load32(context + 16), d9 = Load32(context + 36);
  char text[400];
  std::snprintf(text, sizeof(text),
                "in0=%u/%u in1=%u/%u current=%u read_offset=%u error=%u/%u loop=%u loop_start=%u loop_end=%u "
                "loop_subframes=%u/%u out_valid=%u out_blocks=%u out_write=%u out_read=%u subframes=%u padding=%u "
                "stereo=%u in0_ptr=%08X in1_ptr=%08X",
                (d0 >> 20) & 1, d0 & 0xFFF, (d0 >> 21) & 1, d1 & 0xFFF, d4 >> 31, d2 & 0x3FFFFFF,
                (d2 >> 26) & 0x1F, d2 >> 31, (d0 >> 12) & 0xFF, d3 & 0x3FFFFFF, d4 & 0x3FFFFFF,
                (d1 >> 17) & 0x7, (d1 >> 12) & 0x3, d1 >> 31, (d0 >> 22) & 0x1F, (d0 >> 27) & 0x1F, d9 & 0x1F,
                (d1 >> 20) & 0xF, (d1 >> 24) & 0x7, (d1 >> 29) & 1, Load32(context + 20), Load32(context + 24));
  return text;
}

// Input buffers marked valid and not yet seen consumed: (context, buffer) -> ms.
struct BufferKey {
  uint32_t context, buffer;
  bool operator==(const BufferKey&) const = default;
};
struct BufferKeyHash {
  size_t operator()(const BufferKey& k) const { return (size_t(k.context) << 1) ^ k.buffer; }
};
std::unordered_map<BufferKey, long long, BufferKeyHash> valid_since;
long long next_stuck_scan = 5000;
constexpr long long kStuckMs = 5000;

// Arguments kept from before the call for its after record (one call per thread at a time).
thread_local uint32_t pending_r3 = 0, pending_r4 = 0, pending_r5 = 0, pending_r7 = 0, pending_r9 = 0;
thread_local uint64_t pending_offset = 0;
thread_local std::string pending_path;

}  // namespace

void TraceExport(const char* full_name, PPCContext& ctx, bool before) {
  Trace* t = Open();
  if (!t) return;
  std::string_view name(full_name);
  if (name.starts_with("__imp__")) name.remove_prefix(7);
  const bool open = name == "NtCreateFile" || name == "NtOpenFile";
  const bool read = name == "NtReadFile" || name == "NtReadFileScatter";
  const bool close = name == "NtClose";
  const bool xma = IsXmaControl(name);
  const bool poll = IsXmaBufferPoll(name);
  const bool query = IsXmaQuery(name);
  if (!open && !read && !close && !xma && !poll && !query) return;
  if (before) {
    pending_r3 = ctx.r3.u32, pending_r4 = ctx.r4.u32, pending_r5 = ctx.r5.u32;
    pending_r7 = ctx.r7.u32, pending_r9 = ctx.r9.u32;
    pending_offset = read && ctx.r10.u32 ? Load64(ctx.r10.u32) : 0;
    pending_path = open ? ObjectName(ctx.r5.u32) : std::string();
    return;
  }
  const uint32_t status = ctx.r3.u32;
  std::lock_guard lock(t->mutex);
  const long long now =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t->start).count();
  // Buffers still valid kStuckMs after they were marked: the decoder has not
  // consumed them (a voice that never ends keeps its last buffer).
  if (now >= next_stuck_scan) {
    next_stuck_scan = now + kStuckMs;
    for (const auto& [key, since] : valid_since)
      if (now - since >= kStuckMs)
        Write(*t, "XMA-buffer-not-consumed context=" + std::to_string(key.context) + " buffer=" +
                      std::to_string(key.buffer) + " valid_for_ms=" + std::to_string(now - since) + " " +
                      ContextState(key.context));
    // A context whose input ended (every buffer consumed) kEndedMs ago and that
    // the title has not released: its last packets go to files once, and what
    // the title keeps asking of it is summarized at every scan.
    for (auto& [context, stream] : streams) {
      if (stream.released || now - stream.last_input_ms < kEndedMs) continue;
      if (valid_since.count(BufferKey{context, 0}) || valid_since.count(BufferKey{context, 1})) continue;
      if (!stream.dumped) {
        stream.dumped = true;
        std::string files;
        for (size_t i = 0; i < stream.buffers.size(); ++i) {
          char path[512];
          std::snprintf(path, sizeof(path), "%s/ctx-%08X-%lld-%zu.bin", dump_dir.c_str(), context, now, i);
          if (std::FILE* out = std::fopen(path, "wb")) {
            std::fwrite(stream.buffers[i].data(), 1, stream.buffers[i].size(), out);
            std::fclose(out);
            files += std::string(" ") + path;
          }
        }
        Write(*t, "XMA-stream-ended-unreleased context=" + std::to_string(context) + " idle_ms=" +
                      std::to_string(now - stream.last_input_ms) + " " + ContextState(context) + " packets:" + files);
      } else {
        std::string text;
        for (const auto& [query_name, calls] : stream.queries)
          text += " " + query_name + "=" + std::to_string(calls.first) + "/last=" + std::to_string(calls.second);
        stream.queries.clear();
        Write(*t, "XMA-ended-queries context=" + std::to_string(context) + text + " " + ContextState(context));
      }
    }
  }
  if (query) {
    auto it = streams.find(pending_r3);
    if (it != streams.end() && it->second.dumped && !it->second.released) {
      auto& entry = it->second.queries[std::string(name)];
      ++entry.first;
      entry.second = status;
    }
    return;
  }
  if (poll) {
    const BufferKey key{pending_r3, name == "XMAIsInputBuffer1Valid" ? 1u : 0u};
    auto it = valid_since.find(key);
    if (status == 0 && it != valid_since.end()) {
      // A buffer consumed in under 50 ms (a stream normally takes 150 to 250)
      // also records the context: the decoder may have given it up.
      const long long after = now - it->second;
      Write(*t, "XMA-buffer-consumed context=" + std::to_string(key.context) + " buffer=" +
                    std::to_string(key.buffer) + " after_ms=" + std::to_string(after) +
                    (after < 50 ? " " + ContextState(key.context) : std::string()));
      valid_since.erase(it);
    }
    return;
  }
  if (name == "XMASetInputBuffer0Valid" || name == "XMASetInputBuffer1Valid") {
    const uint32_t buffer = name == "XMASetInputBuffer1Valid" ? 1u : 0u;
    valid_since.try_emplace(BufferKey{pending_r3, buffer}, now);
    // Copy the buffer's packets as they are now (the title refills it later).
    const uint32_t packets = buffer ? Load32(pending_r3 + 4) & 0xFFF : Load32(pending_r3) & 0xFFF;
    const uint8_t* source = GuestPhysical(Load32(pending_r3 + (buffer ? 24 : 20)));
    ContextStream& stream = streams[pending_r3];
    stream.last_input_ms = now;
    stream.released = stream.dumped = false;
    stream.queries.clear();
    if (source && packets) {
      stream.buffers.emplace_back(source, source + size_t(packets) * 2048);
      if (stream.buffers.size() > kKeptBuffers) stream.buffers.erase(stream.buffers.begin());
    }
  }
  if (name == "XMAInitializeContext") streams[pending_r3] = ContextStream{};
  if (name == "XMAReleaseContext") {
    valid_since.erase(BufferKey{pending_r3, 0});
    valid_since.erase(BufferKey{pending_r3, 1});
    streams[pending_r3].released = true;
  }
  if (open) {
    const uint32_t handle = Load32(pending_r3);
    if (status == 0) t->files[handle] = pending_path;
    char line[512];
    std::snprintf(line, sizeof(line), "%.*s path=%s handle=%08X status=%08X", int(name.size()), name.data(),
                  pending_path.c_str(), handle, status);
    Write(*t, line);
  } else if (read) {
    auto it = t->files.find(pending_r3);
    const uint32_t io_status = Load32(pending_r7), information = Load32(pending_r7 + 4);
    char line[512];
    std::snprintf(line, sizeof(line), "%.*s handle=%08X file=%s offset=%llu length=%u event=%08X apc=%08X "
                  "status=%08X io_status=%08X bytes=%u", int(name.size()), name.data(), pending_r3,
                  it != t->files.end() ? it->second.c_str() : "?", static_cast<unsigned long long>(pending_offset),
                  pending_r9, pending_r4, pending_r5, status, io_status, information);
    Write(*t, line);
  } else if (close) {
    auto it = t->files.find(pending_r3);
    if (it == t->files.end()) return;
    Write(*t, "NtClose handle=" + std::to_string(pending_r3) + " file=" + it->second);
    t->files.erase(it);
  } else {
    char line[256];
    std::snprintf(line, sizeof(line), "%.*s args=%08X,%08X,%08X result=%08X", int(name.size()), name.data(),
                  pending_r3, pending_r4, pending_r5, status);
    if (name == "XMACreateContext")
      std::snprintf(line, sizeof(line), "XMACreateContext context=%08X result=%08X", Load32(pending_r3), status);
    Write(*t, line);
  }
}

}  // namespace studies
