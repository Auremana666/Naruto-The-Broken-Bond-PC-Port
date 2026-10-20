# Troubleshooting

[Documentation](README.md) · [Build and run](build-and-run.md) · [Renderers](renderers.md#known-issues)

## Build issues

| Symptom | What to check |
| --- | --- |
| `Get-FileHash` or other commands are not recognized | The script ran in Windows PowerShell 5.1. Run it from PowerShell 7 (`pwsh`) |
| `Visual Studio Build Tools with the C++ workload are required` | Install Visual Studio 2022 or its Build Tools with the C++ workload, the x64 tools, and a Windows SDK |
| `clang` or `ninja` not found | Put LLVM and Ninja on `PATH`; the scripts load the Visual Studio environment, not these tools |
| Preset parsing error | Use CMake 3.26+; the presets use schema version 6 |
| `The libmspack checkout contains symlink placeholders` | The SDK submodules were fetched without symbolic links. Enable them (Windows Developer Mode, or an elevated shell), then fetch the SDK again: `git submodule deinit -f sdk` and `git -c core.symlinks=true submodule update --init --recursive`. The SDK build reapplies the patches |
| `SDK patch ... cannot be applied or verified` | The `sdk` checkout is at another commit or has other changes. Compare it with the pinned commit and the patch; the script keeps your changes instead of forcing the patch |
| `The game executable does not match the supported revision` | `game/default.xex` is another revision. Only the revision in the [README](../README.md#requirements) is supported |
| `Build the SDK with scripts/build-sdk.ps1 ...` | Build the SDK first, or pass `-SdkPrefix` with an installed SDK |
| Out of memory during compilation | Lower the parallelism, for example `-Jobs 2` |

### Native renderer

| Symptom | What to check |
| --- | --- |
| `plume source must be clean at ...` or `allocator source must be clean at ...` | Check out the pinned commits in Plume and its `contrib/D3D12MemoryAllocator` submodule, without local changes; see [Build and run](build-and-run.md#6-native-renderer) |
| `The shader cache needs game/xeshader.sdb` | Extract the game files into `game/` first |
| `The native renderer mapping only supports the documented Broken Bond executable` | Same as the revision check above |
| Python not found during configuration | Install Python 3 and put it on `PATH` |
| DXC, fmt, or another XenosRecomp dependency not found | Clone the five dependencies into `tools/xenosrecomp/thirdparty/` and pass that folder to `-XenosRecompThirdparty` |
| An updated pipeline list is not embedded | `update-pipeline-cache.ps1` marks `src/gpu/shaders/pipelines.cpp` for rebuilding. After copying a list by hand, change that file's timestamp, or the build keeps the old list |

## Runtime issues

### The launcher cannot find the game

Select the folder that directly contains `default.xex` under **Game Files**. A folder that only contains another folder with the game is not the game folder. You can also set `custom_game_root` in `broken_bond.toml`, or start the game with `--game_data_root`.

### The launcher reports "PC port not found"

The launcher starts `broken_bond.exe` from its own folder. Build the game with `scripts/build-game-native.ps1`, and keep the launcher in `build/native/` or next to a copy of the game executable and its DLLs.

### The game closes at startup or during play

`run-game.ps1` reports the exit code and the log path. Look for these lines near the end of the log:

- **A missing function** (an indirect call to an address with no recompiled function). The message has the guest target, return address, and stack pointer. Discovery is conservative, and such a miss is worth an issue with the log.
- **`Unhandled guest access violation`**: a crash in the game code. Report it with the log and what you were doing.
- **A native renderer stop**: the renderer closes the game with a message and writes the reason in the log and the journal. Report it with both.

### No sound

The SDK opens the Windows default playback device when the game starts. The log names it in the `audio endpoint` line. If sound plays on another device, such as a monitor's speakers, make the intended device the Windows default and start the game again.

### The title screen looks different on each launch

Expected: the game picks Konoha's time of day at random each time the title loads.

### Native renderer

| Symptom | What to check |
| --- | --- |
| Short pauses in a new area | Pipelines compile the first time they appear on your driver; later visits do not pause. See [Pipelines](renderers.md#pipelines) |
| A native option has no effect | The options are read at startup: close the game and the launcher, edit `build/native/broken_bond.toml`, and start again; see [Native renderer options](configuration.md#native-renderer-options) |
| The game stops during startup with `REX_NATIVE_DIAGNOSTICS=1` | Known; leave the variable unset |

## Collecting diagnostics

| Started with | Log |
| --- | --- |
| `scripts/run-game.ps1` | `build/logs/runtime-<date>.log`, and the native renderer's journal next to it (`.native.jsonl`) |
| The launcher | `build/native/logs/` |

For more detail, use `run-game.ps1 -LogLevel debug`, or set `log_level = "debug"` in `broken_bond.toml` and restore `"info"` afterwards.

For a crash with the native renderer, set the environment variable `REX_NATIVE_CRASH_DUMP=stack` before starting the game: the crash then writes `crash-stack.txt`, the host stack, which follows the game's call chain. `REX_NATIVE_CRASH_DUMP=1` also writes a full minidump (`crash.dmp`). The files go to the run's folder in `build/pipeline-runs/` with `run-game.ps1`, or to the folder in `REX_NATIVE_FRAME_DIR`, or to the working directory.

`python -I tools/native-renderer/summarize-native-journal.py <journal>` summarizes a native renderer journal.

When reporting a problem, include:

- the commit you built, Windows version, CPU, GPU, and driver version;
- the renderer, and the native renderer's options if you changed them;
- reproduction steps and the expected result;
- the voice language and the game language;
- the log, reviewing personal paths before sharing.
