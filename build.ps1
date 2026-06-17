$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$DistDir = Join-Path $Root "dist"
$ResFile = Join-Path $BuildDir "app.res"
$ExeFile = Join-Path $DistDir "MiniMonitor.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

windres "$Root\src\app.rc" -O coff -o "$ResFile"
if ($LASTEXITCODE -ne 0) {
  throw "windres failed with exit code $LASTEXITCODE"
}

g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows `
  "$Root\src\main.cpp" "$ResFile" `
  -o "$ExeFile" `
  -lgdiplus -lgdi32 -lmsimg32 -luser32 -lshell32 -ladvapi32 -liphlpapi -lpsapi -lpdh -lcomctl32
if ($LASTEXITCODE -ne 0) {
  throw "g++ failed with exit code $LASTEXITCODE"
}

Write-Host "Built $ExeFile"
