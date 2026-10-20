// The host of the native mode (opt-in): the runtime configuration that puts the
// graphics system of the D3D boundary in place of the Xenos plugin, the window
// close and the guest memory reads of the services. The other responsibilities
// of the host live in host/*.cpp (internal.h).
#include "gpu/host/app.h"
#include "gpu/host/internal.h"
#include "gpu/d3d/d3d.h"
#include "gpu/common/guest_memory.h"
#include "gpu/settings.h"
#include "gpu/diagnostics/crash_dump.h"
#include "gpu/diagnostics/heap_watch.h"
#include <cstdlib>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/xmemory.h>

// The renderer of the game: the Xenos plugin (gpu_plugin) or the native one.
// The native renderer runs here without the journal unless a path is given.
REXCVAR_DEFINE_STRING(renderer, "xenos", "broken_bond/Graphics",
                      "Renderer: xenos (the emulated GPU) or native (Direct3D 12 or Vulkan)")
    .allowed({"xenos", "native"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// The diagnostic cases: the native mode with the journal and both options below.
REXCVAR_DEFINE_BOOL(native_renderer_bootstrap, false, "broken_bond/Diagnostics",
                    "Isolate graphics and stop at the first unknown service");
REXCVAR_DEFINE_STRING(native_renderer_bootstrap_path, "", "broken_bond/Diagnostics",
                      "Exclusive JSONL checkpoint output; required in bootstrap mode");
REXCVAR_DEFINE_BOOL(native_renderer_bootstrap_initialize_engines, false, "broken_bond/Diagnostics",
                    "Initialize the native engines");
REXCVAR_DEFINE_BOOL(native_renderer_bootstrap_register_interrupt_callback, false, "broken_bond/Diagnostics",
                    "Retain the reviewed interrupt registration");

namespace gpu::host {
std::atomic<bool> enabled{false};

bool Enabled() { return enabled.load(); }
bool Read(rex::memory::Memory* memory, uint32_t address, void* target, size_t size) {
  if (!address || uint64_t(address) + size > UINT32_MAX) return false;
  return CopyFromGuest(target, memory->TranslateVirtual(address), size);  // guest_memory.h
}

// The window close of the native mode, on the UI thread, before
// ReXApp::OnClosing (TerminateTitle, std::_Exit(0)). The close always proceeds.
bool CloseRequested() {
  if (!enabled.load()) return true;
  return gpu::CloseInOrder();
}

void Configure(rex::RuntimeConfig& config) {
  const bool bootstrap = REXCVAR_GET(native_renderer_bootstrap);
  if (!bootstrap && REXCVAR_GET(renderer) != "native") return;
  // The native mode is the D3D boundary on the own engines and the own
  // interrupt registration; the diagnostic cases name both and the journal.
  const auto path = REXCVAR_GET(native_renderer_bootstrap_path);
  if (bootstrap && (!REXCVAR_GET(native_renderer_bootstrap_initialize_engines) ||
                    !REXCVAR_GET(native_renderer_bootstrap_register_interrupt_callback) || path.empty()))
    std::_Exit(88);
  if (config.graphics) std::_Exit(88);
  if (!path.empty() && !OpenJournal(path)) std::_Exit(88);
  enabled = true;
  // The graphics options, read once before the graphics system exists.
  gpu::RegisterPluginOptions();
  gpu::LoadSettings();
  const auto& backend = gpu::CurrentSettings().native_backend;
  bool available = false;
#if !defined(BROKENBOND_NATIVE_D3D12_DISABLED)
  available |= backend == "d3d12";
#endif
#if defined(BROKENBOND_NATIVE_VULKAN)
  available |= backend == "vulkan";
#endif
  if (!available) {
    REXLOG_ERROR("[broken_bond] Requested native_backend '{}' is unavailable in this build. No backend fallback.", backend);
    std::_Exit(88);
  }
  REXLOG_INFO("[broken_bond] Renderer: native; backend: {}{}", backend, path.empty() ? "" : ", with the journal");
  gpu::crash_dump::Install();  // REX_NATIVE_CRASH_DUMP=1 (crash_dump.h).
  gpu::heap_watch::Install();  // REX_NATIVE_HEAP_WATCH=1 (heap_watch.h).
  config.gpu_plugin.clear();
  config.graphics = CreateGraphicsSystem();
  {
    std::lock_guard lock(event_mutex);
    Record("host-setup", "OnPreSetup-own-system-plugin-cleared", nullptr);
  }
  Record("native-d3d-settings", gpu::SettingsEvidence());
}
}  // namespace gpu::host
