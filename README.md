# Naruto: The Broken Bond — PC Port

An unofficial PC port of **Naruto: The Broken Bond** (Xbox 360, 2008), made by statically
recompiling the original game with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) and
adding a full PC options menu on top.

**Version 2** runs on a **native Direct3D 12 renderer** (from
[jorjbarbosa's Broken Bond recomp](https://github.com/jorjbarbosa/broken-bond-recomp)) instead of
the emulated Xbox graphics of v1: higher frame rates, sharper resolutions and smoother play.

**[▶ Watch the trailer](https://youtu.be/j7WtfO7-Km8)**

![Title screen](docs/images/title-screen.jpg)

> **No game files are included.** You need your own copy of the game as an Xbox 360 disc image
> (ISO). This project is not affiliated with or endorsed by Ubisoft, Bandai Namco, Shueisha, Viz
> Media or Microsoft. Naruto is a trademark of its owners.

## Download and play

1. Download the latest zip from the [Releases](../../releases) page and extract it.
2. Put your game's **ISO** file in the `assets` folder (or next to `Naruto The Broken Bond PC.exe`).
3. Run **Naruto The Broken Bond PC.exe**.
4. Press **F1** (or **Back + Start** on a controller) to open **PC Options**.

Keep the game folder somewhere you can write to, such as `C:\Games`, your Desktop, Documents or
another drive. **Don't put it in `C:\Program Files`**: the game saves your progress (`hdd`) and
settings (`naruto.toml`) next to the exe, and Windows blocks that there.

**Coming from v1?** Copy your `hdd` folder from the v1 folder into the v2 folder to keep your saves
(back them up first with the Save Backup Tool).

The first start can take a moment while Windows scans the new program.

## What's new in v2

![Gameplay](docs/images/gameplay-16-9.jpg)

- **Native Direct3D 12 renderer.** The game's graphics run straight on Direct3D 12 instead of an
  emulated Xbox GPU, with the shaders translated ahead of time.
- **Frame rate up to 240 FPS.** The game still thinks at its own 30 steps per second; above 30 the
  frames in between are drawn smoothly (camera, characters, effects).
- **Render resolution from 720p up to 5K**, real **anti-aliasing OFF / 2X / 4X / 8X**,
  transparency anti-aliasing, FXAA, AMD CAS / FSR upscaling, brightness.
- **Field of view** (PC Options > Display, 90% to 140%), applied right away. The camera zooms the
  game does on its own (the gate in the village, fight cameras) now keep your field of view too.

  ![Field of view 140%](docs/images/field-of-view-140.jpg)
- **Ultrawide that really widens the view:** with 21:9 or 32:9 the game shows more of the world at
  the sides instead of stretching it.

  ![21:9 on a 16:9 screen](docs/images/ultrawide-21-9.jpg)
- **Mouse camera:** the camera turns by how far you move the mouse, like a PC game
  (PC Options > Controls > Mouse Moves Camera).
- **Fewer stutters:** the graphics the game needs are prepared in advance (a cache built from many
  play sessions), and **Skip Unready Graphics** skips an object for a moment instead of freezing
  the game when something new still has to be prepared.
- **New Debug page** (PC Options > Debug): log files, a graphics and performance log to send in
  with a problem, and Skip Unready Graphics.

## Performance

On a desktop PC with a dedicated graphics card the game runs smoothly at high frame rates and
resolutions. The work per frame was cut down a lot for v2 (the stutter freezes are gone, the
render thread does about 20% less work per draw, and Anti-Aliasing OFF is now really off).

On **laptops and PCs with built-in graphics** (Intel Arc / Iris, AMD Radeon integrated), start
with these settings in **PC Options > Graphics**:

| Setting | Laptop / built-in graphics |
|---|---|
| Frame rate | 60, or 30 if it still feels uneven |
| Render resolution | 1280x720 (Original) |
| Anti-aliasing | OFF |
| Edge smoothing | FXAA |

That's as far as this port can be pushed. Half of every frame is the original Xbox game's own
drawing code, and above 30 FPS it runs twice per game step, so weaker CPUs and graphics chips hit
a limit that settings can't remove. It won't get much better than this on low-end hardware.

If you get stutters, turn on **PC Options > Debug > Record Graphics Log**, play, and share the
newest folder in `pipeline-runs`: it shows exactly where the time goes.

## Features

| | |
|---|---|
| ![PC Options](docs/images/pc-options.jpg) | ![Display](docs/images/display.jpg) |
| ![Graphics](docs/images/graphics.jpg) | ![Debug](docs/images/debug.jpg) |
| ![Controller layout](docs/images/controller-layout.jpg) | ![Auto Jutsu](docs/images/auto-jutsu.jpg) |

- **PC Options menu** (F1 or Back + Start), styled like the game's own menus, usable with keyboard,
  mouse or controller. Every change is saved to `naruto.toml`.
  - **Display:** fullscreen or windowed, window size, **aspect ratio (16:9, 21:9, 32:9 or
    automatic)**, **field of view**, V-Sync, G-Sync / FreeSync, FPS counter.
  - **Graphics:** frame rate, render resolution, anti-aliasing, transparency anti-aliasing, edge
    smoothing, upscaling, brightness, **graphics card** (on PCs with more than one), texture
    filtering.
  - **Controls:** play with the keyboard, keyboard as player 1 or 2, mouse camera and sensitivity,
    full key remapping with normal / toggle / turbo presses.
  - **Controller layout:** separate layouts for **Controller 1** and **Controller 2**. Pick a button
    on the drawn controller and press the button it should become, or make it a toggle or turbo.
  - **Audio** and **Game:** mute, language, intro videos on/off, PC title screen on/off.
  - **Cheats:** unlock every character, unlock every arena (your save is not changed), and
    **Auto Jutsu**.
  - **Shortcut Keys:** rebind the shortcuts and jump straight to any page.
  - **Debug:** log files, Record Graphics Log, Skip Unready Graphics.
- **Auto Jutsu:** one button does your character's jutsu for you.
  - Keyboard: **Numpad 1–4** for jutsu 1–4.
  - Controller: hold your jutsu button (**LT**) and press **A, B, Y or X**.
  - Keep holding to charge the jutsu, let go to release it. You still need enough chakra.
- **PC title screen** at start (30 seconds, or press Enter), and the intro videos can be skipped.
- **Save Backup Tool:** run `Save Backup Tool\Save Backup Tool.bat` to zip your saves. Share the
  zip if you need help with a save problem.

| | |
|---|---|
| ![Story](docs/images/story.jpg) | ![Fight](docs/images/fight-kakashi-itachi.jpg) |

## Default controls

| Action | Keyboard | Controller |
|---|---|---|
| Move | W A S D | Left stick |
| Camera | Arrow keys (or the mouse) | Right stick |
| Jump / Confirm | Space | A |
| Block / Back | L | B |
| Basic attack | J | X |
| Heavy attack | K | Y |
| Open inventory | Q | LB |
| Jutsus / Hand seals | Shift | LT |
| Kunai attack | E | RB |
| Sprint / Grab | I | RT |
| Left / right stick click | F / G | LS / RS |
| D-pad | 1 2 3 4 | D-pad |
| Start (pause) / Back | Enter / Tab | Start / Back |

| Shortcut | Key |
|---|---|
| PC Options | F1 (or Back + Start) |
| FPS counter | F2 |
| Log files on / off | Ctrl + F12 |

All keys can be changed in **PC Options > Controls > Key Bindings** and **PC Options > Shortcut Keys**.

## Two players and saves

With a controller connected, **PC Options > Controls > Keyboard Player** decides who the keyboard
controls: shared with player 1, player 1 (controllers become player 2+), or player 2.

**Note:** if you set the keyboard as **player 2** and restart the game, the game signs in with a
different player profile, so it starts a **new save file**. Your old save is not lost: set the
keyboard back and restart to see it again.

## Known issues

- Laptops and built-in graphics: see [Performance](#performance).
- Some virtual controller drivers (DS4Windows / ViGEm, Rainway) make a controller show as
  "connected" even when it's unplugged.

## Building from source

This repository is a fork of [jorjbarbosa/broken-bond-recomp](https://github.com/jorjbarbosa/broken-bond-recomp)
with the PC features added (`src/pc`, `assets`, the SDK patches in `patches`). Building needs your
own copy of the game; see the [recompilation README](docs/recompilation-README.md) and the
[build guide](docs/build-and-run.md). On Windows, with PowerShell 7 and LLVM:

1. `scripts/build-sdk.ps1` (builds the ReXGlue SDK with the patches),
2. `scripts/build-game.ps1` (generates the code from your `default.xex`),
3. `scripts/build-game-native.ps1 -PlumeSource out/plume -XenosRecompThirdparty tools/xenosrecomp/thirdparty -Backend d3d12`.

The v1 code (the emulated-graphics version) is in the [`v1` branch](../../tree/v1).

## Credits

- **Auremana666:** project lead, testing, title screen and trailer.
- **Claude AI (Anthropic):** recompilation, engine fixes and all PC features.
- [jorjbarbosa](https://github.com/jorjbarbosa): the
  [Broken Bond recomp](https://github.com/jorjbarbosa/broken-bond-recomp) and its native
  renderer, which v2 is built on, and [naruto-rise-recomp](https://github.com/jorjbarbosa/naruto-rise-recomp),
  which this port started from.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD-3-Clause), built on
  [Xenia](https://github.com/xenia-project/xenia).
- [Titan One](https://fonts.google.com/specimen/Titan+One) font (SIL Open Font License, see `assets/fonts/TitanOne-OFL.txt`) and
  [stb_image](https://github.com/nothings/stb) (public domain).

## License

The code in this repository is released under the [GNU General Public License v3](LICENSE), the
same license as the projects it builds on. It does not cover the game, its code, art, music or
trademarks, which belong to their owners.
