# Stage 9: experimental DLL-free update-completion probe

Stage 8 now has two user-supplied in-game results: 587 entries in a paused/menu
session, and 1,777 entries during mostly driving with some pause/map use. Both
reported a single observed thread, zero identity mismatches, and confirmed
table restoration. The driving session was reported smooth. These results
establish entry interception, not original-function completion.

Stage 9 adds a separately selected observer that calls the original update,
counts normal returns, and reports overlapping invocations. It does not call
camera functions or enable FreeCam/menu rendering. Its in-game compatibility
is unverified until the user runs it; Windows synthetic tests are separate.

## Test

1. Extract this release to a new folder. Start the supported retail FH3
   v1.0.125.2 in offline free roam.
2. Run the read-only identity check:

   ```powershell
   .\FH3UpdateProbe.exe --inspect
   ```

3. If it reports `INSPECT_OK`, start the first completion test:

   ```powershell
   .\FH3UpdateProbe.exe --probe-completion 10
   ```

4. Switch back to FH3 and drive. Avoid races, loading screens, session changes,
   and shutting down FH3 during the test. Leave the console open until it
   finishes; Ctrl+C requests restoration.
5. Send the new `FH3_Update_Probe_*.txt` and say whether gameplay remained smooth.

Expected successful markers: `UNWIND_REGISTERED`, `RESTORE_OK`, and
`UPDATE_COMPLETION_OBSERVED`. The final report should have matching positive
`Entries` / `Returns`, `Active=0`, `OverlappingEntries=0`, and
`BalancedSamples=1`. Other-thread entries and identity mismatches must be zero.
The duration can be 1..30 seconds; the default is 10.

The older `--probe-update` mode is still the Stage 8 entry-only leaf probe.
Do not use it to test completion. A crash remains possible with the new mode.
If restoration or registration is indeterminate, restart FH3 before another
experiment. Do not terminate the tool while it is observing/restoring.

## Design and evidence boundaries

- The retail runtime capture bounds the update at RVA 0x1B3D830..0x1B3EF8D.
  Its prologue captures RCX and XMM1 before the first call; the normal return
  path sets AL. The reviewed body has no direct incoming stack-argument access.
  This is evidence for this exact target, not a generic hook ABI. The wrapper
  forwards the register arguments only; it must not be reused for functions
  with stack arguments. Existing hash, identity, RTTI and prologue gates remain,
  with additional argument-prologue and return-epilogue signatures.
- The wrapper reserves 40 bytes for shadow space/alignment and preserves all
  nonvolatile registers, incoming argument registers and RAX/XMM0 returns.
  It never substitutes a return address. Normal `call` / `ret` remain paired.
- A version-1 unwind record describes the stack allocation. A leaf bootstrap
  thread tail-calls `RtlAddFunctionTable` before the vptr can be replaced. The
  API is resolved through its owning system module plus RVA, with image and
  byte checks; local/remote DLL bases are not assumed equal. Registration must
  return the canonical thread exit value 1; other results fail closed.
- Entry, return, active and overlap counters use locked operations. Overlap
  includes recursion and simultaneous threads; it does not identify their cause.
  A propagated exception unwinds through the registered frame but deliberately
  leaves an unmatched entry/active count, so it cannot pass as a normal return.
- After table restoration, two matching balanced samples are sought for up to
  two seconds. These samples are observations, not proof of quiescence: stale
  dispatch pointers can still exist. All 16 KiB, including unwind metadata,
  remain allocated and registered until FH3 exits. No remote deletion/free is
  attempted after registration or installation dispatch.
- Identity checks and atomic table exchange do not pin object lifetime. Smooth
  driving, pause and map use do not establish race/loading/teardown safety,
  camera ownership, or permission to issue camera commands.

## Verification

Windows x64 CI runs the actual generated registration bootstrap and observer
against synthetic originals. Tests cover integer/XMM arguments and returns,
stack alignment and all shadow slots, identity mismatch forwarding, nested
calls, concurrent calls, exception propagation through the generated frame,
and virtual unwinding before/after the prologue and across the epilogue.
The eight previous test suites remain; completion adds a ninth Windows suite.
No local C++ compilation or automatic game execution is required.

Exit codes retain Stage 8 meanings: 0 observed/restored, 2 usage, 3 identity/API
gates, 4 preparation/installation rejected, 5 registration/restore not confirmed,
6 unusable callback observation, 7 report I/O failure. Exit 0 in completion mode
requires the new return/balance/overlap checks as well as the original checks.
