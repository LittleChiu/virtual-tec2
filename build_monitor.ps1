$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$ucrtRoot = if ($env:MSYS2_UCRT64_ROOT) { $env:MSYS2_UCRT64_ROOT } else { "C:\msys64\ucrt64" }
$cxx = if ($env:CXX) { $env:CXX } else { Join-Path $ucrtRoot "bin\g++.exe" }
$includeDir = Join-Path $ucrtRoot "include\ncursesw"
$libDir = Join-Path $ucrtRoot "lib"

if (-not (Test-Path $cxx)) {
    throw "UCRT64 g++ not found: $cxx"
}
if (-not (Test-Path (Join-Path $includeDir "curses.h"))) {
    throw "ncursesw header not found. Install MSYS2 package: mingw-w64-ucrt-x86_64-ncurses"
}
if (-not (Test-Path (Join-Path $libDir "libncursesw.a"))) {
    throw "ncursesw library not found. Install MSYS2 package: mingw-w64-ucrt-x86_64-ncurses"
}

$sources = @(
    "bit.cpp"
    "chip.cpp"
    "tec2.cpp"
    "monitor.cpp"
)

& $cxx -std=c++14 -O2 -Wall -Wextra "-I$includeDir" "-L$libDir" -o monitor.exe $sources -lncursesw
if ($LASTEXITCODE -ne 0) {
    throw "monitor build failed with exit code $LASTEXITCODE"
}

$runtimeDir = Join-Path $PSScriptRoot "monitor_runtime"
New-Item -ItemType Directory -Path $runtimeDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "monitor.exe") -Destination (Join-Path $runtimeDir "monitor.exe") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "MCR.ROM") -Destination (Join-Path $runtimeDir "MCR.ROM") -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "MAPROM.ROM") -Destination (Join-Path $runtimeDir "MAPROM.ROM") -Force

# monitor.cpp unconditionally strips the last character of each INST.ROM line.
# With UCRT text-mode input, CRLF is translated to LF first, so a normal CRLF
# file would lose the final mnemonic character. CRCRLF leaves one CR to strip.
$instText = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot "INST.ROM"), [System.Text.Encoding]::ASCII)
$instText = $instText -replace "`r?`n", "`r`r`n"
[System.IO.File]::WriteAllText((Join-Path $runtimeDir "INST.ROM"), $instText, [System.Text.Encoding]::ASCII)

Write-Host "Built $PSScriptRoot\monitor.exe from the original monitor.cpp"
Write-Host "Prepared $runtimeDir with UCRT-compatible INST.ROM line endings"
