# Porting notes: reusing these features in another game

Notes for porting another Xbox 360 game with the ReXGlue SDK and reusing the PC features from this
project. Paths are relative to the repository root.

## How the port was made

1. Build the SDK: rexglue-sdk `v0.10.0` plus `sdk-patches/rexglue-sdk-v0.10.0.patch`.
2. Extract the game's ISO into `game_root/` and create a ReXGlue project (`app/`, with a manifest
   like `app/narutobb_manifest.toml`).
3. `rexglue codegen` generates the recompiled game in `app/generated/`.
4. Run the game. A crash reading "Call to invalid or unregistered function at guest address X"
   means the analysis missed a function: add `"0xX" = {}` to `app/analysis-seeds.toml`, run codegen
   again and rebuild. `tools/seed_loop.ps1` automates this; `tools/find_code_ptr_seeds.py` and
   `tools/find_vtable_seeds.py` find many of them in advance.
5. Game functions are changed with `REX_HOOK_RAW(sub_XXXXXXXX)`; call the original with
   `__imp__sub_XXXXXXXX(ctx, base)`. `ctx.lr` tells who called it.
6. `tools/guest_mem.py` reads the running game's memory (host address = 0x100000000 + guest
   address), which is how game data such as unlock flags and jutsu lists were found.

## Game-independent features (copy and adapt)

| Feature | Files |
|---|---|
| PC Options menu (F1 / Back + Start), game-styled, keyboard/mouse/controller, saves to the .toml | `app/src/options_menu.*` |
| Controller remap with toggle/turbo, separate layouts per controller | `app/src/input_layout*`, SDK patch (input filter) |
| Keyboard as player 1 or 2 | `app/src/player_assignment.*` |
| FPS counter and on-screen messages | `app/src/fps_counter.*` |
| PC title screen with built-in pictures and a secret word (hash checked) | `app/src/title_screen.*`, `app/assets/icon/naruto.rc` |
| Pictures, font and icon built into the exe | `app/assets/icon/naruto.rc`, `OnConfigureFonts` in `app/src/narutobb_app.h` |
| Loading the game straight from an ISO, friendly "ISO missing" message | SDK patch, `app/src/narutobb_app.h` |
| Log files off by default, Ctrl + F12 | SDK patch (`SetFileLogging`), `app/src/narutobb_app.h` |
| Skip intro videos | `app/src/intro_skip.cpp` (hooks the game's CreateFile and world loading) |
| Save Backup Tool | `app/assets/tools/Save Backup Tool.bat` |
| Crash report | `app/src/crash_handler.cpp` |

## Game-specific features (need new research per game)

- **Unlock all characters / arenas** (`app/src/unlock_all.cpp`): hooks the game's unlock check and
  answers "unlocked" only for the character and arena select screens.
- **Auto Jutsu** (`app/src/auto_jutsu.cpp`): reads the current fighter's jutsu list from game
  memory and plays the stick moves through the input filter.

## Release layout

`<Game> PC.exe`, the SDK DLLs, the Visual C++ runtime DLLs, the `.toml` settings, `assets/` (the
player's ISO), `hdd/` (saves, created by the game) and `Save Backup Tool/`.
