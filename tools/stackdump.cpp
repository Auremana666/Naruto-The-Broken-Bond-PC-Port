// Prints the call stack of every thread in a running process (by pid), using dbghelp.
// Used to see which recompiled game functions (sub_XXXXXXXX) each thread is sitting in.
// Usage: stackdump <pid> [max_frames] [samples] [interval_ms]  (samples > 1 repeats the dump)
// Build: clang++ -O1 stackdump.cpp -o stackdump.exe -ldbghelp
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: stackdump <pid> [max_frames]\n");
    return 1;
  }
  const DWORD pid = std::strtoul(argv[1], nullptr, 10);
  const int max_frames = argc > 2 ? std::atoi(argv[2]) : 40;
  const int samples = argc > 3 ? std::atoi(argv[3]) : 1;
  const int interval_ms = argc > 4 ? std::atoi(argv[4]) : 50;
  HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
  if (!process) {
    std::fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
    return 1;
  }
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(process, nullptr, TRUE)) {
    std::fprintf(stderr, "SymInitialize failed: %lu\n", GetLastError());
    return 1;
  }

  for (int sample = 0; sample < samples; ++sample) {
  if (sample) Sleep(interval_ms);
  std::printf("### sample %d\n", sample);
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 entry{sizeof(entry)};
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != pid) continue;
    HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);
    if (!thread) continue;

    std::wstring name;
    PWSTR description = nullptr;
    if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
      name = description;
      LocalFree(description);
    }
    std::printf("=== thread %lu %ls\n", entry.th32ThreadID, name.c_str());

    SuspendThread(thread);
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &context)) {
      STACKFRAME64 frame{};
      frame.AddrPC.Offset = context.Rip;
      frame.AddrPC.Mode = AddrModeFlat;
      frame.AddrFrame.Offset = context.Rbp;
      frame.AddrFrame.Mode = AddrModeFlat;
      frame.AddrStack.Offset = context.Rsp;
      frame.AddrStack.Mode = AddrModeFlat;
      for (int i = 0; i < max_frames; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
          break;
        }
        const DWORD64 pc = frame.AddrPC.Offset;
        if (!pc) break;
        char buffer[sizeof(SYMBOL_INFO) + 256]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        IMAGEHLP_MODULE64 module{sizeof(module)};
        const char* module_name = SymGetModuleInfo64(process, pc, &module) ? module.ModuleName : "?";
        if (SymFromAddr(process, pc, &displacement, symbol)) {
          std::printf("  %s!%s+0x%llx\n", module_name, symbol->Name, displacement);
        } else {
          std::printf("  %s!0x%llx\n", module_name, pc - module.BaseOfImage);
        }
      }
    }
    ResumeThread(thread);
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  }
  SymCleanup(process);
  CloseHandle(process);
  return 0;
}
