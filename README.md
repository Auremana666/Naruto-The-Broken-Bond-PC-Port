# Naruto: The Broken Bond Recomp

Static recompilation of the Xbox 360 version of **Naruto: The Broken Bond**, using the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

## Scope and status

The initial focus is recompilation and game startup using the SDK runtime and its Xenos GPU plugin. Launcher development and custom renderer work are outside this phase.

The native executable reaches the game's main menu, including the rendered background and Story Mode, Fight Mode, and Options entries. Extraction, SDK compilation, code generation, native compilation, and startup have been validated. Gameplay has not been tested. See [recompilation notes](docs/recompilation-notes.md) for the executable identity, discovery fixes, and validation evidence.

## Supported input

- Title ID: `55530825`.
- Media ID: `09FFE047`.
- Executable version: `0.0.0.3`.
- `default.xex` SHA-256: `8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576`.

Game images, extracted assets, generated game code, builds, caches, and user data remain local and are ignored by Git. The build script verifies the executable hash before code generation.

## Windows setup

The validated toolchain is Visual Studio 2022 Build Tools with the C++ workload, Clang 19.1.5, CMake, Ninja, and PowerShell 7. Enable Windows symbolic-link support when checking out the SDK dependencies.

```powershell
git switch develop
git -c core.symlinks=true submodule update --init --recursive
```

Extract the ISO in the repository root. The extraction script downloads a pinned official [extract-xiso](https://github.com/XboxDev/extract-xiso) release, verifies its SHA-256, and writes to `game/`. It refuses to overwrite a populated destination.

```powershell
./scripts/extract-game.ps1
```

If multiple images are present, specify `-IsoPath`. Extraction only needs to run once for a given input.

Build and install the pinned SDK locally, then generate and compile the game:

```powershell
./scripts/build-sdk.ps1
./scripts/build-game.ps1
```

Both scripts initialize the Visual Studio x64 build environment. The SDK is installed under `build/sdk-install`; the game executable is `build/game/broken_bond.exe`. Use `-Jobs` to control build parallelism.

The SDK build applies the patches in `patches/` idempotently. `rexglue-enable-usbcam.patch` enables the existing USB camera exports required by the game's imports. The upstream implementation reports the camera disconnected; camera capture is not implemented. `rexglue-discovery.patch` adds conservative discovery of function-pointer tables without RTTI, preserves exception-handler ownership, and improves missing-function diagnostics. The SDK remains pinned to its original commit with these explicit local patches.

The build script also detects changes to the installed SDK binaries and regenerates code even when their version string is unchanged. Use `-Regenerate` to force regeneration explicitly.

See [automatic function discovery](docs/automatic-function-discovery.md) for the algorithm, rejection rules, regression tests, and reproduction commands.

## Run

```powershell
./scripts/run-game.ps1
# Use the SDK's built-in keyboard controller emulation:
./scripts/run-game.ps1 -Keyboard
```

Keyboard defaults include Enter for Start, Space for A, Backspace for B, and WASD for the left stick. Runtime logs go to `build/logs/runtime-*.log`; local user data and caches go to `build/user/`. Use `-LogLevel debug` for diagnostics.

## Repository conventions

- `main` is the primary branch; `develop` is the working branch.
- `sdk/` is the official ReXGlue submodule at the repository root.
- All code, identifiers, comments, documentation, commit messages, and descriptions must be in English, as required by [AGENTS.md](AGENTS.md).

## License

Original project code and documentation are licensed under the [MIT License](LICENSE).
ReXGlue and its dependencies retain their own licenses, including the
[SDK license](sdk/LICENSE). The project license does not cover the original
game executable, assets, or code translated from the game.
