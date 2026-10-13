# Complete PowerShell 7 build script for the experimental native FH3 camera test.
# Requires CMake and Visual Studio x64 MSVC C++ build tools.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root 'build-windows'
$Run = Join-Path $Root 'Stage5-Windows-Run'
$CMake = Get-Command 'cmake' -ErrorAction SilentlyContinue
if (-not $CMake) { throw 'CMake not found. Install CMake and Visual Studio 2022 or later with C++ desktop build tools.' }
$ctest = Get-Command 'ctest' -ErrorAction SilentlyContinue
if (-not $ctest) { throw 'CTest missing from CMake installation.' }
Write-Host 'Configuring native FH3 Stage 5 test (x64)...' -ForegroundColor Cyan
& cmake -S $Root -B $Build -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
Write-Host 'Building native DLL, injector, and unit tests...' -ForegroundColor Cyan
& cmake --build $Build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }
Write-Host 'Running all five test suites...' -ForegroundColor Cyan
& ctest --test-dir $Build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "CTest failed: $LASTEXITCODE" }
New-Item -ItemType Directory -Force -Path $Run | Out-Null
$Binaries = @('FH3NativeCameraTest.dll', 'FH3NativeCameraLoader.exe')
foreach ($File in $Binaries) {
    $Source = Join-Path $Build "NativeRuntimeTest\Release\$File"
    if (-not (Test-Path -LiteralPath $Source)) { throw "Expected build output missing: $Source" }
    Copy-Item -LiteralPath $Source -Destination (Join-Path $Run $File) -Force
}
Write-Host "Build and tests passed. Windows binaries: $Run" -ForegroundColor Green
Write-Host 'Start FH3 in free roam. Then run:' -ForegroundColor Cyan
Write-Host "  & '$Run\FH3NativeCameraLoader.exe' --check"
Write-Host "  & '$Run\FH3NativeCameraLoader.exe' --activate"
Write-Host 'Commands in FH3: Ctrl+Alt+F8 FreeCam, Ctrl+Alt+F9 restore, Ctrl+Alt+F12 stop commands.'
Write-Host 'Log: $env:TEMP\FH3_Native_Camera_Stage5.log'
