# Stage 8: experimental DLL-free update-entry probe

This stage tests whether retail FH3 dispatches the previously mapped CForza
virtual update slot during gameplay. It closes the gap between Stage 6's inert
remote-thread execution and eventual engine-context camera commands. It does
not activate FreeCam, call a camera function, or draw the developer menu.

Download the Windows x64 ZIP from this stage's GitHub prerelease and extract
it. No local compilation, DLL loading, or RTSS is required.

## Test

1. Start the supported retail v1.0.125.2 in offline free roam.
2. Run the read-only check from the extracted folder:

   ```powershell
   .\FH3UpdateProbe.exe --inspect
   ```

3. If it reports `INSPECT_OK`, run the experimental ten-second observation:

   ```powershell
   .\FH3UpdateProbe.exe --probe-update 10
   ```

4. Switch back to the game and remain in free roam. Do not enter a race, load
   a different session, or shut down the game during the observation.
5. Wait for the console result and send `FH3_Update_Probe_*.txt` plus the
   console output. Report whether gameplay stayed smooth, froze, or crashed.

`--inspect` only reads memory. `--probe-update` temporarily changes one object's
vtable pointer and executes newly allocated code. This is the first in-game
test of that interception path, so a crash remains possible. Ctrl+C requests
early restoration; leave the console open until it finishes. Closing/killing
the tool is not a supported cleanup path.

## Results

- `UPDATE_ENTRY_OBSERVED` plus `RESTORE_OK`: at least one entry was observed,
  the sampled entries used one thread without identity mismatches, and the
  original object table was observed after the restore operation. This does
  **not** prove completion of the original update, that the thread owns the
  camera, or that a before/after-update camera command is safe.
- Zero entries: the interception was not observed in this window. Do not
  infer that the mapped method is unused; dispatch could bypass this slot.
- Other-thread entries or identity mismatches: rejected as a usable result.
  Counters are sampled concurrently; they are observations, not an exact
  frame count. Final counters can include in-flight entries during restoration.
- `RESTORE_NOT_CONFIRMED` or an indeterminate remote thread: restart FH3
  before any further experiments. The tool never overwrites a competing table,
  terminates an indeterminate remote thread, or frees possibly referenced code.

Exit codes: 0 observed/restored; 2 usage; 3 identity/access/policy; 4 preparation
or rejected installation; 5 indeterminate install/restore; 6 no usable callback
observation; 7 report I/O failure. Logs are timestamped in the current folder.

## Implementation and limits

The probe requires the exact executable SHA-256, module size, CForza global,
RTTI locator, original primary table, 31 executable table entries, update slot
18/RVA 0x1B3D830, and the archived runtime prologue signature. It takes stable
snapshots before preparing and again immediately before installation. It
rejects an existing replacement table and prohibited dynamic-code policy.

A clone preserves the RTTI pointer and all 31 slots except update. The new
entry is a leaf routine that records counters and a Windows thread ID, then
tail-jumps to the original update with its incoming arguments and stack intact.
It has no calls, stack adjustments, camera accesses, or render work. Only the
volatile RAX/R10/R11 and flags are used. The installation thread only checks
ownership and atomically compares/exchanges the object's table pointer.

Global checks and atomic compare/exchange do **not** pin the object's lifetime.
Destruction between a check and dereference, pointer reuse, engine transitions,
and third-party hooks remain unverified risks. No claim of safe game-thread
command dispatch is made by this release. Code is RX, clone table is read-only,
and counters/installer arguments are separate writable pages.

After any installation-thread dispatch, the 16 KiB allocation is deliberately
retained until FH3 exits, including after successful restoration. A thread can
have fetched an old table/function pointer without having entered the probe;
an idle counter or restored vptr cannot prove that freeing it is safe. The
tool never changes executable game files or patches original function bytes.

## Verification

Windows CI executes the actual generated leaf and swap routines against
synthetic objects. Tests cover integer/float-register/stack argument forwarding,
return values, concurrent counters, changed object/global/table observations,
successful install/restore, and refusal to overwrite a conflicting table.
These tests do not execute FH3 or establish in-game compatibility.

The existing seven suites are retained. Stage 6/7 assertion-based tests now
keep assertions enabled in Release builds. Stage 7 reports CSV/TXT write and
close failures instead of announcing a completed capture after an I/O failure.

## Next evidence

If this probe succeeds, establish update phase, camera ownership/lifetime,
and a guarded request/restore protocol before adding a camera setter call.
Stage 7's normal-camera trace remains useful and is included in this package.
The original menu's render integration, resources, input and dynamic command
hierarchy remain separate unfinished work.
