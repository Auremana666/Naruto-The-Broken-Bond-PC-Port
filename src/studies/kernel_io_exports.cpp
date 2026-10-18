// broken_bond - ReXGlue Recompiled Project
//
// The SDK's file exports compiled into the native executable with the export
// guard (host/kernel_exports.h), as host/kernel_threading.cpp does, so the
// file study (export_trace.cpp, REX_EXPORT_TRACE) sees them. Same source as
// the runtime DLL's.
#include "gpu/host/kernel_exports.h"
#include "../../sdk/src/kernel/xboxkrnl/xboxkrnl_io.cpp"
