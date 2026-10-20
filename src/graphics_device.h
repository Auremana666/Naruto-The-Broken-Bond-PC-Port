// broken_bond - graphics card choice
//
// The graphics card by its name (graphics_device), for both renderers, as
// Unleashed Recompiled's GraphicsDevice option: empty keeps each renderer's own
// choice. The native renderer passes the name to Plume (gpu/backend/engine.cpp);
// the Xenos renderer takes the adapter index from it here, through the SDK's
// d3d12_adapter, while its D3D12 provider is created.

#pragma once

// Before the Xenos plugin creates its D3D12 provider: d3d12_adapter set to the
// index of the named adapter, when it is present.
void SelectXenosAdapter();

// After: d3d12_adapter back to its configured value, so that a settings save
// (F4) does not keep the index in the configuration.
void RestoreXenosAdapter();
