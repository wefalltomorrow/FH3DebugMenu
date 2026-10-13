# Forza Horizon 3 — Stage 5: experimental native camera activation

## Purpose and scope

This is the **first opt-in in-process camera-command test**, not the complete
original developer debug menu. It uses native C++ and existing retail
`Camera::ForzaMultiCam` functions. **No RTSS, external OSD, or PowerShell at
runtime.** The Stage 5 DLL is not yet validated on Windows or in FH3, and
cannot be presented as a working retail debug-menu port.

The C++ test targets game version `1.0.125.2` **only**, verified with expected
SHA-256 `7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14`.

## Why Stage 5 is a meaningful native step

Earlier live reports proved one `SplitView` and `Camera::ForzaMultiCam` controller
with 22 camera-mode entries. Modes 0, 6, 9 and 12 each have populated handlers.
The setter and getter are pinned to retail RVAs `0x1D63BB0` and `0x1D5DEB0`.
The main retail `CForza` per-frame method is at RVA `0x1B3D830`, vtable slot 18.
The original primary vtable has exactly 31 executable entries and a verified
RTTI Complete Object Locator.

The Windows Stage 5 DLL implements an experimental per-instance **vtable clone**:
- Verify SHA-256, CForza RTTI, vtable and function address before modification.
- Clone the 31 method pointers and preserve MSVC RTTI.
- Replace only method slot 18 on the live `CForza` instance, not the executable on disk.
- Dispatch queued camera requests immediately after the original frame update.
- Validate the live `SplitView` and `ForzaMultiCam` object, vtables, exact setter
  and getter addresses, and handler entry before calling the original setter.
- Remember the previous mode and offer restoration on the same owner.
- Log execution in `%TEMP%\FH3_Native_Camera_Stage5.log`.

No text or graphics menu is provided in this stage. Keyboard shortcuts are
**temporary activation controls**; the goal remains the exact original menu,
using developer `release.exe` and `profile.exe` as donors.

## Windows build

Requires Visual Studio 2022/2026 MSVC C++ build tools and CMake. From PowerShell 7:

```powershell
.\Build-Stage5.ps1
```

The builder configures for x64, builds, runs five test suites, and copies the
loader and DLL into `Stage5-Windows-Run`.

**Important:** The native DLL and loader have **not** been compiled or executed
under Windows in this environment; only the cross-platform C++ library and
unit tests have been built and passed. Windows compilation may expose issues
that the Linux tests cannot check.

## Test (once Windows build succeeds)

Run retail FH3 in free roam. In the `Stage5-Windows-Run` folder:

```powershell
.\FH3NativeCameraLoader.exe --check
.\FH3NativeCameraLoader.exe --activate
```

`--check` is read-only. `--activate` explicitly loads the experiment DLL into
FH3 and swaps its in-memory `CForza` instance vtable, so it **may crash** or be
blocked by Arxan/UWP/security restrictions. Do not run in an online race.

With FH3 as the foreground game:

- **Ctrl+Alt+F8** — request original FreeCam mode 6.
- **Ctrl+Alt+F9** — restore the mode captured before the first FreeCam request.
- **Ctrl+Alt+F12** — stop accepting new commands; module remains resident until
  game exit to avoid unsafe in-flight unloading.

Then send the full `%TEMP%\FH3_Native_Camera_Stage5.log` and say whether
FreeCam switched and restoration worked. If the game crashes, collect the crash
events/dump if available. The log distinguishes failure to attach, absence of
frame dispatch, and execution of the setter.

### Current verification

- Five native C++ unit test suites built and passed on Linux, including the new
  command-state tests and prior owner/mode catalog tests.
- Static PE RVA/RTTI checks against reconstructed retail code passed.
- Stage 4 live JSON showed all four target modes registered in retail.
- **No verified Windows build, injector execution, safe hook or live camera
  activation yet.**

## Source map

- `NativeRuntimeTest/CameraRequestState.hpp/.cpp`: testable queue, snapshots,
  owner-change detection, validation and restoration.
- `NativeRuntimeTest/CameraRuntimeWin.cpp`: Windows in-process test DLL.
- `NativeRuntimeTest/FH3CameraLoaderWin.cpp`: Windows loader with SHA check,
  `--check` and `--activate` modes.
- `NativeRuntimeTest/RetailBuildIdentityWin.hpp`: shared retail hash guard.
- `Tests/test_CameraRequestState.cpp`: new native unit tests.
- `NativeCameraCore`, `NativeCameraOwner`, `NativeCameraDiscovery`,
  `NativeCameraCapabilities`: full existing Stage 1–4 native source.
- `Build-Stage5.ps1`: complete Windows build and test script.
