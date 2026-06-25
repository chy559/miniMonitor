$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$DistDir = Join-Path $Root "dist"
$ResFile = Join-Path $BuildDir "app.res"
$ExeFile = Join-Path $DistDir "MiniMonitor.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$RunningInstances = Get-Process -Name "MiniMonitor" -ErrorAction SilentlyContinue | Where-Object {
  try {
    $_.Path -eq $ExeFile
  } catch {
    $false
  }
}
if ($RunningInstances) {
  $Pids = ($RunningInstances | Select-Object -ExpandProperty Id) -join ", "
  throw "MiniMonitor.exe is running from dist (PID: $Pids). Exit it from the tray before building so the file can be replaced."
}

windres "$Root\src\app.rc" -O coff -o "$ResFile"
if ($LASTEXITCODE -ne 0) {
  throw "windres failed with exit code $LASTEXITCODE"
}

g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static -static-libgcc -static-libstdc++ `
  "$Root\src\main.cpp" "$ResFile" `
  -o "$ExeFile" `
  -lgdiplus -lgdi32 -lmsimg32 -luser32 -lshell32 -ladvapi32 -liphlpapi -lpsapi -lpdh -lwinhttp -lcomctl32
if ($LASTEXITCODE -ne 0) {
  throw "g++ failed with exit code $LASTEXITCODE"
}

Write-Host "Built $ExeFile"
