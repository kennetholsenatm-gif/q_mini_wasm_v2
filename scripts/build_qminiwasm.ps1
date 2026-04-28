# Build the canonical WUI host: <repo>/qminiwasm.exe (see config/path_map.toml).
# This script ALWAYS performs the full SYCL-enabled build and runtime DLL deployment.
# Run:
#   powershell -NoProfile -File .\scripts\build_qminiwasm.ps1

$ErrorActionPreference = "Stop"

function Find-CommandPath {
    param([string]$Name)
    try {
        $cmd = Get-Command $Name -ErrorAction Stop
        return $cmd.Source
    } catch {
        return $null
    }
}

function Import-BatchEnvironment {
    param([string]$BatchPath, [string]$Args = "")
    if (-not (Test-Path $BatchPath)) {
        return $false
    }
    $argSuffix = if ($Args) { " $Args" } else { "" }
    $envDump = & cmd /c "`"$BatchPath`"$argSuffix >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0 -or -not $envDump) {
        return $false
    }
    foreach ($line in $envDump) {
        if ($line -match "^(.*?)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
    return $true
}

function Copy-IfExists {
    param([string]$SourcePath, [string]$DestinationDir)
    if (-not (Test-Path $SourcePath)) {
        return $false
    }
    $leaf = Split-Path $SourcePath -Leaf
    $destPath = Join-Path $DestinationDir $leaf
    $srcFull = (Resolve-Path $SourcePath).Path
    if (Test-Path $destPath) {
        $dstFull = (Resolve-Path $destPath).Path
        if ($srcFull -ieq $dstFull) {
            return $false
        }
    }
    Copy-Item -Path $SourcePath -Destination $destPath -Force
    return $true
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "qminiwasm.exe"
Set-Location $repoRoot

$nativeRoot = Join-Path $repoRoot "q_mini_wasm_v2"
$syclBuildDir = "build_final_sycl"

Write-Host "[build] Repo root: $repoRoot" -ForegroundColor Cyan

$setvarsPath = "C:\Program Files (x86)\Intel\oneAPI\setvars.bat"
if (-not (Import-BatchEnvironment $setvarsPath "intel64")) {
    throw "[build] Missing oneAPI environment: $setvarsPath"
}
Write-Host "[build] Loaded oneAPI setvars.bat" -ForegroundColor Green

$vcvars64Candidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars64 = $vcvars64Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars64) {
    throw "[build] Missing Visual Studio vcvars64.bat"
}
if (-not (Import-BatchEnvironment $vcvars64)) {
    throw "[build] Failed to load Visual Studio environment: $vcvars64"
}
Write-Host "[build] Loaded Visual Studio vcvars64" -ForegroundColor Green

$oneApiCompilerLibDirs = @(
    "C:\Program Files (x86)\Intel\oneAPI\compiler\latest\lib",
    "C:\Program Files (x86)\Intel\oneAPI\compiler\2025.1\lib"
) | Where-Object { Test-Path $_ }
foreach ($libDir in $oneApiCompilerLibDirs) {
    if (-not ($env:LIB -split ";" | Where-Object { $_ -eq $libDir })) {
        $env:LIB = "$libDir;$env:LIB"
    }
}

$dpcpp = Find-CommandPath "dpcpp"
$dpcppCl = Find-CommandPath "dpcpp-cl"
$icxCl = Find-CommandPath "icx-cl"
$icpx = Find-CommandPath "icpx"

if (-not $dpcppCl) {
    $dpcppCl = Get-ChildItem "C:\Program Files (x86)\Intel\oneAPI\compiler" -Recurse -Filter "dpcpp-cl.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $icxCl) {
    $icxCl = Get-ChildItem "C:\Program Files (x86)\Intel\oneAPI\compiler" -Recurse -Filter "icx-cl.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $icpx) {
    $candidateIcpx = Get-ChildItem "C:\Program Files (x86)\Intel\oneAPI\compiler" -Recurse -Filter "icpx.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if ($candidateIcpx) {
        $icpx = $candidateIcpx
        Write-Host "[build -Sycl] Found icpx: $icpx" -ForegroundColor Green
    }
}

if (-not $icxCl) {
    throw "[build] Intel SYCL compiler not found: icx-cl.exe"
}
if ($dpcpp) { Write-Host "[build] dpcpp: $dpcpp" -ForegroundColor Green }
if ($dpcppCl) { Write-Host "[build] dpcpp-cl: $dpcppCl" -ForegroundColor Green }
Write-Host "[build] icx-cl: $icxCl" -ForegroundColor Green
if ($icpx) { Write-Host "[build] icpx: $icpx" -ForegroundColor Green }

Set-Location $nativeRoot
# Single-config generators (Ninja): default is Debug, which enables MSVC debug heap + extra
# abort()/assert paths and the "Debug Error!" dialog; always ship Release for the WUI host.
Write-Host "[build] cmake -DUSE_SYCL=ON -DCMAKE_BUILD_TYPE=Release" -ForegroundColor Cyan
$cmakeArgs = @(
    "-S", ".", "-B", $syclBuildDir,
    "-DUSE_SYCL=ON",
    "-DCMAKE_BUILD_TYPE=Release"
)
if (Test-Path $syclBuildDir) {
    Remove-Item -Recurse -Force $syclBuildDir
}
$ninja = Find-CommandPath "ninja"
if (-not $ninja) {
    $ninjaCandidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    )
    $ninja = $ninjaCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $ninja) {
    throw "[build] Ninja not found"
}
$preferredIntelCompiler = $icxCl
if (-not $preferredIntelCompiler) {
    throw "[build] No Intel compiler path available"
}
$cmakeArgs += "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_CXX_COMPILER=$preferredIntelCompiler"
$cmakeArgs += "-DCMAKE_CXX_FLAGS=-fsycl", "-DCMAKE_SHARED_LINKER_FLAGS=-fsycl", "-DCMAKE_EXE_LINKER_FLAGS=-fsycl"
Write-Host "[build] Ninja: $ninja" -ForegroundColor Green
Write-Host "[build] CXX: $preferredIntelCompiler (-fsycl)" -ForegroundColor Cyan

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[build] cmake --build q_training" -ForegroundColor Cyan
& cmake --build $syclBuildDir --target q_training -j 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Set-Location $repoRoot
Write-Host "[build] go build -o $exe ./cmd/qminiwasm" -ForegroundColor Cyan
& go build -o $exe ./cmd/qminiwasm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[build] Deploy Intel runtime DLLs next to $exe" -ForegroundColor Cyan
$copiedCount = 0
$trainingDllCandidates = @(
    (Join-Path $repoRoot "native_runtime\q_training.dll")
)
foreach ($dll in $trainingDllCandidates) {
    if (Copy-IfExists $dll $repoRoot) { $copiedCount++ }
}

$oneApiBinCandidates = @(
    "C:\Program Files (x86)\Intel\oneAPI\compiler\latest\bin",
    "C:\Program Files (x86)\Intel\oneAPI\compiler\2025.1\bin",
    "C:\Program Files (x86)\Intel\oneAPI\tbb\latest\bin"
) | Where-Object { Test-Path $_ }

$runtimeDllNames = @(
    "libmmd.dll",
    "libmmdd.dll",
    "libiomp5md.dll",
    "svml_dispmd.dll",
    "sycl8.dll",
    "sycl8-preview.dll",
    "ur_loader.dll",
    "ur_adapter_level_zero.dll",
    "ur_adapter_opencl.dll",
    "ur_win_proxy_loader.dll",
    "omptarget.dll",
    "omptarget.rtl.level0.dll",
    "omptarget.rtl.unified_runtime.dll",
    "tbb12.dll",
    "tbbmalloc.dll"
)
$criticalDllNames = @(
    "q_training.dll",
    "libmmd.dll",
    "libiomp5md.dll",
    "sycl8.dll",
    "ur_loader.dll"
)
foreach ($dllName in $runtimeDllNames) {
    foreach ($binDir in $oneApiBinCandidates) {
        $src = Join-Path $binDir $dllName
        if (Copy-IfExists $src $repoRoot) {
            $copiedCount++
            break
        }
    }
}
foreach ($criticalName in $criticalDllNames) {
    if (-not (Test-Path (Join-Path $repoRoot $criticalName))) {
        throw "[build] Missing required runtime DLL after copy: $criticalName"
    }
}
Write-Host "[build] Runtime DLL copies (new/changed): $copiedCount" -ForegroundColor Green

Write-Host "[build] OK: $exe" -ForegroundColor Green
Write-Host '[build] Logs: SYCL default device, [Pipeline] router_sycl=1' -ForegroundColor Cyan
