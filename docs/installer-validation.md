# Installer validation

Initial installer validation performed on Windows x64 on 2026-10-20. This
section records the earlier Xenos payload; the native payload update is below. The installer is adapted from
Rise's Inno Setup flow; see [packaging](../packaging/README.md) for build commands.

| Check | Result |
| --- | --- |
| Standalone Release setup helper | Built with Clang 19.1.5 and static C++ runtime |
| Game and launcher Release targets | Rebuilt successfully before packaging |
| Launcher CTest suite | 4/4 passed |
| Launcher supported-XEX validation | Passed against the local supported executable |
| Setup helper tests | 12/12 passed with the local supported XEX; synthetic disc layouts |
| Helper tests without game files | 6 passed, 6 explicitly skipped |
| Installer integration against the real portable payload | 16/16 passed in 110.025 seconds |
| Original local full-disc ISO inspection | Title ID and SHA-256 accepted; 215 files, 3,404,601,109 bytes |
| Full local-disc installation and uninstall | Passed in 57.503 seconds; imported executable hash matched and all imported files survived uninstall |
| Portable ZIP audit | All 40 payload files matched the portable folder byte for byte; no original game data or ISO tools |
| PE dependency audit | Packaged binaries, setup helper and extractor passed the debug CRT rejection check |

The integration run used `BROKENBOND_TEST_PACKAGE` pointing at the assembled
portable directory and `BROKENBOND_TEST_XEX` pointing at the local supported
executable. Small test ISOs contained that executable and a synthetic fixture.
Every installation used a unique test AppId, temporary destination, disabled
shortcuts and no launcher execution.

Coverage includes fresh installation, optional ISO import into an empty folder,
reinstall, updates with registry-only version metadata, portable conversion,
numeric downgrade rejection, unsupported revision and wrong-title rejection,
invalid modes, seven independent interface languages, directory-junction
rejection, and preservation of settings, saves, caches, imported files and
personal files through updates and uninstall. An inherited test initially
referenced Rise's icon filename; that expectation was corrected before the
passing run.

Local logs are in ignored `build/setup-helper/integration-tests.log`,
`build/setup-helper/full-disc-test.log` and
`build/setup-helper/real-iso-check.log`. Packages and checksum lists are in
ignored `dist/`; previous packages are preserved under `previous-release-*`.
The final default destination is `C:\Games\Naruto The Broken Bond`; colons
cannot be used in Windows directory names. No release was published.

The initial `BrokenBondInstaller.exe` was 21,068,065 bytes, version `0.1.0`,
with SHA-256
`6c9f71df229c158dbfb1a8a791468b583856f9c898d0d5dddfae9b8c82fe3e43`.
Its checksum and all 40 files in the final portable ZIP were verified after
regenerating the installer with the requested default destination.

Interactive installer layout, shortcut interaction and gameplay were not validated
by these installer tests. Successful compilation and installation do not
establish that the game runs or that the whole story is playable.

## Earlier dual-API package update — 2026-10-20

That package used `build/native` with both native Direct3D 12 and Vulkan.
It contains no `rexgpu-xenos.dll`; updating removes that retired plugin while
preserving the user's configuration and data. The launcher migrates the renderer
selection separately when it loads/saves the configuration.

The integration suite ran against the native portable payload: 16 tests passed
in 162.259 seconds; the optional full-disc import test was skipped because the
import implementation was unchanged and had already passed above. Coverage now
also checks the native defaults and removal of an old plugin during update.
The log is `build/setup-helper/native-integration-tests.log`.

That installer was 82,186,911 bytes, version `0.1.0`, SHA-256
`6d3a9c5ead164a4a4b2ccd17a4d63259dc5d8af247ff8266a3cf908d5824b9cc`.
The default installation directory remains `C:\Games\Naruto The Broken Bond`.
See [native launcher validation](native-launcher-validation.md) for UI,
configuration migration and bounded game startup checks.

## Current Direct3D 12-only package — 2026-10-20

This version excludes Vulkan from both the game and launcher. Packaging requires
`BROKENBOND_NATIVE_D3D12=ON` and `BROKENBOND_NATIVE_VULKAN=OFF`; the default native
build script uses `-Backend d3d12`. The Vulkan sources remain available for
explicit development builds.

Fresh defaults, reinstall/uninstall preservation, and update preservation
passed against the real package: 3/3 integration cases in 51.767 seconds.
All four launcher tests passed, including migration from a saved native Vulkan
selection to Direct3D 12 without losing game language or save paths.
The ZIP contains 41 files, verified byte for byte against the portable payload;
checksums and the absence of the Vulkan choice, SPIR-V package and Xenos plugin
were checked.

The current installer is 59,180,810 bytes, version `0.1.0`, SHA-256
`312ebcaeef795ad3086b076b609911c1c19983cf4687c90c89b44c514640c6ad`.
The default destination remains `C:\Games\Naruto The Broken Bond`.
The bounded game boot exited with code 0 but reported incomplete resource
cleanup; see [native launcher validation](native-launcher-validation.md).
