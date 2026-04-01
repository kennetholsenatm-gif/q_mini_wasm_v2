#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Build the Qutrit Training gRPC service.

.DESCRIPTION
    This script builds the qminiwasm_qutrit_training_server gRPC service.

.PARAMETER BuildDir
    The build directory (default: cpp/build)

.PARAMETER Clean
    Clean the build directory before building

.EXAMPLE
    .\scripts\build-qutrit-training-service.ps1
    .\scripts\build-qutrit-training-service.ps1 -Clean
#>

param(
    [string]$BuildDir = "cpp/build",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildPath = Join-Path $RepoRoot $BuildDir

# Clean if requested
if ($Clean -and (Test-Path $BuildPath)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildPath
}

Write-Host "Configuring qminiwasm with qutrit training service..." -ForegroundColor Yellow

$CmakeArgs = @(
    "-S", (Join-Path $RepoRoot "cpp")
    "-B", $BuildPath
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

Write-Host "Building qminiwasm_qutrit_training_server..." -ForegroundColor Cyan
& cmake --build $BuildPath --config Release --target qminiwasm_qutrit_training_server
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    exit 1
}

Write-Host "Build completed successfully!" -ForegroundColor Green
Write-Host "Executable: $BuildPath/qminiwasm_qutrit_training_server.exe" -ForegroundColor Cyan
