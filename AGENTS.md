# Project rules

- Write all code, identifiers, comments, documentation, and descriptions in English. Commit messages and pull request titles and descriptions must also be in English.
- Focus on recompiling Naruto: The Broken Bond for Xbox 360 and getting the game running with ReXGlue. Reuse the Rise of a Ninja launcher and Inno Setup installer flow. Native renderer reuse and reverse-engineering studies are authorized. Ship only native Direct3D 12 in this version. Keep Vulkan available only for explicit development builds. Keep the Xenos investigation host separate from shipping builds.
- Use `develop` as the working branch. Keep `main` as the primary branch.
- Keep the official ReXGlue SDK as the `sdk` submodule at the repository root, pinned to a specific revision.
- Keep game images, extracted assets, and original executables out of version control.
- Report validation accurately. Repository setup or successful code generation does not establish that the game builds or runs.
