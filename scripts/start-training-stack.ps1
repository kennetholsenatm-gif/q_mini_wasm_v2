# Start the C++ gRPC training engine and the Training WUI from repo root.
# If the server binary is missing, runs CMake configure + build (requires cmake, toolchain, gRPC deps).
# Usage: .\scripts\start-training-stack.ps1 [extra args for training-wui]
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent

# Run a native executable and return its exit code. Do not use `& exe @args` + return $LASTEXITCODE here:
# PowerShell captures native stdout as pipeline output, so callers like `$code = Invoke-Native` would get
# CMake's first log line instead of the exit code. Start-Process -Wait -PassThru uses .ExitCode reliably.
function Invoke-Native {
    param(
        [string] $FilePath,
        [string[]] $ArgumentList
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $p = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -NoNewWindow -Wait -PassThru
        if ($null -eq $p) {
            return -1
        }
        return [int]$p.ExitCode
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Get-CppTrainingServerExe {
    param([string] $RepoRoot)
    $candidates = @(
        (Join-Path $RepoRoot "cpp\build\Release\qminiwasm_training_engine_server.exe")
        (Join-Path $RepoRoot "cpp\build\Debug\qminiwasm_training_engine_server.exe")
        (Join-Path $RepoRoot "cpp\build\RelWithDebInfo\qminiwasm_training_engine_server.exe")
        (Join-Path $RepoRoot "cpp\build\qminiwasm_training_engine_server.exe")
        (Join-Path $RepoRoot "cpp\build\qminiwasm_training_engine_server")
    )
    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) {
            return $p
        }
    }
    return $null
}

function Find-CMakeExecutable {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    $pf = $env:ProgramFiles
    if ([string]::IsNullOrEmpty($pf)) { $pf = "C:\Program Files" }
    $pf86 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrEmpty($pf86)) { $pf86 = "C:\Program Files (x86)" }
    foreach ($p in @(
            (Join-Path $pf "CMake\bin\cmake.exe")
            (Join-Path $pf86 "CMake\bin\cmake.exe")
            (Join-Path $env:ProgramData "chocolatey\bin\cmake.exe")
        )) {
        if ($p -and (Test-Path -LiteralPath $p)) {
            return $p
        }
    }
    $vswhere = Join-Path $pf86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $fromVs = & $vswhere -latest -products * -find "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" 2>$null |
            Select-Object -First 1
        if ($fromVs -and (Test-Path -LiteralPath $fromVs)) {
            return $fromVs
        }
        $inst = & $vswhere -latest -products * -property installationPath 2>$null | Select-Object -First 1
        if ($inst) {
            $bundled = Join-Path $inst "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $bundled) {
                return $bundled
            }
        }
    }
    return $null
}

function Find-VcpkgToolchainFile {
    $rel = "scripts\buildsystems\vcpkg.cmake"
    $candidates = @()
    if ($env:VCPKG_ROOT) {
        $candidates += (Join-Path $env:VCPKG_ROOT $rel)
    }
    foreach ($base in @(
            (Join-Path $env:USERPROFILE "vcpkg")
            "C:\vcpkg"
            "C:\dev\vcpkg"
            "C:\src\vcpkg"
            (Join-Path $env:USERPROFILE "source\repos\vcpkg")
        )) {
        if ($base) {
            $candidates += (Join-Path $base $rel)
        }
    }
    foreach ($p in $candidates) {
        if ($p -and (Test-Path -LiteralPath $p)) {
            return $p
        }
    }
    return $null
}

function Build-CppTrainingServer {
    param([string] $RepoRoot)
    $cmakeExe = Find-CMakeExecutable
    if (-not $cmakeExe) {
        Write-Host "CMake not found (not on PATH and not under Program Files or Visual Studio). Add CMake to PATH or install from https://cmake.org/download/ - see cpp/training/README.md" -ForegroundColor Yellow
        return $false
    }
    Write-Host "Using CMake: $cmakeExe" -ForegroundColor DarkGray
    $cppDir = Join-Path $RepoRoot "cpp"
    $buildDir = Join-Path $cppDir "build"
    if (-not (Test-Path -LiteralPath $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    }
    Write-Host "Configuring C++ build in $buildDir ..." -ForegroundColor Cyan
    $vcpkgToolchain = Find-VcpkgToolchainFile
    $configureArgs = @(
        "-S", $cppDir,
        "-B", $buildDir,
        "-DQMINIWASM_WITH_TRAINING_ENGINE=ON",
        "-DQMINIWASM_WITH_GRPC=ON",
        "-DQMINIWASM_BUILD_PYBIND=OFF"
    )
    if ($vcpkgToolchain) {
        $tcNorm = $vcpkgToolchain -replace "\\", "/"
        Write-Host "Using vcpkg toolchain: $tcNorm" -ForegroundColor DarkGray
        $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$tcNorm"
    } else {
        Write-Host "vcpkg toolchain not found (set VCPKG_ROOT or clone vcpkg to `$HOME\vcpkg). CMake will not see gRPC/Protobuf." -ForegroundColor Yellow
        Write-Host "  Quick install: git clone https://github.com/microsoft/vcpkg `$HOME\vcpkg ; & `$HOME\vcpkg\bootstrap-vcpkg.bat ; `$env:VCPKG_ROOT=`"`$HOME\vcpkg`"" -ForegroundColor Yellow
    }
    $code = Invoke-Native -FilePath $cmakeExe -ArgumentList $configureArgs
    if ($code -ne 0) {
        Write-Host "CMake configure failed (exit $code). Install vcpkg, set VCPKG_ROOT, then re-run (or wipe cpp\build if you just installed vcpkg). Details: cpp/training/README.md" -ForegroundColor Red
        return $false
    }
    Write-Host "Building qminiwasm_training_engine_server ..." -ForegroundColor Cyan
    $buildArgs = @("--build", $buildDir, "--target", "qminiwasm_training_engine_server", "--parallel")
    # Multi-config generators (Visual Studio) need an explicit configuration.
    $genFile = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path -LiteralPath $genFile) {
        $cache = Get-Content -LiteralPath $genFile -Raw
        if ($cache -match "CMAKE_GENERATOR:INTERNAL=(.+)") {
            $gen = $Matches[1]
            if ($gen -match "Visual Studio") {
                $buildArgs += @("--config", "Release")
            }
        }
    }
    $code = Invoke-Native -FilePath $cmakeExe -ArgumentList $buildArgs
    if ($code -ne 0) {
        Write-Host "CMake build failed (exit $code)." -ForegroundColor Red
        return $false
    }
    return $true
}

$CppExe = Get-CppTrainingServerExe -RepoRoot $Root
if (-not $CppExe) {
    if (Build-CppTrainingServer -RepoRoot $Root) {
        $CppExe = Get-CppTrainingServerExe -RepoRoot $Root
    }
}

function Get-LibTorchBinForPath {
    param([string] $RepoRoot)
    $candidates = @()
    if ($env:LIBTORCH_ROOT) {
        $candidates += (Join-Path $env:LIBTORCH_ROOT "lib")
    }
    $candidates += (Join-Path $RepoRoot "cpp\.deps\libtorch\lib")
    foreach ($dir in $candidates) {
        if ($dir -and (Test-Path (Join-Path $dir "torch_cpu.dll"))) {
            return $dir
        }
    }
    return $null
}

$cppJob = $null
if ($CppExe) {
    $ltLib = Get-LibTorchBinForPath -RepoRoot $Root
    if ($ltLib) {
        $env:PATH = "${ltLib};${env:PATH}"
        Write-Host "Prepended LibTorch lib to PATH for training server: $ltLib" -ForegroundColor DarkGray
    } else {
        Write-Host "LibTorch lib not found on PATH hint (torch_cpu.dll). If the server exits with a missing DLL, set LIBTORCH_ROOT or run scripts\install-libtorch.ps1, or rebuild so CMake copies DLLs next to the exe." -ForegroundColor Yellow
    }
    $cppJob = Start-Process -FilePath $CppExe -ArgumentList "127.0.0.1:50061" -PassThru -WindowStyle Hidden
    Write-Host "Started C++ TrainingEngineService (pid $($cppJob.Id)) on 127.0.0.1:50061"
} else {
    Write-Host "C++ training server binary not available after build attempt; starting WUI only (Python training / preflight still work)." -ForegroundColor Yellow
}

try {
    Set-Location -LiteralPath (Join-Path $Root "training-wui")
    $goArgs = @("run", ".", "-root", $Root) + @($args)
    $goExe = (Get-Command go -ErrorAction Stop).Source
    $goCode = Invoke-Native -FilePath $goExe -ArgumentList $goArgs
    if ($goCode -ne 0) {
        exit $goCode
    }
} finally {
    if ($null -ne $cppJob -and -not $cppJob.HasExited) {
        Stop-Process -Id $cppJob.Id -Force -ErrorAction SilentlyContinue
    }
}
