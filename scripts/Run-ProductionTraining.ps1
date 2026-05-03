#Requires -Version 5.1
<#
.SYNOPSIS
  Production training host on this machine: Intel SYCL q_training + qminiwasm + data dir + transcript log.

.DESCRIPTION
  "Production" here means the same native stack the repo is designed for on Windows:
  - Real q_training.dll built with the mandatory SYCL native CMake path (MoE routing can use SYCL; see CONTRIBUTING.md).
  - Intel oneAPI runtime DLLs beside qminiwasm.exe (from build_qminiwasm.ps1).
  - QMINI_DATA_DIR pointing at your durable tree (default C:\q_mini_data).
  - Full PowerShell transcript so crashes and stalls are auditable.

  This is NOT a toy MSVC-only DLL next to a SYCL TOML (sycl_route_mode=on). Run build_qminiwasm.ps1 path.

.PARAMETER SkipBuild
  If set, do not invoke build_qminiwasm.ps1 (use when you already rebuilt and only want launch + logging).

.PARAMETER DataDir
  Override QMINI_DATA_DIR (default C:\q_mini_data).

.PARAMETER TrainingConfig
  Optional path or filename passed to QMINI_TRAINING_CONFIG (absolute, or name under <DataDir>\config\).

.EXAMPLE
  powershell -NoProfile -File .\scripts\Run-ProductionTraining.ps1

.EXAMPLE
  powershell -NoProfile -File .\scripts\Run-ProductionTraining.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [string] $DataDir = "C:\q_mini_data",
    [string] $TrainingConfig = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$exe = Join-Path $repoRoot "qminiwasm.exe"
$dll = Join-Path $repoRoot "q_training.dll"

function Test-CriticalDll {
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Production check failed: missing $Name at $Path. Run without -SkipBuild or copy runtimes from oneAPI bin."
    }
}

if (-not $SkipBuild) {
    Write-Host "[production] Invoking scripts\build_qminiwasm.ps1 (SYCL Release + publish)..." -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "build_qminiwasm.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host "[production] -SkipBuild: assuming qminiwasm.exe + q_training.dll + Intel runtimes already published." -ForegroundColor Yellow
}

Test-CriticalDll $exe "qminiwasm.exe"
Test-CriticalDll $dll "q_training.dll"
foreach ($n in @("sycl8.dll", "ur_loader.dll", "libiomp5md.dll", "libmmd.dll")) {
    Test-CriticalDll (Join-Path $repoRoot $n) $n
}

if (-not (Test-Path -LiteralPath $DataDir)) {
    throw "QMINI_DATA_DIR target missing: $DataDir (create it and place config\, datasets\, etc.)"
}
$env:QMINI_DATA_DIR = $DataDir
if ($TrainingConfig -ne "") {
    $env:QMINI_TRAINING_CONFIG = $TrainingConfig
}

if (-not $env:OMP_NUM_THREADS) {
    $env:OMP_NUM_THREADS = [Environment]::ProcessorCount.ToString()
    Write-Host "[production] OMP_NUM_THREADS=$($env:OMP_NUM_THREADS)" -ForegroundColor Green
}

$logDir = Join-Path $DataDir "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$transcript = Join-Path $logDir "qminiwasm-production-$stamp.log"

Write-Host "[production] Repo:        $repoRoot" -ForegroundColor Cyan
Write-Host "[production] Data:        $DataDir" -ForegroundColor Cyan
Write-Host "[production] Transcript:  $transcript" -ForegroundColor Cyan
if ($env:QMINI_TRAINING_CONFIG) {
    Write-Host "[production] TOML override: QMINI_TRAINING_CONFIG=$($env:QMINI_TRAINING_CONFIG)" -ForegroundColor Cyan
}

$code = 0
try {
    Start-Transcript -Path $transcript -Force | Out-Null
    Write-Host "[production] Starting qminiwasm (Ctrl+C to stop; MCP/WUI on QMINI_HTTP_PORT or 9090)..." -ForegroundColor Green
    & $exe
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }
} finally {
    try { Stop-Transcript | Out-Null } catch { }
}
exit $code
