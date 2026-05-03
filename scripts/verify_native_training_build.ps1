#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Verify qminiwasm.exe and q_training.dll are co-located for CGO loading (same folder as exe).

.DESCRIPTION
  Checks repo-root layout expected by cmd/qminiwasm CGO (-L paths / load beside exe).
  Optionally compares last-write times (warn if DLL much older than exe).

.PARAMETER RepoRoot
  Defaults to parent of scripts/.

.EXAMPLE
  .\scripts\verify_native_training_build.ps1
#>
param([string]$RepoRoot = "")

$ErrorActionPreference = "Stop"
if (-not $RepoRoot) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$exe = Join-Path $RepoRoot "qminiwasm.exe"
$dll = Join-Path $RepoRoot "q_training.dll"
$dllRt = Join-Path $RepoRoot "q_mini_wasm_v2\native_runtime\q_training.dll"

Write-Host "=== Native training build layout ===" -ForegroundColor Cyan
Write-Host "Repo: $RepoRoot"

if (-not (Test-Path $exe)) {
    Write-Host "MISSING: $exe (go build -o qminiwasm.exe ./cmd/qminiwasm)" -ForegroundColor Red
    exit 1
}
Write-Host "OK: $exe ($(Get-Item $exe | Select-Object -ExpandProperty LastWriteTime))"

if (Test-Path $dll) {
    $t = Get-Item $dll
    Write-Host "OK: $dll ($($t.LastWriteTime))"
    $ex = Get-Item $exe
    if ($t.LastWriteTime -lt $ex.LastWriteTime.AddHours(-24)) {
        Write-Host "WARN: q_training.dll is older than qminiwasm.exe by >24h; rebuild DLL from same commit as Go host." -ForegroundColor Yellow
    }
} else {
    Write-Host "MISSING: $dll - copy from SYCL build output or native_runtime (exe directory must contain DLL at runtime)." -ForegroundColor Red
}

if (Test-Path $dllRt) {
    Write-Host "Also found: $dllRt (CMake publish target)"
}

Write-Host "`nWhen Training_InitSession runs you should see stdout: crash_hooks_installed=1 - see q_mini_docs/POST_POLL_NATIVE_DEBUG.md" -ForegroundColor DarkGray
exit 0
