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
#   .\scripts\start_cpp_training_engine.ps1 -SkipLibTorch   # no LibTorch (synthetic loss / JSON checkpoints)
#
# LibTorch: install with .\scripts\install-libtorch.ps1 (cpp\.deps\libtorch) or set LIBTORCH_ROOT.
#
param(
    [string]$Listen = '127.0.0.1:50061',
    [switch]$SkipLibTorch
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

function Test-LibTorchRoot {
    param([string]$Root)
    if ([string]::IsNullOrWhiteSpace($Root)) { return $false }
    $cfg = Join-Path $Root 'share\cmake\Torch\TorchConfig.cmake'
    return (Test-Path -LiteralPath $cfg)
}

$libTorchCMakeArgs = @()
if ($SkipLibTorch) {
    $libTorchCMakeArgs += @('-DQMINIWASM_TRAINING_WITH_LIBTORCH=OFF')
    Write-Host 'LibTorch disabled (-SkipLibTorch): native engine uses synthetic loss + JSON checkpoints.'
} else {
    $lt = $null
    if (Test-LibTorchRoot $env:LIBTORCH_ROOT) {
        $lt = $env:LIBTORCH_ROOT
        Write-Host "Using LIBTORCH_ROOT: $lt"
    } else {
        $defaultLt = Join-Path $CppDir '.deps\libtorch'
        if (Test-LibTorchRoot $defaultLt) {
            $lt = $defaultLt
            Write-Host "Using LibTorch from install script: $lt"
        }
    }
    if ($lt) {
        # find_package(Torch) needs this on the prefix path (LIBTORCH_ROOT is also read in cpp/CMakeLists.txt).
        $env:LIBTORCH_ROOT = $lt
        $libTorchCMakeArgs += @('-DCMAKE_PREFIX_PATH=' + $lt)
    } else {
        Write-Warning @'
LibTorch not found (expected cpp\.deps\libtorch after install-libtorch.ps1, or valid LIBTORCH_ROOT).
Configuring with -DQMINIWASM_TRAINING_WITH_LIBTORCH=OFF. For real TPEM weights run:
  .\scripts\install-libtorch.ps1
Then re-run this script.
'@
        $libTorchCMakeArgs += @('-DQMINIWASM_TRAINING_WITH_LIBTORCH=OFF')
    }
}

$cmakeConfigure = @(
    '-S', $CppDir,
    '-B', $BuildDir,
    '-G', $generator,
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
    '-DQMINIWASM_WITH_TRAINING_ENGINE=ON',
    '-DQMINIWASM_WITH_GRPC=ON',
    '-DQMINIWASM_BUILD_PYBIND=OFF'
) + $libTorchCMakeArgs + $toolchainArgs

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

# LibTorch DLLs load from PATH or the exe directory; official zips keep them in <libtorch>\lib.
$ltLib = $null
if (-not $SkipLibTorch -and (Test-LibTorchRoot $env:LIBTORCH_ROOT)) {
    $ltLib = Join-Path $env:LIBTORCH_ROOT 'lib'
} elseif (-not $SkipLibTorch) {
    $def = Join-Path $CppDir '.deps\libtorch'
    if (Test-LibTorchRoot $def) {
        $ltLib = Join-Path $def 'lib'
    }
}
if ($ltLib -and (Test-Path (Join-Path $ltLib 'torch_cpu.dll'))) {
    $env:PATH = "${ltLib};${env:PATH}"
    Write-Host "Prepended LibTorch to PATH: $ltLib" -ForegroundColor DarkGray
}

Write-Host "Starting $exe $Listen"
& $exe $Listen
