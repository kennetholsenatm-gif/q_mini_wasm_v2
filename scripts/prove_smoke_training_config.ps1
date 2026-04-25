# Proves QMINI_TRAINING_CONFIG + metrics wiring: starts qminiwasm on a free port (QMINI_HTTP_PORT),
# checks startup log keys, POSTs wui_get_training_metrics, expects training_config_path to contain
# training_config.smoke.toml. Requires CGO-built qminiwasm.exe at repo root.
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$smoke = Join-Path $repoRoot "config\training_config.smoke.toml"
if (-not (Test-Path $smoke)) {
    throw "Missing $smoke"
}

$exe = Join-Path $repoRoot "qminiwasm.exe"
if (-not (Test-Path $exe)) {
    Write-Host "[prove] Building qminiwasm.exe..." -ForegroundColor Yellow
    go build -o $exe ./cmd/qminiwasm
}

$outLog = Join-Path $repoRoot "prove_smoke_server.stdout.log"
$errLog = Join-Path $repoRoot "prove_smoke_server.stderr.log"
Remove-Item $outLog, $errLog -ErrorAction SilentlyContinue

$env:QMINI_TRAINING_CONFIG = $smoke
$env:QMINI_TRAINING_VERBOSE = "0"

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
    throw "Could not find a free TCP port after 40 attempts"
}
$httpPort = Get-FreeTcpPort
$env:QMINI_HTTP_PORT = "$httpPort"
Write-Host "[prove] Using QMINI_HTTP_PORT=$httpPort" -ForegroundColor Cyan

$p = Start-Process -FilePath $exe -WorkingDirectory $repoRoot -PassThru `
    -WindowStyle Hidden -RedirectStandardOutput $outLog -RedirectStandardError $errLog

$deadline = (Get-Date).AddSeconds(45)
$ready = $false
while ((Get-Date) -lt $deadline) {
    if (Test-Path $outLog) {
        $txt = Get-Content $outLog -Raw -ErrorAction SilentlyContinue
        if ($txt -and ($txt -match "Resolved keys: training.batch_size=256") -and ($txt -match "moe_top_k=8") -and ($txt -match "steane_correction=false")) {
            $ready = $true
            break
        }
        if ($txt -match "HTTP:.*9090|address already in use|bind") {
            throw "Server failed to bind (see $outLog / $errLog)"
        }
    }
    Start-Sleep -Milliseconds 250
}

if (-not $ready) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    throw "Timeout: server did not print expected Resolved keys (see $outLog)"
}

$mcp = "http://127.0.0.1:$httpPort/mcp"
$body = '{"id":"prove1","method":"wui_get_training_metrics","params":{}}'
$resp = Invoke-RestMethod -Uri $mcp -Method Post -ContentType "application/json" -Body $body -TimeoutSec 15
if (-not $resp.result) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    throw "Unexpected MCP response: $($resp | ConvertTo-Json -Depth 6)"
}
$path = [string]$resp.result.training_config_path
if ($path -notmatch [regex]::Escape("training_config.smoke.toml")) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    throw "training_config_path should contain training_config.smoke.toml; got: $path"
}

$ld = Invoke-RestMethod -Uri $mcp -Method Post -ContentType "application/json" `
    -Body '{"id":"prove2","method":"wui_load_config","params":{}}' -TimeoutSec 15
$ldPath = [string]$ld.result.path
if ($ldPath -notmatch [regex]::Escape("training_config.smoke.toml")) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    throw "wui_load_config path should be smoke TOML; got: $ldPath"
}

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

Write-Host ""
Write-Host "PASS: smoke config is active (startup log + MCP training_config_path + wui_load_config.path)." -ForegroundColor Green
Write-Host "  training_config_path = $path"
Write-Host "  wui_load_config.path   = $ldPath"
Write-Host "  Log tail (stdout):"
Get-Content $outLog -Tail 8
Remove-Item $outLog, $errLog -ErrorAction SilentlyContinue
exit 0
