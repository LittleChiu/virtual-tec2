$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# Launch the scriptable, NON-curses CLI. Because it writes with plain stdout
# (no alternate screen), the terminal's own scrollback works normally:
# scroll up/down with the wheel, scrollbar or PageUp/PageDown.
if (-not (Test-Path .\tec2_cli.exe)) {
    & .\build.ps1
}

.\tec2_cli.exe
