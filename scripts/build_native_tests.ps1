param(
    [string]$Target = "q_mini_wasm_v2_tests",
    [string]$BuildDir = "build_sycl",
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$nativeRoot = Join-Path $repoRoot "q_mini_wasm_v2"
$setvarsPath = "C:\Program Files (x86)\Intel\oneAPI\setvars.bat"
$vcvars64Path = "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path $setvarsPath)) {
    throw "[tests] Missing oneAPI setvars.bat: $setvarsPath"
}
if (-not (Test-Path $vcvars64Path)) {
    throw "[tests] Missing Visual Studio vcvars64.bat: $vcvars64Path"
}

$testExe = Join-Path $nativeRoot (Join-Path $BuildDir ($Target + ".exe"))
$cmd = @(
    "call `"$setvarsPath`" intel64",
    "call `"$vcvars64Path`"",
    "cd /d `"$nativeRoot`"",
    "cmake --build `"$BuildDir`" --target `"$Target`" -j 8"
)
if (-not $NoRun) {
    $cmd += "`"$testExe`""
}
$cmdLine = $cmd -join " && "

Write-Host "[tests] Repo root: $repoRoot" -ForegroundColor Cyan
Write-Host "[tests] Native root: $nativeRoot" -ForegroundColor Cyan
Write-Host "[tests] Target: $Target (build dir: $BuildDir)" -ForegroundColor Cyan
if ($NoRun) {
    Write-Host "[tests] NoRun enabled: build only" -ForegroundColor Yellow
}

& cmd /c $cmdLine
if ($LASTEXITCODE -ne 0) {
    throw "[tests] Failed: build/run for target '$Target'"
}

if ($NoRun) {
    Write-Host "[tests] Build complete: $Target" -ForegroundColor Green
} else {
    Write-Host "[tests] Build+run complete: $Target" -ForegroundColor Green
}
