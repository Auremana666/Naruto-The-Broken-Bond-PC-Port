// Crash evidence of a host crash (crash_dump.h). The SDK's vectored
// handler logs "Unhandled guest access violation" and continues the search, so
// the process reaches the unhandled-exception filter installed here, which
// writes a full minidump without a debugger attached (the crash did not
// reproduce in 9 minutes under CDB: it is a timing race). The ring of the
// title's frees (820C0018, the engine free that reaches dlfree 822007C8 and
// unlink_large_chunk 821FF188, where the corruption is detected) names the
// thread, the lr and the time of the last 1024 frees, to match the corrupted
// chunk of the dump with the free that released it.
#include "gpu/diagnostics/crash_dump.h"
#include "gpu/diagnostics/heap_watch.h"
#include <windows.h>
#include <dbghelp.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <rex/system/thread_state.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace gpu::crash_dump {
namespace {
struct FreeEntry {
  uint32_t kind, pointer, heap, arg4, lr, guest_thread;
  int64_t qpc;
};
constexpr uint32_t kRing = 1024;
std::array<FreeEntry, kRing> ring{};
std::atomic<uint32_t> ring_count{0};
bool installed = false;
bool stack_only = false;  // REX_NATIVE_CRASH_DUMP=stack: no minidump.
std::string directory;

const char* Directory() {
  if (directory.empty()) {
    const char* dir = std::getenv("REX_NATIVE_FRAME_DIR");
    directory = dir && *dir ? dir : ".";
  }
  return directory.c_str();
}

void WriteFrees(const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  const uint32_t count = ring_count.load();
  const uint32_t first = count > kRing ? count - kRing : 0;
  std::fprintf(f, "{\"frees\":%u,\"entries\":[", count);
  for (uint32_t i = first; i < count; ++i) {
    const FreeEntry& e = ring[i % kRing];
    std::fprintf(f, "%s{\"n\":%u,\"kind\":%u,\"pointer\":%u,\"heap\":%u,\"arg4\":%u,\"lr\":%u,\"guest_thread\":%u,"
                 "\"qpc\":%lld}", i == first ? "" : ",", i, e.kind, e.pointer, e.heap, e.arg4, e.lr, e.guest_thread,
                 static_cast<long long>(e.qpc));
  }
  std::fprintf(f, "]}\n");
  std::fclose(f);
}

// The host stack of the faulting thread with symbols from the PDB: each
// recompiled guest function is a host function (sub_XXXXXXXX), so the frames
// name the guest call chain. The guest registers come from the thread's
// PPCContext; lr is the return address of the last guest call.
void WriteStack(const char* path, EXCEPTION_POINTERS* pointers) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  const EXCEPTION_RECORD* record = pointers ? pointers->ExceptionRecord : nullptr;
  std::fprintf(f, "code 0x%08lX address %p", record ? record->ExceptionCode : 0ul,
               record ? record->ExceptionAddress : nullptr);
  if (record && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
    std::fprintf(f, " access %llu host 0x%llX", static_cast<unsigned long long>(record->ExceptionInformation[0]),
                 static_cast<unsigned long long>(record->ExceptionInformation[1]));
  std::fprintf(f, "\n");
  if (auto* ts = rex::runtime::ThreadState::Get(); ts && ts->context()) {
    const PPCContext& c = *ts->context();
    std::fprintf(f, "guest thread %u lr %08X ctr %08X r1 %08X r2 %08X r13 %08X\n", ts->thread_id(),
                 uint32_t(c.lr), c.ctr.u32, c.r1.u32, c.r2.u32, c.r13.u32);
    const PPCRegister* gpr[] = {&c.r3, &c.r4, &c.r5, &c.r6, &c.r7, &c.r8, &c.r9, &c.r10, &c.r11, &c.r12,
                                &c.r14, &c.r15, &c.r16, &c.r17, &c.r18, &c.r19, &c.r20, &c.r21, &c.r22,
                                &c.r23, &c.r24, &c.r25, &c.r26, &c.r27, &c.r28, &c.r29, &c.r30, &c.r31};
    const int names[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
                         28, 29, 30, 31};
    for (size_t i = 0; i < std::size(gpr); ++i)
      std::fprintf(f, "r%d %08X%s", names[i], gpr[i]->u32, i % 7 == 6 ? "\n" : " ");
  } else {
    std::fprintf(f, "no guest thread state\n");
  }
  HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
  using Initialize = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
  using Options = DWORD(WINAPI*)(DWORD);
  using FromAddr = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
  using Walk = BOOL(WINAPI*)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
                             PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64,
                             PTRANSLATE_ADDRESS_ROUTINE64);
  auto initialize = dbghelp ? reinterpret_cast<Initialize>(GetProcAddress(dbghelp, "SymInitialize")) : nullptr;
  auto options = dbghelp ? reinterpret_cast<Options>(GetProcAddress(dbghelp, "SymSetOptions")) : nullptr;
  auto from_addr = dbghelp ? reinterpret_cast<FromAddr>(GetProcAddress(dbghelp, "SymFromAddr")) : nullptr;
  auto walk = dbghelp ? reinterpret_cast<Walk>(GetProcAddress(dbghelp, "StackWalk64")) : nullptr;
  auto table = dbghelp ? reinterpret_cast<PFUNCTION_TABLE_ACCESS_ROUTINE64>(
                             GetProcAddress(dbghelp, "SymFunctionTableAccess64")) : nullptr;
  auto module_base = dbghelp ? reinterpret_cast<PGET_MODULE_BASE_ROUTINE64>(
                                   GetProcAddress(dbghelp, "SymGetModuleBase64")) : nullptr;
  if (!initialize || !options || !from_addr || !walk || !table || !module_base || !pointers ||
      !pointers->ContextRecord) {
    std::fprintf(f, "no stack: dbghelp unavailable\n");
    std::fclose(f);
    return;
  }
  const HANDLE process = GetCurrentProcess();
  options(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  initialize(process, nullptr, TRUE);
  CONTEXT context = *pointers->ContextRecord;
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 256];
  for (int depth = 0; depth < 64; ++depth) {
    if (!walk(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr, table,
              module_base, nullptr) || !frame.AddrPC.Offset)
      break;
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    std::memset(storage, 0, sizeof(storage));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (from_addr(process, frame.AddrPC.Offset, &displacement, symbol))
      std::fprintf(f, "#%02d %016llX %s+0x%llX\n", depth, static_cast<unsigned long long>(frame.AddrPC.Offset),
                   symbol->Name, static_cast<unsigned long long>(displacement));
    else
      std::fprintf(f, "#%02d %016llX ?\n", depth, static_cast<unsigned long long>(frame.AddrPC.Offset));
  }
  std::fclose(f);
}

LONG WINAPI Filter(EXCEPTION_POINTERS* pointers) {
  static std::atomic<int> entered{0};
  if (entered.fetch_add(1)) return EXCEPTION_CONTINUE_SEARCH;
  WriteStack((std::string(Directory()) + "/crash-stack.txt").c_str(), pointers);
  if (stack_only) return EXCEPTION_CONTINUE_SEARCH;
  const std::string dump = std::string(Directory()) + "/crash.dmp";
  const std::string frees = std::string(Directory()) + "/crash-frees.json";
  WriteFrees(frees.c_str());
  heap_watch::WriteReport((std::string(Directory()) + "/crash-heap-watch.json").c_str());
  using Writer = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
  HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
  auto writer = dbghelp ? reinterpret_cast<Writer>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
  HANDLE file = CreateFileA(dump.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  BOOL written = FALSE;
  if (writer && file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), pointers, FALSE};
    written = writer(GetCurrentProcess(), GetCurrentProcessId(), file,
                     MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                   MiniDumpWithFullMemoryInfo),
                     &info, nullptr, nullptr);
  }
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  char text[512];
  std::snprintf(text, sizeof(text), "native crash dump: code 0x%08lX address %p written=%d path=%s\n",
                pointers && pointers->ExceptionRecord ? pointers->ExceptionRecord->ExceptionCode : 0ul,
                pointers && pointers->ExceptionRecord ? pointers->ExceptionRecord->ExceptionAddress : nullptr,
                int(written), dump.c_str());
  OutputDebugStringA(text);
  std::fputs(text, stderr);
  return EXCEPTION_CONTINUE_SEARCH;  // Windows ends the process; the evidence is on disk.
}
}  // namespace

void Install() {
  const char* text = std::getenv("REX_NATIVE_CRASH_DUMP");
  if (!text || (std::string_view(text) != "1" && std::string_view(text) != "stack") || installed) return;
  stack_only = std::string_view(text) == "stack";
  SetUnhandledExceptionFilter(Filter);
  installed = true;
}

void NoteFree(uint32_t kind, uint32_t pointer, uint32_t heap, uint32_t arg4, uint32_t lr, uint32_t guest_thread) {
  if (!installed) return;
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  const uint32_t n = ring_count.fetch_add(1);
  ring[n % kRing] = FreeEntry{kind, pointer, heap, arg4, lr, guest_thread, now.QuadPart};
}
}  // namespace gpu::crash_dump

// The Rise engine free hook (820C0018) is not ported: Broken Bond reaches its
// free through a function pointer that is not mapped, so the free ring stays empty.
