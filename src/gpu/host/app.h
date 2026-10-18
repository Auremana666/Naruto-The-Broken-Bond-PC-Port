#pragma once

#include <rex/runtime.h>
#include <rex/ppc/context.h>

// The host of the native renderer (opt-in): the runtime configuration, the guard
// of the guest kernel exports (uncontracted operations always stop) and the close.
namespace gpu::host {
// Puts the native renderer in place of the Xenos plugin when the configuration
// chooses it (renderer = "native", or the diagnostic bootstrap options).
void Configure(rex::RuntimeConfig& config);
// The native renderer runs (Configure chose it).
bool Enabled();
bool BeforeExport(const char* name, PPCContext& ctx);
// Ordered close of the native mode on the window close (UI thread,
// before ReXApp::OnClosing). Returns true: the close always proceeds.
bool CloseRequested();
void AfterExport(const char* name, PPCContext& ctx);
}  // namespace gpu::host
