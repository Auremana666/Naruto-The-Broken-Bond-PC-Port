# Launcher game options

This investigation supports the launcher port from Rise of a Ninja. It is
specific to Broken Bond `game/default.xex`, SHA-256
`8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576`.
Static evidence and a host harness do not establish a successful game boot.

| Option | Implementation | Validation boundary |
| --- | --- | --- |
| `skip_intro_videos` | Optional hook for read-only opens of `Ubisoft.bik` | File-wrapper and Bink failure paths verified statically; host harness passed; paired Xenos boots reached the title with skip off/on |
| `ultrawide_target_aspect` | Unavailable; keep the launcher row disabled | Broken Bond projection constant and presentation contract remain unresolved |
| FPS overlay | Reuse the Rise host overlay with the SDK counter API | SDK source supplies guest swap timing when performance counters are enabled; otherwise label the fallback as Host |
| Mouse camera | SDK mouse-to-right-stick emulation | Rise's separate direct exploration-camera hook has not been ported or validated for Broken Bond |

## Intro video hook

`src/intro_skip.cpp` defines `skip_intro_videos`, defaulting to false. Its
`sub_8222D838` hook passes every normal request to the original generated
`__imp__sub_8222D838` function. When enabled, it intercepts read-only opens of
the exact basename `Ubisoft.bik`, ignoring ASCII case. It accepts slash and
backslash paths, rejects unterminated/truncated paths, and leaves write opens
and every other filename on the original path. The extracted local game has
`Ubisoft.bik`; the additional Rise `logo_corpo_*.bik` names were not copied.
Story `intro_*.bik`, `AttractMode_*.bik`, and other cutscenes remain eligible
for normal playback.

The special failure sequence matches this Broken Bond wrapper: pass NTSTATUS
`0xC0000035` to `sub_821F7F48`, pass DOS error 80 to `sub_821F7F28`, and return
the original sign-extended invalid handle, -1. These helpers update the
thread-local last error; they are not engine logging functions. No Rise guest
addresses are used.

`src/intro_skip.cpp` is included in `BROKENBOND_SOURCES` for both builds.
The generated weak function aliases already expose the original `__imp__`
entries; no SDK edit or app lifecycle callback is needed.

## Static evidence ledger

Analysis ran through
`build/renderer-study/rea/run.mjs` using the existing isolated REA/IDA profile,
with `tools/native-renderer/collect-storage-followup.mjs` retaining complete
responses in `build/launcher-study/game-features-20261020/`. The session was
exported to `evidence.json` and closed successfully (`run.json` has
`failure: null`). The output directory contains replay requests and the
collector. Game files and evidence containing original code stay under ignored
local paths.

| Observation | Evidence ID | Retained response |
| --- | --- | --- |
| `sub_8222D838` receives the path in r3, calls `RtlInitAnsiString`, translates the selected status to error 80, and returns -1 | `ev_27536c04e67ebe82717e86ce2e32e2f30bf589e2a55f64b647b4e9a15f081612` | `006-analyze_function.json` |
| `sub_82C14FD0` tries two read-only opens through that wrapper, then returns zero when both handles are -1 | `ev_f21f91a303c252ea4d4bf1f878edd4c111a5a4422b09461e2e010a4a97dab383` | `007-analyze_function.json` |
| `sub_821F7F28` forwards to the TLS last-error setter | `ev_5ccf048e4acbb88aad5ba3f6c3ad61b545e28028f02785ac5c6313bc19846cb2` | `008-analyze_function.json` |
| `sub_821F7F48` calls `RtlNtStatusToDosError` and updates TLS | `ev_138eec9b4bfd1aa28d557d78554df20e6805233db73bbf731297b828ababba07` | `009-analyze_function.json` |
| `sub_821F7F30` writes the supplied error value into TLS when enabled | `ev_e21fd693ed5065d30165fbf5a34d1d0027ea6d07640fdcea87fdb4e0f60ca508` | `012-analyze_function.json` |
| The Bink open function `sub_82C0B8F8` selects `sub_82C14FD0`; a zero result sets an opening-error string and returns zero | `ev_a5a505e3c3335a7385108e61222c748671153954bc720e3ae3982fc94d56141e` | `014-analyze_function.json` |
| Localized `intro_*_bik` names are normalized to `Intro_bik` by `sub_8277D030`; the hook does not select those names | `ev_1f8a62bd3dae139d9cdabe72748cee70aece2f93d6dfa5d534a0bed00edde372` | `015-analyze_function.json` |

The Bink decompiler reports failed local-variable allocation. The relevant
callback selection and zero-result branch are also present in its retained
assembly. The adapter does not provide complete function-body ranges, typed
references, basic blocks, or complete indirect-call coverage. Static Bink
failure handling establishes the immediate return path; it does not establish
the boot sequence's reaction, timing, or visual result.

## Ultrawide gap

Rise patches the float at `0x826F5108` to the reciprocal target aspect and
modifies its SDK command processors to pass that aspect to the presenter.
That address has no established Broken Bond meaning. The pinned Broken Bond
SDK has no `ultrawide_target_aspect` presenter override. Neither portion was
copied into the working game.

The bounded Broken Bond search covered provider-defined strings matching
`projection`, their `ir_projection` reference, and the corresponding candidate
address. Evidence
`ev_702e8f3b88a078d19ea2fc73d2a08d2a283dbafc889328e670551ab8b92b2f2b`
(`003-search_strings.json`) contains the string matches. The `ir_projection`
reference is `0x825D398C`, evidence
`ev_3b49d39b3f6c527158b7309418f620e42247d6918522cf1c80b29d081edf0545`
(`011-xrefs.json`); IDA did not recognize a containing function for that address
(`016-analyze_function.json`). This search did not identify a projection
constant or establish its consumers. It is not evidence that Broken Bond
cannot support ultrawide.

Enable the launcher option only after identifying the projection calculation
in this executable, proving the original constant and its uses, implementing
the corresponding host presentation behavior, and comparing live 16:9 and
wide gameplay, menus, HUD, and video. Stretching a 16:9 image alone does not
validate this option. Xenos remains the working default.

## Validation

The real `src/intro_skip.cpp` passed Clang C++23 syntax checking against the
installed SDK. An isolated host harness including that source passed ten hook
routes and three bounded-path checks, using stubs only for the original guest
entry and the two error setters. It verifies opt-in behavior, case/slash
matching, exact basenames, story and attract exclusions, write exclusion,
error-call order, returned handle, and bounded path handling. The harness is
retained as `intro_skip_harness.cpp` and `intro_skip_harness.exe` in the study
directory. It does not execute the game or emulate the TLS helper bodies.

On 2026-10-20, paired 60-second Xenos boots launched through the real Play
button reached the title screen with skip off/on. The enabled run logged
`boot logo video skipped: d:\ubisoft.bik`; the disabled run did not log a
skip and showed a boot logo at 12 seconds. Both closed with exit code zero
and no fatal log. Logs, screenshots, private configurations and process IDs
are retained in `build/launcher-study/runtime-20261020-003148/`. This validates
the boot path. Localized story introductions and attract videos have not
been replayed for this change; their exclusion is covered by the host harness.

For the FPS overlay, `sdk/src/graphics/command_processor.cpp` records
`kFps`/`kFrameTimeUs` at guest swap packets under
`REXGLUE_ENABLE_PERF_COUNTERS`. `rex::perf::GetSnapshotCounter` and
`ReXApp::SetGuestFrameStats` are available in the pinned SDK. The Rise overlay
already distinguishes guest timing from ImGui host repaint timing and can be
reused without a title-specific hook.

Rise additionally defines `mouse_camera_direct` and implements its direct
exploration-camera hook inside script modules (`mouse_camera_script.cpp` in
its AI2C DLLs). The current port uses the SDK's generic right-stick emulation.
Equivalent direct angle-per-pixel camera behavior in Broken Bond has not been
investigated or established by this bounded study. Launcher mouse controls
must not imply that the Rise script hook has been validated for this title.
