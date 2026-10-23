// The performance log (perf_log.h).
#include "gpu/diagnostics/perf_log.h"
#include "gpu/diagnostics/profile.h"
#include "gpu/common/title_loading.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <fmt/format.h>
#include <rex/cvar.h>

namespace gpu::perf_log {
namespace {
std::FILE* out = nullptr;
bool opened = false;
uint64_t started = 0, second_start = 0, last_swap = 0;
std::vector<uint64_t> intervals;  // The frame times of the current second, ns.
std::array<uint64_t, profile::kCount> slot_ns{};
std::array<uint64_t, profile::kCounterCount> counter_sums{};
uint64_t wait_us_max = 0, loading_frames = 0;
uint64_t process_100ns = 0;  // Process CPU time at the second's start.

uint64_t ProcessCpu100ns() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
  auto ticks = [](const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
  return ticks(kernel) + ticks(user);
}

std::string Narrow(const wchar_t* text) {
  std::string s;
  for (const wchar_t* c = text; *c && s.size() < 200; ++c) s += *c < 32 || *c > 126 || *c == L',' ? ' ' : char(*c);
  return s;
}

std::string CpuName() {
  char name[256]{};
  DWORD size = sizeof(name);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
                   RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS)
    return "unknown";
  return name;
}

void WriteHeader() {
  std::fprintf(out, "# cpu,%s,logical processors,%u\n", CpuName().c_str(), GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    std::vector<LUID> seen;  // A card can be listed once per output.
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc{};
      if (FAILED(adapter->GetDesc1(&desc))) continue;
      if (std::any_of(seen.begin(), seen.end(), [&](const LUID& l) {
            return l.LowPart == desc.AdapterLuid.LowPart && l.HighPart == desc.AdapterLuid.HighPart;
          }))
        continue;
      seen.push_back(desc.AdapterLuid);
      std::fprintf(out, "# gpu,%s,vram MB,%llu,shared MB,%llu\n", Narrow(desc.Description).c_str(),
                   static_cast<unsigned long long>(desc.DedicatedVideoMemory >> 20),
                   static_cast<unsigned long long>(desc.SharedSystemMemory >> 20));
    }
  }
  MEMORYSTATUSEX memory{sizeof(memory)};
  GlobalMemoryStatusEx(&memory);
  SYSTEM_POWER_STATUS power{};
  GetSystemPowerStatus(&power);
  std::fprintf(out, "# ram MB,%llu,plugged in,%s,battery saver,%s\n",
               static_cast<unsigned long long>(memory.ullTotalPhys >> 20),
               power.ACLineStatus == 1 ? "yes" : power.ACLineStatus == 0 ? "no" : "unknown",
               power.SystemStatusFlag ? "on" : "off");
  std::string settings;
  for (const char* name : {"frame_rate", "internal_resolution_scale", "msaa_samples", "transparency_antialiasing",
                           "swap_post_effect", "present_effect", "anisotropic_override", "vsync", "fullscreen",
                           "aspect_ratio", "graphics_device"})
    settings += fmt::format(",{},{}", name, rex::cvar::GetFlagByName(name));
  std::fprintf(out, "# settings%s\n", settings.c_str());
  std::string columns = "seconds,frames,avg_ms,worst_ms,over_25ms,over_50ms,over_100ms,loading_frames,cpu_percent";
  for (unsigned slot = 0; slot < profile::kCount; ++slot) columns += fmt::format(",{}_ms", profile::kSlotNames[slot]);
  for (unsigned counter = 0; counter < profile::kCounterCount; ++counter)
    if (counter != profile::kPipelineWaitUsMax) columns += fmt::format(",{}", profile::kCounterNames[counter]);
  columns += ",pipeline_wait_us_max";
  std::fprintf(out, "%s\n", columns.c_str());
  std::fflush(out);
}

void WriteSecond(uint64_t now) {
  uint64_t sum = 0, worst = 0, over25 = 0, over50 = 0, over100 = 0;
  for (const uint64_t ns : intervals) {
    sum += ns;
    worst = std::max(worst, ns);
    over25 += ns > 25'000'000;
    over50 += ns > 50'000'000;
    over100 += ns > 100'000'000;
  }
  const uint64_t cpu = ProcessCpu100ns();
  const double wall_100ns = double(now - second_start) / 100.0;
  const unsigned cores = std::max<unsigned>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
  std::string line = fmt::format(
      "{:.0f},{},{:.2f},{:.1f},{},{},{},{},{:.0f}", double(second_start - started) / 1e9, intervals.size(),
      intervals.empty() ? 0.0 : double(sum) / intervals.size() / 1e6, double(worst) / 1e6, over25, over50, over100,
      loading_frames, wall_100ns > 0 ? 100.0 * double(cpu - process_100ns) / wall_100ns / cores : 0.0);
  for (unsigned slot = 0; slot < profile::kCount; ++slot) line += fmt::format(",{:.1f}", double(slot_ns[slot]) / 1e6);
  for (unsigned counter = 0; counter < profile::kCounterCount; ++counter)
    if (counter != profile::kPipelineWaitUsMax) line += fmt::format(",{}", counter_sums[counter]);
  line += fmt::format(",{}", wait_us_max);
  std::fprintf(out, "%s\n", line.c_str());
  std::fflush(out);
  intervals.clear();
  slot_ns = {};
  counter_sums = {};
  wait_us_max = loading_frames = 0;
  process_100ns = cpu;
  second_start = now;
}
}  // namespace

bool Enabled() {
  static const bool value = [] {
    const char* text = std::getenv("REX_NATIVE_PERF_LOG");
    const char* dir = std::getenv("REX_NATIVE_FRAME_DIR");
    return text && *text && dir && *dir;
  }();
  return value;
}

void AtSwap() {
  if (!Enabled()) return;
  if (!opened) {
    opened = true;
    out = std::fopen(fmt::format("{}/performance.csv", std::getenv("REX_NATIVE_FRAME_DIR")).c_str(), "w");
    if (out) WriteHeader();
  }
  if (!out) return;
  const uint64_t now = profile::Now();
  if (!last_swap) {
    started = second_start = last_swap = now;
    process_100ns = ProcessCpu100ns();
    return;
  }
  intervals.push_back(now - last_swap);
  last_swap = now;
  if (TitleLoading()) ++loading_frames;
  for (unsigned i = 0; i < profile::kCount; ++i) {
    slot_ns[i] += profile::slots[i].ns.exchange(0, std::memory_order_relaxed);
    profile::slots[i].calls.store(0, std::memory_order_relaxed);
  }
  for (unsigned i = 0; i < profile::kCounterCount; ++i) {
    const uint64_t value = profile::counters[i].exchange(0, std::memory_order_relaxed);
    if (i == profile::kPipelineWaitUsMax) wait_us_max = std::max(wait_us_max, value);
    else counter_sums[i] += value;
  }
  if (now - second_start >= 1'000'000'000) WriteSecond(now);
}
}  // namespace gpu::perf_log
