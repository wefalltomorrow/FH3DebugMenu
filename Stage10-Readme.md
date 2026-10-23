# Stage 10: read-only camera mode observation

This experimental recorder reads the numeric camera mode from the retail
camera controller's separate storage and checks it against the selected
catalog handler/reference pair. It does not call the getter or setter, install
a hook, inject a DLL, or change game memory. The native debug menu and FreeCam
are not restored yet.

## Run

Extract the ZIP to a new folder. With supported retail FH3 v1.0.125.2 running
in offline free roam, open PowerShell in that folder:

```powershell
.\FH3CameraTrace.exe --inspect
.\FH3CameraTrace.exe --record 30
```

During recording, cycle through the normal in-game camera views, leaving each
visible for a few seconds. Send the resulting `FH3_Camera_Trace_*.txt` and
matching `.csv`, and say whether gameplay stayed smooth. No repeat of the
update-completion probe is needed for this step.

`--inspect` reports `MODE_OBSERVATION: CONSISTENT mode=<number>` when two reads
agree and the selected handler pair matches that mode's catalog entry. Other
statuses are useful evidence too; do not try the older setter tools to fix them.

## What changed

The first owner-byte trace completed 300 samples while the user cycled all
ordinary views and reported smooth gameplay. Only the low DWORDs of the
selected pointer pair at owner+0x228/+0x230 changed: ten transitions across
five pointer pairs. No view names or numeric mode IDs were established by
that trace. The raw mode element lives outside the captured owner bytes.

Stage 10 retains that raw capture and adds mode status, numeric ID, storage
address, selected/catalog pointer pairs and container metadata to each CSV
row. Rejected observations leave decoded columns blank. The TXT reports
status counts and accepted mode counts.

## Evidence and limits

The independent reader follows the supported retail getter at RVA 0x1D5DEB0:
the interface at owner+0x1A0 addresses a container at owner+0x1D0. Its fields
are a proxy, block map, map size, start position and element count. The proxy
must point back to that exact container. The last element uses
`position = start + count - 1`, block index `(position >> 2) & (mapSize - 1)`,
and DWORD offset `(position & 3) * 4`. The setter's storage helper has the
same address arithmetic; it assigns the catalog entry's pair at +0x30/+0x38
to the owner's +0x228/+0x230 pair. This is static analysis, not evidence that
calling either method is safe. No proprietary binary or reference source is
included.

The reader accepts only bounded, nonempty, power-of-two block maps, a valid
catalog index and matching nonnull handler/reference pointers. It compares
two complete sets of relevant reads and rechecks owner identity afterward.
These checks detect some changes, but cannot provide an atomic snapshot,
pin lifetime, detect every change-and-change-back race, or authorize a setter
call. A rejected sample can reflect a transition or an unsupported layout.
The raw owner capture and mode observation also occur sequentially.

Synthetic tests cover block boundaries, ring wrap, failed reads at each step,
invalid bounds, empty/sentinel modes, proxy mismatch, handler mismatch and
changes between reads. They do not establish actual gameplay behavior. The
Stage 10 decoder still needs its first in-game trace.

The earlier Stage 9 update wrapper has separately returned normally 604 times
in one driving test with smooth gameplay reported. That observation does not
establish camera-command safety, loading/teardown safety or FreeCam. Earlier
tools are included for continuity; use only `FH3CameraTrace.exe` for this step.
