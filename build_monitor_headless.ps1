$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

powershell -ExecutionPolicy Bypass -File .\build_monitor.ps1 | Out-Host

$cxx = if ($env:CXX) { $env:CXX } else { "g++" }
$runtimeDir = Join-Path $PSScriptRoot "monitor_runtime"
$sources = @(
    "bit.cpp",
    "chip.cpp",
    "tec2.cpp",
    "monitor.cpp"
)

& $cxx -std=c++14 -O2 -Wall -Wextra "-I$(Join-Path $PSScriptRoot 'headless_curses')" -o (Join-Path $runtimeDir "monitor_headless.exe") $sources
if ($LASTEXITCODE -ne 0) {
    throw "headless monitor build failed with exit code $LASTEXITCODE"
}

$monitorObject = Join-Path $runtimeDir "monitor_f10_monitor.o"
& $cxx -std=c++14 -O2 -Wall -Wextra "-I$(Join-Path $PSScriptRoot 'headless_curses')" -Dmain=monitor_original_main -c "monitor.cpp" -o $monitorObject
if ($LASTEXITCODE -ne 0) {
    throw "headless monitor F10 object build failed with exit code $LASTEXITCODE"
}

& $cxx -std=c++14 -O2 -Wall -Wextra "-I$(Join-Path $PSScriptRoot 'headless_curses')" `
    -o (Join-Path $runtimeDir "monitor_f10_headless.exe") `
    "bit.cpp" "chip.cpp" "tec2.cpp" $monitorObject "headless_curses\monitor_f10_main.cpp"
if ($LASTEXITCODE -ne 0) {
    throw "headless monitor F10 oracle build failed with exit code $LASTEXITCODE"
}

& $cxx -std=c++14 -O2 -Wall -Wextra "-I$(Join-Path $PSScriptRoot 'headless_curses')" `
    -o (Join-Path $runtimeDir "monitor_f10_dump_headless.exe") `
    "bit.cpp" "chip.cpp" "tec2.cpp" $monitorObject "headless_curses\monitor_f10_dump_main.cpp"
if ($LASTEXITCODE -ne 0) {
    throw "headless monitor F10 dump oracle build failed with exit code $LASTEXITCODE"
}

& $cxx -std=c++14 -O2 -Wall -Wextra "-I$(Join-Path $PSScriptRoot 'headless_curses')" `
    -o (Join-Path $runtimeDir "monitor_f10_seed_headless.exe") `
    "bit.cpp" "chip.cpp" "tec2.cpp" $monitorObject "headless_curses\monitor_f10_seed_main.cpp"
if ($LASTEXITCODE -ne 0) {
    throw "headless monitor F10 seeded oracle build failed with exit code $LASTEXITCODE"
}

& $cxx -std=c++14 -O2 -Wall -Wextra "-I$(Join-Path $PSScriptRoot 'headless_curses')" `
    -o (Join-Path $runtimeDir "monitor_f10_payload_headless.exe") `
    "bit.cpp" "chip.cpp" "tec2.cpp" $monitorObject "headless_curses\monitor_f10_payload_main.cpp"
if ($LASTEXITCODE -ne 0) {
    throw "headless monitor F10 payload oracle build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $runtimeDir\monitor_headless.exe from original monitor.cpp with headless curses shim"
Write-Host "Built $runtimeDir\monitor_f10_headless.exe for original monitor.cpp onF10 oracle"
Write-Host "Built $runtimeDir\monitor_f10_dump_headless.exe for original monitor.cpp onF10 read/dump oracle"
Write-Host "Built $runtimeDir\monitor_f10_seed_headless.exe for original monitor.cpp seeded F10 oracle"
Write-Host "Built $runtimeDir\monitor_f10_payload_headless.exe for original monitor.cpp payload F10 oracle"
