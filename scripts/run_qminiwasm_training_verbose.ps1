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
  -SmokePostPoll: use config/training_config.smoke_postpoll.toml (binary search for first process_batch / silent exit).

  Always runs repo-root qminiwasm.exe (built here if missing). Do not use a stale
  cmd\qminiwasm\qminiwasm.exe - older builds ignore QMINI_TRAINING_CONFIG, so -Smoke would
  look identical to normal.

  Build: go build -o qminiwasm.exe ./cmd/qminiwasm  (or .\scripts\build_qminiwasm.ps1)
  Layout: config/path_map.toml — q_training.dll is loaded from the exe directory; CMake also publishes to native_runtime/ when the root copy is locked.

  Parallelism:
  - OMP_NUM_THREADS defaults here to ProcessorCount so GF3/OpenMP layers in q_training.dll use all logical CPUs (override env if needed).
  - GPU/SYCL routing requires a native q_training build with Intel oneAPI (icx/icpx) per CONTRIBUTING.md; training.sycl_route_mode alone does not bundle SYCL code.
  Control-plane rule: epochs come from the active TOML only. Do not send WUI epochs overrides.
  Progress rule: samples/samples_total are real training counters (no ingestion substitution).
#>
param(
    [switch]$Smoke,
    [switch]$SmokePostPoll,
    # Rebuild before run (recommended once after pulling changes that touch cmd/qminiwasm).
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

# OpenMP in q_training.dll (GF3 / layer loops): use all logical processors if unset.
if (-not $env:OMP_NUM_THREADS) {
    $n = [Environment]::ProcessorCount
    $env:OMP_NUM_THREADS = "$n"
    Write-Host "[run] OMP_NUM_THREADS=$n (GF3 OpenMP; set env yourself to cap/override)" -ForegroundColor Cyan
} else {
    Write-Host "[run] OMP_NUM_THREADS=$($env:OMP_NUM_THREADS) (already set)" -ForegroundColor Cyan
}

# Go host + q_training.dll (SYCL/Intel) can load two OpenMP runtimes; without this, SIGABRT is common in crash log.
if (-not $env:KMP_DUPLICATE_LIB_OK) {
    $env:KMP_DUPLICATE_LIB_OK = "TRUE"
    Write-Host "[run] KMP_DUPLICATE_LIB_OK=TRUE (duplicate OpenMP runtimes; qminiwasm also sets on Windows if unset)" -ForegroundColor Cyan
} else {
    Write-Host "[run] KMP_DUPLICATE_LIB_OK=$($env:KMP_DUPLICATE_LIB_OK) (already set)" -ForegroundColor Cyan
}

$env:QMINI_TRAINING_VERBOSE = "1"

if ($SmokePostPoll) {
    $pp = Join-Path $repoRoot "config\training_config.smoke_postpoll.toml"
    if (-not (Test-Path $pp)) {
        Write-Error "Missing $pp"
        exit 1
    }
    $env:QMINI_TRAINING_CONFIG = $pp
    Write-Host "[run] QMINI_TRAINING_CONFIG=$($env:QMINI_TRAINING_CONFIG) (smoke post-poll / first batch search)" -ForegroundColor Cyan
} elseif ($Smoke) {
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
Write-Host "[run] For [TrainingTiming] breakdowns, set training.timing_to_stderr=true in the active TOML (see q_mini_docs/GPU_FEED_TUNING_E2E.md)" -ForegroundColor DarkGray

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
