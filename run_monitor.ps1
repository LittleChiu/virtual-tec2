$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$ucrtRoot = if ($env:MSYS2_UCRT64_ROOT) { $env:MSYS2_UCRT64_ROOT } else { "C:\msys64\ucrt64" }
$ucrtBin = Join-Path $ucrtRoot "bin"
$runtimeDir = Join-Path $PSScriptRoot "monitor_runtime"

if (-not (Test-Path (Join-Path $runtimeDir "monitor.exe"))) {
    & .\build_monitor.ps1
}

$env:PATH = "$ucrtBin;$env:PATH"
$env:TERMINFO = Join-Path $ucrtRoot "share\terminfo"
if (-not $env:TERM) {
    $env:TERM = "xterm-256color"
}

Push-Location $runtimeDir
try {
    & .\monitor.exe
}
finally {
    Pop-Location
}
