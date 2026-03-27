# Build and run qminiwasm_training_engine_server (C++ gRPC training backend) on Windows.
#
# Prerequisites:
#   - CMake >= 3.20, MSVC or Clang (C++23), Ninja recommended: cmake -G Ninja
#   - Protobuf + gRPC (vcpkg is the path of least resistance):
#       $env:VCPKG_ROOT = 'C:\path\to\vcpkg'
#       & "$env:VCPKG_ROOT\vcpkg.exe" install grpc:x64-windows protobuf:x64-windows
#
# Usage:
#   .\scripts\start_cpp_training_engine.ps1
#   .\scripts\start_cpp_training_engine.ps1 -Listen '0.0.0.0:50061'
#
param(
    [string]$Listen = '127.0.0.1:50061'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$CppDir = Join-Path $RepoRoot 'cpp'
$BuildDir = Join-Path $CppDir 'build-training-engine'

$toolchainArgs = @()
if ($env:CMAKE_TOOLCHAIN_FILE) {
    $toolchainArgs += @('-DCMAKE_TOOLCHAIN_FILE=' + $env:CMAKE_TOOLCHAIN_FILE)
}
elseif ($env:VCPKG_ROOT) {
    $tc = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'
    if (Test-Path $tc) {
        $toolchainArgs += @('-DCMAKE_TOOLCHAIN_FILE=' + $tc)
    }
}

# Prefer Ninja if available (single-config output path is predictable).
$generator = 'Ninja'
$vsBuildConfig = @()
try {
    $null = Get-Command ninja -ErrorAction Stop
} catch {
    $vsGen = 'Visual Studio 17 2022'
    $vsInst = $null
    $c18 = "${env:ProgramFiles}\Microsoft Visual Studio\18\Community"
    $c22 = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community"
    if (Test-Path $c18) {
        $vsGen = 'Visual Studio 18 2026'
        $vsInst = $c18
    } elseif (Test-Path $c22) {
        $vsInst = $c22
    }
    $generator = $vsGen
    $toolchainArgs += @('-A', 'x64')
    if ($vsInst) {
        $toolchainArgs += @('-DCMAKE_GENERATOR_INSTANCE=' + $vsInst)
    }
    $vsBuildConfig = @('--config', 'Release')
}

$cmakeExe = 'cmake'
try {
    $null = Get-Command cmake -ErrorAction Stop
} catch {
    $bundled = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    $bundled18 = "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $bundled18) {
        $cmakeExe = $bundled18
    } elseif (Test-Path $bundled) {
        $cmakeExe = $bundled
    } else {
        throw 'cmake not on PATH and bundled VS CMake not found — install CMake or Visual Studio with C++ workload.'
    }
}

$cmakeConfigure = @(
    '-S', $CppDir,
    '-B', $BuildDir,
    '-G', $generator,
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    '-DQMINIWASM_WITH_TRAINING_ENGINE=ON',
    '-DQMINIWASM_WITH_GRPC=ON'
) + $toolchainArgs

if ($generator -eq 'Ninja') {
    $cmakeConfigure += @('-DCMAKE_BUILD_TYPE=Release')
}

Write-Host ($cmakeExe + ' ' + ($cmakeConfigure -join ' '))
& $cmakeExe @cmakeConfigure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmakeExe --build $BuildDir @vsBuildConfig --target qminiwasm_training_engine_server --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$candidates = @(
    (Join-Path $BuildDir 'qminiwasm_training_engine_server.exe'),
    (Join-Path $BuildDir 'Release\qminiwasm_training_engine_server.exe'),
    (Join-Path $BuildDir 'Debug\qminiwasm_training_engine_server.exe')
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "Could not find qminiwasm_training_engine_server.exe under $BuildDir"
}

Write-Host "Starting $exe $Listen"
& $exe $Listen
