$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$sources = @(
    "bit.cpp"
    "chip.cpp"
    "tec2.cpp"
    "tec2_cli.cpp"
)

# Static link so tec2_cli.exe runs from any shell without runtime DLLs
# (mixing UCRT64 / MINGW64 DLLs otherwise causes hard-to-debug crashes).
g++ -std=c++17 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -o tec2_cli.exe $sources
if ($LASTEXITCODE -ne 0) {
    throw "g++ build failed with exit code $LASTEXITCODE"
}
Write-Host "Built $PSScriptRoot\\tec2_cli.exe (static)"
