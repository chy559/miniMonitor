$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$TestExe = Join-Path $BuildDir "monitor_logic_tests.exe"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic `
  "$Root\tests\monitor_logic_tests.cpp" `
  -o "$TestExe"
if ($LASTEXITCODE -ne 0) {
  throw "Test compilation failed with exit code $LASTEXITCODE"
}

& $TestExe
if ($LASTEXITCODE -ne 0) {
  throw "Tests failed with exit code $LASTEXITCODE"
}
