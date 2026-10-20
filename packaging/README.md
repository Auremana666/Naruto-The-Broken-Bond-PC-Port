# Broken Bond packaging and installer

The Windows installer reuses Rise of a Ninja's Inno Setup implementation from
the sibling checkout at `b278a0deedcf1421f80876c32ce0ef76a89f1ab8`.
The setup script, translations, ISO process wrapper and regression tests are
adapted under this repository's GPL-3.0-only license. The old ImGui installer,
Rise DLC engines, bundled translations and historical version migrations are
not part of this port.

## Build

Use PowerShell 7, the existing Windows C++/LLVM build tools, and Inno Setup 6.4
or later. First configure/build the native port using
`scripts/build-game-native.ps1 -Backend d3d12` (see the [build guide](../docs/build-and-run.md#6-native-renderer)). Then:

```powershell
./packaging/package-release.ps1
# Set the release version without editing the source:
./packaging/package-release.ps1 -Version 0.1.1
```

The version is `major.minor.patch` with an optional pre-release suffix, such as
`0.1.0-beta`; updates compare the numeric part only. The game and the launcher
show it in their window title and under the cover, read from the packaged
`broken_bond.version`.

Packaging refreshes the game and launcher targets in `build/native`, builds the
standalone setup helper, verifies the pinned extract-xiso archive, and checks
PE imports for debug CRT dependencies. This version requires a Release build with native Direct3D 12 enabled and
Vulkan disabled. Packaging rejects a Vulkan-enabled build. DXIL shaders are
embedded in the game executable; SPIR-V and Vulkan dependency notices are not
packaged. The SDK submodule revision is unchanged.

Output in ignored `dist/`:

- `BrokenBondInstaller.exe`
- `broken-bond-recomp-win-amd64/`
- `broken-bond-recomp-win-amd64.zip`
- `SHA256SUMS.txt`

`-OutputRootDir` selects another artifact directory, `-NoZip` omits the ZIP,
`-NoSetup` builds only the portable package, and `-IsccPath` selects an Inno
compiler. Packages are assembled in a unique staging directory. Existing
artifacts are moved to `previous-release-*` only after assembly succeeds;
failed staging is retained for diagnosis. No artifacts are published remotely.

Only explicitly selected port binaries, launcher assets, fresh configuration,
version metadata and notices are packaged. Local game data, shader caches,
saves, logs, personal settings and original executables are excluded.
`third-party-licenses.psd1` inventories the included components; their notices
and source revisions are written to `licenses/`. The game and launcher need
the Microsoft Visual C++ x64 runtime. The helper links its C++ runtime statically.

## Installation and updates

Setup defaults to `C:\Games\Naruto The Broken Bond`, with per-user registration
and no elevation. Windows does not allow a colon in a directory name; the
displayed product name remains `Naruto: The Broken Bond`. Choose a writable
destination if the Games directory is restricted on the current machine.
It offers a new installation with optional ISO import, or an update to an
existing installation. Updates require both `broken_bond.exe` and
`broken_bond_launcher.exe` and skip ISO import. Portable copies can be selected
as update destinations. Numeric downgrades are rejected; reinstalling the same
version is allowed. Keep the production AppId stable across releases.

Setup and the launcher offer English, Brazilian Portuguese, French, German,
Spanish, Italian and Russian. Setup starts in English. New configurations
inherit its interface language; the game's language stays independent.
Setup preserves existing `broken_bond.toml` byte for byte. On the next launcher
load, a legacy Xenos selection migrates to native; other user preferences remain.
Updates remove the retired `rexgpu-xenos.dll`. New packages do not contain it.
Direct3D 12 is the only launcher choice in this version. Saved Vulkan choices
are migrated to Direct3D 12 on launcher load; adapter selections from Vulkan
are cleared while other preferences are preserved.

The setup helper verifies Title ID `55530825` and the supported `default.xex`
SHA-256 before extracting anything. Hash verification is shared with the
launcher. A matching Title ID alone is insufficient. Full-disc and trimmed
XISO layouts are supported. The helper and extract-xiso run from Setup's private
temporary directory and are never installed beside the launcher or in the ZIP.

Import progress and cancellation follow Rise's workflow. Extraction goes to a
private temporary folder, then a unique staging folder on the destination
volume. Only a complete import becomes `game/`, by directory rename. Populated
folders and reparse points are refused; empty folders can receive an import.
Both volumes are conservatively checked for two copies of the data, the port,
and 256 MB of additional space. Normal failure/cancellation cleans staging.
An interrupted process or power loss may leave `.broken_bond-import-*` folders
for manual inspection.

Uninstall removes installed port files and shortcuts, preserving game data,
configuration, saves, caches and unrelated files. Saves/cache normally live in
`user/` beside the installed launcher; a configured `user_data_root` still wins.

```powershell
./dist/BrokenBondInstaller.exe /MODE=update /DIR="D:\Games\BrokenBond"
./dist/BrokenBondInstaller.exe /MODE=install /GAMEISO="D:\Original.iso"
```

## Validation

```powershell
./scripts/build-setup-helper.ps1
python packaging/tests/test_setup_helper.py -v
python packaging/tests/test_setup_inno.py -v

# Optional local supported XEX enables successful import/cancellation fixtures:
$env:BROKENBOND_TEST_XEX = (Resolve-Path game/default.xex).Path
python packaging/tests/test_setup_helper.py -v
python packaging/tests/test_setup_inno.py -v

# Exercise the assembled port payload instead of the default synthetic binaries:
$env:BROKENBOND_TEST_PACKAGE = (Resolve-Path dist/broken-bond-recomp-win-amd64).Path
python packaging/tests/test_setup_inno.py -v

# Optional full-disc import into a temporary installation (several GB of space):
$env:BROKENBOND_TEST_ISO = "D:\Original.iso"
python packaging/tests/test_setup_inno.py InnoSetupTests.test_full_local_disc_import -v
```

Integration tests use unique AppIds, temporary destinations, no shortcuts and
no launcher execution. Without `BROKENBOND_TEST_XEX`, tests needing a supported
revision are skipped; rejection and preservation tests use synthetic data.
With it, synthetic ISOs contain that local executable plus a small fixture;
they are not full game dumps. `BROKENBOND_TEST_ISO` additionally enables a full
local-disc import and checks that uninstall preserves all imported files.
Nothing is added to version control.
Override helper/compiler locations with `BROKENBOND_TEST_BUILD` and
`BROKENBOND_TEST_ISCC`. Python 3.11+ is required.

See [validation results](../docs/installer-validation.md). A successful package
build or setup test does not establish that the game runs.
