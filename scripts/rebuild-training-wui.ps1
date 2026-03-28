# Build the Training WUI Go binary from the repository root (embeds web/index.html).
#
# Usage (from repo root):
#   .\scripts\rebuild-training-wui.ps1
#   .\scripts\rebuild-training-wui.ps1 -Test   # runs go test -count=1 (no cached results)
#   .\scripts\rebuild-training-wui.ps1 -Generate
#
# Output: training-wui\training-wui.exe on Windows, training-wui/training-wui elsewhere.
param(
    [switch] $Test,
    [switch] $Generate
)

$ErrorActionPreference = "Stop"

$Root = Split-Path $PSScriptRoot -Parent
$WuiDir = Join-Path $Root "training-wui"

if (-not (Test-Path -LiteralPath $WuiDir)) {
    Write-Error "training-wui directory not found: $WuiDir"
}

$null = Get-Command go -ErrorAction Stop

Push-Location $WuiDir
try {
    if ($Test) {
        Write-Host "go test -count=1 ./... (running tests; -count=1 disables cache)" -ForegroundColor DarkGray
        go test -count=1 ./...
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if ($Generate) {
        go generate ./...
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    $isWin = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
        [System.Runtime.InteropServices.OSPlatform]::Windows)
    $outName = if ($isWin) { "training-wui.exe" } else { "training-wui" }

    go build -trimpath -o $outName .
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $built = Join-Path $WuiDir $outName
    Write-Host "Built: $built"
    Write-Host "Run from repo root: & '$built' -root '$Root' -addr :8765"
} finally {
    Pop-Location
}
