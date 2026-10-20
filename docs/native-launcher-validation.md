# Native launcher validation

The earlier dual-API build was validated on Windows x64 on 2026-10-20.
This version now ships Direct3D 12 only; the release change is recorded below.
The SDK revision is unchanged.

## Configuration and UI

- All four launcher CTest cases passed with both native APIs enabled.
- Configuration tests also passed in separate D3D12-only and Vulkan-only
  launcher builds. Legacy Xenos selections migrate to native, unavailable APIs
  reset to the compiled default, and adapter selections from unavailable APIs
  are cleared. User language, save paths and unrelated settings are preserved.
- The packaged launcher was opened with a private legacy configuration.
  The Display dropdown showed exactly `Native: Direct3D 12` and `Native: Vulkan`.
  Selecting Vulkan and closing saved `renderer = 'native'` and
  `native_backend = 'vulkan'`, removed `gpu_plugin`, and preserved
  `user_language = 4`.
- Screenshots and the private configuration are retained in ignored
  `build/native-launcher-validation/ui/`. No real user configuration was edited.

The launcher hides the retired Xenos dithering and overlay controls. Ultrawide
remains disabled with its existing validation boundary. The Vulkan frame
latency field remains read-only.

## Bounded native startup

Each backend ran for 25 seconds from a separate directory containing the game,
runtime and private configuration, without `rexgpu-xenos.dll`. Both started from
legacy `renderer = 'xenos'` / `gpu_plugin = 'xenos'` settings. The runtime log
confirmed migration and the requested native backend.

| Backend | Exit | Shutdown evidence |
| --- | --- | --- |
| Direct3D 12 | 0, no forced termination | 702 presents; 17/17 guest threads joined; no stragglers or resource failures; engine released |
| Vulkan | 0, no forced termination | 17/17 guest threads joined; no stragglers or resource failures; presentation retired and engine released |

Only windows owned by the test process received the close request. No gameplay
input was injected. Journals, logs and reports are under
`build/native-launcher-validation/d3d12-*` and `vulkan-*`. Both tested executables
match the packaged executable byte for byte. These checks establish bounded
startup and shutdown, not full gameplay or graphics-driver coverage.

## Distribution

The native game and launcher rebuilt successfully. The installer integration
suite passed 16 tests against the real native package, with the optional full
disc test skipped (the unchanged import flow had already passed that test).
The suite checks native defaults, removal of a retired plugin during update,
and preservation of existing configuration and personal data.

The final portable ZIP contains 45 files, all matching the staged payload.
Checksums were verified, the port binaries match `build/native`, and no Xenos
plugin, ISO tools, original XEX, ISO or extracted game files are included.
Dependency import checks rejected no debug CRTs. Native dependency notices
are included. See [installer validation](installer-validation.md) for artifact
identity and [packaging](../packaging/README.md) for reproduction commands.

## Direct3D 12-only release — 2026-10-20

Vulkan is disabled in the shipping game and launcher. The native build script
now defaults to `-Backend d3d12`, and packaging rejects a build with Vulkan
enabled. Vulkan source and explicit development builds remain available.
The shader build generated 5,009 DXIL shaders and 5,160 variants with no SPIR-V
package. All four launcher tests passed, including a new migration check for
an existing native Vulkan selection: it becomes Direct3D 12, clears the Vulkan
adapter choice and preserves game language and save paths.

The installer passed three targeted integration cases against the rebuilt
payload: fresh defaults, reinstall/uninstall preservation, and updating between
releases while preserving user data (51.767 seconds). The optional ISO import
implementation was unchanged. The final ZIP has 41 files, all matching the
portable directory, with verified checksums. The packaged launcher contains
the Direct3D 12 choice and no Vulkan choice; the build metadata and output
confirm that the SPIR-V package is absent.

A 25-second D3D12 boot without the Xenos DLL produced 699 presents and exited
with code 0 after an owned-window close request, without forced termination.
However, the shutdown journal recorded only 16/17 guest threads joined, one
straggler, and `owner_released = false` / `engine_released = false`.
This run does **not** validate complete graphics cleanup. Resource failure and
device-removal counters were zero. No lifecycle implementation was changed as
part of removing the Vulkan release option; the cause of this incomplete
shutdown remains unestablished.

Evidence is retained in ignored `build/d3d12-release-validation/d3d12-*` and
`build/setup-helper/d3d12-integration-tests.log`.
