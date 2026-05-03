# Build the canonical WUI host: <repo>/qminiwasm.exe (see config/path_map.toml).
#
# Canonical workflow (same folder must contain qminiwasm.exe + published q_training.dll):
#   Build: powershell -NoProfile -File C:\GitHub\q_mini_wasm_v2\scripts\build_qminiwasm.ps1
#   Run:   C:\GitHub\q_mini_wasm_v2\qminiwasm.exe
#
# Phases (do not confuse compile with packaging):
#   1) Configure + link q_training (SYCL) — the actual build.
#   2) Publish q_training.dll — PowerShell copy + SHA256 verify (repo root + native_runtime/ for CGO).
#   3) Publish vcpkg curl peers — cmake -P post_copy_vcpkg_curl_runtime_dlls.cmake when VCPKG_ROOT (or default tree) has libcurl.
#   4) go build qminiwasm.exe
#   5) Copy Intel oneAPI runtime DLLs next to the exe (same dirs as q_training for load order).
#
# Run:
#   powershell -NoProfile -File .\scripts\build_qminiwasm.ps1

$ErrorActionPreference = "Stop"
# PS 7+: CMake prints warnings to stderr; avoid treating them as red "NativeCommandError" noise in the host.
if ($PSVersionTable.PSVersion.Major -ge 7) {
    $PSNativeCommandUseErrorActionPreference = $false
}

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

function Find-BuiltTrainingDll {
    param([string]$BuildTreeRoot)
    if (-not (Test-Path -LiteralPath $BuildTreeRoot)) {
        return $null
    }
    $hit = Get-ChildItem -LiteralPath $BuildTreeRoot -Filter "q_training.dll" -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

function Invoke-CMakePublishScript {
    param(
        [string]$CmakeExe,
        [string[]]$DefineArgs,
        [string]$ScriptPath
    )
    if (-not (Test-Path -LiteralPath $ScriptPath)) {
        throw "[build] Missing CMake script: $ScriptPath"
    }
    $allArgs = @()
    foreach ($d in $DefineArgs) { $allArgs += $d }
    $allArgs += "-P", $ScriptPath
    & $CmakeExe @allArgs
    if ($LASTEXITCODE -ne 0) {
        throw "[build] cmake -P failed: $ScriptPath (exit $LASTEXITCODE)"
    }
}

function Publish-QTrainingDll {
    <#
      Copy built q_training.dll to native_runtime then repo root; verify SHA256 matches build tree output.
      CMake copy_if_different can appear to succeed while leaving an old locked file — this fails loud.
    #>
    param(
        [Parameter(Mandatory = $true)][string]$SourceDll,
        [Parameter(Mandatory = $true)][string]$DestinationPrimary,
        [Parameter(Mandatory = $true)][string]$DestinationAlternate
    )
    if (-not (Test-Path -LiteralPath $SourceDll)) {
        throw "[build] Publish-QTrainingDll: source missing: $SourceDll"
    }
    $altDir = Split-Path -Parent $DestinationAlternate
    New-Item -ItemType Directory -Force -Path $altDir | Out-Null
    Copy-Item -LiteralPath $SourceDll -Destination $DestinationAlternate -Force
    $hashSrc = (Get-FileHash -LiteralPath $SourceDll -Algorithm SHA256).Hash
    $hashAlt = (Get-FileHash -LiteralPath $DestinationAlternate -Algorithm SHA256).Hash
    if ($hashSrc -ne $hashAlt) {
        throw "[build] native_runtime q_training.dll hash mismatch after copy (internal error)."
    }
    try {
        Copy-Item -LiteralPath $SourceDll -Destination $DestinationPrimary -Force -ErrorAction Stop
    } catch {
        $msg = $_.Exception.Message
        if (Test-Path -LiteralPath $DestinationPrimary) {
            $hashOld = (Get-FileHash -LiteralPath $DestinationPrimary -Algorithm SHA256).Hash
            if ($hashOld -eq $hashSrc) {
                Write-Host "[build] Repo-root q_training.dll already matches build output (ok)." -ForegroundColor Green
                return
            }
            throw "[build] Cannot update repo-root q_training.dll (likely file locked). Close qminiwasm.exe (or any host loading it) and re-run. Fresh DLL is at: $DestinationAlternate. Details: $msg"
        }
        throw "[build] Cannot write repo-root q_training.dll: $msg"
    }
    $hashPri = (Get-FileHash -LiteralPath $DestinationPrimary -Algorithm SHA256).Hash
    if ($hashPri -ne $hashSrc) {
        throw "[build] Repo-root q_training.dll exists but does not match build output (hash mismatch)."
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "qminiwasm.exe"
Write-Host "[build] PSScriptRoot=$PSScriptRoot" -ForegroundColor DarkGray
Write-Host "[build] Resolved repo root (outputs): $repoRoot" -ForegroundColor Cyan
Write-Host "[build] qminiwasm.exe path: $exe" -ForegroundColor DarkGray
Set-Location $repoRoot

$nativeRoot = Join-Path $repoRoot "q_mini_wasm_v2"
$syclBuildDir = "build_sycl"

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
$cmakeExe = Find-CommandPath "cmake"
if (-not $cmakeExe) {
    throw "[build] cmake not on PATH (needed for configure, publish, and vcpkg curl copy)"
}
if ($dpcpp) { Write-Host "[build] dpcpp: $dpcpp" -ForegroundColor Green }
if ($dpcppCl) { Write-Host "[build] dpcpp-cl: $dpcppCl" -ForegroundColor Green }
Write-Host "[build] icx-cl: $icxCl" -ForegroundColor Green
if ($icpx) { Write-Host "[build] icpx: $icpx" -ForegroundColor Green }

Set-Location $nativeRoot
# Single-config generators (Ninja): default is Debug, which enables MSVC debug heap + extra
# abort()/assert paths and the "Debug Error!" dialog; always ship Release for the WUI host.
Write-Host "[build] cmake (SYCL mandatory) -DCMAKE_BUILD_TYPE=Release" -ForegroundColor Cyan
# Never pass -DUSE_SYCL=OFF: q_mini_wasm_v2 CMakeLists.txt fatals; q_training requires USE_SYCL=1.
$cmakeArgs = @(
    "-S", ".", "-B", $syclBuildDir,
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

& $cmakeExe @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# --- Phase 1: link training DLL (build) ---
Write-Host "[build] Phase 1: cmake --build q_training (SYCL link)" -ForegroundColor Cyan
& $cmakeExe --build $syclBuildDir --target q_training -j 8
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# --- Phase 2: publish q_training.dll (layout / CGO; not part of compile) ---
# Same logic as CMake POST_BUILD, invoked here so publish always runs after a successful link.
$buildTree = Join-Path $nativeRoot $syclBuildDir
$builtDll = Find-BuiltTrainingDll $buildTree
if (-not $builtDll) {
    throw "[build] Built q_training.dll not found under $buildTree (recursive search)"
}
$dstPrimary = Join-Path $repoRoot "q_training.dll"
$nrDest = Join-Path $repoRoot "native_runtime"
New-Item -ItemType Directory -Force -Path $nrDest | Out-Null
$dstAlt = Join-Path $nrDest "q_training.dll"
Write-Host "[build] Phase 2: publish q_training.dll (built: $builtDll)" -ForegroundColor Cyan
Publish-QTrainingDll -SourceDll $builtDll -DestinationPrimary $dstPrimary -DestinationAlternate $dstAlt
Write-Host "[build] Published + verified q_training.dll -> $dstPrimary ; $dstAlt" -ForegroundColor Green

# --- Phase 3: vcpkg curl + peers beside q_training (runtime deps; optional) ---
function Test-VcpkgLibCurlBin {
    param([string]$Root)
    if (-not $Root) { return $false }
    $r = $Root.TrimEnd(@('\', '/'))
    return (Test-Path -LiteralPath (Join-Path $r "installed\x64-windows\bin\libcurl.dll"))
}

$vcpkgCandidates = @()
if ($env:VCPKG_ROOT) {
    $vcpkgCandidates += $env:VCPKG_ROOT.TrimEnd(@('\', '/'))
}
$vcpkgCandidates += @(
    "C:\vcpkg",
    "C:\tools\vcpkg",
    (Join-Path $env:USERPROFILE "vcpkg"),
    (Join-Path $env:USERPROFILE "source\repos\vcpkg")
)
$vcpkgRoot = $null
foreach ($cand in $vcpkgCandidates) {
    if ($cand -and (Test-VcpkgLibCurlBin $cand)) {
        $vcpkgRoot = $cand.TrimEnd(@('\', '/'))
        break
    }
}

$curlScript = Join-Path $nativeRoot "cmake\post_copy_vcpkg_curl_runtime_dlls.cmake"
if ($vcpkgRoot -and -not (Test-Path -LiteralPath $curlScript)) {
    Write-Host "[build] WARN: vcpkg copy script missing: $curlScript" -ForegroundColor Yellow
}
if ($vcpkgRoot -and (Test-Path -LiteralPath $curlScript)) {
    Write-Host "[build] Phase 3: cmake -P post_copy_vcpkg_curl_runtime_dlls.cmake (VCPKG_ROOT=$vcpkgRoot)" -ForegroundColor Cyan
    Invoke-CMakePublishScript $cmakeExe @(
        "-DVCPKG_ROOT=$vcpkgRoot",
        "-DCONFIG=Release",
        "-DDST_PRIMARY_DIR=$repoRoot",
        "-DDST_ALT_DIR=$nrDest"
    ) $curlScript
    $libcurlPrimary = Join-Path $repoRoot "libcurl.dll"
    $libcurlAlt = Join-Path $nrDest "libcurl.dll"
    if ((Test-Path -LiteralPath $libcurlPrimary) -and (Test-Path -LiteralPath $libcurlAlt)) {
        Write-Host "[build] vcpkg curl runtime DLLs beside q_training.dll (repo + native_runtime)" -ForegroundColor Green
    } else {
        Write-Host "[build] WARN: libcurl.dll not present in both repo root and native_runtime after Phase 3 (HTTP synthesis may fail). Install: vcpkg install curl:x64-windows, or use a VCPKG_ROOT tree that contains installed\x64-windows\bin\libcurl.dll" -ForegroundColor Yellow
    }
} else {
    Write-Host "[build] WARN: No vcpkg installation with curl:x64-windows found (skip Phase 3). Set VCPKG_ROOT to a tree containing installed\x64-windows\bin\libcurl.dll" -ForegroundColor Yellow
}

# --- Phase 4: Go host ---
Set-Location $repoRoot
$goExe = Find-CommandPath "go"
if (-not $goExe) {
    throw "[build] go not on PATH (install Go or open a Developer shell where go is available)."
}
Write-Host "[build] Phase 4: go build -o `"$exe`" ./cmd/qminiwasm (go=$goExe)" -ForegroundColor Cyan
& go build -o $exe ./cmd/qminiwasm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not (Test-Path -LiteralPath $exe)) {
    throw "[build] go build reported success but exe missing: $exe"
}
$exeItem = Get-Item -LiteralPath $exe
Write-Host "[build] qminiwasm.exe OK: $($exeItem.FullName) size=$($exeItem.Length) LastWriteTime=$($exeItem.LastWriteTime.ToString('o'))" -ForegroundColor Green

# --- Phase 5: Intel oneAPI runtimes next to exe ---
Write-Host "[build] Phase 5: Deploy Intel runtime DLLs next to $exe" -ForegroundColor Cyan
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
