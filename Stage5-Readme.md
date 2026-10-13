# Forza Horizon 3 — Stage 5.2: DLL-loading diagnostics

## Status

This package is **experimental and not the original developer debug menu**.
Windows x64 compilation and five unit tests have passed in GitHub Actions.
Live FH3 v1.0.125.2 reports that the original camera DLL fails to load:
`LoadLibraryW` remote thread exit 0; no camera DLL was listed afterward.

**Stage 5.2 is designed to isolate this failure. It does not yet fix native camera activation.**

The supported retail executable SHA-256 is
`7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14`.
The original camera handlers (Follow=0, Free=6, Wheel=9, TrackSeg=12)
were verified registered in an earlier read-only live report.

## Prebuilt Windows package

Download the latest experimental prerelease from
https://github.com/wefalltomorrow/FH3DebugMenu/releases

Extract the ZIP, leave the EXE and DLLs together, start FH3, and run
the following in PowerShell from the extracted folder.

### First: read-only diagnosis

```powershell
.\FH3NativeCameraLoader.exe --diagnose
```

Reports whether the FH3 process has a package identity, an AppContainer
token, and image-loading / signature / dynamic-code mitigation policies.
No game memory is modified by this mode. Query failures are reported
as such; they are not automatically evidence of anti-tamper protection.

### Second: inert loading probe

```powershell
.\FH3NativeCameraLoader.exe --probe
```

Attempts to load `FH3NativeLoadProbe.dll`, a deliberately inert DLL
with no CRT startup, imports, hooks, game callbacks, worker threads
or runtime-library dependencies. This **does load a DLL into FH3 if successful**,
so it isn't read-only, but it does not intentionally modify game state.

- `PROBE_LOADED`: the process accepted a simple DLL. Investigate
  camera-DLL dependencies, initialization and loader restrictions next.
- `INERT_PROBE_FAILED`: even the simple DLL didn't appear. Investigate
  package identity, integrity policies, path access and process protections.
  It does not by itself prove anti-tamper blocking.
- `REMOTE_THREAD_EXIT: 0`: compatible with a `LoadLibraryW` failure;
  the exit code is only 32 bits, and cannot transport `GetLastError`
  from the target process.

**Please send the full output of both commands.**

### Camera activation (only after loading is understood)

```powershell
.\FH3NativeCameraLoader.exe --check
.\FH3NativeCameraLoader.exe --activate
```

`--activate` attempts to load the camera DLL and swap a game-owned
`CForza` instance vtable. It may fail or crash FH3. It is **not** yet
validated in a live game. Test only in offline free roam.

If camera activation eventually works, experimental controls are
Ctrl+Alt+F8 (FreeCam), Ctrl+Alt+F9 (restore) and Ctrl+Alt+F12
(disable further commands). The log is
`%TEMP%\FH3_Native_Camera_Stage5.log`.

## Contents

- `FH3NativeCameraLoader.exe`: guarded loader, read-only diagnostics,
  inert probe and opt-in experimental activation.
- `FH3NativeLoadProbe.dll`: minimal inert DLL.
- `FH3NativeCameraTest.dll`: experimental native camera test.
- `NativeRuntimeTest/`: loader, probe, process diagnostics and camera code.
- `NativeCameraCore/`, `NativeCameraDiscovery/`,
  `NativeCameraOwner/`, `NativeCameraCapabilities/`, `Tests/`:
  complete native source and five unit tests.

Everything is built by Windows GitHub Actions. **No local compilation required.**
There is no RTSS dependency.
