#pragma once
#include <rex/hook.h>
#include "gpu/host/app.h"

// Compile the complete SDK translation unit into the diagnostic executable.
// Historical guest direct calls bind these local exports. The generic runtime
// DLL retains its registry, so dynamic procedure lookup is guarded separately.
// Normal builds keep SDK exports.
#undef REX_HOOK
#define REX_HOOK(name, function) \
  extern "C" REX_FUNC(name) { \
    if (::gpu::host::BeforeExport(#name, ctx)) return; \
    rex::ppc::HostToGuestFunction<function>(ctx, base); \
    ::gpu::host::AfterExport(#name, ctx); \
  }
#undef REX_EXPORT_STUB
#define REX_EXPORT_STUB(name) \
  extern "C" REX_FUNC(name) { \
    if (::gpu::host::BeforeExport(#name, ctx)) return; \
    REXKRNL_WARN("{} STUB", #name); \
    ::gpu::host::AfterExport(#name, ctx); \
  } \
  static rex::ppc::detail::PPCFuncRegistrar _ppc_reg_##name(#name, &name);
