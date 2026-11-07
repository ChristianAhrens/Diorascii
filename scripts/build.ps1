# Build, test and package on Windows (PowerShell).   usage: scripts/build.ps1 [dev|release]
param([string]$Mode = "release")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

cmake --preset $Mode
cmake --build "build/$Mode" --config Release --parallel
ctest --test-dir "build/$Mode" -C Release --output-on-failure
if ($Mode -eq "release") {
  Push-Location "build/$Mode"; cpack -C Release; Pop-Location
  Write-Host "binary: build/$Mode/src/Release/diorascii.exe"
}
