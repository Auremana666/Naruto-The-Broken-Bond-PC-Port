# Naruto: The Broken Bond Recomp

Static recompilation project for the Xbox 360 version of **Naruto: The Broken Bond**, using the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

## Current status

The repository has been initialized. There is no recompiled game code, executable-specific configuration, or validated runnable build yet.

- `main`: empty starting point for the project.
- `develop`: working branch for recompilation.
- `sdk`: official ReXGlue SDK submodule, pinned to a specific Git revision.

## Setup

After cloning this repository:

```sh
git switch develop
git submodule update --init --recursive
```

Game images and extracted assets remain local. Use `game/` for extracted content. Git ignores disc images, archives, XEX executables, code under `generated/`, and builds under `build/`.

## Initial scope

The goal is to get the recompiled game running. Launcher development and custom renderer work are outside this phase.

1. Extract and identify the game executable and its revision.
2. Prepare the ReXGlue tool and recompilation configuration for that executable.
3. Generate and compile the translated code with the SDK runtime.
4. Validate startup and resolve issues required to reach gameplay.

Build configuration and commands will be added as they are verified against the SDK and the local executable.

## Project language

All code, comments, documentation, and descriptions must be written in English. This also applies to commit messages and pull request titles and descriptions.
