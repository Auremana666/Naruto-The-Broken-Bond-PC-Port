# Recompilation notes

## Input and SDK

- Input: locally extracted `game/default.xex` (not tracked).
- SHA-256: `8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576`.
- Title ID: `55530825`; media ID: `09FFE047`; executable version: `0.0.0.3`.
- SDK revision: `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.
- Validation tool: source-built ReXGlue `0.10.0.2-dev.gc94f5eb`.

## Function boundaries

The initial generation emitted nine unresolved conditional-branch diagnostics.
`config/codegen.toml` supplies two exclusive function ends without replacing
instructions or bypassing their conditions:

| Entry | Exclusive end | Evidence and correction |
| --- | --- | --- |
| `0x82225A40` | `0x82225A78` | Wrappers at `0x82224388` and `0x82224390` branch to the same character-classification tail. Its checks at `0x82225A48` and `0x82225A50` branch to the zero-result block at `0x82225A70`. Declaring the shared function preserves these branches. |
| `0x827545F0` | `0x827546A4` | The search loop branches from `0x82754610` to `0x8275463C`. That success block has five checks returning to the fallback vector stores at `0x82754624`. The last return is at `0x827546A0`, followed by zero padding. Declaring the complete function prevents the success block from becoming an isolated function. |

The second boundary was checked directly against decrypted XEX instruction
words using .NET AES and the basic-compression layout implemented in
`sdk/src/system/xex_module.cpp`. These are local binary observations, not REA
provider evidence. No original executable or decrypted payload is committed.

## Code-generation validation

An isolated run used `build/codegen-check.toml`, output under
`build/codegen-check/`, and the shared configuration above. The command was:

```powershell
& build/sdk-install/bin/rexglue.exe codegen build/codegen-check.toml
```

The run exited successfully and wrote 389 files. Its log is
`build/logs/codegen-check.log`. No unresolved-branch diagnostics remained, and
the generated C++ contained no `REX_FATAL` calls. Inspection confirmed that the
wrappers tail-call `sub_82225A40`, both character checks reach the zero-result
label, and all five success-block checks reach the fallback inside
`sub_827545F0`.

The existing warning about function `0x82C49D08` exceeding the source-file size
target remains: its generated body is 1,358,335 bytes against a 1,048,576-byte
target. This validation establishes code generation and the corrected branch
structure only; it does not establish a successful native build or game startup.

## Runtime callback discovery

The first runtime attempt loaded the XEX and initialized the SDK graphics and
audio systems, then stopped at unregistered callback `0x821FE040`. The evidence
is `build/logs/runtime-20261016-000323.log`; this is not a successful game boot.

Inspection found two pointer tables without MSVC RTTI locators. The table at
`0x82001808..0x82001848` has 16 slots and was missing `0x821FE040` and
`0x821FE0A8`. The adjacent table at `0x8200186C..0x8200191C` has 44 slots and
was missing `0x822086E0`. The first two are register-forwarding virtual-call
thunks ending at `0x821FE05C` and `0x821FE0BC`; the third is a null-guarded tail
call ending at `0x822086F8`. Temporary explicit entries initially preserved those bodies.
Isolated generation with those entries succeeded, registered all three, and
reported no unresolved branches (`build/logs/codegen-check-dispatch.log`).

A later attempt reached input, GPU interrupt callback, and audio registration
before stopping at `0x821C9DE8`
(`build/logs/runtime-20261016-000615.log`). Its table spans
`0x820E7190..0x820E7254` and contains 49 slots. The three missing entries are
an object-adjustment thunk at `0x821C9DE0..0x821C9DE8` and descriptor-writing
leaf functions at `0x821C9DE8..0x821C9E00` and
`0x821D0D18..0x821D0D30`. Their table slots are `0x820E71B4`, `0x820E71D8`,
and `0x820E7248`. These six temporary entries have now been removed from the
config after successful automatic recovery.

At the pinned SDK revision, `VTableScanner::scan` follows MSVC Complete Object
Locators only. The first tables above lack these locators. Gap filling splits
regions at returns and branches to known function entries but does not split
at indirect tail calls. This leaves adjacent thunks in a segment whose first
entry is already registered, causing the segment to be skipped. Improving
automatic discovery must retain checks against executable bounds, invalid or
misaligned addresses, and labels inside existing function blocks.

## Automatic discovery patch

The complete implementation walkthrough and reproduction commands are in
[Automatic function discovery](automatic-function-discovery.md).

`patches/rexglue-discovery.patch` adds a second discovery pass over contiguous
code-pointer runs in non-executable `.rdata`. It requires at least three slots
and two distinct functions already discovered before the pass. Candidate entries
must be aligned executable addresses at a decoded region start or immediately
after an unconditional non-call branch. Existing block coverage and full declared
PDATA/config ranges prevent internal labels from becoming new functions.

The declared-range check is essential: SEH scope records also contain consecutive
code addresses. An intermediate implementation misidentified inline exception
handlers as functions. The final patch rejects those labels while preserving
legitimate callbacks. No game instructions are replaced by this pass.

Synthetic regression tests are included in the SDK patch at
`tests/unit/codegen/function_pointer_table_test.cpp`, with tag `[pointer_table]`.
The original detector fails the positive recovery case; an intermediate detector
fails the declared-extent cases. The corrected detector passes 48 assertions in
four cases. Local results are retained in `build/discovery-*-results.log`.

An isolated generation without any of the six temporary callback overrides
completed successfully (`build/logs/codegen-auto-validated.log`). All ten
investigated callbacks are declared automatically: `821FE040`, `821FE0A8`,
`822086E0`, `821C9DE0`, `821C9DE8`, `821D0D18`, `821CD298`, `821CD2E0`,
`821C6418`, and `821CAD00`. The generated C++ has no `REX_FATAL` calls or
unresolved-branch diagnostics. Relative to the earlier build with six manual
entries, the corrected output adds 53 function declarations and removes seven
incorrect standalone registrations of internal labels; their parent functions
retain the original control flow.

On 2026-10-17 the game crashed at the end of the first fight. `qsort` at
`0x8221B460` called the comparator `0x8215D000`, which was never registered:
its address is built with `lis` plus `addi` in `0x8215D588` and nothing calls
it with `bl`. The patch now also registers functions whose address is built in
discovered code (`discoverCodeAddressReferences`, test tag `[code_address]`).
The generation adds 19 functions, including that comparator and another one at
`0x825085F8`. Two former standalone blocks become labels of their real
functions, and all other generated function bodies are unchanged. The tests
pass 68 assertions in six cases (`build/discovery-tests`).

Missing-function runtime diagnostics now include guest LR and SP. SDK executable
and runtime DLL hashes are checked by `scripts/build-game.ps1` to invalidate
codegen after local SDK patches, whose version string may remain unchanged.

## Native build and startup validation

The final native build completed successfully with the locally patched SDK
(`build/logs/build-game-auto.log`). The configuration retains only the two
original function-boundary overrides; callback discovery is automatic.

On 2026-10-16, `scripts/run-game.ps1 -Keyboard -LogLevel debug` progressed
through the opening logos to the actual main menu, including the rendered
background and Story Mode, Fight Mode, Xbox Live, Options, Marketplace, and
Exclusive Content entries. The window was responsive. Runtime evidence is
`build/logs/runtime-20261016-002058.log`; the captured main menu is
`build/logs/main-menu.png`. The runtime detected the local AMD Radeon RX 9070 XT
and DualSense controller. No missing-function fatal occurred during this run
through the main menu.

This establishes startup and menu rendering, not gameplay, save/load, online
features, or full compatibility. The session subsequently closed through the
window shutdown path with exit code zero.
