# PROOF GATE: native training must report samples_total > 0 AND gf3_hebbian_weight_cell_updates > 0
# (expert FF routes + GF(3) Hebbian steps applied to expert linear weight cells).
#
# Creates a tiny local text corpus under C:\q_mini_data\datasets\proof_corpus,
# generates a data_sources TOML with ONLY that directory (no web APIs),
# sets QMINI_TRAINING_CONFIG to config/training_config.proof.toml (tiny MoE),
# starts qminiwasm on a free port, init, start training, polls MCP until samples move.
#
# Requires: CGO-built qminiwasm.exe + q_training.dll (real training, not stub).
#
param([int]$MaxWaitSec = 240)

$ErrorActionPreference = "Stop"
$dataDir = "C:\q_mini_data"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$corpusDir = Join-Path $dataDir "datasets\proof_corpus"
New-Item -ItemType Directory -Force -Path $corpusDir | Out-Null
$proofTxt = Join-Path $corpusDir "proof_lines.txt"
# Long lines so acquisition / min-length gates never drop everything (>= 120 chars).
$oneLine = ("PROOF|" * 25) + ("0123456789" * 6)
Set-Content -Path $proofTxt -Value "" -Encoding UTF8
1..500 | ForEach-Object { Add-Content -Path $proofTxt -Value $oneLine -Encoding UTF8 }

$artifacts = Join-Path $repoRoot "prove_artifacts"
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$dsGen = Join-Path $artifacts "data_sources.generated.toml"
@(
    '[settings]',
    'max_concurrent = 2',
    '',
    '[[source]]',
    'name = "proof_corpus_txt"',
    'type = "directory"',
    "path = `"$corpusDir`"",
    'enabled = true',
    'pattern = "*.txt"',
    ''
) | Set-Content -Path $dsGen -Encoding UTF8

$env:QMINI_DATA_SOURCES_TOML = $dsGen
# Epochs are sourced from the training TOML (no WUI/CLI epochs override on this path).
# Progress counters are strict real training counters (no ingestion substitution).
$env:QMINI_TRAINING_CONFIG = (Join-Path $repoRoot "config\training_config.proof.toml")
$env:QMINI_TRAINING_VERBOSE = "1"

function Get-FreeTcpPort {
    for ($i = 0; $i -lt 50; $i++) {
        $p = Get-Random -Minimum 23000 -Maximum 49000
        try {
            $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $p)
            $l.Start()
            $l.Stop()
            return $p
        } catch { }
    }
    throw "no free port"
}
$httpPort = Get-FreeTcpPort
$env:QMINI_HTTP_PORT = "$httpPort"

# Single canonical binary at repository root (see config/path_map.toml).
$exe = Join-Path $repoRoot "qminiwasm.exe"
$dllSrc = Join-Path $repoRoot "q_mini_wasm_v2\build_final\q_training.dll"
if (-not (Test-Path -LiteralPath $dllSrc)) {
    throw "Missing $dllSrc - run: cmake --build q_mini_wasm_v2/build_final --target q_training --config Release"
}
# Prefer fresh DLL next to the canonical exe (CGO runtime load order).
Copy-Item -LiteralPath $dllSrc -Destination (Join-Path $repoRoot "q_training.dll") -Force -ErrorAction SilentlyContinue
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot "q_training.dll"))) {
    $alt = Join-Path $repoRoot "native_runtime\q_training.dll"
    if (Test-Path -LiteralPath $alt) {
        Copy-Item -LiteralPath $alt -Destination (Join-Path $repoRoot "q_training.dll") -Force
    }
}
Write-Host "[prove] Building qminiwasm.exe -> $exe" -ForegroundColor Yellow
Push-Location $repoRoot
go build -o $exe ./cmd/qminiwasm
Pop-Location

$outLog = Join-Path $artifacts "server.stdout.log"
$errLog = Join-Path $artifacts "server.stderr.log"
Remove-Item $outLog, $errLog -ErrorAction SilentlyContinue

Write-Host "[prove] QMINI_HTTP_PORT=$httpPort" -ForegroundColor Cyan
Write-Host "[prove] QMINI_DATA_SOURCES_TOML=$($env:QMINI_DATA_SOURCES_TOML)" -ForegroundColor Cyan
Write-Host "[prove] QMINI_TRAINING_CONFIG=$($env:QMINI_TRAINING_CONFIG)" -ForegroundColor Cyan

$p = Start-Process -FilePath $exe -WorkingDirectory $repoRoot -PassThru `
    -WindowStyle Hidden -RedirectStandardOutput $outLog -RedirectStandardError $errLog
# WorkingDirectory=repoRoot so WUI and config/path_map.toml resolve; q_training.dll loads from exe directory.

$deadline = (Get-Date).AddSeconds(40)
while ((Get-Date) -lt $deadline) {
    if (Test-Path $outLog) {
        $t = Get-Content $outLog -Raw -ErrorAction SilentlyContinue
        if ($t -match "Listening on http://localhost:$httpPort") { break }
    }
    Start-Sleep -Milliseconds 200
}

$mcp = "http://127.0.0.1:$httpPort/mcp"
function Invoke-Mcp($method, $paramsObj) {
    $body = @{ id = "t"; method = $method; params = $paramsObj } | ConvertTo-Json -Compress -Depth 8
    return Invoke-RestMethod -Uri $mcp -Method Post -ContentType "application/json" -Body $body -TimeoutSec 180
}

try {
    $init = Invoke-Mcp "wui_init_training_pipeline" @{}
    if ($init.error) { throw "init: $($init.error.message)" }

    $st = Invoke-Mcp "wui_start_ff_training" @{}
    if ($st.error) { throw "start: $($st.error.message)" }

    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $MaxWaitSec) {
        Start-Sleep -Seconds 2
        $m = Invoke-Mcp "wui_get_training_metrics" @{}
        if ($m.error) { throw "metrics: $($m.error.message)" }
        $r = $m.result
        $stot = [int64]$r.samples_total
        $s = [int]$r.samples
        $hw = [int64]$r.gf3_hebbian_weight_cell_updates
        $phase = [string]$r.phase
        Write-Host ("[poll] phase={0} samples={1} samples_total={2} gf3_hebbian_weight_cell_updates={3}" -f $phase, $s, $stot, $hw)
        if (($stot -gt 0 -or $s -gt 0) -and $hw -gt 0) {
            Write-Host ""
            Write-Host "PASS: TRAINING WORKS - expert routes ran AND GF(3) Hebbian weight updates applied (steps=$hw, samples_total=$stot)." -ForegroundColor Green
            $null = Invoke-Mcp "wui_stop_ff_training" @{}
            exit 0
        }
    }

    Write-Host ""
    Write-Host "FAIL: Did not see samples_total>0 AND gf3_hebbian_weight_cell_updates>0 within ${MaxWaitSec}s. See logs:" -ForegroundColor Red
    Write-Host "  $outLog"
    Write-Host "  $errLog"
    $null = Invoke-Mcp "wui_stop_ff_training" @{}
    exit 1
}
finally {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
