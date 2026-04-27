# Build the single canonical WUI host: <repo>/qminiwasm.exe (see config/path_map.toml).
# Run from anywhere:  powershell -NoProfile -File .\scripts\build_qminiwasm.ps1
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "qminiwasm.exe"
Set-Location $repoRoot
Write-Host "[build] go build -o $exe ./cmd/qminiwasm" -ForegroundColor Cyan
& go build -o $exe ./cmd/qminiwasm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "[build] OK: $exe" -ForegroundColor Green
