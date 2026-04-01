#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Start the Qutrit Training gRPC service.

.DESCRIPTION
    This script starts the qminiwasm_qutrit_training_server gRPC service
    for the Qutrit Clifford training paradigm.

.PARAMETER Address
    The address to listen on (default: 0.0.0.0:50053)

.PARAMETER BuildDir
    The build directory (default: cpp/build)

.PARAMETER Build
    Build the service before starting

.EXAMPLE
    .\scripts\start-qutrit-training-service.ps1
    .\scripts\start-qutrit-training-service.ps1 -Address "0.0.0.0:50054"
    .\scripts\start-qutrit-training-service.ps1 -Build
#>

param(
    [string]$Address = "0.0.0.0:50053",
    [string]$BuildDir = "cpp/build",
    [switch]$Build
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ServerExe = Join-Path $RepoRoot $BuildDir "qminiwasm_qutrit_training_server.exe"

# Check if we need to build
if ($Build -or -not (Test-Path $ServerExe)) {
    Write-Host "Building qutrit training service..." -ForegroundColor Yellow
    
    $CmakeArgs = @(
        "-S", (Join-Path $RepoRoot "cpp")
        "-B", (Join-Path $RepoRoot $BuildDir)
        "-DCMAKE_BUILD_TYPE=Release"
        "-DQMINIWASM_WITH_QUANTUM=ON"
        "-DQMINIWASM_WITH_QUTRIT_TRAINING_GRPC=ON"
        "-DQMINIWASM_BUILD_PYBIND=OFF"
    )
    
    Write-Host "Running cmake configure..." -ForegroundColor Cyan
    & cmake @CmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configure failed"
        exit 1
    }
    
    Write-Host "Building..." -ForegroundColor Cyan
    & cmake --build (Join-Path $RepoRoot $BuildDir) --config Release --target qminiwasm_qutrit_training_server
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed"
        exit 1
    }
}

# Check if server executable exists
if (-not (Test-Path $ServerExe)) {
    Write-Error "Server executable not found: $ServerExe"
    Write-Host "Try running with -Build flag to build first" -ForegroundColor Yellow
    exit 1
}

Write-Host "Starting Qutrit Training Service on $Address..." -ForegroundColor Green
Write-Host "Press Ctrl+C to stop" -ForegroundColor Yellow
Write-Host ""

& $ServerExe $Address
