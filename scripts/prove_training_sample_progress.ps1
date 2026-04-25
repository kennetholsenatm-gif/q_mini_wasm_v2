# Tries to prove the NATIVE pipeline is doing work: init -> start -> poll MCP until
# samples or samples_total increases (or ingestion data_acquired increases while training).
#
# Prerequisites (same as normal training):
#   - Real q_training.dll next to qminiwasm.exe (CGO build), not a stub-only link
#   - C:\q_mini_data\config\data_sources.toml present
#   - Dataset dir from training TOML [paths].dataset_dir with at least one .txt or .jsonl file
#
# Env (optional):
#   QMINI_TRAINING_CONFIG - absolute path to TOML (defaults to repo config\training_config.smoke.toml)
#
param(
    # First process_batch() can exceed 60s even on smoke; raise if you only care about final PASS.
    [int]$MaxWaitSec = 300,
    [int]$PollSec = 3
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$dataDir = "C:\q_mini_data"
$dataSources = Join-Path $dataDir "config\data_sources.toml"
$smoke = Join-Path $repoRoot "config\training_config.smoke.toml"
if (-not (Test-Path $smoke)) { throw "Missing $smoke" }

if (-not (Test-Path $dataSources)) {
    Write-Host "SKIP: Native processing cannot be verified without data_sources.toml at:" -ForegroundColor Yellow
    Write-Host "  $dataSources"
    Write-Host "Copy from repo config\data_sources.toml into that folder, then re-run."
    exit 2
}

$exe = Join-Path $repoRoot "qminiwasm.exe"
if (-not (Test-Path $exe)) {
    Write-Host "[prove] Building qminiwasm.exe..." -ForegroundColor Yellow
    go build -o $exe ./cmd/qminiwasm
}

function Get-FreeTcpPort {
    for ($i = 0; $i -lt 40; $i++) {
        $p = Get-Random -Minimum 22000 -Maximum 48000
        try {
            $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $p)
            $l.Start()
            $l.Stop()
            return $p
        } catch { }
    }
    throw "Could not find a free TCP port"
}

$httpPort = Get-FreeTcpPort
$env:QMINI_HTTP_PORT = "$httpPort"
if (-not $env:QMINI_TRAINING_CONFIG) {
    $env:QMINI_TRAINING_CONFIG = $smoke
}
$env:QMINI_TRAINING_VERBOSE = "1"

$outLog = Join-Path $repoRoot "prove_progress_server.stdout.log"
$errLog = Join-Path $repoRoot "prove_progress_server.stderr.log"
Remove-Item $outLog, $errLog -ErrorAction SilentlyContinue

Write-Host "[prove] QMINI_HTTP_PORT=$httpPort QMINI_TRAINING_CONFIG=$($env:QMINI_TRAINING_CONFIG)" -ForegroundColor Cyan

$p = Start-Process -FilePath $exe -WorkingDirectory $repoRoot -PassThru `
    -WindowStyle Hidden -RedirectStandardOutput $outLog -RedirectStandardError $errLog

$deadline = (Get-Date).AddSeconds(35)
while ((Get-Date) -lt $deadline) {
    if (Test-Path $outLog) {
        $t = Get-Content $outLog -Raw -ErrorAction SilentlyContinue
        if ($t -match "Listening on http://localhost:$httpPort") { break }
    }
    Start-Sleep -Milliseconds 200
}

$mcp = "http://127.0.0.1:$httpPort/mcp"

function Invoke-Mcp($method, $paramsObj) {
    $body = @{ id = "p"; method = $method; params = $paramsObj } | ConvertTo-Json -Compress -Depth 8
    return Invoke-RestMethod -Uri $mcp -Method Post -ContentType "application/json" -Body $body -TimeoutSec 120
}

try {
    $init = Invoke-Mcp "wui_init_training_pipeline" (@{})
    if ($init.error) {
        throw "wui_init_training_pipeline: $($init.error.message)"
    }
    Write-Host "[prove] Pipeline configured (Go)." -ForegroundColor Green

    $start = Invoke-Mcp "wui_start_ff_training" (@{})
    if ($start.error) {
        throw "wui_start_ff_training: $($start.error.message)"
    }
    Write-Host "[prove] Native start requested; polling metrics up to ${MaxWaitSec}s..." -ForegroundColor Green

    $t0 = Get-Date
    $last = $null
    $da0 = -1
    while (((Get-Date) - $t0).TotalSeconds -lt $MaxWaitSec) {
        Start-Sleep -Seconds $PollSec
        $m = Invoke-Mcp "wui_get_training_metrics" (@{})
        if ($m.error) { throw "wui_get_training_metrics: $($m.error.message)" }
        $r = $m.result
        $s = [int64]($r.samples_total)
        $se = [int]($r.samples)
        $da = [int]($r.ingestion.data_acquired)
        $dp = [int]($r.ingestion.data_perturbed)
        if ($da0 -lt 0) { $da0 = $da }
        $phase = [string]$r.phase
        $run = [bool]$r.is_running
        Write-Host ("[poll] phase={0} running={1} samples={2} samples_total={3} data_acquired={4} data_perturbed={5}" -f $phase, $run, $se, $s, $da, $dp)
        $last = $r
        if ($s -gt 0 -or $se -gt 0) {
            Write-Host ""
            Write-Host "PASS (training): Native pipeline reported expert-route work (samples_total=$s samples_epoch=$se)." -ForegroundColor Green
            $null = Invoke-Mcp "wui_stop_ff_training" (@{})
            exit 0
        }
    }

    Write-Host ""
    $daDelta = if ($last) { [int]($last.ingestion.data_acquired) - $da0 } else { 0 }
    if ($daDelta -ge 200) {
        Write-Host "PARTIAL PASS (data pipeline): ingestion/perturbation moved (data_acquired delta=$daDelta) while phase=$($last.phase), but samples/samples_total stayed 0 within ${MaxWaitSec}s." -ForegroundColor Yellow
        Write-Host "That usually means either (1) first process_batch() is still running (heavy MoE), or (2) batches complete with zero expert routes — see native stderr / server logs."
        $last | ConvertTo-Json -Depth 4
        $null = Invoke-Mcp "wui_stop_ff_training" (@{})
        exit 4
    }

    Write-Host "NO PROGRESS within ${MaxWaitSec}s (samples flat and data_acquired barely moved). Last snapshot:" -ForegroundColor Yellow
    $last | ConvertTo-Json -Depth 6
    Write-Host ""
    Write-Host "Check native stderr and:" -ForegroundColor Yellow
    Write-Host "  $outLog"
    Write-Host "  $errLog"
    $null = Invoke-Mcp "wui_stop_ff_training" (@{})
    exit 1
}
finally {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
    Remove-Item $outLog, $errLog -ErrorAction SilentlyContinue
}
