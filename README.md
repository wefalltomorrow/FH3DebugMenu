# FH3DebugMenu

Research project to restore the **original developer debug menu** in retail Forza Horizon 3 v1.0.125.2.

**Status:** The original developer menu is **not yet restored**. The repo has native C++ foundations and diagnostic tools. No RTSS dependency. The Windows x64 releases are compiled using GitHub Actions; users do not need to compile locally.

## Verified in the retail game

- Exact supported game executable SHA-256: `7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14`
- Live `SplitView` and `Camera::ForzaMultiCam` object resolution
- Four developer-referenced camera handlers registered: Follow=0, Free=6, Wheel=9, TrackSegment=12
- Stage 6 inert six-byte native code probe executed in the game and returned its expected marker
- Stage 5 DLL loading failed in the target process. In-game camera setter calls remain **untested**.

## Current work: Stage 10 read-only mode observation

The first owner trace recorded 300 samples, ten handler-pair transitions and
five distinct pairs while the user cycled all normal views and reported smooth
gameplay. Stage 10 extends `FH3CameraTrace.exe` to read the separate numeric
mode storage, compare two observations and check catalog-handler agreement.
It makes no game calls or memory changes. Its decoder is awaiting its first
in-game result. Read [Stage10-Readme.md](Stage10-Readme.md).

The Stage 9 update-completion probe separately recorded 604 entries and 604
normal returns in a driving test, with successful restoration and smooth
gameplay reported. Camera setter safety is still unverified. Read
[Stage9-Readme.md](Stage9-Readme.md) for the wrapper's limits.

## Stage 7 read-only camera tracing

`FH3CameraTrace.exe` samples the **known game camera-owner object** while users cycle through FH3's ordinary camera views. It produces a CSV trace and a TXT summary of infrequently changing 32-bit fields.

This is a **read-only diagnostic**, not a working camera changer or graphical debug menu.

Instructions: [Stage7-Readme.md](Stage7-Readme.md)

## Earlier experimental tools

- [Stage 6](Stage6-Readme.md): external camera resolver and inert DLL-free execution probe. Experimental unsafe camera-setter commands are not recommended until game-thread behaviour is verified.
- [Stage 5.2](Stage5-Readme.md): native DLL loader, Windows process-policy diagnostics and inert DLL loading probe. DLL activation is not verified.

[Releases](https://github.com/wefalltomorrow/FH3DebugMenu/releases) · [Windows build history](https://github.com/wefalltomorrow/FH3DebugMenu/actions)

## Roadmap

Recover native camera-state mappings, establish game-thread-safe camera command delivery, then restore the original developer UI's render path, hierarchy, input and command registrations. The target is the real developer-style in-game menu, not a global OSD or an RTSS approximation.

This is an unofficial research mod. Test only in offline free roam. Preserve the original game files.
