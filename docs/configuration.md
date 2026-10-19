# Configuration

[Documentation](README.md) · [Renderers](renderers.md) · [Build and run](build-and-run.md)

## Where to configure settings

Use the launcher whenever possible. It reads and saves `broken_bond.toml` **next to its executable** (`build/game/broken_bond.toml` in a repository build) when you play or close it. The game reads the same file when it starts from that folder, also without the launcher. Close both the game and the launcher before editing the file by hand.

The native renderer's executable lives in `build/native/` and reads `build/native/broken_bond.toml`. The launcher does not write that file; create or edit it by hand for the native renderer's options.

Every option can also be set as an environment variable named `REX_` plus the key in capitals (such as `REX_MSAA_SAMPLES=4`), or on the command line (`--msaa_samples 4`). The command line takes precedence over environment variables, and both over the file.

## Launcher

The launcher has three tabs: **Play**, **Settings**, and **Game Files**. **Play Game** checks `default.xex` against the supported SHA-256, saves the settings, and starts `broken_bond.exe` from the launcher's folder.

The **Settings** tab has three groups. **Reset to Defaults** restores the values in the table under [Main settings](#main-settings).

| Group | Options |
| --- | --- |
| Game | Game language |
| Graphics Quality | Resolution, texture filtering, anti-aliasing, post-processing, fullscreen, VSync, dithering. The **Display** and **Renderer** rows show the monitor and the renderer in use (Xenos) |
| Controls | SDL controller backend, keyboard controller emulation; the connected controller is shown below |

The **Game Files** tab detects the repository's `game/` folder, or a `game/` folder next to the launcher, and lets you select another folder that contains `default.xex`. Your original files are never moved or deleted. A selected folder that no longer exists is reported; the launcher does not fall back to another location.

## Game language

Open **Settings → Game → Game language**. The selection is applied on the next launch. Text and voice availability depends on the language assets in your game files.

| Language | `user_language` |
| --- | --- |
| English (default) | `1` |
| French | `4` |
| German | `3` |
| Spanish | `5` |
| Italian | `6` |

A code set by hand outside this list is kept and shown as **Custom (code)** until you select a listed language.

The voices (English or Japanese) are chosen in the game's own options menu.

## Main settings

Defaults written by the launcher:

| Key | Default | Effect |
| --- | --- | --- |
| `user_language` | `1` | Game language; see the codes above |
| `resolution_scale` | `1` | Internal resolution: 1 (1280×720, the original) to 4 (5120×2880) |
| `draw_resolution_scale_x`, `draw_resolution_scale_y` | `1`, `1` | Draw scales, kept equal to `resolution_scale` by the launcher |
| `anisotropic_override` | `3` | Texture filtering in the SDK's encoding: `-1` the game's, `0` off, `1` to `5` for 1× to 16×; `3` is 4× |
| `swap_post_effect` | `"none"` | `"fxaa"` or `"fxaa_extreme"` for edge smoothing |
| `present_effect` | `"bilinear"` | `"cas"` (sharpen) or `"fsr"` (upscale) when presenting |
| `present_dither` | `false` | Dithering when presenting (Xenos only) |
| `fullscreen` | `true` | Fullscreen mode |
| `vsync` | `true` | Synchronize presentation with the display |
| `input_backend` | `"sdl"` | `"sdl"` for Xbox and PlayStation controllers through SDL, `"xinput"` for XInput only |
| `mnk_mode` | `false` | Keyboard controller emulation: Enter = Start, Space = A, Backspace = B, WASD = left stick |
| `gpu_plugin` | `"xenos"` | The SDK's GPU plugin |
| `custom_game_root` | `""` | The game folder selected in **Game Files**; empty uses the detected one |

Other SDK options can be added to the file by hand; the launcher keeps the keys it does not know when it saves. `log_level = "debug"` adds detail to the log.

## Renderer

Builds with the native renderer (`scripts/build-game-native.ps1`, see [Build and run](build-and-run.md#6-native-renderer-optional)) carry both renderers in the same executable, chosen at launch:

| Renderer | `renderer` |
| --- | --- |
| Xenos, the SDK's emulated Xbox 360 GPU (default) | `"xenos"` |
| Native, the game's Direct3D calls drawn with Direct3D 12 | `"native"` |

`scripts/run-game.ps1 -Renderer native` passes `--renderer native` for you. Builds without the native renderer ignore the key and always use Xenos. [Renderers](renderers.md) explains how the two differ.

### Options of each renderer

| Option | Xenos | Native |
| --- | --- | --- |
| Resolution (`resolution_scale`) | 1× to 4× | 1× to 4×, or a fraction with `internal_resolution_scale` |
| Texture filtering, FXAA, CAS/FSR, VSync, fullscreen | Yes | Yes |
| Dithering (`present_dither`) | Yes | No |
| The options below | No | Yes |

### Native renderer options

These keys exist only in native renderer builds. Each takes effect on the next launch.

| Key | Default | Effect |
| --- | --- | --- |
| `msaa_samples` | `0` | Samples of the targets the game creates with MSAA: `0` keeps the game's, or `2`, `4`, `8`, limited to what the graphics card supports. Single-sample targets stay single-sample |
| `transparency_antialiasing` | `false` | In multisampled targets, alpha-tested draws use alpha to coverage instead of the alpha test, smoothing the edges of cut-out surfaces |
| `internal_resolution_scale` | `0.0` | Internal resolution from 1 to 4 with fractions (`1.5` turns 1280×720 into 1920×1080); `0` uses `resolution_scale` |
| `brightness` | `0.5` | Output brightness from 0 to 1; `0.5` is the game's own |
| `max_frame_latency` | `1` | Frames queued ahead of the display, 1 to 3; 1 has the lowest latency |
| `triple_buffering` | `false` | Three swapchain buffers instead of two |
| `graphics_device` | `""` | The graphics card, by the name Windows gives it; empty is automatic. The log names the card in use (`Native renderer adapter:`) |

For example, in `build/native/broken_bond.toml`:

```toml
renderer = "native"
msaa_samples = 4
transparency_antialiasing = true
internal_resolution_scale = 1.5
```

## User files

| Started with | Game files | Saves and caches | Logs |
| --- | --- | --- | --- |
| The launcher, in a repository build | `game/`, or the folder selected in **Game Files** | `build/user/` | `build/game/logs/` |
| `scripts/run-game.ps1` | `game/` | `build/user/` | `build/logs/runtime-<date>.log` |
| The launcher copied elsewhere | A `game/` folder next to it, or the selected folder | A `user/` folder next to it | `logs/` next to it |

The launcher and `run-game.ps1` share `build/user/`, so saves carry over between them. Native runs started by `run-game.ps1` also write the renderer's journal next to the log (`.native.jsonl`) and the pipelines they created to `build/pipeline-runs/<run>/`.
