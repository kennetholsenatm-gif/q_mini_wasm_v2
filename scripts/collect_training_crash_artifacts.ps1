#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Collect native training crash artifacts after qminiwasm exits silently (plan: POST_POLL_NATIVE_DEBUG).

.DESCRIPTION
  Prints paths and tails %LOCALAPPDATA%\q_mini_training_crash.log (and %TEMP% fallback if present).
  Use after reproducing from a terminal so stdout/stderr lines can be matched to phases:

  Last line BEFORE "[Training] About to call C.Training_StartTraining..." -> Training_InitSession fault / early DLL.
  Last line BEFORE "[Training] C.Training_StartTraining returned: 0"      -> synchronous initialize/start_training.
  Returned 0 then exit                                                       -> training_thread / first process_batch.

  SIGABRT / vectored SEH lines reference q_mini_docs/POST_POLL_NATIVE_DEBUG.md.

.PARAMETER TailLines
  Lines to show from the crash log (default 60).

.EXAMPLE
  .\scripts\collect_training_crash_artifacts.ps1
#>
param([int]$TailLines = 60)

$ErrorActionPreference = "Stop"
$local = Join-Path $env:LOCALAPPDATA "q_mini_training_crash.log"
$tmp = Join-Path $env:TEMP "q_mini_training_crash.log"

Write-Host "=== Training crash artifact collector ===" -ForegroundColor Cyan
Write-Host "Primary log: $local"
Write-Host "Temp mirror: $tmp (may be absent)"

foreach ($p in @($local, $tmp)) {
    if (Test-Path $p) {
        Write-Host "`n--- Tail $TailLines lines of $p ---" -ForegroundColor Yellow
        Get-Content $p -Tail $TailLines -ErrorAction Stop
    }
}

Write-Host "`nOptional: enable minidumps with .\scripts\enable_wer_localdumps.ps1" -ForegroundColor DarkGray

Write-Host "`n=== Phase cheat sheet (match last console line / bc= breadcrumb) ===" -ForegroundColor Green
Write-Host "Before 'About to call C.Training_StartTraining'     -> Training_InitSession / SYCL probe / GPU checks."
Write-Host "After that line, no 'Training_StartTraining returned' -> pipeline initialize() or start_training() on Go thread."
Write-Host "'returned: 0' then exit                         -> training_thread; check stderr process_batch phases."
Write-Host "bc=pipeline:process_batch:enter                  -> first native batch entry (see POST_POLL_NATIVE_DEBUG.md)."
Write-Host "SIGABRT + libomp/intel in stack                  -> try KMP_DUPLICATE_LIB_OK=TRUE (set by qminiwasm init + run script)."
