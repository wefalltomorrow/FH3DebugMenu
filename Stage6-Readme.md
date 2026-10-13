# Forza Horizon 3 — Stage 6: DLL-free external camera path

Stage 6 explores the external-memory execution pattern used by Forza-Mods-AIO
without copying that project's GPL-3.0 code. The C++ implementation is original
to FH3DebugMenu and uses Windows APIs and our own existing Stage 2–4
SplitView / Camera::ForzaMultiCam object resolver.

**This is not yet the original native developer menu.** There is no graphical
debug menu, and the FreeCam setter has **not been validated in the live game**.

GitHub Actions compiles the Windows x64 standalone FH3ExternalCamera.exe.
No compilation, dependency installation or RTSS is required on your PC.

## Supported FH3 binary

Only retail FH3 v1.0.125.2, executable SHA-256:
7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14.

The program refuses other executable hashes or changed camera vtables.
It validates the getter/setter machine-code signatures, two stable
SplitView -> ForzaMultiCam snapshots, and all four camera table handlers
(0, 6, 9, 12) before proceeding.

## Commands

Start FH3 in offline free roam, then open PowerShell in the extracted folder.

**Read-only inspection:**

    .\FH3ExternalCamera.exe --inspect

This reads the verified game process and prints camera object/controller
addresses and registered mode count. No game memory writes or game code calls.

**DLL-free execution probe:**

    .\FH3ExternalCamera.exe --probe-exec

This opt-in test allocates one page in FH3, writes six bytes of inert code
(mov eax, 0x46483336; ret), changes it to execute/read and runs it via
CreateRemoteThread. It tests whether non-DLL executable memory is permitted.
It does **not** call an engine function or install a hook. It does
temporarily modify process memory and may fail or destabilize the game.

EXEC_PROBE_OK means the thread returned our expected sentinel.
It does **not** prove camera engine calls are safe on a new thread.

**Experimental engine calls — not recommended until probe results are reviewed:**

    .\FH3ExternalCamera.exe --unsafe-freecam
    .\FH3ExternalCamera.exe --unsafe-followcam

These explicitly unsafe commands call FH3's existing ForzaMultiCam mode
setter, using mode 6 or 0 respectively, via a temporary code stub — not
LoadLibraryW. They are *experimental remote-thread calls*, not game-frame
callbacks. Their runtime behaviour is unverified. They may crash FH3,
succeed without changing the camera, or cause unstable gameplay.

The followcam command selects mode 0; it is NOT a snapshot/restore feature.
The return marker confirms only that the stub reached the end, not that
the camera changed. No automatic restoration is implemented.

Temporary code is freed when the remote thread finishes. If a thread
hangs, its code is intentionally left allocated to avoid freeing executing
memory. No FH3 files are modified on disk.

## Why this exists

Stage 5.1 confirmed that CreateRemoteThread succeeded, but LoadLibraryW
returned 0 and the DLL did not appear in FH3. The reason is not yet known:
possible Windows package/image-load restrictions, DLL initialization or
dependency issues, or runtime protections. Stage 5.2 --diagnose and --probe
outputs remain useful.

Stage 6 isolates **executing code without a DLL**. It does not disable
Windows or game security protections or prove correct game-thread dispatch.
Restoring the original developer menu requires its input, rendering,
hierarchy and command registration against retail engine objects.

## Source and tests

- NativeExternalCamera/RemoteCallStub.cpp: deterministic, independently
  unit-tested x64 code encoder (not copied from Forza-Mods-AIO).
- NativeExternalCamera/FH3ExternalCameraWin.cpp: external inspection and
  optional one-shot code execution.
- NativeCameraDiscovery, NativeCameraOwner, NativeCameraCapabilities:
  existing retail read-only resolvers.

Research build. Use only in offline play, at your own risk.
