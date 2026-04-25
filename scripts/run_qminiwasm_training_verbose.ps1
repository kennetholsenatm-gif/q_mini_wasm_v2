#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Run qminiwasm with QMINI_TRAINING_VERBOSE=1 so the Go poll loop logs DLL progress every ~60s.

  From repo root (use powershell.exe if pwsh is not on PATH):
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run_qminiwasm_training_verbose.ps1 -Smoke -Rebuild

.DESCRIPTION
  Native stderr (C++) is where the pipeline prints stall hints; watch the same console for:
    [TrainingPipeline] TIMEOUT
    [TrainingPipeline] Waiting for DataSynthesizer to produce samples
    [TrainingPipeline] moe_input_dim is 0
    PREFILL_STARVATION

  -Smoke: set QMINI_TRAINING_CONFIG to repo config/training_config.smoke.toml (smaller batch, top_k=8, Steane off).

  Always runs repo-root qminiwasm.exe (built here if missing). Do not use a stale
  cmd\qminiwasm\qminiwasm.exe - older builds ignore QMINI_TRAINING_CONFIG, so -Smoke would
  look identical to normal.

  Build: go build -o qminiwasm.exe ./cmd/qminiwasm
  Run from repo root so CGO can find q_training.dll under q_mini_wasm_v2/build_final.
#>
param(
    [switch]$Smoke,
    # Rebuild before run (recommended once after pulling changes that touch cmd/qminiwasm).
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$env:QMINI_TRAINING_VERBOSE = "1"

if ($Smoke) {
    $smokePath = Join-Path $repoRoot "config\training_config.smoke.toml"
    if (-not (Test-Path $smokePath)) {
        Write-Error "Missing $smokePath"
        exit 1
    }
    $env:QMINI_TRAINING_CONFIG = $smokePath
    Write-Host "[run] QMINI_TRAINING_CONFIG=$($env:QMINI_TRAINING_CONFIG)" -ForegroundColor Cyan
    if (-not $Rebuild) {
        Write-Host "[run] Tip: if server startup still shows batch_size=16384, your qminiwasm.exe is stale - run: .\scripts\run_qminiwasm_training_verbose.ps1 -Smoke -Rebuild" -ForegroundColor Yellow
    }
} else {
    Remove-Item Env:\QMINI_TRAINING_CONFIG -ErrorAction SilentlyContinue
    Write-Host "[run] Using default training config: C:\q_mini_data\config\training_config.toml" -ForegroundColor Cyan
}

Write-Host "[run] QMINI_TRAINING_VERBOSE=1 (stderr patterns listed in script header)" -ForegroundColor Green

$exe = Join-Path $repoRoot "qminiwasm.exe"
$needsBuild = $Rebuild -or -not (Test-Path $exe)
if ($needsBuild) {
    Write-Host "Building qminiwasm.exe at repo root (CGO)..." -ForegroundColor Yellow
    go build -o $exe ./cmd/qminiwasm
    if (-not (Test-Path $exe)) {
        Write-Error "go build failed; expected $exe"
        exit 1
    }
}

Write-Host ('[run] Executable: ' + $exe) -ForegroundColor DarkGray
& $exe
