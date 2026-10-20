#include "gpu/backend/title_witness.h"
#include <cstdlib>
#include <windows.h>
#include <rex/system/xthread.h>

extern "C" __declspec(dllexport) uint32_t NativeTitleWitnessArm(uint32_t version, uint32_t hooks) {
  return gpu::title_wait_witness.Arm(version, hooks) ? 1 : 0;
}
extern "C" __declspec(dllexport) void NativeTitleWitnessWait(uint32_t tid, uintptr_t object,
                                                          uintptr_t backend, uintptr_t handle, uint32_t entering) {
  if (!gpu::title_wait_witness.OriginalWait(tid, object, backend, handle, entering != 0))
    NativeTitleWitnessFault();
}
extern "C" __declspec(dllexport) void NativeTitleWitnessMutation(uint32_t operation, uintptr_t address,
                                                               uint64_t bytes, uint32_t entering) {
  if (!gpu::title_wait_witness.Mutation(operation, address, bytes, entering != 0))
    NativeTitleWitnessFault();
}
// An original host-only marker. The qualified observer detaches at this point
// after the last diagnostic sample, before CRT/process teardown.
extern "C" __declspec(dllexport) __declspec(noinline) void NativeTitleWitnessDiagnosticEnd() {
  // A real host-only instruction makes this original marker interceptable.
  gpu::title_diagnostic_end_count.fetch_add(1, std::memory_order_relaxed);
}
extern "C" __declspec(dllexport) __declspec(noreturn) void NativeTitleWitnessFault() {
  // A broken synchronous observer must not resume a title continuation.
  // This is a failed experiment exit, never an accepted retirement or stop.
  TerminateProcess(GetCurrentProcess(), 88);
  std::_Exit(88);
}
extern "C" __declspec(dllexport) void NativeTitleWitnessHostWorker(uint32_t kind, uintptr_t owner, uint32_t entering) {
  auto* thread = rex::system::XThread::GetCurrentThread();
  if (!thread || thread->is_guest_thread() || !thread->thread()) {
    gpu::title_wait_witness.Invalidate();
    if (!gpu::title_wait_witness.ContinueOriginal()) NativeTitleWitnessFault();
    return;
  }
  gpu::title_wait_witness.HostWorker(kind, thread->thread_id(), thread->thread()->system_id(),
      reinterpret_cast<uintptr_t>(thread), owner, entering != 0);
  if (!gpu::title_wait_witness.ContinueOriginal()) NativeTitleWitnessFault();
}

extern "C" __declspec(dllexport) __declspec(noreturn) void NativeTitleWitnessComplete(uint32_t exit_code) {
  if (exit_code != 86 || !gpu::title_wait_witness.CompletionReady()) NativeTitleWitnessFault();
  // The process handle is the current experimental PID. Its original-call
  // observers remain active up to process termination, without CRT cleanup.
  TerminateProcess(GetCurrentProcess(), exit_code);
  NativeTitleWitnessFault();
}

extern "C" __declspec(dllexport) void NativeTitleWitnessSelfExit(uint32_t tid, uintptr_t caller,
                                                               uint32_t status, uint32_t entering) {
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  FILETIME created{}, exited{}, kernel_time{}, user_time{};
  // The creation time distinguishes the bound dispatcher owner from a reused TID.
  if (!ntdll || tid != GetCurrentThreadId() || caller != reinterpret_cast<uintptr_t>(ntdll) + 0xACB86 ||
      !GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel_time, &user_time) ||
      !gpu::title_wait_witness.OriginalPrivateSelfExit(tid, status, entering != 0,
          (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime))
    NativeTitleWitnessFault();
}
