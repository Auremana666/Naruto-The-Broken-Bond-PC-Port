// Writes a crash report (and a minidump) to logs/ when the game crashes, so a crash that happens
// on a player's PC can be traced afterwards. Installed before main runs.

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <cstdio>
#include <ctime>
#include <filesystem>

namespace {

std::filesystem::path LogsFolder() {
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  auto folder = std::filesystem::path(exe).parent_path() / "logs";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  return folder;
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
  static volatile LONG entered = 0;
  if (InterlockedExchange(&entered, 1)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  char stamp[32];
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
  const auto folder = LogsFolder();

  // Which module the faulting address belongs to, and the offset inside it.
  const auto* record = info->ExceptionRecord;
  const auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
  char module_name[MAX_PATH] = "?";
  uintptr_t module_base = 0;
  HMODULE module = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(record->ExceptionAddress), &module)) {
    GetModuleBaseNameA(GetCurrentProcess(), module, module_name, sizeof(module_name));
    module_base = reinterpret_cast<uintptr_t>(module);
  }

  const auto report_path = folder / (std::string("crash_") + stamp + ".txt");
  if (FILE* report = _wfopen(report_path.c_str(), L"w")) {
    std::fprintf(report, "Naruto: The Broken Bond crashed.\n");
    std::fprintf(report, "Exception code: 0x%08lX\n", record->ExceptionCode);
    std::fprintf(report, "Address: %s+0x%llX\n", module_name,
                 static_cast<unsigned long long>(address - module_base));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
      std::fprintf(report, "Access violation %s address 0x%llX\n",
                   record->ExceptionInformation[0] == 1 ? "writing" : "reading",
                   static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    std::fprintf(report, "Thread: %lu\n", GetCurrentThreadId());
    std::fclose(report);
  }

  const auto dump_path = folder / (std::string("crash_") + stamp + ".dmp");
  HANDLE file = CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
                                                 MiniDumpWithIndirectlyReferencedMemory),
                      &exception, nullptr, nullptr);
    CloseHandle(file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

struct CrashHandlerInstaller {
  CrashHandlerInstaller() { SetUnhandledExceptionFilter(OnUnhandledException); }
} g_crash_handler_installer;

}  // namespace
