# Launcher validation

Local Windows x64 validation for the launcher port on 2026-10-20. These checks
cover the launcher and bounded title boots, not the full game or story.

## Source and visual comparison

The reference is `C:/Users/Jorge/dev/naruto-rise-dev/launcher`, commit
`4e8766dd562745233b4a05d99f8e62b2475ef589`. It was built in a private directory
using this project's installed SDK, without modifying the source checkout.
Both launchers were captured at the same 1060x720 client size, with the same
font files and temporary cover artwork to compare the content geometry.

`build/launcher-study/comparison-20261020-003043/` retains both sets of captures,
the comparison script, configurations and report. The controls and mapping
screens place the groups, rows, sliders, slots, description panel, and actions
at the same coordinates. Raster comparison is not completely pixel-identical:
the control-description text has a small glyph difference, and the mapping
body differs in a one-pixel-wide portion of its hint text. The intentionally
different header redistributes the removed DLC tab's space among three tabs.
Game identification, version text, and the unsupported-aspect explanation
also differ. These are real executable captures, not mockups.

The earlier `ui-20261020-002846/` captures show GPU selection, controls and key
mapping from a portable path containing spaces and `ç`. This exposed a font
fallback through a narrow Windows filename; fonts now load through Unicode
filesystem paths. The cover loader also accepts Unicode paths. Original
Broken Bond cover and icon bytes were preserved.

## Builds and regression tests

The SDK was rebuilt and installed with `rexglue-mnk-modifiers.patch`, retaining
the pinned revision and the existing local patches. Both game variants and
both matching launchers built successfully.

| Check | Result | Local record |
| --- | --- | --- |
| Xenos game and launcher build | Passed | `build/launcher-game-build.log`, `build/launcher-final-games.log` |
| Native-capable game and launcher build | Passed | `build/launcher-native-game-build.log`, `build/launcher-final-games.log` |
| Xenos launcher tests | 4/4 passed | `build/launcher-tests.log` |
| Native-capable launcher tests | 4/4 passed | `build/launcher-native-tests.log` |
| Actual supported `default.xex` hash validation | Passed | Configuration/path test with `game/default.xex` as the second argument |
| Intro hook host harness | 13 checks passed | `build/launcher-study/game-features-20261020/` |

The four CTest cases cover configuration round trips, unknown and malformed
settings preservation, native settings gating, shared save paths, custom paths,
Windows argument quoting, key names and defaults, seven localization tables
and printf placeholders, and the actual SDK keyboard driver. The driver test
failed on the original runtime, then passed after the modifier patch: Shift
and Control activate both triggers while W still moves the left stick;
explicit Shift+Up combinations remain distinct, and focus loss clears input.

## Key mapping interaction

`build/launcher-study/key-capture-20261020-003737/` retains the UI interaction
script, screenshots and report. In the real SDL window, assigning W to A's
second slot removed it from the left stick and persisted `Space,W`. A captured
F1 was rejected without replacing B's existing mapping. Right-clicking an idle
first slot cleared it while preserving the second slot as `,W`. Restore default
keys brought A and the left stick back to Space and W. Settings were verified
from the saved TOML after each normal window close.

## Launching the game

All runs below used the real launcher Play button, original files from `game/`,
an explicitly selected AMD Radeon RX 9070 XT, and private configuration and save
directories under ignored `build/launcher-study/`. User saves were not changed.

| Mode | Logo skip | Observation after 60 seconds | Exit |
| --- | --- | --- | --- |
| Xenos | Off | Boot logo visible at 12 seconds; title screen reached | 0 |
| Xenos | On | Exact `d:\ubisoft.bik` skips logged; title screen reached | 0 |
| Native, 30 fps | On | Native selection and adapter logged; title screen reached | 0 |

The launchers exited normally after spawning their game process. All three
games stayed alive for 60 seconds and closed through their own window with
exit code zero; no forced termination or fatal log was recorded. Xenos logs,
process IDs, configurations and screenshots are in
`build/launcher-study/runtime-20261020-003148/`; the Native case is in
`build/launcher-study/runtime-native-20261020-003446/`.

The Xenos captures also verify that the FPS overlay appears. This Release SDK
does not publish guest performance counters: the overlay correctly labels its
fallback as **Host**, approximately 270 window repaints per second in these
boots. That number is not the game's frame rate. Native's FPS overlay row
remains unavailable, as in the source launcher; its existing window title
reports the native frame rate separately.

## Remaining boundaries

- Ultrawide stays disabled until Broken Bond camera and presentation contracts
  are validated. See the [REA/IDA ledger](launcher-game-features.md).
- Mouse camera uses the SDK's right-stick emulation. Rise's direct exploration
  script hook is not implemented for Broken Bond.
- Localized story and attract videos were excluded by the hook harness, but
  were not replayed during these boots.
- These checks do not establish full-game stability, controller hardware
  coverage, non-Windows behavior, or high-DPI behavior on other displays.
