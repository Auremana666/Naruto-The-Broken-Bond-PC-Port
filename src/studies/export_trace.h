// broken_bond - ReXGlue Recompiled Project
//
// Study: file reads and XMA voice control around scripted scenes
// (export_trace.cpp, REX_EXPORT_TRACE=<file>).

#pragma once

#include <rex/ppc/context.h>

namespace studies {
// Called by the native host's export guard before (before = true) and after
// the SDK runs a kernel export.
void TraceExport(const char* name, PPCContext& ctx, bool before);
}  // namespace studies
