# Build and run the monolithic server
$ErrorActionPreference = "Stop"

Write-Host "Building qminiwasm server..." -ForegroundColor Cyan

# Build
Push-Location $PSScriptRoot\cmd\qminiwasm
$env:CGO_ENABLED = "0"
go build -o ..\..\qminiwasm.exe .
if (-not $?) { throw "Build failed" }
Pop-Location

Write-Host "Build successful!" -ForegroundColor Green

# Kill old
Get-Process | Where-Object { $_.ProcessName -like "*qmini*" } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# Run
Write-Host "Starting server..." -ForegroundColor Cyan
$proc = Start-Process .\qminiwasm.exe -PassThru -WindowStyle Normal
Start-Sleep -Seconds 3

Write-Host "Server running on PID: $($proc.Id)" -ForegroundColor Green
Write-Host "Open http://localhost:7345 in your browser" -ForegroundColor Yellow

# Test
Write-Host "Testing..." -ForegroundColor Cyan
try {
    $resp = Invoke-WebRequest -Uri "http://localhost:7345" -UseBasicParsing -TimeoutSec 5
    if ($resp.Content -match 'window\.callTool') {
        Write-Host "SUCCESS: callTool is in the HTML!" -ForegroundColor Green -BackgroundColor Black
    } else {
        Write-Host "FAIL: callTool NOT found in HTML" -ForegroundColor Red
    }
} catch {
    Write-Host "Test error: $($_.Exception.Message)" -ForegroundColor Red
}
