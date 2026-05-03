# Copies the repo frontier training_config.toml into the active QMINI data tree so qminiwasm
# (which loads <DataDir>/config/training_config.toml by default) picks up throughput fixes.
# Usage (from repo root, PowerShell):
#   .\scripts\sync_frontier_training_config_to_data_dir.ps1
# Optional: $env:QMINI_DATA_DIR = "D:\q_mini_data"

$ErrorActionPreference = "Stop"
# scripts/ -> repository root (contains config/training_config.toml)
$repoRoot = Split-Path $PSScriptRoot -Parent
$src = Join-Path $repoRoot "config\training_config.toml"
if (-not (Test-Path $src)) {
    Write-Error "Missing repo file: $src (run from q_mini_wasm_v2 clone with config/training_config.toml)"
}
$dataRoot = $env:QMINI_DATA_DIR
if ([string]::IsNullOrWhiteSpace($dataRoot)) {
    $dataRoot = "C:\q_mini_data"
}
$destDir = Join-Path $dataRoot "config"
$dest = Join-Path $destDir "training_config.toml"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null
if (Test-Path $dest) {
    $bak = "$dest.bak." + (Get-Date -Format "yyyyMMdd-HHmmss")
    Copy-Item -LiteralPath $dest -Destination $bak -Force
    Write-Host "Backed up existing config to $bak"
}
Copy-Item -LiteralPath $src -Destination $dest -Force
Write-Host "Wrote frontier training config:"
Write-Host "  $dest"
Write-Host "Restart qminiwasm and rebuild q_training.dll if you changed Training_InitSession ABI."
