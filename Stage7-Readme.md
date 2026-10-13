# Forza Horizon 3 — Stage 7: read-only camera state tracing

This is the next diagnostic step toward the original developer debug-menu port.
Stage 6 already confirmed that camera controller inspection and an inert
executable-memory probe work against retail FH3.

**Stage 7 does NOT inject DLLs, install hooks, execute game code,
allocate executable memory, call camera setters, or change FH3 memory.**

The tool monitors only the verified ForzaMultiCam object while you change
between ordinary, built-in camera views in free roam.

## Requirements

Retail FH3 v1.0.125.2 with supported executable SHA-256
7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14.
Game running in free roam. Extract the prebuilt Windows x64 ZIP.
No compilation or RTSS needed.

## Test

Open PowerShell inside the extracted folder:

    .\FH3CameraTrace.exe --inspect

This should confirm a SplitView and a ForzaMultiCam controller with 22
registered camera-mode slots.

Next run:

    .\FH3CameraTrace.exe --record 30

Immediately switch focus to the running game and cycle through the normal
in-game camera views, spending a few seconds on each. Revisit an earlier
camera view at least once before recording finishes.

The tool saves two files in the PowerShell working directory:

- FH3_Camera_Trace_YYYYMMDD_HHMMSS.csv : full 10 Hz snapshots of the
  known camera owner at byte offsets +0x000 through +0x23f.
- FH3_Camera_Trace_YYYYMMDD_HHMMSS.txt : summary of 4-byte-aligned
  values that changed 1–30 times during recording.

**Upload both files.** Also mention the normal camera views used.
This should let us correlate normal game state changes without
invoking game engine methods on the wrong thread.

## Validation and limitations

The tool validates the executable hash, Forza/CForza identity, SplitView
getter, ForzaMultiCam vtables, mode setter/getter function addresses,
four registered developer-camera handlers, and stable camera ownership.

It opens FH3 with PROCESS_QUERY_INFORMATION | PROCESS_VM_READ.
It reads only the validated owner object and associated pointer chain.
There are NO writes to the game process, injection or function calls.

Sparse-changing fields are only candidates, NOT proven camera mode IDs.
Normal chase/cockpit/hood camera changes are not necessarily the same
as the old developer ForceFreeCam or ForceWheelCam modes.

The original developer debug menu is still not implemented.
This recorder is a diagnostic milestone, not a working menu.

The raw trace contains game camera-object bytes and pointer values.
It does not intentionally capture personal files or other processes.

Full source and tests are under NativeCameraTrace, built with GitHub Actions.
