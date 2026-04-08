#!/usr/bin/env pwsh
# Test script for desktop .exe

Write-Host "=== Testing qminiwasm_wui Desktop .exe ===" -ForegroundColor Green

# Check process
$proc = Get-Process qminiwasm_wui_test -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "✓ Desktop .exe is running (PID: $($proc.Id), RAM: $([math]::Round($proc.WorkingSet/1MB,2)) MB)" -ForegroundColor Green
} else {
    Write-Host "✗ Desktop .exe is NOT running" -ForegroundColor Red
    exit 1
}

# Check extracted assets
$wuiPath = "c:\GitHub\q_mini_wasm_v2\releases\desktop\extracted-assets\wui"
if (Test-Path "$wuiPath\training.html") {
    Write-Host "✓ training.html exists ($((Get-Item "$wuiPath\training.html").Length) bytes)" -ForegroundColor Green
} else {
    Write-Host "✗ training.html NOT found" -ForegroundColor Red
}

if (Test-Path "$wuiPath\js\training-sse-client.js") {
    Write-Host "✓ training-sse-client.js exists" -ForegroundColor Green
} else {
    Write-Host "✗ training-sse-client.js NOT found" -ForegroundColor Red
}

# Check MCP bridge binary
$bridgePath = "c:\GitHub\q_mini_wasm_v2\releases\desktop\extracted-assets\runtime\native\wui-cli-bridge.exe"
if (Test-Path $bridgePath) {
    Write-Host "✓ MCP bridge exists ($((Get-Item $bridgePath).Length) bytes)" -ForegroundColor Green
} else {
    Write-Host "✗ MCP bridge NOT found" -ForegroundColor Red
}

# Check trainer binary  
$trainerPath = "c:\GitHub\q_mini_wasm_v2\releases\desktop\extracted-assets\runtime\native\q_mini_wasm_v2_trainer.exe"
if (Test-Path $trainerPath) {
    Write-Host "✓ Trainer exists ($((Get-Item $trainerPath).Length) bytes)" -ForegroundColor Green
} else {
    Write-Host "✗ Trainer NOT found" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Desktop .exe Test Complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "To use the WUI Training features:"
Write-Host "1. Open Chrome/Edge and navigate to:" 
Write-Host "   file:///C:/GitHub/q_mini_wasm_v2/releases/desktop/extracted-assets/wui/index.html"
Write-Host "2. Click 'Training' tab"
Write-Host "3. Test API key storage and training controls"
Write-Host ""
Write-Host "Note: SSE server (port 9090) starts automatically when training begins."
