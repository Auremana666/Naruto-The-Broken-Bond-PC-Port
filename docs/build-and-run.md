# Build and run

[Documentation](README.md) · [Configuration](configuration.md) · [Renderers](renderers.md)

This guide describes the project's **Windows x64 / Release** workflow. Run the commands from the repository root in PowerShell 7. The scripts load the Visual Studio x64 build environment themselves when it is not loaded. Stop the sequence if any command fails.

## 1. Prerequisites

| Tool | Requirement |
| --- | --- |
| Git | With submodule support and symbolic links enabled (Windows Developer Mode, or an elevated shell) |
| Visual Studio 2022 or Build Tools | The C++ workload, with the x64 tools and a Windows SDK |
| LLVM/Clang | `clang` and `clang++` on `PATH`; 19.1.5 is the validated version |
| CMake | **3.26+**, for the version 6 presets |
| Ninja | On `PATH` |
| PowerShell | 7 or newer (`pwsh`); Windows PowerShell 5.1 is not supported |
| Python | 3.x on `PATH`, only for the native renderer |
| Game files | An ISO or an extracted copy of the supported revision (see [the README](../README.md#requirements)) |

The SDK is built for x86-64-v2 processors (SSE4.2). The first SDK configuration and the extraction script download dependencies, so an internet connection is required.

Check your environment:

```powershell
git --version
clang --version
cmake --version
ninja --version
$PSVersionTable.PSVersion
```

## 2. Fetch the SDK

The official ReXGlue SDK is the `sdk` submodule, pinned to a specific commit. Fetch it with symbolic links enabled, which some of its dependencies need:

```powershell
git -c core.symlinks=true submodule update --init --recursive
```

If the submodules were fetched without symbolic links, `build-sdk.ps1` stops with a message about the `libmspack` checkout; see [Troubleshooting](troubleshooting.md#build-issues).

## 3. Prepare the game files

Place the ISO of your copy in the repository root and extract it:

```powershell
./scripts/extract-game.ps1
```

The script downloads a pinned [extract-xiso](https://github.com/XboxDev/extract-xiso) release, verifies its SHA-256, and writes the disc contents to `game/`. It refuses to write into a folder that already has files. Without exactly one ISO in the root, pass it with `-IsoPath`; `-Destination` writes elsewhere. Extraction only needs to run once.

The result is the disc contents directly in `game/`, without an extra enclosing folder:

```text
game/
├── default.xex
├── xeshader.sdb
└── ...            # remaining disc files and folders
```

The build checks the SHA-256 of `default.xex` and stops on any other revision: the recompilation configuration and the native renderer's hooks use addresses of this revision.

Game files, extracted assets, and anything derived from them stay out of Git.

## 4. Build and install the SDK

```powershell
./scripts/build-sdk.ps1
```

The script applies the patches in `patches/` to the `sdk` submodule, builds the SDK with Clang, and installs it in `build/sdk-install`. Patches that are already applied are detected and left as they are; a patch that neither applies nor is found applied stops the build without touching the SDK's local changes. Use `-Jobs` to limit parallelism and `-Configuration Debug` or `RelWithDebInfo` to install another configuration next to Release.

| Patch | Purpose |
| --- | --- |
| `rexglue-enable-usbcam.patch` | Enables the USB camera exports the game imports. The camera is reported as disconnected; capture is not implemented |
| `rexglue-discovery.patch` | Discovers functions reached only through pointer tables without RTTI or through addresses built in code, and adds the guest return address to missing-function errors. See [Automatic function discovery](automatic-function-discovery.md) |
| `rexglue-xma-decoder.patch` | XMA decoder fixes ported from Xenia Canary, plus local fixes for frames that cross a buffer boundary. The patch header lists each change |

Rebuild the SDK with this script after changing any patch.

## 5. Generate the C++ and build the game

```powershell
./scripts/build-game.ps1
```

The script verifies `default.xex`, runs the ReXGlue code generation on `broken_bond_manifest.toml` (output in `generated/`, ignored by Git), configures the `windows-release` preset, and builds the game and the launcher in `build/game`.

Code generation runs again when the installed SDK changes, even if its version string does not, because the local patches can change the generated code. Use `-Regenerate` to force it, `-Jobs` to limit parallelism, and `-SdkPrefix` to use another installed SDK.

Main files in `build/game/`:

```text
broken_bond.exe            # the game, with the Xenos renderer
broken_bond_launcher.exe
broken_bond.toml           # written by the launcher
rexruntime.dll
rexgpu-xenos.dll
assets/                    # launcher artwork and fonts
```

## 6. Native renderer (optional)

The [native renderer](renderers.md) is built into a separate directory, `build/native`, so `build/game` keeps the Xenos-only executable. The native executable also carries Xenos, and the renderer is chosen at launch. Build the game once with `build-game.ps1` first; the native build reuses its generated code.

It needs:

- a clean [Plume](https://github.com/renderbag/plume) checkout at commit `d72379344dacd3dbf9f810f92ddc87e6de1845b1`, with its `contrib/D3D12MemoryAllocator` submodule at `9ef66bc14edd10dee0de3a545b98578363552f66`. Configuration stops if either one is at another commit or has local changes;
- XenosRecomp's dependencies (`fmt`, `dxc-bin`, `smol-v`, `xxHash`, and `zstd`) at the revisions in [`tools/native-renderer/dependencies.json`](../tools/native-renderer/dependencies.json), cloned into `tools/xenosrecomp/thirdparty/`, where Git ignores them;
- `game/xeshader.sdb`, from the extracted game files;
- Python 3 on `PATH`.

```powershell
git clone https://github.com/renderbag/plume.git out/plume
git -C out/plume checkout d72379344dacd3dbf9f810f92ddc87e6de1845b1
git -C out/plume submodule update --init contrib/D3D12MemoryAllocator

$deps = (Get-Content tools/native-renderer/dependencies.json -Raw | ConvertFrom-Json).xenosrecomp_thirdparty
foreach ($name in 'fmt', 'dxc-bin', 'smol-v', 'xxHash', 'zstd') {
  git clone $deps.$name.url "tools/xenosrecomp/thirdparty/$name"
  git -C "tools/xenosrecomp/thirdparty/$name" checkout $deps.$name.revision
}

./scripts/build-game-native.ps1 -PlumeSource out/plume -XenosRecompThirdparty tools/xenosrecomp/thirdparty
```

The build translates the game's shaders with the XenosRecomp fork in `tools/xenosrecomp/`, compiles them with DXC, and embeds the result in `broken_bond.exe`, so no shader compiler runs during play. The first build translates every shader of `xeshader.sdb` and takes several minutes; `build/shader-store` keeps the results, and later builds reuse them while the game files, the fork, and DXC do not change. The shader cache derives from your game files and stays out of Git.

### Pipeline list

The list of pipelines compiled at startup, `src/gpu/shaders/pipeline_cache.inc`, is optional and also stays out of Git, because it is generated from local play sessions. Without it, the build succeeds, and each pipeline compiles the first time it appears, with a short pause.

Every native run started by `run-game.ps1` leaves the pipelines it created in `build/pipeline-runs/<run>/pipelines.json`. To add them to the list and embed it:

```powershell
./scripts/update-pipeline-cache.ps1
./scripts/build-game-native.ps1 -PlumeSource out/plume -XenosRecompThirdparty tools/xenosrecomp/thirdparty
```

The script starts from the current list, so its entries and order are kept. `-Sources` builds the list from the given files only.

## 7. Run

### With the launcher

```powershell
./build/game/broken_bond_launcher.exe
```

The launcher finds the repository's `game/` folder, or a folder you select under **Game Files**. **Play Game** checks `default.xex` against the supported SHA-256, saves the settings, and starts `broken_bond.exe` from its own folder, which uses Xenos. See [Configuration](configuration.md) for the settings and where saves and logs go.

To build only the launcher and run its regression tests, without generating the game code again:

```powershell
./scripts/build-launcher.ps1 -Test
```

### From the repository

```powershell
./scripts/run-game.ps1                    # Xenos
./scripts/run-game.ps1 -Keyboard          # with the SDK's keyboard controller emulation
./scripts/run-game.ps1 -Renderer native   # the build of build-game-native.ps1
```

The script uses `game/` for the game files and `build/user/` for saves and caches, and writes the log to `build/logs/runtime-<date>.log`. `-LogLevel debug` or `trace` adds detail. With `-Renderer native`, it also writes the renderer's journal next to the log (`.native.jsonl`) and the run's pipelines to `build/pipeline-runs/<run>/`.

Keyboard defaults include Enter for Start, Space for A, Backspace for B, and WASD for the left stick.

### Directly

```powershell
./build/game/broken_bond.exe --game_data_root="C:/path/to/game"
./build/native/broken_bond.exe --game_data_root="C:/path/to/game" --renderer native
```

Without `--user_data_root`, the SDK keeps saves and caches in its default user folder instead of `build/user/`.

## Tests

| Command | What it checks |
| --- | --- |
| `./scripts/build-launcher.ps1 -Test` | Launcher configuration and path handling, with synthetic fixtures; no game files needed |
| `python -I tools/native-renderer/test_sdb_extraction.py` | The shader container extraction of the native renderer's build, with synthetic data |
| The `[pointer_table]` and `[code_address]` SDK unit tests | The discovery patch; see [Automatic function discovery](automatic-function-discovery.md) for how to build and run them |

A successful build or passing tests do not establish that the game runs; run it and check the log.
