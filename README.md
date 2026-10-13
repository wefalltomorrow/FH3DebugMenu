# FH3DebugMenu

Native Forza Horizon 3 developer debug-menu restoration research.

## Current status

**Experimental Stage 5 only:** the native camera-command DLL and loader are not yet verified in the retail game. The native test targets only retail FH3 v1.0.125.2 with SHA-256 `7FC4821640BAF5E6F6199D0BEA19861F70550AA4A7E58C0634D7D96CDEA92C14`.

The goals are an in-game menu matching the developer build and functionality without RTSS. Stage 5 does **not** yet implement the graphical menu, and the camera command has **not** been proven in a live game.

A Windows x64 GitHub Actions workflow will build the DLL, loader and tests, then upload binaries. **No compilation on the user's PC is required.**

Use the `--check` read-only verification before considering `--activate`. Loading the experimental DLL may crash the game. See `Stage5-Readme.md` for detailed behaviour after source import.
