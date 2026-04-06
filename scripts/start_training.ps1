#!/usr/bin/env powershell
# Start Training Script for q_mini_wasm_v2
# This script initializes and starts the autonomous training pipeline

$ErrorActionPreference = "Stop"

$repoRoot = "c:\GitHub\q_mini_wasm_v2"
$mcpPath = "$repoRoot\agents\cmd\wui-cli-bridge\wui-mcp.exe"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "q_mini_wasm_v2 Training Pipeline Starter" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# Check if MCP bridge exists
if (-not (Test-Path $mcpPath)) {
    Write-Host "Building MCP bridge..." -ForegroundColor Yellow
    Set-Location "$repoRoot\agents\cmd\wui-cli-bridge"
    go build -o wui-mcp.exe .
    if (-not (Test-Path $mcpPath)) {
        Write-Error "Failed to build MCP bridge"
        exit 1
    }
}

Write-Host "`nStarting MCP bridge..." -ForegroundColor Green
Write-Host "Send the following JSON-RPC commands to start training:" -ForegroundColor Yellow
Write-Host "`n# 1. Initialize the pipeline:" -ForegroundColor Cyan
Write-Host '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"wui_init_training_pipeline","arguments":{"num_experts":243,"graph_nodes":64,"enable_betti_guidance":true,"epochs":1000}}}' -ForegroundColor White

Write-Host "`n# 2. Start training:" -ForegroundColor Cyan  
Write-Host '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"wui_start_ff_training","arguments":{"layers":[0,1,2],"epochs":1000}}}' -ForegroundColor White

Write-Host "`n# 3. Check metrics:" -ForegroundColor Cyan
Write-Host '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"wui_get_training_metrics","arguments":{}}}' -ForegroundColor White

Write-Host "`n==========================================" -ForegroundColor Cyan
Write-Host "MCP bridge starting... (Ctrl+C to stop)" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# Start the MCP bridge
Set-Location "$repoRoot\agents\cmd\wui-cli-bridge"
& $mcpPath
