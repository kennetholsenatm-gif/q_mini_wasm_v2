# Sample qminiwasm CPU usage over a short window to infer single-thread saturation vs idle.
# Run while training is active. Does not require admin.
#
# Usage (from repo root):
#   powershell -NoProfile -File .\scripts\verify_training_cpu_pattern.ps1
#   powershell -NoProfile -File .\scripts\verify_training_cpu_pattern.ps1 -Name qminiwasm -Samples 8 -IntervalSec 2
param(
    [string]$Name = "qminiwasm",
    [int]$Samples = 6,
    [int]$IntervalSec = 2
)

$ErrorActionPreference = "Stop"
$logicalCores = [Environment]::ProcessorCount
Write-Host "[verify] Logical processors: $logicalCores" -ForegroundColor Cyan
Write-Host "[verify] Sampling process '$Name' ($Samples x ${IntervalSec}s). If one training thread is CPU-bound, expect ~$([math]::Round(100.0 / $logicalCores, 1))% total CPU per fully utilized core (Task Manager shows per-process % of all CPUs)." -ForegroundColor DarkGray
Write-Host "[verify] Also open Task Manager -> Details -> qminiwasm -> right-click -> 'Analyze wait chain' if available." -ForegroundColor DarkGray
Write-Host ""

$p = Get-Process -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) {
    Write-Host "[verify] ERROR: No process named '$Name'. Start training first (repo-root qminiwasm.exe)." -ForegroundColor Red
    exit 1
}

$prevCpu = $p.TotalProcessorTime
$prevAt = Get-Date
for ($i = 0; $i -lt $Samples; $i++) {
    Start-Sleep -Seconds $IntervalSec
    try {
        $p.Refresh()
    } catch {
        Write-Host "[verify] Process exited." -ForegroundColor Yellow
        exit 0
    }
    $now = Get-Date
    $cpu = $p.TotalProcessorTime
    $wall = ($now - $prevAt).TotalSeconds
    $cpuSec = ($cpu - $prevCpu).TotalSeconds
    # TotalProcessorTime is sum across all threads; divide by wall and cores -> rough "average % of machine"
    $approxPct = if ($wall -gt 0) { [math]::Round(100.0 * $cpuSec / ($wall * $logicalCores), 1) } else { 0 }
    $threads = $p.Threads.Count
    Write-Host ("[{0}] threads={1} approx_avg_cpu%={2} (heuristic; see doc)" -f (Get-Date -Format "HH:mm:ss"), $threads, $approxPct)
    $prevCpu = $cpu
    $prevAt = $now
}

Write-Host ""
Write-Host "[verify] Done. Interpret: sustained low % with flat 'samples' -> likely blocked I/O/sync; ~100/logicalCores % with slow 'samples' -> often one hot thread in scalar training loops. Set training.timing_to_stderr = true in training TOML for per-batch ms breakdown (stderr)." -ForegroundColor Green
