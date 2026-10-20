# Renderers

[Documentation](README.md) · [Configuration](configuration.md#renderer) · [Build and run](build-and-run.md#6-native-renderer)

This page explains the native renderer, its frame rate option, how it handles pipelines, and what is still open. The option reference is in [Configuration](configuration.md#renderer).

## The native renderer

The game draws with the native Direct3D 12 renderer. Instead of emulating the Xbox 360 GPU, it intercepts the game's Direct3D calls (device, resources, render states, draws, and presents) and issues them with Direct3D 12 through [Plume](https://github.com/renderbag/plume). The game's shaders are translated ahead of time by the XenosRecomp fork in `tools/xenosrecomp/`, compiled when the port is built, and embedded in `broken_bond.exe`, so no shader compiler runs during play.

It is the renderer of the [Naruto: Rise of a Ninja recomp](https://github.com/jorjbarbosa/naruto-rise-recomp), the first game's port, adapted to this game: the hooks follow this game's addresses, and the parts specific to this game, such as its instanced draws in the open world, are its own. It has been played through the menus, the start of Story Mode with its first fights and cutscenes, Fight Mode, and the open world, at the game's 30 fps and at 60 fps, without missing draws.

Without an emulation layer, it offers options the console did not have: frame rates above the game's own, MSAA, transparency antialiasing, a fractional internal resolution, brightness, frame latency, triple buffering, and graphics card selection.

## Starting the game

Build the game and its launcher ([Build and run](build-and-run.md#6-native-renderer)), then start it with the launcher (`build/native/broken_bond_launcher.exe`) or from the repository:

```powershell
./scripts/run-game.ps1
```

## Frame rate

### The game's rates

The game advances its logic in fixed steps: 1/30 s in the open world, and 1/60 s in fights. The menus measure the time of each frame instead. Animation, physics, and timers depend on that step, so the console's frame rate follows it: 30 in the open world, 60 in fights. The native renderer keeps these rates by default.

The native renderer paces its frames with its own limiter and writes the rate and the frame time of the last half second in the window title, for example `| 30.0 fps | 33.3 ms (max 36.1)`. Set the environment variable `REX_NATIVE_FPS_TITLE=0` to hide it.

### Frames between the logic steps

With the native renderer, `frame_rate` in `build/native/broken_bond.toml` sets the frame rate, from 30 to 240:

```toml
frame_rate = 60
```

At 30, the default, the game runs as on the console. Above 30, the game's logic keeps its own step, and the renderer draws the frames in between. For each of those frames it:

- places objects and the camera between their positions at the last two logic steps;
- does the same for the characters' skinned poses;
- advances visual effects, such as particles and dust, by the frame's share of the step.

Game speed, physics, and timing stay those of the console, because the logic itself does not change. The menus run at the chosen rate with their measured time.

Where the step is not well below the frame time, the frames follow the step instead, so the game keeps its speed: fights show at 60 when `frame_rate` is below `76`, and the open world shows at 30 when it is between `31` and `37`. With VSync on, the rate does not exceed the monitor's refresh rate; choose a rate the refresh rate is a multiple of, such as 60 on a 60, 120, or 240 Hz monitor.

### Limits

- The image trails the logic by one step: up to about 33 ms in the open world and 17 ms in fights.
- So far, 60 has been played in the open world, in fights, with the pause menu, and across loading screens. Higher rates have not been played yet.

## Pipelines

A pipeline is the combination of shaders, vertex layout, and render states a draw needs. Direct3D 12 compiles it before the draw can run. The native renderer never skips a draw for a missing pipeline: the draw waits for it instead.

- **Startup list.** Builds that carry a list of the pipelines seen in play (`src/gpu/shaders/pipeline_cache.inc`, see [Build and run](build-and-run.md#pipeline-list)) compile it in the background at startup.
- **Ahead of each batch.** The pipelines a batch of draws lacks compile in parallel before its draws run.
- **When a world loads.** The pipelines the list knows for that world are compiled when the game loads it.
- **Loading screens.** While the game shows a loading screen, the compilation threads run at the highest priority.

The graphics driver keeps compiled pipelines in its own cache between sessions, so a pipeline outside the list is slow only the first time it appears on a given driver. In a test without the list and with the driver cache already filled, each pipeline was created in about 0.2 ms.

## Known issues

| Issue | Details |
| --- | --- |
| Short pause when something new appears | A pipeline outside the startup list compiles the first time it appears on a given driver, and the draw waits for it. New areas of the open world can stutter on the first visit; later visits do not |
| Frame rates above 60 not yet played | `frame_rate` accepts up to 240, but only 60 has been played; see [Frame rate](#frame-rate) |
| Diagnostic mode stops the game | `REX_NATIVE_DIAGNOSTICS=1` stops the game during device creation; leave it unset |
| No sound | The SDK opens the Windows default playback device at startup; see [Troubleshooting](troubleshooting.md#no-sound) |
| Sounds missing late in a session | Reported once, before the audio fixes; not seen since, but not checked specifically |

## Reporting problems

Include the graphics card and driver, the internal resolution, the renderer options if you changed them, and the log of the run. For a run started by `run-game.ps1`, the journal next to the log (`.native.jsonl`) helps too.
