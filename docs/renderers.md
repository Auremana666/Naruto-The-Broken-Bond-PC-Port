# Renderers

[Documentation](README.md) · [Configuration](configuration.md#renderer) · [Build and run](build-and-run.md#6-native-renderer-optional)

The port can draw the game with two renderers. This page explains how they differ, how the native renderer handles pipelines, and what is still open. The option reference is in [Configuration](configuration.md#renderer).

## The two renderers

### Xenos (default)

The ReXGlue SDK's emulation of the Xbox 360 GPU, derived from Xenia. It executes the command buffers the game writes for the GPU, translates the game's shaders the first time they appear, and keeps them in the user cache. It is the renderer of the launcher and of every build, and it stays the default until the native renderer has been checked across the whole game.

### Native (optional build)

The native renderer replaces the GPU emulation. It intercepts the game's Direct3D calls (device, resources, render states, draws, and presents) and issues them with Direct3D 12 through [Plume](https://github.com/renderbag/plume). The game's shaders are translated ahead of time by the XenosRecomp fork in `tools/xenosrecomp/`, compiled when the port is built, and embedded in `broken_bond.exe`, so no shader compiler runs during play.

It is the renderer of the [Naruto: Rise of a Ninja recomp](https://github.com/jorjbarbosa/naruto-rise-recomp), the first game's port, adapted to this game: the hooks follow this game's addresses, and the parts specific to this game, such as its instanced draws in the open world, are its own. It has been played through the menus, the start of Story Mode with its first fights, and the open world, without missing draws. It is built separately (`scripts/build-game-native.ps1`) and is not offered by the launcher yet.

Without the emulation layer, it offers options that Xenos does not have: MSAA, transparency antialiasing, a fractional internal resolution, brightness, frame latency, triple buffering, and graphics card selection.

### Comparison

| | Xenos | Native |
| --- | --- | --- |
| Drawing | Emulated Xbox 360 GPU (Direct3D 12 backend) | The game's Direct3D calls on Direct3D 12 |
| Shaders | Translated on first appearance, cached per user | Translated at build time, embedded in the executable |
| Frame rate | The game's own | The game's own |
| Resolution | 1× to 4× | 1× to 4×, with fractions |
| Anti-aliasing | FXAA, CAS/FSR | The game's MSAA or 2×/4×/8×, transparency antialiasing, FXAA, CAS/FSR |
| Frame rate display | — | The window title |
| Started by | The launcher, `run-game.ps1` | `run-game.ps1 -Renderer native`, or `renderer = "native"` |

## Choosing the renderer

Build the native renderer ([Build and run](build-and-run.md#6-native-renderer-optional)), then start it from the repository:

```powershell
./scripts/run-game.ps1 -Renderer native
```

Or set `renderer` in `build/native/broken_bond.toml` and start `build/native/broken_bond.exe`:

```toml
renderer = "native"   # or "xenos"
```

Without the key, Xenos is used. The choice takes effect on the next launch. The log names the graphics card in use: `DXGI adapter:` for Xenos, `Native renderer adapter:` for the native renderer.

## Frame rate

Both renderers run the game at its own frame rate. The native renderer paces its frames with a limiter on the presentation interval the game asks for, and writes the rate and the frame time of the last half second in the window title, for example `| 30.0 fps | 33.3 ms (max 36.1)`. Set the environment variable `REX_NATIVE_FPS_TITLE=0` to hide it.

Frame rates above the game's own, which the Rise of a Ninja port reaches by drawing frames between the game's logic steps, are not available in this port yet.

## Pipelines

A pipeline is the combination of shaders, vertex layout, and render states a draw needs. Direct3D 12 compiles it before the draw can run. The native renderer never skips a draw for a missing pipeline: the draw waits for it instead.

- **Startup list.** Builds that carry a list of the pipelines seen in play (`src/gpu/shaders/pipeline_cache.inc`, see [Build and run](build-and-run.md#pipeline-list)) compile it in the background at startup.
- **Ahead of each batch.** The pipelines a batch of draws lacks compile in parallel before its draws run.
- **When a world loads.** The pipelines the list knows for that world are compiled when the game loads it.
- **Loading screens.** While the game shows a loading screen, the compilation threads run at the highest priority.

The graphics driver keeps compiled pipelines in its own cache between sessions, so a pipeline outside the list is slow only the first time it appears on a given driver. In a test without the list and with the driver cache already filled, each pipeline was created in about 0.2 ms.

## Known issues

| Issue | Renderer | Details |
| --- | --- | --- |
| Short pause when something new appears | Native | A pipeline outside the startup list compiles the first time it appears on a given driver, and the draw waits for it. New areas of the open world can stutter on the first visit; later visits do not |
| Not in the launcher | Native | Start it with `run-game.ps1 -Renderer native` or `renderer = "native"`; see [Choosing the renderer](#choosing-the-renderer) |
| No frame rates above the game's | Native | See [Frame rate](#frame-rate) |
| Diagnostic mode stops the game | Native | `REX_NATIVE_DIAGNOSTICS=1` stops the game during device creation; leave it unset |
| Black frames | Xenos | Black frames flicker during play. `execute_unclipped_draw_vs_on_cpu = true` in the configuration stops them |
| No sound | Both | The SDK opens the Windows default playback device at startup; see [Troubleshooting](troubleshooting.md#no-sound) |
| Sounds missing late in a session | Both | Reported once, before the audio fixes; not seen since, but not checked specifically |

Not issues, but easy to mistake for one:

- **The title screen changes between launches.** The game picks Konoha's time of day at random each time the title loads (morning, evening, night, and others), on both renderers. Compare renderers only when both show the same time of day.
- **Jutsu images stay on screen for about a second.** The original game shows them for the same time.

## Reporting problems

Include the renderer, the graphics card and driver, the internal resolution, the native renderer's options if you changed them, and the log of the run. For a native run started by `run-game.ps1`, the journal next to the log (`.native.jsonl`) helps too. If a problem appears only with the native renderer, check whether it also happens with Xenos and mention the result.
