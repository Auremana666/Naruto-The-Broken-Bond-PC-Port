# Broken Bond launcher

This Windows launcher reuses the current SDL3/ImGui launcher from the sibling
`naruto-rise-dev` checkout, commit `4e8766dd562745233b4a05d99f8e62b2475ef589`.
The window dimensions, cover crop, theme, Lato fonts, widgets, settings groups,
description panel, sliders, and key mapping screen come directly from that
source. The three remaining tabs share the space formerly used by four. The
title, icon, cover, executable, configuration, and save paths identify Broken
Bond. There is no DLC page or installer.

## Feature parity

| Rise feature | Broken Bond implementation |
| --- | --- |
| Play and game readiness | Same page; additionally verifies the supported `default.xex` SHA-256 before launch |
| Interface languages | English, Portuguese, French, German, Spanish, Italian, Russian; English by default |
| Game language | English, French, German, Spanish, Italian; custom codes are preserved |
| Settings layout | Game, Display, Quality, Controls, Advanced; same description panel and recommended-settings action |
| Graphics card | Physical Direct3D 12 adapter names; bridges the saved name to the SDK adapter on Xenos and the existing native selector |
| Graphics quality | Resolution 1x–4x, texture filtering, FXAA, CAS/FSR, dithering, VSync, fullscreen |
| Native options | Renderer selection, fractional resolution, brightness, frame rate, MSAA, transparency AA, frame latency, triple buffering, in native builds |
| Controllers | SDL/XInput switch and connected-controller name |
| Keyboard and mouse | Enable switch, mouse-to-right-stick camera, sensitivity, 24 controller actions with two editable alternatives each |
| Key mapping | Physical key names, mouse buttons, five-second capture, duplicate reassignment, reserved-key rejection, right-click clearing, default restoration |
| FPS overlay | Rise's F1 overlay on Xenos; explicitly labels guest swap timing or the host fallback |
| Skip intro videos | Broken Bond hook skips only `Ubisoft.bik`; see the [game evidence](../docs/launcher-game-features.md) |
| Game files | Select, reset, and open an extracted folder; a missing custom path never silently falls back |
| Version under the cover | Optional `broken_bond.version`, otherwise `BROKENBOND_VERSION` (`dev` by default) |
| Ultrawide | Same row, disabled with a localized explanation; camera and presentation contracts remain unvalidated |
| Direct exploration mouse camera | SDK stick emulation works; Rise's separate script-module camera hook is not ported |

Xenos remains the default, including builds that offer Native. Native frame rate
starts at the game's original 30 fps. Other recommended graphics defaults match
Rise: original resolution, 16x filtering, FXAA, and CAS. Existing choices load
from `broken_bond.toml` beside the launcher. Unknown keys and malformed files are
preserved; failed saves are reported. Valid configuration is replaced only after
a complete temporary-file write.

Keyboard triggers bound to Shift and Control require
`patches/rexglue-mnk-modifiers.patch`, applied by `scripts/build-sdk.ps1`. This
ports the relevant Rise driver correction, allowing triggers and movement to be
held together while preserving explicit modifier combinations. The SDK revision
remains pinned.

## Build and run

```powershell
./scripts/build-launcher.ps1 -Test
# For build/native/broken_bond.exe, built with the native renderer:
./scripts/build-launcher.ps1 -NativeRenderer -Test
```

The first command installs the launcher and assets in `build/game/`; the second
uses `build/native/`. Both repository layouts find `game/` and share `build/user/`.
A portable copy finds its adjacent `game/` and uses its adjacent `user/`, unless
`user_data_root` was configured manually. Relative overrides resolve against the
launcher. Original game files are never moved or deleted.

The root CMake build includes the launcher on Windows. Set
`BROKENBOND_BUILD_LAUNCHER=OFF` for a game-only build. Standalone CMake uses
`BROKENBOND_LAUNCHER_NATIVE_RENDERER=ON` only for an executable carrying that
renderer. `scripts/build-game-native.ps1` builds both the game and its matching
launcher. No installer or ISO import helper is built.

## Validation and limitations

The four test targets cover configuration and paths, process argument quoting,
key names and defaults, all seven localization tables, and the actual SDK input
driver. The configuration test accepts an optional second argument pointing to
the supported local `default.xex`. Tests otherwise use synthetic fixtures.

Local validation and remaining runtime boundaries are recorded in
[launcher validation](../docs/launcher-validation.md) and the
[game-feature evidence ledger](../docs/launcher-game-features.md). Building the
launcher alone does not establish that the game runs. Full story playback,
controller hardware coverage, and direct mouse-camera parity require separate
validation.

## Attribution

Reused launcher code is copyright (c) 2026 Jorge Barbosa and covered by this
repository's [GPL-3.0-only license](../LICENSE). SDL3 and toml++ come from the
installed SDK; ImGui and stb come from the pinned `sdk` submodule. The launcher
uses Direct3D 11 for its own window; game rendering is configured separately.
Lato fonts retain their [SIL Open Font License](assets/fonts/OFL.txt).

The existing user-supplied `assets/banner-broken-bond.png` and `assets/favicon.ico`
are preserved. The banner keeps the centered cover crop. The icon is embedded
and used for both Windows icon sizes. This artwork is not covered by the code
license. No game data is distributed.
